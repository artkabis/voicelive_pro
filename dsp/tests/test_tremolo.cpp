// SPDX-License-Identifier: MIT
#include <cmath>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Tremolo.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::SampleRate;
using voicelive::dsp::Tremolo;

TEST(Tremolo, sans_prepare_laisse_le_signal_intact) {
    Tremolo trem;
    std::vector<float> block{0.5F, -0.5F, 1.0F};
    trem.process(block);
    CHECK(block[0] == 0.5F);
    CHECK(block[2] == 1.0F);
}

TEST(Tremolo, mix_zero_est_un_passthrough) {
    Tremolo trem;
    trem.prepare(SampleRate::studio(), 64);
    trem.setMix(0.0F);
    std::vector<float> block{0.1F, 0.2F, -0.3F};
    trem.process(block);
    CHECK_NEAR(block[0], 0.1F, 1e-6);
    CHECK_NEAR(block[2], -0.3F, 1e-6);
}

TEST(Tremolo, module_l_amplitude) {
    Tremolo trem;
    trem.prepare(SampleRate::studio(), 64);
    trem.setRate(5.0F);
    trem.setDepth(1.0F);
    trem.setMix(1.0F);
    // Entrée constante : la sortie doit varier dans le temps (modulation d'amplitude).
    std::vector<float> block(static_cast<std::size_t>(SampleRate::kStudio / 5), 1.0F);
    trem.process(block);
    float minV = block[0];
    float maxV = block[0];
    for (const float sample : block) {
        CHECK(std::isfinite(sample));
        CHECK(sample >= -1e-4F);  // gain ∈ [0, 1] → reste positif
        minV = std::min(minV, sample);
        maxV = std::max(maxV, sample);
    }
    CHECK(maxV - minV > 0.5F);  // forte profondeur → grande variation
}

TEST(Tremolo, parametres_clampes) {
    Tremolo trem;
    trem.setRate(100.0F);
    trem.setDepth(5.0F);
    trem.setMix(-1.0F);
    CHECK(trem.rate() == 20.0F);
    CHECK(trem.depth() == 1.0F);
    CHECK(trem.mix() == 0.0F);
}
