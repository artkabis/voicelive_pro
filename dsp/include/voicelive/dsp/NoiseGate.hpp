// SPDX-License-Identifier: MIT
//
// NoiseGate — porte de bruit. Suit l'enveloppe du signal et coupe (atténue) la
// sortie quand le niveau passe sous un seuil, supprimant souffle et larsen entre
// les notes. Indispensable dès qu'on ajoute du gain (distorsion) sur une guitare.
#pragma once

#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Effect.hpp"

namespace voicelive::dsp {

class NoiseGate final : public Effect {
public:
    void prepare(core::SampleRate sampleRate, std::size_t maxBlockSize) override;
    void process(std::span<float> block) noexcept override;
    void reset() noexcept override;

    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    void setThreshold(float thresholdDb) noexcept;  ///< Seuil d'ouverture [-80, 0] dB.
    void setAttack(float ms) noexcept;              ///< Temps d'ouverture [0.1, 100] ms.
    void setRelease(float ms) noexcept;             ///< Temps de fermeture [1, 1000] ms.

    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }
    [[nodiscard]] float thresholdDb() const noexcept { return thresholdDb_; }
    [[nodiscard]] float attackMs() const noexcept { return attackMs_; }
    [[nodiscard]] float releaseMs() const noexcept { return releaseMs_; }

private:
    void updateCoeffs() noexcept;

    unsigned sampleRate_ = core::SampleRate::kStudio;
    float env_ = 0.0F;   ///< Enveloppe suivie du signal.
    float gain_ = 0.0F;  ///< Gain de porte courant [0, 1].
    float attackCoeff_ = 0.0F;
    float releaseCoeff_ = 0.0F;
    float envCoeff_ = 0.0F;  ///< Lissage de l'enveloppe (détecteur).

    bool enabled_ = false;
    float thresholdDb_ = -45.0F;
    float attackMs_ = 5.0F;
    float releaseMs_ = 120.0F;
};

}  // namespace voicelive::dsp
