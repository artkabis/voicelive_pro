// SPDX-License-Identifier: MIT
#include "voicelive/dsp/Compressor.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"

namespace voicelive::dsp {

void Compressor::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate.hz();
    reset();
}

float Compressor::smoothingCoefficient(float milliseconds) const noexcept {
    const float samples = (milliseconds * 0.001F) * static_cast<float>(sampleRate_);
    if (samples <= 0.0F) {
        return 0.0F;
    }
    return std::exp(-1.0F / samples);
}

void Compressor::process(std::span<float> block) noexcept {
    const float attackCoeff = smoothingCoefficient(attackMs_);
    const float releaseCoeff = smoothingCoefficient(releaseMs_);
    // kneeSlope : fraction de l'overshoot à réduire. ratio=∞ → 1 (limiting pur),
    // ratio=1 → 0 (pas de compression). Formule standard gain-computer dB-domain.
    const float kneeSlope = 1.0F - (1.0F / ratio_);

    for (float& sample : block) {
        const float level = std::abs(sample);
        const float levelDb = 20.0F * std::log10(std::max(level, 1.0e-6F));
        const float overshoot = levelDb - thresholdDb_;
        const float target = (overshoot > 0.0F) ? (overshoot * kneeSlope) : 0.0F;

        const float coeff = (target > reductionDb_) ? attackCoeff : releaseCoeff;
        reductionDb_ = (coeff * reductionDb_) + ((1.0F - coeff) * target);

        const float gainDb = makeupDb_ - reductionDb_;
        sample *= std::pow(10.0F, gainDb / 20.0F);
    }
}

void Compressor::reset() noexcept {
    reductionDb_ = 0.0F;
}

void Compressor::setThreshold(float decibels) noexcept {
    thresholdDb_ = std::clamp(decibels, -60.0F, 0.0F);
}

void Compressor::setRatio(float ratio) noexcept {
    ratio_ = std::clamp(ratio, 1.0F, 20.0F);
}

void Compressor::setAttack(float milliseconds) noexcept {
    attackMs_ = std::clamp(milliseconds, 0.1F, 200.0F);
}

void Compressor::setRelease(float milliseconds) noexcept {
    releaseMs_ = std::clamp(milliseconds, 1.0F, 2000.0F);
}

void Compressor::setMakeup(float decibels) noexcept {
    makeupDb_ = std::clamp(decibels, 0.0F, 24.0F);
}

}  // namespace voicelive::dsp
