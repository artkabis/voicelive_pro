// SPDX-License-Identifier: MIT
//
// Wah — filtre passe-bande résonant (state-variable) dont la fréquence centrale
// est balayée par un LFO (auto-wah).
#pragma once

#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Effect.hpp"

namespace voicelive::dsp {

class Wah final : public Effect {
public:
    void prepare(core::SampleRate sampleRate, std::size_t maxBlockSize) override;
    void process(std::span<float> block) noexcept override;
    void reset() noexcept override;

    void setRate(float hz) noexcept;              ///< Vitesse du balayage [0.05, 10] Hz.
    void setMinFrequency(float hz) noexcept;      ///< Bas du balayage [20, 8000] Hz.
    void setMaxFrequency(float hz) noexcept;      ///< Haut du balayage [20, 8000] Hz.
    void setResonance(float resonance) noexcept;  ///< Acuité [0.5, 20].
    void setMix(float mix) noexcept;              ///< Dry ↔ wet [0, 1].

    [[nodiscard]] float minFrequency() const noexcept { return minFreq_; }
    [[nodiscard]] float maxFrequency() const noexcept { return maxFreq_; }
    [[nodiscard]] float resonance() const noexcept { return resonance_; }
    [[nodiscard]] float mix() const noexcept { return mix_; }

private:
    unsigned sampleRate_ = core::SampleRate::kStudio;
    double lfoPhase_ = 0.0;
    float low_ = 0.0F;
    float band_ = 0.0F;

    float rate_ = 1.5F;
    float minFreq_ = 400.0F;
    float maxFreq_ = 2000.0F;
    float resonance_ = 4.0F;
    float mix_ = 1.0F;
};

}  // namespace voicelive::dsp
