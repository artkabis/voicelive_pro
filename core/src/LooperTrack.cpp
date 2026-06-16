// SPDX-License-Identifier: MIT
#include "voicelive/core/LooperTrack.hpp"

#include <string>

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

namespace {

/// Fabrique un échec de transition au message homogène et exploitable.
Status invalidTransition(const char* action, TrackState from) {
    return Status::failure(ErrorCode::InvalidTransition,
                           std::string{"Transition interdite : '"} + action +
                               "' impossible depuis l'état " + toString(from));
}

}  // namespace

Status LooperTrack::record() {
    if (state_ != TrackState::Empty) {
        return invalidTransition("record", state_);
    }
    state_ = TrackState::Recording;
    return Status::success();
}

Status LooperTrack::finishRecording() {
    if (state_ != TrackState::Recording) {
        return invalidTransition("finishRecording", state_);
    }
    state_ = TrackState::Playing;
    return Status::success();
}

Status LooperTrack::play() {
    if (state_ != TrackState::Stopped) {
        return invalidTransition("play", state_);
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
            return invalidTransition("stop", state_);
    }
    return invalidTransition("stop", state_);
}

Status LooperTrack::startOverdub() {
    if (state_ != TrackState::Playing) {
        return invalidTransition("startOverdub", state_);
    }
    state_ = TrackState::Overdubbing;
    return Status::success();
}

Status LooperTrack::stopOverdub() {
    if (state_ != TrackState::Overdubbing) {
        return invalidTransition("stopOverdub", state_);
    }
    state_ = TrackState::Playing;
    return Status::success();
}

void LooperTrack::clear() noexcept {
    state_ = TrackState::Empty;
}

}  // namespace voicelive::core
