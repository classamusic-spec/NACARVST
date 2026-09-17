#include "SampleLoader.h"

#include <algorithm>
#include <atomic>

namespace nacar
{
    namespace
    {
        /** How much of the file one pass of the decode loop reads.  Rounded up
            to a whole number of overview buckets so that a bucket is never
            split across two reads - the overview is built inside this loop, not
            in a second pass over the finished buffer, and a bucket that
            straddled a chunk boundary would have to be. */
        constexpr int kTargetChunkSamples = 65536;
    }

    // =======================================================================
    //  Shared state
    //
    //  Held by shared_ptr so that a decode still running when the loader is
    //  destroyed has something valid to write its (by then ignored) result
    //  into, rather than a dangling `this`.
    // =======================================================================
    struct SampleLoader::Shared
    {
        Shared() { formats.registerBasicFormats(); }

        juce::AudioFormatManager formats;

        /** The number of the most recent request.  A job whose own number is
            not this one has been superseded and must publish nothing. */
        std::atomic<int> requestCounter { 0 };

        /** Non-zero while a decode is queued or running. */
        std::atomic<int> outstanding { 0 };

        juce::CriticalSection lock;
        bool              hasPending = false;   // guarded
        SampleBuffer::Ptr pendingBuffer;        // guarded
        Result            pendingResult;        // guarded
    };

    // =======================================================================
    //  The decode job
    // =======================================================================
    class SampleLoader::DecodeJob : public juce::ThreadPoolJob
    {
    public:
        DecodeJob (std::shared_ptr<Shared> s, juce::File f, int generation)
            : juce::ThreadPoolJob ("nacar sample decode"),
              shared (std::move (s)), file (std::move (f)), myGeneration (generation)
        {
        }

        /** The pool deletes the job whether it ran or was removed from the
            queue before it got the chance, so the "still going" count is
            balanced here rather than at the end of runJob - a superseded job
            that never ran would otherwise leave isBusy() stuck at true. */
        ~DecodeJob() override
        {
            shared->outstanding.fetch_sub (1, std::memory_order_acq_rel);
        }

        JobStatus runJob() override
        {
            Result result;
            result.file = file;
            result.displayName = file.getFileName();

            SampleBuffer::Ptr decoded;

            if (! superseded())
                decode (result, decoded);

            {
                const juce::ScopedLock sl (shared->lock);

                // Last request wins.  A slow first decode finishing after a
                // fast second one must not overwrite it, and neither must a
                // decode whose file has since been cleared.
                if (shared->requestCounter.load (std::memory_order_acquire) == myGeneration)
                {
                    shared->pendingBuffer = decoded;
                    shared->pendingResult = result;
                    shared->hasPending = true;
                }
            }

            return jobHasFinished;
        }

    private:
        bool superseded() const noexcept
        {
            return shouldExit()
                || shared->requestCounter.load (std::memory_order_acquire) != myGeneration;
        }

