// SPDX-License-Identifier: MIT
#include <fstream>
#include <ios>
#include <string>

#include "voicelive/engine/WavFile.hpp"
#include "voicelive_testing/testing.hpp"

namespace wav = voicelive::engine::wav;
using voicelive::engine::wav::AudioData;

TEST(WavFile, frame_count) {
    AudioData data;
    data.channels = 2;
    data.samples.resize(8);
    CHECK(data.frameCount() == 4U);
}

TEST(WavFile, ecrit_et_relit_mono) {
    AudioData data;
    data.channels = 1;
    data.sampleRate = 48000;
    data.samples = {0.0F, 0.5F, -0.5F, 0.25F};

    const std::string path = "vlpro_wav_mono.wav";
    REQUIRE(wav::write(path, data).ok());

    auto loaded = wav::read(path);
    REQUIRE(loaded.ok());
    const AudioData& restored = loaded.value();
    CHECK(restored.channels == 1U);
    CHECK(restored.sampleRate == 48000U);
    REQUIRE(restored.samples.size() == 4U);
    CHECK_NEAR(restored.samples[1], 0.5F, 1e-3);  // tolérance quantif. 16 bits
    CHECK_NEAR(restored.samples[2], -0.5F, 1e-3);
    CHECK_NEAR(restored.samples[3], 0.25F, 1e-3);
}

TEST(WavFile, conserve_les_canaux_stereo) {
    AudioData data;
    data.channels = 2;
    data.sampleRate = 44100;
    data.samples = {0.1F, 0.2F, 0.3F, 0.4F};  // 2 frames stéréo

    const std::string path = "vlpro_wav_stereo.wav";
    REQUIRE(wav::write(path, data).ok());

    auto loaded = wav::read(path);
    REQUIRE(loaded.ok());
    CHECK(loaded.value().channels == 2U);
    CHECK(loaded.value().sampleRate == 44100U);
    CHECK(loaded.value().frameCount() == 2U);
}

TEST(WavFile, parametres_invalides_rejetes) {
    AudioData data;
    data.channels = 0;  // invalide
    CHECK(!wav::write("vlpro_wav_bad.wav", data).ok());
}

TEST(WavFile, fichier_absent_donne_erreur) {
    CHECK(!wav::read("chemin/inexistant/audio.wav").ok());
}

TEST(WavFile, donnees_non_wav_rejetees) {
    const std::string path = "vlpro_notwav.bin";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "PAS-UN-WAV-DU-TOUT";
    file.close();
    CHECK(!wav::read(path).ok());
}
