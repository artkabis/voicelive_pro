// SPDX-License-Identifier: MIT
//
// LooperTrack — machine à états finie d'une piste de looper.
//
// C'est le cœur de la logique métier « qu'on ne peut pas contourner » : toutes
// les transitions passent par des méthodes gardées. Il est *impossible*
// d'atteindre un état incohérent (ex. lire une piste vide, faire un overdub
// sur une piste en enregistrement). Toute transition interdite renvoie un
// `Status` en erreur plutôt que de muter l'état.
//
// Diagramme des transitions autorisées :
//
//     Empty ──record──▶ Recording ──finishRecording──▶ Playing
//       ▲                   │                            │  ▲
//       │                   └──────── stop ──────────▶ Stopped
//       │                                                │  │
//       └──────────────── clear (depuis tout état) ──────┘  │
//                                                            │
//     Playing ──startOverdub──▶ Overdubbing ──stopOverdub──▶┘
//     Playing ──stop──▶ Stopped ──play──▶ Playing
#pragma once

#include <cstdint>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/Result.hpp"

namespace voicelive::core {

/// États possibles d'une piste. Le passage de l'un à l'autre est strictement
/// contrôlé par `LooperTrack`.
enum class TrackState : std::uint8_t {
    Empty,        ///< Aucun contenu enregistré.
    Recording,    ///< Capture de la boucle initiale en cours.
    Playing,      ///< Lecture en boucle du contenu.
    Overdubbing,  ///< Lecture + superposition d'une nouvelle couche.
    Stopped,      ///< Contenu présent, lecture arrêtée.
};

/// Représentation textuelle stable d'un état (journalisation, UI, tests).
const char* toString(TrackState state) noexcept;

class LooperTrack {
public:
    LooperTrack() = default;

    [[nodiscard]] TrackState state() const noexcept { return state_; }

    /// Vrai dès qu'une boucle a été capturée (états autres que `Empty`).
    [[nodiscard]] bool hasContent() const noexcept { return state_ != TrackState::Empty; }

    /// Vrai si la piste produit du son en sortie (Playing/Overdubbing).
    [[nodiscard]] bool isAudible() const noexcept {
        return state_ == TrackState::Playing || state_ == TrackState::Overdubbing;
    }

    [[nodiscard]] Gain gain() const noexcept { return gain_; }
    [[nodiscard]] bool isMuted() const noexcept { return muted_; }

    // --- Transitions (toutes gardées) -------------------------------------

    /// Empty → Recording. Démarre la capture de la boucle initiale.
    Status record();

    /// Recording → Playing. Fige la boucle et bascule en lecture.
    Status finishRecording();

    /// Stopped → Playing. Relance la lecture d'un contenu existant.
    Status play();

    /// {Recording, Playing, Overdubbing} → Stopped.
    Status stop();

    /// Playing → Overdubbing. Superpose une couche par-dessus la boucle.
    Status startOverdub();

    /// Overdubbing → Playing. Termine la superposition.
    Status stopOverdub();

    /// Tout état → Empty. Efface le contenu (toujours autorisé).
    void clear() noexcept;

    // --- Paramètres de mixage (indépendants de l'état) --------------------

    void setGain(Gain gain) noexcept { gain_ = gain; }
    void setMuted(bool muted) noexcept { muted_ = muted; }

private:
    TrackState state_ = TrackState::Empty;
    Gain gain_ = Gain::unity();
    bool muted_ = false;
};

}  // namespace voicelive::core
