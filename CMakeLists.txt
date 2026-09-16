# ============================================================================
#  NACAR - MEMORY INSTRUMENT
#  Sounds with a past.
#
#  Root build script.  See Docs/BUILD.md for platform notes.
# ============================================================================
cmake_minimum_required(VERSION 3.22)

project(NACAR VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    if (NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
endif()

# ---------------------------------------------------------------------------
#  JUCE
# ---------------------------------------------------------------------------
set(NACAR_JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout")

if (NACAR_JUCE_PATH AND EXISTS "${NACAR_JUCE_PATH}/CMakeLists.txt")
    message(STATUS "NACAR: using JUCE at ${NACAR_JUCE_PATH}")
    add_subdirectory("${NACAR_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "NACAR: using vendored JUCE")
    add_subdirectory(JUCE EXCLUDE_FROM_ALL)
else()
    message(STATUS "NACAR: fetching JUCE 8.0.4")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
#  Formats
#     VST3      : all platforms
#     AU        : macOS only
#     Standalone: all platforms (development / audition host)
#     AAX / CLAP: not enabled for V1, but nothing in the codebase assumes
#                 a format - adding them is a one-line change here.
# ---------------------------------------------------------------------------
set(NACAR_FORMATS VST3 Standalone)
if (APPLE)
    list(APPEND NACAR_FORMATS AU)
endif()

juce_add_plugin(NACAR
    COMPANY_NAME                "Nacar Audio"
    BUNDLE_ID                   "com.nacaraudio.nacar"
    PLUGIN_MANUFACTURER_CODE    Ncar
    PLUGIN_CODE                 Ncr1
    FORMATS                     ${NACAR_FORMATS}
    PRODUCT_NAME                "NACAR"
    DESCRIPTION                 "Memory Instrument"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             "Instrument" "Synth" "Sampler"
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice)

juce_generate_juce_header(NACAR)

# ---------------------------------------------------------------------------
#  Sources
# ---------------------------------------------------------------------------
file(GLOB_RECURSE NACAR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")

target_sources(NACAR PRIVATE ${NACAR_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX "Source" FILES ${NACAR_SOURCES})

target_include_directories(NACAR PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(NACAR PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_ALSA=1
    JUCE_JACK=0)

target_link_libraries(NACAR PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if (MSVC)
    target_compile_options(NACAR PRIVATE /W4 /wd4100)
else()
    target_compile_options(NACAR PRIVATE
        -Wall -Wextra
        -Wno-unused-parameter
        -Wno-missing-field-initializers)
endif()

# Release builds: fast maths is deliberately NOT enabled - denormal and NaN
# behaviour matters in the filters and the reverb tails.
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# ---------------------------------------------------------------------------
#  Tests (headless DSP / analysis / mutation checks)
# ---------------------------------------------------------------------------
option(NACAR_BUILD_TESTS "Build the NACAR test runner" ON)
if (NACAR_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/CMakeLists.txt")
    add_subdirectory(Tests)
endif()
