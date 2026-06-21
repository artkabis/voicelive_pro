// SPDX-License-Identifier: MIT
#include <cmath>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Flanger.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::SampleRate;
using voicelive::dsp::Flanger;

TEST(Flanger, sans_prepare_laisse_le_signal_intact) {
    Flanger flanger;
    std::vector<float> block{0.5F, -0.5F, 1.0F};
    flanger.process(block);
    CHECK(block[0] == 0.5F);
    CHECK(block[2] == 1.0F);
}

TEST(Flanger, mix_zero_est_un_passthrough) {
    Flanger flanger;
    flanger.prepare(SampleRate::studio(), 64);
    flanger.setMix(0.0F);
    std::vector<float> block{0.1F, 0.2F, -0.3F};
    flanger.process(block);
    CHECK_NEAR(block[0], 0.1F, 1e-6);
    CHECK_NEAR(block[2], -0.3F, 1e-6);
}

TEST(Flanger, reste_stable_avec_feedback) {
    Flanger flanger;
    flanger.prepare(SampleRate::studio(), 64);
    flanger.setRate(0.5F);
    flanger.setDepth(1.0F);
    flanger.setFeedback(0.9F);
    flanger.setMix(0.5F);
    std::vector<float> block(4096, 0.5F);
    flanger.process(block);
    for (const float sample : block) {
        CHECK(std::isfinite(sample));
        CHECK(std::abs(sample) < 10.0F);
    }
}

TEST(Flanger, parametres_clampes) {
    Flanger flanger;
    flanger.setRate(100.0F);
    flanger.setDepth(5.0F);
    flanger.setFeedback(2.0F);
    flanger.setMix(-1.0F);
    CHECK(flanger.rate() == 10.0F);
    CHECK(flanger.depth() == 1.0F);
    CHECK(flanger.feedback() == 0.95F);
    CHECK(flanger.mix() == 0.0F);
}
