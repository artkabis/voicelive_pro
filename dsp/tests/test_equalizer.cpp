// SPDX-License-Identifier: MIT
#include <cmath>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Equalizer.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::SampleRate;
using voicelive::dsp::Equalizer;

TEST(Equalizer, plat_est_un_passthrough) {
    Equalizer eq;
    eq.prepare(SampleRate::studio(), 64);  // tous gains à 0 dB → identité
    std::vector<float> block{0.1F, -0.2F, 0.3F, -0.4F, 0.5F};
    const std::vector<float> input = block;
    eq.process(block);
    for (std::size_t i = 0; i < block.size(); ++i) {
        CHECK_NEAR(block[i], input[i], 1e-5);
    }
}

TEST(Equalizer, boost_grave_amplifie_le_continu) {
    Equalizer eq;
    eq.prepare(SampleRate::studio(), 64);
    eq.setLowGain(12.0F);  // +12 dB dans le grave (le shelf agit jusqu'au DC)
    std::vector<float> block(2000, 0.5F);
    eq.process(block);
    CHECK(block.back() > 1.0F);  // régime établi nettement amplifié
}

TEST(Equalizer, sortie_finie_sur_un_signal) {
    Equalizer eq;
    eq.prepare(SampleRate::studio(), 256);
    eq.setHighGain(6.0F);
    eq.setMidGain(-6.0F);
    std::vector<float> block(256, 0.0F);
    for (std::size_t i = 0; i < block.size(); ++i) {
        block[i] = (i % 2 == 0) ? 0.4F : -0.4F;
    }
    eq.process(block);
    for (const float sample : block) {
        CHECK(std::isfinite(sample));
    }
}

TEST(Equalizer, gains_clampes) {
    Equalizer eq;
    eq.setLowGain(100.0F);
    eq.setHighGain(-100.0F);
    CHECK(eq.lowGain() == 24.0F);
    CHECK(eq.highGain() == -24.0F);
}
