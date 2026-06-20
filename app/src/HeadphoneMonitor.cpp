// SPDX-License-Identifier: MIT
#include "HeadphoneMonitor.h"

#include "voicelive/engine/AudioDeviceHint.hpp"

#if JUCE_ANDROID
#include <jni.h>

// getEnv() is JUCE's accessor for the JNIEnv of the current thread. It is
// declared in JUCE's internal native header (juce_android_JNIHelpers.h), which
// is not pulled in by the public module umbrella headers. It has external
// linkage and is compiled into the same shared object, so we forward-declare it
// here at global scope (must NOT be nested inside voicelive::app, or it would
// declare a distinct voicelive::app::juce::getEnv and shadow the real ::juce).
namespace juce {
JNIEnv* getEnv() noexcept;
}  // namespace juce
#endif

namespace voicelive::app {

// ─── HeadphoneLed ─────────────────────────────────────────────────────────────

void HeadphoneLed::setConnected(bool connected) noexcept {
    if (connected_ == connected) {
        return;
    }
    connected_ = connected;
    repaint();
}

void HeadphoneLed::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat().reduced(2.0F);
    const juce::Colour fill = connected_ ? juce::Colour(0xFF00C853) : juce::Colour(0xFFD50000);
    g.setColour(fill);
    g.fillEllipse(bounds);
    const juce::Colour glow = connected_ ? juce::Colour(0xFF69F0AE) : juce::Colour(0xFFFF5252);
    g.setColour(glow.withAlpha(0.5F));
    g.drawEllipse(bounds.reduced(1.0F), 1.5F);
    g.setColour(juce::Colours::white.withAlpha(0.25F));
    g.fillEllipse(bounds.withWidth(bounds.getWidth() * 0.5F)
                        .withHeight(bounds.getHeight() * 0.5F)
                        .reduced(2.0F));
}

// ─── HeadphoneMonitor ─────────────────────────────────────────────────────────

#if JUCE_ANDROID
namespace {

// Clears any pending Java exception so subsequent JNI calls are safe.
// Returns true if an exception was pending. JNI behaviour is undefined if any
// call is made while an exception is pending, so we check after each Call*.
bool clearPendingException(JNIEnv* env) noexcept {
    if (env->ExceptionCheck() != JNI_FALSE) {
        env->ExceptionClear();
        return true;
    }
    return false;
}

// Resultat d'une sonde JNI, avec de quoi diagnostiquer un echec silencieux.
struct HeadsetScan {
    bool found = false;
    int jniCode = 0;      // 0=ok scanne, 1=pas de JNIEnv, 2=pas de contexte app,
                          // 3=pas d'AudioManager, 4=getDevices null/exception
    int outputCount = 0;  // nb de peripheriques de sortie enumeres
    int firstType = -1;   // 1er type AudioDeviceInfo rencontre (sinon -1)
};

// On Android, JUCE always reports "Android Audio" as device name regardless of
// the physical output hardware — USB-C DAC, wired headset, etc. are all opaque.
// This helper queries AudioManager.getDevices(GET_DEVICES_OUTPUTS) via JNI to
// retrieve the actual output hardware list and check for headset/USB types.
//
// AudioDeviceInfo type constants (android.media.AudioDeviceInfo) — VALEURS EXACTES :
//   TYPE_WIRED_HEADSET    = 3   (jack 3.5mm avec micro)
//   TYPE_WIRED_HEADPHONES = 4   (jack 3.5mm sans micro)
//   TYPE_BLUETOOTH_A2DP   = 8   (casque/enceinte Bluetooth)
//   TYPE_USB_DEVICE       = 11  (USB audio generique, ex. DAC USB-C)
//   TYPE_USB_ACCESSORY    = 12  (mode accessoire USB)
//   TYPE_USB_HEADSET      = 22  (casque USB-C — API 26) ⚠ PAS 8
//   TYPE_BLE_HEADSET      = 26  (casque Bluetooth LE — API 31)
HeadsetScan scanAndroidOutputs() noexcept {
    HeadsetScan scan;

    JNIEnv* env = juce::getEnv();
    if (env == nullptr) {
        scan.jniCode = 1;
        return scan;
    }

    // ActivityThread.currentApplication() — avoids needing an Activity reference.
    jclass atClass = env->FindClass("android/app/ActivityThread");
    if (atClass == nullptr) {
        clearPendingException(env);
        scan.jniCode = 2;
        return scan;
    }
    jmethodID currentApp = env->GetStaticMethodID(atClass, "currentApplication",
                                                   "()Landroid/app/Application;");
    if (currentApp == nullptr) {
        clearPendingException(env);
        env->DeleteLocalRef(atClass);
        scan.jniCode = 2;
        return scan;
    }
    jobject appObj = env->CallStaticObjectMethod(atClass, currentApp);
    env->DeleteLocalRef(atClass);
    if (clearPendingException(env) || appObj == nullptr) {
        if (appObj != nullptr) {
            env->DeleteLocalRef(appObj);
        }
        scan.jniCode = 2;
        return scan;
    }

    // context.getSystemService("audio")
    jclass ctxClass = env->GetObjectClass(appObj);
    jmethodID getService = env->GetMethodID(ctxClass, "getSystemService",
                                            "(Ljava/lang/String;)Ljava/lang/Object;");
    env->DeleteLocalRef(ctxClass);
    if (getService == nullptr) {
        clearPendingException(env);
        env->DeleteLocalRef(appObj);
        scan.jniCode = 3;
        return scan;
    }
    jstring audioKey = env->NewStringUTF("audio");
    jobject am = env->CallObjectMethod(appObj, getService, audioKey);
    env->DeleteLocalRef(audioKey);
    env->DeleteLocalRef(appObj);
    if (clearPendingException(env) || am == nullptr) {
        if (am != nullptr) {
            env->DeleteLocalRef(am);
        }
        scan.jniCode = 3;
        return scan;
    }

    // AudioManager.getDevices(GET_DEVICES_OUTPUTS = 2)
    jclass amClass = env->GetObjectClass(am);
    jmethodID getDevices = env->GetMethodID(amClass, "getDevices",
                                            "(I)[Landroid/media/AudioDeviceInfo;");
    env->DeleteLocalRef(amClass);
    if (getDevices == nullptr) {
        clearPendingException(env);
        env->DeleteLocalRef(am);
        scan.jniCode = 4;
        return scan;
    }
    auto* devices = static_cast<jobjectArray>(env->CallObjectMethod(am, getDevices, (jint) 2));
    env->DeleteLocalRef(am);
    if (clearPendingException(env) || devices == nullptr) {
        if (devices != nullptr) {
            env->DeleteLocalRef(devices);
        }
        scan.jniCode = 4;
        return scan;
    }

    constexpr jint kWiredHeadset    = 3;
    constexpr jint kWiredHeadphones = 4;
    constexpr jint kBluetoothA2dp   = 8;
    constexpr jint kUsbDevice       = 11;
    constexpr jint kUsbAccessory    = 12;
    constexpr jint kUsbHeadset      = 22;
    constexpr jint kBleHeadset      = 26;
    constexpr jint kBleSpeaker      = 27;

    const jsize count = env->GetArrayLength(devices);
    scan.outputCount = static_cast<int>(count);
    for (jsize i = 0; i < count && !scan.found; ++i) {
        jobject dev = env->GetObjectArrayElement(devices, i);
        if (dev == nullptr) {
            continue;
        }
        jclass devClass = env->GetObjectClass(dev);
        jmethodID getType = env->GetMethodID(devClass, "getType", "()I");
        env->DeleteLocalRef(devClass);
        if (getType != nullptr) {
            const jint t = env->CallIntMethod(dev, getType);
            if (clearPendingException(env)) {
                env->DeleteLocalRef(dev);
                break;
            }
            if (scan.firstType < 0) {
                scan.firstType = static_cast<int>(t);
            }
            if (t == kWiredHeadset || t == kWiredHeadphones || t == kUsbHeadset ||
                t == kUsbDevice || t == kUsbAccessory || t == kBluetoothA2dp ||
                t == kBleHeadset || t == kBleSpeaker) {
                scan.found = true;
            }
        }
        env->DeleteLocalRef(dev);
    }
    env->DeleteLocalRef(devices);
    return scan;
}

}  // namespace
#endif

