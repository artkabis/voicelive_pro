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
#include <cstdint>

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

    /// Sonde l'etat courant et met a jour l'atomique.
    /// A appeler depuis le thread UI (ex. timerCallback) pour couvrir les
    /// evenements hotplug USB-C qu'Android ne remonte pas toujours au
    /// ChangeListener de l'AudioDeviceManager.
    void poll(juce::AudioDeviceManager& mgr) noexcept;

    /// Vrai si le peripherique detecte est USB audio (types 11=USB_DEVICE,
    /// 12=USB_ACCESSORY, 22=USB_HEADSET). Ces types declenchent Oboe:517 en
    /// duplex → activer le mode split pour eviter le freeze.
    /// Thread-safe (atomic).
    [[nodiscard]] bool isUsbAudioConnected() const noexcept {
        const int t = diagFoundType_.load(std::memory_order_acquire);
        return t == 11 || t == 12 || t == 22;
    }

    /// Ligne de diagnostic lisible (affichee dans le panneau Diag) expliquant
    /// le resultat de la derniere detection : utile car sur Android la detection
    /// passe par JNI et peut echouer silencieusement (contexte null, etc.).
    [[nodiscard]] juce::String diagnostic() const;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    std::atomic<bool> connected_{false};

    // Diagnostic de la derniere sonde (rempli par poll()).
    // status : 0=non sonde, 1=desktop(heuristique), 2=Android JNI.
    std::atomic<int> diagStatus_{0};
    // Code JNI : 0=ok scanne, 1=pas de JNIEnv, 2=pas de contexte app,
    //            3=pas d'AudioManager, 4=getDevices null/exception.
    std::atomic<int> diagJniCode_{0};
    std::atomic<int> diagOutputCount_{0};  // nb de peripheriques de sortie vus
    // Packed: bits[3:0]=count, bits[9:4]=type[0], bits[15:10]=type[1], ..., 6 bits/type, max 10.
    std::atomic<std::int64_t> diagAllTypes_{0};
    std::atomic<int> diagFoundType_{-1};  // type qui a declenche found=true, -1 si absent
};

}  // namespace voicelive::app
