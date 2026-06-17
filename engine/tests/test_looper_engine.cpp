// SPDX-License-Identifier: MIT
//
// Tests du moteur assemblé : configuration, contrôle synchrone et via la file
// de commandes lock-free, mixage multi-pistes, et pont avec core::Project.
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/Project.hpp"
#include "voicelive/core/Transport.hpp"
#include "voicelive/dsp/Delay.hpp"
#include "voicelive/engine/LooperEngine.hpp"
#include "voicelive/engine/WavFile.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::Bpm;
using voicelive::core::ErrorCode;
using voicelive::core::Gain;
using voicelive::core::Project;
using voicelive::core::SampleRate;
using voicelive::core::TrackState;
using voicelive::dsp::Delay;
using voicelive::engine::EngineCommand;
using voicelive::engine::LooperEngine;

namespace {

void initEngine(LooperEngine& engine, std::size_t trackCount) {
    REQUIRE(engine.prepare(SampleRate::studio(), trackCount, 1000, 64).ok());
}

}  // namespace

TEST(LooperEngine, prepare_valide_le_nombre_de_pistes) {
    LooperEngine engine;
    CHECK(!engine.prepare(SampleRate::studio(), 0, 100, 64).ok());
    CHECK(!engine.prepare(SampleRate::studio(), 99, 100, 64).ok());
    REQUIRE(engine.prepare(SampleRate::studio(), 3, 100, 64).ok());
    CHECK(engine.trackCount() == 3U);
}

TEST(LooperEngine, controle_synchrone_rejette_index_invalide) {
    LooperEngine engine;
    initEngine(engine, 2);
    const auto status = engine.recordTrack(5);
    CHECK(!status.ok());
    CHECK(status.error().code == ErrorCode::OutOfRange);
}

TEST(LooperEngine, cycle_enregistrement_lecture) {
    LooperEngine engine;
    initEngine(engine, 1);
    REQUIRE(engine.recordTrack(0).ok());

    const std::array<float, 4> input{0.5F, 0.5F, 0.5F, 0.5F};
    std::vector<float> output(4, 0.0F);
    engine.process(output, input);  // enregistre
    REQUIRE(engine.finishRecordingTrack(0).ok());
    CHECK(engine.track(0)->track().state() == TrackState::Playing);

    const std::array<float, 4> silence{};
    engine.process(output, silence);  // lit la boucle
    CHECK_NEAR(output[0], 0.5F, 1e-6);
}

TEST(LooperEngine, file_de_commandes_lock_free) {
    LooperEngine engine;
    initEngine(engine, 1);

    CHECK(engine.post({EngineCommand::Action::Record, 0}));
    const std::array<float, 4> input{0.5F, 0.5F, 0.5F, 0.5F};
    std::vector<float> output(4, 0.0F);
    engine.process(output, input);  // draine Record puis enregistre
    CHECK(engine.track(0)->track().state() == TrackState::Recording);
    CHECK(engine.track(0)->audio().length() == 4U);

    CHECK(engine.post({EngineCommand::Action::FinishRecording, 0}));
    const std::array<float, 4> silence{};
    engine.process(output, silence);  // draine Finish puis lit
    CHECK(engine.track(0)->track().state() == TrackState::Playing);
    CHECK_NEAR(output[0], 0.5F, 1e-6);
}

TEST(LooperEngine, mixage_de_deux_pistes) {
    LooperEngine engine;
    initEngine(engine, 2);
    std::vector<float> scratch(4, 0.0F);

    REQUIRE(engine.recordTrack(0).ok());
    engine.process(scratch, std::array<float, 4>{0.3F, 0.3F, 0.3F, 0.3F});
    REQUIRE(engine.finishRecordingTrack(0).ok());

    REQUIRE(engine.recordTrack(1).ok());
    engine.process(scratch, std::array<float, 4>{0.2F, 0.2F, 0.2F, 0.2F});
    REQUIRE(engine.finishRecordingTrack(1).ok());

    const std::array<float, 4> silence{};
    std::vector<float> output(4, 0.0F);
    engine.process(output, silence);
    CHECK_NEAR(output[0], 0.5F, 1e-6);  // 0.3 + 0.2
    CHECK_NEAR(output[3], 0.5F, 1e-6);
}

TEST(LooperEngine, export_reglages_vers_project) {
    LooperEngine engine;
    initEngine(engine, 3);
    engine.setName("Session A");
    engine.transport().setBpm(Bpm::fromValue(140.0));
    REQUIRE(engine.setTrackGain(1, Gain::fromLinear(0.5F)).ok());
    REQUIRE(engine.setTrackMuted(2, true).ok());
    REQUIRE(engine.selectTrack(2).ok());

    auto result = engine.exportSettings();
    REQUIRE(result.ok());
    const Project& project = result.value();
    CHECK(project.name() == "Session A");
    CHECK(project.trackCount() == 3U);
    CHECK(project.transport().bpm().value() == 140.0);
    CHECK(project.track(1)->gain().linear() == 0.5F);
    CHECK(project.track(2)->isMuted());
    CHECK(project.selectedIndex() == 2U);
}

