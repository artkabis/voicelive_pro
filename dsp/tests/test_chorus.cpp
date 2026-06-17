// SPDX-License-Identifier: MIT
#include <cmath>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Chorus.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::SampleRate;
using voicelive::dsp::Chorus;

TEST(Chorus, sans_prepare_laisse_le_signal_intact) {
    Chorus chorus;
    std::vector<float> block{0.5F, -0.5F, 1.0F};
    chorus.process(block);
    CHECK(block[0] == 0.5F);
    CHECK(block[2] == 1.0F);
}

TEST(Chorus, mix_zero_est_un_passthrough) {
    Chorus chorus;
    chorus.prepare(SampleRate::studio(), 64);
    chorus.setMix(0.0F);
    std::vector<float> block{0.1F, 0.2F, -0.3F};
    chorus.process(block);
    CHECK_NEAR(block[0], 0.1F, 1e-6);
    CHECK_NEAR(block[2], -0.3F, 1e-6);
}

TEST(Chorus, melange_le_signal_retarde) {
    Chorus chorus;
    chorus.prepare(SampleRate::studio(), 64);
    chorus.setMix(0.5F);
    std::vector<float> block(8, 1.0F);
    chorus.process(block);
    // La ligne de retard est vide au départ → wet ≈ 0, donc out ≈ dry * (1-mix).
    CHECK_NEAR(block[0], 0.5F, 1e-4);
    for (const float sample : block) {
        CHECK(std::isfinite(sample));
    }
}

TEST(Chorus, parametres_clampes) {
    Chorus chorus;
    chorus.setRate(100.0F);
    chorus.setDepth(5.0F);
    chorus.setMix(-1.0F);
    CHECK(chorus.rate() == 10.0F);
    CHECK(chorus.depth() == 1.0F);
    CHECK(chorus.mix() == 0.0F);
}
