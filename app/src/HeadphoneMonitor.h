// SPDX-License-Identifier: MIT
//
// HeadphoneMonitor -- detecte la presence d'un casque/ecouteurs USB-C ou jack.
// HeadphoneLed     -- indicateur visuel : vert = casque detecte, rouge = HP.
//
// HeadphoneMonitor::isConnected() est thread-safe (atomic) : l'etat peut etre
// lu depuis le thread audio dans getNextAudioBlock() sans verrou.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

namespace voicelive::app {

/// LED visuelle ronde : vert si casque detecte, rouge sinon.
struct HeadphoneLed final : public juce::Component {
    void setConnected(bool connected) noexcept;
    void paint(juce::Graphics& g) override;

private:
    bool connected_ = false;
};

/// Ecoute les changements de peripherique audio (ChangeListener sur
/// AudioDeviceManager) et expose l'etat casque via un atomic bool.
class HeadphoneMonitor final : public juce::ChangeListener {
public:
    /// Enregistre le moniteur aupres du gestionnaire audio.
    void attach(juce::AudioDeviceManager& mgr) noexcept;

    /// Desenregistre proprement (appeler avant shutdownAudio).
    void detach(juce::AudioDeviceManager& mgr) noexcept;

    /// Lit l'etat courant (thread-safe, sans verrou).
    [[nodiscard]] bool isConnected() const noexcept;

private:
    void poll(juce::AudioDeviceManager& mgr) noexcept;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    std::atomic<bool> connected_{false};
};

}  // namespace voicelive::app
