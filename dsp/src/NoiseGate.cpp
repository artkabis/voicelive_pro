// SPDX-License-Identifier: MIT
#include "voicelive/dsp/NoiseGate.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"

namespace voicelive::dsp {
namespace {

/// Coefficient de lissage exponentiel pour une constante de temps donnée (ms).
float timeCoeff(float ms, unsigned sampleRate) noexcept {
    const double tau = static_cast<double>(ms) * 0.001 * static_cast<double>(sampleRate);
    if (tau <= 0.0) {
        return 1.0F;
    }
    return static_cast<float>(1.0 - std::exp(-1.0 / tau));
}

}  // namespace

void NoiseGate::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate.hz();
    updateCoeffs();
    reset();
}

void NoiseGate::updateCoeffs() noexcept {
    if (sampleRate_ == 0) {
        return;
    }
    attackCoeff_ = timeCoeff(attackMs_, sampleRate_);
    releaseCoeff_ = timeCoeff(releaseMs_, sampleRate_);
    envCoeff_ = timeCoeff(2.0F, sampleRate_);  // détecteur d'enveloppe rapide (~2 ms)
}

void NoiseGate::process(std::span<float> block) noexcept {
    if (!enabled_ || sampleRate_ == 0) {
        return;
    }
    const float threshold = std::pow(10.0F, thresholdDb_ * 0.05F);  // dB → linéaire

    for (float& sample : block) {
        const float rectified = std::abs(sample);
        env_ += envCoeff_ * (rectified - env_);

        // Cible : porte ouverte (1) au-dessus du seuil, fermée (0) en dessous.
        const float target = env_ >= threshold ? 1.0F : 0.0F;
        const float coeff = target > gain_ ? attackCoeff_ : releaseCoeff_;
        gain_ += coeff * (target - gain_);

        sample *= gain_;
    }
}

void NoiseGate::reset() noexcept {
    env_ = 0.0F;
    gain_ = 0.0F;
}

void NoiseGate::setThreshold(float thresholdDb) noexcept {
    thresholdDb_ = std::clamp(thresholdDb, -80.0F, 0.0F);
}

void NoiseGate::setAttack(float ms) noexcept {
    attackMs_ = std::clamp(ms, 0.1F, 100.0F);
    updateCoeffs();
}

void NoiseGate::setRelease(float ms) noexcept {
    releaseMs_ = std::clamp(ms, 1.0F, 1000.0F);
    updateCoeffs();
}

}  // namespace voicelive::dsp
