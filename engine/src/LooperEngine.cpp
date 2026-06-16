// SPDX-License-Identifier: MIT
#include "voicelive/engine/LooperEngine.hpp"

#include <algorithm>
#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/LooperTrack.hpp"
#include "voicelive/core/Project.hpp"
#include "voicelive/core/Result.hpp"
#include "voicelive/core/Transport.hpp"
#include "voicelive/engine/Mixer.hpp"
#include "voicelive/engine/TrackProcessor.hpp"

namespace voicelive::engine {

core::Status LooperEngine::prepare(core::SampleRate sampleRate, std::size_t trackCount,
                                   std::size_t loopCapacity, std::size_t maxBlockSize) {
    if (trackCount < core::Project::kMinTracks || trackCount > kMaxTracks) {
        return core::Status::failure(core::ErrorCode::OutOfRange,
                                     "Le nombre de pistes doit être dans [1, 8]");
    }
    transport_.setSampleRate(sampleRate);
    tracks_.clear();
    tracks_.resize(trackCount);
    for (TrackProcessor& processor : tracks_) {
        processor.prepare(sampleRate, loopCapacity, maxBlockSize);
    }
    metronome_.prepare(sampleRate, maxBlockSize);
    scratch_.assign(maxBlockSize, 0.0F);
    selected_ = 0;
    return core::Status::success();
}

core::Status LooperEngine::applyCommand(const EngineCommand& command) {
    if (command.action == Cmd::SelectTrack) {
        if (command.track >= tracks_.size()) {
            return core::Status::failure(core::ErrorCode::OutOfRange, "Index de piste hors bornes");
        }
        selected_ = command.track;
        return core::Status::success();
    }

    if (command.track >= tracks_.size()) {
        return core::Status::failure(core::ErrorCode::OutOfRange, "Index de piste hors bornes");
    }
    TrackProcessor& processor = tracks_[command.track];
    switch (command.action) {
        case Cmd::Record:
            return processor.startRecording();
        case Cmd::FinishRecording:
            return processor.finishRecording();
        case Cmd::Play:
            return processor.play();
        case Cmd::Stop:
            return processor.stop();
        case Cmd::StartOverdub:
            return processor.startOverdub();
        case Cmd::StopOverdub:
            return processor.stopOverdub();
        case Cmd::Clear:
            processor.clearTrack();
            return core::Status::success();
        case Cmd::SetGain:
            processor.setGain(core::Gain::fromLinear(command.gain));
            return core::Status::success();
        case Cmd::SetMuted:
            processor.setMuted(command.muted);
            return core::Status::success();
        case Cmd::SelectTrack:
            break;  // déjà traité plus haut
    }
    return core::Status::success();
}

void LooperEngine::process(std::span<float> output, std::span<const float> input) noexcept {
    // 1) Vider la file de commandes (côté thread audio). Une commande invalide
    //    est un no-op sûr : la machine à états refuse sans muter l'état.
    EngineCommand command;
    while (commands_.pop(command)) {
        [[maybe_unused]] const core::Status status = applyCommand(command);
    }

    // 2) Rendre et sommer chaque piste, puis limiter.
    std::fill(output.begin(), output.end(), 0.0F);
    const std::size_t blockSize = std::min(output.size(), scratch_.size());
    const std::span<float> scratch{scratch_.data(), blockSize};
    const std::span<float> mixTarget = output.subspan(0, blockSize);
    for (TrackProcessor& processor : tracks_) {
        processor.process(scratch, input);
        mixer::addScaled(mixTarget, scratch, 1.0F);
    }
    metronome_.process(mixTarget, transport_);
    mixer::limit(output);
}

core::Result<core::Project> LooperEngine::exportSettings() const {
    // `create` ne peut échouer que sur un nombre de pistes invalide, or
    // `tracks_.size()` est toujours valide après `prepare`. On modifie le projet
    // en place (référence) et on renvoie le Result tel quel (NRVO, zéro copie).
    auto result = core::Project::create(name_, tracks_.size());
    if (result.ok()) {
        core::Project& project = result.value();
        project.transport() = transport_;
        for (std::size_t i = 0; i < tracks_.size(); ++i) {
            const core::LooperTrack& source = tracks_[i].track();
            core::LooperTrack* destination = project.track(i);
            destination->setGain(source.gain());
            destination->setMuted(source.isMuted());
        }
        static_cast<void>(project.selectTrack(selected_));
    }
    return result;
}

core::Status LooperEngine::applySettings(const core::Project& project) {
    name_ = project.name();
    transport_ = project.transport();
    const std::size_t count = std::min(tracks_.size(), project.trackCount());
    for (std::size_t i = 0; i < count; ++i) {
        const core::LooperTrack* source = project.track(i);
        tracks_[i].setGain(source->gain());
        tracks_[i].setMuted(source->isMuted());
    }
    if (project.selectedIndex() < tracks_.size()) {
        selected_ = project.selectedIndex();
    }
    return core::Status::success();
}

}  // namespace voicelive::engine
