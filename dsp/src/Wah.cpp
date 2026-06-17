// SPDX-License-Identifier: MIT
#include "voicelive/dsp/Wah.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>

#include "voicelive/core/AudioParams.hpp"

namespace voicelive::dsp {

void Wah::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate.hz();
    reset();
}

void Wah::process(std::span<float> block) noexcept {
    if (sampleRate_ == 0) {
        return;
    }
    const float damping = 1.0F / resonance_;
    const auto nyquist = static_cast<float>(sampleRate_) * 0.5F;

    for (float& sample : block) {
        const auto lfo01 =
            static_cast<float>((std::sin(2.0 * std::numbers::pi * lfoPhase_) + 1.0) * 0.5);
        const float centre = minFreq_ + ((maxFreq_ - minFreq_) * lfo01);

        // Filtre state-variable (Chamberlin). `f` borné pour rester stable.
        const float ratio = std::clamp(centre / nyquist, 0.0F, 0.49F);
        const auto coeff =
            static_cast<float>(2.0 * std::sin(std::numbers::pi * static_cast<double>(ratio)));

        low_ += coeff * band_;
        const float high = sample - low_ - (damping * band_);
        band_ += coeff * high;

        lfoPhase_ += static_cast<double>(rate_) / static_cast<double>(sampleRate_);
        if (lfoPhase_ >= 1.0) {
            lfoPhase_ -= 1.0;
        }

        sample = (sample * (1.0F - mix_)) + (band_ * mix_);
    }
}

void Wah::reset() noexcept {
    low_ = 0.0F;
    band_ = 0.0F;
    lfoPhase_ = 0.0;
}

void Wah::setRate(float hz) noexcept {
    rate_ = std::clamp(hz, 0.05F, 10.0F);
}

void Wah::setMinFrequency(float hz) noexcept {
    minFreq_ = std::clamp(hz, 20.0F, 8000.0F);
}

void Wah::setMaxFrequency(float hz) noexcept {
    maxFreq_ = std::clamp(hz, 20.0F, 8000.0F);
}

void Wah::setResonance(float resonance) noexcept {
    resonance_ = std::clamp(resonance, 0.5F, 20.0F);
}

void Wah::setMix(float mix) noexcept {
    mix_ = std::clamp(mix, 0.0F, 1.0F);
}

}  // namespace voicelive::dsp
