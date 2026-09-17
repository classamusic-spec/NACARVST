#include "SampleBuffer.h"

#include <vector>

namespace nacar
{
    /**
        THE HANDOFF.

        `SampleBuffer.h` states the rule this file exists to enforce: the audio
        thread never runs a SampleBuffer destructor.  Reference counting alone
        does not give that.  It gives the audio thread a safe *read*, and in
        exchange it hands the audio thread the possibility of being the last
        holder - at which point `decReferenceCount()` frees several megabytes
        inside the callback.

        So the slot keeps a reference to everything it has ever published until
        the message thread can prove nobody else wants it.

        ------------------------------------------------------------------
        WHY THERE IS A SECOND COUNTER

        The header sketches `acquire()` as one relaxed load and one atomic
        increment, and describes the collection rule as "the reference count
        has fallen to one".  Implemented literally that has a window:

            audio    loads currentRaw            -> B   (refcount 1)
            message  publish(C): retires B, drops its own handle on B
            message  collectGarbage: B's count is 1, frees B
            audio    increments the count on freed memory

        The count is 1 during that window precisely because the reader has not
        got to its increment yet.  The count cannot distinguish "nobody wants
        it" from "somebody is a few nanoseconds away from wanting it".

        So `acquire()` brackets itself: it bumps `enterCount` BEFORE the load
        and stores the same value into `exitCount` after the increment.  The
        reader is a single thread, so `exitCount >= enterCount` read in that
        order proves that every acquire which began before the read has
        finished.  A retired buffer is freed only when

            (a) every acquire in flight at the time of the check has finished,
                so nothing can still be about to take a reference to it, and
            (b) its reference count is one - the one this slot holds.

        Together those are sound rather than probabilistic.  The cost on the
        audio thread is one more atomic store per block, and the benefit over
        a pure count test is that it is also correct when the audio thread is
        IDLE: with enter == exit == 0 a retired buffer is collectable at once,
        instead of sitting in the retired list until the slot is destroyed.

        All four operations are sequentially consistent on purpose.  The proof
        above is a statement about the single total order seq_cst gives; with
        acquire/release alone the ordering between the reader's counter bump
        and the writer's read of that counter is not established, and this is
        once per block, not once per sample.
    */
    class SampleSlot::Impl
    {
    public:
        // -- audio thread ---------------------------------------------------
        std::atomic<SampleBuffer*> currentRaw { nullptr };
        std::atomic<juce::int64>   enterCount { 0 };
        std::atomic<juce::int64>   exitCount  { 0 };

        // -- message thread only --------------------------------------------
        SampleBuffer::Ptr currentOwned;
        std::vector<SampleBuffer::Ptr> retired;

        // Not const: the counters below are written on the reader's own path.
        SampleBuffer::Ptr acquire() noexcept
        {
            const auto ticket = enterCount.fetch_add (1, std::memory_order_seq_cst) + 1;

            SampleBuffer::Ptr result (currentRaw.load (std::memory_order_seq_cst));

            exitCount.store (ticket, std::memory_order_seq_cst);

            return result;
        }

        void publish (SampleBuffer::Ptr incoming)
        {
            if (incoming.get() == currentRaw.load (std::memory_order_seq_cst))
                return;

            // Retire before republishing, never after: between the two the
            // audio thread must never be able to see a pointer this object
            // does not hold a reference to.
            if (currentOwned.get() != nullptr)
                retired.push_back (currentOwned);

            currentOwned = incoming;
            currentRaw.store (incoming.get(), std::memory_order_seq_cst);
        }

        void collectGarbage()
        {
            if (retired.empty())
                return;

            // Read enter first, then exit.  Any acquire that had begun before
            // the first load has a ticket no greater than `entered`, and an
            // exit value of at least `entered` proves it has finished - the
            // reader is one thread, so its tickets retire in order.
            const auto entered = enterCount.load (std::memory_order_seq_cst);
            const auto exited  = exitCount.load  (std::memory_order_seq_cst);

            if (exited < entered)
                return;

            auto* const live = currentRaw.load (std::memory_order_seq_cst);

            for (int i = (int) retired.size(); --i >= 0;)
            {
                auto& held = retired[(size_t) i];

                // Defensive: a buffer that has been published again is not
                // garbage, whatever the retired list says.
                if (held.get() == live)
                {
                    retired.erase (retired.begin() + i);
                    continue;
                }

                if (held->getReferenceCount() == 1)
                    retired.erase (retired.begin() + i);   // frees, here, on this thread
            }
        }
    };

    // =======================================================================
    SampleSlot::SampleSlot() : impl (std::make_unique<Impl>()) {}

    SampleSlot::~SampleSlot()
    {
        // Teardown is message thread with the audio stopped, so anything still
        // retired can go now.  Clearing `retired` before `currentOwned` keeps
        // the invariant that the slot outlives everything it published.
        impl->currentRaw.store (nullptr, std::memory_order_seq_cst);
        impl->retired.clear();
        impl->currentOwned = nullptr;
    }

    void SampleSlot::publish (SampleBuffer::Ptr incoming)
    {
        impl->publish (std::move (incoming));
    }

    SampleBuffer::Ptr SampleSlot::acquire() const noexcept
    {
        return impl->acquire();
    }

    void SampleSlot::collectGarbage()
    {
        impl->collectGarbage();
    }

    bool SampleSlot::hasRetired() const
    {
        return ! impl->retired.empty();
    }
}
