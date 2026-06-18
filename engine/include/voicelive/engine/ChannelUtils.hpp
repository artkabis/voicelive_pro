// SPDX-License-Identifier: MIT
//
// ChannelUtils — conversion entre les buffers multicanaux du système audio
// (façon JUCE : tableaux de pointeurs de canaux) et le signal mono du moteur.
//
// Fonctions pures, `noexcept`, sans allocation : utilisables dans le callback.
// Isolées ici pour être testées SANS dépendance à JUCE (le câblage JUCE n'est
// alors plus qu'une fine couche d'appel).
#pragma once

#include <cstddef>
#include <span>

namespace voicelive::engine::channels {

/// Réduit `numChannels` canaux d'entrée en un signal mono (moyenne).
/// `inputChannels[c]` doit pointer sur au moins `mono.size()` échantillons.
/// Sort du silence si aucune entrée valide.
void downmixToMono(std::span<float> mono, const float* const* inputChannels,
                   std::size_t numChannels) noexcept;

/// Recopie le signal mono sur chacun des `numChannels` canaux de sortie.
/// Ignore les pointeurs de canaux nuls.
void spreadToChannels(float* const* outputChannels, std::size_t numChannels,
                      std::span<const float> mono) noexcept;

}  // namespace voicelive::engine::channels
