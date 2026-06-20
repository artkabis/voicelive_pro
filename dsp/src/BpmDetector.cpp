// SPDX-License-Identifier: MIT
#include "voicelive/dsp/BpmDetector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace voicelive::dsp {

BpmDetector::BpmDetector() noexcept : cfg_(Config{}) {}
BpmDetector::BpmDetector(Config cfg) noexcept : cfg_(cfg) {}

// ─── helpers privés ───────────────────────────────────────────────────────────

void BpmDetector::computeOdf(std::span<const float> audio, std::vector<float>& odf) const noexcept {
    const auto hop = static_cast<std::size_t>(cfg_.hopSize);
    const auto numFrame = audio.size() / hop;
    odf.assign(numFrame, 0.0F);

    float prevRms = 0.0F;
    for (std::size_t f = 0; f < numFrame; ++f) {
        const float* p = audio.data() + f * hop;
        float sumSq = 0.0F;
        for (std::size_t i = 0; i < hop; ++i) {
            sumSq += p[i] * p[i];
        }
        const float rms = std::sqrt(sumSq / static_cast<float>(hop));
        // Flux d'énergie semi-redressé (ne garde que les montées)
        odf[f] = std::max(0.0F, rms - prevRms);
        prevRms = rms;
    }
}

float BpmDetector::acf(std::span<const float> odf, int lag) noexcept {
    const auto n = static_cast<int>(odf.size());
    if (lag < 0 || lag >= n) {
        return 0.0F;
    }
    const int count = n - lag;
    float sum = 0.0F;
    for (int i = 0; i < count; ++i) {
        sum += odf[static_cast<std::size_t>(i)] *
               odf[static_cast<std::size_t>(i) + static_cast<std::size_t>(lag)];
    }
    // Normalisation par le nombre de paires pour comparer des lags différents
    return sum / static_cast<float>(count);
}

// ─── API publique ─────────────────────────────────────────────────────────────

std::optional<float> BpmDetector::detect(std::span<const float> audio) const noexcept {
    // Durée minimale : 2 temps complets au tempo le plus lent recherché
    const auto minSamples =
        static_cast<std::size_t>(2.0F * 60.0F / cfg_.minBpm * static_cast<float>(cfg_.sampleRate));
    if (audio.size() < minSamples) {
        return std::nullopt;
    }

    // Vérification du plancher de signal
    float sumSq = 0.0F;
    for (const float s : audio) {
        sumSq += s * s;
    }
    if (std::sqrt(sumSq / static_cast<float>(audio.size())) < cfg_.minRms) {
        return std::nullopt;
    }

    // Calcul de la fonction d'onset
    std::vector<float> odf;
    computeOdf(audio, odf);

    // Ancre de normalisation : énergie totale de l'ODF (ACF au lag 0)
    const float e0 = acf(odf, 0);
    if (e0 < 1e-10F) {
        return std::nullopt;
    }

    // Bornes de recherche (en trames ODF)
    const auto sr = static_cast<float>(cfg_.sampleRate);
    const auto hop = static_cast<float>(cfg_.hopSize);
    const int lagMin = static_cast<int>(std::ceil(60.0F / cfg_.maxBpm * sr / hop));
    const int lagMax = static_cast<int>(std::floor(60.0F / cfg_.minBpm * sr / hop));
    const int odfLen = static_cast<int>(odf.size());
    if (lagMin >= lagMax || lagMax >= odfLen) {
        return std::nullopt;
    }

    // Recherche du pic d'ACF dominant dans la plage de périodes de battement
    int bestLag = lagMin;
    float bestScore = -1.0F;
    for (int lag = lagMin; lag <= lagMax; ++lag) {
        const float a = acf(odf, lag) / e0;
        if (a > bestScore) {
            bestScore = a;
            bestLag = lag;
        }
    }

    // Rejeter si la périodicité n'est pas claire
    if (bestScore < 0.02F) {
        return std::nullopt;
    }

    const float rawBpm = 60.0F * sr / (static_cast<float>(bestLag) * hop);

    // Correction d'octave : essayer ×2, ÷2, ×1.5, ÷1.5.
    // Préférer un candidat dans la plage 80–160 BPM si son score ACF est
    // comparable (≥ 95 % du meilleur score) — corrige les erreurs d'octave
    // fréquentes quand le pattern a de forts accents sur le 1 et le 3.
    float finalBpm = rawBpm;
    float finalScore = bestScore;

    for (const float mult : {2.0F, 0.5F, 1.5F, 1.0F / 1.5F}) {
        const float cand = rawBpm * mult;
        if (cand < cfg_.minBpm || cand > cfg_.maxBpm) {
            continue;
        }
        const int cLag = static_cast<int>(std::round(60.0F / cand * sr / hop));
        if (cLag < 1 || cLag >= odfLen) {
            continue;
        }
        const float cScore = acf(odf, cLag) / e0;
        const bool inPreferred = (cand >= 80.0F && cand <= 160.0F);
        // Adopter le candidat si : il est dans la plage préférée ET son score
        // ACF est presque aussi bon (≥ 95 % du meilleur trouvé jusqu'ici)
        if (inPreferred && cScore >= finalScore * 0.95F) {
            finalBpm = cand;
            finalScore = cScore;
        }
    }

    return finalBpm;
}

std::optional<int> BpmDetector::phaseOffset(std::span<const float> audioA,
                                            std::span<const float> audioB) const noexcept {
    const auto bpmA = detect(audioA);
    const auto bpmB = detect(audioB);
    if (!bpmA || !bpmB) {
        return std::nullopt;
    }
    // Mesure de phase uniquement si les tempos correspondent à ± 5 %
    if (std::abs(*bpmA - *bpmB) / *bpmA > 0.05F) {
        return std::nullopt;
    }

    std::vector<float> odfA;
    std::vector<float> odfB;
    computeOdf(audioA, odfA);
    computeOdf(audioB, odfB);

    const auto sr = static_cast<float>(cfg_.sampleRate);
    const auto hop = static_cast<float>(cfg_.hopSize);
    const int beatLag = static_cast<int>(std::round(60.0F / *bpmA * sr / hop));
    if (beatLag < 1) {
        return std::nullopt;
    }

    // Corrélation croisée sur une période de battement
    const int n = static_cast<int>(std::min(odfA.size(), odfB.size()));
    const int maxShift = std::min(beatLag, n - 1);

    float bestXcf = -1.0F;
    int bestOff = 0;
    for (int d = 0; d <= maxShift; ++d) {
        const int count = n - d;
        float xcf = 0.0F;
        for (int i = 0; i < count; ++i) {
            xcf += odfA[static_cast<std::size_t>(i)] *
                   odfB[static_cast<std::size_t>(i) + static_cast<std::size_t>(d)];
        }
        xcf /= static_cast<float>(count);
        if (xcf > bestXcf) {
            bestXcf = xcf;
            bestOff = d;
        }
    }

    return bestOff * cfg_.hopSize;  // convertir en échantillons
}

}  // namespace voicelive::dsp
