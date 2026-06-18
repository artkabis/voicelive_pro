// SPDX-License-Identifier: MIT
#include "voicelive/engine/RingBuffer.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::engine::RingBuffer;

TEST(RingBuffer, vide_au_depart) {
    RingBuffer<int> ring(4);
    CHECK(ring.empty());
    CHECK(ring.capacity() == 4U);
    int out = -1;
    CHECK(!ring.pop(out));  // rien à retirer
}

TEST(RingBuffer, fifo_ordonne) {
    RingBuffer<int> ring(4);
    CHECK(ring.push(10));
    CHECK(ring.push(20));
    CHECK(ring.push(30));

    int out = 0;
    REQUIRE(ring.pop(out));
    CHECK(out == 10);
    REQUIRE(ring.pop(out));
    CHECK(out == 20);
    REQUIRE(ring.pop(out));
    CHECK(out == 30);
    CHECK(ring.empty());
}

TEST(RingBuffer, refuse_quand_plein) {
    RingBuffer<int> ring(2);
    CHECK(ring.push(1));
    CHECK(ring.push(2));
    CHECK(!ring.push(3));  // plein → refusé, jamais bloquant
}

TEST(RingBuffer, reutilisable_apres_wraparound) {
    RingBuffer<int> ring(2);
    int out = 0;
    for (int i = 0; i < 10; ++i) {
        REQUIRE(ring.push(i));
        REQUIRE(ring.pop(out));
        CHECK(out == i);
    }
    CHECK(ring.empty());
}
