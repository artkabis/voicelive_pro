// SPDX-License-Identifier: MIT
#include "AndroidMicCapture.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#if JUCE_ANDROID
#include <android/log.h>
#define VLMIC_TAG "VoiceLiveMic"
#define VLMIC_I(...) __android_log_print(ANDROID_LOG_INFO,  VLMIC_TAG, __VA_ARGS__)
#define VLMIC_E(...) __android_log_print(ANDROID_LOG_ERROR, VLMIC_TAG, __VA_ARGS__)

// getEnv() est fourni par JUCE pour le thread courant (declare en liaison externe).
namespace juce { JNIEnv* getEnv() noexcept; }
#endif

namespace voicelive::app {

// ─── Ring buffer (compile sur toutes les plateformes) ────────────────────────

int AndroidMicCapture::readSamples(float* dst, int count) noexcept {
    const uint32_t r = ringRead_.load(std::memory_order_relaxed);
    const uint32_t w = ringWrite_.load(std::memory_order_acquire);
    const int avail = static_cast<int>((w - r) & static_cast<uint32_t>(kRingSize - 1));
    const int n = std::min(count, avail);
    if (n <= 0) return 0;

    const uint32_t rMask = r & static_cast<uint32_t>(kRingSize - 1);
    const int first = std::min(n, static_cast<int>(kRingSize - rMask));
    std::memcpy(dst, ring_.data() + rMask, static_cast<std::size_t>(first) * sizeof(float));
    if (first < n) {
        std::memcpy(dst + first, ring_.data(),
                    static_cast<std::size_t>(n - first) * sizeof(float));
    }
    ringRead_.store(r + static_cast<uint32_t>(n), std::memory_order_release);
    return n;
}

// ─── Implementation Android ──────────────────────────────────────────────────

#if JUCE_ANDROID

void AndroidMicCapture::pushSamples(const float* src, int count) noexcept {
    const uint32_t w = ringWrite_.load(std::memory_order_relaxed);
    const uint32_t r = ringRead_.load(std::memory_order_acquire);
    const int space =
        static_cast<int>((kRingSize - 1) - ((w - r) & static_cast<uint32_t>(kRingSize - 1)));
    const int n = std::min(count, space);
    if (n <= 0) return;  // ring plein : echantillons droppes (rare si tampon bien dimensionne)

    const uint32_t wMask = w & static_cast<uint32_t>(kRingSize - 1);
    const int first = std::min(n, static_cast<int>(kRingSize - wMask));
    std::memcpy(ring_.data() + wMask, src, static_cast<std::size_t>(first) * sizeof(float));
    if (first < n) {
        std::memcpy(ring_.data(), src + first,
                    static_cast<std::size_t>(n - first) * sizeof(float));
    }
    ringWrite_.store(w + static_cast<uint32_t>(n), std::memory_order_release);
}

void AndroidMicCapture::captureLoop() noexcept {
    JNIEnv* env = nullptr;
    if (jvm_->AttachCurrentThread(&env, nullptr) != JNI_OK || env == nullptr) {
        VLMIC_E("captureLoop: AttachCurrentThread echec");
        running_.store(false, std::memory_order_release);
        return;
    }

    jclass arClass = env->FindClass("android/media/AudioRecord");
    if (arClass == nullptr) {
        VLMIC_E("captureLoop: classe AudioRecord introuvable");
        jvm_->DetachCurrentThread();
        running_.store(false, std::memory_order_release);
        return;
    }
    // read(float[] audioData, int offsetInFloats, int sizeInFloats, int readMode) : int
    jmethodID readMethod = env->GetMethodID(arClass, "read", "([FIII)I");
    env->DeleteLocalRef(arClass);
    if (readMethod == nullptr) {
        VLMIC_E("captureLoop: methode read([FIII)I introuvable");
        jvm_->DetachCurrentThread();
        running_.store(false, std::memory_order_release);
        return;
    }

    // Tampon de lecture JNI : 512 floats (~10 ms @ 48 kHz).
    // READ_NON_BLOCKING pour ne pas bloquer le thread de capture.
    constexpr int kReadBlock = 512;
    jfloatArray jBuf = env->NewFloatArray(static_cast<jsize>(kReadBlock));
    if (jBuf == nullptr) {
        VLMIC_E("captureLoop: NewFloatArray echec");
        jvm_->DetachCurrentThread();
        running_.store(false, std::memory_order_release);
        return;
    }

    constexpr jint kReadNonBlocking = 1;  // AudioRecord.READ_NON_BLOCKING
    float nativeBuf[kReadBlock];

    while (running_.load(std::memory_order_acquire)) {
        const jint n = env->CallIntMethod(audioRecord_, readMethod,
                                          jBuf, (jint)0, (jint)kReadBlock, kReadNonBlocking);
        if (n > 0) {
            env->GetFloatArrayRegion(jBuf, 0, static_cast<jsize>(n), nativeBuf);
            pushSamples(nativeBuf, static_cast<int>(n));
        } else {
            // Aucun echantillon disponible : pause courte pour eviter le busy-wait.
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }

    env->DeleteLocalRef(jBuf);
    jvm_->DetachCurrentThread();
}

bool AndroidMicCapture::start(int sampleRate) noexcept {
    if (running_.load(std::memory_order_acquire)) return true;

    JNIEnv* env = juce::getEnv();
    if (env == nullptr) {
        VLMIC_E("start: pas de JNIEnv");
        return false;
    }
    if (env->GetJavaVM(&jvm_) != JNI_OK || jvm_ == nullptr) {
        VLMIC_E("start: GetJavaVM echec");
        return false;
    }

    jclass arClass = env->FindClass("android/media/AudioRecord");
    if (arClass == nullptr) {
        VLMIC_E("start: classe AudioRecord introuvable");
        return false;
    }

    // AudioRecord.getMinBufferSize(sampleRate, CHANNEL_IN_MONO=16, ENCODING_PCM_FLOAT=4)
    jmethodID minBufId =
        env->GetStaticMethodID(arClass, "getMinBufferSize", "(III)I");
    if (minBufId == nullptr) {
        VLMIC_E("start: getMinBufferSize introuvable");
        env->DeleteLocalRef(arClass);
        return false;
    }
    constexpr jint kChannelInMono   = 16;
    constexpr jint kEncodingPcmFloat = 4;
    const jint minBuf = env->CallStaticIntMethod(arClass, minBufId,
                                                  (jint)sampleRate,
                                                  kChannelInMono,
                                                  kEncodingPcmFloat);
    if (minBuf <= 0) {
        VLMIC_E("start: getMinBufferSize=%d (sampleRate=%d non supporte?)", (int)minBuf, sampleRate);
        env->DeleteLocalRef(arClass);
        return false;
    }
    const jint bufSize = minBuf * 4;  // marge confortable

    // new AudioRecord(audioSource=MIC, sampleRate, channelConfig, audioFormat, bufferSizeBytes)
    jmethodID ctor = env->GetMethodID(arClass, "<init>", "(IIIII)V");
    if (ctor == nullptr) {
        VLMIC_E("start: constructeur AudioRecord(IIIII) introuvable");
        env->DeleteLocalRef(arClass);
        return false;
    }
    constexpr jint kAudioSourceMic = 1;  // MediaRecorder.AudioSource.MIC
    jobject localAr = env->NewObject(arClass, ctor,
                                      kAudioSourceMic,
                                      (jint)sampleRate,
                                      kChannelInMono,
                                      kEncodingPcmFloat,
                                      bufSize);
    env->DeleteLocalRef(arClass);
    if (localAr == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        VLMIC_E("start: new AudioRecord() echec");
        if (localAr != nullptr) env->DeleteLocalRef(localAr);
        return false;
    }

    // Verifier l'etat : getState() doit retourner STATE_INITIALIZED = 1
    {
        jclass cls = env->GetObjectClass(localAr);
        jmethodID getState = env->GetMethodID(cls, "getState", "()I");
        env->DeleteLocalRef(cls);
        if (getState != nullptr) {
            const jint state = env->CallIntMethod(localAr, getState);
            if (state != 1) {
                VLMIC_E("start: AudioRecord non initialise (getState=%d)", (int)state);
                env->DeleteLocalRef(localAr);
                return false;
            }
        }
    }

    audioRecord_ = env->NewGlobalRef(localAr);
    env->DeleteLocalRef(localAr);
    if (audioRecord_ == nullptr) {
        VLMIC_E("start: NewGlobalRef echec");
        return false;
    }

    // audioRecord.startRecording()
    {
        jclass cls = env->GetObjectClass(audioRecord_);
        jmethodID startRec = env->GetMethodID(cls, "startRecording", "()V");
        env->DeleteLocalRef(cls);
        if (startRec == nullptr) {
            VLMIC_E("start: startRecording() introuvable");
            env->DeleteGlobalRef(audioRecord_);
            audioRecord_ = nullptr;
            return false;
        }
        env->CallVoidMethod(audioRecord_, startRec);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            VLMIC_E("start: startRecording() a leve une exception");
            env->DeleteGlobalRef(audioRecord_);
            audioRecord_ = nullptr;
            return false;
        }
    }

    // Reset du ring buffer avant de demarrer le thread producteur.
    ringWrite_.store(0, std::memory_order_relaxed);
    ringRead_.store(0, std::memory_order_relaxed);

    running_.store(true, std::memory_order_release);
    captureThread_ = std::thread([this]() { captureLoop(); });

    VLMIC_I("start: AudioRecord demarre a %d Hz, bufMin=%d", sampleRate, (int)minBuf);
    return true;
}

void AndroidMicCapture::stop() noexcept {
    if (!running_.load(std::memory_order_acquire)) return;
    running_.store(false, std::memory_order_release);

    if (captureThread_.joinable()) captureThread_.join();

    JNIEnv* env = juce::getEnv();
    if (env != nullptr && audioRecord_ != nullptr) {
        jclass cls = env->GetObjectClass(audioRecord_);
        jmethodID stopM    = env->GetMethodID(cls, "stop",    "()V");
        jmethodID releaseM = env->GetMethodID(cls, "release", "()V");
        env->DeleteLocalRef(cls);
        if (stopM    != nullptr) { env->CallVoidMethod(audioRecord_, stopM);    env->ExceptionClear(); }
        if (releaseM != nullptr) { env->CallVoidMethod(audioRecord_, releaseM); env->ExceptionClear(); }
        env->DeleteGlobalRef(audioRecord_);
        audioRecord_ = nullptr;
    }

    VLMIC_I("stop: AudioRecord arrete");
}

#else  // ─── Stubs non-Android ─────────────────────────────────────────────────

bool AndroidMicCapture::start(int) noexcept  { return false; }
void AndroidMicCapture::stop()  noexcept {}

#endif

}  // namespace voicelive::app
