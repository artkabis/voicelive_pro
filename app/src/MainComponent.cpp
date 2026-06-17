// SPDX-License-Identifier: MIT
#include "MainComponent.h"

#include <algorithm>
#include <array>
#include <memory>
#include <span>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/LooperTrack.hpp"
#include "voicelive/core/Music.hpp"
#include "voicelive/core/Transport.hpp"
#include "voicelive/engine/ChannelUtils.hpp"

namespace channels = voicelive::engine::channels;
namespace music = voicelive::core::music;
using voicelive::core::Bpm;
using voicelive::core::SampleRate;
using voicelive::core::TrackState;
using voicelive::engine::EngineCommand;

MainComponent::MainComponent() {
    titleLabel_.setText("VoiceLive Pro", juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centred);
    titleLabel_.setFont(juce::Font(juce::FontOptions{}.withHeight(22.0F)));
    addAndMakeVisible(titleLabel_);

    tunerLabel_.setText("Accordeur : —", juce::dontSendNotification);
    tunerLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(tunerLabel_);

    for (std::size_t i = 0; i < kTrackCount; ++i) {
        setupTrackStrip(i);
    }

    // Transport : métronome + tempo.
    transportLabel_.setText("Transport", juce::dontSendNotification);
    addAndMakeVisible(transportLabel_);
    metronomeButton_.setButtonText("Métronome");
    metronomeButton_.onClick = [this] {
        engine_.setMetronomeEnabled(metronomeButton_.getToggleState());
    };
    addAndMakeVisible(metronomeButton_);
    bpmSlider_.setRange(40.0, 240.0, 1.0);
    bpmSlider_.setValue(120.0, juce::dontSendNotification);
    bpmSlider_.setTextValueSuffix(" BPM");
    bpmSlider_.onValueChange = [this] {
        engine_.transport().setBpm(Bpm::fromValue(bpmSlider_.getValue()));
    };
    addAndMakeVisible(bpmSlider_);

    // Mastering : égaliseur 3 bandes inséré une fois dans le bus master.
    masterLabel_.setText("Mastering (EQ)", juce::dontSendNotification);
    addAndMakeVisible(masterLabel_);
    {
        auto equalizer = std::make_unique<voicelive::dsp::Equalizer>();
        masterEq_ = equalizer.get();
        engine_.masterEffects().add(std::move(equalizer));
    }
    const auto setupEq = [this](juce::Slider& slider, const juce::String& name) {
        slider.setRange(-12.0, 12.0, 0.5);
        slider.setValue(0.0, juce::dontSendNotification);
        slider.setSliderStyle(juce::Slider::LinearVertical);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        slider.setName(name);
        addAndMakeVisible(slider);
    };
    setupEq(lowEqSlider_, "Grave");
    setupEq(midEqSlider_, "Médium");
    setupEq(highEqSlider_, "Aigu");
    lowEqSlider_.onValueChange = [this] {
        if (masterEq_ != nullptr) {
            masterEq_->setLowGain(static_cast<float>(lowEqSlider_.getValue()));
        }
    };
    midEqSlider_.onValueChange = [this] {
        if (masterEq_ != nullptr) {
            masterEq_->setMidGain(static_cast<float>(midEqSlider_.getValue()));
        }
    };
    highEqSlider_.onValueChange = [this] {
        if (masterEq_ != nullptr) {
            masterEq_->setHighGain(static_cast<float>(highEqSlider_.getValue()));
        }
    };

    analysis_.assign(kAnalysisSize, 0.0F);
    setSize(760, 540);
    setAudioChannels(2, 2);
    startTimerHz(10);
}

MainComponent::~MainComponent() {
    stopTimer();
    shutdownAudio();
}

