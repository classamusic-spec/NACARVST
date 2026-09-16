#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ParameterRegistry.h"
#include "StateManager.h"
#include "../Audio/Sources/Synth/SynthEngine.h"

namespace nacar
{
    /**
        NACAR - MEMORY INSTRUMENT.

        The processor owns the parameter registry, the persisted session state
        and every audio engine.  It contains no DSP of its own beyond routing
        and the output stage.

        Threading: process() is the only method that runs on the audio thread,
        and everything it calls obeys the realtime contract - no allocation, no
        locks, no file IO, no logging.
    */
    class NacarProcessor : public juce::AudioProcessor
    {
    public:
        NacarProcessor();
        ~NacarProcessor() override;

        // -- AudioProcessor -------------------------------------------------
        void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return "NACAR"; }

        bool acceptsMidi() const override           { return true; }
        bool producesMidi() const override          { return false; }
        bool isMidiEffect() const override          { return false; }
        double getTailLengthSeconds() const override;

        int getNumPrograms() override               { return 1; }
        int getCurrentProgram() override            { return 0; }
        void setCurrentProgram (int) override       {}
        const juce::String getProgramName (int) override { return "Default"; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        // -- NACAR ----------------------------------------------------------
        juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
        const ParameterRegistry& getParameters() const noexcept { return registry; }
        StateManager& getStateManager() noexcept { return stateManager; }
        juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

        /** Output meter, one value per channel, 0..1, decayed for display. */
        float getMeterLevel (int channel) const noexcept;

        /** Host tempo as of the last block; 120 when the host provides none. */
        double getHostBpm() const noexcept { return hostBpm.load (std::memory_order_relaxed); }

    private:
        void updateMeters (const juce::AudioBuffer<float>&);
        void pullTransportInfo();

        juce::AudioProcessorValueTreeState apvts;
        ParameterRegistry registry;
        StateManager stateManager;

        SynthEngine synth;

        juce::MidiKeyboardState keyboardState;
        juce::dsp::Gain<float> outputGain;

        std::atomic<float> meter[2] { { 0.0f }, { 0.0f } };
        std::atomic<double> hostBpm { 120.0 };

        double currentSampleRate = 44100.0;
        int currentBlockSize = 512;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NacarProcessor)
    };
}
