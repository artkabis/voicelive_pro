// SPDX-License-Identifier: MIT
#include <cmath>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Compressor.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::SampleRate;
using voicelive::dsp::Compressor;

TEST(Compressor, silence_donne_silence) {
    Compressor comp;
    comp.prepare(SampleRate::studio(), 64);
    std::vector<float> block(64, 0.0F);
    comp.process(block);
    for (const float sample : block) {
        CHECK(sample == 0.0F);
    }
}

TEST(Compressor, signal_sous_le_seuil_passe_inchange) {
    Compressor comp;
    comp.prepare(SampleRate::studio(), 64);
    comp.setThreshold(-18.0F);
    comp.setMakeup(0.0F);
    std::vector<float> block(2400, 0.05F);  // ~ -26 dB, sous le seuil
    comp.process(block);
    CHECK_NEAR(block.back(), 0.05F, 1e-3);
}

TEST(Compressor, signal_fort_est_attenue) {
    Compressor comp;
    comp.prepare(SampleRate::studio(), 64);
    comp.setThreshold(-18.0F);
    comp.setRatio(4.0F);
    comp.setMakeup(0.0F);
    std::vector<float> block(4800, 1.0F);  // 0 dB, bien au-dessus du seuil
    comp.process(block);
    CHECK(std::abs(block.back()) < 0.5F);  // compressé une fois l'attaque passée
}

TEST(Compressor, parametres_clampes) {
    Compressor comp;
    comp.setThreshold(-200.0F);
    comp.setRatio(100.0F);
    comp.setMakeup(99.0F);
    CHECK(comp.threshold() == -60.0F);
    CHECK(comp.ratio() == 20.0F);
    CHECK(comp.makeup() == 24.0F);
}
