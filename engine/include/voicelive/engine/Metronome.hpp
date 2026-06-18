// SPDX-License-Identifier: MIT
//
// Metronome — générateur de clics temps réel piloté par le Transport.
//
// Émet un clic à chaque temps (accent sur le 1er temps de la mesure). Tout est
// pré-dimensionné dans prepare() ; process() n'alloue rien et ajoute les clics
// au bloc fourni (pour se mixer avec les pistes).
#pragma once

#include <cstddef>
#include <span>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/Transport.hpp"

namespace voicelive::engine {

class Metronome {
public:
    void prepare(core::SampleRate sampleRate, std::size_t maxBlockSize);

    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }
    void setEnabled(bool enabled) noexcept {
        enabled_ = enabled;
        if (enabled) {
            reset();  // (re)démarre aligné sur un temps
        }
    }
    void setGain(core::Gain gain) noexcept { gain_ = gain; }

    /// Remet la grille et la voix de clic à zéro (sans réallouer).
    void reset() noexcept;

    /// Ajoute les clics du métronome dans `out`, selon le tempo/signature du
    /// transport. Temps réel : aucune allocation. No-op si désactivé.
    void process(std::span<float> out, const core::Transport& transport) noexcept;

private:
    void advanceBeat(double samplesPerBeat, long beatsPerBar) noexcept;
    void triggerClick(bool accent) noexcept;
    [[nodiscard]] float renderClickSample() noexcept;

    // Configuration (fixée dans prepare()).
    unsigned sampleRate_ = 0;
    std::size_t clickLength_ = 0;  // durée du clic en échantillons
    double decaySamples_ = 1.0;    // constante de décroissance

    // Paramètres.
    bool enabled_ = false;
    core::Gain gain_ = core::Gain::unity();

    // État de la grille.
    bool started_ = false;
    double beatPhase_ = 0.0;  // position dans le temps courant [0, samplesPerBeat)
    long beatCount_ = 0;      // index de temps depuis le démarrage

    // État de la voix de clic.
    bool clickActive_ = false;
    std::size_t clickPhase_ = 0;
    float clickFreq_ = 1000.0F;
    float clickAmp_ = 0.6F;
};

}  // namespace voicelive::engine
