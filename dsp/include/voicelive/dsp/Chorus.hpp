// SPDX-License-Identifier: MIT
//
// Chorus — ligne de retard courte modulée par un LFO, mixée au signal direct.
// Donne l'impression de plusieurs voix légèrement désaccordées.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Effect.hpp"

namespace voicelive::dsp {

class Chorus final : public Effect {
public:
    void prepare(core::SampleRate sampleRate, std::size_t maxBlockSize) override;
    void process(std::span<float> block) noexcept override;
    void reset() noexcept override;

    void setRate(float hz) noexcept;      ///< Vitesse du LFO [0.05, 10] Hz.
    void setDepth(float depth) noexcept;  ///< Profondeur de modulation [0, 1].
    void setMix(float mix) noexcept;      ///< Dry ↔ wet [0, 1].

    [[nodiscard]] float rate() const noexcept { return rate_; }
    [[nodiscard]] float depth() const noexcept { return depth_; }
    [[nodiscard]] float mix() const noexcept { return mix_; }

private:
    [[nodiscard]] float readFractional(double delaySamples) const noexcept;

    std::vector<float> line_;
    std::size_t writePos_ = 0;
    double lfoPhase_ = 0.0;  // [0, 1)
    unsigned sampleRate_ = core::SampleRate::kStudio;

    float rate_ = 1.5F;
    float depth_ = 0.5F;
    float mix_ = 0.5F;
};

}  // namespace voicelive::dsp
