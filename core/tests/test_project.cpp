// SPDX-License-Identifier: MIT
#include "voicelive/core/Project.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::ErrorCode;
using voicelive::core::Project;
using voicelive::core::TrackState;

TEST(Project, creation_valide) {
    auto result = Project::create("Ma session", 3);
    REQUIRE(result.ok());
    const auto& project = result.value();
    CHECK(project.name() == "Ma session");
    CHECK(project.trackCount() == 3U);
    CHECK(project.selectedIndex() == 0U);
}

TEST(Project, refuse_nombre_de_pistes_invalide) {
    CHECK(!Project::create("x", 0).ok());
    CHECK(!Project::create("x", 9).ok());
    CHECK(Project::create("x", 0).error().code == ErrorCode::OutOfRange);
}

TEST(Project, acces_piste_borne) {
    auto result = Project::create("s", 2);
    REQUIRE(result.ok());
    auto& project = result.value();

    CHECK(project.track(0) != nullptr);
    CHECK(project.track(1) != nullptr);
    CHECK(project.track(2) == nullptr);  // hors bornes → nullptr, jamais d'UB
}

TEST(Project, selection_piste_gardee) {
    auto result = Project::create("s", 3);
    REQUIRE(result.ok());
    auto& project = result.value();

    REQUIRE(project.selectTrack(2).ok());
    CHECK(project.selectedIndex() == 2U);

    CHECK(!project.selectTrack(3).ok());   // hors bornes rejeté
    CHECK(project.selectedIndex() == 2U);  // sélection inchangée
}

TEST(Project, pistes_independantes_et_mutables) {
    auto result = Project::create("s", 2);
    REQUIRE(result.ok());
    auto& project = result.value();

    REQUIRE(project.track(0)->record().ok());
    CHECK(project.track(0)->state() == TrackState::Recording);
    CHECK(project.track(1)->state() == TrackState::Empty);  // l'autre piste intacte
}
