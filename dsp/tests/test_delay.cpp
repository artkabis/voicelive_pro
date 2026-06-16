// SPDX-License-Identifier: MIT
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Delay.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::SampleRate;
using voicelive::dsp::Delay;

TEST(Delay, sans_prepare_laisse_le_signal_intact) {
    Delay delay;
    std::vector<float> block{0.5F, -0.5F, 1.0F};
    delay.process(block);
    CHECK(block[0] == 0.5F);
    CHECK(block[2] == 1.0F);
}

TEST(Delay, mix_zero_est_un_passthrough) {
    Delay delay;
    delay.prepare(SampleRate::studio(), 64);
    delay.setMix(0.0F);
    std::vector<float> block{0.1F, 0.2F, -0.3F};
    delay.process(block);
    CHECK_NEAR(block[0], 0.1F, 1e-6);
    CHECK_NEAR(block[1], 0.2F, 1e-6);
    CHECK_NEAR(block[2], -0.3F, 1e-6);
}

TEST(Delay, retarde_l_impulsion) {
    Delay delay;
    delay.prepare(SampleRate::studio(), 64);
    delay.setMix(1.0F);                      // 100 % wet
    delay.setFeedback(0.0F);                 // pas de réinjection
    delay.setDelaySeconds(2.0F / 48000.0F);  // 2 échantillons

    std::vector<float> block{1.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    delay.process(block);
    CHECK_NEAR(block[0], 0.0F, 1e-6);
    CHECK_NEAR(block[2], 1.0F, 1e-6);  // l'impulsion ressort 2 échantillons plus tard
    CHECK_NEAR(block[3], 0.0F, 1e-6);
}

TEST(Delay, parametres_clampes) {
    Delay delay;
    delay.setDelaySeconds(100.0F);
    delay.setFeedback(5.0F);
    delay.setMix(-1.0F);
    CHECK(delay.delaySeconds() == Delay::kMaxDelaySeconds);
    CHECK(delay.feedback() == 0.95F);
    CHECK(delay.mix() == 0.0F);
}
