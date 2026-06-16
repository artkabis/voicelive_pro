// SPDX-License-Identifier: MIT
//
// LooperEngine — assemblage de haut niveau du moteur temps réel.
//
// Possède N pistes (`TrackProcessor`), le `Transport` partagé, et une file de
// commandes lock-free (`RingBuffer`) alimentée par l'UI et vidée par le thread
// audio au début de chaque `process()`. Rend le mix complet (somme des pistes +
// limiteur) en une passe sans allocation.
//
// Réconciliation avec `core::Project` : le moteur est l'autorité d'exécution ;
// `Project` est le modèle de RÉGLAGES persistés (nom, transport, gain/mute,
// piste sélectionnée). `exportSettings()` / `applySettings()` font le pont.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/Project.hpp"
#include "voicelive/core/Result.hpp"
#include "voicelive/core/Transport.hpp"
#include "voicelive/engine/RingBuffer.hpp"
#include "voicelive/engine/TrackProcessor.hpp"

namespace voicelive::engine {

/// Commande de contrôle, transmissible via la file lock-free.
struct EngineCommand {
    enum class Action : std::uint8_t {
        Record,
        FinishRecording,
        Play,
        Stop,
        StartOverdub,
        StopOverdub,
        Clear,
        SetGain,
        SetMuted,
        SelectTrack,
    };

    Action action = Action::Stop;
    std::size_t track = 0;
    float gain = 1.0F;   ///< Pour SetGain (linéaire).
    bool muted = false;  ///< Pour SetMuted.
};

class LooperEngine {
public:
    static constexpr std::size_t kMaxTracks = core::Project::kMaxTracks;

    /// Configure le moteur (hors temps réel). Valide le nombre de pistes.
    core::Status prepare(core::SampleRate sampleRate, std::size_t trackCount,
                         std::size_t loopCapacity, std::size_t maxBlockSize);

    [[nodiscard]] std::size_t trackCount() const noexcept { return tracks_.size(); }
    [[nodiscard]] const core::Transport& transport() const noexcept { return transport_; }
    [[nodiscard]] core::Transport& transport() noexcept { return transport_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void setName(std::string name) { name_ = std::move(name); }
    [[nodiscard]] std::size_t selectedIndex() const noexcept { return selected_; }

    /// Accès borné à une piste (jamais d'UB).
    [[nodiscard]] const TrackProcessor* track(std::size_t index) const noexcept {
        return index < tracks_.size() ? &tracks_[index] : nullptr;
    }

    /// Chaîne d'effets d'une piste (pour insérer des effets ; hors temps réel).
    [[nodiscard]] dsp::EffectChain* effectsForTrack(std::size_t index) noexcept {
        return index < tracks_.size() ? &tracks_[index].effects() : nullptr;
    }

    // --- Contrôle synchrone validé (renvoie Status) -----------------------
    core::Status recordTrack(std::size_t i) { return applyCommand({Cmd::Record, i}); }
    core::Status finishRecordingTrack(std::size_t i) {
        return applyCommand({Cmd::FinishRecording, i});
    }
    core::Status playTrack(std::size_t i) { return applyCommand({Cmd::Play, i}); }
    core::Status stopTrack(std::size_t i) { return applyCommand({Cmd::Stop, i}); }
    core::Status overdubTrack(std::size_t i) { return applyCommand({Cmd::StartOverdub, i}); }
    core::Status stopOverdubTrack(std::size_t i) { return applyCommand({Cmd::StopOverdub, i}); }
    core::Status clearTrack(std::size_t i) { return applyCommand({Cmd::Clear, i}); }
    core::Status setTrackGain(std::size_t i, core::Gain gain) {
        return applyCommand({Cmd::SetGain, i, gain.linear()});
    }
    core::Status setTrackMuted(std::size_t i, bool muted) {
        EngineCommand command{Cmd::SetMuted, i};
        command.muted = muted;
        return applyCommand(command);
    }
    core::Status selectTrack(std::size_t i) { return applyCommand({Cmd::SelectTrack, i}); }

    /// Dépose une commande pour le thread audio (lock-free). false si plein.
    bool post(const EngineCommand& command) noexcept { return commands_.push(command); }

    /// Vide la file de commandes puis rend le mix complet. Temps réel.
    void process(std::span<float> output, std::span<const float> input) noexcept;

    // --- Pont avec core::Project (réglages persistés) ---------------------
    [[nodiscard]] core::Result<core::Project> exportSettings() const;
    core::Status applySettings(const core::Project& project);

private:
    using Cmd = EngineCommand::Action;

    core::Status applyCommand(const EngineCommand& command);

    std::vector<TrackProcessor> tracks_;
    core::Transport transport_;
    std::string name_;
    std::size_t selected_ = 0;
    std::vector<float> scratch_;  // buffer de rendu par piste (taille maxBlock)
    RingBuffer<EngineCommand> commands_{64};
};

}  // namespace voicelive::engine
