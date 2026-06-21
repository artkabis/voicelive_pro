// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "voicelive/engine/SampleFifo.hpp"

#if JUCE_ANDROID
#include <jni.h>
#endif

namespace voicelive::app {

// Sortie audio Android via AudioTrack en mode split (sortie casque USB + entree micro natif).
//
// Utilise android.media.AudioTrack(STREAM_MUSIC) via JNI.
// AudioTrack avec STREAM_MUSIC suit la politique de routage audio du systeme Android :
// quand un casque USB est branche, Android le route automatiquement sans qu'on
// selectionne explicitement le peripherique dans Oboe. Cela evite entierement
// les assertions Oboe:236 (cross-HAL) et Oboe:517 (USB+USB duplex).
//
// Threading :
//   Producteur   = thread audio JUCE (getNextAudioBlock) → writeSamples()
//   Consommateur = thread de sortie (background natif attache a la JVM) → AudioTrack.write()
//   Communication: SampleFifo (file SPSC lock-free).
//
// Politique de perte :
//   - over-run  : si le FIFO est plein, le producteur abandonne les echantillons
//     en trop et incremente droppedSamples().
//   - under-run : si le FIFO est vide, le consommateur ecrit du silence et
//     incremente underrunSamples().
class AndroidAudioOutput {
public:
    AndroidAudioOutput() = default;
    ~AndroidAudioOutput() { stop(); }

    AndroidAudioOutput(const AndroidAudioOutput&) = delete;
    AndroidAudioOutput& operator=(const AndroidAudioOutput&) = delete;

    // Demarre la sortie audio. Thread message uniquement.
    // Cree un AudioTrack(STREAM_MUSIC, sampleRate, CHANNEL_OUT_MONO, ENCODING_PCM_FLOAT)
    // qui route automatiquement vers le casque USB si present.
    // Idempotent : un second appel sans stop() prealable est ignore (retourne true).
    bool start(int sampleRate) noexcept;

    // Arrete la sortie et libere AudioTrack. Thread message uniquement.
    // Idempotent : sans sortie active, ne fait rien.
    void stop() noexcept;

    [[nodiscard]] bool isRunning() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    // Ecrit jusqu'a `count` echantillons float dans le FIFO.
    // Appele UNIQUEMENT depuis getNextAudioBlock (thread audio temps reel, lock-free).
    // Retourne le nombre ecrit (< count si le FIFO est plein ; surplus abandonne).
    int writeSamples(const float* src, int count) noexcept;

    // Compteurs de diagnostic (cumules, lisibles depuis n'importe quel thread).
    [[nodiscard]] std::uint64_t droppedSamples() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t underrunSamples() const noexcept {
        return underrun_.load(std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t kFifoCapacity = 16384;
    voicelive::engine::SampleFifo<kFifoCapacity> fifo_;

    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> underrun_{0};

    std::atomic<bool> running_{false};
    std::thread outputThread_;

#if JUCE_ANDROID
    jobject audioTrack_{nullptr};
    JavaVM* jvm_{nullptr};

    void outputLoop() noexcept;
#endif
};

}  // namespace voicelive::app
