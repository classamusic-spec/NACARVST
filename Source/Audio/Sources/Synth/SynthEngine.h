#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "../../../Plugin/ParameterRegistry.h"

namespace nacar
{
    /** Realtime quality tier.  STUDIO is the default; offline rendering always
        uses ULTRA regardless of the parameter. */
    enum class Quality { eco, studio, ultra };

    /** The three NACAR synthesis characters.  They share one core - oscillators,
        voice allocation, envelopes, filters, unison - and differ in behaviour,
        not in architecture. */
    enum class Character { mirage, haze, mass };

    /**
        THE NACAR SYNTH.

        Owns voice allocation and every per-voice signal path.  Reads its
        settings straight from the ParameterRegistry (lock-free atomics) at the
        start of each block, so there is no parameter plumbing to keep in sync.

        Realtime contract: after prepare(), no method below allocates, locks,
        touches the filesystem or logs.
    */
    class SynthEngine
    {
    public:
        SynthEngine();
        ~SynthEngine();

        /** Allocates everything the engine will ever need. Message thread. */
        void prepare (double sampleRate, int maximumBlockSize, int numChannels);

        /** Silences all voices and clears every filter and delay state. */
        void reset();

        /** Overrides the quality parameter, for offline rendering. */
        void setOfflineRendering (bool shouldRenderOffline) noexcept;

        /** Renders `numSamples` of synth output, adding into `buffer`.
            `midi` is consumed with sample-accurate timing. */
        void process (juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midi,
                      const ParameterRegistry& params,
                      double hostBpm);

        /** Immediately releases every voice (transport stop, panic). */
        void allNotesOff();

        /** How many voices are currently sounding - for the UI. */
        int getActiveVoiceCount() const noexcept;

        /** Peak level of the last block, for the synth visualiser. */
        float getLastPeak() const noexcept;

        /** True once prepare() has run. */
        bool isPrepared() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthEngine)
    };
}
