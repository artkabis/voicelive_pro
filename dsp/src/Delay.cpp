// SPDX-License-Identifier: MIT
#include "voicelive/dsp/Delay.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"

namespace voicelive::dsp {

void Delay::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate.hz();
    const auto maxSamples = static_cast<std::size_t>(static_cast<double>(kMaxDelaySeconds) *
                                                     static_cast<double>(sampleRate_)) +
                            1;
    line_.assign(maxSamples, 0.0F);
    writePos_ = 0;
}

void Delay::process(std::span<float> block) noexcept {
    const std::size_t size = line_.size();
    if (size == 0) {
        return;  // prepare() non appelé.
    }

    // Délai constant sur le bloc (les paramètres ne changent pas en cours).
    const auto requested = static_cast<std::size_t>(
        std::lround(static_cast<double>(delaySeconds_) * static_cast<double>(sampleRate_)));
    const std::size_t delaySamples = std::clamp<std::size_t>(requested, 1, size - 1);
    std::size_t readPos = (writePos_ + size - delaySamples) % size;

    for (float& sample : block) {
        const float input = sample;
        const float delayed = line_[readPos];
        line_[writePos_] = input + (delayed * feedback_);
        sample = (input * (1.0F - mix_)) + (delayed * mix_);
        writePos_ = (writePos_ + 1) % size;
        readPos = (readPos + 1) % size;
    }
}

void Delay::reset() noexcept {
    std::fill(line_.begin(), line_.end(), 0.0F);
    writePos_ = 0;
}

void Delay::setDelaySeconds(float seconds) noexcept {
    delaySeconds_ = std::clamp(seconds, 0.0F, kMaxDelaySeconds);
}

void Delay::setFeedback(float feedback) noexcept {
    feedback_ = std::clamp(feedback, 0.0F, 0.95F);
}

void Delay::setMix(float mix) noexcept {
    mix_ = std::clamp(mix, 0.0F, 1.0F);
}

}  // namespace voicelive::dsp
