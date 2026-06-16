// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Reverb.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::SampleRate;
using voicelive::dsp::Reverb;

namespace {

float blockEnergy(const std::vector<float>& block) {
    float energy = 0.0F;
    for (const float sample : block) {
        energy += std::abs(sample);
    }
    return energy;
}

bool allFinite(const std::vector<float>& block) {
    return std::ranges::all_of(block, [](float sample) { return std::isfinite(sample); });
}

}  // namespace

TEST(Reverb, sans_prepare_laisse_le_signal_intact) {
    Reverb reverb;
    std::vector<float> block{0.5F, -0.5F, 1.0F};
    reverb.process(block);  // prepare() non appelé
    CHECK(block[0] == 0.5F);
    CHECK(block[1] == -0.5F);
    CHECK(block[2] == 1.0F);
}

TEST(Reverb, silence_en_entree_donne_silence_en_sortie) {
    Reverb reverb;
    reverb.prepare(SampleRate::studio(), 256);
    std::vector<float> block(256, 0.0F);
    reverb.process(block);
    CHECK(blockEnergy(block) == 0.0F);
}

TEST(Reverb, passthrough_dry_pur) {
    Reverb reverb;
    reverb.prepare(SampleRate::studio(), 4);
    reverb.setDry(1.0F);
    reverb.setWet(0.0F);
    std::vector<float> block{0.1F, 0.2F, -0.3F, 0.4F};
    reverb.process(block);
    CHECK_NEAR(block[0], 0.1F, 1e-6);
    CHECK_NEAR(block[1], 0.2F, 1e-6);
    CHECK_NEAR(block[2], -0.3F, 1e-6);
    CHECK_NEAR(block[3], 0.4F, 1e-6);
}

TEST(Reverb, parametres_clampes_0_1) {
    Reverb reverb;
    reverb.setRoomSize(5.0F);
    reverb.setWet(-1.0F);
    CHECK(reverb.roomSize() == 1.0F);
    CHECK(reverb.wet() == 0.0F);
}

TEST(Reverb, reponse_impulsionnelle_decroit) {
    Reverb reverb;
    reverb.prepare(SampleRate::studio(), 512);
    reverb.setRoomSize(0.6F);
    reverb.setWet(0.5F);
    reverb.setDry(0.5F);

    constexpr std::size_t kBlock = 512;
    constexpr std::size_t kBlocks = 60;

    // Bloc 0 : une impulsion, puis du silence.
    std::vector<float> block(kBlock, 0.0F);
    block[0] = 1.0F;
    reverb.process(block);
    REQUIRE(allFinite(block));

    std::vector<float> energies;
    energies.reserve(kBlocks);
    for (std::size_t b = 0; b < kBlocks; ++b) {
        std::fill(block.begin(), block.end(), 0.0F);
        reverb.process(block);
        REQUIRE(allFinite(block));  // jamais de NaN/Inf
        energies.push_back(blockEnergy(block));
    }

    // La queue de réverbération existe...
    const float earlyTail = energies[3] + energies[4] + energies[5];
    // ...et décroît dans le temps (filtre stable).
    const float lateTail = energies[45] + energies[46] + energies[47];

    CHECK(earlyTail > 0.0F);
    CHECK(lateTail < earlyTail);
}

TEST(Reverb, reset_efface_la_queue) {
    Reverb reverb;
    reverb.prepare(SampleRate::studio(), 256);

    std::vector<float> impulse(256, 0.0F);
    impulse[0] = 1.0F;
    reverb.process(impulse);
    reverb.reset();

    std::vector<float> block(256, 0.0F);
    reverb.process(block);
    CHECK(blockEnergy(block) == 0.0F);  // plus aucune énergie résiduelle
}
