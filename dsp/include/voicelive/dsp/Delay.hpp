// SPDX-License-Identifier: MIT
//
// Delay — écho à ligne de retard, en C++ pur. 2ᵉ effet de la chaîne.
//
// La ligne de retard est dimensionnée une fois dans prepare() (pour le délai
// maximal). process() lit/écrit dans ce buffer sans allocation.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Effect.hpp"

namespace voicelive::dsp {

class Delay final : public Effect {
public:
    static constexpr float kMaxDelaySeconds = 2.0F;

    void prepare(core::SampleRate sampleRate, std::size_t maxBlockSize) override;
    void process(std::span<float> block) noexcept override;
    void reset() noexcept override;

    void setDelaySeconds(float seconds) noexcept;  ///< [0, kMaxDelaySeconds].
    void setFeedback(float feedback) noexcept;     ///< [0, 0.95] (réinjection).
    void setMix(float mix) noexcept;               ///< [0, 1] (dry↔wet).

    [[nodiscard]] float delaySeconds() const noexcept { return delaySeconds_; }
    [[nodiscard]] float feedback() const noexcept { return feedback_; }
    [[nodiscard]] float mix() const noexcept { return mix_; }

private:
    std::vector<float> line_;
    std::size_t writePos_ = 0;
    unsigned sampleRate_ = core::SampleRate::kStudio;
    float delaySeconds_ = 0.25F;
    float feedback_ = 0.4F;
    float mix_ = 0.3F;
};

}  // namespace voicelive::dsp
