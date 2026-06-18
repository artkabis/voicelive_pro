// SPDX-License-Identifier: MIT
#include "voicelive/core/Transport.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::Bpm;
using voicelive::core::Grid;
using voicelive::core::TimeSignature;
using voicelive::core::Transport;

TEST(Bpm, defaut_120) {
    CHECK(Bpm{}.value() == 120.0);
}

TEST(Bpm, clampe_les_bornes) {
    CHECK(Bpm::fromValue(1.0).value() == Bpm::kMin);
    CHECK(Bpm::fromValue(100000.0).value() == Bpm::kMax);
    CHECK(Bpm::fromValue(128.0).value() == 128.0);
}

TEST(TimeSignature, defaut_4_4) {
    const TimeSignature sig;
    CHECK(sig.numerator() == 4U);
    CHECK(sig.denominator() == 4U);
    CHECK(sig.beatsPerBar() == 4.0);
}

TEST(TimeSignature, refuse_denominateur_non_puissance_de_deux) {
    CHECK(!TimeSignature::create(4, 3).ok());
    CHECK(!TimeSignature::create(0, 4).ok());
    REQUIRE(TimeSignature::create(6, 8).ok());
    CHECK(TimeSignature::create(6, 8).value().beatsPerBar() == 3.0);  // 6/8 = 3 noires
}

TEST(Transport, samples_par_temps_a_120bpm_48k) {
    const Transport t;  // 120 BPM, 4/4, 48 kHz
    // 120 BPM → 0,5 s/temps → 24000 échantillons/temps à 48 kHz.
    CHECK_NEAR(t.samplesPerBeat(), 24000.0, 1e-6);
    CHECK_NEAR(t.samplesPerBar(), 96000.0, 1e-6);
}

TEST(Transport, samples_par_temps_suivent_le_tempo) {
    Transport t;
    t.setBpm(Bpm::fromValue(60.0));  // 1 s/temps
    CHECK_NEAR(t.samplesPerBeat(), 48000.0, 1e-6);
}

TEST(Transport, etat_lecture) {
    Transport t;
    CHECK(!t.isPlaying());
    t.play();
    CHECK(t.isPlaying());
    t.stop();
    CHECK(!t.isPlaying());
}

TEST(Transport, quantize_to_grid_arrondit_au_temps) {
    const Transport t;                                       // 24000 échantillons/temps
    CHECK(t.quantizeToGrid(25000.0, Grid::Beat) == 24000U);  // arrondi vers le bas
    CHECK(t.quantizeToGrid(36000.0, Grid::Beat) == 48000U);  // arrondi vers le haut (2 temps)
    CHECK(t.quantizeToGrid(100.0, Grid::Beat) == 24000U);    // minimum 1 temps
    CHECK(t.quantizeToGrid(0.0, Grid::Beat) == 0U);          // longueur nulle
}

TEST(Transport, choose_loop_multiple_aligne_sur_la_reference) {
    const std::size_t master = 48000;
    CHECK(Transport::chooseLoopMultiple(47000, master) == master);       // ≈ 1×
    CHECK(Transport::chooseLoopMultiple(95000, master) == 2 * master);   // ≈ 2×
    CHECK(Transport::chooseLoopMultiple(25000, master) == master / 2);   // ≈ ½×
    CHECK(Transport::chooseLoopMultiple(190000, master) == 4 * master);  // ≈ 4×
    CHECK(Transport::chooseLoopMultiple(33333, 0) == 33333U);            // pas de référence
}
