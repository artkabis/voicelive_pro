// SPDX-License-Identifier: MIT
//
// SampleFifo — file SPSC (un producteur, un consommateur) lock-free pour le
// transfert en bloc d'echantillons audio float.
//
// Difference avec RingBuffer<T> (meme dossier) : RingBuffer transporte des
// elements UN PAR UN (commandes UI -> audio). SampleFifo transporte des BLOCS
// d'echantillons (ecriture/lecture par lots via memcpy), ce qui est le bon
// outil pour relier deux threads audio de cadences differentes — typiquement
// la capture micro Android (AudioRecord, producteur) et le callback audio JUCE
// (consommateur).
//
// Contrat de concurrence (strict SPSC) :
//   - UN SEUL thread appelle write() (le producteur)
//   - UN SEUL thread appelle read() (le consommateur)
//   - reset() ne doit etre appele QUE lorsqu'aucun des deux threads n'est actif
//
// Les index sont des compteurs monotones uint32_t masques par (Capacite-1) ;
// la capacite doit donc etre une puissance de deux (masquage au lieu de modulo).
//
// PRIMITIVE PURE : write()/read() renvoient le nombre reellement transfere et
// ne tiennent AUCUN compteur de perte. La distinction entre "le tampon est
// plein, je vais reessayer" (contre-pression) et "le tampon est plein, j'abandonne
// ces echantillons" (drop) est une decision de POLITIQUE qui appartient a
// l'appelant. C'est donc l'appelant (ex. AndroidMicCapture) qui comptabilise
// les over-runs (drop) et under-runs (silence) a partir des valeurs de retour.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace voicelive::engine {

template <std::size_t Capacity>
class SampleFifo {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity doit etre une puissance de deux");
    static_assert(Capacity >= 2, "Capacity doit valoir au moins 2");

public:
    SampleFifo() = default;

    /// Nombre maximal d'echantillons stockables simultanement.
    /// Une case est reservee pour distinguer plein de vide.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

    /// Ecrit jusqu'a `count` echantillons. Appele UNIQUEMENT par le producteur.
    /// Renvoie le nombre reellement ecrit (< count si le tampon est plein) ;
    /// l'appelant decide s'il reessaie (contre-pression) ou abandonne (drop).
    std::size_t write(const float* src, std::size_t count) noexcept {
        const std::uint32_t w = write_.load(std::memory_order_relaxed);
        const std::uint32_t r = read_.load(std::memory_order_acquire);
        const std::size_t used = (w - r) & kMask;
        const std::size_t space = capacity() - used;
        const std::size_t n = std::min(count, space);
        if (n == 0) {
            return 0;
        }
        const std::uint32_t wMasked = w & kMask;
        const std::size_t first = std::min(n, static_cast<std::size_t>(Capacity - wMasked));
        std::memcpy(buf_.data() + wMasked, src, first * sizeof(float));
        if (first < n) {
            std::memcpy(buf_.data(), src + first, (n - first) * sizeof(float));
        }
        write_.store(w + static_cast<std::uint32_t>(n), std::memory_order_release);
        return n;
    }

    /// Lit jusqu'a `count` echantillons. Appele UNIQUEMENT par le consommateur.
    /// Renvoie le nombre reellement lu (< count si sous-remplissage) ; l'appelant
    /// complete generalement par du silence et comptabilise l'under-run.
    std::size_t read(float* dst, std::size_t count) noexcept {
        const std::uint32_t r = read_.load(std::memory_order_relaxed);
        const std::uint32_t w = write_.load(std::memory_order_acquire);
        const std::size_t avail = (w - r) & kMask;
        const std::size_t n = std::min(count, avail);
        if (n == 0) {
            return 0;
        }
        const std::uint32_t rMasked = r & kMask;
        const std::size_t first = std::min(n, static_cast<std::size_t>(Capacity - rMasked));
        std::memcpy(dst, buf_.data() + rMasked, first * sizeof(float));
        if (first < n) {
            std::memcpy(dst + first, buf_.data(), (n - first) * sizeof(float));
        }
        read_.store(r + static_cast<std::uint32_t>(n), std::memory_order_release);
        return n;
    }

    /// Nombre d'echantillons actuellement disponibles a la lecture.
    [[nodiscard]] std::size_t available() const noexcept {
        const std::uint32_t w = write_.load(std::memory_order_acquire);
        const std::uint32_t r = read_.load(std::memory_order_acquire);
        return (w - r) & kMask;
    }

    /// Vide la file. A n'appeler QUE lorsqu'aucun thread n'ecrit/lit (ex. avant
    /// de demarrer le thread producteur).
    void reset() noexcept {
        write_.store(0, std::memory_order_relaxed);
        read_.store(0, std::memory_order_relaxed);
    }

private:
    static constexpr std::uint32_t kMask = static_cast<std::uint32_t>(Capacity) - 1U;

    std::array<float, Capacity> buf_{};
    std::atomic<std::uint32_t> write_{0};  // ecrit par le producteur
    std::atomic<std::uint32_t> read_{0};   // ecrit par le consommateur
};

}  // namespace voicelive::engine
