#include "SourcePipeline.h"

#include "../../../Analysis/SampleAnalyser.h"
#include "../../../Mutation/MutationEngine.h"

namespace nacar
{
    SourcePipeline::SourcePipeline()
    {
        decoder.onFinished = [this] (const SampleLoader::Result& result)
        {
            // A new file invalidates whatever the last analysis concluded, and
            // it is cleared BEFORE the new one starts rather than left standing.
            // For the second or two the analyser takes, the honest answer to
            // "what key is this in?" is that the instrument does not know yet -
            // and the harmony engine is built to behave correctly when told
            // that, so telling it is safe as well as true.
            clearAnalysis();

            if (onLoadFinished != nullptr)
                onLoadFinished (result);

            if (result.ok)
                startAnalysis();
        };
    }

    SourcePipeline::~SourcePipeline()
    {
        // The worker holds `this`. It has to be finished with it before any
        // member below it is destroyed.
        worker.removeAllJobs (true, 4000);
    }

    void SourcePipeline::load (const juce::File& file)
    {
        if (file.getFullPathName().isEmpty())
        {
            decoder.clear();
            clearAnalysis();

            if (onLoadFinished != nullptr)
                onLoadFinished ({});

            return;
        }

        decoder.loadAsync (file);
    }

    void SourcePipeline::clearAnalysis()
    {
        current = AnalysisResult();
        ++generation;

        if (onAnalysisChanged != nullptr)
            onAnalysisChanged (current);
    }

    void SourcePipeline::startAnalysis()
    {
        auto sample = sampleSlot.acquire();

        if (sample == nullptr || sample->isEmpty())
            return;

        const int tag = generation.load (std::memory_order_relaxed);

        // The job holds its own reference, so the slot may be replaced under it
        // without the analysis reading freed memory. It is the generation tag,
        // not the pointer, that decides whether the answer is still wanted.
        worker.addJob ([this, sample, tag]
        {
            auto result = SampleAnalyser::analyse (*sample);

            if (generation.load (std::memory_order_relaxed) != tag)
                return;

            analysisPending = std::move (result);
            analysisReady.store (true, std::memory_order_release);
        });
    }

    bool SourcePipeline::mutate (const mutation::Recipe& recipe,
                                 const harmony::Context& context,
                                 juce::String& failure)
    {
        if (mutationBusy.load (std::memory_order_relaxed))
        {
            failure = "A MUTATION IS ALREADY RUNNING";
            return false;
        }

        auto source = sampleSlot.acquire();

        if (source == nullptr || source->isEmpty())
        {
            failure = "NOTHING TO MUTATE";
            return false;
        }

        mutationBusy.store (true, std::memory_order_relaxed);

        const auto snapshot = current;

        worker.addJob ([this, recipe, source, snapshot, context]
        {
            mutationPending = mutation::MutationEngine::render (recipe, *source, snapshot, context);
            mutationReady.store (true, std::memory_order_release);
        });

        return true;
    }

    bool SourcePipeline::print (const PrintEngine::Snapshot& snapshot,
                                const PrintEngine::Settings& settings,
                                juce::String& failure)
    {
        if (printBusy.load (std::memory_order_relaxed))
        {
            failure = "A PRINT IS ALREADY RUNNING";
            return false;
        }

        // Unlike mutate(), this needs no source: printing is how a source comes
        // into existence. It is the answer to NOTHING TO MUTATE.
        printBusy.store (true, std::memory_order_relaxed);

        worker.addJob ([this, snapshot, settings]
        {
            printPending = PrintEngine::render (snapshot, settings);
            printReady.store (true, std::memory_order_release);
        });

        return true;
    }

    void SourcePipeline::poll()
    {
        decoder.poll();

        if (analysisReady.load (std::memory_order_acquire))
        {
            current = analysisPending;
            analysisReady.store (false, std::memory_order_relaxed);

            if (onAnalysisChanged != nullptr)
                onAnalysisChanged (current);
        }

        if (printReady.load (std::memory_order_acquire))
        {
            const auto result = printPending;

            printPending = PrintEngine::Result();
            printReady.store (false, std::memory_order_relaxed);
            printBusy.store (false, std::memory_order_relaxed);

            // A print BECOMES the sample, exactly as a mutation does: the thing
            // you just rendered is the thing you are now playing, and the thing
            // MUTATE will work on.
            if (result.ok && result.audio != nullptr && ! result.audio->isEmpty())
            {
                sampleSlot.publish (result.audio);

                clearAnalysis();
                startAnalysis();
            }

            if (onPrintFinished != nullptr)
                onPrintFinished (result);
        }

        if (mutationReady.load (std::memory_order_acquire))
        {
            const auto result = mutationPending;

            mutationPending = mutation::Result();
            mutationReady.store (false, std::memory_order_relaxed);
            mutationBusy.store (false, std::memory_order_relaxed);

            // A successful mutation BECOMES the sample: published into the same
            // slot the engine plays from, so the thing you just made is the
            // thing you are now playing. That is the whole point of the
            // control, and it is why a Result carries a buffer rather than a
            // path to a file.
            if (result.ok && result.audio != nullptr && ! result.audio->isEmpty())
            {
                sampleSlot.publish (result.audio);

                // New audio, so what was known about the old audio no longer
                // describes it. Clearing bumps the generation, which also
                // discards any analysis still running on the previous buffer.
                clearAnalysis();
                startAnalysis();
            }

            if (onMutationFinished != nullptr)
                onMutationFinished (result);
        }

        sampleSlot.collectGarbage();
    }
}
