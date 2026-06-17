// SPDX-License-Identifier: MIT
//
// LoopAudio — stockage mono d'une boucle, à capacité fixe.
//
// La mémoire est allouée une seule fois dans prepare() (hors thread audio). Les
// opérations temps réel (append / readLooped / overdub) ne font AUCUNE
// allocation : elles écrivent/lisent dans le buffer pré-alloué, dans la limite
// de la capacité. C'est ce qui rend l'enregistrement et la lecture sûrs dans le
// callback.
#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace voicelive::engine {

class LoopAudio {
public:
    /// Alloue la capacité maximale (en échantillons). À appeler hors temps réel.
    void prepare(std::size_t capacitySamples) {
        samples_.assign(capacitySamples, 0.0F);
        length_ = 0;
    }

    /// Vide le contenu sans libérer la mémoire (réutilisable immédiatement).
    void clear() noexcept {
        length_ = 0;
        loopLength_ = 0;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return samples_.size(); }
    [[nodiscard]] std::size_t length() const noexcept { return length_; }
    [[nodiscard]] std::size_t loopLength() const noexcept { return loopLength_; }
    [[nodiscard]] bool empty() const noexcept { return length_ == 0; }

    /// Ajoute des échantillons (enregistrement), borné par la capacité restante.
    /// Renvoie le nombre réellement écrit. La période de boucle suit le contenu.
    std::size_t append(std::span<const float> input) noexcept {
        const std::size_t room = samples_.size() - length_;
        const std::size_t count = std::min(input.size(), room);
        for (std::size_t i = 0; i < count; ++i) {
            samples_[length_ + i] = input[i];
        }
        length_ += count;
        loopLength_ = length_;
        return count;
    }

    /// Force la période de boucle (pour la synchronisation : alignement sur un
    /// multiple musical). Bornée à [1, capacité] ; au-delà du contenu, la boucle
    /// est complétée par du silence (le buffer est déjà à zéro).
    void setLoopLength(std::size_t length) noexcept {
        if (samples_.empty()) {
            return;
        }
        loopLength_ = std::clamp<std::size_t>(length, 1, samples_.size());
    }

    /// Lit `output.size()` échantillons en bouclant à partir de `position`.
    /// Renvoie la nouvelle position de lecture. Sortie silencieuse si vide.
    std::size_t readLooped(std::span<float> output, std::size_t position) const noexcept {
        if (loopLength_ == 0) {
            std::fill(output.begin(), output.end(), 0.0F);
            return 0;
        }
        std::size_t pos = position % loopLength_;
        for (float& sample : output) {
            sample = samples_[pos];
            pos = (pos + 1) % loopLength_;
        }
        return pos;
    }

    /// Superpose (somme) un signal sur le contenu existant à partir de
    /// `position`, en bouclant. Renvoie la nouvelle position. No-op si vide.
    std::size_t overdub(std::span<const float> input, std::size_t position) noexcept {
        if (loopLength_ == 0) {
            return 0;
        }
        std::size_t pos = position % loopLength_;
        for (const float value : input) {
            samples_[pos] += value;
            pos = (pos + 1) % loopLength_;
        }
        return pos;
    }

    /// Lecture d'un échantillon (tests / inspection).
    [[nodiscard]] float sampleAt(std::size_t index) const noexcept { return samples_[index]; }

private:
    std::vector<float> samples_;
    std::size_t length_ = 0;      // échantillons enregistrés
    std::size_t loopLength_ = 0;  // période de boucle effective (≤ capacité)
};

}  // namespace voicelive::engine
