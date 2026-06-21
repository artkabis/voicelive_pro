// SPDX-License-Identifier: MIT
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "voicelive/engine/SampleFifo.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::engine::SampleFifo;

TEST(SampleFifo, vide_au_depart) {
    SampleFifo<8> fifo;
    CHECK(fifo.available() == 0U);
    CHECK(fifo.capacity() == 7U);  // une case reservee plein/vide
    float dst[4] = {1, 2, 3, 4};
    CHECK(fifo.read(dst, 4) == 0U);  // rien a lire
}

TEST(SampleFifo, ecrit_puis_lit_fifo) {
    SampleFifo<8> fifo;
    const float src[5] = {1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
    CHECK(fifo.write(src, 5) == 5U);
    CHECK(fifo.available() == 5U);

    float dst[5] = {};
    CHECK(fifo.read(dst, 5) == 5U);
    for (int i = 0; i < 5; ++i) {
        CHECK(dst[i] == src[i]);
    }
    CHECK(fifo.available() == 0U);
}

TEST(SampleFifo, lecture_partielle_quand_sous_rempli) {
    SampleFifo<8> fifo;
    const float src[3] = {10.0F, 20.0F, 30.0F};
    fifo.write(src, 3);

    float dst[6] = {};
    const std::size_t got = fifo.read(dst, 6);  // demande 6, seulement 3 dispo
    CHECK(got == 3U);                           // l'appelant comblerait par du silence
    CHECK(dst[0] == 10.0F);
    CHECK(dst[2] == 30.0F);
}

TEST(SampleFifo, ecriture_partielle_quand_plein) {
    SampleFifo<8> fifo;  // capacite utile = 7
    std::vector<float> src(10);
    for (int i = 0; i < 10; ++i) {
        src[static_cast<std::size_t>(i)] = static_cast<float>(i);
    }
    const std::size_t written = fifo.write(src.data(), 10);
    CHECK(written == 7U);  // seulement 7 cases ; l'appelant decide drop vs retry
    CHECK(fifo.available() == 7U);
}

TEST(SampleFifo, wraparound_preserve_l_ordre) {
    SampleFifo<8> fifo;  // capacite utile = 7
    float next = 0.0F;
    float expect = 0.0F;
    // Boucle longue : force le passage de l'index au-dela de Capacity plusieurs fois.
    for (int iter = 0; iter < 100; ++iter) {
        float src[3] = {next, next + 1.0F, next + 2.0F};
        next += 3.0F;
        REQUIRE(fifo.write(src, 3) == 3U);

        float dst[3] = {};
        REQUIRE(fifo.read(dst, 3) == 3U);
        for (int i = 0; i < 3; ++i) {
            CHECK(dst[i] == expect);
            expect += 1.0F;
        }
    }
    CHECK(fifo.available() == 0U);
}

TEST(SampleFifo, reset_vide_la_file) {
    SampleFifo<8> fifo;
    const float src[4] = {1.0F, 2.0F, 3.0F, 4.0F};
    fifo.write(src, 4);
    CHECK(fifo.available() == 4U);

    fifo.reset();
    CHECK(fifo.available() == 0U);
    float dst[4];
    CHECK(fifo.read(dst, 4) == 0U);
}

// Politique cote appelant : drop a l'over-run (producteur ne reessaie pas).
// Reproduit le comportement de AndroidMicCapture::pushSamples.
TEST(SampleFifo, politique_drop_sur_overrun) {
    SampleFifo<8> fifo;  // capacite utile = 7
    std::uint64_t dropped = 0;
    const float src[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    const std::size_t w = fifo.write(src, 10);
    dropped += 10 - w;  // l'appelant comptabilise lui-meme la perte
    CHECK(w == 7U);
    CHECK(dropped == 3U);
}

// Stress concurrent producteur/consommateur : verifie l'absence de corruption
// et la conservation de l'ordre FIFO sous concurrence reelle (deux threads).
// Le producteur envoie une rampe verifiable ; le producteur REESSAIE quand le
// tampon est plein (contre-pression) => aucune perte attendue.
TEST(SampleFifo, stress_concurrent_spsc) {
    constexpr std::size_t kCapacity = 1024;
    constexpr std::uint64_t kTotal = 2'000'000;  // echantillons a faire transiter
    SampleFifo<kCapacity> fifo;

    std::atomic<bool> producerDone{false};

    std::thread producer([&] {
        std::uint64_t produced = 0;
        float block[64];
        while (produced < kTotal) {
            const std::size_t batch =
                static_cast<std::size_t>(std::min<std::uint64_t>(64, kTotal - produced));
            for (std::size_t i = 0; i < batch; ++i) {
                block[i] = static_cast<float>((produced + i) & 0xFFFFFF);  // motif verifiable
            }
            std::size_t off = 0;
            while (off < batch) {
                off += fifo.write(block + off, batch - off);  // reessaie si plein
            }
            produced += batch;
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::uint64_t consumed = 0;
    bool ordreOk = true;
    float dst[128];
    while (consumed < kTotal) {
        const std::size_t got = fifo.read(dst, 128);
        for (std::size_t i = 0; i < got; ++i) {
            const float expected = static_cast<float>((consumed + i) & 0xFFFFFF);
            if (dst[i] != expected) {
                ordreOk = false;
            }
        }
        consumed += got;
        if (got == 0 && producerDone.load(std::memory_order_acquire) && fifo.available() == 0) {
            break;  // securite : producteur fini et file vide
        }
    }

    producer.join();
    CHECK(ordreOk);
    CHECK(consumed == kTotal);
}
