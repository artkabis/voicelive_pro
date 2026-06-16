// SPDX-License-Identifier: MIT
//
// Couverture exhaustive de la machine à états LooperTrack : on vérifie chaque
// transition valide ET on prouve que chaque transition interdite est rejetée
// sans muter l'état (le contrat « impossible d'atteindre un état incohérent »).
#include "voicelive/core/LooperTrack.hpp"
#include "voicelive_testing/testing.hpp"

using voicelive::core::ErrorCode;
using voicelive::core::Gain;
using voicelive::core::LooperTrack;
using voicelive::core::TrackState;

TEST(LooperTrack, demarre_vide) {
    LooperTrack track;
    CHECK(track.state() == TrackState::Empty);
    CHECK(!track.hasContent());
    CHECK(!track.isAudible());
    CHECK(!track.isMuted());
    CHECK(track.gain() == Gain::unity());
}

TEST(LooperTrack, cycle_nominal_enregistrement_lecture) {
    LooperTrack track;

    REQUIRE(track.record().ok());
    CHECK(track.state() == TrackState::Recording);

    REQUIRE(track.finishRecording().ok());
    CHECK(track.state() == TrackState::Playing);
    CHECK(track.hasContent());
    CHECK(track.isAudible());

    REQUIRE(track.stop().ok());
    CHECK(track.state() == TrackState::Stopped);
    CHECK(track.hasContent());
    CHECK(!track.isAudible());

    REQUIRE(track.play().ok());
    CHECK(track.state() == TrackState::Playing);
}

TEST(LooperTrack, cycle_overdub) {
    LooperTrack track;
    REQUIRE(track.record().ok());
    REQUIRE(track.finishRecording().ok());

    REQUIRE(track.startOverdub().ok());
    CHECK(track.state() == TrackState::Overdubbing);
    CHECK(track.isAudible());

    REQUIRE(track.stopOverdub().ok());
    CHECK(track.state() == TrackState::Playing);
}

TEST(LooperTrack, stop_depuis_overdub) {
    LooperTrack track;
    REQUIRE(track.record().ok());
    REQUIRE(track.finishRecording().ok());
    REQUIRE(track.startOverdub().ok());

    REQUIRE(track.stop().ok());
    CHECK(track.state() == TrackState::Stopped);
}

TEST(LooperTrack, clear_depuis_nimporte_quel_etat) {
    LooperTrack track;
    REQUIRE(track.record().ok());
    track.clear();
    CHECK(track.state() == TrackState::Empty);

    REQUIRE(track.record().ok());
    REQUIRE(track.finishRecording().ok());
    track.clear();
    CHECK(track.state() == TrackState::Empty);
    CHECK(!track.hasContent());
}

// --- Transitions interdites : doivent échouer SANS changer l'état ----------

TEST(LooperTrack, lecture_piste_vide_interdite) {
    LooperTrack track;
    const auto status = track.play();
    CHECK(!status.ok());
    CHECK(status.error().code == ErrorCode::InvalidTransition);
    CHECK(!status.error().message.empty());     // message statique renseigné
    CHECK(track.state() == TrackState::Empty);  // état inchangé
}

TEST(LooperTrack, stop_piste_vide_interdit) {
    LooperTrack track;
    CHECK(!track.stop().ok());
    CHECK(track.state() == TrackState::Empty);
}

TEST(LooperTrack, overdub_sans_lecture_interdit) {
    LooperTrack track;
    CHECK(!track.startOverdub().ok());  // depuis Empty
    CHECK(track.state() == TrackState::Empty);

    REQUIRE(track.record().ok());
    CHECK(!track.startOverdub().ok());  // depuis Recording
    CHECK(track.state() == TrackState::Recording);
}

TEST(LooperTrack, double_record_interdit) {
    LooperTrack track;
    REQUIRE(track.record().ok());
    const auto status = track.record();
    CHECK(!status.ok());
    CHECK(track.state() == TrackState::Recording);
}

TEST(LooperTrack, finishRecording_hors_enregistrement_interdit) {
    LooperTrack track;
    CHECK(!track.finishRecording().ok());
    CHECK(track.state() == TrackState::Empty);
}

TEST(LooperTrack, stopOverdub_hors_overdub_interdit) {
    LooperTrack track;
    REQUIRE(track.record().ok());
    REQUIRE(track.finishRecording().ok());
    CHECK(!track.stopOverdub().ok());  // on est en Playing, pas Overdubbing
    CHECK(track.state() == TrackState::Playing);
}

// --- Paramètres de mixage ---------------------------------------------------

TEST(LooperTrack, gain_et_mute_independants_de_letat) {
    LooperTrack track;
    track.setGain(Gain::silence());
    CHECK(track.gain().isSilent());

    track.setMuted(true);
    CHECK(track.isMuted());

    // Le mixage ne perturbe pas la machine à états.
    REQUIRE(track.record().ok());
    CHECK(track.state() == TrackState::Recording);
    CHECK(track.isMuted());
}
