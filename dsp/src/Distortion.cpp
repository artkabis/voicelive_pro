// SPDX-License-Identifier: MIT
#include "voicelive/dsp/Distortion.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>

#include "voicelive/core/AudioParams.hpp"

namespace voicelive::dsp {
namespace {

/// Mise en forme d'onde selon le mode choisi. `x` est déjà multiplié par le drive.
float shape(float x, Distortion::Mode mode) noexcept {
    switch (mode) {
        case Distortion::Mode::SoftClip:
            // Overdrive doux : la tangente hyperbolique sature en douceur.
            return std::tanh(x);
        case Distortion::Mode::HardClip:
            // Écrêtage franc : son carré, agressif.
            return std::clamp(x, -1.0F, 1.0F);
        case Distortion::Mode::Fuzz: {
            // Fuzz asymétrique : redresse partiellement, très riche en harmoniques.
            const float pos = 1.0F - std::exp(-std::abs(x));
            return x >= 0.0F ? pos : -0.7F * (1.0F - std::exp(-std::abs(0.7F * x)));
        }
    }
    return x;
}

}  // namespace

void Distortion::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate.hz();
    reset();
}

void Distortion::process(std::span<float> block) noexcept {
    if (sampleRate_ == 0) {
        return;
    }
    // Tonalité : passe-bas un pôle. tone=1 → coupure haute (brillant),
    // tone=0 → coupure basse (sombre). Coupure mappée [800, 12000] Hz.
    const float cutoff = 800.0F + (tone_ * 11200.0F);
    const auto nyquist = static_cast<float>(sampleRate_) * 0.5F;
    const float ratio = std::clamp(cutoff / nyquist, 0.0F, 0.99F);
    const auto coeff = static_cast<float>(
        1.0 - std::exp(-2.0 * std::numbers::pi * static_cast<double>(ratio) * 0.5));

    for (float& sample : block) {
        const float dry = sample;
        float wet = shape(dry * drive_, mode_);
        toneState_ += coeff * (wet - toneState_);
        wet = toneState_ * level_;
        sample = (dry * (1.0F - mix_)) + (wet * mix_);
    }
}

void Distortion::reset() noexcept {
    toneState_ = 0.0F;
}

void Distortion::setDrive(float drive) noexcept {
    drive_ = std::clamp(drive, 1.0F, 50.0F);
}

void Distortion::setTone(float tone) noexcept {
    tone_ = std::clamp(tone, 0.0F, 1.0F);
}

void Distortion::setLevel(float level) noexcept {
    level_ = std::clamp(level, 0.0F, 1.0F);
}

void Distortion::setMix(float mix) noexcept {
    mix_ = std::clamp(mix, 0.0F, 1.0F);
}

}  // namespace voicelive::dsp
