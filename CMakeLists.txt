cmake_minimum_required(VERSION 3.22)
project(HeadroomV2 VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is downloaded automatically at configure time -- no manual install needed.
include(FetchContent)
FetchContent_Declare(
    JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.14.tar.gz
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(HeadroomV2
    COMPANY_NAME "MyPlugins"            # change to whatever you like
    PLUGIN_MANUFACTURER_CODE Mypl       # 4 chars, first uppercase
    PLUGIN_CODE Hdt2                    # new ID: v2 installs alongside v1
    FORMATS VST3                        # on a Mac you can add AU here too: FORMATS VST3 AU
    PRODUCT_NAME "Headroom v2"          # name shown in Ableton's browser
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics
)

target_sources(HeadroomV2 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(HeadroomV2 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(HeadroomV2
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Optional console test that hammers the No Startles engine with random bursts
# and asserts that no output sample ever exceeds the Headroom ceiling.
# Enable with: cmake -B build -DHEADROOM_BUILD_TESTS=ON
option(HEADROOM_BUILD_TESTS "Build the lookahead engine ceiling test" OFF)

if(HEADROOM_BUILD_TESTS)
    juce_add_console_app(EngineTest PRODUCT_NAME "EngineTest")
    target_sources(EngineTest PRIVATE Tests/EngineTest.cpp)
    target_compile_definitions(EngineTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(EngineTest PRIVATE
        juce::juce_dsp
        juce::juce_recommended_config_flags
    )
endif()
