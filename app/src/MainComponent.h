// SPDX-License-Identifier: MIT
//
// MainComponent -- pont JUCE <-> moteur + UI multipiste.
//
// Le composant est la source audio (AudioAppComponent) et l'interface. Les
// actions de piste (record/play/stop/clear/gain/mute) passent par la file
// lock-free du moteur (appliquees sur le thread audio). Les reglages continus
// (BPM, metronome, EQ master, effets de piste) sont ecrits directement : course
// benigne sur des scalaires, sans reallocation.
//
// Fonctionnalites :
//   - Effets par piste : Reverb / Delay / Wah / Chorus (toggle + parametre)
//   - Selection sur la forme d'onde : toucher/glisser pour definir une region
//   - Edition : couper la selection, garder la selection (trim)
//   - Export : mix complet WAV, piste individuelle WAV
//   - Sauvegarde / chargement des reglages (ProjectSerializer)
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "HeadphoneMonitor.h"
#include "voicelive/dsp/BpmDetector.hpp"
#include "voicelive/dsp/Chorus.hpp"
#include "voicelive/dsp/Delay.hpp"
#include "voicelive/dsp/Distortion.hpp"
#include "voicelive/dsp/Equalizer.hpp"
#include "voicelive/dsp/Flanger.hpp"
#include "voicelive/dsp/NoiseGate.hpp"
#include "voicelive/dsp/Phaser.hpp"
#include "voicelive/dsp/Reverb.hpp"
#include "voicelive/dsp/Tremolo.hpp"
#include "voicelive/dsp/Wah.hpp"
#include "voicelive/engine/LoopAudio.hpp"
#include "voicelive/engine/LooperEngine.hpp"

