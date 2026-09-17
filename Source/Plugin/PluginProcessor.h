#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

#include "ParameterRegistry.h"
#include "StateManager.h"
#include "../Audio/NacarEngine.h"
#include "../Audio/Sources/Sample/SourcePipeline.h"
#include "../Analysis/AnalysisResult.h"
#include "../Mutation/MutationRecipe.h"

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
    class NacarProcessor : public juce::AudioProcessor,
                           private juce::ValueTree::Listener,
                           private juce::AsyncUpdater,
                           private juce::Timer
    {
    public:
        NacarProcessor();
        ~NacarProcessor() override;

        // -- AudioProcessor -------------------------------------------------
        void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

        // NACAR renders in single precision.  The double-precision overload is
        // pulled back into scope rather than hidden, so the base class's
        // default still handles a host that asks for it.
        using juce::AudioProcessor::processBlock;

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

        /** How many synth voices are sounding, for the viewport. */
        int getActiveVoiceCount() const noexcept;

        /** The decoded sample the audio thread is playing, and the decoder that
            publishes into it.  Message thread only, and read-only to everything
            but the loader: see `Source/Audio/Sources/Sample/SampleBuffer.h` for
            who is allowed to publish and who is allowed to free. */
        SampleSlot& getSampleSlot() noexcept       { return pipeline.slot(); }
        SampleLoader& getSampleLoader() noexcept   { return pipeline.loader(); }

        /** The path a sound takes through the instrument: decode, analyse,
            mutate, play. The processor owns it, drives its clock and writes
            what it produces into the session tree; the logic itself lives in
            SourcePipeline so that it can be tested, which it cannot be here. */
        SourcePipeline& getPipeline() noexcept { return pipeline; }

        // -------------------------------------------------------------------
        //  ANALYSIS
        //
        //  Runs automatically when a decode succeeds, on a background thread,
        //  and writes itself into the session's ANALYSIS branch when it lands.
        //  Everything downstream - the harmony context, every mutation - reads
        //  what this produced, so it must never be a guess presented as a fact:
        //  see the note on confidence in AnalysisResult.h.
        // -------------------------------------------------------------------

        /** What the instrument currently knows about its sample. Message
            thread. `analysed == false` until one has actually finished. */
        const AnalysisResult& getAnalysis() const noexcept { return pipeline.analysis(); }

        /** Called on the message thread when an analysis lands. */
        std::function<void()> onAnalysisFinished;

        // -------------------------------------------------------------------
        //  MUTATION
        //
        //  MUTATE renders on a background thread and publishes the result into
        //  the same slot the sample engine plays from, so a mutation becomes
        //  the thing you are playing.
        //
        //  IT NEEDS A SOURCE. With no sample loaded there is nothing to mutate
        //  and the request fails with a message saying so, rather than
        //  inventing one. Rendering the synth's own output into a buffer first
        //  is PRINT, which is phase 22 and does not exist.
        // -------------------------------------------------------------------

        /** Message thread. Starts a mutation from the recipe against whatever
            sample is loaded. Returns false, with `failure` set, when it cannot
            start at all - no sample, or one already running. */
        bool requestMutation (const mutation::Recipe&, juce::String& failure);

        bool isMutating() const noexcept { return pipeline.isMutating(); }

        // -------------------------------------------------------------------
        //  PRINT  (phase 22)
        //
        //  Renders the instrument's own output into the sample slot, which is
        //  how a patch becomes audio. Before it existed the only source the
        //  instrument could reach was a file somebody dropped on it, so MUTATE
        //  on a sound you had just designed refused - correctly, but it made
        //  the second half of the instrument unreachable from the first.
        //
        //  Each print is recorded in the session's GENERATIONS branch with the
        //  file it wrote and the generation it came from, so a lineage survives
        //  a save and reload.
        // -------------------------------------------------------------------

        /** Message thread. Starts a print of the current patch. Returns false,
            with `failure` set, when one is already running. */
        bool requestPrint (juce::String& failure);

        bool isPrinting() const noexcept { return pipeline.isPrinting(); }

        /** Called on the message thread when a print finishes. */
        std::function<void (const PrintEngine::Result&)> onPrintFinished;

        // -------------------------------------------------------------------
        //  MAKE INSTRUMENT  (phase 23)
        // -------------------------------------------------------------------

        /** Message thread. Commits whatever is in the slot - a print or a
            mutation - as the instrument's playable source: writes it to disk,
            points the SAMPLE branch at it, switches source_mode to SAMPLE and
            advances the generation lineage. Returns false with `failure` set
            when there is nothing to commit. */
        bool makeInstrument (juce::String& failure);

        /** Called on the message thread when an instrument is made. */
        std::function<void (const juce::String& name)> onInstrumentMade;

        /** Called on the message thread when a mutation finishes, successfully
            or not. The Result carries its own failure text. */
        std::function<void (const mutation::Result&)> onMutationFinished;

        // -------------------------------------------------------------------
        //  Scope tap
        //
        //  A lock-free window onto the instrument's recent output, so the
        //  optical viewport can draw the actual waveform rather than a level
        //  trace.  The audio thread writes peak-decimated frames into a ring
        //  and publishes one index; the UI copies the newest frames out.  A
        //  torn read costs one frame of a 30 Hz animation, which is why this
        //  needs no lock.
        // -------------------------------------------------------------------
        static constexpr int scopeSize = 2048;

        /** Copies up to `maxFrames` of the newest scope data into `dest`,
            oldest first, and returns how many were written.  Each frame is a
            peak-decimated pair: dest[2i] is left, dest[2i + 1] is right. */
        int readScope (float* dest, int maxFrames) const noexcept;

        /** Samples per scope frame at the current sample rate. */
        int getScopeDecimation() const noexcept { return scopeDecimation; }

        /** Host tempo as of the last block; 120 when the host provides none. */
        double getHostBpm() const noexcept { return hostBpm.load (std::memory_order_relaxed); }

    private:
        void updateMeters (const juce::AudioBuffer<float>&);
        void pushScope (const juce::AudioBuffer<float>&) noexcept;
        void pullTransportInfo();

        // The audio thread must never read a ValueTree, so the FX chain's order
        // and bypasses are resolved here, on the message thread, whenever the
        // session tree changes, and published to the engine as one packed
        // integer.
        void publishFxOrder();
        void publishModMatrix();

        /** Starts (or cancels) an asynchronous decode of whatever the SAMPLE
            branch names.  Message thread; returns immediately. */
        void startSampleLoad (const juce::File&);

        /** Appends a GENERATION child naming this file and the generation it
            came from, and returns its index. Message thread. */
        int recordGeneration (const juce::File&, const juce::String& displayName);

        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
        void valueTreeParentChanged (juce::ValueTree&) override;
        void valueTreeRedirected (juce::ValueTree&) override;

        /** Reports the chain's latency to the host.

            Retro and Crush each delay the signal while they are active, and
            neither does while bypassed, so the figure moves as the user
            switches modules on and off.  setLatencySamples() notifies the host
            and its listeners, which is not something to do from the audio
            thread - so the audio thread only notices the change and this runs
            on the message thread. */
        void handleAsyncUpdate() override;

        /** Drives the pipeline's clock: advances a decode and applies whatever
            background work has finished.

            It is HERE and not in the editor, which is the whole point. A plugin
            with no window open must still finish decoding the file it was given
            and still finish the mutation it was asked for; an instrument whose
            work stops when you close its interface is broken in a way that only
            shows up in somebody's session. */
        void timerCallback() override;

        juce::AudioProcessorValueTreeState apvts;
        ParameterRegistry registry;
        StateManager stateManager;

        // Declared before the engine so that they outlive it: the engine holds
        // a pointer to the slot, and destruction runs in reverse.
        SourcePipeline pipeline;


        NacarEngine engine;

        juce::MidiKeyboardState keyboardState;
        juce::dsp::Gain<float> outputGain;

        std::atomic<float> meter[2] { { 0.0f }, { 0.0f } };
        std::atomic<double> hostBpm { 120.0 };
        TransportInfo transport;
        std::atomic<int> reportedLatency { -1 };

        // Scope ring.  Interleaved stereo peaks, written by the audio thread.
        std::array<float, (size_t) scopeSize * 2> scope {};
        std::atomic<int> scopeWrite { 0 };
        int scopeDecimation = 16;
        int scopeCounter = 0;
        float scopePeakL = 0.0f, scopePeakR = 0.0f;

        double currentSampleRate = 44100.0;
        int currentBlockSize = 512;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NacarProcessor)
    };
}
