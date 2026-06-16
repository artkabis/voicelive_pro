// SPDX-License-Identifier: MIT
//
// Transport musical : tempo, signature rythmique, état de lecture, et toutes
// les conversions temps musical ↔ échantillons. Sert de référence d'horloge
// pour la synchronisation et la quantification des boucles.
//
// Comme le reste de `core/`, c'est du C++ pur, testable sans matériel audio.
#pragma once

#include <cstddef>
#include <cstdint>

#include "voicelive/core/AudioParams.hpp"
#include "voicelive/core/Result.hpp"

namespace voicelive::core {

/// Tempo en noires par minute (BPM). Borné — une valeur hors plage est clampée
/// (entrée d'un curseur d'UI, jamais une erreur). Par défaut : 120 BPM.
class Bpm {
public:
    static constexpr double kMin = 20.0;
    static constexpr double kMax = 999.0;
    static constexpr double kDefault = 120.0;

    constexpr Bpm() noexcept = default;

    static Bpm fromValue(double bpm) noexcept {
        if (bpm < kMin) {
            return Bpm{kMin};
        }
        if (bpm > kMax) {
            return Bpm{kMax};
        }
        return Bpm{bpm};
    }

    [[nodiscard]] double value() const noexcept { return value_; }

    friend bool operator==(Bpm lhs, Bpm rhs) noexcept { return lhs.value_ == rhs.value_; }

private:
    explicit constexpr Bpm(double value) noexcept : value_(value) {}
    double value_ = kDefault;
};

/// Signature rythmique (numérateur / dénominateur). Le dénominateur doit être
/// une puissance de deux usuelle. Par défaut : 4/4.
class TimeSignature {
public:
    constexpr TimeSignature() noexcept = default;

    static Result<TimeSignature> create(unsigned numerator, unsigned denominator) {
        if (numerator < 1 || numerator > 32) {
            return Result<TimeSignature>::failure(ErrorCode::OutOfRange,
                                                  "Le numérateur doit être dans [1, 32]");
        }
        if (!isSupportedDenominator(denominator)) {
            return Result<TimeSignature>::failure(ErrorCode::OutOfRange,
                                                  "Le dénominateur doit être 1, 2, 4, 8, 16 ou 32");
        }
        return TimeSignature{numerator, denominator};
    }

    [[nodiscard]] unsigned numerator() const noexcept { return numerator_; }
    [[nodiscard]] unsigned denominator() const noexcept { return denominator_; }

    /// Nombre de noires (quarter notes) dans une mesure.
    [[nodiscard]] double beatsPerBar() const noexcept {
        return static_cast<double>(numerator_) * 4.0 / static_cast<double>(denominator_);
    }

private:
    static constexpr bool isSupportedDenominator(unsigned d) noexcept {
        return d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32;
    }
    constexpr TimeSignature(unsigned numerator, unsigned denominator) noexcept
        : numerator_(numerator), denominator_(denominator) {}

    unsigned numerator_ = 4;
    unsigned denominator_ = 4;
};

/// Grille de quantification.
enum class Grid : std::uint8_t { Beat, Bar };

/// État de lecture du transport.
enum class TransportState : std::uint8_t { Stopped, Playing };

class Transport {
public:
    Transport() = default;
    Transport(Bpm bpm, TimeSignature signature, SampleRate sampleRate) noexcept
        : bpm_(bpm), signature_(signature), sampleRate_(sampleRate) {}

    // --- Paramètres -------------------------------------------------------
    [[nodiscard]] Bpm bpm() const noexcept { return bpm_; }
    [[nodiscard]] TimeSignature signature() const noexcept { return signature_; }
    [[nodiscard]] SampleRate sampleRate() const noexcept { return sampleRate_; }

    void setBpm(Bpm bpm) noexcept { bpm_ = bpm; }
    void setSignature(TimeSignature signature) noexcept { signature_ = signature; }
    void setSampleRate(SampleRate sampleRate) noexcept { sampleRate_ = sampleRate; }

    // --- État -------------------------------------------------------------
    [[nodiscard]] bool isPlaying() const noexcept { return state_ == TransportState::Playing; }
    void play() noexcept { state_ = TransportState::Playing; }
    void stop() noexcept { state_ = TransportState::Stopped; }

    // --- Conversions temps musical ↔ échantillons -------------------------
    [[nodiscard]] double samplesPerBeat() const noexcept {
        return static_cast<double>(sampleRate_.hz()) * 60.0 / bpm_.value();
    }
    [[nodiscard]] double samplesPerBar() const noexcept {
        return samplesPerBeat() * signature_.beatsPerBar();
    }

    /// Arrondit une longueur (en échantillons) au plus proche multiple de la
    /// grille choisie, avec un minimum d'une unité de grille.
    [[nodiscard]] std::size_t quantizeToGrid(double lengthSamples, Grid grid) const noexcept;

    /// Choisit la longueur de boucle alignée sur un multiple musical d'une
    /// boucle de référence (¼, ½, 1×, 2×, 4×), au plus proche de l'enregistré.
    /// Si `reference` vaut 0, renvoie `recorded` inchangé.
    [[nodiscard]] static std::size_t chooseLoopMultiple(std::size_t recorded,
                                                        std::size_t reference) noexcept;

private:
    Bpm bpm_;
    TimeSignature signature_;
    SampleRate sampleRate_ = SampleRate::studio();
    TransportState state_ = TransportState::Stopped;
};

}  // namespace voicelive::core
