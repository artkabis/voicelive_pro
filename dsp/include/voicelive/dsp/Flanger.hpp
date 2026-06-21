// SPDX-License-Identifier: MIT
//
// Flanger — ligne de retard très courte (1–7 ms) modulée par un LFO et réinjectée
// (feedback), mixée au signal direct. Produit le « jet stream » métallique
// caractéristique, plus marqué que le chorus grâce au retour.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Effect.hpp"

namespace voicelive::dsp {

class Flanger final : public Effect {
public:
    void prepare(core::SampleRate sampleRate, std::size_t maxBlockSize) override;
    void process(std::span<float> block) noexcept override;
    void reset() noexcept override;

    void setRate(float hz) noexcept;            ///< Vitesse du LFO [0.05, 10] Hz.
    void setDepth(float depth) noexcept;        ///< Profondeur de modulation [0, 1].
    void setFeedback(float feedback) noexcept;  ///< Retour [0, 0.95].
    void setMix(float mix) noexcept;            ///< Dry ↔ wet [0, 1].

    [[nodiscard]] float rate() const noexcept { return rate_; }
    [[nodiscard]] float depth() const noexcept { return depth_; }
    [[nodiscard]] float feedback() const noexcept { return feedback_; }
    [[nodiscard]] float mix() const noexcept { return mix_; }

private:
    [[nodiscard]] float readFractional(double delaySamples) const noexcept;

    std::vector<float> line_;
    std::size_t writePos_ = 0;
    double lfoPhase_ = 0.0;
    unsigned sampleRate_ = core::SampleRate::kStudio;

    float rate_ = 0.3F;
    float depth_ = 0.6F;
    float feedback_ = 0.5F;
    float mix_ = 0.5F;
};

}  // namespace voicelive::dsp
