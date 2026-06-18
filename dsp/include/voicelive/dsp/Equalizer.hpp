// SPDX-License-Identifier: MIT
//
// Equalizer — égaliseur 3 bandes (low shelf / mid peak / high shelf) à base de
// filtres biquad (coefficients RBJ). Gains nuls = passthrough exact. Brique de
// la chaîne de mastering.
#pragma once

#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Effect.hpp"

namespace voicelive::dsp {

class Equalizer final : public Effect {
public:
    void prepare(core::SampleRate sampleRate, std::size_t maxBlockSize) override;
    void process(std::span<float> block) noexcept override;
    void reset() noexcept override;

    void setLowGain(float decibels) noexcept;   ///< Grave (shelf ~120 Hz) [-24, 24] dB.
    void setMidGain(float decibels) noexcept;   ///< Médium (peak ~1 kHz) [-24, 24] dB.
    void setHighGain(float decibels) noexcept;  ///< Aigu (shelf ~6 kHz) [-24, 24] dB.

    [[nodiscard]] float lowGain() const noexcept { return lowGainDb_; }
    [[nodiscard]] float midGain() const noexcept { return midGainDb_; }
    [[nodiscard]] float highGain() const noexcept { return highGainDb_; }

private:
    /// Filtre biquad (forme directe transposée II).
    struct Biquad {
        float b0 = 1.0F;
        float b1 = 0.0F;
        float b2 = 0.0F;
        float a1 = 0.0F;
        float a2 = 0.0F;
        float z1 = 0.0F;
        float z2 = 0.0F;

        float process(float input) noexcept {
            const float output = (b0 * input) + z1;
            z1 = (b1 * input) - (a1 * output) + z2;
            z2 = (b2 * input) - (a2 * output);
            return output;
        }
        void clear() noexcept {
            z1 = 0.0F;
            z2 = 0.0F;
        }
    };

    void updateCoefficients() noexcept;

    unsigned sampleRate_ = core::SampleRate::kStudio;
    Biquad low_;
    Biquad mid_;
    Biquad high_;
    float lowGainDb_ = 0.0F;
    float midGainDb_ = 0.0F;
    float highGainDb_ = 0.0F;
};

}  // namespace voicelive::dsp
