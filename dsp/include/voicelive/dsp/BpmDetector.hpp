// SPDX-License-Identifier: MIT
#pragma once

#include <optional>
#include <span>
#include <vector>

namespace voicelive::dsp {

/// Détecte le tempo dominant (BPM) d'un signal audio par autocorrélation de
/// l'enveloppe d'onset — même principe que libaubio/Essentia en mode lightweight.
///
/// Algorithme (entièrement offline, aucune allocation dans detect()) :
///   1. Découpe l'audio en trames de `hopSize` échantillons.
///   2. Calcule le RMS par trame et en extrait la fonction d'onset (ODF) :
///      flux d'énergie semi-redressé (max(0, RMS[n] − RMS[n-1])).
///   3. Calcule l'autocorrélation normalisée (ACF) de l'ODF sur la plage de
///      périodes correspondant à [minBpm, maxBpm].
///   4. Sélectionne le pic dominant et applique une correction d'octave
///      (préférence 80–160 BPM).
///
/// Complexité :
///   - ODF  : O(|audio|) — une passe linéaire.
///   - ACF  : O(N × ΔLag) où N = |audio|/hopSize et ΔLag ≪ N.
///   - Pour 30 s @ 48 kHz, hop=512 : ~185 000 multiplications — négligeable.
///
/// Précision typique : ±1 BPM sur contenu percussif, ±5 BPM sur contenu
/// mélodique, pour 4 s+ d'audio.
class BpmDetector {
public:
    struct Config {
        unsigned sampleRate = 48000;
        int      hopSize    = 512;    ///< échantillons par trame ODF (~10.7 ms @ 48 kHz)
        float    minBpm     = 60.0F;
        float    maxBpm     = 200.0F;
        float    minRms     = 1e-4F;  ///< plancher de signal (en dessous → nullopt)
    };

    BpmDetector() noexcept;                     ///< configuration par défaut
    explicit BpmDetector(Config cfg) noexcept;  ///< configuration personnalisée

    /// Détecte le BPM dominant. Retourne nullopt si :
    ///   – l'audio est plus court que 2 temps à minBpm,
    ///   – le signal est sous le plancher de bruit,
    ///   – aucune structure périodique claire n'est trouvée.
    [[nodiscard]] std::optional<float>
    detect(std::span<const float> audio) const noexcept;

    /// Retourne le décalage de phase (en échantillons) qui aligne au mieux
    /// la grille rythmique de audioB sur celle de audioA, sur une période de
    /// battement. Utilise la corrélation croisée des ODFs.
    /// Retourne nullopt si l'un des deux pistes ne contient pas de tempo détectable
    /// ou si leurs tempos divergent de plus de 5 %.
    [[nodiscard]] std::optional<int>
    phaseOffset(std::span<const float> audioA,
                std::span<const float> audioB) const noexcept;

private:
    Config cfg_;

    /// Remplit `odf` avec la fonction d'onset RMS semi-redressée.
    void computeOdf(std::span<const float> audio,
                    std::vector<float>&    odf) const noexcept;

    /// Valeur d'ACF normalisée au délai `lag` (en trames ODF).
    [[nodiscard]] float acf(std::span<const float> odf,
                            int                    lag) const noexcept;
};

}  // namespace voicelive::dsp
