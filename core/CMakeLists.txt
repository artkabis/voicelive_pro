# SPDX-License-Identifier: MIT
#
# Module `core` : logique métier pure, sans aucune dépendance externe ni JUCE.
# C'est volontaire — ce code doit compiler à l'identique en natif et en
# WebAssembly, et être testable à 100 % en isolation.

add_library(voicelive_core
    src/LooperTrack.cpp
    src/Transport.cpp
    src/ProjectSerializer.cpp
)
add_library(voicelive::core ALIAS voicelive_core)

target_include_directories(voicelive_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(voicelive_core PUBLIC cxx_std_20)

voicelive_set_target_warnings(voicelive_core)
voicelive_enable_sanitizers(voicelive_core)

# --- Tests -------------------------------------------------------------------
if(VOICELIVE_BUILD_TESTS)
    add_executable(core_tests
        tests/test_main.cpp
        tests/test_looper_track.cpp
        tests/test_audio_params.cpp
        tests/test_transport.cpp
        tests/test_project.cpp
        tests/test_project_serializer.cpp
    )
    target_link_libraries(core_tests PRIVATE voicelive::core voicelive::testing)

    voicelive_set_target_warnings(core_tests)
    voicelive_enable_sanitizers(core_tests)

    add_test(NAME core_tests COMMAND core_tests)
endif()
