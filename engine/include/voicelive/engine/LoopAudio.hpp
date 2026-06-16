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
    void clear() noexcept { length_ = 0; }

    [[nodiscard]] std::size_t capacity() const noexcept { return samples_.size(); }
    [[nodiscard]] std::size_t length() const noexcept { return length_; }
    [[nodiscard]] bool empty() const noexcept { return length_ == 0; }

    /// Ajoute des échantillons (enregistrement), borné par la capacité restante.
    /// Renvoie le nombre réellement écrit.
    std::size_t append(std::span<const float> input) noexcept {
        const std::size_t room = samples_.size() - length_;
        const std::size_t count = std::min(input.size(), room);
        for (std::size_t i = 0; i < count; ++i) {
            samples_[length_ + i] = input[i];
        }
        length_ += count;
        return count;
    }

    /// Lit `output.size()` échantillons en bouclant à partir de `position`.
    /// Renvoie la nouvelle position de lecture. Sortie silencieuse si vide.
    std::size_t readLooped(std::span<float> output, std::size_t position) const noexcept {
        if (length_ == 0) {
            std::fill(output.begin(), output.end(), 0.0F);
            return 0;
        }
        std::size_t pos = position % length_;
        for (float& sample : output) {
            sample = samples_[pos];
            pos = (pos + 1) % length_;
        }
        return pos;
    }

    /// Superpose (somme) un signal sur le contenu existant à partir de
    /// `position`, en bouclant. Renvoie la nouvelle position. No-op si vide.
    std::size_t overdub(std::span<const float> input, std::size_t position) noexcept {
        if (length_ == 0) {
            return 0;
        }
        std::size_t pos = position % length_;
        for (const float value : input) {
            samples_[pos] += value;
            pos = (pos + 1) % length_;
        }
        return pos;
    }

    /// Lecture d'un échantillon (tests / inspection).
    [[nodiscard]] float sampleAt(std::size_t index) const noexcept { return samples_[index]; }

private:
    std::vector<float> samples_;
    std::size_t length_ = 0;
};

}  // namespace voicelive::engine