        void decode (Result& result, SampleBuffer::Ptr& out)
        {
            if (file.getFullPathName().isEmpty())
            {
                result.error = "No file.";
                return;
            }

            if (! file.existsAsFile())
            {
                result.error = "The file could not be found.";
                return;
            }

            std::unique_ptr<juce::AudioFormatReader> reader (
                shared->formats.createReaderFor (file));

            if (reader == nullptr)
            {
                // Everything the format manager cannot open lands here: a
                // format it does not know, and a file whose header is damaged
                // enough that the format it claims cannot be believed.
                result.error = "Unsupported or unreadable audio format.";
                return;
            }

            const double rate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
            const int fileChannels = (int) reader->numChannels;

            if (fileChannels <= 0)
            {
                result.error = "The file declares no audio channels.";
                return;
            }

            juce::int64 total = reader->lengthInSamples;

            if (total <= 0)
            {
                result.error = "The file contains no audio.";
                return;
            }

            const auto ceiling = (juce::int64) (maxLengthSeconds * rate);

            if (total > ceiling)
            {
                total = ceiling;
                result.truncated = true;
            }

            const int length   = (int) total;
            const int channels = juce::jmin (2, fileChannels);

            result.channelsDiscarded = fileChannels > 2;
            result.sourceRate  = rate;
            result.numChannels = channels;

            SampleBuffer::Ptr buffer (new SampleBuffer());

            try
            {
                // Cleared on allocation: a read that fails part-way leaves the
                // rest of the destination untouched, and untouched must mean
                // silence rather than whatever the heap had in it.
                buffer->audio.setSize (channels, length, false, true, false);
            }
            catch (...)
            {
                result.error = "Not enough memory to decode this file.";
                return;
            }

            buffer->sourceRate  = rate;
            buffer->file        = file;
            buffer->displayName = file.getFileName();

            // -- the overview, sized before the pass that fills it -----------
            const int bucketSamples =
                juce::jmax (1, (length + targetPeakBuckets - 1) / targetPeakBuckets);
            const int numBuckets = (length + bucketSamples - 1) / bucketSamples;

            auto& peaks = buffer->peaks;
            peaks.bucketSamples = bucketSamples;
            peaks.numBuckets    = numBuckets;
            peaks.numChannels   = channels;
            peaks.minimum.assign ((size_t) (numBuckets * channels), 0.0f);
            peaks.maximum.assign ((size_t) (numBuckets * channels), 0.0f);

            const int chunkBuckets = juce::jmax (1, kTargetChunkSamples / bucketSamples);
            const int chunkSamples = chunkBuckets * bucketSamples;

            int repaired = 0;

            for (int pos = 0; pos < length;)
            {
                if (superseded())
                {
                    result.error = "Superseded by a newer file.";
                    return;
                }

                const int n = juce::jmin (chunkSamples, length - pos);

                if (! reader->read (&buffer->audio, pos, n, (juce::int64) pos, true, true))
                {
                    // The reader failing mid-file is what a truncated or
                    // corrupt body looks like once the header has been
                    // believed.  Nothing partial is published.
                    result.error = "The file could not be decoded ("
                                 + juce::String ((int) (100.0 * pos / juce::jmax (1, length)))
                                 + "% in).";
                    return;
                }

                repaired += sanitise (*buffer, pos, n);
                buildPeaks (*buffer, pos, n, bucketSamples, length);

                pos += n;
            }

            if (repaired > 0)
            {
                // Not a failure: the audio is usable and the instrument would
                // rather play a repaired file than refuse it.  Reported so
                // that nobody concludes from silence that the file was clean.
                result.error = juce::String (repaired)
                             + " non-finite samples in this file were replaced with silence.";
            }

            result.lengthSamples = length;
            result.ok = true;
            out = buffer;
        }

        /** Replaces anything non-finite with silence and says how much it
            found.  One NaN reaching a voice's interpolator poisons the whole
            mix for the rest of the session, and a file is the one place in the
            instrument where a NaN can arrive from outside. */
        static int sanitise (SampleBuffer& buffer, int start, int numSamples) noexcept
        {
            int found = 0;

            for (int ch = 0; ch < buffer.audio.getNumChannels(); ++ch)
            {
                auto* d = buffer.audio.getWritePointer (ch);

                for (int i = start; i < start + numSamples; ++i)
                {
                    if (! std::isfinite (d[i]))
                    {
                        d[i] = 0.0f;
                        ++found;
                    }
                }
            }

            return found;
        }

        /** Reduces the chunk just read into min/max pairs.  Here rather than in
            a second pass because the second pass would read a buffer the audio
            thread may already be playing, and because walking a decoded file
            twice doubles the cost of the one operation the user is waiting on.

            Chunks are a whole number of buckets, so a bucket never straddles
            two calls. */
        static void buildPeaks (SampleBuffer& buffer, int start, int numSamples,
                                int bucketSamples, int length) noexcept
        {
            auto& peaks = buffer.peaks;
            const int channels = peaks.numChannels;

            for (int b = start / bucketSamples;
                 b * bucketSamples < start + numSamples && b < peaks.numBuckets;
                 ++b)
            {
                const int from = b * bucketSamples;
                const int to   = juce::jmin (length, from + bucketSamples);

                for (int ch = 0; ch < channels; ++ch)
                {
                    const auto* d = buffer.audio.getReadPointer (ch);

                    float lo = 0.0f, hi = 0.0f;

                    if (to > from)
                    {
                        lo = hi = d[from];

                        for (int i = from + 1; i < to; ++i)
                        {
                            lo = juce::jmin (lo, d[i]);
                            hi = juce::jmax (hi, d[i]);
                        }
                    }

                    peaks.minimum[(size_t) (b * channels + ch)] = lo;
                    peaks.maximum[(size_t) (b * channels + ch)] = hi;
                }
            }
        }

