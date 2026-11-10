cmake_minimum_required(VERSION 3.22)
project(SkellSpacer VERSION 1.0.0 LANGUAGES C CXX)
include(FetchContent)
if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
  add_subdirectory("${JUCE_PATH}" JUCE)
else()
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.12 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()
juce_add_plugin(SkellSpacer
  COMPANY_NAME "Skell Audio"
  BUNDLE_ID "audio.skell.spacer"
  PLUGIN_MANUFACTURER_CODE SkAu
  PLUGIN_CODE Sk02
  FORMATS VST3
  PRODUCT_NAME "Skell Spacer"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)
juce_generate_juce_header(SkellSpacer)
target_sources(SkellSpacer PRIVATE Source/PluginProcessor.cpp Source/PluginProcessor.h Source/PluginEditor.cpp Source/PluginEditor.h Source/SpacerDSP.h Source/SkellTheme.h)
target_compile_features(SkellSpacer PRIVATE cxx_std_17)
target_compile_definitions(SkellSpacer PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)
target_compile_definitions(SkellSpacer PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(SkellSpacer PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra)
