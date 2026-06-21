// SPDX-License-Identifier: MIT
#include "voicelive/dsp/Equalizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>

#include "voicelive/core/AudioParams.hpp"

namespace voicelive::dsp {
namespace {

constexpr double kLowFreq = 120.0;
constexpr double kMidFreq = 1000.0;
constexpr double kHighFreq = 6000.0;
constexpr double kMidQ = 0.9;  // Q du filtre peak médium : large (~1,5 octave), adapté au mix

struct Coeffs {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
};

Coeffs normalise(double b0, double b1, double b2, double a0, double a1, double a2) {
    return Coeffs{static_cast<float>(b0 / a0), static_cast<float>(b1 / a0),
                  static_cast<float>(b2 / a0), static_cast<float>(a1 / a0),
                  static_cast<float>(a2 / a0)};
}

// Coefficients RBJ « Audio EQ Cookbook ».
Coeffs makeLowShelf(double freq, double gainDb, double rate) {
    const double amp = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * std::numbers::pi * freq / rate;
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / 2.0 * std::numbers::sqrt2;
    const double twoSqrtAlpha = 2.0 * std::sqrt(amp) * alpha;
    return normalise(amp * ((amp + 1.0) - ((amp - 1.0) * cosw0) + twoSqrtAlpha),
                     2.0 * amp * ((amp - 1.0) - ((amp + 1.0) * cosw0)),
                     amp * ((amp + 1.0) - ((amp - 1.0) * cosw0) - twoSqrtAlpha),
                     (amp + 1.0) + ((amp - 1.0) * cosw0) + twoSqrtAlpha,
                     -2.0 * ((amp - 1.0) + ((amp + 1.0) * cosw0)),
                     (amp + 1.0) + ((amp - 1.0) * cosw0) - twoSqrtAlpha);
}

Coeffs makeHighShelf(double freq, double gainDb, double rate) {
    const double amp = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * std::numbers::pi * freq / rate;
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / 2.0 * std::numbers::sqrt2;
    const double twoSqrtAlpha = 2.0 * std::sqrt(amp) * alpha;
    return normalise(amp * ((amp + 1.0) + ((amp - 1.0) * cosw0) + twoSqrtAlpha),
                     -2.0 * amp * ((amp - 1.0) + ((amp + 1.0) * cosw0)),
                     amp * ((amp + 1.0) + ((amp - 1.0) * cosw0) - twoSqrtAlpha),
                     (amp + 1.0) - ((amp - 1.0) * cosw0) + twoSqrtAlpha,
                     2.0 * ((amp - 1.0) - ((amp + 1.0) * cosw0)),
                     (amp + 1.0) - ((amp - 1.0) * cosw0) - twoSqrtAlpha);
}

Coeffs makePeaking(double freq, double gainDb, double quality, double rate) {
    const double amp = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * std::numbers::pi * freq / rate;
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * quality);
    return normalise(1.0 + (alpha * amp), -2.0 * cosw0, 1.0 - (alpha * amp), 1.0 + (alpha / amp),
                     -2.0 * cosw0, 1.0 - (alpha / amp));
}

}  // namespace

void Equalizer::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate.hz();
    reset();
    updateCoefficients();
}

void Equalizer::updateCoefficients() noexcept {
    const auto rate = static_cast<double>(sampleRate_);
    const auto assign = [](Biquad& biquad, const Coeffs& coeffs) noexcept {
        biquad.b0 = coeffs.b0;
        biquad.b1 = coeffs.b1;
        biquad.b2 = coeffs.b2;
        biquad.a1 = coeffs.a1;
        biquad.a2 = coeffs.a2;
    };
    assign(low_, makeLowShelf(kLowFreq, static_cast<double>(lowGainDb_), rate));
    assign(mid_, makePeaking(kMidFreq, static_cast<double>(midGainDb_), kMidQ, rate));
    assign(high_, makeHighShelf(kHighFreq, static_cast<double>(highGainDb_), rate));
}

void Equalizer::process(std::span<float> block) noexcept {
    for (float& sample : block) {
        sample = high_.process(mid_.process(low_.process(sample)));
    }
}

void Equalizer::reset() noexcept {
    low_.clear();
    mid_.clear();
    high_.clear();
}

void Equalizer::setLowGain(float decibels) noexcept {
    lowGainDb_ = std::clamp(decibels, -24.0F, 24.0F);
    updateCoefficients();
}

void Equalizer::setMidGain(float decibels) noexcept {
    midGainDb_ = std::clamp(decibels, -24.0F, 24.0F);
    updateCoefficients();
}

void Equalizer::setHighGain(float decibels) noexcept {
    highGainDb_ = std::clamp(decibels, -24.0F, 24.0F);
    updateCoefficients();
}

}  // namespace voicelive::dsp
