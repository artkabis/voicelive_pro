// SPDX-License-Identifier: MIT
#include <cmath>
#include <cstddef>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/Transport.hpp"
#include "voicelive/engine/Metronome.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::SampleRate;
using voicelive::core::Transport;
using voicelive::engine::Metronome;

namespace {

float windowPeak(const std::vector<float>& buffer, std::size_t begin, std::size_t end) {
    float peak = 0.0F;
    for (std::size_t i = begin; i < end && i < buffer.size(); ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

}  // namespace

TEST(Metronome, desactive_ne_produit_rien) {
    Metronome metronome;
    metronome.prepare(SampleRate::studio(), 512);
    const Transport transport;  // 120 BPM, 4/4
    std::vector<float> out(1000, 0.0F);
    metronome.process(out, transport);
    CHECK(windowPeak(out, 0, out.size()) == 0.0F);
}

TEST(Metronome, clic_au_premier_temps) {
    Metronome metronome;
    metronome.prepare(SampleRate::studio(), 4096);
    metronome.setEnabled(true);
    const Transport transport;
    std::vector<float> out(2000, 0.0F);
    metronome.process(out, transport);
    CHECK(windowPeak(out, 0, 2000) > 0.0F);  // un clic démarre au temps 0
}

TEST(Metronome, clics_espaces_d_un_temps) {
    Metronome metronome;
    metronome.prepare(SampleRate::studio(), 48000);
    metronome.setEnabled(true);
    const Transport transport;  // 120 BPM @ 48k -> 24000 échantillons/temps
    std::vector<float> out(48000, 0.0F);
    metronome.process(out, transport);

    CHECK(windowPeak(out, 0, 2000) > 0.0F);        // temps 0
    CHECK(windowPeak(out, 23500, 25500) > 0.0F);   // temps 1 (~24000)
    CHECK(windowPeak(out, 11000, 13000) == 0.0F);  // milieu de temps : silence
}

TEST(Metronome, accent_sur_le_premier_temps_de_la_mesure) {
    Metronome metronome;
    metronome.prepare(SampleRate::studio(), 48000);
    metronome.setEnabled(true);
    const Transport transport;  // 4/4
    std::vector<float> out(48000, 0.0F);
    metronome.process(out, transport);

    const float accentPeak = windowPeak(out, 0, 2000);       // temps 0 (accent)
    const float normalPeak = windowPeak(out, 23500, 25500);  // temps 1 (normal)
    CHECK(accentPeak > normalPeak);
}