void HeadphoneMonitor::attach(juce::AudioDeviceManager& mgr) noexcept {
    mgr.addChangeListener(this);
    poll(mgr);
}

void HeadphoneMonitor::detach(juce::AudioDeviceManager& mgr) noexcept {
    mgr.removeChangeListener(this);
}

bool HeadphoneMonitor::isConnected() const noexcept {
    return connected_.load(std::memory_order_acquire);
}

void HeadphoneMonitor::poll(juce::AudioDeviceManager& mgr) noexcept {
#if JUCE_ANDROID
    // JUCE reports "Android Audio" regardless of physical output — use JNI to
    // query the actual hardware device list from AudioManager.
    const HeadsetScan scan = scanAndroidOutputs();
    diagStatus_.store(2, std::memory_order_relaxed);
    diagJniCode_.store(scan.jniCode, std::memory_order_relaxed);
    diagOutputCount_.store(scan.outputCount, std::memory_order_relaxed);
    diagFirstType_.store(scan.firstType, std::memory_order_relaxed);
    connected_.store(scan.found, std::memory_order_release);
    (void) mgr;
#else
    diagStatus_.store(1, std::memory_order_relaxed);
    // Desktop: heuristique sur le nom/type retournes par JUCE.
    if (auto* device = mgr.getCurrentAudioDevice(); device != nullptr) {
        if (voicelive::engine::looksLikeHeadphones(device->getName().toStdString(),
                                                   device->getTypeName().toStdString())) {
            connected_.store(true, std::memory_order_release);
            return;
        }
    }

    for (auto* type : mgr.getAvailableDeviceTypes()) {
        if (type != nullptr &&
            voicelive::engine::looksLikeHeadphones("", type->getTypeName().toStdString())) {
            connected_.store(true, std::memory_order_release);
            return;
        }
    }

    connected_.store(false, std::memory_order_release);
#endif
}

juce::String HeadphoneMonitor::diagnostic() const {
    const int status = diagStatus_.load(std::memory_order_relaxed);
    if (status == 0) {
        return "casque : non sonde";
    }
    if (status == 1) {
        return juce::String("casque : ") + (isConnected() ? "detecte" : "non") + " (desktop)";
    }
    // Android JNI.
    const int code = diagJniCode_.load(std::memory_order_relaxed);
    const char* codeText = "ok";
    switch (code) {
        case 1:
            codeText = "pas de JNIEnv";
            break;
        case 2:
            codeText = "pas de contexte app";
            break;
        case 3:
            codeText = "pas d'AudioManager";
            break;
        case 4:
            codeText = "getDevices KO";
            break;
        default:
            codeText = "ok";
            break;
    }
    return juce::String("casque JNI: ") + codeText + ", sorties=" +
           juce::String(diagOutputCount_.load(std::memory_order_relaxed)) +
           ", type1=" + juce::String(diagFirstType_.load(std::memory_order_relaxed));
}

void HeadphoneMonitor::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (auto* mgr = dynamic_cast<juce::AudioDeviceManager*>(source)) {
        poll(*mgr);
    }
}

}  // namespace voicelive::app
