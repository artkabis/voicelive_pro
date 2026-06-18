// SPDX-License-Identifier: MIT
//
// MainComponent — pont JUCE ↔ moteur + UI multipiste.
//
// Le composant est la source audio (AudioAppComponent) et l'interface. Les
// actions de piste (record/play/stop/clear/gain/mute) passent par la file
// lock-free du moteur (appliquées sur le thread audio). Les réglages continus
// (BPM, métronome, égaliseur de mastering) sont écrits directement : course
// bénigne sur des scalaires, sans réallocation. L'accordeur est rafraîchi par
// un Timer qui lit une fenêtre d'analyse alimentée par le callback audio.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <cstddef>
#include <vector>

#include "voicelive/dsp/Equalizer.hpp"
#include "voicelive/engine/LooperEngine.hpp"

class MainComponent final : public juce::AudioAppComponent, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    static constexpr std::size_t kTrackCount = 3;
    static constexpr int kMaxChannels = 8;
    static constexpr std::size_t kAnalysisSize = 4096;

    /// Contrôles d'une piste.
    struct TrackStrip {
        juce::Label label;
        juce::TextButton recordButton;
        juce::TextButton playButton;
        juce::TextButton stopButton;
        juce::TextButton clearButton;
        juce::Slider gainSlider;
        juce::ToggleButton muteButton;
    };

    void setupTrackStrip(std::size_t index);
    void recordOrFinish(std::size_t index);
    void postCommand(voicelive::engine::EngineCommand::Action action, std::size_t track, float gain,
                     bool muted);
    void timerCallback() override;
    void updateDiagnostics();

    /// Journal thread-safe : capture tous les messages JUCE (et les nôtres) pour
    /// les afficher dans l'app et les copier (récupérables sans console).
    class AppLogger final : public juce::Logger {
    public:
        void logMessage(const juce::String& message) override;
        [[nodiscard]] juce::String snapshot() const;

    private:
        juce::CriticalSection lock_;
        juce::StringArray lines_;
    };

    AppLogger appLogger_;

    voicelive::engine::LooperEngine engine_;
    voicelive::dsp::Equalizer* masterEq_ = nullptr;  // possédé par la chaîne de mastering

    std::vector<float> monoIn_;
    std::vector<float> monoOut_;
    std::vector<float> analysis_;  // fenêtre glissante pour l'accordeur

    // Conteneur de défilement — contentPane_ avant viewport_ pour que viewport_
    // soit détruit en premier (évite use-after-free lors de la destruction).
    juce::Component contentPane_;
    juce::Viewport viewport_;

    juce::Label titleLabel_;
    juce::Label tunerLabel_;
    std::array<TrackStrip, kTrackCount> strips_;

    juce::ToggleButton metronomeButton_;
    juce::Slider bpmSlider_;

    juce::Label masterLabel_;
    juce::Label lowEqLabel_;
    juce::Label midEqLabel_;
    juce::Label highEqLabel_;
    juce::Slider lowEqSlider_;
    juce::Slider midEqSlider_;
    juce::Slider highEqSlider_;

    juce::TextEditor diagView_;    // panneau de diagnostic (observabilité mobile)
    juce::TextButton copyButton_;  // copie le diagnostic dans le presse-papier

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
