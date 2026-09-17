#include "SequencerEngine.h"

#include <cmath>

namespace nacar
{
    SequencerEngine::SequencerEngine()
    {
        stage[0] = Pattern {};
        stage[1] = Pattern {};
        live = Pattern {};

        for (auto& s : stepDisplay)
            s.store (0, std::memory_order_relaxed);
    }

    void SequencerEngine::prepare (const EngineSpec&) noexcept
    {
        reset();
    }

    void SequencerEngine::reset() noexcept
    {
        freeBeats = 0.0;
        numAppliedTargets = 0;

        for (auto& a : appliedTargets)
            a = Applied {};

        for (auto& s : stepDisplay)
            s.store (0, std::memory_order_relaxed);
    }

    // -----------------------------------------------------------------------
    //  The handoff.  See the header: this is ModMatrix's, not a variation.
    // -----------------------------------------------------------------------
    void SequencerEngine::publish (const Pattern& p) noexcept
    {
        const int next = 1 - published.load (std::memory_order_relaxed);

        stage[next] = p;

        published.store (next, std::memory_order_release);
        generation.fetch_add (1, std::memory_order_release);
    }

    void SequencerEngine::refreshLive() noexcept
    {
        const auto g0 = generation.load (std::memory_order_acquire);

        if (g0 == seenGeneration)
            return;

        const int index = published.load (std::memory_order_acquire);
        const Pattern candidate = stage[(size_t) juce::jlimit (0, 1, index)];

        // If the counter moved while that copy was being taken, the copy may be
        // a mixture of two patterns.  Discarding it costs one block of
        // staleness; accepting it could play a step nobody ever wrote.
        if (generation.load (std::memory_order_acquire) != g0)
            return;

        live = candidate;
        seenGeneration = g0;
    }

    void SequencerEngine::clear()
    {
        publish (Pattern {});
    }

    void SequencerEngine::rebuildFromTree (const juce::ValueTree& sequencerTree)
    {
        Pattern built {};

        built.beatsPerStep = seq::beatsForDivision (
            (int) sequencerTree.getProperty (ids::seqDivision, seq::defaultDivision));

        const int n = juce::jmin (numLanes, sequencerTree.getNumChildren());

        for (int i = 0; i < n; ++i)
        {
            const auto laneTree = sequencerTree.getChild (i);

            if (! laneTree.hasType (ids::SEQLANE))
                continue;

            auto& lane = built.lanes[(size_t) i];

            // The target is stored as the parameter's permanent string ID, so a
            // lane survives the parameter list growing.  An ID this build does
            // not have resolves to PID::count and the lane is inert rather than
            // pointing at whatever now sits at that index - which is also what
            // happens to a session written by a newer build.
            lane.target = ParameterRegistry::fromString (
                              laneTree.getProperty (ids::laneTarget).toString());

            lane.enabled = (bool) laneTree.getProperty (ids::laneEnabled);

            lane.length = juce::jlimit (1, numSteps,
                                        (int) laneTree.getProperty (ids::laneLength, numSteps));

            const auto values = juce::StringArray::fromTokens (
                laneTree.getProperty (ids::laneValues).toString(), ",", "");

            const auto gates = laneTree.getProperty (ids::laneGates).toString();

            for (int s = 0; s < numSteps; ++s)
            {
                lane.values[(size_t) s] = s < values.size()
                                              ? juce::jlimit (0.0f, 1.0f, values[s].getFloatValue())
                                              : 0.0f;

                lane.gates[(size_t) s] = s < gates.length() && gates[s] == '1';
            }
        }

        publish (built);
    }

    // -----------------------------------------------------------------------
    int SequencerEngine::stepAtBeats (double beats, double beatsPerStepIn, int length) noexcept
    {
        const double perStep = juce::jmax (1.0e-6, beatsPerStepIn);
        const int    len     = juce::jlimit (1, numSteps, length);

        // floor, not truncation: a host reporting a negative position during a
        // count-in must walk backwards through the pattern rather than sticking
        // on step 0 and then jumping.
        const double index = std::floor (beats / perStep);

        double wrapped = std::fmod (index, (double) len);

        if (wrapped < 0.0)
            wrapped += (double) len;

        return juce::jlimit (0, len - 1, (int) wrapped);
    }

    int SequencerEngine::heldStep (const Lane& lane, int step) noexcept
    {
        const int len = juce::jlimit (1, numSteps, lane.length);

        for (int back = 0; back < len; ++back)
        {
            const int s = ((step - back) % len + len) % len;

            if (lane.gates[(size_t) s])
                return s;
        }

        return -1;
    }

    // -----------------------------------------------------------------------
    void SequencerEngine::beginBlock (int numSamples, const mod::Clock& clock) noexcept
    {
        refreshLive();

        const double bps = clock.beatsPerSample();      // always > 0

        // Playing: the song position, so the same bar produces the same steps
        // every pass and a loop needs no resynchronisation.  Stopped: an
        // internal beat counter at the host tempo, so a pattern can be
        // auditioned without pressing play.  freeBeats is kept level with the
        // song while the transport runs, so stopping continues from there.
        const double start = clock.playing ? clock.ppqPosition : freeBeats;

        freeBeats = start + bps * (double) juce::jmax (0, numSamples);

        numAppliedTargets = 0;

        for (int i = 0; i < numLanes; ++i)
        {
            const auto& lane = live.lanes[(size_t) i];

            // Every lane, enabled or not: the page's playhead has to read
            // correctly for a lane the user is in the middle of switching on.
            //
            // THE BLOCK-RATE SEAM.  The step is the one containing the block's
            // first sample, so a boundary that lands mid-block takes effect at
            // the top of the next block - late by at most one buffer, never
            // skipped, and never early.  The overlay carries one value per
            // parameter per block, so that is the finest grain available here;
            // a division whose step is shorter than a buffer (1/32 at 300 BPM
            // is 25 ms, a 2048-sample buffer at 44.1 kHz is 46 ms) would
            // under-sample the pattern, and because the step is derived from
            // position rather than accumulated the lane stays locked to the
            // song instead of falling progressively behind.
            const int step = stepAtBeats (start, live.beatsPerStep, lane.length);

            stepDisplay[i].store (step, std::memory_order_relaxed);

            if (! lane.enabled || lane.target == PID::count)
                continue;

            const int held = heldStep (lane, step);

            if (held < 0)
                continue;       // no gate anywhere in the lane: it says nothing

            // One lane per parameter, lowest-numbered wins.  See the header.
            bool taken = false;

            for (int j = 0; j < numAppliedTargets && ! taken; ++j)
                taken = appliedTargets[j].target == lane.target;

            if (taken)
                continue;

            auto& a = appliedTargets[numAppliedTargets++];

            a.target = lane.target;
            a.value  = juce::jlimit (0.0f, 1.0f, lane.values[(size_t) held]);
            a.lane   = i;
        }
    }

    const SequencerEngine::Applied& SequencerEngine::applied (int index) const noexcept
    {
        return appliedTargets[(size_t) juce::jlimit (0, numLanes - 1, index)];
    }

    int SequencerEngine::currentStep (int lane) const noexcept
    {
        return stepDisplay[(size_t) juce::jlimit (0, numLanes - 1, lane)]
                   .load (std::memory_order_relaxed);
    }
}
