// SPDX-License-Identifier: MIT
//
// Interface commune des effets DSP.
//
// Contrat temps réel STRICT :
//   • prepare() : peut allouer / dimensionner les buffers (hors thread audio).
//   • process() : AUCUNE allocation, AUCUN verrou, AUCUNE I/O — `noexcept`.
//   • reset()   : remet l'état interne à zéro sans réallouer.
// Respecter ce contrat est ce qui garantit l'absence de craquements.
#pragma once

#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"

namespace voicelive::dsp {

class Effect {
public:
    virtual ~Effect() = default;

    /// Alloue/dimensionne l'état interne pour la fréquence et la taille de bloc
    /// maximale données. Appelé hors du thread audio.
    virtual void prepare(core::SampleRate sampleRate, std::size_t maxBlockSize) = 0;

    /// Traite un bloc mono sur place. Temps réel : ne doit rien allouer.
    virtual void process(std::span<float> block) noexcept = 0;

    /// Réinitialise l'état (queues de réverbération, etc.) sans réallouer.
    virtual void reset() noexcept = 0;

protected:
    Effect() = default;
    Effect(const Effect&) = default;
    Effect(Effect&&) = default;
    Effect& operator=(const Effect&) = default;
    Effect& operator=(Effect&&) = default;
};

}  // namespace voicelive::dsp
