// SPDX-License-Identifier: MIT
#include <string>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/Project.hpp"
#include "voicelive/core/ProjectSerializer.hpp"
#include "voicelive/core/Transport.hpp"
#include "voicelive_testing/testing.hpp"

namespace io = voicelive::core::project_io;
using voicelive::core::Bpm;
using voicelive::core::Gain;
using voicelive::core::Project;
using voicelive::core::SampleRate;
using voicelive::core::TimeSignature;

TEST(ProjectSerializer, round_trip_par_chaine) {
    auto created = Project::create("Ma Session", 3);
    REQUIRE(created.ok());
    Project& project = created.value();
    project.transport().setBpm(Bpm::fromValue(140.0));
    project.transport().setSignature(TimeSignature::create(3, 4).value());
    project.transport().setSampleRate(SampleRate::create(44100).value());
    project.track(0)->setGain(Gain::fromLinear(0.5F));
    project.track(1)->setMuted(true);
    REQUIRE(project.selectTrack(2).ok());

    const std::string text = io::serialize(project);
    auto loaded = io::deserialize(text);
    REQUIRE(loaded.ok());
    const Project& restored = loaded.value();

    CHECK(restored.name() == "Ma Session");
    CHECK(restored.trackCount() == 3U);
    CHECK_NEAR(restored.transport().bpm().value(), 140.0, 1e-6);
    CHECK(restored.transport().signature().numerator() == 3U);
    CHECK(restored.transport().signature().denominator() == 4U);
    CHECK(restored.transport().sampleRate().hz() == 44100U);
    CHECK_NEAR(restored.track(0)->gain().linear(), 0.5F, 1e-6);
    CHECK(restored.track(1)->isMuted());
    CHECK(restored.selectedIndex() == 2U);
}

TEST(ProjectSerializer, rejette_entete_invalide) {
    CHECK(!io::deserialize("MAUVAIS\nbpm=120.0\n").ok());
}

TEST(ProjectSerializer, rejette_champ_requis_manquant) {
    CHECK(!io::deserialize("VOICELIVE_PROJECT v1\nname=x\n").ok());
}

TEST(ProjectSerializer, rejette_nombre_de_pistes_incoherent) {
    const std::string text =
        "VOICELIVE_PROJECT v1\nname=x\nbpm=120.0\ntime_signature=4/4\n"
        "sample_rate=48000\nselected=0\ntrack_count=2\ntrack=1.0,0\n";  // 1 piste pour 2
    CHECK(!io::deserialize(text).ok());
}

TEST(ProjectSerializer, round_trip_par_fichier) {
    auto created = Project::create("Disque", 2);
    REQUIRE(created.ok());
    const std::string path = "vlpro_serializer_test.vlp";

    REQUIRE(io::saveToFile(path, created.value()).ok());
    auto loaded = io::loadFromFile(path);
    REQUIRE(loaded.ok());
    CHECK(loaded.value().name() == "Disque");
    CHECK(loaded.value().trackCount() == 2U);
}

TEST(ProjectSerializer, fichier_absent_donne_erreur) {
    CHECK(!io::loadFromFile("chemin/inexistant/projet.vlp").ok());
}
