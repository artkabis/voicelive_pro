# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.24)

project(VoiceLivePro
    VERSION 2.0.0
    DESCRIPTION "Moteur audio temps réel multiplateforme (desktop / mobile / web)"
    LANGUAGES CXX)

# --- Standard & politiques ---------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)            # pas d'extensions GNU : code portable
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # alimente clang-tidy / IDE

# Garde-fou : interdire les builds in-source.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Build in-source interdit. Utilisez un dossier build/ dédié.")
endif()

# --- Options -----------------------------------------------------------------
option(VOICELIVE_BUILD_TESTS "Compiler les tests unitaires du cœur" ON)
option(VOICELIVE_WARNINGS_AS_ERRORS "Traiter les warnings comme des erreurs" ON)
option(VOICELIVE_ENABLE_SANITIZERS "Activer ASan/UBSan" OFF)
# L'app JUCE récupère JUCE via le réseau et nécessite les libs système (X11,
# ALSA…). Désactivée par défaut pour garder le build du cœur autonome.
option(VOICELIVE_BUILD_APP "Compiler l'application desktop JUCE" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

if(VOICELIVE_BUILD_TESTS)
    enable_testing()
endif()

# --- Modules -----------------------------------------------------------------
# `core`   : logique métier pure C++, zéro dépendance, compile natif ET WASM.
# `dsp`    : effets temps réel en C++ pur (s'appuie sur les types de `core`).
# `engine` : briques temps réel (ring buffer, boucles, mixage, processeur).
# La couche `app` (JUCE / shell web WASM) viendra ensuite.
if(VOICELIVE_BUILD_TESTS)
    add_subdirectory(testing)
endif()
add_subdirectory(core)
add_subdirectory(dsp)
add_subdirectory(engine)

# Couche application JUCE (desktop / mobile) — opt-in.
if(VOICELIVE_BUILD_APP)
    add_subdirectory(app)
endif()
