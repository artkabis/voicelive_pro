// SPDX-License-Identifier: MIT
//
// Test d'intégration : enchaîne état métier (core) et rendu audio (engine) sur
// un cycle complet, puis démontre le mixage de deux pistes.
#include <array>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/LooperTrack.hpp"
#include "voicelive/engine/Mixer.hpp"
#include "voicelive/engine/TrackProcessor.hpp"
#include "voicelive_testing/testing.hpp"

namespace mixer = voicelive::engine::mixer;
using voicelive::core::Gain;
using voicelive::core::TrackState;
using voicelive::engine::TrackProcessor;

TEST(TrackProcessor, enregistrement_capture_l_entree) {
    TrackProcessor proc;
    proc.prepare(1000);

    REQUIRE(proc.startRecording().ok());
    CHECK(proc.track().state() == TrackState::Recording);

    const std::array<float, 4> input{0.5F, 0.5F, 0.5F, 0.5F};
    std::vector<float> output(4, 0.0F);
    proc.process(output, input);

    CHECK(proc.audio().length() == 4U);
    CHECK(output[0] == 0.5F);  // monitoring : l'entrée ressort
}

TEST(TrackProcessor, lecture_boucle_le_contenu_enregistre) {
    TrackProcessor proc;
    proc.prepare(1000);
    REQUIRE(proc.startRecording().ok());
    const std::array<float, 4> input{0.5F, 0.5F, 0.5F, 0.5F};
    std::vector<float> scratch(4, 0.0F);
    proc.process(scratch, input);
    REQUIRE(proc.finishRecording().ok());
    CHECK(proc.track().state() == TrackState::Playing);

    const std::array<float, 4> silence{};
    std::vector<float> output(4, 0.0F);
    proc.process(output, silence);
    CHECK(output[0] == 0.5F);
    CHECK(output[3] == 0.5F);
}

TEST(TrackProcessor, gain_et_mute_s_appliquent_en_lecture) {
    TrackProcessor proc;
    proc.prepare(1000);
    REQUIRE(proc.startRecording().ok());
    const std::array<float, 4> input{1.0F, 1.0F, 1.0F, 1.0F};
    std::vector<float> scratch(4, 0.0F);
    proc.process(scratch, input);
    REQUIRE(proc.finishRecording().ok());

    const std::array<float, 4> silence{};
    std::vector<float> output(4, 0.0F);

    proc.setGain(Gain::fromLinear(0.5F));
    proc.process(output, silence);
    CHECK_NEAR(output[0], 0.5F, 1e-6);

    proc.setMuted(true);
    proc.process(output, silence);
    CHECK(output[0] == 0.0F);  // muet → silence
}

TEST(TrackProcessor, overdub_superpose_sur_la_boucle) {
    TrackProcessor proc;
    proc.prepare(1000);
    REQUIRE(proc.startRecording().ok());
    const std::array<float, 4> base{1.0F, 1.0F, 1.0F, 1.0F};
    std::vector<float> scratch(4, 0.0F);
    proc.process(scratch, base);
    REQUIRE(proc.finishRecording().ok());
    REQUIRE(proc.startOverdub().ok());
    CHECK(proc.track().state() == TrackState::Overdubbing);

    const std::array<float, 4> layer{0.25F, 0.25F, 0.25F, 0.25F};
    std::vector<float> output(4, 0.0F);
    proc.process(output, layer);

    // La sortie reflète le contenu AVANT overdub...
    CHECK_NEAR(output[0], 1.0F, 1e-6);
    // ...et le stockage contient désormais la couche ajoutée.
    CHECK_NEAR(proc.audio().sampleAt(0), 1.25F, 1e-6);
}

TEST(TrackProcessor, stop_donne_du_silence) {
    TrackProcessor proc;
    proc.prepare(1000);
    REQUIRE(proc.startRecording().ok());
    const std::array<float, 4> input{1.0F, 1.0F, 1.0F, 1.0F};
    std::vector<float> scratch(4, 0.0F);
    proc.process(scratch, input);
    REQUIRE(proc.finishRecording().ok());
    REQUIRE(proc.stop().ok());

    const std::array<float, 4> silence{};
    std::vector<float> output(4, 9.0F);
    proc.process(output, silence);
    CHECK(output[0] == 0.0F);
}

TEST(TrackProcessor, mixage_de_deux_pistes) {
    auto record = [](TrackProcessor& proc, float value) {
        proc.prepare(1000);
        CHECK(proc.startRecording().ok());
        const std::array<float, 4> in{value, value, value, value};
        std::vector<float> scratch(4, 0.0F);
        proc.process(scratch, in);
        CHECK(proc.finishRecording().ok());
    };

    TrackProcessor trackA;
    TrackProcessor trackB;
    record(trackA, 0.3F);
    record(trackB, 0.2F);

    const std::array<float, 4> silence{};
    std::vector<float> blockA(4, 0.0F);
    std::vector<float> blockB(4, 0.0F);
    trackA.process(blockA, silence);
    trackB.process(blockB, silence);

    std::vector<float> master(4, 0.0F);
    mixer::addScaled(master, blockA, 1.0F);
    mixer::addScaled(master, blockB, 1.0F);
    mixer::limit(master);

    CHECK_NEAR(master[0], 0.5F, 1e-6);  // 0.3 + 0.2
    CHECK_NEAR(master[3], 0.5F, 1e-6);
}
