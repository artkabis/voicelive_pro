// SPDX-License-Identifier: MIT
//
// TrackProcessor — exécution temps réel d'une piste.
//
// Réunit l'état métier (`core::LooperTrack`, qui interdit les transitions
// invalides) et les données audio (`LoopAudio`), plus une tête de lecture. Les
// méthodes de transition encapsulent l'état ET l'audio pour qu'ils restent
// toujours cohérents (ex. : démarrer un enregistrement vide la boucle et remet
// la tête de lecture à zéro).
#pragma once

#include <algorithm>
#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/LooperTrack.hpp"
#include "voicelive/core/Result.hpp"
#include "voicelive/dsp/EffectChain.hpp"
#include "voicelive/engine/LoopAudio.hpp"

namespace voicelive::engine {

class TrackProcessor {
public:
    static constexpr std::size_t kDefaultMaxBlock = 4096;

    /// Alloue la capacité de boucle et prépare la chaîne d'effets (hors RT).
    void prepare(core::SampleRate sampleRate, std::size_t capacitySamples,
                 std::size_t maxBlockSize) {
        audio_.prepare(capacitySamples);
        effects_.prepare(sampleRate, maxBlockSize);
    }

    /// Surcharge de commodité (fréquence studio, bloc max par défaut).
    void prepare(std::size_t capacitySamples) {
        prepare(core::SampleRate::studio(), capacitySamples, kDefaultMaxBlock);
    }

    [[nodiscard]] const core::LooperTrack& track() const noexcept { return track_; }
    [[nodiscard]] const LoopAudio& audio() const noexcept { return audio_; }
    [[nodiscard]] std::size_t playhead() const noexcept { return playhead_; }

    /// Chaîne d'effets de la piste (ajout d'effets hors temps réel).
    [[nodiscard]] dsp::EffectChain& effects() noexcept { return effects_; }
    [[nodiscard]] const dsp::EffectChain& effects() const noexcept { return effects_; }

    void setGain(core::Gain gain) noexcept { track_.setGain(gain); }
    void setMuted(bool muted) noexcept { track_.setMuted(muted); }

    /// Force la période de boucle (synchronisation sur la piste maître).
    void setLoopLength(std::size_t length) noexcept { audio_.setLoopLength(length); }

    // --- Transitions (état + audio maintenus cohérents) -------------------
    core::Status startRecording() {
        const core::Status status = track_.record();
        if (status.ok()) {
            audio_.clear();
            playhead_ = 0;
        }
        return status;
    }
    core::Status finishRecording() {
        const core::Status status = track_.finishRecording();
        if (status.ok()) {
            playhead_ = 0;
        }
        return status;
    }
    core::Status play() {
        const core::Status status = track_.play();
        if (status.ok()) {
            playhead_ = 0;
        }
        return status;
    }
    core::Status stop() { return track_.stop(); }
    core::Status startOverdub() { return track_.startOverdub(); }  // garde la tête de lecture
    core::Status stopOverdub() { return track_.stopOverdub(); }
    void clearTrack() noexcept {
        track_.clear();
        audio_.clear();
        playhead_ = 0;
    }

    /// Rend un bloc de sortie mono à partir de l'entrée mono, selon l'état.
    /// Temps réel : aucune allocation.
    void process(std::span<float> output, std::span<const float> input) noexcept {
        switch (track_.state()) {
            case core::TrackState::Recording:
                audio_.append(input);
                monitorInput(output, input);
                break;
            case core::TrackState::Playing:
                playhead_ = audio_.readLooped(output, playhead_);
                effects_.process(output);
                applyGainAndMute(output);
                break;
            case core::TrackState::Overdubbing: {
                const std::size_t startPos = playhead_;
                playhead_ = audio_.readLooped(output, startPos);
                audio_.overdub(input, startPos);
                effects_.process(output);
                applyGainAndMute(output);
                break;
            }
            case core::TrackState::Empty:
            case core::TrackState::Stopped:
                std::fill(output.begin(), output.end(), 0.0F);
                break;
        }
    }

private:
    static void monitorInput(std::span<float> output, std::span<const float> input) noexcept {
        const std::size_t count = std::min(output.size(), input.size());
        for (std::size_t i = 0; i < count; ++i) {
            output[i] = input[i];
        }
        for (std::size_t i = count; i < output.size(); ++i) {
            output[i] = 0.0F;
        }
    }

    void applyGainAndMute(std::span<float> output) const noexcept {
        if (track_.isMuted()) {
            std::fill(output.begin(), output.end(), 0.0F);
            return;
        }
        const float gain = track_.gain().linear();
        for (float& sample : output) {
            sample *= gain;
        }
    }

    core::LooperTrack track_;
    LoopAudio audio_;
    dsp::EffectChain effects_;
    std::size_t playhead_ = 0;
};

}  // namespace voicelive::engine
