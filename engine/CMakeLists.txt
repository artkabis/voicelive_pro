# SPDX-License-Identifier: MIT
# Module `engine` : briques temps réel (ring buffer, stockage de boucle, mixage,
# processeur de piste). Dépend de `core`. Toujours sans dépendance externe.

add_library(voicelive_engine
    src/Mixer.cpp
    src/LooperEngine.cpp
    src/ChannelUtils.cpp
    src/Metronome.cpp
)
add_library(voicelive::engine ALIAS voicelive_engine)

target_include_directories(voicelive_engine
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(voicelive_engine PUBLIC voicelive::core voicelive::dsp)
target_compile_features(voicelive_engine PUBLIC cxx_std_20)

voicelive_set_target_warnings(voicelive_engine)
voicelive_enable_sanitizers(voicelive_engine)

if(VOICELIVE_BUILD_TESTS)
    add_executable(engine_tests
        tests/test_main.cpp
        tests/test_ring_buffer.cpp
        tests/test_loop_audio.cpp
        tests/test_mixer.cpp
        tests/test_track_processor.cpp
        tests/test_looper_engine.cpp
        tests/test_channel_utils.cpp
        tests/test_metronome.cpp
    )
    target_link_libraries(engine_tests PRIVATE voicelive::engine voicelive::testing)

    voicelive_set_target_warnings(engine_tests)
    voicelive_enable_sanitizers(engine_tests)

    add_test(NAME engine_tests COMMAND engine_tests)
endif()
