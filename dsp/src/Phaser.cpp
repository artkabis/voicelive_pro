// SPDX-License-Identifier: MIT
#include "voicelive/dsp/Phaser.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>

#include "voicelive/core/AudioParams.hpp"

namespace voicelive::dsp {
namespace {
constexpr float kMinFreq = 300.0F;   // bas du balayage des encoches
constexpr float kMaxFreq = 2200.0F;  // haut du balayage des encoches
}  // namespace

void Phaser::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate.hz();
    reset();
}

void Phaser::process(std::span<float> block) noexcept {
    if (sampleRate_ == 0) {
        return;
    }
    const double increment = static_cast<double>(rate_) / static_cast<double>(sampleRate_);
    const auto nyquist = static_cast<float>(sampleRate_) * 0.5F;

    for (float& sample : block) {
        const auto lfo01 =
            static_cast<float>((std::sin(2.0 * std::numbers::pi * lfoPhase_) + 1.0) * 0.5);
        const float centre = kMinFreq + ((kMaxFreq - kMinFreq) * lfo01 * depth_);

        // Coefficient passe-tout du premier ordre pour la fréquence courante.
        const float ratio = std::clamp(centre / nyquist, 0.001F, 0.99F);
        const auto tangent =
            static_cast<float>(std::tan(std::numbers::pi * static_cast<double>(ratio) * 0.5));
        const float coeff = (tangent - 1.0F) / (tangent + 1.0F);

        float x = sample + (feedbackSample_ * feedback_);
        for (float& state : stageState_) {
            const float y = (coeff * x) + state;
            state = x - (coeff * y);
            x = y;
        }
        feedbackSample_ = x;

        lfoPhase_ += increment;
        if (lfoPhase_ >= 1.0) {
            lfoPhase_ -= 1.0;
        }

        sample = (sample * (1.0F - mix_)) + (x * mix_);
    }
}

void Phaser::reset() noexcept {
    lfoPhase_ = 0.0;
    stageState_.fill(0.0F);
    feedbackSample_ = 0.0F;
}

void Phaser::setRate(float hz) noexcept {
    rate_ = std::clamp(hz, 0.05F, 10.0F);
}

void Phaser::setDepth(float depth) noexcept {
    depth_ = std::clamp(depth, 0.0F, 1.0F);
}

void Phaser::setFeedback(float feedback) noexcept {
    feedback_ = std::clamp(feedback, 0.0F, 0.95F);
}

void Phaser::setMix(float mix) noexcept {
    mix_ = std::clamp(mix, 0.0F, 1.0F);
}

}  // namespace voicelive::dsp
