// SPDX-License-Identifier: MIT
#include "voicelive/core/LooperTrack.hpp"

#include "voicelive/core/Result.hpp"

namespace voicelive::core {

const char* toString(TrackState state) noexcept {
    switch (state) {
        case TrackState::Empty:
            return "Empty";
        case TrackState::Recording:
            return "Recording";
        case TrackState::Playing:
            return "Playing";
        case TrackState::Overdubbing:
            return "Overdubbing";
        case TrackState::Stopped:
            return "Stopped";
    }
    return "Unknown";
}

// Messages d'erreur STATIQUES (littéraux) : aucune allocation, donc une
// transition refusée peut être produite sur le thread audio sans danger.
Status LooperTrack::record() {
    if (state_ != TrackState::Empty) {
        return Status::failure(ErrorCode::InvalidTransition,
                               "record interdit : la piste n'est pas vide");
    }
    state_ = TrackState::Recording;
    return Status::success();
}

Status LooperTrack::finishRecording() {
    if (state_ != TrackState::Recording) {
        return Status::failure(ErrorCode::InvalidTransition,
                               "finishRecording interdit : aucun enregistrement en cours");
    }
    state_ = TrackState::Playing;
    return Status::success();
}

Status LooperTrack::play() {
    if (state_ != TrackState::Stopped) {
        return Status::failure(ErrorCode::InvalidTransition,
                               "play interdit : aucun contenu arrêté à relire");
    }
    state_ = TrackState::Playing;
    return Status::success();
}

Status LooperTrack::stop() {
    switch (state_) {
        case TrackState::Recording:
        case TrackState::Playing:
        case TrackState::Overdubbing:
            state_ = TrackState::Stopped;
            return Status::success();
        case TrackState::Empty:
        case TrackState::Stopped:
            break;
    }
    return Status::failure(ErrorCode::InvalidTransition,
                           "stop interdit : la piste n'est ni en lecture ni en enregistrement");
}

Status LooperTrack::startOverdub() {
    if (state_ != TrackState::Playing) {
        return Status::failure(ErrorCode::InvalidTransition,
                               "startOverdub interdit : la piste n'est pas en lecture");
    }
    state_ = TrackState::Overdubbing;
    return Status::success();
}

Status LooperTrack::stopOverdub() {
    if (state_ != TrackState::Overdubbing) {
        return Status::failure(ErrorCode::InvalidTransition,
                               "stopOverdub interdit : aucun overdub en cours");
    }
    state_ = TrackState::Playing;
    return Status::success();
}

void LooperTrack::clear() noexcept {
    state_ = TrackState::Empty;
}

}  // namespace voicelive::core
