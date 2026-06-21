// SPDX-License-Identifier: MIT
#include "AndroidAudioOutput.hpp"

#include <algorithm>
#include <cstddef>

#if JUCE_ANDROID
#include <android/log.h>
#define VLOUT_TAG "VoiceLiveOut"
#define VLOUT_I(...) __android_log_print(ANDROID_LOG_INFO, VLOUT_TAG, __VA_ARGS__)
#define VLOUT_E(...) __android_log_print(ANDROID_LOG_ERROR, VLOUT_TAG, __VA_ARGS__)

namespace juce {
JNIEnv* getEnv() noexcept;
}
#endif

namespace voicelive::app {

// ─── Producteur (thread audio JUCE) : compile sur toutes les plateformes ─────

int AndroidAudioOutput::writeSamples(const float* src, int count) noexcept {
    if (count <= 0)
        return 0;
    const std::size_t want = static_cast<std::size_t>(count);
    const std::size_t wrote = fifo_.write(src, want);
    if (wrote < want) {
        dropped_.fetch_add(static_cast<std::uint64_t>(want - wrote), std::memory_order_relaxed);
    }
    return static_cast<int>(wrote);
}

// ─── Implementation Android ──────────────────────────────────────────────────

#if JUCE_ANDROID

void AndroidAudioOutput::outputLoop() noexcept {
    JNIEnv* env = nullptr;
    if (jvm_->AttachCurrentThread(&env, nullptr) != JNI_OK || env == nullptr) {
        VLOUT_E("outputLoop: AttachCurrentThread echec");
        running_.store(false, std::memory_order_release);
        return;
    }

    jclass atClass = env->FindClass("android/media/AudioTrack");
    if (atClass == nullptr) {
        VLOUT_E("outputLoop: classe AudioTrack introuvable");
        jvm_->DetachCurrentThread();
        running_.store(false, std::memory_order_release);
        return;
    }
    // write(float[] audioData, int offsetInFloats, int sizeInFloats, int writeMode) : int
    jmethodID writeMethod = env->GetMethodID(atClass, "write", "([FIII)I");
    env->DeleteLocalRef(atClass);
    if (writeMethod == nullptr) {
        VLOUT_E("outputLoop: methode write([FIII)I introuvable");
        jvm_->DetachCurrentThread();
        running_.store(false, std::memory_order_release);
        return;
    }

    constexpr int kWriteBlock = 512;
    jfloatArray jBuf = env->NewFloatArray(static_cast<jsize>(kWriteBlock));
    if (jBuf == nullptr) {
        VLOUT_E("outputLoop: NewFloatArray echec");
        jvm_->DetachCurrentThread();
        running_.store(false, std::memory_order_release);
        return;
    }

    constexpr jint kWriteBlocking = 0;  // AudioTrack.WRITE_BLOCKING
    float nativeBuf[kWriteBlock];

    while (running_.load(std::memory_order_acquire)) {
        const std::size_t avail = fifo_.available();
        if (avail >= static_cast<std::size_t>(kWriteBlock)) {
            fifo_.read(nativeBuf, static_cast<std::size_t>(kWriteBlock));
        } else if (avail > 0) {
            fifo_.read(nativeBuf, avail);
            std::fill(nativeBuf + avail, nativeBuf + kWriteBlock, 0.0F);
            underrun_.fetch_add(
                static_cast<std::uint64_t>(static_cast<std::size_t>(kWriteBlock) - avail),
                std::memory_order_relaxed);
        } else {
            // Underrun complet : ecrire du silence pour maintenir le flux AudioTrack.
            std::fill(nativeBuf, nativeBuf + kWriteBlock, 0.0F);
            underrun_.fetch_add(static_cast<std::uint64_t>(kWriteBlock), std::memory_order_relaxed);
        }

        env->SetFloatArrayRegion(jBuf, 0, static_cast<jsize>(kWriteBlock), nativeBuf);
        const jint written = env->CallIntMethod(audioTrack_, writeMethod, jBuf, (jint)0,
                                                (jint)kWriteBlock, kWriteBlocking);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            VLOUT_E("outputLoop: write() exception");
            break;
        }
        // Valeur negative = erreur (ex. AudioTrack mis en pause/arret par stop()).
        if (written < 0) {
            VLOUT_E("outputLoop: write() retourne %d, sortie du loop", (int)written);
            break;
        }
    }

    env->DeleteLocalRef(jBuf);
    jvm_->DetachCurrentThread();
}

