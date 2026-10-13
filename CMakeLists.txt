cmake_minimum_required(VERSION 3.22)
project(WarpsPlugin VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  # Native Apple silicon by default; uncomment for a universal binary.
  # set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Plugin formats: AU + VST3 (+ Standalone for quick testing).
set(FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FORMATS AU)
endif()

juce_add_plugin(Warps
  COMPANY_NAME "particledots"
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE Warp
  FORMATS ${FORMATS}
  PRODUCT_NAME "Unofficial Warps"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicEffect   # lets Logic route MIDI to it as a "MIDI-controlled effect"
  COPY_PLUGIN_AFTER_BUILD TRUE)

# --- DSP core: vendored Warps/stmlib code + our JUCE-free wrapper, built with plain flags ---
add_library(warps_core STATIC
  src/WarpsEngine.cpp
  third_party/warps/dsp/filter_bank.cc
  third_party/warps/dsp/modulator.cc
  third_party/warps/dsp/oscillator.cc
  third_party/warps/dsp/vocoder.cc
  third_party/warps/resources.cc
  third_party/stmlib/utils/random.cc
  third_party/stmlib/dsp/units.cc)
target_include_directories(warps_core PUBLIC src third_party)
target_compile_definitions(warps_core PRIVATE TEST=1)
set_target_properties(warps_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Some upstream headers rely on <cstdio>/<cstdlib>/<cmath> being pulled in transitively.
if(MSVC)
  target_compile_options(warps_core PRIVATE /FIcstdio /FIcstdlib /FIcmath)
else()
  target_compile_options(warps_core PRIVATE "SHELL:-include cstdio" "SHELL:-include cstdlib" "SHELL:-include cmath" -Wno-unused-local-typedef)
endif()

# --- The plugin ---
target_sources(Warps PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
target_compile_definitions(Warps PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Warps PRIVATE
  warps_core
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

# --- Headless test of the DSP wrapper:  cmake --build build --target engine_test && build/engine_test ---
add_executable(engine_test EXCLUDE_FROM_ALL src/engine_test.cpp)
target_link_libraries(engine_test PRIVATE warps_core)
