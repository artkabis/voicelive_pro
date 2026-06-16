// SPDX-License-Identifier: MIT
//
// MainComponent — pont JUCE ↔ moteur + UI minimale (transport piste 1).
//
// Le composant est à la fois la source audio (AudioAppComponent) et l'interface.
// La conversion canaux ↔ mono est déléguée à engine::channels (testée). Les
// boutons déposent des commandes dans la file lock-free du moteur ; le thread
// audio les applique au début de chaque bloc.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <vector>

#include "voicelive/engine/LooperEngine.hpp"

class MainComponent final : public juce::AudioAppComponent {
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void resized() override;

private:
    void postRecordOrFinish();
    void post(voicelive::engine::EngineCommand::Action action);

    static constexpr std::size_t kTrackCount = 3;
    static constexpr std::size_t kActiveTrack = 0;
    static constexpr int kMaxChannels = 8;

    voicelive::engine::LooperEngine engine_;
    std::vector<float> monoIn_;
    std::vector<float> monoOut_;

    juce::Label titleLabel_;
    juce::TextButton recordButton_{"Record"};
    juce::TextButton playButton_{"Play"};
    juce::TextButton stopButton_{"Stop"};
    juce::TextButton clearButton_{"Clear"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
