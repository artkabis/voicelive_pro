// SPDX-License-Identifier: MIT
#include "voicelive/engine/Mixer.hpp"

#include <algorithm>
#include <cstddef>
#include <span>

namespace voicelive::engine::mixer {

void addScaled(std::span<float> destination, std::span<const float> source, float gain) noexcept {
    const std::size_t count = std::min(destination.size(), source.size());
    for (std::size_t i = 0; i < count; ++i) {
        destination[i] += source[i] * gain;
    }
}

void limit(std::span<float> block) noexcept {
    for (float& sample : block) {
        sample = std::clamp(sample, -1.0F, 1.0F);
    }
}

}  // namespace voicelive::engine::mixer
