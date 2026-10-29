cmake_minimum_required(VERSION 3.22)
project(Kickforge VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8
    GIT_SHALLOW TRUE)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE Catch2)

# ---- shared engine sources (plugin + tests compile the same list) ----
set(KICKFORGE_ENGINE_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Params.cpp
    Source/core/Curve.cpp
    Source/core/Tuning.cpp
    Source/core/GlobParams.cpp
    Source/synth/SubParams.cpp
    Source/synth/SubVoice.cpp
    Source/click/ClickParams.cpp
    Source/click/ClickLayer.cpp
    Source/fx/FxParams.cpp
    Source/fx/FxChain.cpp
    Source/fx/FxSlot.cpp)

juce_add_plugin(Kickforge
    COMPANY_NAME "Kickforge"
    PLUGIN_MANUFACTURER_CODE Kfrg
    PLUGIN_CODE Kkfg
    PRODUCT_NAME "Kickforge"
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Synth Drum
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Kickforge PRIVATE
    ${KICKFORGE_ENGINE_SOURCES}
    Source/PluginEntry.cpp)
target_include_directories(Kickforge PRIVATE Source)
target_compile_definitions(Kickforge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Kickforge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- tests ----
enable_testing()
add_executable(KickforgeTests
    ${KICKFORGE_ENGINE_SOURCES}
    tests/CurveTests.cpp
    tests/TuningTests.cpp
    tests/RenderTests.cpp
    tests/FxTests.cpp)
target_include_directories(KickforgeTests PRIVATE Source)
target_compile_definitions(KickforgeTests PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(KickforgeTests PRIVATE
    Catch2::Catch2WithMain
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
add_test(NAME KickforgeTests COMMAND KickforgeTests)
