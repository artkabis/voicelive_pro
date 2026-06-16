# SPDX-License-Identifier: MIT
# Micro-framework de test partagé (header-only) pour tous les modules.
add_library(voicelive_testing INTERFACE)
add_library(voicelive::testing ALIAS voicelive_testing)
target_include_directories(voicelive_testing
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(voicelive_testing INTERFACE cxx_std_20)