class MainComponent final : public juce::AudioAppComponent,
                            private juce::Timer,
                            private juce::ChangeListener {
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

    /// Controles d'une piste.
    struct TrackStrip {
        juce::Label label;
        juce::TextButton recordButton;
        juce::TextButton playButton;
        juce::TextButton stopButton;
        juce::TextButton clearButton;
        juce::Slider gainSlider;
        juce::ToggleButton muteButton;
    };

    /// Mini-waveform d'une piste avec selection interactive, zoom et tete de lecture.
    struct TrackWaveform final : public juce::Component {
        void setAudio(const voicelive::engine::LoopAudio* audio) noexcept;
        void setPlayhead(std::size_t pos, std::size_t totalLen, double sr) noexcept;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;
        void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;
        [[nodiscard]] bool hasSelection() const noexcept;
        [[nodiscard]] std::pair<float, float> selectionNormalized() const noexcept;
        void clearSelection() noexcept;

        /// Appelee a chaque double-clic avec la position normalisee [0,1] dans la boucle.
        std::function<void(float normPos)> onSeek;

        /// Zoom programmatique : factor > 1 = avant, < 1 = arriere, centre sur la vue.
        void zoomBy(float factor) noexcept;

    private:
        /// Convertit une position pixel [0,w] en position normalisee [0,1] dans la boucle.
        [[nodiscard]] float pixelToNorm(float px) const noexcept;
        /// Convertit une position normalisee [0,1] en position pixel [0,w].
        [[nodiscard]] float normToPixel(float norm) const noexcept;

        const voicelive::engine::LoopAudio* audio_ = nullptr;
        float selStart_ = 0.0F;
        float selEnd_ = 0.0F;
        bool selActive_ = false;
        // Fenetre de zoom : portion de la boucle affichee ([0,1] = pas de zoom).
        float viewStart_ = 0.0F;
        float viewEnd_ = 1.0F;
        // Peak cache: rebuilt when loopLength, width, or zoom window changes.
        mutable std::vector<float> peakCache_;
        mutable std::size_t cachedLoopLength_ = 0;
        mutable int cachedWidth_ = 0;
        mutable float cachedViewStart_ = -1.0F;
        mutable float cachedViewEnd_ = -1.0F;
        // Playhead (message thread only)
        std::size_t playheadPos_ = 0;
        std::size_t playheadLen_ = 0;
        double playheadSr_ = 48000.0;
    };

    /// Spectre de frequences temps-reel (FFT Cooley-Tukey 512 points).
    struct SpectrumView final : public juce::Component {
        static constexpr int kFftSize = 512;
        void update(std::span<const float> analysis);
        void paint(juce::Graphics& g) override;

    private:
        std::array<float, kFftSize / 2> smoothed_{};
        bool hasData_ = false;
    };

    /// Panneau d'effets par piste : un toggle + un parametre par effet.
    /// Les pointeurs raw sont possedes par l'EffectChain correspondante.
    struct TrackFxPanel {
        juce::TextButton reverbBtn, delayBtn, wahBtn, chorusBtn;
        juce::TextButton distBtn, gateBtn, tremBtn, phaserBtn, flangerBtn;
        juce::Slider reverbWetSlider;    ///< wet level  [0, 1]
        juce::Slider delayTimeSlider;    ///< delay time [0.05, 1.0] s
        juce::Slider wahRateSlider;      ///< LFO rate   [0.1, 5.0] Hz
        juce::Slider chorusDepthSlider;  ///< modulation depth [0, 1]
        juce::Slider distDriveSlider;    ///< drive      [1, 30]
        juce::Slider gateThreshSlider;   ///< threshold  [-80, 0] dB
        juce::Slider tremRateSlider;     ///< LFO rate   [0.5, 12] Hz
        juce::Slider phaserRateSlider;   ///< sweep rate [0.05, 4] Hz
        juce::Slider flangerRateSlider;  ///< sweep rate [0.05, 4] Hz
        voicelive::dsp::Reverb* reverb = nullptr;
        voicelive::dsp::Delay* delay = nullptr;
        voicelive::dsp::Wah* wah = nullptr;
        voicelive::dsp::Chorus* chorus = nullptr;
        voicelive::dsp::Distortion* distortion = nullptr;
        voicelive::dsp::NoiseGate* gate = nullptr;
        voicelive::dsp::Tremolo* tremolo = nullptr;
        voicelive::dsp::Phaser* phaser = nullptr;
        voicelive::dsp::Flanger* flanger = nullptr;
    };

    void setupTrackStrip(std::size_t index);
    void setupFxPanel(std::size_t index);
    void setupEffects();
    void recordOrFinish(std::size_t index);
    void postCommand(voicelive::engine::EngineCommand::Action action, std::size_t track, float gain,
                     bool muted);
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void updateDiagnostics();
    void refreshDeviceList();     ///< met a jour les listes de peripheriques (sortie/entree)
    void applyDeviceSelection();  ///< applique le choix via setAudioDeviceSetup (routage Oboe)

    // --- Transport global ---------------------------------------------------
    /// Joue toutes les pistes ayant du contenu depuis le debut (synchronise).
    void playAllTracks();
    /// Met en pause toutes les pistes en cours de lecture (conserve le playhead).
    void pauseAllTracks();
    /// Arrete toutes les pistes ayant du contenu.
    void stopAllTracks();

    // --- Edition de piste ----------------------------------------------------
    void cutSelection(std::size_t index);
    void trimToSelection(std::size_t index);
    void applyTrackEdit(std::size_t index, std::vector<float> newSamples);
    void checkPendingEdit();

    // --- Mix Master (piste de rendu editable) --------------------------------
    void renderMixToTrack();
    void cutMixSelection();
    void trimMixSelection();

    // --- Export / Sauvegarde -------------------------------------------------
    void exportMix();
    void exportTrack(std::size_t index);
    void saveProject();
    void loadProject();

    /// Journal thread-safe : capture tous les messages JUCE pour l'app mobile.
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
    voicelive::dsp::Equalizer* masterEq_ = nullptr;  // possede par masterChain_

    voicelive::app::HeadphoneMonitor headphoneMonitor_;
    voicelive::app::HeadphoneLed headphoneLed_;

    std::vector<float> monoIn_;
    std::vector<float> monoOut_;
    std::vector<float> analysis_;

    std::atomic<bool> anyTrackRecording_{false};
    // Indicateurs de diagnostic (ecrits par le thread audio, lus par l'UI). Tres
    // bon marche : un store relaxe par bloc, aucune allocation.
    std::atomic<float> inputLevel_{0.0F};  ///< crete |monoIn| du dernier bloc capture
    std::atomic<int> lastBlockSize_{0};    ///< taille de bloc reellement vue au callback

    // Watchdog audio (thread UI only) : detecte un callback audio fige (ex. echec
    // de reroutage Oboe au branchement USB-C) et relance le peripherique. La relance
    // est non destructive : prepareToPlay() conserve les pistes (cf. reconfigure()).
    std::uint64_t lastSeenBlocks_ = 0;  ///< dernier compteur de blocs moteur observe
    int audioStaleTicks_ = 0;           ///< ticks consecutifs sans nouveau bloc
    int restartCooldownTicks_ = 0;      ///< ticks restants avant relance autorisee
    bool audioWasAlive_ = false;        ///< vrai des qu'au moins un bloc a ete vu
    int watchdogStartupAttempts_ = 0;   ///< tentatives de demarrage echouees (cas 2)

    double sampleRate_ = 48000.0;
    bool effectsSetup_ = false;
    int timerTickCount_ = 0;
    std::array<std::size_t, kTrackCount> lastWaveformLength_{};
    std::size_t lastMixWavePos_ = 0;

    // Mix preview player (AudioTransportSource gere la thread-safety audio/UI).
    juce::AudioBuffer<float> mixAudioBuffer_;
    std::unique_ptr<juce::MemoryAudioSource> mixMemorySource_;
    juce::AudioTransportSource mixTransport_;
    juce::AudioBuffer<float> mixTmpBuf_;  ///< tampon pre-alloue pour le callback audio

    /// Edition en attente : appliquee des que la piste atteint Stopped/Empty.
    struct PendingEdit {
        std::size_t trackIndex = 0;
        std::vector<float> newSamples;
    };
    std::optional<PendingEdit> pendingEdit_;

    // Conteneur de defilement -- contentPane_ avant viewport_ pour que viewport_
    // soit detruit en premier (evite use-after-free lors de la destruction).
    juce::Component contentPane_;
    juce::Viewport viewport_;

    juce::Label titleLabel_;
    juce::Label tunerLabel_;
    juce::ToggleButton tunerActiveButton_;

    std::array<TrackStrip, kTrackCount> strips_;
    std::array<TrackWaveform, kTrackCount> waveforms_;
    std::array<TrackFxPanel, kTrackCount> fxPanels_;

    std::array<juce::TextButton, kTrackCount> cutBtns_;
    std::array<juce::TextButton, kTrackCount> trimBtns_;
    std::array<juce::TextButton, kTrackCount> exportTrackBtns_;
    std::array<juce::ToggleButton, kTrackCount> includeBtns_;  ///< Include track in mix export.
    std::array<juce::TextButton, kTrackCount> bpmSyncBtns_;
    std::array<juce::TextButton, kTrackCount> zoomInBtns_;   ///< Zoom avant sur la waveform.
    std::array<juce::TextButton, kTrackCount> zoomOutBtns_;  ///< Zoom arriere / reset.

    voicelive::dsp::BpmDetector bpmDetector_;
    void detectAndSyncBpm(std::size_t trackIndex);

    juce::TextButton globalPlayBtn_;   ///< Joue toutes les pistes depuis le debut.
    juce::TextButton globalPauseBtn_;  ///< Met en pause toutes les pistes.
    juce::TextButton globalStopBtn_;   ///< Arrete toutes les pistes.

    juce::ToggleButton metronomeButton_;
    juce::Slider bpmSlider_;

    juce::Label masterLabel_;
    juce::Label lowEqLabel_;
    juce::Label midEqLabel_;
    juce::Label highEqLabel_;
    juce::Slider lowEqSlider_;
    juce::Slider midEqSlider_;
    juce::Slider highEqSlider_;

    SpectrumView spectrumView_;

    // Mix Master : piste de rendu editable (UI-thread only, jamais sur l'audio thread).
    voicelive::engine::LoopAudio mixTrackAudio_;
    TrackWaveform mixWaveform_;
    juce::Label mixLabel_;
    juce::TextButton renderMixBtn_;
    juce::TextButton mixPlayBtn_;
    juce::TextButton mixPauseBtn_;
    juce::TextButton mixStopBtn_;
    juce::Label mixTimeLabel_;
    juce::TextButton cutMixBtn_;
    juce::TextButton trimMixBtn_;
    juce::TextButton exportMixBtn_;  ///< Export mix depuis mixTrackAudio_.

    // Selection explicite du peripherique audio. JUCE 8 expose la liste physique
    // (nom produit + type) via OboeAudioIODeviceType::getDeviceNames() et route
    // reellement le flux via builder.setDeviceId() : aucun hack natif necessaire.
    juce::Label audioDevLabel_;
    juce::Label outputDevLabel_;
    juce::Label inputDevLabel_;
    juce::ComboBox outputDeviceBox_;
    juce::ComboBox inputDeviceBox_;

    juce::Label ioLabel_;
    juce::TextButton saveProjectBtn_;
    juce::TextButton loadProjectBtn_;

    // logViewport_ doit etre declare AVANT diagView_ : la destruction C++ se fait
    // en ordre inverse, donc diagView_ est detruit en premier, ce qui le detache
    // proprement de logViewport_ avant que logViewport_ ne soit lui-meme detruit.
    juce::Viewport logViewport_;
    juce::TextEditor diagView_;
    juce::TextButton copyButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
