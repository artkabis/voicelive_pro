// SPDX-License-Identifier: MIT
//
// EffectChain — chaîne d'effets ordonnée, appliquée séquentiellement à un bloc.
//
// L'allocation a lieu à l'ajout d'un effet et dans prepare() (hors temps réel).
// process() ne fait qu'enchaîner les process() des effets : aucune allocation.
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/dsp/Effect.hpp"

namespace voicelive::dsp {

class EffectChain {
public:
    /// Prépare la chaîne (et tout effet déjà présent). Hors temps réel.
    void prepare(core::SampleRate sampleRate, std::size_t maxBlockSize);

    /// Ajoute un effet en fin de chaîne. S'il est ajouté après prepare(),
    /// l'effet est préparé immédiatement. Ignore un pointeur nul.
    void add(std::unique_ptr<Effect> effect);

    /// Applique tous les effets dans l'ordre, sur place. Temps réel.
    void process(std::span<float> block) noexcept;

    /// Réinitialise l'état interne de chaque effet (sans réallouer).
    void reset() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return effects_.size(); }
    [[nodiscard]] bool empty() const noexcept { return effects_.empty(); }
    void clear() noexcept { effects_.clear(); }

private:
    std::vector<std::unique_ptr<Effect>> effects_;
    core::SampleRate sampleRate_ = core::SampleRate::studio();
    std::size_t maxBlockSize_ = 0;
    bool prepared_ = false;
};

}  // namespace voicelive::dsp
