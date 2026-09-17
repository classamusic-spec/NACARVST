#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>

#include "SampleLoader.h"
#include "../../../Analysis/AnalysisResult.h"
#include "../../../Mutation/MutationRecipe.h"
#include "../../../Harmony/Harmony.h"
#include "../../Print/PrintEngine.h"

namespace nacar
{
    /**
        THE PATH A SOUND TAKES THROUGH THE INSTRUMENT.

        A file arrives, it decodes, it is analysed, a mutation is rendered from
        it, and that mutation becomes the thing being played. Four subsystems
        written separately, and this is the object that joins them.

        It lives here rather than inside NacarProcessor for one reason that
        turned out to matter: the processor cannot be linked into the test
        runner - it defines the plugin's entry points and reaches for the editor
        - so logic that lives there is logic nothing can test. The hand-offs
        below are exactly the part that unit tests of the four engines do not
        cover, so they are the part that most needed to be testable.

        THREADING. Everything public is message thread only, except that the
        audio thread reads `slot()`. One background worker, not a pool: neither
        analysis nor mutation is worth parallelising against the other, and a
        single worker also means a mutation cannot start while the analysis of
        the same buffer is still deciding what key it is in.

        The caller drives it: `poll()` on a timer, which advances the decode and
        applies whatever has finished. Nothing here owns a clock.
    */
    class SourcePipeline
    {
    public:
        SourcePipeline();
        ~SourcePipeline();

        /** What the audio thread plays from. Never null; may hold nothing. */
        SampleSlot& slot() noexcept { return sampleSlot; }
        const SampleSlot& slot() const noexcept { return sampleSlot; }

        /** The decoder, for a caller that wants its progress or its limits. */
        SampleLoader& loader() noexcept { return decoder; }

        /** Starts an asynchronous decode, replacing anything in flight. An
            empty path means "the user removed the sample" and empties the
            slot. */
        void load (const juce::File&);

        /** Advances the decode and applies any finished background work. Cheap
            and safe to call at any rate; call it from a timer. */
        void poll();

        /** What the instrument currently knows about what it is playing.
            `analysed == false` until an analysis has actually finished - which
            includes the seconds after a new file arrives, when the honest
            answer is that it does not know yet. */
        const AnalysisResult& analysis() const noexcept { return current; }

        /** Starts a mutation against whatever is loaded. Returns false with
            `failure` set when it cannot start: nothing loaded, or one already
            running. With no source it does NOT invent one - rendering the
            synth's own output into a buffer is PRINT, and PRINT does not
            exist. */
        bool mutate (const mutation::Recipe&, const harmony::Context&, juce::String& failure);

        /** Renders the instrument's own output into the slot, so a patch you
            have just designed becomes audio you can mutate.

            The snapshot is taken by the CALLER, on the message thread, before
            this is called: a print must be of one state, and the pipeline
            cannot see the session tree the fx order and matrix come from.
            Returns false with `failure` set when one is already running. */
        bool print (const PrintEngine::Snapshot&, const PrintEngine::Settings&,
                    juce::String& failure);

        bool isPrinting() const noexcept { return printBusy.load (std::memory_order_relaxed); }

        /** A print finished. A successful one is already in the slot. */
        std::function<void (const PrintEngine::Result&)> onPrintFinished;

        bool isMutating() const noexcept { return mutationBusy.load (std::memory_order_relaxed); }
        bool isLoading() const noexcept  { return decoder.isBusy(); }

        // -- what the owner hears about, all on the message thread ----------

        /** A decode finished. Carries the loader's own result, including the
            failure text when it did not. */
        std::function<void (const SampleLoader::Result&)> onLoadFinished;

        /** An analysis landed, or was cleared because a new file arrived. */
        std::function<void (const AnalysisResult&)> onAnalysisChanged;

        /** A mutation finished. A successful one has already been published
            into the slot by the time this is called. */
        std::function<void (const mutation::Result&)> onMutationFinished;

    private:
        void startAnalysis();
        void clearAnalysis();

        SampleSlot   sampleSlot;
        SampleLoader decoder { sampleSlot };

        juce::ThreadPool worker { 1 };

        AnalysisResult current;

        /** Written by the worker, read by the owner once the matching flag
            reads true. The release/acquire pairing on the flag is what makes
            that safe; there is no lock and none is needed. */
        AnalysisResult     analysisPending;
        mutation::Result   mutationPending;
        PrintEngine::Result printPending;

        std::atomic<bool> analysisReady { false };
        std::atomic<bool> mutationReady { false };
        std::atomic<bool> mutationBusy  { false };
        std::atomic<bool> printReady    { false };
        std::atomic<bool> printBusy     { false };

        /** Bumped whenever a new buffer is published. A job that finishes
            holding a stale generation is discarded: the user has moved on, and
            writing an old file's key into the session would be worse than
            having none at all. */
        std::atomic<int> generation { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SourcePipeline)
    };
}
