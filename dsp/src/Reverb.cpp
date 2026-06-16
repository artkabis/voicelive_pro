// SPDX-License-Identifier: MIT
#include "voicelive/dsp/Reverb.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"

namespace voicelive::dsp {
namespace {

// Constantes Freeverb d'origine (réglées pour 44,1 kHz).
constexpr std::array<int, 8> kCombTuning{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<int, 4> kAllpassTuning{556, 441, 341, 225};
constexpr float kFixedGain = 0.015F;
constexpr float kRoomScale = 0.28F;
constexpr float kRoomOffset = 0.7F;
constexpr float kDampScale = 0.4F;
constexpr float kAllpassFeedback = 0.5F;
constexpr double kReferenceRate = 44'100.0;

float clamp01(float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

/// Met à l'échelle un délai (réglé à 44,1 kHz) pour la fréquence courante.
std::size_t scaledDelay(int tuning, double rateRatio) noexcept {
    const long scaled = std::lround(static_cast<double>(tuning) * rateRatio);
    return static_cast<std::size_t>(std::max<long>(scaled, 1));
}

}  // namespace

float Reverb::Comb::process(float input, float feedback, float damp) noexcept {
    const float output = buffer[pos];
    store = (output * (1.0F - damp)) + (store * damp);
    buffer[pos] = input + (store * feedback);
    pos = (pos + 1) % buffer.size();
    return output;
}

void Reverb::Comb::clear() noexcept {
    std::fill(buffer.begin(), buffer.end(), 0.0F);
    pos = 0;
    store = 0.0F;
}

float Reverb::Allpass::process(float input) noexcept {
    const float buffered = buffer[pos];
    const float output = -input + buffered;
    buffer[pos] = input + (buffered * kAllpassFeedback);
    pos = (pos + 1) % buffer.size();
    return output;
}

void Reverb::Allpass::clear() noexcept {
    std::fill(buffer.begin(), buffer.end(), 0.0F);
    pos = 0;
}

void Reverb::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockSize*/) {
    const double rateRatio = static_cast<double>(sampleRate.hz()) / kReferenceRate;

    combs_.assign(kCombCount, Comb{});
    for (std::size_t i = 0; i < kCombCount; ++i) {
        combs_[i].buffer.assign(scaledDelay(kCombTuning[i], rateRatio), 0.0F);
    }

    allpasses_.assign(kAllpassCount, Allpass{});
    for (std::size_t i = 0; i < kAllpassCount; ++i) {
        allpasses_[i].buffer.assign(scaledDelay(kAllpassTuning[i], rateRatio), 0.0F);
    }
}

void Reverb::process(std::span<float> block) noexcept {
    if (combs_.empty()) {
        return;  // prepare() non appelé : ne touche à rien.
    }

    const float feedback = (roomSize_ * kRoomScale) + kRoomOffset;
    const float damp = damping_ * kDampScale;

    for (float& sample : block) {
        const float input = sample * kFixedGain;

        float wetSignal = 0.0F;
        for (Comb& comb : combs_) {
            wetSignal += comb.process(input, feedback, damp);
        }
        for (Allpass& allpass : allpasses_) {
            wetSignal = allpass.process(wetSignal);
        }

        sample = (sample * dry_) + (wetSignal * wet_);
    }
}

void Reverb::reset() noexcept {
    for (Comb& comb : combs_) {
        comb.clear();
    }
    for (Allpass& allpass : allpasses_) {
        allpass.clear();
    }
}

void Reverb::setRoomSize(float value) noexcept {
    roomSize_ = clamp01(value);
}
void Reverb::setDamping(float value) noexcept {
    damping_ = clamp01(value);
}
void Reverb::setWet(float value) noexcept {
    wet_ = clamp01(value);
}
void Reverb::setDry(float value) noexcept {
    dry_ = clamp01(value);
}

}  // namespace voicelive::dsp
