// SPDX-License-Identifier: MIT
#include <array>
#include <vector>

#include "voicelive/engine/ChannelUtils.hpp"
#include "voicelive_testing/testing.hpp"

namespace channels = voicelive::engine::channels;

TEST(ChannelUtils, downmix_moyenne_les_canaux) {
    std::array<float, 3> left{1.0F, 1.0F, 1.0F};
    std::array<float, 3> right{0.0F, 0.0F, 0.0F};
    const std::array<const float*, 2> inputs{left.data(), right.data()};

    std::vector<float> mono(3, 0.0F);
    channels::downmixToMono(mono, inputs.data(), 2);
    CHECK_NEAR(mono[0], 0.5F, 1e-6);  // (1 + 0) / 2
    CHECK_NEAR(mono[2], 0.5F, 1e-6);
}

TEST(ChannelUtils, downmix_sans_entree_donne_silence) {
    std::vector<float> mono(4, 9.0F);
    channels::downmixToMono(mono, nullptr, 0);
    CHECK(mono[0] == 0.0F);
    CHECK(mono[3] == 0.0F);
}

TEST(ChannelUtils, spread_recopie_sur_chaque_canal) {
    const std::array<float, 3> mono{0.1F, 0.2F, 0.3F};
    std::array<float, 3> left{};
    std::array<float, 3> right{};
    std::array<float*, 2> outputs{left.data(), right.data()};

    channels::spreadToChannels(outputs.data(), 2, mono);
    CHECK(left[0] == 0.1F);
    CHECK(left[2] == 0.3F);
    CHECK(right[0] == 0.1F);
    CHECK(right[2] == 0.3F);
}

TEST(ChannelUtils, spread_ignore_les_canaux_nuls) {
    const std::array<float, 2> mono{0.5F, 0.5F};
    std::array<float, 2> left{};
    std::array<float*, 2> outputs{left.data(), nullptr};

    channels::spreadToChannels(outputs.data(), 2, mono);  // ne déréférence pas le nul
    CHECK(left[0] == 0.5F);
}

TEST(ChannelUtils, aller_retour_mono_vers_stereo) {
    std::array<float, 2> in{0.4F, 0.6F};
    const std::array<const float*, 1> inputs{in.data()};
    std::vector<float> mono(2, 0.0F);
    channels::downmixToMono(mono, inputs.data(), 1);

    std::array<float, 2> left{};
    std::array<float, 2> right{};
    std::array<float*, 2> outputs{left.data(), right.data()};
    channels::spreadToChannels(outputs.data(), 2, mono);

    CHECK_NEAR(left[0], 0.4F, 1e-6);
    CHECK_NEAR(right[1], 0.6F, 1e-6);
}