        std::shared_ptr<Shared> shared;
        juce::File file;
        const int myGeneration;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DecodeJob)
    };

    // =======================================================================
    //  SampleLoader
    // =======================================================================
    SampleLoader::SampleLoader (SampleSlot& slotToFill)
        : shared (std::make_shared<Shared>()),
          slot (slotToFill)
    {
        // One thread.  Two would let a superseded decode keep a core busy
        // while the file the user actually wants waits behind it, and the
        // work is IO-bound anyway.
        pool = std::make_unique<juce::ThreadPool> (1);

        startTimerHz (10);
    }

    SampleLoader::~SampleLoader()
    {
        stopTimer();

        // Nothing started from here may still be running when `shared` goes:
        // the job writes its result under the lock at the very end.  Four
        // seconds is generous for a decode that has already been told to stop
        // between chunks, and the pool's own destructor would wait anyway.
        shared->requestCounter.fetch_add (1, std::memory_order_acq_rel);
        pool->removeAllJobs (true, 4000);
        pool.reset();
    }

    void SampleLoader::loadAsync (const juce::File& file)
    {
        // Bumping the counter before anything else is what cancels the decode
        // already in flight: it checks this between chunks, and its result is
        // discarded under the lock even if it finishes first.
        const int generation = shared->requestCounter.fetch_add (1, std::memory_order_acq_rel) + 1;

        // Ask, do not wait.  removeAllJobs with a zero timeout signals the
        // running job to exit and returns; blocking the message thread here is
        // exactly the stall this class exists to avoid.
        pool->removeAllJobs (true, 0);

        {
            // Anything a superseded decode left behind is not this file.
            const juce::ScopedLock sl (shared->lock);
            shared->hasPending = false;
            shared->pendingBuffer = nullptr;
            shared->pendingResult = Result();
        }

        shared->outstanding.fetch_add (1, std::memory_order_acq_rel);
        pool->addJob (new DecodeJob (shared, file, generation), true);
    }

    void SampleLoader::clear()
    {
        shared->requestCounter.fetch_add (1, std::memory_order_acq_rel);
        pool->removeAllJobs (true, 0);

        {
            const juce::ScopedLock sl (shared->lock);
            shared->hasPending = false;
            shared->pendingBuffer = nullptr;
            shared->pendingResult = Result();
        }

        slot.publish (nullptr);
        slot.collectGarbage();
    }

    bool SampleLoader::isBusy() const noexcept
    {
        return shared->outstanding.load (std::memory_order_acquire) > 0;
    }

    void SampleLoader::poll()
    {
        bool  gotResult = false;
        Result result;
        SampleBuffer::Ptr buffer;

        {
            const juce::ScopedLock sl (shared->lock);

            if (shared->hasPending)
            {
                gotResult = true;
                result = shared->pendingResult;
                buffer = shared->pendingBuffer;

                shared->hasPending = false;
                shared->pendingBuffer = nullptr;
                shared->pendingResult = Result();
            }
        }

        if (gotResult)
        {
            // A failed decode clears the slot rather than leaving the previous
            // sample playing.  The session tree already names the new file, and
            // an instrument that plays the old sample while the interface names
            // the new one is worse than an instrument that plays nothing.
            slot.publish (result.ok ? buffer : SampleBuffer::Ptr());

            if (onFinished != nullptr)
                onFinished (result);
        }

        // Every tick, not only after a result: the audio thread has to have
        // moved on before a retired buffer can go, and that is a later tick
        // than the one that retired it.
        slot.collectGarbage();
    }

    void SampleLoader::timerCallback()
    {
        poll();
    }
}
