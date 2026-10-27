cmake_minimum_required(VERSION 3.15)
project(XefirusChain VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.9
)
FetchContent_MakeAvailable(juce)

juce_add_plugin(XefirusChain
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE "Xefi"
    PLUGIN_CODE "Xefc"
    FORMATS VST3
    PRODUCT_NAME "Xefirus Chain")

target_compile_features(XefirusChain PRIVATE cxx_std_17)

target_sources(XefirusChain PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(XefirusChain PRIVATE Source Source/Metering Source/UI)

target_link_libraries(XefirusChain PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics
    RECOMMENDED)
