# ---------------------------------------------------------------------------
#  NACAR offline tools
#
#  Console targets that drive the engines without a host or an audio device.
#  The benchmark renderer is the objective half of the synth quality gate: it
#  cannot tell us whether NACAR sounds good, but it can measure the things the
#  build specification makes hard requirements - aliasing, mono compatibility,
#  low-end stability, gain staging, DC.
# ---------------------------------------------------------------------------

juce_add_console_app(NacarBench PRODUCT_NAME "NacarBench")
juce_generate_juce_header(NacarBench)

set(NACAR_BENCH_ENGINE_SOURCES ${NACAR_SOURCES})
list(FILTER NACAR_BENCH_ENGINE_SOURCES EXCLUDE REGEX ".*/Plugin/PluginEditor\\.cpp$")
list(FILTER NACAR_BENCH_ENGINE_SOURCES EXCLUDE REGEX ".*/Plugin/PluginProcessor\\.cpp$")
list(FILTER NACAR_BENCH_ENGINE_SOURCES EXCLUDE REGEX ".*/UI/.*")

target_sources(NacarBench PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/SynthBenchmarkRenderer.cpp"
    ${NACAR_BENCH_ENGINE_SOURCES})

target_include_directories(NacarBench PRIVATE "${CMAKE_SOURCE_DIR}/Source")

target_compile_definitions(NacarBench PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(NacarBench PRIVATE
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
    target_compile_options(NacarBench PRIVATE -Wno-unused-parameter)
endif()
