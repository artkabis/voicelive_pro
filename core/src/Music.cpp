// SPDX-License-Identifier: MIT
#include "voicelive/core/Music.hpp"

#include <array>
#include <cmath>
#include <cstddef>

namespace voicelive::core::music {
namespace {
constexpr std::array<const char*, 12> kNames{"C",  "C#", "D",  "D#", "E",  "F",
                                             "F#", "G",  "G#", "A",  "A#", "B"};
}  // namespace

Note fromFrequency(double hertz) {
    if (hertz <= 0.0) {
        return Note{0, 0.0};
    }
    const double midiFloat = 69.0 + (12.0 * std::log2(hertz / 440.0));
    const int midi = static_cast<int>(std::lround(midiFloat));
    const double cents = (midiFloat - static_cast<double>(midi)) * 100.0;
    return Note{midi, cents};
}

const char* name(int midi) noexcept {
    const int index = (((midi % 12) + 12) % 12);
    return kNames[static_cast<std::size_t>(index)];
}

int octave(int midi) noexcept {
    // Division plancher pour les MIDI négatifs : en C++, -1/12 == 0 (tronqué vers 0),
    // mais on veut -1 → on soustrait 11 avant de diviser (équivalent floor division).
    const int floored = (midi >= 0) ? (midi / 12) : ((midi - 11) / 12);
    return floored - 1;  // MIDI 69 (La) → octave 4
}

}  // namespace voicelive::core::music
