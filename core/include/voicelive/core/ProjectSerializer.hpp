// SPDX-License-Identifier: MIT
//
// Sérialisation de Project : sauvegarde/chargement des RÉGLAGES (nom, transport,
// gains/mute, sélection). Format texte versionné, sans dépendance externe — le
// cœur reste portable (natif + WASM).
//
// Les données audio (boucles) ne sont PAS sérialisées ici : c'est le rôle de
// l'export audio (fonctionnalité distincte).
#pragma once

#include <string>
#include <string_view>

#include "voicelive/core/Project.hpp"
#include "voicelive/core/Result.hpp"

namespace voicelive::core::project_io {

/// Sérialise les réglages d'un projet en texte (format « VOICELIVE_PROJECT v1 »).
[[nodiscard]] std::string serialize(const Project& project);

/// Reconstruit un projet depuis le texte. Erreur si l'en-tête, un champ requis
/// ou une valeur est invalide.
[[nodiscard]] Result<Project> deserialize(std::string_view text);

/// Écrit le projet sérialisé dans un fichier.
[[nodiscard]] Status saveToFile(const std::string& path, const Project& project);

/// Charge un projet depuis un fichier.
[[nodiscard]] Result<Project> loadFromFile(const std::string& path);

}  // namespace voicelive::core::project_io
