// SPDX-License-Identifier: MIT
//
// Types forts à invariants pour les paramètres audio.
//
// Principe : rendre les états invalides *non représentables*. Un `Gain` ou un
// `SampleRate` ne peut exister que s'il est valide — la validation a lieu une
// seule fois, à la construction (`create`), et plus jamais ensuite.
#pragma once

#include "voicelive/core/Result.hpp"

namespace voicelive::core {

/// Fréquence d'échantillonnage validée (en Hz). Toujours > 0.
class SampleRate {
public:
    /// Valeurs usuelles, fournies par commodité.
    static constexpr unsigned kCd = 44'100;
    static constexpr unsigned kStudio = 48'000;

    /// Seule fabrique : refuse toute valeur nulle.
    static Result<SampleRate> create(unsigned hz) {
        if (hz == 0) {
            return Result<SampleRate>::failure(ErrorCode::OutOfRange,
                                               "La fréquence d'échantillonnage doit être > 0 Hz");
        }
        return SampleRate{hz};
    }

    [[nodiscard]] unsigned hz() const noexcept { return hz_; }

    friend bool operator==(SampleRate lhs, SampleRate rhs) noexcept { return lhs.hz_ == rhs.hz_; }

private:
    explicit constexpr SampleRate(unsigned hz) noexcept : hz_(hz) {}
    unsigned hz_;
};

/// Gain linéaire borné. 1.0 = unité (0 dB). Bornes : [0, kMaxLinear].
/// Une construction hors bornes est *clampée* (jamais d'échec) : on ne veut
/// pas qu'un curseur d'UI puisse produire une erreur, mais on garantit
/// qu'aucune valeur dangereuse ne traverse la chaîne audio.
class Gain {
public:
    static constexpr float kMaxLinear = 4.0F;  ///< ≈ +12 dB.
    static constexpr float kUnity = 1.0F;      ///< 0 dB.
    static constexpr float kSilence = 0.0F;

    static Gain fromLinear(float linear) noexcept {
        if (linear < kSilence) {
            return Gain{kSilence};
        }
        if (linear > kMaxLinear) {
            return Gain{kMaxLinear};
        }
        return Gain{linear};
    }

    static Gain unity() noexcept { return Gain{kUnity}; }
    static Gain silence() noexcept { return Gain{kSilence}; }

    [[nodiscard]] float linear() const noexcept { return linear_; }
    [[nodiscard]] bool isSilent() const noexcept { return linear_ == kSilence; }

    friend bool operator==(Gain lhs, Gain rhs) noexcept { return lhs.linear_ == rhs.linear_; }

private:
    explicit constexpr Gain(float linear) noexcept : linear_(linear) {}
    float linear_;
};

}  // namespace voicelive::core
