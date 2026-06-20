// SPDX-License-Identifier: MIT
// Tests unitaires pour dsp::BpmDetector.
// Stratégie : pistes de clics synthétiques (impulsions aux positions de temps
// exactes) → résultat déterministe et indépendant de données audio réelles.
#include <cmath>
#include <cstddef>
#include <vector>

#include "voicelive/dsp/BpmDetector.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::dsp::BpmDetector;

namespace {

// Génère une piste de clics à `bpm` battements/min.
// Chaque temps = impulsion d'amplitude 1.0 à la position exacte (arrondie).
std::vector<float> makeClickTrack(float bpm, float durationSec, unsigned sr = 48000) {
    const auto N = static_cast<std::size_t>(durationSec * static_cast<float>(sr));
    std::vector<float> buf(N, 0.0F);
    const float samplesPerBeat = 60.0F / bpm * static_cast<float>(sr);
    for (std::size_t beat = 0;; ++beat) {
        const auto pos = static_cast<std::size_t>(static_cast<float>(beat) * samplesPerBeat);
        if (pos >= N) {
            break;
        }
        buf[pos] = 1.0F;
    }
    return buf;
}

// Bruit pseudo-aléatoire déterministe (LCG), sans dépendance extérieure.
std::vector<float> makeNoise(std::size_t n, float amplitude) {
    std::vector<float> buf(n);
    unsigned state = 0xDEADBEEFU;
    for (float& s : buf) {
        state = state * 1664525U + 1013904223U;
        // Convertir en [-1, 1] puis amplifier
        const float v =
            static_cast<float>(static_cast<int>(state >> 1)) / static_cast<float>(0x40000000);
        s = v * amplitude;
    }
    return buf;
}

constexpr float kBpmTol = 5.0F;  // tolérance ±5 BPM admise

}  // namespace

// ─── Cas limites ──────────────────────────────────────────────────────────────

TEST(BpmDetector, silence_returns_nullopt) {
    BpmDetector det;
    std::vector<float> silence(std::size_t{48000} * 5, 0.0F);
    REQUIRE(!det.detect(silence).has_value());
}

TEST(BpmDetector, too_short_returns_nullopt) {
    BpmDetector det;
    // 0.5 s à 120 BPM = seulement 1 temps → insuffisant
    const auto click = makeClickTrack(120.0F, 0.5F);
    REQUIRE(!det.detect(click).has_value());
}

TEST(BpmDetector, noise_floor_returns_nullopt) {
    BpmDetector det;
    // Bruit très faible, sous le plancher minRms par défaut (1e-4)
    const auto noise = makeNoise(std::size_t{48000} * 8, 1e-5F);
    REQUIRE(!det.detect(noise).has_value());
}

TEST(BpmDetector, empty_buffer_returns_nullopt) {
    BpmDetector det;
    std::vector<float> empty;
    REQUIRE(!det.detect(empty).has_value());
}

// ─── Détection de tempo ───────────────────────────────────────────────────────

TEST(BpmDetector, detects_80_bpm) {
    BpmDetector det;
    const auto click = makeClickTrack(80.0F, 12.0F);
    const auto bpm = det.detect(click);
    REQUIRE(bpm.has_value());
    CHECK_NEAR(*bpm, 80.0F, kBpmTol);
}

TEST(BpmDetector, detects_90_bpm) {
    BpmDetector det;
    const auto click = makeClickTrack(90.0F, 10.0F);
    const auto bpm = det.detect(click);
    REQUIRE(bpm.has_value());
    CHECK_NEAR(*bpm, 90.0F, kBpmTol);
}

TEST(BpmDetector, detects_100_bpm) {
    BpmDetector det;
    const auto click = makeClickTrack(100.0F, 8.0F);
    const auto bpm = det.detect(click);
    REQUIRE(bpm.has_value());
    CHECK_NEAR(*bpm, 100.0F, kBpmTol);
}

