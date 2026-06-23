// SPDX-License-Identifier: MIT
#include "voicelive/engine/LooperEngine.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/LooperTrack.hpp"
#include "voicelive/core/Music.hpp"
#include "voicelive/core/Project.hpp"
#include "voicelive/core/Result.hpp"
#include "voicelive/core/Transport.hpp"
#include "voicelive/dsp/EffectChain.hpp"
#include "voicelive/dsp/PitchDetector.hpp"
#include "voicelive/engine/Mixer.hpp"
#include "voicelive/engine/TrackProcessor.hpp"
#include "voicelive/engine/WavFile.hpp"

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
    masterChain_.prepare(sampleRate, maxBlockSize);
    scratch_.assign(maxBlockSize, 0.0F);
    selected_ = 0;
    masterLength_ = 0;
    return core::Status::success();
}

core::Status LooperEngine::reconfigure(core::SampleRate sampleRate, std::size_t maxBlockSize) {
    if (tracks_.empty()) {
        return core::Status::failure(core::ErrorCode::InvalidArgument,
                                     "reconfigure() avant prepare() : aucune piste");
    }
    transport_.setSampleRate(sampleRate);
    // Re-préparer les chaînes d'effets EXISTANTES (les objets Effect ne sont pas
    // recréés, donc les pointeurs distribués à l'UI restent valides). Le contenu
    // audio des pistes (LoopAudio) et leur état (LooperTrack) ne sont PAS touchés.
    for (TrackProcessor& processor : tracks_) {
        processor.effects().prepare(sampleRate, maxBlockSize);
    }
    metronome_.prepare(sampleRate, maxBlockSize);
    masterChain_.prepare(sampleRate, maxBlockSize);
    scratch_.assign(maxBlockSize, 0.0F);
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
        case Cmd::FinishRecording: {
            const core::Status status = processor.finishRecording();
            if (status.ok()) {
                alignTrackLoop(processor);
            }
            return status;
        }
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
        case Cmd::Seek:
            processor.setPlayhead(command.position);
            return core::Status::success();
        case Cmd::SelectTrack:
            break;  // déjà traité plus haut
    }
    return core::Status::success();
}

core::Status LooperEngine::importTrack(std::size_t index, const wav::AudioData& audio) {
    if (index >= tracks_.size()) {
        return core::Status::failure(core::ErrorCode::OutOfRange, "Index de piste hors bornes");
    }
    if (audio.channels == 0) {
        return core::Status::failure(core::ErrorCode::InvalidArgument, "Audio sans canal");
    }

    const std::size_t frames = audio.frameCount();
    std::vector<float> mono(frames, 0.0F);
    const auto channels = static_cast<float>(audio.channels);
    // Les échantillons WAV sont entrelacés : [L0, R0, L1, R1, ...].
    // On somme les canaux de chaque trame et on normalise pour obtenir le mono.
    for (std::size_t frame = 0; frame < frames; ++frame) {
        float sum = 0.0F;
        for (unsigned channel = 0; channel < audio.channels; ++channel) {
            sum += audio.samples[(frame * audio.channels) + channel];
        }
        mono[frame] = sum / channels;
    }

    tracks_[index].loadContent(mono);
    if (masterLength_ == 0 && frames > 0) {
        masterLength_ = frames;  // un sample importé peut servir de référence
    }
    return core::Status::success();
}

core::Status LooperEngine::importTrackFromFile(std::size_t index, const std::string& path) {
    auto audio = wav::read(path);
    if (!audio.ok()) {
        return core::Status::failure(audio.error().code, audio.error().message);
    }
    return importTrack(index, audio.value());
}

wav::AudioData LooperEngine::renderMix(std::size_t frames) {
    // Sauvegarde de l'état de lecture (l'export ne doit pas perturber le live).
    std::vector<std::size_t> savedPlayheads(tracks_.size());
    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        savedPlayheads[i] = tracks_[i].playhead();
        tracks_[i].setPlayhead(0);  // rendu depuis le début des boucles
    }
    const bool metronomeWasEnabled = metronome_.isEnabled();
    metronome_.setEnabled(false);  // mix exporté = pistes seules, sans clic

    wav::AudioData mix;
    mix.channels = 1;
    mix.sampleRate = transport_.sampleRate().hz();
    mix.samples.assign(frames, 0.0F);

    const std::size_t block = std::max<std::size_t>(scratch_.size(), 1);
    const std::vector<float> silence(block, 0.0F);
    for (std::size_t pos = 0; pos < frames; pos += block) {
        const std::size_t count = std::min(block, frames - pos);
        process(std::span<float>{mix.samples}.subspan(pos, count),
                std::span<const float>{silence}.subspan(0, count));
    }

    // Restauration de l'état.
    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        tracks_[i].setPlayhead(savedPlayheads[i]);
    }
    metronome_.setEnabled(metronomeWasEnabled);
    return mix;
}

core::Status LooperEngine::exportMixToFile(const std::string& path, std::size_t frames) {
    return wav::write(path, renderMix(frames));
}

void LooperEngine::alignTrackLoop(TrackProcessor& processor) {
    const std::size_t recorded = processor.audio().length();
    if (recorded == 0) {
        return;
    }
    if (masterLength_ == 0) {
        masterLength_ = recorded;  // première boucle enregistrée = référence
        return;
    }
    const std::size_t aligned = core::Transport::chooseLoopMultiple(recorded, masterLength_);
    processor.setLoopLength(aligned);
}

void LooperEngine::process(std::span<float> output, std::span<const float> input) noexcept {
    blocksProcessed_.fetch_add(1, std::memory_order_relaxed);

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
    masterChain_.process(mixTarget);  // mastering sur le mix complet
    mixer::limit(output);
}

std::optional<core::music::Note> LooperEngine::tune(std::span<const float> input) const {
    const auto frequency = tuner_.detect(input, transport_.sampleRate());
    if (!frequency) {
        return std::nullopt;
    }
    return core::music::fromFrequency(*frequency);
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
