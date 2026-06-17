// SPDX-License-Identifier: MIT
//
// Lecture / écriture de fichiers WAV (RIFF/WAVE), en C++ pur.
//
// Écriture : PCM 16 bits (format le plus compatible). Lecture : PCM 16 bits et
// IEEE float 32 bits, mono ou multicanal. Les fichiers importés sont une surface
// d'attaque : le parseur est borné et valide tout (voir SECURITY.md).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "voicelive/core/Result.hpp"

namespace voicelive::engine::wav {

/// Échantillons entrelacés en virgule flottante [-1, 1], + métadonnées.
struct AudioData {
    std::vector<float> samples;  // entrelacé (frame = `channels` échantillons)
    unsigned sampleRate = 48000;
    unsigned channels = 1;

    [[nodiscard]] std::size_t frameCount() const noexcept {
        return channels == 0 ? 0 : samples.size() / channels;
    }
};

/// Écrit un WAV PCM 16 bits. Erreur si paramètres invalides ou I/O échouée.
[[nodiscard]] core::Status write(const std::string& path, const AudioData& data);

/// Lit un WAV (PCM 16 bits ou float 32 bits). Erreur si fichier illisible ou
/// malformé / format non supporté.
[[nodiscard]] core::Result<AudioData> read(const std::string& path);

}  // namespace voicelive::engine::wav