TEST(BpmDetector, detects_120_bpm) {
    BpmDetector det;
    const auto click = makeClickTrack(120.0F, 8.0F);
    const auto bpm = det.detect(click);
    REQUIRE(bpm.has_value());
    CHECK_NEAR(*bpm, 120.0F, kBpmTol);
}

TEST(BpmDetector, detects_140_bpm) {
    BpmDetector det;
    const auto click = makeClickTrack(140.0F, 6.0F);
    const auto bpm = det.detect(click);
    REQUIRE(bpm.has_value());
    CHECK_NEAR(*bpm, 140.0F, kBpmTol);
}

TEST(BpmDetector, detects_160_bpm) {
    BpmDetector det;
    const auto click = makeClickTrack(160.0F, 6.0F);
    const auto bpm = det.detect(click);
    REQUIRE(bpm.has_value());
    CHECK_NEAR(*bpm, 160.0F, kBpmTol);
}

// ─── Robustesse ───────────────────────────────────────────────────────────────

TEST(BpmDetector, robust_to_low_noise) {
    BpmDetector det;
    auto click = makeClickTrack(120.0F, 8.0F);
    const auto noise = makeNoise(click.size(), 1e-3F);  // bruit léger
    for (std::size_t i = 0; i < click.size(); ++i) {
        click[i] += noise[i];
    }
    const auto bpm = det.detect(click);
    REQUIRE(bpm.has_value());
    CHECK_NEAR(*bpm, 120.0F, kBpmTol);
}

TEST(BpmDetector, works_at_44100_hz) {
    BpmDetector::Config cfg;
    cfg.sampleRate = 44100;
    BpmDetector det{cfg};
    const auto click = makeClickTrack(110.0F, 10.0F, 44100);
    const auto bpm = det.detect(click);
    REQUIRE(bpm.has_value());
    CHECK_NEAR(*bpm, 110.0F, kBpmTol);
}

TEST(BpmDetector, custom_bpm_range_200_bpm) {
    BpmDetector::Config cfg;
    cfg.maxBpm = 220.0F;
    BpmDetector det{cfg};
    const auto click = makeClickTrack(200.0F, 5.0F);
    const auto bpm = det.detect(click);
    REQUIRE(bpm.has_value());
    // À 200 BPM, la correction d'octave peut ramener à 100 BPM — tolérer les deux
    const bool nearExpected =
        std::abs(*bpm - 200.0F) < kBpmTol || std::abs(*bpm - 100.0F) < kBpmTol;
    CHECK(nearExpected);
}

// ─── phaseOffset ──────────────────────────────────────────────────────────────

TEST(BpmDetector, phase_offset_same_track_is_zero) {
    BpmDetector det;
    const auto trackA = makeClickTrack(120.0F, 8.0F);
    const auto off = det.phaseOffset(trackA, trackA);
    REQUIRE(off.has_value());
    CHECK(*off == 0);
}

TEST(BpmDetector, phase_offset_different_tempos_nullopt) {
    BpmDetector det;
    const auto trackA = makeClickTrack(120.0F, 8.0F);
    const auto trackB = makeClickTrack(90.0F, 8.0F);
    // Tempos différents → pas de mesure de phase cohérente
    REQUIRE(!det.phaseOffset(trackA, trackB).has_value());
}

TEST(BpmDetector, phase_offset_half_beat_shift) {
    BpmDetector det;
    // Piste A : temps à 0, 24000, 48000, ...
    // Piste B : même tempo, décalée d'un demi-temps (12000 samples)
    auto trackA = makeClickTrack(120.0F, 8.0F);
    // Construire B en copiant A décalée
    const std::size_t shift = 12000;  // demi-temps à 120 BPM @ 48 kHz
    std::vector<float> trackB(trackA.size(), 0.0F);
    for (std::size_t i = shift; i < trackA.size(); ++i) {
        trackB[i] = trackA[i - shift];
    }
    const auto off = det.phaseOffset(trackA, trackB);
    REQUIRE(off.has_value());
    // Le décalage doit être ≤ 1 demi-temps (12000 samples) par la corrélation croisée
    CHECK(*off <= static_cast<int>(shift + 512));
}
