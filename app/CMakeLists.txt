# SPDX-License-Identifier: MIT
# Application desktop JUCE. Récupère JUCE via FetchContent (réseau requis).
# Cette cible n'est PAS construite par défaut : activer -DVOICELIVE_BUILD_APP=ON.

include(FetchContent)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

juce_add_gui_app(VoiceLiveApp
    PRODUCT_NAME "VoiceLive Pro"
    COMPANY_NAME "VoiceLive")

target_sources(VoiceLiveApp PRIVATE
    src/Main.cpp
    src/MainComponent.cpp
    src/HeadphoneMonitor.cpp
    src/AndroidMicCapture.cpp
    src/AndroidAudioOutput.cpp)

target_compile_features(VoiceLiveApp PRIVATE cxx_std_20)

target_compile_definitions(VoiceLiveApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="VoiceLive Pro"
    JUCE_APPLICATION_VERSION_STRING="2.0.0")

# Le moteur (voicelive::engine) tire core + dsp.
target_link_libraries(VoiceLiveApp PRIVATE
    voicelive::engine
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
