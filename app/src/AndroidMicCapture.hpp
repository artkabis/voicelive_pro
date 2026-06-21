// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <thread>

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
//   - Entree  : micro integre (AudioRecord, AUDIO_SOURCE_MIC = toujours le micro
//               physique du telephone, meme quand un casque USB est branche)
//
// Threading :
//   Producteur = thread de capture (background thread natif attache a la JVM)
//   Consommateur = thread audio JUCE (getNextAudioBlock)
//   Communication : ring buffer SPSC lock-free.
class AndroidMicCapture {
public:
    AndroidMicCapture() = default;
    ~AndroidMicCapture() { stop(); }

    AndroidMicCapture(const AndroidMicCapture&) = delete;
    AndroidMicCapture& operator=(const AndroidMicCapture&) = delete;

    // Demarre la capture. Appele depuis le thread message (jamais le thread audio).
    // sampleRate doit correspondre a la frequence de sortie JUCE courante.
    // Retourne false en cas d'echec (permission refusee, API non supportee, etc.)
    bool start(int sampleRate) noexcept;

    // Arrete la capture et libere AudioRecord. Thread message uniquement.
    void stop() noexcept;

    [[nodiscard]] bool isRunning() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    // Lit jusqu'a `count` echantillons float depuis le ring buffer.
    // Appele UNIQUEMENT depuis getNextAudioBlock (thread audio temps reel, lock-free).
    // Retourne le nombre d'echantillons effectivement lus (peut etre < count si sous-remplissage).
    int readSamples(float* dst, int count) noexcept;

private:
    // Ring buffer SPSC audio. Taille puissance de 2 pour masquage d'index (pas de modulo).
    // 16384 floats @ 48 kHz = ~341 ms de tampon — largement suffisant pour absorber
    // les variations de timing entre le thread de capture et le thread audio.
    static constexpr std::size_t kRingSize = 16384;
    static_assert((kRingSize & (kRingSize - 1)) == 0, "kRingSize doit etre puissance de 2");

    std::array<float, kRingSize> ring_{};
    std::atomic<uint32_t> ringWrite_{0};  // ecrit par le thread de capture
    std::atomic<uint32_t> ringRead_{0};   // ecrit par le thread audio

    std::atomic<bool> running_{false};
    std::thread captureThread_;

#if JUCE_ANDROID
    jobject audioRecord_{nullptr};  // reference globale JNI (valide entre Start et stop)
    JavaVM* jvm_{nullptr};

    void captureLoop() noexcept;
    void pushSamples(const float* src, int count) noexcept;
#endif
};

}  // namespace voicelive::app
