// SPDX-License-Identifier: MIT
#include "voicelive/dsp/EffectChain.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <utility>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Effect.hpp"

namespace voicelive::dsp {

void EffectChain::prepare(core::SampleRate sampleRate, std::size_t maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    prepared_ = true;
    for (const std::unique_ptr<Effect>& effect : effects_) {
        effect->prepare(sampleRate, maxBlockSize);
    }
}

void EffectChain::add(std::unique_ptr<Effect> effect) {
    if (effect == nullptr) {
        return;
    }
    if (prepared_) {
        effect->prepare(sampleRate_, maxBlockSize_);
    }
    effects_.push_back(std::move(effect));
}

void EffectChain::process(std::span<float> block) noexcept {
    for (const std::unique_ptr<Effect>& effect : effects_) {
        effect->process(block);
    }
}

void EffectChain::reset() noexcept {
    for (const std::unique_ptr<Effect>& effect : effects_) {
        effect->reset();
    }
}

}  // namespace voicelive::dsp
