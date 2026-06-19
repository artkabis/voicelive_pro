// SPDX-License-Identifier: MIT
#include "HeadphoneMonitor.h"

#include "voicelive/engine/AudioDeviceHint.hpp"

#if JUCE_ANDROID
#include <jni.h>
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

// On Android, JUCE always reports "Android Audio" as device name regardless of
// the physical output hardware — USB-C DAC, wired headset, etc. are all opaque.
// This helper queries AudioManager.getDevices(GET_DEVICES_OUTPUTS) via JNI to
// retrieve the actual output hardware list and check for headset/USB types.
//
// AudioDeviceInfo type constants (android.media.AudioDeviceInfo):
//   TYPE_WIRED_HEADSET    = 3  (3.5mm with mic)
//   TYPE_WIRED_HEADPHONES = 4  (3.5mm without mic)
//   TYPE_USB_HEADSET      = 8  (USB-C headset reported as headset profile)
//   TYPE_USB_DEVICE       = 11 (generic USB audio, e.g. USB-C DAC)
//   TYPE_USB_ACCESSORY    = 12 (USB accessory mode)
bool androidHasHeadsetOutput() noexcept {
    JNIEnv* env = juce::getEnv();
    if (env == nullptr) {
        return false;
    }

    // ActivityThread.currentApplication() — avoids needing an Activity reference.
    jclass atClass = env->FindClass("android/app/ActivityThread");
    if (atClass == nullptr) {
        env->ExceptionClear();
        return false;
    }
    jmethodID currentApp = env->GetStaticMethodID(atClass, "currentApplication",
                                                   "()Landroid/app/Application;");
    jobject appObj = (currentApp != nullptr)
                         ? env->CallStaticObjectMethod(atClass, currentApp)
                         : nullptr;
    env->DeleteLocalRef(atClass);
    if (appObj == nullptr) {
        return false;
    }

    // context.getSystemService("audio")
    jclass ctxClass = env->GetObjectClass(appObj);
    jmethodID getService = env->GetMethodID(ctxClass, "getSystemService",
                                            "(Ljava/lang/String;)Ljava/lang/Object;");
    env->DeleteLocalRef(ctxClass);
    if (getService == nullptr) {
        env->DeleteLocalRef(appObj);
        return false;
    }
    jstring audioKey = env->NewStringUTF("audio");
    jobject am = env->CallObjectMethod(appObj, getService, audioKey);
    env->DeleteLocalRef(audioKey);
    env->DeleteLocalRef(appObj);
    if (am == nullptr) {
        return false;
    }

    // AudioManager.getDevices(GET_DEVICES_OUTPUTS = 2)
    jclass amClass = env->GetObjectClass(am);
    jmethodID getDevices = env->GetMethodID(amClass, "getDevices",
                                            "(I)[Landroid/media/AudioDeviceInfo;");
    env->DeleteLocalRef(amClass);
    if (getDevices == nullptr) {
        env->DeleteLocalRef(am);
        return false;
    }
    auto* devices = static_cast<jobjectArray>(env->CallObjectMethod(am, getDevices, (jint) 2));
    env->DeleteLocalRef(am);
    if (devices == nullptr) {
        return false;
    }

    constexpr jint kWiredHeadset    = 3;
    constexpr jint kWiredHeadphones = 4;
    constexpr jint kUsbHeadset      = 8;
    constexpr jint kUsbDevice       = 11;
    constexpr jint kUsbAccessory    = 12;

    bool found = false;
    const jsize count = env->GetArrayLength(devices);
    for (jsize i = 0; i < count && !found; ++i) {
        jobject dev = env->GetObjectArrayElement(devices, i);
        if (dev == nullptr) {
            continue;
        }
        jclass devClass = env->GetObjectClass(dev);
        jmethodID getType = env->GetMethodID(devClass, "getType", "()I");
        env->DeleteLocalRef(devClass);
        if (getType != nullptr) {
            const jint t = env->CallIntMethod(dev, getType);
            if (t == kWiredHeadset || t == kWiredHeadphones ||
                t == kUsbHeadset  || t == kUsbDevice        || t == kUsbAccessory) {
                found = true;
            }
        }
        env->DeleteLocalRef(dev);
    }
    env->DeleteLocalRef(devices);
    return found;
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
    connected_.store(androidHasHeadsetOutput(), std::memory_order_release);
    (void) mgr;
#else
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

void HeadphoneMonitor::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (auto* mgr = dynamic_cast<juce::AudioDeviceManager*>(source)) {
        poll(*mgr);
    }
}

}  // namespace voicelive::app
