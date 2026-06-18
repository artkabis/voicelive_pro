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

namespace {
const char* actionName(EngineCommand::Action action) {
    switch (action) {
        case EngineCommand::Action::Record:
            return "Record";
        case EngineCommand::Action::FinishRecording:
            return "Finish";
        case EngineCommand::Action::Play:
            return "Play";
        case EngineCommand::Action::Stop:
            return "Stop";
        case EngineCommand::Action::StartOverdub:
            return "Overdub";
        case EngineCommand::Action::StopOverdub:
            return "StopOverdub";
        case EngineCommand::Action::Clear:
            return "Clear";
        case EngineCommand::Action::SetGain:
            return "SetGain";
        case EngineCommand::Action::SetMuted:
            return "SetMuted";
        case EngineCommand::Action::SelectTrack:
            return "Select";
    }
    return "?";
}
}  // namespace

void MainComponent::AppLogger::logMessage(const juce::String& message) {
    {
        const juce::ScopedLock scoped(lock_);
        lines_.add(message);
        while (lines_.size() > 200) {
            lines_.remove(0);
        }
    }
    juce::Logger::outputDebugString(message);  // → logcat sur Android
}

juce::String MainComponent::AppLogger::snapshot() const {
    const juce::ScopedLock scoped(lock_);
    return lines_.joinIntoString("\n");
}

MainComponent::MainComponent() {
    // Capture tous les logs (les nôtres + JUCE) pour les rendre copiables.
    juce::Logger::setCurrentLogger(&appLogger_);
    juce::Logger::writeToLog("VoiceLive Pro démarré");

    // Viewport de défilement vertical : tout le contenu UI est dans contentPane_.
    viewport_.setScrollBarsShown(true, false);
    viewport_.setViewedComponent(&contentPane_, false);
    addAndMakeVisible(viewport_);

    // Demande explicite de la permission micro (indispensable à l'entrée audio).
    juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, [](bool granted) {
        juce::Logger::writeToLog(granted ? "Permission micro : ACCORDÉE"
                                         : "Permission micro : REFUSÉE");
    });

    titleLabel_.setText("VoiceLive Pro", juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centred);
    titleLabel_.setFont(juce::Font(juce::FontOptions{}.withHeight(22.0F)));
    contentPane_.addAndMakeVisible(titleLabel_);

    tunerLabel_.setText("Accordeur : —", juce::dontSendNotification);
    tunerLabel_.setJustificationType(juce::Justification::centred);
    contentPane_.addAndMakeVisible(tunerLabel_);

    for (std::size_t i = 0; i < kTrackCount; ++i) {
        setupTrackStrip(i);
    }

    // Transport : métronome + tempo.
    metronomeButton_.setButtonText("Métronome");
    metronomeButton_.onClick = [this] {
        engine_.setMetronomeEnabled(metronomeButton_.getToggleState());
    };
    contentPane_.addAndMakeVisible(metronomeButton_);
    bpmSlider_.setRange(40.0, 240.0, 1.0);
    bpmSlider_.setValue(120.0, juce::dontSendNotification);
    bpmSlider_.setTextValueSuffix(" BPM");
    bpmSlider_.onValueChange = [this] {
        engine_.transport().setBpm(Bpm::fromValue(bpmSlider_.getValue()));
    };
    contentPane_.addAndMakeVisible(bpmSlider_);

    // Mastering : égaliseur 3 bandes inséré une fois dans le bus master.
    masterLabel_.setText("Mastering — EQ 3 bandes", juce::dontSendNotification);
    contentPane_.addAndMakeVisible(masterLabel_);
    {
        auto equalizer = std::make_unique<voicelive::dsp::Equalizer>();
        masterEq_ = equalizer.get();
        engine_.masterEffects().add(std::move(equalizer));
    }
    const auto setupEq = [this](juce::Slider& slider, const juce::String& name) {
        slider.setRange(-12.0, 12.0, 0.5);
        slider.setValue(0.0, juce::dontSendNotification);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
        slider.setName(name);
        contentPane_.addAndMakeVisible(slider);
    };

    lowEqLabel_.setText("Grave", juce::dontSendNotification);
    lowEqLabel_.setJustificationType(juce::Justification::centredRight);
    contentPane_.addAndMakeVisible(lowEqLabel_);
    setupEq(lowEqSlider_, "Grave");
    lowEqSlider_.onValueChange = [this] {
        if (masterEq_ != nullptr) {
            masterEq_->setLowGain(static_cast<float>(lowEqSlider_.getValue()));
        }
    };

    midEqLabel_.setText("Médium", juce::dontSendNotification);
    midEqLabel_.setJustificationType(juce::Justification::centredRight);
    contentPane_.addAndMakeVisible(midEqLabel_);
    setupEq(midEqSlider_, "Médium");
    midEqSlider_.onValueChange = [this] {
        if (masterEq_ != nullptr) {
            masterEq_->setMidGain(static_cast<float>(midEqSlider_.getValue()));
        }
    };

    highEqLabel_.setText("Aigu", juce::dontSendNotification);
    highEqLabel_.setJustificationType(juce::Justification::centredRight);
    contentPane_.addAndMakeVisible(highEqLabel_);
    setupEq(highEqSlider_, "Aigu");
    highEqSlider_.onValueChange = [this] {
        if (masterEq_ != nullptr) {
            masterEq_->setHighGain(static_cast<float>(highEqSlider_.getValue()));
        }
    };

    // Panneau de diagnostic : rend l'app observable sur mobile (pas de console).
    diagView_.setMultiLine(true);
    diagView_.setReadOnly(true);
    diagView_.setCaretVisible(false);
    diagView_.setScrollbarsShown(true);
    diagView_.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0F)));
    contentPane_.addAndMakeVisible(diagView_);

    copyButton_.setButtonText("Copier le diagnostic");
    copyButton_.onClick = [this] {
        juce::SystemClipboard::copyTextToClipboard(diagView_.getText());
        juce::Logger::writeToLog("Diagnostic copié dans le presse-papier");
    };
    contentPane_.addAndMakeVisible(copyButton_);

    analysis_.assign(kAnalysisSize, 0.0F);
    // Taille initiale adaptée mobile (le système Android redimensionne au démarrage).
    setSize(400, 700);
    setAudioChannels(2, 2);
    startTimerHz(10);
}

