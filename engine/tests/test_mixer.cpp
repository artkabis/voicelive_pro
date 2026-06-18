// SPDX-License-Identifier: MIT
#include <array>
#include <vector>

#include "voicelive/engine/Mixer.hpp"
#include "voicelive_testing/testing.hpp"

namespace mixer = voicelive::engine::mixer;

TEST(Mixer, add_scaled_accumule_avec_gain) {
    std::vector<float> dst{0.0F, 1.0F, 2.0F};
    const std::array<float, 3> src{1.0F, 1.0F, 1.0F};
    mixer::addScaled(dst, src, 0.5F);
    CHECK(dst[0] == 0.5F);
    CHECK(dst[1] == 1.5F);
    CHECK(dst[2] == 2.5F);
}

TEST(Mixer, add_scaled_mixe_plusieurs_sources) {
    std::vector<float> master(4, 0.0F);
    const std::array<float, 4> trackA{0.2F, 0.2F, 0.2F, 0.2F};
    const std::array<float, 4> trackB{0.3F, 0.3F, 0.3F, 0.3F};
    mixer::addScaled(master, trackA, 1.0F);
    mixer::addScaled(master, trackB, 1.0F);
    CHECK_NEAR(master[0], 0.5F, 1e-6);
    CHECK_NEAR(master[3], 0.5F, 1e-6);
}

TEST(Mixer, add_scaled_traite_le_plus_petit_bloc) {
    std::vector<float> dst{1.0F, 1.0F};
    const std::array<float, 4> src{1.0F, 1.0F, 1.0F, 1.0F};
    mixer::addScaled(dst, src, 1.0F);  // ne déborde pas dst
    CHECK(dst[0] == 2.0F);
    CHECK(dst[1] == 2.0F);
}

TEST(Mixer, limit_borne_dans_moins_un_plus_un) {
    std::vector<float> block{-2.0F, -0.5F, 0.5F, 3.0F};
    mixer::limit(block);
    CHECK(block[0] == -1.0F);
    CHECK(block[1] == -0.5F);
    CHECK(block[2] == 0.5F);
    CHECK(block[3] == 1.0F);
}
