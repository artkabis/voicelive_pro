// SPDX-License-Identifier: MIT
#include <cmath>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Phaser.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::SampleRate;
using voicelive::dsp::Phaser;

TEST(Phaser, sans_prepare_laisse_le_signal_intact) {
    Phaser phaser;
    std::vector<float> block{0.5F, -0.5F, 1.0F};
    phaser.process(block);
    CHECK(block[0] == 0.5F);
    CHECK(block[2] == 1.0F);
}

TEST(Phaser, mix_zero_est_un_passthrough) {
    Phaser phaser;
    phaser.prepare(SampleRate::studio(), 64);
    phaser.setMix(0.0F);
    std::vector<float> block{0.1F, 0.2F, -0.3F};
    phaser.process(block);
    CHECK_NEAR(block[0], 0.1F, 1e-6);
    CHECK_NEAR(block[2], -0.3F, 1e-6);
}

TEST(Phaser, reste_stable_et_borne) {
    Phaser phaser;
    phaser.prepare(SampleRate::studio(), 64);
    phaser.setRate(1.0F);
    phaser.setDepth(1.0F);
    phaser.setFeedback(0.9F);
    phaser.setMix(0.5F);
    std::vector<float> block(4096, 0.7F);
    phaser.process(block);
    for (const float sample : block) {
        CHECK(std::isfinite(sample));
        CHECK(std::abs(sample) < 10.0F);
    }
}

TEST(Phaser, parametres_clampes) {
    Phaser phaser;
    phaser.setRate(100.0F);
    phaser.setDepth(5.0F);
    phaser.setFeedback(2.0F);
    phaser.setMix(-1.0F);
    CHECK(phaser.rate() == 10.0F);
    CHECK(phaser.depth() == 1.0F);
    CHECK(phaser.feedback() == 0.95F);
    CHECK(phaser.mix() == 0.0F);
}
