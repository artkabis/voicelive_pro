# SPDX-License-Identifier: MIT
# Module `dsp` : effets temps réel en C++ pur. Dépend uniquement de `core`.

add_library(voicelive_dsp
    src/Reverb.cpp
)
add_library(voicelive::dsp ALIAS voicelive_dsp)

target_include_directories(voicelive_dsp
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(voicelive_dsp PUBLIC voicelive::core)
target_compile_features(voicelive_dsp PUBLIC cxx_std_20)

voicelive_set_target_warnings(voicelive_dsp)
voicelive_enable_sanitizers(voicelive_dsp)

if(VOICELIVE_BUILD_TESTS)
    add_executable(dsp_tests
        tests/test_main.cpp
        tests/test_reverb.cpp
    )
    target_link_libraries(dsp_tests PRIVATE voicelive::dsp voicelive::testing)

    voicelive_set_target_warnings(dsp_tests)
    voicelive_enable_sanitizers(dsp_tests)

    add_test(NAME dsp_tests COMMAND dsp_tests)
endif()
