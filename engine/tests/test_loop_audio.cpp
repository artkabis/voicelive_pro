// SPDX-License-Identifier: MIT
#include <array>
#include <vector>

#include "voicelive/engine/LoopAudio.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::engine::LoopAudio;

TEST(LoopAudio, prepare_alloue_et_vide) {
    LoopAudio loop;
    loop.prepare(100);
    CHECK(loop.capacity() == 100U);
    CHECK(loop.empty());
}

TEST(LoopAudio, append_borne_par_la_capacite) {
    LoopAudio loop;
    loop.prepare(4);
    const std::array<float, 3> first{0.1F, 0.2F, 0.3F};
    CHECK(loop.append(first) == 3U);
    CHECK(loop.length() == 3U);

    const std::array<float, 3> second{0.4F, 0.5F, 0.6F};
    CHECK(loop.append(second) == 1U);  // il ne reste qu'une place
    CHECK(loop.length() == 4U);
    CHECK(loop.sampleAt(3) == 0.4F);
}

TEST(LoopAudio, read_looped_boucle_le_contenu) {
    LoopAudio loop;
    loop.prepare(8);
    const std::array<float, 3> content{1.0F, 2.0F, 3.0F};
    loop.append(content);

    std::vector<float> out(7, 0.0F);
    const std::size_t newPos = loop.readLooped(out, 0);
    // 7 échantillons à partir de 0 sur une boucle de 3 : 1,2,3,1,2,3,1
    CHECK(out[0] == 1.0F);
    CHECK(out[3] == 1.0F);
    CHECK(out[6] == 1.0F);
    CHECK(newPos == 1U);  // (0 + 7) % 3
}

TEST(LoopAudio, read_looped_vide_donne_silence) {
    LoopAudio loop;
    loop.prepare(8);
    std::vector<float> out(4, 9.0F);
    CHECK(loop.readLooped(out, 0) == 0U);
    CHECK(out[0] == 0.0F);
    CHECK(out[3] == 0.0F);
}

TEST(LoopAudio, overdub_ajoute_au_contenu) {
    LoopAudio loop;
    loop.prepare(8);
    const std::array<float, 3> base{1.0F, 1.0F, 1.0F};
    loop.append(base);

    const std::array<float, 3> layer{0.5F, 0.5F, 0.5F};
    loop.overdub(layer, 0);
    CHECK(loop.sampleAt(0) == 1.5F);
    CHECK(loop.sampleAt(1) == 1.5F);
    CHECK(loop.sampleAt(2) == 1.5F);
}

TEST(LoopAudio, clear_remet_a_zero_la_longueur) {
    LoopAudio loop;
    loop.prepare(8);
    const std::array<float, 4> content{1.0F, 2.0F, 3.0F, 4.0F};
    loop.append(content);
    loop.clear();
    CHECK(loop.empty());
    CHECK(loop.capacity() == 8U);  // mémoire conservée
}
