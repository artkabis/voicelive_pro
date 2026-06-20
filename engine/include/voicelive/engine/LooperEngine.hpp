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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/Music.hpp"
#include "voicelive/core/Project.hpp"
#include "voicelive/core/Result.hpp"
#include "voicelive/core/Transport.hpp"
#include "voicelive/dsp/EffectChain.hpp"
#include "voicelive/dsp/PitchDetector.hpp"
#include "voicelive/engine/Metronome.hpp"
#include "voicelive/engine/RingBuffer.hpp"
#include "voicelive/engine/TrackProcessor.hpp"
#include "voicelive/engine/WavFile.hpp"

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
        Seek,  ///< Deplace la tete de lecture : position en echantillons (champ position).
    };

    Action action = Action::Stop;
    std::size_t track = 0;
    float gain = 1.0F;          ///< Pour SetGain (lineaire).
    bool muted = false;         ///< Pour SetMuted.
    std::size_t position = 0;   ///< Pour Seek (en echantillons).
};

/// Instantané d'état du moteur, pour le diagnostic (panneau Diag de l'app).
struct Diagnostics {
    unsigned sampleRate = 0;
    std::size_t trackCount = 0;
    std::uint64_t blocksProcessed = 0;
    std::uint32_t droppedCommands = 0;  // file pleine : commandes UI perdues
    bool metronomeEnabled = false;
    std::size_t masterEffectCount = 0;
};

class LooperEngine {
public:
    static constexpr std::size_t kMaxTracks = core::Project::kMaxTracks;

    /// Configure le moteur (hors temps réel). Valide le nombre de pistes.
    core::Status prepare(core::SampleRate sampleRate, std::size_t trackCount,
                         std::size_t loopCapacity, std::size_t maxBlockSize);

    /// Re-prépare UNIQUEMENT les tampons de traitement (chaînes d'effets, scratch,
    /// métronome, master) pour une nouvelle taille de bloc, SANS toucher au contenu
    /// enregistré des pistes ni à leur état. À utiliser lors d'un simple changement
    /// de périphérique audio (rebranchement casque USB-C) où la configuration de base
    /// est inchangée : permet de relancer l'audio sans perdre les boucles. Les
    /// pointeurs d'effets déjà distribués (UI) restent valides. Hors temps réel.
    /// Échoue si le moteur n'a jamais été préparé (appeler prepare() d'abord).
    core::Status reconfigure(core::SampleRate sampleRate, std::size_t maxBlockSize);

    [[nodiscard]] std::size_t trackCount() const noexcept { return tracks_.size(); }
    [[nodiscard]] const core::Transport& transport() const noexcept { return transport_; }
    [[nodiscard]] core::Transport& transport() noexcept { return transport_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void setName(std::string name) { name_ = std::move(name); }
    [[nodiscard]] std::size_t selectedIndex() const noexcept { return selected_; }

    /// Longueur de la boucle maître (0 = aucune piste n'a encore défini la
    /// référence). Les pistes suivantes s'alignent sur un multiple de celle-ci.
    [[nodiscard]] std::size_t masterLoopLength() const noexcept { return masterLength_; }

    /// Accès borné à une piste (jamais d'UB).
    [[nodiscard]] const TrackProcessor* track(std::size_t index) const noexcept {
        return index < tracks_.size() ? &tracks_[index] : nullptr;
    }

    /// Chaîne d'effets d'une piste (pour insérer des effets ; hors temps réel).
    [[nodiscard]] dsp::EffectChain* effectsForTrack(std::size_t index) noexcept {
        return index < tracks_.size() ? &tracks_[index].effects() : nullptr;
    }

    // --- Import audio (charger un sample dans une piste) ------------------
    /// Charge des données audio (downmixées en mono) dans une piste, qui passe
    /// en lecture. Hors temps réel.
    core::Status importTrack(std::size_t index, const wav::AudioData& audio);

    /// Charge un fichier WAV dans une piste.
    core::Status importTrackFromFile(std::size_t index, const std::string& path);

    // --- Mastering (effets sur le mix final) -----------------------------
    /// Chaîne d'effets appliquée au mix complet (compresseur, égaliseur…).
    [[nodiscard]] dsp::EffectChain& masterEffects() noexcept { return masterChain_; }

    // --- Accordeur -------------------------------------------------------
    /// Estime la note jouée dans un bloc d'entrée (pour l'accordeur).
    [[nodiscard]] std::optional<core::music::Note> tune(std::span<const float> input) const;

    // --- Export audio (rendu du mix) -------------------------------------
    /// Rend `frames` échantillons du mix (mono) depuis le début des boucles,
    /// métronome exclu. N'altère pas l'état de lecture courant. Hors temps réel.
    [[nodiscard]] wav::AudioData renderMix(std::size_t frames);

    /// Rend le mix et l'écrit dans un fichier WAV.
    core::Status exportMixToFile(const std::string& path, std::size_t frames);

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

    // --- Métronome --------------------------------------------------------
    [[nodiscard]] bool isMetronomeEnabled() const noexcept { return metronome_.isEnabled(); }
    void setMetronomeEnabled(bool enabled) noexcept { metronome_.setEnabled(enabled); }
    void setMetronomeGain(core::Gain gain) noexcept { metronome_.setGain(gain); }

    /// Dépose une commande pour le thread audio (lock-free). false si plein
    /// (la commande est alors comptabilisée comme perdue, cf. diagnostics()).
    bool post(const EngineCommand& command) noexcept {
        if (commands_.push(command)) {
            return true;
        }
        droppedCommands_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    /// Instantané d'état pour le diagnostic (lecture sûre depuis le thread UI).
    [[nodiscard]] Diagnostics diagnostics() const noexcept {
        Diagnostics diag;
        diag.sampleRate = transport_.sampleRate().hz();
        diag.trackCount = tracks_.size();
        diag.blocksProcessed = blocksProcessed_.load(std::memory_order_relaxed);
        diag.droppedCommands = droppedCommands_.load(std::memory_order_relaxed);
        diag.metronomeEnabled = metronome_.isEnabled();
        diag.masterEffectCount = masterChain_.size();
        return diag;
    }

    /// Vide la file de commandes puis rend le mix complet. Temps réel.
    void process(std::span<float> output, std::span<const float> input) noexcept;

    // --- Pont avec core::Project (réglages persistés) ---------------------
    [[nodiscard]] core::Result<core::Project> exportSettings() const;
    core::Status applySettings(const core::Project& project);

private:
    using Cmd = EngineCommand::Action;

    core::Status applyCommand(const EngineCommand& command);

    /// Fixe ou aligne la longueur de boucle d'une piste qui vient d'être
    /// enregistrée : la 1ʳᵉ définit la référence, les suivantes s'y alignent.
    void alignTrackLoop(TrackProcessor& processor);

    std::vector<TrackProcessor> tracks_;
    core::Transport transport_;
    Metronome metronome_;
    dsp::EffectChain masterChain_;
    dsp::PitchDetector tuner_;
    std::string name_;
    std::size_t selected_ = 0;
    std::size_t masterLength_ = 0;  // longueur de la boucle maître (0 = aucune)
    std::vector<float> scratch_;    // buffer de rendu par piste (taille maxBlock)
    RingBuffer<EngineCommand> commands_{64};
    std::atomic<std::uint64_t> blocksProcessed_{0};
    std::atomic<std::uint32_t> droppedCommands_{0};
};

}  // namespace voicelive::engine
