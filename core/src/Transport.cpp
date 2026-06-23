// SPDX-License-Identifier: MIT
#include "voicelive/core/Transport.hpp"

#include <array>
#include <cmath>
#include <cstddef>

namespace voicelive::core {

std::size_t Transport::quantizeToGrid(double lengthSamples, Grid grid) const noexcept {
    const double gridLength = (grid == Grid::Bar) ? samplesPerBar() : samplesPerBeat();
    if (gridLength <= 0.0 || lengthSamples <= 0.0) {
        return 0;
    }
    double units = std::round(lengthSamples / gridLength);
    if (units < 1.0) {
        units = 1.0;
    }
    return static_cast<std::size_t>(std::llround(units * gridLength));
}

std::size_t Transport::chooseLoopMultiple(std::size_t recorded, std::size_t reference) noexcept {
    if (reference == 0) {
        return recorded;
    }

    // Multiples musicaux supportés : de ¼ boucle (fill) à 4× (pont long).
    constexpr std::array<double, 5> kMultiples{0.25, 0.5, 1.0, 2.0, 4.0};
    const auto recordedF = static_cast<double>(recorded);
    const auto referenceF = static_cast<double>(reference);

    double bestCandidate = referenceF;
    double bestError = std::abs(recordedF - referenceF);
    for (const double multiple : kMultiples) {
        const double candidate = referenceF * multiple;
        const double error = std::abs(recordedF - candidate);
        if (error < bestError) {
            bestError = error;
            bestCandidate = candidate;
        }
    }
    return static_cast<std::size_t>(std::llround(bestCandidate));
}

}  // namespace voicelive::core
