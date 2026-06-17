// SPDX-License-Identifier: MIT
#include <cmath>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Wah.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::SampleRate;
using voicelive::dsp::Wah;

TEST(Wah, silence_en_entree_donne_silence) {
    Wah wah;
    wah.prepare(SampleRate::studio(), 64);
    std::vector<float> block(32, 0.0F);
    wah.process(block);
    for (const float sample : block) {
        CHECK(sample == 0.0F);
    }
}

TEST(Wah, mix_zero_est_un_passthrough) {
    Wah wah;
    wah.prepare(SampleRate::studio(), 64);
    wah.setMix(0.0F);
    std::vector<float> block{0.2F, -0.4F, 0.6F};
    wah.process(block);
    CHECK_NEAR(block[0], 0.2F, 1e-6);
    CHECK_NEAR(block[1], -0.4F, 1e-6);
    CHECK_NEAR(block[2], 0.6F, 1e-6);
}

TEST(Wah, sortie_finie_et_bornee_sur_un_signal) {
    Wah wah;
    wah.prepare(SampleRate::studio(), 256);
    wah.setMix(1.0F);
    wah.setResonance(8.0F);

    std::vector<float> block(256, 0.0F);
    for (std::size_t i = 0; i < block.size(); ++i) {
        block[i] = (i % 2 == 0) ? 0.5F : -0.5F;  // signal alterné
    }
    wah.process(block);
    for (const float sample : block) {
        CHECK(std::isfinite(sample));  // filtre stable, jamais de NaN/Inf
    }
}

TEST(Wah, parametres_clampes) {
    Wah wah;
    wah.setMinFrequency(1.0F);
    wah.setMaxFrequency(99999.0F);
    wah.setResonance(100.0F);
    wah.setMix(2.0F);
    CHECK(wah.minFrequency() == 20.0F);
    CHECK(wah.maxFrequency() == 8000.0F);
    CHECK(wah.resonance() == 20.0F);
    CHECK(wah.mix() == 1.0F);
}
