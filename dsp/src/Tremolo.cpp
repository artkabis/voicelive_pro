// SPDX-License-Identifier: MIT
#include "voicelive/dsp/Tremolo.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>

#include "voicelive/core/AudioParams.hpp"

namespace voicelive::dsp {

void Tremolo::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate.hz();
    lfoPhase_ = 0.0;
}

void Tremolo::process(std::span<float> block) noexcept {
    if (sampleRate_ == 0) {
        return;
    }
    const double increment = static_cast<double>(rate_) / static_cast<double>(sampleRate_);

    for (float& sample : block) {
        const auto lfo01 =
            static_cast<float>((std::sin(2.0 * std::numbers::pi * lfoPhase_) + 1.0) * 0.5);
        // gain ∈ [1-depth, 1] : à depth=1 le signal est totalement coupé au creux.
        const float modGain = (1.0F - depth_) + (depth_ * lfo01);
        const float wet = sample * modGain;

        lfoPhase_ += increment;
        if (lfoPhase_ >= 1.0) {
            lfoPhase_ -= 1.0;
        }

        sample = (sample * (1.0F - mix_)) + (wet * mix_);
    }
}

void Tremolo::reset() noexcept {
    lfoPhase_ = 0.0;
}

void Tremolo::setRate(float hz) noexcept {
    rate_ = std::clamp(hz, 0.1F, 20.0F);
}

void Tremolo::setDepth(float depth) noexcept {
    depth_ = std::clamp(depth, 0.0F, 1.0F);
}

void Tremolo::setMix(float mix) noexcept {
    mix_ = std::clamp(mix, 0.0F, 1.0F);
}

}  // namespace voicelive::dsp
