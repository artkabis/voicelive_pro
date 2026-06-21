// SPDX-License-Identifier: MIT
#include <cmath>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Distortion.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::SampleRate;
using voicelive::dsp::Distortion;

TEST(Distortion, sans_prepare_laisse_le_signal_intact) {
    Distortion dist;
    std::vector<float> block{0.5F, -0.5F, 1.0F};
    dist.process(block);
    CHECK(block[0] == 0.5F);
    CHECK(block[2] == 1.0F);
}

TEST(Distortion, mix_zero_est_un_passthrough) {
    Distortion dist;
    dist.prepare(SampleRate::studio(), 64);
    dist.setMix(0.0F);
    std::vector<float> block{0.1F, 0.2F, -0.3F};
    dist.process(block);
    CHECK_NEAR(block[0], 0.1F, 1e-6);
    CHECK_NEAR(block[2], -0.3F, 1e-6);
}

TEST(Distortion, sature_les_fortes_amplitudes) {
    Distortion dist;
    dist.prepare(SampleRate::studio(), 64);
    dist.setMode(Distortion::Mode::SoftClip);
    dist.setDrive(20.0F);
    dist.setTone(1.0F);
    dist.setLevel(1.0F);
    dist.setMix(1.0F);
    std::vector<float> block(64, 0.9F);
    dist.process(block);
    // tanh(0.9*20) sature près de 1 → sortie bornée et finie.
    for (const float sample : block) {
        CHECK(std::isfinite(sample));
        CHECK(std::abs(sample) <= 1.0F);
    }
}

TEST(Distortion, hard_clip_borne_la_sortie) {
    Distortion dist;
    dist.prepare(SampleRate::studio(), 64);
    dist.setMode(Distortion::Mode::HardClip);
    dist.setDrive(10.0F);
    dist.setTone(1.0F);
    dist.setLevel(1.0F);
    dist.setMix(1.0F);
    std::vector<float> block(32, 1.0F);
    dist.process(block);
    for (const float sample : block) {
        CHECK(std::abs(sample) <= 1.0F + 1e-4F);
    }
}

TEST(Distortion, parametres_clampes) {
    Distortion dist;
    dist.setDrive(1000.0F);
    dist.setTone(5.0F);
    dist.setLevel(-1.0F);
    dist.setMix(2.0F);
    CHECK(dist.drive() == 50.0F);
    CHECK(dist.tone() == 1.0F);
    CHECK(dist.level() == 0.0F);
    CHECK(dist.mix() == 1.0F);
}
