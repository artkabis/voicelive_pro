// SPDX-License-Identifier: MIT
#include "voicelive/dsp/Chorus.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>

#include "voicelive/core/AudioParams.hpp"

namespace voicelive::dsp {
namespace {
constexpr double kBaseDelaySeconds = 0.015;  // 15 ms
constexpr double kMaxDepthSeconds = 0.010;   // ± 10 ms
constexpr double kMaxBufferSeconds = 0.050;
}  // namespace

void Chorus::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate.hz();
    const auto size =
        static_cast<std::size_t>(kMaxBufferSeconds * static_cast<double>(sampleRate_)) + 4;
    line_.assign(size, 0.0F);
    writePos_ = 0;
    lfoPhase_ = 0.0;
}

float Chorus::readFractional(double delaySamples) const noexcept {
    const auto size = static_cast<double>(line_.size());
    auto readPos = static_cast<double>(writePos_) - delaySamples;
    while (readPos < 0.0) {
        readPos += size;
    }
    const auto index0 = static_cast<std::size_t>(readPos) % line_.size();
    const std::size_t index1 = (index0 + 1) % line_.size();
    const auto frac = static_cast<float>(readPos - std::floor(readPos));
    return (line_[index0] * (1.0F - frac)) + (line_[index1] * frac);
}

void Chorus::process(std::span<float> block) noexcept {
    if (line_.empty()) {
        return;
    }
    const double rate = static_cast<double>(rate_) / static_cast<double>(sampleRate_);
    const double baseSamples = kBaseDelaySeconds * static_cast<double>(sampleRate_);
    const double depthSamples =
        static_cast<double>(depth_) * kMaxDepthSeconds * static_cast<double>(sampleRate_);
    const auto maxDelay = static_cast<double>(line_.size() - 2);

    for (float& sample : block) {
        const double lfo = std::sin(2.0 * std::numbers::pi * lfoPhase_);
        const double delay = std::clamp(baseSamples + (depthSamples * lfo), 1.0, maxDelay);
        const float wet = readFractional(delay);

        line_[writePos_] = sample;
        writePos_ = (writePos_ + 1) % line_.size();

        lfoPhase_ += rate;
        if (lfoPhase_ >= 1.0) {
            lfoPhase_ -= 1.0;
        }

        sample = (sample * (1.0F - mix_)) + (wet * mix_);
    }
}

void Chorus::reset() noexcept {
    std::fill(line_.begin(), line_.end(), 0.0F);
    writePos_ = 0;
    lfoPhase_ = 0.0;
}

void Chorus::setRate(float hz) noexcept {
    rate_ = std::clamp(hz, 0.05F, 10.0F);
}

void Chorus::setDepth(float depth) noexcept {
    depth_ = std::clamp(depth, 0.0F, 1.0F);
}

void Chorus::setMix(float mix) noexcept {
    mix_ = std::clamp(mix, 0.0F, 1.0F);
}

}  // namespace voicelive::dsp
