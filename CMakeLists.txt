cmake_minimum_required(VERSION 3.22)
project(EvolutionVox VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(EVOLUTIONVOX_BUILD_PLUGIN "Build the VST3 plugin using JUCE" ON)
option(EVOLUTIONVOX_BUILD_TESTS "Build standalone DSP unit tests" ON)
add_library(EvolutionVoxDSP INTERFACE)
target_include_directories(EvolutionVoxDSP INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
if(EVOLUTIONVOX_BUILD_TESTS)
  enable_testing()
  add_executable(evolutionvox_dsp_tests Tests/test_dsp.cpp)
  target_link_libraries(evolutionvox_dsp_tests PRIVATE EvolutionVoxDSP)
  add_test(NAME evolutionvox_dsp_tests COMMAND evolutionvox_dsp_tests)
  add_executable(evolutionvox_demo Tests/render_demo.cpp)
  target_link_libraries(evolutionvox_demo PRIVATE EvolutionVoxDSP)
endif()
if(EVOLUTIONVOX_BUILD_PLUGIN)
  # With JUCE installed locally: cmake -S . -B build -DFETCH_JUCE=OFF -DJUCE_DIR=/path/to/JUCE
  # Otherwise CMake downloads the pinned JUCE source during configuration.
  option(FETCH_JUCE "Fetch JUCE when not already installed" ON)
  if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" juce)
  elseif(FETCH_JUCE)
    include(FetchContent)
    FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.17-security GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
  else()
    message(FATAL_ERROR "JUCE not found. Supply -DJUCE_DIR=/path/to/JUCE or -DFETCH_JUCE=ON")
  endif()
  juce_add_plugin(EvolutionVox
      COMPANY_NAME "EvolutionVox"
      BUNDLE_ID "audio.evolutionvox.plugin"
      PLUGIN_MANUFACTURER_CODE Evol
      PLUGIN_CODE Evo1
      FORMATS VST3
      VST3_CAN_REPLACE_VST2 FALSE
      PRODUCT_NAME "EvolutionVox"
      IS_SYNTH FALSE
      NEEDS_MIDI_INPUT FALSE
      NEEDS_MIDI_OUTPUT FALSE
      IS_MIDI_EFFECT FALSE
      COPY_PLUGIN_AFTER_BUILD FALSE)
  target_sources(EvolutionVox PRIVATE
      Source/Plugin/PluginProcessor.cpp
      Source/Plugin/PluginEditor.cpp)
  target_link_libraries(EvolutionVox PRIVATE
      EvolutionVoxDSP
      juce::juce_audio_utils
      juce::juce_audio_processors
      juce::juce_dsp
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags
      juce::juce_recommended_warning_flags)
  target_compile_definitions(EvolutionVox PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
  juce_generate_juce_header(EvolutionVox)
endif()
