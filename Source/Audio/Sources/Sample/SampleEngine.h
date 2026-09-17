#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>

#include "../../EngineContext.h"
#include "SampleBuffer.h"

namespace nacar
{
    /**
        THE NACAR SAMPLE SOURCE.

        A polyphonic player for one decoded sample, sitting alongside the synth
        as the chain's second SOURCE.  It reads the eleven `sample_*` parameters
        and nothing else of its own: start and end, loop with a real crossfade,
        reverse, gain, tune, key tracking and root note.

        It follows the synth's shape rather than the chain's, because it is a
        source: `process` takes the MIDI buffer, and it ADDS into the buffer
        rather than working on it in place.  `prepare` takes an EngineSpec so
        that it is told about the sample rate the same way every other engine
        in the instrument is.

        ------------------------------------------------------------------
        WHERE THE AUDIO COMES FROM

        Never from here.  The engine holds a pointer to a SampleSlot and asks it
        once per block for whatever is currently published.  A null answer is
        the normal "nothing loaded" state and produces silence - not a sine, not
        a click, not a placeholder.  See `SampleBuffer.h` for why the engine
        must not be the last holder of a reference, and `SampleBuffer.cpp` for
        how the slot makes sure it never is.

        ------------------------------------------------------------------
        MACROS

        The sample source has no macro response.  Nothing in `MacroState` is
        read: `age`, `grit` and the rest describe what has happened TO a sound,
        and every engine that applies them is downstream of this one.  The
        MacroState argument is taken for the block length and the convention,
        and that is stated here rather than left to be discovered.

        Realtime contract: after prepare(), nothing below allocates, locks,
        touches the filesystem or logs.
    */
    class SampleEngine
    {
    public:
        SampleEngine();
        ~SampleEngine();

        /** Allocates every voice the engine will ever use.  Message thread. */
        void prepare (const EngineSpec&);

        /** Silences every voice and forgets the sample it last saw. */
        void reset();

        /** Message thread, before audio starts.  Not owned; must outlive this. */
        void setSlot (const SampleSlot*) noexcept;

        /** Renders into `buffer`, adding to whatever is already there.  MIDI is
            consumed with sample-accurate timing.  Audio thread only. */
        void process (juce::AudioBuffer<float>&, juce::MidiBuffer&,
                      const ParameterRegistry&, const MacroState&);

        /** Releases every voice immediately (transport stop, panic). */
        void allNotesOff();

        /** How many voices are sounding, including those still decaying. */
        int getActiveVoiceCount() const noexcept;

        /** True when the slot had a sample in it as of the last block. */
        bool hasSample() const noexcept;

        /** The highest polyphony the engine will ever allocate for.  Matches the
            top of the `polyphony` parameter's range, which is shared with the
            synth - the sample source has no voice count of its own and none is
            reserved. */
        static constexpr int maxVoices = 32;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleEngine)
    };
}
