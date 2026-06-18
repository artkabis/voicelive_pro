// SPDX-License-Identifier: MIT
#include <string>

#include "voicelive/core/Music.hpp"
#include "voicelive_testing/testing.hpp"

namespace music = voicelive::core::music;

TEST(Music, la4_vaut_440hz) {
    const auto note = music::fromFrequency(440.0);
    CHECK(note.midi == 69);
    CHECK_NEAR(note.cents, 0.0, 1.0);
}

TEST(Music, do4_vaut_261hz) {
    CHECK(music::fromFrequency(261.6256).midi == 60);
}

TEST(Music, ecart_en_cents) {
    // Légèrement au-dessus du La : cents > 0.
    const auto note = music::fromFrequency(445.0);
    CHECK(note.midi == 69);
    CHECK(note.cents > 0.0);
}

TEST(Music, nom_et_octave) {
    CHECK(std::string{music::name(69)} == "A");
    CHECK(music::octave(69) == 4);
    CHECK(std::string{music::name(60)} == "C");
    CHECK(music::octave(60) == 4);
}

TEST(Music, frequence_invalide_est_sure) {
    const auto note = music::fromFrequency(0.0);
    CHECK(note.midi == 0);  // pas de log2 de zéro, pas de crash
}
