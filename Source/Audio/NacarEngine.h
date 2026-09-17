#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>

#include "../Plugin/ParameterRegistry.h"
#include "EngineContext.h"
#include "Sources/Synth/SynthEngine.h"

namespace nacar
{
    /** Declared in `Sources/Sample/SampleBuffer.h`.  Forward-declared here so
        that every translation unit which routes audio does not have to pull in
        the audio-format module as well. */
    class SampleSlot;

    /** The six reorderable chain slots.  Display order is DSP order. */
    enum class FxSlot { retro = 0, crush, filter, rewind, grain, space, count };

    inline constexpr int numFxSlots = (int) FxSlot::count;

    /** Canonical slot name, as stored in the FXCHAIN state tree. */
    const char* fxSlotName (FxSlot) noexcept;

    /** Parses a slot name back.  Returns FxSlot::count when unknown. */
    FxSlot fxSlotFromName (juce::StringRef) noexcept;

    /**
        The chain order and its bypasses, handed across the thread boundary as
        a packed integer.

        The audio thread must never read a ValueTree, so the editor resolves
        both once on the message thread, packs them into 32 bits and publishes
        them with a single atomic store.  The audio thread unpacks at the top of
        each block.  27 bits are used: six three-bit slot indices, a three-bit
        count and a six-bit bypass mask.
    */
    struct FxOrder
    {
        std::array<FxSlot, (size_t) numFxSlots> slots {};
        int count = 0;
        juce::uint32 bypassMask = 0;        ///< bit N set == slot N bypassed

        static FxOrder defaultOrder() noexcept;

        /** Parses the FXCHAIN tree's two comma-separated properties. */
        static FxOrder fromState (juce::StringRef order, juce::StringRef bypassed) noexcept;

        bool isBypassed (FxSlot s) const noexcept
        {
            return (bypassMask & (1u << (juce::uint32) s)) != 0;
        }

        juce::uint32 pack() const noexcept;
        static FxOrder unpack (juce::uint32) noexcept;
    };

    /** What the host says about the transport, as of this block. */
    struct TransportInfo
    {
        double bpm = 120.0;
        double ppqPosition = 0.0;
        bool   playing = false;
    };

    /**
        THE NACAR SIGNAL CHAIN.

        Owns every engine and routes between them.  The processor holds one and
        does nothing but hand it a buffer.

            SOURCE        the synth, and in later phases the four other engines
              -> MEMORY       generational history
              -> FX CHAIN     retro, crush, filter, rewind, grain, space,
                              in whatever order the user has put them
              -> SHADOW       an atmospheric duplicate of the finished sound
              -> AURA         the environment it all sits in
              -> PATINA       the surface it has ended up with
              -> WEIGHT       physical mass, last
              -> OUTPUT       Pulse's volume and width destinations

        Why that order.  Memory is first because it is about what the *source*
        has been through - putting it after the effects would age the effects
        rather than the sound.  The FX chain is where the user's decisions live.
        Shadow duplicates the finished sound rather than the raw one, or it
        would be a duplicate of something nobody heard.  Aura is the environment
        and so contains everything.  Patina is the surface of the final object.
        Weight is last because it is the only stage whose job is the finished
        thing's physical size.

        Realtime contract: after prepare(), nothing below allocates, locks,
        touches the filesystem or logs.
    */
    class NacarEngine
    {
    public:
        NacarEngine();
        ~NacarEngine();

        void prepare (double sampleRate, int maximumBlockSize, int numChannels);
        void reset();

        /** Forces maximum quality regardless of the quality parameter. */
        void setOfflineRendering (bool) noexcept;

        /** Publishes a new chain order and bypass mask.  Message thread. */
        void setFxOrder (const FxOrder&) noexcept;

        /** Renders one block.  Audio thread only. */
        void process (juce::AudioBuffer<float>&, juce::MidiBuffer&,
                      const ParameterRegistry&, const TransportInfo&);

        void allNotesOff();

        int getActiveVoiceCount() const noexcept;
        float getLastPeak() const noexcept;

        /** Samples of latency the chain currently adds.  Changes as modules are
            switched on and off; the processor reports it to the host. */
        int getLatencySamples() const noexcept;

        /** Direct access for the parts of the UI that visualise the synth. */
        SynthEngine& getSynth() noexcept;

        /** Points the sample SOURCE at the slot the loader publishes into.
            Message thread, before audio starts; the slot is not owned and must
            outlive the engine.  Null means the sample source is silent, which
            is the state the instrument is in until a file is dropped. */
        void setSampleSlot (const SampleSlot*) noexcept;

        /** How many sample voices are sounding.  Separate from the synth's
            count because they are separate sources, and reporting one number
            for both would make an empty synth look busy. */
        int getSampleVoiceCount() const noexcept;

        /** Rebuilds the modulation matrix from the session tree and publishes
            it to the audio thread.  Message thread only. */
        void rebuildModMatrix (const juce::ValueTree&);

        /** Rebuilds the four sequencer lanes from the session tree's SEQUENCER
            branch and publishes them to the audio thread.  Message thread only;
            an invalid tree publishes four empty lanes, which do nothing. */
        void rebuildSequencer (const juce::ValueTree&);

        /** Which step a sequencer lane is on as of the last block, for the SEQ
            page's playhead.  Safe from the message thread: one relaxed load. */
        int getSequencerStep (int lane) const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NacarEngine)
    };
}
