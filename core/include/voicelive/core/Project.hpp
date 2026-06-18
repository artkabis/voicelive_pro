// SPDX-License-Identifier: MIT
//
// Project : agrégat racine de la session. Possède le transport et un nombre
// fixe de pistes de looper, plus la notion de piste sélectionnée. Toutes les
// invariantes (nombre de pistes, index valides) sont garanties par l'API.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "voicelive/core/LooperTrack.hpp"
#include "voicelive/core/Result.hpp"
#include "voicelive/core/Transport.hpp"

namespace voicelive::core {

class Project {
public:
    static constexpr std::size_t kMinTracks = 1;
    static constexpr std::size_t kMaxTracks = 8;

    /// Seule fabrique : valide le nombre de pistes. Le nom peut être vide.
    static Result<Project> create(std::string name, std::size_t trackCount) {
        if (trackCount < kMinTracks || trackCount > kMaxTracks) {
            return Result<Project>::failure(ErrorCode::OutOfRange,
                                            "Le nombre de pistes doit être dans [1, 8]");
        }
        return Project{std::move(name), trackCount};
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void setName(std::string name) noexcept { name_ = std::move(name); }

    [[nodiscard]] Transport& transport() noexcept { return transport_; }
    [[nodiscard]] const Transport& transport() const noexcept { return transport_; }

    [[nodiscard]] std::size_t trackCount() const noexcept { return tracks_.size(); }

    /// Accès borné : renvoie `nullptr` si l'index est invalide (jamais d'UB).
    [[nodiscard]] LooperTrack* track(std::size_t index) noexcept {
        return index < tracks_.size() ? &tracks_[index] : nullptr;
    }
    [[nodiscard]] const LooperTrack* track(std::size_t index) const noexcept {
        return index < tracks_.size() ? &tracks_[index] : nullptr;
    }

    [[nodiscard]] std::size_t selectedIndex() const noexcept { return selected_; }

    /// Sélectionne une piste ; rejette tout index hors bornes.
    Status selectTrack(std::size_t index) {
        if (index >= tracks_.size()) {
            return Status::failure(ErrorCode::OutOfRange, "Index de piste hors bornes");
        }
        selected_ = index;
        return Status::success();
    }

private:
    Project(std::string name, std::size_t trackCount)
        : name_(std::move(name)), tracks_(trackCount) {}

    std::string name_;
    Transport transport_;
    std::vector<LooperTrack> tracks_;
    std::size_t selected_ = 0;
};

}  // namespace voicelive::core
