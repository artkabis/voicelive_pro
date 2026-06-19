// SPDX-License-Identifier: MIT
//
// AudioDeviceHint -- heuristique de detection casque sans dependance JUCE ni OS.
//
// Prend le nom et le type du peripherique audio de sortie (chaines brutes issues
// de JUCE ou d'un stub de test) et renvoie true si le signal suggere un casque
// ou ecouteurs plutot que le haut-parleur integre.
//
// Conception :
//   - Aucune allocation : string_view en entree, bool en sortie.
//   - Insensible a la casse : tolower() applique avant chaque comparaison.
//   - Testable offline : pas de JUCE, pas d'OS, compilable en natif pur.
#pragma once

#include <string_view>

namespace voicelive::engine {

/// Renvoie true si deviceName ou typeName indique un casque, ecouteurs ou
/// un peripherique audio USB (couvre USB-C DAC/headset et jack 3.5mm).
[[nodiscard]] bool looksLikeHeadphones(std::string_view deviceName,
                                       std::string_view typeName) noexcept;

}  // namespace voicelive::engine
