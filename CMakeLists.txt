cmake_minimum_required(VERSION 3.22)
project(CarcosaSim VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(CARCOSA_BUILD_TESTS "Build the offline DSP test" OFF)
option(CARCOSA_MINIMAL_X11 "Linux only: build without Xrandr/Xinerama/Xcursor headers" OFF)

# JUCE: use a local ./JUCE checkout if present, otherwise download it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(CARCOSA_FORMATS VST3)
if(APPLE)
    list(APPEND CARCOSA_FORMATS AU)
endif()

juce_add_plugin(CarcosaSim
    COMPANY_NAME "CarcosaSim"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Crcm
    PLUGIN_CODE Crc1
    FORMATS ${CARCOSA_FORMATS}
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
    PRODUCT_NAME "Carcosa Sim")

target_sources(CarcosaSim PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/NoiseGate.h
    Source/CarcosaDSP.h)

target_compile_definitions(CarcosaSim PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(CARCOSA_MINIMAL_X11)
    target_compile_definitions(CarcosaSim PUBLIC
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
endif()

target_link_libraries(CarcosaSim
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(CARCOSA_BUILD_TESTS)
    add_executable(dsp_test Tests/dsp_test.cpp)
    target_include_directories(dsp_test PRIVATE Source)
    target_compile_definitions(dsp_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(dsp_test PRIVATE juce::juce_dsp juce::juce_recommended_config_flags)

    add_executable(gate_test Tests/gate_test.cpp)
    target_include_directories(gate_test PRIVATE Source)
    target_compile_definitions(gate_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(gate_test PRIVATE juce::juce_dsp juce::juce_recommended_config_flags)
endif()
