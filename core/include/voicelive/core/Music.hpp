// SPDX-License-Identifier: MIT
//
// Conversions musicales pures (12-TET, La4 = A4 = 440 Hz) : fréquence ↔ note +
// écart en cents. Sert de base à l'accordeur. Aucune dépendance.
#pragma once

namespace voicelive::core::music {

/// Note tempérée : numéro MIDI + écart par rapport à la note juste, en cents.
struct Note {
    int midi = 69;       ///< 69 = La4 (A4) = 440 Hz.
    double cents = 0.0;  ///< Écart dans [-50, 50] cents.
};

/// Convertit une fréquence (Hz, > 0) en note la plus proche + cents.
[[nodiscard]] Note fromFrequency(double hertz);

/// Nom de la note dans l'octave (« C », « C#», … « B »).
[[nodiscard]] const char* name(int midi) noexcept;

/// Octave de la note (La4 → 4).
[[nodiscard]] int octave(int midi) noexcept;

}  // namespace voicelive::core::music
