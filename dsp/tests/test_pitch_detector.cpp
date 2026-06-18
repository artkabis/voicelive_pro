// SPDX-License-Identifier: MIT
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/Music.hpp"
#include "voicelive/dsp/PitchDetector.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::SampleRate;
using voicelive::dsp::PitchDetector;

namespace {

std::vector<float> sine(double frequency, double rate, std::size_t count) {
    std::vector<float> out(count, 0.0F);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = static_cast<float>(
            std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(i) / rate));
    }
    return out;
}

}  // namespace

TEST(PitchDetector, detecte_le_la_440) {
    const PitchDetector detector;
    const auto frequency = detector.detect(sine(440.0, 48000.0, 4096), SampleRate::studio());
    REQUIRE(frequency.has_value());
    CHECK_NEAR(*frequency, 440.0, 2.0);
}

TEST(PitchDetector, detecte_le_mi_grave_82hz) {
    const PitchDetector detector;
    const auto frequency = detector.detect(sine(82.41, 48000.0, 8192), SampleRate::studio());
    REQUIRE(frequency.has_value());
    CHECK_NEAR(*frequency, 82.41, 2.0);
}

TEST(PitchDetector, silence_ne_donne_pas_de_hauteur) {
    const PitchDetector detector;
    const std::vector<float> silence(4096, 0.0F);
    CHECK(!detector.detect(silence, SampleRate::studio()).has_value());
}

TEST(PitchDetector, accordeur_mappe_la_hauteur_sur_la_note) {
    const PitchDetector detector;
    const auto frequency = detector.detect(sine(440.0, 48000.0, 4096), SampleRate::studio());
    REQUIRE(frequency.has_value());
    const auto note = voicelive::core::music::fromFrequency(*frequency);
    CHECK(note.midi == 69);  // La4
}
