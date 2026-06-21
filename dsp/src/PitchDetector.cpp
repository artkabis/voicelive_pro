// SPDX-License-Identifier: MIT
#include "voicelive/dsp/PitchDetector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "voicelive/core/AudioParams.hpp"

namespace voicelive::dsp {
namespace {

constexpr double kRmsFloor = 1.0e-4;  // en deçà : signal jugé silencieux (évite de détecter sur du bruit)
constexpr double kPeakRatio = 0.9;    // seuil du premier pic NSDF : 90 % du max global évite les harmoniques

/// Fonction de différence carrée normalisée pour les retards [minLag, maxLag].
std::vector<double> computeNsdf(std::span<const float> samples, std::size_t minLag,
                                std::size_t maxLag) {
    const std::size_t count = samples.size();
    std::vector<double> nsdf(maxLag + 1, 0.0);
    for (std::size_t tau = minLag; tau <= maxLag; ++tau) {
        double numerator = 0.0;
        double denominator = 0.0;
        for (std::size_t i = 0; (i + tau) < count; ++i) {
            const auto a = static_cast<double>(samples[i]);
            const auto b = static_cast<double>(samples[i + tau]);
            numerator += a * b;
            denominator += (a * a) + (b * b);
        }
        nsdf[tau] = (denominator > 0.0) ? ((2.0 * numerator) / denominator) : 0.0;
    }
    return nsdf;
}

double rms(std::span<const float> samples) {
    double energy = 0.0;
    for (const float sample : samples) {
        const auto value = static_cast<double>(sample);
        energy += value * value;
    }
    return std::sqrt(energy / static_cast<double>(samples.size()));
}

}  // namespace

void PitchDetector::setRange(double minHertz, double maxHertz) noexcept {
    if (minHertz > 0.0 && maxHertz > minHertz) {
        minHertz_ = minHertz;
        maxHertz_ = maxHertz;
    }
}

std::optional<double> PitchDetector::detect(std::span<const float> samples,
                                            core::SampleRate sampleRate) const {
    const std::size_t count = samples.size();
    if (count < 4 || rms(samples) < kRmsFloor) {
        return std::nullopt;
    }

    const auto rate = static_cast<double>(sampleRate.hz());
    const auto minLag = std::max<std::size_t>(2, static_cast<std::size_t>(rate / maxHertz_));
    const auto maxLag =
        std::min<std::size_t>(count - 1, static_cast<std::size_t>(rate / minHertz_));
    if (minLag >= maxLag) {
        return std::nullopt;
    }

    const std::vector<double> nsdf = computeNsdf(samples, minLag, maxLag);

    double globalMax = 0.0;
    for (std::size_t tau = minLag; tau <= maxLag; ++tau) {
        globalMax = std::max(globalMax, nsdf[tau]);
    }
    if (globalMax <= 0.0) {
        return std::nullopt;
    }

    // Premier maximum local dépassant le seuil : évite les erreurs d'octave.
    const double threshold = kPeakRatio * globalMax;
    std::size_t bestTau = 0;
    for (std::size_t tau = minLag + 1; tau < maxLag; ++tau) {
        if (nsdf[tau] >= threshold && nsdf[tau] >= nsdf[tau - 1] && nsdf[tau] >= nsdf[tau + 1]) {
            bestTau = tau;
            break;
        }
    }
    if (bestTau == 0) {
        return std::nullopt;
    }

    // Interpolation parabolique pour une précision sous-échantillon.
    const double left = nsdf[bestTau - 1];
    const double mid = nsdf[bestTau];
    const double right = nsdf[bestTau + 1];
    const double denom = (left - (2.0 * mid)) + right;
    const double shift = (denom != 0.0) ? ((0.5 * (left - right)) / denom) : 0.0;
    const double refinedLag = static_cast<double>(bestTau) + shift;
    return rate / refinedLag;
}

}  // namespace voicelive::dsp
