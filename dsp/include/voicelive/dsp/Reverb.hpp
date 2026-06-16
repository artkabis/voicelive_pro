// SPDX-License-Identifier: MIT
//
// Reverb — algorithme Freeverb (Schroeder/Moorer) mono, en C++ pur.
//
// Contrairement à la v1 Python (boucle échantillon par échantillon dans le
// callback, allocations à chaque bloc), tous les buffers sont alloués une fois
// dans prepare() ; process() ne fait que lire/écrire dans ces buffers.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Effect.hpp"

namespace voicelive::dsp {

class Reverb final : public Effect {
public:
    void prepare(core::SampleRate sampleRate, std::size_t maxBlockSize) override;
    void process(std::span<float> block) noexcept override;
    void reset() noexcept override;

    // Paramètres (bornés à [0, 1]). Lus à chaque bloc, modifiables à tout moment.
    void setRoomSize(float value) noexcept;  ///< Taille/longueur de la queue.
    void setDamping(float value) noexcept;   ///< Atténuation des aigus.
    void setWet(float value) noexcept;       ///< Niveau du signal réverbéré.
    void setDry(float value) noexcept;       ///< Niveau du signal direct.

    [[nodiscard]] float roomSize() const noexcept { return roomSize_; }
    [[nodiscard]] float damping() const noexcept { return damping_; }
    [[nodiscard]] float wet() const noexcept { return wet_; }
    [[nodiscard]] float dry() const noexcept { return dry_; }

private:
    /// Filtre en peigne (comb) avec amortissement passe-bas dans la boucle.
    struct Comb {
        std::vector<float> buffer;
        std::size_t pos = 0;
        float store = 0.0F;
        float process(float input, float feedback, float damp) noexcept;
        void clear() noexcept;
    };

    /// Filtre passe-tout (allpass) pour diffuser la réverbération.
    struct Allpass {
        std::vector<float> buffer;
        std::size_t pos = 0;
        float process(float input) noexcept;
        void clear() noexcept;
    };

    static constexpr std::size_t kCombCount = 8;
    static constexpr std::size_t kAllpassCount = 4;

    std::vector<Comb> combs_;
    std::vector<Allpass> allpasses_;

    float roomSize_ = 0.5F;
    float damping_ = 0.5F;
    float wet_ = 0.3F;
    float dry_ = 0.7F;
};

}  // namespace voicelive::dsp
