// SPDX-License-Identifier: MIT
//
// Phaser — cascade de filtres passe-tout du premier ordre dont la fréquence est
// balayée par un LFO, recombinée avec le signal direct. Crée des encoches mobiles
// dans le spectre : le « woosh » psychédélique typique de la guitare.
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Effect.hpp"

namespace voicelive::dsp {

class Phaser final : public Effect {
public:
    static constexpr int kStages = 6;  ///< Nombre d'étages passe-tout (3 encoches).

    void prepare(core::SampleRate sampleRate, std::size_t maxBlockSize) override;
    void process(std::span<float> block) noexcept override;
    void reset() noexcept override;

    void setRate(float hz) noexcept;            ///< Vitesse du balayage [0.05, 10] Hz.
    void setDepth(float depth) noexcept;        ///< Amplitude du balayage [0, 1].
    void setFeedback(float feedback) noexcept;  ///< Résonance [0, 0.95].
    void setMix(float mix) noexcept;            ///< Dry ↔ wet [0, 1].

    [[nodiscard]] float rate() const noexcept { return rate_; }
    [[nodiscard]] float depth() const noexcept { return depth_; }
    [[nodiscard]] float feedback() const noexcept { return feedback_; }
    [[nodiscard]] float mix() const noexcept { return mix_; }

private:
    unsigned sampleRate_ = 0;  ///< 0 avant prepare() → process() est un passthrough.
    double lfoPhase_ = 0.0;
    std::array<float, kStages> stageState_{};  ///< Mémoire de chaque passe-tout.
    float feedbackSample_ = 0.0F;

    float rate_ = 0.5F;
    float depth_ = 0.7F;
    float feedback_ = 0.4F;
    float mix_ = 0.5F;
};

}  // namespace voicelive::dsp
