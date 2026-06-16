// SPDX-License-Identifier: MIT
#include "voicelive/engine/Metronome.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/Transport.hpp"

namespace voicelive::engine {

void Metronome::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate.hz();
    clickLength_ = static_cast<std::size_t>(0.05 * static_cast<double>(sampleRate_));  // 50 ms
    decaySamples_ = 0.012 * static_cast<double>(sampleRate_);                          // ~12 ms
    reset();
}

void Metronome::reset() noexcept {
    started_ = false;
    beatPhase_ = 0.0;
    beatCount_ = 0;
    clickActive_ = false;
    clickPhase_ = 0;
}

void Metronome::process(std::span<float> out, const core::Transport& transport) noexcept {
    if (!enabled_ || sampleRate_ == 0) {
        return;
    }
    const double samplesPerBeat = transport.samplesPerBeat();
    if (samplesPerBeat <= 0.0) {
        return;
    }
    const long beatsPerBar = std::max<long>(1, std::lround(transport.signature().beatsPerBar()));
    const float gain = gain_.linear();

    for (float& sample : out) {
        advanceBeat(samplesPerBeat, beatsPerBar);
        sample += renderClickSample() * gain;
    }
}

void Metronome::advanceBeat(double samplesPerBeat, long beatsPerBar) noexcept {
    if (!started_) {
        triggerClick(true);  // premier temps = accent
        started_ = true;
        return;
    }
    beatPhase_ += 1.0;
    if (beatPhase_ >= samplesPerBeat) {
        beatPhase_ -= samplesPerBeat;
        ++beatCount_;
        triggerClick((beatCount_ % beatsPerBar) == 0);
    }
}

void Metronome::triggerClick(bool accent) noexcept {
    clickActive_ = true;
    clickPhase_ = 0;
    clickFreq_ = accent ? 1500.0F : 1000.0F;
    clickAmp_ = accent ? 1.0F : 0.6F;
}

float Metronome::renderClickSample() noexcept {
    if (!clickActive_) {
        return 0.0F;
    }
    const double time = static_cast<double>(clickPhase_) / static_cast<double>(sampleRate_);
    const double envelope = std::exp(-static_cast<double>(clickPhase_) / decaySamples_);
    const double value = static_cast<double>(clickAmp_) *
                         std::sin(2.0 * std::numbers::pi * static_cast<double>(clickFreq_) * time) *
                         envelope;

    ++clickPhase_;
    if (clickPhase_ >= clickLength_) {
        clickActive_ = false;
    }
    return static_cast<float>(value);
}

}  // namespace voicelive::engine
