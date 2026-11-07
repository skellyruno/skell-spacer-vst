cmake_minimum_required(VERSION 3.22)
project(SkellCoreVSTTemplate VERSION 1.0.0 LANGUAGES C CXX)
include(FetchContent)
if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
  add_subdirectory("${JUCE_PATH}" JUCE)
else()
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.12 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()
add_subdirectory(SkellCore)
juce_add_plugin(SkellPlugin
  COMPANY_NAME "Skell Audio"
  BUNDLE_ID "audio.skell.quickstart"
  PLUGIN_MANUFACTURER_CODE SkAu
  PLUGIN_CODE Sk01
  FORMATS VST3
  PRODUCT_NAME "Skell Quickstart"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)
juce_generate_juce_header(SkellPlugin)
target_sources(SkellPlugin PRIVATE Source/PluginProcessor.cpp Source/PluginProcessor.h Source/PluginEditor.cpp Source/PluginEditor.h)
target_compile_features(SkellPlugin PRIVATE cxx_std_17)
target_compile_definitions(SkellPlugin PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)
target_compile_definitions(SkellPlugin PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(SkellPlugin PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra)

target_link_libraries(SkellPlugin PRIVATE SkellCore::DSP SkellCore::UI)
