// SPDX-License-Identifier: MIT
//
// Compressor — compresseur de dynamique (détecteur de crête + attaque/relâche).
// Réduit le niveau au-dessus d'un seuil selon un ratio, avec gain de
// compensation (makeup). Brique de la chaîne de mastering.
#pragma once

#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Effect.hpp"

namespace voicelive::dsp {

class Compressor final : public Effect {
public:
    void prepare(core::SampleRate sampleRate, std::size_t maxBlockSize) override;
    void process(std::span<float> block) noexcept override;
    void reset() noexcept override;

    void setThreshold(float decibels) noexcept;    ///< Seuil [-60, 0] dB.
    void setRatio(float ratio) noexcept;           ///< Ratio [1, 20]:1.
    void setAttack(float milliseconds) noexcept;   ///< Attaque [0.1, 200] ms.
    void setRelease(float milliseconds) noexcept;  ///< Relâche [1, 2000] ms.
    void setMakeup(float decibels) noexcept;       ///< Gain de compensation [0, 24] dB.

    [[nodiscard]] float threshold() const noexcept { return thresholdDb_; }
    [[nodiscard]] float ratio() const noexcept { return ratio_; }
    [[nodiscard]] float makeup() const noexcept { return makeupDb_; }

private:
    [[nodiscard]] float smoothingCoefficient(float milliseconds) const noexcept;

    unsigned sampleRate_ = core::SampleRate::kStudio;
    float thresholdDb_ = -18.0F;
    float ratio_ = 4.0F;
    float attackMs_ = 10.0F;
    float releaseMs_ = 120.0F;
    float makeupDb_ = 0.0F;
    float reductionDb_ = 0.0F;  // réduction de gain courante (dB, ≥ 0)
};

}  // namespace voicelive::dsp
