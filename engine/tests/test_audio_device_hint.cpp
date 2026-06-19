// SPDX-License-Identifier: MIT
#include "voicelive/engine/AudioDeviceHint.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::engine::looksLikeHeadphones;

TEST(AudioDeviceHint, usbc_detecte_via_nom_device) {
    CHECK(looksLikeHeadphones("USB Audio Device", "Android Audio"));
    CHECK(looksLikeHeadphones("USB-C Headphones", "Android Audio"));
    CHECK(looksLikeHeadphones("My USB DAC", ""));
}

TEST(AudioDeviceHint, usbc_detecte_via_type) {
    CHECK(looksLikeHeadphones("", "USB Audio"));
    CHECK(looksLikeHeadphones("Android Audio", "USB Audio"));
}

TEST(AudioDeviceHint, casque_jack_detecte) {
    CHECK(looksLikeHeadphones("Wired Headset", "Android Audio"));
    CHECK(looksLikeHeadphones("Headphone Output", ""));
    CHECK(looksLikeHeadphones("Earphone Jack", ""));
    CHECK(looksLikeHeadphones("Wired Output", ""));
}

TEST(AudioDeviceHint, haut_parleur_non_detecte) {
    CHECK(!looksLikeHeadphones("Android Audio Output", "Android Audio"));
    CHECK(!looksLikeHeadphones("Built-in Speaker", "CoreAudio"));
    CHECK(!looksLikeHeadphones("Internal Speaker", "Android Audio"));
    CHECK(!looksLikeHeadphones("", ""));
}

TEST(AudioDeviceHint, insensible_a_la_casse) {
    CHECK(looksLikeHeadphones("USB AUDIO DEVICE", ""));
    CHECK(looksLikeHeadphones("HEADSET", ""));
    CHECK(looksLikeHeadphones("usb audio", ""));
    CHECK(looksLikeHeadphones("headphones", ""));
    CHECK(!looksLikeHeadphones("SPEAKER", "ANDROID AUDIO"));
}

TEST(AudioDeviceHint, chaines_vides_safe) {
    CHECK(!looksLikeHeadphones("", ""));
    CHECK(!looksLikeHeadphones("Speaker", ""));
}
