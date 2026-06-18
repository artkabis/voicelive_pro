// SPDX-License-Identifier: MIT
#include <memory>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Delay.hpp"
#include "voicelive/dsp/EffectChain.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::SampleRate;
using voicelive::dsp::Delay;
using voicelive::dsp::EffectChain;

TEST(EffectChain, vide_est_un_passthrough) {
    EffectChain chain;
    chain.prepare(SampleRate::studio(), 64);
    CHECK(chain.empty());
    std::vector<float> block{0.1F, 0.2F, 0.3F};
    chain.process(block);
    CHECK(block[0] == 0.1F);
    CHECK(block[2] == 0.3F);
}

TEST(EffectChain, ignore_un_effet_nul) {
    EffectChain chain;
    chain.add(nullptr);
    CHECK(chain.empty());
}

TEST(EffectChain, applique_un_effet_ajoute) {
    EffectChain chain;
    chain.prepare(SampleRate::studio(), 64);

    auto delay = std::make_unique<Delay>();
    delay->setMix(1.0F);
    delay->setFeedback(0.0F);
    delay->setDelaySeconds(2.0F / 48000.0F);
    chain.add(std::move(delay));  // ajouté après prepare → préparé automatiquement
    CHECK(chain.size() == 1U);

    std::vector<float> block{1.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    chain.process(block);
    CHECK_NEAR(block[2], 1.0F, 1e-6);  // l'effet a bien été appliqué (retard de 2)
}