void MainComponent::setupTrackStrip(std::size_t index) {
    TrackStrip& strip = strips_[index];
    strip.label.setText("Piste " + juce::String(static_cast<int>(index) + 1),
                        juce::dontSendNotification);
    addAndMakeVisible(strip.label);

    strip.recordButton.setButtonText("Rec");
    strip.recordButton.onClick = [this, index] { recordOrFinish(index); };
    addAndMakeVisible(strip.recordButton);

    strip.playButton.setButtonText("Play");
    strip.playButton.onClick = [this, index] {
        postCommand(EngineCommand::Action::Play, index, 1.0F, false);
    };
    addAndMakeVisible(strip.playButton);

    strip.stopButton.setButtonText("Stop");
    strip.stopButton.onClick = [this, index] {
        postCommand(EngineCommand::Action::Stop, index, 1.0F, false);
    };
    addAndMakeVisible(strip.stopButton);

    strip.clearButton.setButtonText("Clear");
    strip.clearButton.onClick = [this, index] {
        postCommand(EngineCommand::Action::Clear, index, 1.0F, false);
    };
    addAndMakeVisible(strip.clearButton);

    strip.gainSlider.setRange(0.0, 2.0, 0.01);
    strip.gainSlider.setValue(1.0, juce::dontSendNotification);
    strip.gainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    strip.gainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
    strip.gainSlider.onValueChange = [this, index] {
        postCommand(EngineCommand::Action::SetGain, index,
                    static_cast<float>(strips_[index].gainSlider.getValue()), false);
    };
    addAndMakeVisible(strip.gainSlider);

    strip.muteButton.setButtonText("Mute");
    strip.muteButton.onClick = [this, index] {
        postCommand(EngineCommand::Action::SetMuted, index, 1.0F,
                    strips_[index].muteButton.getToggleState());
    };
    addAndMakeVisible(strip.muteButton);
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    const auto blockSize = static_cast<std::size_t>(juce::jmax(1, samplesPerBlockExpected));
    const auto rate = static_cast<unsigned>(juce::jmax(1.0, sampleRate));
    const auto capacity = static_cast<std::size_t>(juce::jmax(1.0, sampleRate) * 30.0);

    if (const auto sr = SampleRate::create(rate); sr.ok()) {
        static_cast<void>(engine_.prepare(sr.value(), kTrackCount, capacity, blockSize));
    }
    monoIn_.assign(blockSize, 0.0F);
    monoOut_.assign(blockSize, 0.0F);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    juce::AudioBuffer<float>* buffer = bufferToFill.buffer;
    const auto numSamples = static_cast<std::size_t>(bufferToFill.numSamples);
    if (numSamples > monoIn_.size()) {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    const int numChannels = juce::jmin(buffer->getNumChannels(), kMaxChannels);
    std::array<const float*, kMaxChannels> inputPtrs{};
    std::array<float*, kMaxChannels> outputPtrs{};
    for (int channel = 0; channel < numChannels; ++channel) {
        float* data = buffer->getWritePointer(channel, bufferToFill.startSample);
        outputPtrs[static_cast<std::size_t>(channel)] = data;
        inputPtrs[static_cast<std::size_t>(channel)] = data;
    }

    const std::span<float> monoIn{monoIn_.data(), numSamples};
    const std::span<float> monoOut{monoOut_.data(), numSamples};
    channels::downmixToMono(monoIn, inputPtrs.data(), static_cast<std::size_t>(numChannels));

    // Alimente la fenêtre d'analyse de l'accordeur (course bénigne avec le Timer).
    if (numSamples < analysis_.size()) {
        const std::size_t keep = analysis_.size() - numSamples;
        std::copy(analysis_.begin() + static_cast<std::ptrdiff_t>(numSamples), analysis_.end(),
                  analysis_.begin());
        std::copy(monoIn_.begin(), monoIn_.begin() + static_cast<std::ptrdiff_t>(numSamples),
                  analysis_.begin() + static_cast<std::ptrdiff_t>(keep));
    }

    engine_.process(monoOut, monoIn);
    channels::spreadToChannels(outputPtrs.data(), static_cast<std::size_t>(numChannels), monoOut);
}

void MainComponent::releaseResources() {}

void MainComponent::timerCallback() {
    if (analysis_.empty()) {
        return;
    }
    const auto note = engine_.tune(std::span<const float>{analysis_.data(), analysis_.size()});
    if (!note.has_value()) {
        tunerLabel_.setText("Accordeur : —", juce::dontSendNotification);
        return;
    }
    const int cents = juce::roundToInt(note->cents);
    const juce::String text = juce::String("Accordeur : ") + music::name(note->midi) +
                              juce::String(music::octave(note->midi)) + "   " +
                              (cents >= 0 ? "+" : "") + juce::String(cents) + " cents";
    tunerLabel_.setText(text, juce::dontSendNotification);
}

void MainComponent::resized() {
    auto area = getLocalBounds().reduced(12);
    titleLabel_.setBounds(area.removeFromTop(32));
    tunerLabel_.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);

    for (TrackStrip& strip : strips_) {
        auto row = area.removeFromTop(46);
        strip.label.setBounds(row.removeFromLeft(70));
        strip.recordButton.setBounds(row.removeFromLeft(58).reduced(2));
        strip.playButton.setBounds(row.removeFromLeft(58).reduced(2));
        strip.stopButton.setBounds(row.removeFromLeft(58).reduced(2));
        strip.clearButton.setBounds(row.removeFromLeft(58).reduced(2));
        strip.muteButton.setBounds(row.removeFromRight(70));
        strip.gainSlider.setBounds(row.reduced(4));
        area.removeFromTop(6);
    }

    area.removeFromTop(8);
    auto transportRow = area.removeFromTop(46);
    transportLabel_.setBounds(transportRow.removeFromLeft(90));
    metronomeButton_.setBounds(transportRow.removeFromLeft(110));
    bpmSlider_.setBounds(transportRow.reduced(4));

    area.removeFromTop(8);
    masterLabel_.setBounds(area.removeFromTop(24));
    auto eqRow = area;
    const int third = juce::jmax(1, eqRow.getWidth() / 3);
    lowEqSlider_.setBounds(eqRow.removeFromLeft(third).reduced(8));
    midEqSlider_.setBounds(eqRow.removeFromLeft(third).reduced(8));
    highEqSlider_.setBounds(eqRow.reduced(8));
}

void MainComponent::recordOrFinish(std::size_t index) {
    const auto* processor = engine_.track(index);
    if (processor != nullptr && processor->track().state() == TrackState::Recording) {
        postCommand(EngineCommand::Action::FinishRecording, index, 1.0F, false);
    } else {
        postCommand(EngineCommand::Action::Record, index, 1.0F, false);
    }
}

void MainComponent::postCommand(EngineCommand::Action action, std::size_t track, float gain,
                                bool muted) {
    EngineCommand command;
    command.action = action;
    command.track = track;
    command.gain = gain;
    command.muted = muted;
    static_cast<void>(engine_.post(command));
}
