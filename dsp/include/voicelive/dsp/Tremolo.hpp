// SPDX-License-Identifier: MIT
//
// Tremolo — modulation périodique de l'amplitude par un LFO sinusoïdal. Effet
// « vintage / surf » classique de la guitare électrique.
#pragma once

#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Effect.hpp"

namespace voicelive::dsp {

class Tremolo final : public Effect {
public:
    void prepare(core::SampleRate sampleRate, std::size_t maxBlockSize) override;
    void process(std::span<float> block) noexcept override;
    void reset() noexcept override;

    void setRate(float hz) noexcept;      ///< Vitesse du LFO [0.1, 20] Hz.
    void setDepth(float depth) noexcept;  ///< Profondeur de modulation [0, 1].
    void setMix(float mix) noexcept;      ///< Dry ↔ wet [0, 1].

    [[nodiscard]] float rate() const noexcept { return rate_; }
    [[nodiscard]] float depth() const noexcept { return depth_; }
    [[nodiscard]] float mix() const noexcept { return mix_; }

private:
    unsigned sampleRate_ = 0;  ///< 0 avant prepare() → process() est un passthrough.
    double lfoPhase_ = 0.0;

    float rate_ = 5.0F;
    float depth_ = 0.7F;
    float mix_ = 1.0F;
};

}  // namespace voicelive::dsp