bool AndroidAudioOutput::start(int sampleRate) noexcept {
    if (running_.load(std::memory_order_acquire) || outputThread_.joinable()) {
        return true;
    }
    if (sampleRate < 4000 || sampleRate > 192000) {
        VLOUT_E("start: sampleRate invalide (%d)", sampleRate);
        return false;
    }

    JNIEnv* env = juce::getEnv();
    if (env == nullptr) {
        VLOUT_E("start: pas de JNIEnv");
        return false;
    }
    if (env->GetJavaVM(&jvm_) != JNI_OK || jvm_ == nullptr) {
        VLOUT_E("start: GetJavaVM echec");
        return false;
    }

    jclass atClass = env->FindClass("android/media/AudioTrack");
    if (atClass == nullptr) {
        VLOUT_E("start: classe AudioTrack introuvable");
        return false;
    }

    // AudioTrack.getMinBufferSize(sampleRate, CHANNEL_OUT_MONO=4, ENCODING_PCM_FLOAT=4)
    jmethodID minBufId = env->GetStaticMethodID(atClass, "getMinBufferSize", "(III)I");
    if (minBufId == nullptr) {
        VLOUT_E("start: getMinBufferSize introuvable");
        env->DeleteLocalRef(atClass);
        return false;
    }
    constexpr jint kChannelOutMono = 4;
    constexpr jint kEncodingPcmFloat = 4;
    const jint minBuf = env->CallStaticIntMethod(atClass, minBufId, (jint)sampleRate,
                                                 kChannelOutMono, kEncodingPcmFloat);
    if (minBuf <= 0) {
        VLOUT_E("start: getMinBufferSize=%d (sampleRate=%d non supporte?)", (int)minBuf,
                sampleRate);
        env->DeleteLocalRef(atClass);
        return false;
    }
    const jint bufSize = minBuf * 4;

    // new AudioTrack(streamType=STREAM_MUSIC, sampleRate, channelConfig, audioFormat,
    //                bufferSizeInBytes, mode=MODE_STREAM)
    jmethodID ctor = env->GetMethodID(atClass, "<init>", "(IIIIII)V");
    if (ctor == nullptr) {
        VLOUT_E("start: constructeur AudioTrack(IIIIII) introuvable");
        env->DeleteLocalRef(atClass);
        return false;
    }
    constexpr jint kStreamMusic = 3;  // AudioManager.STREAM_MUSIC
    constexpr jint kModeStream = 1;   // AudioTrack.MODE_STREAM
    jobject localAt = env->NewObject(atClass, ctor, kStreamMusic, (jint)sampleRate, kChannelOutMono,
                                     kEncodingPcmFloat, bufSize, kModeStream);
    env->DeleteLocalRef(atClass);
    if (localAt == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        VLOUT_E("start: new AudioTrack() echec");
        if (localAt != nullptr)
            env->DeleteLocalRef(localAt);
        return false;
    }

    // Verifier l'etat : getState() doit retourner STATE_INITIALIZED = 1
    {
        jclass cls = env->GetObjectClass(localAt);
        jmethodID getState = env->GetMethodID(cls, "getState", "()I");
        env->DeleteLocalRef(cls);
        if (getState != nullptr) {
            const jint state = env->CallIntMethod(localAt, getState);
            if (state != 1) {
                VLOUT_E("start: AudioTrack non initialise (getState=%d)", (int)state);
                env->DeleteLocalRef(localAt);
                return false;
            }
        }
    }

    audioTrack_ = env->NewGlobalRef(localAt);
    env->DeleteLocalRef(localAt);
    if (audioTrack_ == nullptr) {
        VLOUT_E("start: NewGlobalRef echec");
        return false;
    }

    // audioTrack.play()
    {
        jclass cls = env->GetObjectClass(audioTrack_);
        jmethodID playM = env->GetMethodID(cls, "play", "()V");
        env->DeleteLocalRef(cls);
        if (playM == nullptr) {
            VLOUT_E("start: play() introuvable");
            env->DeleteGlobalRef(audioTrack_);
            audioTrack_ = nullptr;
            return false;
        }
        env->CallVoidMethod(audioTrack_, playM);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            VLOUT_E("start: play() a leve une exception");
            env->DeleteGlobalRef(audioTrack_);
            audioTrack_ = nullptr;
            return false;
        }
    }

    fifo_.reset();
    dropped_.store(0, std::memory_order_relaxed);
    underrun_.store(0, std::memory_order_relaxed);

    running_.store(true, std::memory_order_release);
    outputThread_ = std::thread([this]() { outputLoop(); });

    VLOUT_I("start: AudioTrack (STREAM_MUSIC) demarre a %d Hz, bufMin=%d", sampleRate, (int)minBuf);
    return true;
}

void AndroidAudioOutput::stop() noexcept {
    running_.store(false, std::memory_order_release);

    // Mettre en pause l'AudioTrack pour debloquer tout write(WRITE_BLOCKING) en cours
    // dans outputLoop() avant d'appeler join() (sinon deadlock possible).
    JNIEnv* env = juce::getEnv();
    if (env != nullptr && audioTrack_ != nullptr) {
        jclass cls = env->GetObjectClass(audioTrack_);
        jmethodID pauseM = env->GetMethodID(cls, "pause", "()V");
        env->DeleteLocalRef(cls);
        if (pauseM != nullptr) {
            env->CallVoidMethod(audioTrack_, pauseM);
            env->ExceptionClear();
        }
    }

    if (outputThread_.joinable()) {
        outputThread_.join();
    }

    if (env != nullptr && audioTrack_ != nullptr) {
        jclass cls = env->GetObjectClass(audioTrack_);
        jmethodID stopM = env->GetMethodID(cls, "stop", "()V");
        jmethodID releaseM = env->GetMethodID(cls, "release", "()V");
        env->DeleteLocalRef(cls);
        if (stopM != nullptr) {
            env->CallVoidMethod(audioTrack_, stopM);
            env->ExceptionClear();
        }
        if (releaseM != nullptr) {
            env->CallVoidMethod(audioTrack_, releaseM);
            env->ExceptionClear();
        }
        env->DeleteGlobalRef(audioTrack_);
        audioTrack_ = nullptr;
    }

    VLOUT_I("stop: AudioTrack arrete (drop=%llu underrun=%llu)",
            (unsigned long long)dropped_.load(std::memory_order_relaxed),
            (unsigned long long)underrun_.load(std::memory_order_relaxed));
}

#else  // ─── Stubs non-Android ─────────────────────────────────────────────────

bool AndroidAudioOutput::start(int) noexcept {
    return false;
}
void AndroidAudioOutput::stop() noexcept {}

#endif

}  // namespace voicelive::app
