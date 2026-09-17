#pragma once

#include <juce_events/juce_events.h>

#include <array>
#include <atomic>
#include <functional>
#include <vector>

#include "AnalysisResult.h"
#include "../Audio/Sources/Sample/SampleBuffer.h"

namespace nacar
{
    /**
        WHAT THE INSTRUMENT MEASURES WHEN IT IS GIVEN A FILE.

        One decoded SampleBuffer in, one AnalysisResult out.  Phases 20 and 21
        make musical decisions from what comes out of here, so every estimate
        carries a confidence and every confidence is meant: see the note at the
        top of AnalysisResult.h, and the "Confidence" section of the README next
        to this file.

        THREADING.  `analyse()` is a pure function of its input and may be
        called from any thread that is allowed to allocate - which means any
        thread except the audio one.  The object wrapper around it owns a
        background thread: `startAnalysis()` cancels whatever is running,
        returns immediately, and delivers the result on the message thread
        through `onAnalysisFinished`.  Dropping a second file while the first is
        still being analysed is the case this is built for.

        The only lock in here is taken around a copy of the finished result.
        The message thread never waits on the worker's progress through a file.
    */
    class SampleAnalyser : private juce::Thread,
                           private juce::AsyncUpdater
    {
    public:
        SampleAnalyser();
        ~SampleAnalyser() override;

        /** Polled throughout the analysis.  Returning true stops it at the next
            check - within a few milliseconds of audio in every loop - and
            `analyse()` then returns a default AnalysisResult with `analysed`
            still false.  An aborted analysis is never a partial one. */
        using AbortCheck = std::function<bool()>;

        /** Called with 0..1 as the analysis proceeds.  Called from whichever
            thread is running the analysis, so a UI must marshal. */
        using ProgressSink = std::function<void (float)>;

        /**
            Everything the analyser works out that AnalysisResult has no room
            for.  It exists because a test that can only see the result cannot
            tell a correct chroma from a chroma that happens to produce a
            plausible key, and the chroma is the part of this phase that phase
            21 leans on hardest.
        */
        struct Detail
        {
            std::array<float, 12> chroma {};   ///< C first, normalised so max == 1
            float chromaSalience = 0.0f;       ///< 0 = flat, no key information at all
            int   chromaFrames   = 0;          ///< frames that passed the energy gate

            std::vector<float> onsetEnvelope;  ///< the detection function, one value per frame
            double envelopeRate = 0.0;         ///< frames per second of the above

            float tempoAutocorrPeak       = 0.0f;  ///< 0..1, the winning lag's normalised ACF
            float tempoPhaseConcentration = 0.0f;  ///< 0..1, how tightly onsets sit on the grid
            double tempoBeforePrior       = 0.0;   ///< BPM of the raw ACF peak, before the refit

            bool  wasSilent  = false;  ///< nothing above the noise floor anywhere
            bool  wasAborted = false;
        };

        /** The measurement itself.  Any thread that may allocate. */
        static AnalysisResult analyse (const SampleBuffer&,
                                       Detail* = nullptr,
                                       const AbortCheck& = {},
                                       const ProgressSink& = {});

        // -- the background wrapper ------------------------------------------

        /** Message thread.  Cancels anything in flight - waiting only for the
            worker to notice, not for it to finish the file - and starts again
            on the buffer given.  A null or empty buffer just cancels. */
        void startAnalysis (SampleBuffer::Ptr);

        /** Message thread.  Stops the current analysis.  `onAnalysisFinished`
            is not called for a cancelled run. */
        void cancelAnalysis();

        bool isAnalysing() const noexcept;

        /** 0..1.  Exposed for an interface that wants to show it; nothing in
            the build reads it yet. */
        float getProgress() const noexcept { return progress.load (std::memory_order_relaxed); }

        /** The last completed result.  Message thread. */
        AnalysisResult getResult() const;

        /** Called on the message thread when an analysis completes.  Not called
            when one is cancelled. */
        std::function<void (const AnalysisResult&)> onAnalysisFinished;

    private:
        void run() override;
        void handleAsyncUpdate() override;

        SampleBuffer::Ptr pending;              // message thread writes, worker reads once
        AnalysisResult    finishedResult;
        mutable juce::CriticalSection resultLock;

        std::atomic<float> progress { 0.0f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleAnalyser)
    };
}
