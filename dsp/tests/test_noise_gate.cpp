// SPDX-License-Identifier: MIT
#include <cmath>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/NoiseGate.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::SampleRate;
using voicelive::dsp::NoiseGate;

TEST(NoiseGate, desactivee_est_un_passthrough) {
    NoiseGate gate;
    gate.prepare(SampleRate::studio(), 64);
    gate.setEnabled(false);
    std::vector<float> block{0.5F, -0.5F, 0.25F};
    gate.process(block);
    CHECK(block[0] == 0.5F);
    CHECK(block[2] == 0.25F);
}

TEST(NoiseGate, ferme_sur_signal_faible) {
    NoiseGate gate;
    gate.prepare(SampleRate::studio(), 64);
    gate.setEnabled(true);
    gate.setThreshold(-20.0F);  // ~0.1 linéaire
    gate.setRelease(1.0F);      // fermeture quasi immédiate
    // Signal très faible, sous le seuil → doit être fortement atténué.
    std::vector<float> block(2000, 0.001F);
    gate.process(block);
    CHECK(std::abs(block.back()) < 0.0005F);
}

TEST(NoiseGate, ouvre_sur_signal_fort) {
    NoiseGate gate;
    gate.prepare(SampleRate::studio(), 64);
    gate.setEnabled(true);
    gate.setThreshold(-40.0F);
    gate.setAttack(0.5F);
    // Signal fort au-dessus du seuil → la porte s'ouvre, le signal passe.
    std::vector<float> block(2000, 0.5F);
    gate.process(block);
    CHECK(std::abs(block.back()) > 0.4F);
    for (const float sample : block) {
        CHECK(std::isfinite(sample));
    }
}

TEST(NoiseGate, parametres_clampes) {
    NoiseGate gate;
    gate.prepare(SampleRate::studio(), 64);
    gate.setThreshold(20.0F);
    gate.setAttack(0.0F);
    gate.setRelease(100000.0F);
    CHECK(gate.thresholdDb() == 0.0F);
    CHECK(gate.attackMs() == 0.1F);
    CHECK(gate.releaseMs() == 1000.0F);
}
