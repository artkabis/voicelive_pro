// SPDX-License-Identifier: MIT
#include "MainComponent.h"

#include <array>
#include <span>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/LooperTrack.hpp"
#include "voicelive/engine/ChannelUtils.hpp"

namespace channels = voicelive::engine::channels;
using voicelive::core::SampleRate;
using voicelive::core::TrackState;
using voicelive::engine::EngineCommand;

MainComponent::MainComponent() {
    titleLabel_.setText("VoiceLive Pro - piste 1", juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel_);

    addAndMakeVisible(recordButton_);
    addAndMakeVisible(playButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(clearButton_);

    recordButton_.onClick = [this] { postRecordOrFinish(); };
    playButton_.onClick = [this] { post(EngineCommand::Action::Play); };
    stopButton_.onClick = [this] { post(EngineCommand::Action::Stop); };
    clearButton_.onClick = [this] { post(EngineCommand::Action::Clear); };

    setSize(440, 200);
    setAudioChannels(2, 2);  // 2 entrées, 2 sorties
}

MainComponent::~MainComponent() {
    shutdownAudio();
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    const auto blockSize = static_cast<std::size_t>(juce::jmax(1, samplesPerBlockExpected));
    const auto rate = static_cast<unsigned>(juce::jmax(1.0, sampleRate));
    const auto capacity =
        static_cast<std::size_t>(juce::jmax(1.0, sampleRate) * 30.0);  // 30 s/piste

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
        inputPtrs[static_cast<std::size_t>(channel)] = data;  // in/out partagent le buffer
    }

    const std::span<float> monoIn{monoIn_.data(), numSamples};
    const std::span<float> monoOut{monoOut_.data(), numSamples};
    channels::downmixToMono(monoIn, inputPtrs.data(), static_cast<std::size_t>(numChannels));
    engine_.process(monoOut, monoIn);
    channels::spreadToChannels(outputPtrs.data(), static_cast<std::size_t>(numChannels), monoOut);
}

void MainComponent::releaseResources() {}

void MainComponent::resized() {
    auto area = getLocalBounds().reduced(12);
    titleLabel_.setBounds(area.removeFromTop(40));
    area.removeFromTop(12);
    auto row = area.removeFromTop(44);
    const int width = row.getWidth() / 4;
    recordButton_.setBounds(row.removeFromLeft(width).reduced(4));
    playButton_.setBounds(row.removeFromLeft(width).reduced(4));
    stopButton_.setBounds(row.removeFromLeft(width).reduced(4));
    clearButton_.setBounds(row.reduced(4));
}

void MainComponent::postRecordOrFinish() {
    // Lecture d'état depuis le thread message (course bénigne sur un enum) :
    // suffisant pour décider quelle commande poster.
    const auto* processor = engine_.track(kActiveTrack);
    if (processor != nullptr && processor->track().state() == TrackState::Recording) {
        post(EngineCommand::Action::FinishRecording);
    } else {
        post(EngineCommand::Action::Record);
    }
}

void MainComponent::post(EngineCommand::Action action) {
    EngineCommand command;
    command.action = action;
    command.track = kActiveTrack;
    static_cast<void>(engine_.post(command));
}
