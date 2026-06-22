// SPDX-License-Identifier: MIT
#include "AndroidMicCapture.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <thread>

#if JUCE_ANDROID
#include <android/log.h>
#define VLMIC_TAG "VoiceLiveMic"
#define VLMIC_I(...) __android_log_print(ANDROID_LOG_INFO, VLMIC_TAG, __VA_ARGS__)
#define VLMIC_E(...) __android_log_print(ANDROID_LOG_ERROR, VLMIC_TAG, __VA_ARGS__)

// getEnv() est fourni par JUCE pour le thread courant (declare en liaison externe).
namespace juce {
JNIEnv* getEnv() noexcept;
}
#endif

namespace voicelive::app {

// ─── Consommateur (thread audio) : compile sur toutes les plateformes ────────

int AndroidMicCapture::readSamples(float* dst, int count) noexcept {
    if (count <= 0) {
        return 0;
    }
    const std::size_t got = fifo_.read(dst, static_cast<std::size_t>(count));
    // Under-run : on a demande plus que disponible. L'appelant comblera par du
    // silence ; on comptabilise pour le diagnostic (producteur en retard / arrete).
    if (got < static_cast<std::size_t>(count)) {
        underrun_.fetch_add(static_cast<std::uint64_t>(static_cast<std::size_t>(count) - got),
                            std::memory_order_relaxed);
    }
    return static_cast<int>(got);
}

// ─── Implementation Android ──────────────────────────────────────────────────

#if JUCE_ANDROID

// Cherche le premier AudioDeviceInfo de type TYPE_BUILTIN_MIC (15) parmi les
// peripheriques d'entree. Retourne une local-ref JNI (appelant doit DeleteLocalRef),
// ou nullptr si introuvable / API indisponible. Ne leve jamais d'exception JNI.
static jobject findBuiltinMicDevice(JNIEnv* env) noexcept {
    env->ExceptionClear();

    // ActivityThread.currentApplication() → Application (sous-type de Context).
    // API interne Android stable depuis API 1 ; pas de Context passe en parametre requis.
    jclass atClass = env->FindClass("android/app/ActivityThread");
    if (atClass == nullptr) { env->ExceptionClear(); return nullptr; }
    jmethodID curApp = env->GetStaticMethodID(atClass, "currentApplication",
                                               "()Landroid/app/Application;");
    if (curApp == nullptr) { env->ExceptionClear(); env->DeleteLocalRef(atClass); return nullptr; }
    jobject context = env->CallStaticObjectMethod(atClass, curApp);
    env->DeleteLocalRef(atClass);
    if (context == nullptr || env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }

    // context.getSystemService("audio") → AudioManager
    jclass ctxCls = env->GetObjectClass(context);
    jmethodID getSvc = env->GetMethodID(ctxCls, "getSystemService",
                                         "(Ljava/lang/String;)Ljava/lang/Object;");
    env->DeleteLocalRef(ctxCls);
    if (getSvc == nullptr) { env->ExceptionClear(); env->DeleteLocalRef(context); return nullptr; }
    jstring audioKey = env->NewStringUTF("audio");
    jobject am = env->CallObjectMethod(context, getSvc, audioKey);
    env->DeleteLocalRef(audioKey);
    env->DeleteLocalRef(context);
    if (am == nullptr || env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }

    // am.getDevices(GET_DEVICES_INPUTS=2) → AudioDeviceInfo[]
    jclass amCls = env->GetObjectClass(am);
    jmethodID getDevs = env->GetMethodID(amCls, "getDevices",
                                          "(I)[Landroid/media/AudioDeviceInfo;");
    env->DeleteLocalRef(amCls);
    if (getDevs == nullptr) { env->ExceptionClear(); env->DeleteLocalRef(am); return nullptr; }
    auto* devArr = (jobjectArray)env->CallObjectMethod(am, getDevs, (jint)2);
    env->DeleteLocalRef(am);
    if (devArr == nullptr || env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }

    // Parcourir et trouver TYPE_BUILTIN_MIC = 15
    jobject found = nullptr;
    const jint len = env->GetArrayLength(devArr);
    for (jint i = 0; i < len && found == nullptr; ++i) {
        jobject dev = env->GetObjectArrayElement(devArr, i);
        if (dev == nullptr) continue;
        jclass devCls = env->GetObjectClass(dev);
        jmethodID getType = env->GetMethodID(devCls, "getType", "()I");
        env->DeleteLocalRef(devCls);
        if (getType != nullptr && env->CallIntMethod(dev, getType) == 15 /*TYPE_BUILTIN_MIC*/) {
            found = dev;
        } else {
            env->DeleteLocalRef(dev);
        }
        env->ExceptionClear();
    }
    env->DeleteLocalRef(devArr);
    return found;  // local-ref ou nullptr ; appelant doit DeleteLocalRef si non-null
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
        const jint n = env->CallIntMethod(audioRecord_, readMethod, jBuf, (jint)0, (jint)kReadBlock,
                                          kReadNonBlocking);
        if (n > 0) {
            env->GetFloatArrayRegion(jBuf, 0, static_cast<jsize>(n), nativeBuf);
            // Politique drop : le thread de capture ne peut pas bloquer ; si le
            // FIFO est plein (consommateur en retard), on abandonne le surplus.
            const std::size_t want = static_cast<std::size_t>(n);
            const std::size_t wrote = fifo_.write(nativeBuf, want);
            if (wrote < want) {
                dropped_.fetch_add(static_cast<std::uint64_t>(want - wrote),
                                   std::memory_order_relaxed);
            }
        } else {
            // Aucun echantillon disponible : pause courte pour eviter le busy-wait.
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }

    env->DeleteLocalRef(jBuf);
    jvm_->DetachCurrentThread();
}

bool AndroidMicCapture::start(int sampleRate) noexcept {
    // Idempotent + anti double-start : eviter d'orpheliner un thread de capture.
    if (running_.load(std::memory_order_acquire) || captureThread_.joinable()) {
        return true;
    }
    if (sampleRate < 4000 || sampleRate > 192000) {
        VLMIC_E("start: sampleRate invalide (%d)", sampleRate);
        return false;
    }

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
    jmethodID minBufId = env->GetStaticMethodID(arClass, "getMinBufferSize", "(III)I");
    if (minBufId == nullptr) {
        VLMIC_E("start: getMinBufferSize introuvable");
        env->DeleteLocalRef(arClass);
        return false;
    }
    constexpr jint kChannelInMono = 16;
    constexpr jint kEncodingPcmFloat = 4;
    const jint minBuf = env->CallStaticIntMethod(arClass, minBufId, (jint)sampleRate,
                                                 kChannelInMono, kEncodingPcmFloat);
    if (minBuf <= 0) {
        VLMIC_E("start: getMinBufferSize=%d (sampleRate=%d non supporte?)", (int)minBuf,
                sampleRate);
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
    jobject localAr = env->NewObject(arClass, ctor, kAudioSourceMic, (jint)sampleRate,
                                     kChannelInMono, kEncodingPcmFloat, bufSize);
    env->DeleteLocalRef(arClass);
    if (localAr == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        VLMIC_E("start: new AudioRecord() echec");
        if (localAr != nullptr) {
            env->DeleteLocalRef(localAr);
        }
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

    // Forcer le micro integre physique comme source de capture.
    // AUDIO_SOURCE_MIC peut etre route vers le micro USB quand un casque USB est
    // l'interface de sortie active (politique audio Android). Sur Samsung A26,
    // "SM-A266B built-in microphone" est le vrai micro du telephone ; setPreferredDevice
    // avec TYPE_BUILTIN_MIC garantit la capture sur ce peripherique, independamment
    // du routage audio. Non bloquant : en cas d'echec on continue avec le defaut.
    {
        jclass cls = env->GetObjectClass(localAr);
        jmethodID setPref = env->GetMethodID(cls, "setPreferredDevice",
                                              "(Landroid/media/AudioDeviceInfo;)Z");
        env->DeleteLocalRef(cls);
        if (setPref != nullptr) {
            jobject builtinMic = findBuiltinMicDevice(env);
            if (builtinMic != nullptr) {
                const jboolean ok = env->CallBooleanMethod(localAr, setPref, builtinMic);
                env->ExceptionClear();
                env->DeleteLocalRef(builtinMic);
                VLMIC_I("start: setPreferredDevice(TYPE_BUILTIN_MIC)=%s", ok ? "OK" : "echec");
            } else {
                VLMIC_I("start: TYPE_BUILTIN_MIC introuvable, AUDIO_SOURCE_MIC par defaut");
            }
        } else {
            env->ExceptionClear();
            VLMIC_I("start: setPreferredDevice() indisponible (API<23?), AUDIO_SOURCE_MIC par defaut");
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

    // Vider le FIFO et les compteurs avant de demarrer le thread producteur.
    fifo_.reset();
    dropped_.store(0, std::memory_order_relaxed);
    underrun_.store(0, std::memory_order_relaxed);

    running_.store(true, std::memory_order_release);
    captureThread_ = std::thread([this]() { captureLoop(); });

    VLMIC_I("start: AudioRecord demarre a %d Hz, bufMin=%d", sampleRate, (int)minBuf);
    return true;
}

void AndroidMicCapture::stop() noexcept {
    // Idempotent, mais on doit toujours joindre un thread eventuellement lance
    // meme si running_ a deja ete remis a false par un echec interne du captureLoop.
    running_.store(false, std::memory_order_release);
    if (captureThread_.joinable()) {
        captureThread_.join();
    }

    JNIEnv* env = juce::getEnv();
    if (env != nullptr && audioRecord_ != nullptr) {
        jclass cls = env->GetObjectClass(audioRecord_);
        jmethodID stopM = env->GetMethodID(cls, "stop", "()V");
        jmethodID releaseM = env->GetMethodID(cls, "release", "()V");
        env->DeleteLocalRef(cls);
        if (stopM != nullptr) {
            env->CallVoidMethod(audioRecord_, stopM);
            env->ExceptionClear();
        }
        if (releaseM != nullptr) {
            env->CallVoidMethod(audioRecord_, releaseM);
            env->ExceptionClear();
        }
        env->DeleteGlobalRef(audioRecord_);
        audioRecord_ = nullptr;
    }

    if (audioRecord_ == nullptr) {
        VLMIC_I("stop: AudioRecord arrete (drop=%llu underrun=%llu)",
                (unsigned long long)dropped_.load(std::memory_order_relaxed),
                (unsigned long long)underrun_.load(std::memory_order_relaxed));
    }
}

#else  // ─── Stubs non-Android ─────────────────────────────────────────────────

bool AndroidMicCapture::start(int) noexcept {
    return false;
}
void AndroidMicCapture::stop() noexcept {}

#endif

}  // namespace voicelive::app
