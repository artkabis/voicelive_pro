// SPDX-License-Identifier: MIT
//
// RingBuffer — file SPSC (un producteur, un consommateur) lock-free.
//
// C'est le canal de communication UI → thread audio : les commandes (changer un
// gain, déclencher un enregistrement…) y sont déposées sans verrou, donc sans
// risque de blocage du callback audio. C'est l'application directe du contrat
// « aucun verrou dans le thread temps réel ».
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace voicelive::engine {

template <typename T>
class RingBuffer {
public:
    /// Capacité utile = nombre d'éléments stockables simultanément.
    explicit RingBuffer(std::size_t capacity) : slots_(capacity + 1) {}

    /// Déposé par le producteur. Renvoie false si plein (jamais bloquant).
    bool push(const T& item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = increment(head, slots_.size());
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // plein
        }
        slots_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    /// Retiré par le consommateur (thread audio). Renvoie false si vide.
    bool pop(T& out) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // vide
        }
        out = slots_[tail];
        tail_.store(increment(tail, slots_.size()), std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size() - 1; }

private:
    static std::size_t increment(std::size_t index, std::size_t size) noexcept {
        return (index + 1) % size;
    }

    std::vector<T> slots_;
    std::atomic<std::size_t> head_{0};  // écrit par le producteur
    std::atomic<std::size_t> tail_{0};  // écrit par le consommateur
};

}  // namespace voicelive::engine
