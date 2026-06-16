// SPDX-License-Identifier: MIT
//
// Mixer — primitives de mixage temps réel (sommation pondérée + limiteur).
// Fonctions pures, `noexcept`, sans allocation : utilisables dans le callback.
#pragma once

#include <span>

namespace voicelive::engine::mixer {

/// Accumule `source * gain` dans `destination` (mix additif). Traite le plus
/// petit des deux blocs.
void addScaled(std::span<float> destination, std::span<const float> source, float gain) noexcept;

/// Limiteur dur : borne chaque échantillon dans [-1, 1] (anti-clipping de sortie).
void limit(std::span<float> block) noexcept;

}  // namespace voicelive::engine::mixer
