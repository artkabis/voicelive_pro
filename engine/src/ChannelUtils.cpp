// SPDX-License-Identifier: MIT
#include "voicelive/engine/ChannelUtils.hpp"

#include <algorithm>
#include <cstddef>
#include <span>

namespace voicelive::engine::channels {

void downmixToMono(std::span<float> mono, const float* const* inputChannels,
                   std::size_t numChannels) noexcept {
    if (numChannels == 0 || inputChannels == nullptr) {
        std::fill(mono.begin(), mono.end(), 0.0F);
        return;
    }

    const float scale = 1.0F / static_cast<float>(numChannels);
    for (std::size_t i = 0; i < mono.size(); ++i) {
        float sum = 0.0F;
        for (std::size_t channel = 0; channel < numChannels; ++channel) {
            const float* data = inputChannels[channel];
            if (data != nullptr) {
                sum += data[i];
            }
        }
        mono[i] = sum * scale;
    }
}

void spreadToChannels(float* const* outputChannels, std::size_t numChannels,
                      std::span<const float> mono) noexcept {
    if (outputChannels == nullptr) {
        return;
    }
    for (std::size_t channel = 0; channel < numChannels; ++channel) {
        float* data = outputChannels[channel];
        if (data == nullptr) {
            continue;
        }
        for (std::size_t i = 0; i < mono.size(); ++i) {
            data[i] = mono[i];
        }
    }
}

}  // namespace voicelive::engine::channels
