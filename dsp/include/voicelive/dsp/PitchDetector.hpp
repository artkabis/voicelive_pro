// SPDX-License-Identifier: MIT
//
// PitchDetector — estimation de la hauteur fondamentale (Hz) d'un signal mono,
// par fonction de différence normalisée (NSDF, méthode McLeod). Base de
// l'accordeur. Analyse hors temps réel (sur une fenêtre d'échantillons).
#pragma once

#include <optional>
#include <span>

#include "voicelive/core/AudioParams.hpp"

namespace voicelive::dsp {

class PitchDetector {
public:
    /// Bornes de recherche de la fondamentale (Hz).
    void setRange(double minHertz, double maxHertz) noexcept;

    /// Estime la fondamentale d'une fenêtre. `std::nullopt` si le signal est
    /// trop faible ou si aucune hauteur stable n'est trouvée.
    [[nodiscard]] std::optional<double> detect(std::span<const float> samples,
                                               core::SampleRate sampleRate) const;

private:
    double minHertz_ = 50.0;
    double maxHertz_ = 1500.0;
};

}  // namespace voicelive::dsp
