// SPDX-License-Identifier: MIT
//
// Distortion — saturation par mise en forme d'onde (waveshaping). Trois modes :
// soft-clip (tanh, type overdrive), hard-clip (écrêtage franc) et fuzz (asymétrique
// agressif). Un filtre de tonalité (passe-bas un pôle) adoucit les aigus en sortie.
// Cœur du son « guitare électrique saturée ».
#pragma once

#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Effect.hpp"

namespace voicelive::dsp {

class Distortion final : public Effect {
public:
    enum class Mode { SoftClip, HardClip, Fuzz };

    void prepare(core::SampleRate sampleRate, std::size_t maxBlockSize) override;
    void process(std::span<float> block) noexcept override;
    void reset() noexcept override;

    void setDrive(float drive) noexcept;  ///< Gain d'attaque avant écrêtage [1, 50].
    void setTone(float tone) noexcept;    ///< Brillance de sortie [0, 1] (0 = sombre).
    void setLevel(float level) noexcept;  ///< Niveau de sortie [0, 1].
    void setMix(float mix) noexcept;      ///< Dry ↔ wet [0, 1].
    void setMode(Mode mode) noexcept { mode_ = mode; }

    [[nodiscard]] float drive() const noexcept { return drive_; }
    [[nodiscard]] float tone() const noexcept { return tone_; }
    [[nodiscard]] float level() const noexcept { return level_; }
    [[nodiscard]] float mix() const noexcept { return mix_; }
    [[nodiscard]] Mode mode() const noexcept { return mode_; }

private:
    unsigned sampleRate_ = 0;  ///< 0 avant prepare() → process() est un passthrough.
    float toneState_ = 0.0F;   ///< État du passe-bas de tonalité.

    float drive_ = 8.0F;
    float tone_ = 0.5F;
    float level_ = 0.6F;
    float mix_ = 1.0F;
    Mode mode_ = Mode::SoftClip;
};

}  // namespace voicelive::dsp
