#pragma once

// ===========================================================================
//  THE SHARED TEST HARNESS.
//
//  Split out of Main.cpp when the suite outgrew one file. Every test file
//  needs an APVTS-owning host and nothing else needs to be shared, so this is
//  all that lives here.
//
//  One file per subject under Tests/ - the CMake target globs them - because
//  a single translation unit of several thousand lines is a file nobody can
//  work in and two people cannot work in at once. Compare Source/Presets,
//  which was split for the same reason.
// ===========================================================================

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Source/Plugin/ParameterRegistry.h"

using namespace nacar;

// ===========================================================================
//  A minimal host for the parameter tree.
//
//  The real NacarProcessor pulls in the plugin client, which needs a host to
//  link against.  The tests only need something that owns an APVTS, so they
//  own the smallest AudioProcessor that can.
// ===========================================================================
class TestHost : public juce::AudioProcessor
{
public:
    TestHost()
        : juce::AudioProcessor (BusesProperties()
                                    .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMETERS", ParameterRegistry::createLayout())
    {
        registry.attach (apvts);
    }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "TestHost"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
    ParameterRegistry registry;
};
