// SPDX-License-Identifier: MIT
#include "HeadphoneMonitor.h"

#include "voicelive/engine/AudioDeviceHint.hpp"

namespace voicelive::app {

// ─── HeadphoneLed ─────────────────────────────────────────────────────────────

void HeadphoneLed::setConnected(bool connected) noexcept {
    if (connected_ == connected)
        return;
    connected_ = connected;
    repaint();
}

void HeadphoneLed::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat().reduced(2.0F);
    // Fond colore
    const juce::Colour fill = connected_ ? juce::Colour(0xFF00C853) : juce::Colour(0xFFD50000);
    g.setColour(fill);
    g.fillEllipse(bounds);
    // Halo lumineux
    const juce::Colour glow = connected_ ? juce::Colour(0xFF69F0AE) : juce::Colour(0xFFFF5252);
    g.setColour(glow.withAlpha(0.5F));
    g.drawEllipse(bounds.reduced(1.0F), 1.5F);
    // Reflet blanc en haut a gauche
    g.setColour(juce::Colours::white.withAlpha(0.25F));
    g.fillEllipse(bounds.removeFromLeft(bounds.getWidth() * 0.5F)
                      .removeFromTop(bounds.getHeight() * 0.5F)
                      .reduced(2.0F));
}

// ─── HeadphoneMonitor ─────────────────────────────────────────────────────────

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
    // 1. Peripherique actuellement ouvert
    if (auto* device = mgr.getCurrentAudioDevice(); device != nullptr) {
        if (voicelive::engine::looksLikeHeadphones(device->getName().toStdString(),
                                                   device->getTypeName().toStdString())) {
            connected_.store(true, std::memory_order_release);
            return;
        }
    }

    // 2. Scanner les types disponibles : couvre USB-C branche mais pas encore
    //    selectionne comme peripherique actif.
    for (auto* type : mgr.getAvailableDeviceTypes()) {
        if (type != nullptr &&
            voicelive::engine::looksLikeHeadphones("", type->getTypeName().toStdString())) {
            connected_.store(true, std::memory_order_release);
            return;
        }
    }

    connected_.store(false, std::memory_order_release);
}

void HeadphoneMonitor::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (auto* mgr = dynamic_cast<juce::AudioDeviceManager*>(source)) {
        poll(*mgr);
    }
}

}  // namespace voicelive::app
