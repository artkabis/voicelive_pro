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

// Capture du micro integre Android en mode split (sortie USB + entree micro natif).
//
// Utilise android.media.AudioRecord via JNI — API disponible des Android 1.
// La capture est completement independante de JUCE/Oboe : deux HALs distincts,
// aucune session partagee. Cela permet d'avoir :
//   - Sortie  : casque USB (stream Oboe gere par JUCE, setAudioChannels(2,0))
//   - Entree  : micro integre (AudioRecord + setPreferredDevice(TYPE_BUILTIN_MIC)).
//               Sur Samsung A26 (et quand USB audio est actif), Android peut router
//               AUDIO_SOURCE_MIC vers le micro USB ; setPreferredDevice force
//               explicitement le micro integre du telephone.
//
// Threading :
//   Producteur   = thread de capture (background natif attache a la JVM)
//   Consommateur = thread audio JUCE (getNextAudioBlock)
//   Communication: SampleFifo (file SPSC lock-free, transfert par blocs).
//
// Politique de perte (assuree ICI, pas par le FIFO) :
//   - over-run  : si le FIFO est plein, le producteur ABANDONNE les echantillons
//     en trop (il ne peut pas bloquer le thread de capture) et incremente
//     droppedSamples(). Cela arrive si le consommateur est en retard.
//   - under-run : si le FIFO est vide, le consommateur recoit moins que demande,
//     complete par du silence et incremente underrunSamples().
class AndroidMicCapture {
public:
    AndroidMicCapture() = default;
    ~AndroidMicCapture() { stop(); }

    AndroidMicCapture(const AndroidMicCapture&) = delete;
    AndroidMicCapture& operator=(const AndroidMicCapture&) = delete;

    // Demarre la capture. Appele depuis le thread message (jamais le thread audio).
    // sampleRate doit correspondre a la frequence de sortie JUCE courante.
    // Idempotent : un second appel sans stop() prealable est ignore (retourne true).
    // Retourne false en cas d'echec (sampleRate invalide, permission refusee, API KO).
    bool start(int sampleRate) noexcept;

    // Arrete la capture et libere AudioRecord. Thread message uniquement.
    // Idempotent : sans capture active, ne fait rien.
    void stop() noexcept;

    [[nodiscard]] bool isRunning() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    // Lit jusqu'a `count` echantillons float depuis le FIFO.
    // Appele UNIQUEMENT depuis getNextAudioBlock (thread audio temps reel, lock-free).
    // Retourne le nombre lu (< count en sous-remplissage) ; l'appelant complete par
    // du silence. L'under-run est comptabilise pour diagnostic.
    int readSamples(float* dst, int count) noexcept;

    // Compteurs de diagnostic (cumules, lisibles depuis n'importe quel thread).
    [[nodiscard]] std::uint64_t droppedSamples() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t underrunSamples() const noexcept {
        return underrun_.load(std::memory_order_relaxed);
    }

private:
    // FIFO SPSC audio. 16384 floats @ 48 kHz = ~341 ms de tampon — large marge
    // pour absorber le jitter d'ordonnancement entre capture et thread audio.
    static constexpr std::size_t kFifoCapacity = 16384;
    voicelive::engine::SampleFifo<kFifoCapacity> fifo_;

    std::atomic<std::uint64_t> dropped_{0};   // over-run cumule (producteur)
    std::atomic<std::uint64_t> underrun_{0};  // under-run cumule (consommateur)

    std::atomic<bool> running_{false};
    std::thread captureThread_;

#if JUCE_ANDROID
    jobject audioRecord_{nullptr};  // reference globale JNI (valide entre start et stop)
    JavaVM* jvm_{nullptr};

    void captureLoop() noexcept;
#endif
};

}  // namespace voicelive::app