MainComponent::~MainComponent() {
    stopTimer();
    shutdownAudio();
    juce::Logger::setCurrentLogger(nullptr);
}

void MainComponent::setupTrackStrip(std::size_t index) {
    TrackStrip& strip = strips_[index];
    strip.label.setText("Piste " + juce::String(static_cast<int>(index) + 1),
                        juce::dontSendNotification);
    contentPane_.addAndMakeVisible(strip.label);

    strip.recordButton.setButtonText("Rec");
    strip.recordButton.onClick = [this, index] { recordOrFinish(index); };
    contentPane_.addAndMakeVisible(strip.recordButton);

    strip.playButton.setButtonText("Play");
    strip.playButton.onClick = [this, index] {
        postCommand(EngineCommand::Action::Play, index, 1.0F, false);
    };
    contentPane_.addAndMakeVisible(strip.playButton);

    strip.stopButton.setButtonText("Stop");
    strip.stopButton.onClick = [this, index] {
        postCommand(EngineCommand::Action::Stop, index, 1.0F, false);
    };
    contentPane_.addAndMakeVisible(strip.stopButton);

    strip.clearButton.setButtonText("Clear");
    strip.clearButton.onClick = [this, index] {
        postCommand(EngineCommand::Action::Clear, index, 1.0F, false);
    };
    contentPane_.addAndMakeVisible(strip.clearButton);

    strip.gainSlider.setRange(0.0, 2.0, 0.01);
    strip.gainSlider.setValue(1.0, juce::dontSendNotification);
    strip.gainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    strip.gainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
    strip.gainSlider.onValueChange = [this, index] {
        postCommand(EngineCommand::Action::SetGain, index,
                    static_cast<float>(strips_[index].gainSlider.getValue()), false);
    };
    contentPane_.addAndMakeVisible(strip.gainSlider);

    strip.muteButton.setButtonText("Mute");
    strip.muteButton.onClick = [this, index] {
        postCommand(EngineCommand::Action::SetMuted, index, 1.0F,
                    strips_[index].muteButton.getToggleState());
    };
    contentPane_.addAndMakeVisible(strip.muteButton);
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    try {
        const auto blockSize = static_cast<std::size_t>(juce::jmax(1, samplesPerBlockExpected));
        const auto rate = static_cast<unsigned>(juce::jmax(1.0, sampleRate));
        const auto capacity = static_cast<std::size_t>(juce::jmax(1.0, sampleRate) * 30.0);

        if (const auto sr = SampleRate::create(rate); sr.ok()) {
            static_cast<void>(engine_.prepare(sr.value(), kTrackCount, capacity, blockSize));
            // Trace visible via `adb logcat` sur Android (sinon invisible sur mobile).
            juce::Logger::writeToLog("VoiceLive: audio prêt " + juce::String(rate) + " Hz, bloc " +
                                     juce::String(static_cast<int>(blockSize)));
        } else {
            juce::Logger::writeToLog("VoiceLive: ERREUR fréquence " + juce::String(rate));
        }
        monoIn_.assign(blockSize, 0.0F);
        monoOut_.assign(blockSize, 0.0F);
    } catch (const std::exception& e) {
        juce::Logger::writeToLog(juce::String("ERREUR prepareToPlay: ") + e.what());
    } catch (...) {
        juce::Logger::writeToLog("ERREUR inconnue dans prepareToPlay");
    }
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    if (bufferToFill.buffer == nullptr) {
        return;
    }
    juce::AudioBuffer<float>* buffer = bufferToFill.buffer;
    const auto numSamples = static_cast<std::size_t>(bufferToFill.numSamples);
    if (numSamples == 0 || numSamples > monoIn_.size()) {
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

    updateDiagnostics();
}

void MainComponent::updateDiagnostics() {
    juce::String text;
    text << "VoiceLive Pro v2.0.0  |  JUCE " << juce::SystemStats::getJUCEVersion() << "\n";
    text << "Build : " << __DATE__ << " " << __TIME__ << "\n";

    if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr) {
        text << "Audio : " << device->getName() << "  "
             << juce::String(device->getCurrentSampleRate(), 0) << " Hz / buffer "
             << device->getCurrentBufferSizeSamples() << "\n";
    } else {
        text << "Audio : NON DÉMARRÉ (vérifier les permissions micro)\n";
    }

    const auto diag = engine_.diagnostics();
    text << "Moteur : " << static_cast<int>(diag.trackCount) << " pistes, "
         << static_cast<int>(diag.blocksProcessed) << " blocs, "
         << static_cast<int>(diag.droppedCommands) << " cmd perdues, métronome "
         << (diag.metronomeEnabled ? "ON" : "OFF") << ", master FX "
         << static_cast<int>(diag.masterEffectCount) << "\n";

    for (std::size_t i = 0; i < diag.trackCount; ++i) {
        const auto* processor = engine_.track(i);
        if (processor == nullptr) {
            continue;
        }
        const auto& track = processor->track();
        text << "  Piste " << static_cast<int>(i) + 1 << " : "
             << voicelive::core::toString(track.state()) << "  gain "
             << juce::String(track.gain().linear(), 2) << (track.isMuted() ? "  [MUTE]" : "")
             << "\n";
    }

    text << "\n--- Journal (récent) ---\n" << appLogger_.snapshot();
    diagView_.setText(text, false);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized() {
    viewport_.setBounds(getLocalBounds());

    // Largeur utilisable = largeur du viewport moins la barre de défilement verticale.
    const int usableW = juce::jmax(300, viewport_.getMaximumVisibleWidth());
    const int pad = 12;

    // Hauteurs fixes pour chaque section (px).
    constexpr int kTitleH = 34;
    constexpr int kTunerH = 30;
    constexpr int kRowH = 44;               // hauteur d'une rangée de boutons/slider
    constexpr int kTrackH = kRowH * 2 + 6;  // 2 rangées par piste + séparation
    constexpr int kTransH = 48;
    constexpr int kEqLblH = 26;
    constexpr int kEqRowH = 44;
    constexpr int kCopyH = 40;
    constexpr int kDiagH = 220;
    constexpr int kGap = 10;

    const int trackCount = static_cast<int>(kTrackCount);
    const int totalH = pad + kTitleH + kGap / 2 + kTunerH + kGap +
                       trackCount * (kTrackH + kGap / 2) + kGap + kTransH + kGap + kEqLblH + 4 +
                       3 * (kEqRowH + 4) + kGap + kCopyH + kGap / 2 + kDiagH + pad;

    contentPane_.setSize(usableW, totalH);
    auto area = contentPane_.getLocalBounds().reduced(pad);

    // Titre et accordeur.
    titleLabel_.setBounds(area.removeFromTop(kTitleH));
    area.removeFromTop(kGap / 2);
    tunerLabel_.setBounds(area.removeFromTop(kTunerH));
    area.removeFromTop(kGap);

    // Pistes : 2 rangées chacune (boutons + gain/mute).
    const int w = area.getWidth();
    for (TrackStrip& strip : strips_) {
        auto trackArea = area.removeFromTop(kTrackH);

        // Rangée 1 : label + boutons Rec/Play/Stop/Clear.
        auto row1 = trackArea.removeFromTop(kRowH);
        const int labelW = 56;
        const int btnW = juce::jmax(42, (w - labelW) / 4);
        strip.label.setBounds(row1.removeFromLeft(labelW));
        strip.recordButton.setBounds(row1.removeFromLeft(btnW).reduced(2));
        strip.playButton.setBounds(row1.removeFromLeft(btnW).reduced(2));
        strip.stopButton.setBounds(row1.removeFromLeft(btnW).reduced(2));
        strip.clearButton.setBounds(row1.removeFromLeft(btnW).reduced(2));

        trackArea.removeFromTop(6);

        // Rangée 2 : slider gain + bouton Mute.
        auto row2 = trackArea.removeFromTop(kRowH);
        strip.muteButton.setBounds(row2.removeFromRight(64).reduced(2));
        strip.gainSlider.setBounds(row2.reduced(2));

        area.removeFromTop(kGap / 2);
    }

    // Transport : Métronome + BPM.
    area.removeFromTop(kGap);
    {
        auto row = area.removeFromTop(kTransH);
        metronomeButton_.setBounds(row.removeFromLeft(120).reduced(2));
        bpmSlider_.setBounds(row.reduced(4));
    }

    // Égaliseur master 3 bandes (sliders horizontaux + labels).
    area.removeFromTop(kGap);
    masterLabel_.setBounds(area.removeFromTop(kEqLblH));
    area.removeFromTop(4);

    const int eqLabelW = 68;
    const std::pair<juce::Slider*, juce::Label*> eqRows[3] = {
        {&lowEqSlider_, &lowEqLabel_},
        {&midEqSlider_, &midEqLabel_},
        {&highEqSlider_, &highEqLabel_},
    };
    for (auto [slider, label] : eqRows) {
        auto row = area.removeFromTop(kEqRowH);
        label->setBounds(row.removeFromLeft(eqLabelW));
        slider->setBounds(row.reduced(2));
        area.removeFromTop(4);
    }

    // Diagnostic : bouton copie puis panneau texte.
    area.removeFromTop(kGap);
    copyButton_.setBounds(area.removeFromTop(kCopyH).reduced(2));
    area.removeFromTop(kGap / 2);
    diagView_.setBounds(area.removeFromTop(kDiagH));
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
    const bool accepted = engine_.post(command);

    // Trace chaque clic : prouve que l'UI répond, même si l'audio est mort.
    juce::Logger::writeToLog(juce::String("Clic piste ") +
                             juce::String(static_cast<int>(track) + 1) + " : " +
                             actionName(action) + (accepted ? "" : "  (FILE PLEINE)"));
}