TEST(LooperEngine, apply_reglages_depuis_project) {
    auto result = Project::create("Session B", 2);
    REQUIRE(result.ok());
    Project& project = result.value();
    project.transport().setBpm(Bpm::fromValue(90.0));
    project.track(0)->setMuted(true);

    LooperEngine engine;
    initEngine(engine, 2);
    REQUIRE(engine.applySettings(project).ok());
    CHECK(engine.name() == "Session B");
    CHECK(engine.transport().bpm().value() == 90.0);
    CHECK(engine.track(0)->track().isMuted());
}

TEST(LooperEngine, synchronisation_aligne_sur_la_piste_maitre) {
    LooperEngine engine;
    initEngine(engine, 2);
    std::vector<float> scratch(8, 0.0F);

    // Piste 0 = maître : 4 échantillons enregistrés.
    REQUIRE(engine.recordTrack(0).ok());
    engine.process(scratch, std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F});
    REQUIRE(engine.finishRecordingTrack(0).ok());
    CHECK(engine.masterLoopLength() == 4U);
    CHECK(engine.track(0)->audio().loopLength() == 4U);

    // Piste 1 : 7 échantillons enregistrés → alignés sur 2× le maître = 8.
    REQUIRE(engine.recordTrack(1).ok());
    engine.process(scratch, std::array<float, 7>{1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F});
    REQUIRE(engine.finishRecordingTrack(1).ok());
    CHECK(engine.track(1)->audio().length() == 7U);      // réellement enregistré
    CHECK(engine.track(1)->audio().loopLength() == 8U);  // boucle alignée (2×)
    CHECK(engine.masterLoopLength() == 4U);              // maître inchangé
}

TEST(LooperEngine, metronome_se_mixe_dans_la_sortie) {
    LooperEngine engine;
    initEngine(engine, 1);
    engine.setMetronomeEnabled(true);
    CHECK(engine.isMetronomeEnabled());

    // Aucune piste enregistrée : la seule source de son est le métronome.
    const std::array<float, 64> silence{};
    std::vector<float> output(64, 0.0F);
    engine.process(output, silence);

    float peak = 0.0F;
    for (const float sample : output) {
        peak = std::max(peak, std::abs(sample));
    }
    CHECK(peak > 0.0F);  // un clic est présent au premier temps
}

TEST(LooperEngine, import_charge_un_sample_mono_dans_une_piste) {
    LooperEngine engine;
    initEngine(engine, 2);

    voicelive::engine::wav::AudioData data;
    data.channels = 1;
    data.sampleRate = 48000;
    data.samples = {0.2F, 0.2F, 0.2F, 0.2F};

    REQUIRE(engine.importTrack(0, data).ok());
    CHECK(engine.track(0)->track().state() == TrackState::Playing);
    CHECK(engine.track(0)->audio().length() == 4U);

    const std::array<float, 4> silence{};
    std::vector<float> out(4, 0.0F);
    engine.process(out, silence);
    CHECK_NEAR(out[0], 0.2F, 1e-6);  // le sample importé est joué
}

TEST(LooperEngine, import_downmixe_le_stereo_en_mono) {
    LooperEngine engine;
    initEngine(engine, 1);

    voicelive::engine::wav::AudioData data;
    data.channels = 2;
    data.samples = {0.0F, 0.4F, 0.0F, 0.4F};  // 2 frames → moyenne 0.2

    REQUIRE(engine.importTrack(0, data).ok());
    CHECK(engine.track(0)->audio().length() == 2U);

    const std::array<float, 2> silence{};
    std::vector<float> out(2, 0.0F);
    engine.process(out, silence);
    CHECK_NEAR(out[0], 0.2F, 1e-6);
}

TEST(LooperEngine, import_index_invalide_rejete) {
    LooperEngine engine;
    initEngine(engine, 1);
    voicelive::engine::wav::AudioData data;
    data.channels = 1;
    data.samples = {0.1F};
    CHECK(!engine.importTrack(9, data).ok());
}

TEST(LooperEngine, import_depuis_fichier_wav) {
    LooperEngine engine;
    initEngine(engine, 1);

    voicelive::engine::wav::AudioData data;
    data.channels = 1;
    data.samples = {0.5F, 0.5F};
    REQUIRE(voicelive::engine::wav::write("vlpro_import.wav", data).ok());

    REQUIRE(engine.importTrackFromFile(0, "vlpro_import.wav").ok());
    CHECK(engine.track(0)->audio().length() == 2U);
    CHECK(engine.track(0)->track().state() == TrackState::Playing);
}

TEST(LooperEngine, import_fichier_absent_rejete) {
    LooperEngine engine;
    initEngine(engine, 1);
    CHECK(!engine.importTrackFromFile(0, "chemin/inexistant/sample.wav").ok());
}

TEST(LooperEngine, chaine_d_effets_par_piste) {
    LooperEngine engine;
    initEngine(engine, 1);

    auto* effects = engine.effectsForTrack(0);
    REQUIRE(effects != nullptr);
    effects->add(std::make_unique<Delay>());
    CHECK(engine.effectsForTrack(0)->size() == 1U);

    CHECK(engine.effectsForTrack(5) == nullptr);  // index hors bornes
}
