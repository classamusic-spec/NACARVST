# ---------------------------------------------------------------------------
#  NACAR test runner
#
#  Headless checks that need no audio device and no plugin host: parameter
#  table integrity, state round-tripping, and DSP behaviour at every sample
#  rate and block size the product claims to support.
# ---------------------------------------------------------------------------

juce_add_console_app(NacarTests PRODUCT_NAME "NacarTests")

juce_generate_juce_header(NacarTests)

file(GLOB NACAR_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# The tests exercise the real engines, so they compile the same sources the
# plugin does - minus the plugin entry points, which need a host.
set(NACAR_TESTED_SOURCES ${NACAR_SOURCES})
list(FILTER NACAR_TESTED_SOURCES EXCLUDE REGEX ".*/Plugin/PluginEditor\\.cpp$")
list(FILTER NACAR_TESTED_SOURCES EXCLUDE REGEX ".*/Plugin/PluginProcessor\\.cpp$")
list(FILTER NACAR_TESTED_SOURCES EXCLUDE REGEX ".*/UI/.*")

target_sources(NacarTests PRIVATE ${NACAR_TEST_SOURCES} ${NACAR_TESTED_SOURCES})

target_include_directories(NacarTests PRIVATE "${CMAKE_SOURCE_DIR}/Source")

target_compile_definitions(NacarTests PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_UNIT_TESTS=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(NacarTests PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

if (NOT MSVC)
    target_compile_options(NacarTests PRIVATE -Wno-unused-parameter)
endif()
