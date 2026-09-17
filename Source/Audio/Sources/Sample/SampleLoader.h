#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

#include "SampleBuffer.h"

namespace nacar
{
    /**
        ASYNCHRONOUS SAMPLE DECODING.

        A file is chosen on the message thread.  It is decoded on a background
        thread.  The finished, immutable SampleBuffer is published to a
        SampleSlot on the message thread, which is the only thread allowed to
        publish and the only thread allowed to free.

        Nothing here ever blocks the message thread and nothing here is ever
        touched by the audio thread.  While a decode is running the instrument
        keeps playing whatever the slot already holds - the slot is only
        replaced at the instant the new buffer is complete.

        ------------------------------------------------------------------
        WHAT "ASYNCHRONOUS" HAS TO SURVIVE

        A second file dropped while the first is still decoding.  Each request
        gets a number; the decoder checks that number between chunks and drops
        a result that a later request has superseded, so the last file dropped
        is the one that plays regardless of which decode finishes first.

        A file that is unsupported, corrupt, empty, or gone.  All four end the
        same way: no buffer is published, the slot is cleared, `onFinished`
        reports the reason, and the rest of the instrument carries on.  There
        is no partial or invented sample.

        ------------------------------------------------------------------
        DRIVING IT

        `poll()` is what moves a finished decode into the slot and frees
        anything the slot has retired.  A Timer calls it ten times a second
        when a message loop is running; a headless test calls it directly.
        Everything the loader does on the message thread happens in there, so
        there is exactly one place where the handoff is observable.
    */
    class SampleLoader : private juce::Timer
    {
    public:
        /** The slot is not owned and must outlive the loader. */
        explicit SampleLoader (SampleSlot&);
        ~SampleLoader() override;

        /** What a finished decode concluded.  Reported on the message thread,
            whether it worked or not. */
        struct Result
        {
            juce::File   file;
            juce::String displayName;

            bool ok = false;
            juce::String error;        ///< empty when ok; user-facing, one line

            double sourceRate    = 0.0;
            int    lengthSamples = 0;
            int    numChannels   = 0;

            /** True when the file was longer than the loader's ceiling and only
                the first `lengthSamples` were kept. */
            bool truncated = false;

            /** True when the file had more than two channels and only the
                first two were kept. */
            bool channelsDiscarded = false;
        };

        /** Message thread.  Starts a decode, replacing any decode already in
            flight.  Returns immediately.

            An empty or non-existent file is not an error to start: it finishes
            promptly with `ok == false` and clears the slot, which is what
            "the user removed the sample" has to do anyway. */
        void loadAsync (const juce::File&);

        /** Message thread.  Cancels anything in flight and empties the slot. */
        void clear();

        /** Message thread.  Picks up a finished decode, publishes it, and frees
            anything the audio thread has finished with.  Called by the timer;
            safe and cheap to call at any rate. */
        void poll();

        /** True between loadAsync() and the matching onFinished(). */
        bool isBusy() const noexcept;

        /** Set before loadAsync().  Called on the message thread from poll(). */
        std::function<void (const Result&)> onFinished;

        /** The maximum length the loader will decode, in seconds.  Anything
            past this is discarded and reported as truncated rather than
            silently dropped or allowed to exhaust memory. */
        static constexpr double maxLengthSeconds = 600.0;

        /** Target width of the waveform overview.  The real bucket count is
            whatever this works out to for the file's length; a file shorter
            than this gets one bucket per sample and no more. */
        static constexpr int targetPeakBuckets = 4096;

    private:
        void timerCallback() override;

        struct Shared;
        class DecodeJob;

        std::shared_ptr<Shared> shared;
        std::unique_ptr<juce::ThreadPool> pool;
        SampleSlot& slot;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleLoader)
    };
}
