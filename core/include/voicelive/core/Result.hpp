// SPDX-License-Identifier: MIT
//
// Result / Status / Error — gestion d'erreur explicite, sans exceptions.
//
// Choix de conception :
//  - Aucune méthode métier ne lève d'exception : tout chemin d'échec est
//    matérialisé par une valeur de retour `Status` ou `Result<T>`.
//  - `[[nodiscard]]` force l'appelant à traiter le résultat : impossible
//    d'ignorer une erreur par mégarde.
//  - Pas de dépendance externe : ce header compile en natif ET en WebAssembly.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace voicelive::core {

/// Catégorie machine d'une erreur. Stable : ne jamais réordonner, seulement
/// ajouter à la fin (les valeurs peuvent être journalisées/sérialisées).
enum class ErrorCode : std::uint8_t {
    InvalidTransition,  ///< Transition d'état interdite par la machine métier.
    OutOfRange,         ///< Valeur hors des bornes autorisées d'un type fort.
    InvalidArgument,    ///< Argument incohérent fourni par l'appelant.
};

/// Erreur métier : un code exploitable par le programme + un message humain.
struct Error {
    ErrorCode code;
    std::string message;
};

/// Résultat d'une opération qui ne renvoie pas de valeur (succès ou erreur).
/// Équivalent d'un `Result<void>`.
class [[nodiscard]] Status {
public:
    /// Construit un statut de succès.
    Status() noexcept = default;

    /// Construit un statut d'échec.
    explicit Status(Error error) : error_(std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }

    /// Précondition : `!ok()`. Accès à l'erreur sous-jacente.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) — garanti par la précondition.
    [[nodiscard]] const Error& error() const { return *error_; }

    static Status success() noexcept { return Status{}; }
    static Status failure(ErrorCode code, std::string message) {
        return Status{Error{code, std::move(message)}};
    }

private:
    std::optional<Error> error_;
};

/// Résultat d'une opération renvoyant une valeur `T`, ou une erreur.
template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : data_(std::move(value)) {}      // NOLINT(google-explicit-constructor)
    Result(Error error) : data_(std::move(error)) {}  // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(data_); }
    explicit operator bool() const noexcept { return ok(); }

    /// Précondition : `ok()`. Accès à la valeur (mutable sur un `Result` lvalue,
    /// constant sinon) — évite toute copie inutile de la valeur transportée.
    [[nodiscard]] T& value() & { return std::get<T>(data_); }
    [[nodiscard]] const T& value() const& { return std::get<T>(data_); }

    /// Précondition : `!ok()`.
    [[nodiscard]] const Error& error() const { return std::get<Error>(data_); }

    static Result failure(ErrorCode code, std::string message) {
        return Result{Error{code, std::move(message)}};
    }

private:
    std::variant<T, Error> data_;
};

}  // namespace voicelive::core
