#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <vector>

namespace nacar
{
    /**
        A DECODED SAMPLE, and the rules for handing one to the audio thread.

        Phases 18 to 21 all need to look at the same decoded audio: the sample
        engine plays it, the analyser measures it, the mutation engine reads it
        as a source and writes a new one. It is decoded once, it is never
        modified after publication, and it is shared.

        IMMUTABLE AFTER PUBLICATION. Nothing may write to `audio`, `peaks` or
        any field below once publish() has been called with it. That is the
        whole reason the audio thread can read it without a lock: there is no
        writer to race with. A change means decoding a new one and publishing
        that instead.
    */
    class SampleBuffer : public juce::ReferenceCountedObject
    {
    public:
        using Ptr = juce::ReferenceCountedObjectPtr<SampleBuffer>;

        juce::AudioBuffer<float> audio;          ///< deinterleaved, at sourceRate
        double      sourceRate    = 44100.0;     ///< the file's own rate, NOT the host's
        juce::File  file;
        juce::String displayName;                ///< what the viewport shows

        /**
            The waveform overview, precomputed at decode time.

            The viewport cannot walk a five-minute buffer at 30 Hz, and it must
            never touch `audio` while the audio thread might be reading it. So
            the decoder reduces the file to min/max pairs per bucket, once, and
            the viewport draws only from these.

            `bucketSamples` is how many source samples each pair covers.
            Channel c's bucket b is at index (b * numChannels + c).
        */
        struct Peaks
        {
            std::vector<float> minimum, maximum;
            int bucketSamples = 0;
            int numBuckets    = 0;
            int numChannels   = 0;
        };

        Peaks peaks;

        int  numChannels()   const noexcept { return audio.getNumChannels(); }
        int  lengthSamples() const noexcept { return audio.getNumSamples(); }
        bool isEmpty()       const noexcept { return audio.getNumSamples() <= 0; }

        double lengthSeconds() const noexcept
        {
            return sourceRate > 0.0 ? (double) lengthSamples() / sourceRate : 0.0;
        }
    };

    /**
        THE HANDOFF, and the one thing that is easy to get fatally wrong.

        The audio thread reads a SampleBuffer while the message thread may be
        replacing it. Reference counting makes the read safe, but it introduces
        a worse hazard than the one it solves: if the audio thread happens to
        drop the LAST reference, it runs the destructor - which frees a
        multi-megabyte buffer, on the audio thread, inside the callback. That is
        a realtime violation that will not show up in any test and will show up
        as a click in somebody's session.

        So the rule is: THE AUDIO THREAD NEVER DESTROYS A SAMPLE.

        `publish()` moves the outgoing buffer into `retired` rather than letting
        it go. `collectGarbage()` - message thread, called from a timer - is
        what actually frees it, and only once the audio thread has demonstrably
        moved on (the reference count has fallen to one, the one this holds).

        Realtime contract for `acquire()`: one relaxed load and one atomic
        increment. No allocation, no lock, no branch that can block.
    */
    class SampleSlot
    {
    public:
        SampleSlot();
        ~SampleSlot();

        /** Message thread. Replaces what the audio thread will read next block.
            The outgoing buffer is retired, not freed. */
        void publish (SampleBuffer::Ptr);

        /** Audio thread, once per block. May return nullptr, which means "no
            sample loaded" and is a normal state, not an error. */
        SampleBuffer::Ptr acquire() const noexcept;

        /** Message thread. Frees anything the audio thread has finished with.
            Safe to call every timer tick; it does nothing when there is
            nothing to free. */
        void collectGarbage();

        /** Message thread. True while a buffer is waiting to be freed - for a
            test that wants to prove the audio thread did not free it. */
        bool hasRetired() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleSlot)
    };
}
