#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <atomic>

#include "../../Plugin/StateManager.h"   // ids::SEQUENCER, seq::divisions
#include "../EngineContext.h"
#include "LFO.h"                    // mod::Clock

namespace nacar
{
    // =======================================================================
    //  THE STEP SEQUENCER  -  specification section 129
    //
    //  Four lanes of sixteen steps.  Each lane has its own length, its own
    //  enable and its own target parameter; the whole sequencer shares one
    //  tempo division.  The SEQ page edits and persists all of it under
    //  ids::SEQUENCER; this is the part that advances it against the host
    //  clock and hands the result to the modulation overlay.
    //
    //  ---------------------------------------------------------------------
    //  THE HANDOFF.  Exactly ModMatrix's, deliberately: two staging buffers,
    //  an atomic index and a generation counter that the audio thread
    //  validates across its own copy.
    //
    //    writer (message thread)      reader (audio thread, once per block)
    //    ----------------------       ------------------------------------
    //    fill stage[1 - published]    g0 = generation (acquire)
    //    published = that index       if g0 == seen: nothing changed, done
    //    generation += 1              copy stage[published] into `live`
    //                                 g1 = generation (acquire)
    //                                 if g1 == g0: accept, seen = g0
    //                                 else:        discard, keep last block's
    //
    //  The index alone would be almost right; two publications inside one
    //  block - which dragging a value across a lane produces easily - can land
    //  the second in the buffer the audio thread is reading.  The counter
    //  closes that, because a torn copy is exactly a copy across which it
    //  moved.  The failure mode is one block of staleness during a drag, never
    //  a torn read, and the reader never blocks, spins or retries.  There is no
    //  second pattern in this instrument and this must not become one.
    //
    //  ---------------------------------------------------------------------
    //  POSITION, NOT ACCUMULATION.  While the transport runs, a lane's step is
    //  computed from `ppqPosition` - the position of the block's FIRST sample -
    //  and nothing is accumulated.  Pressing play twice over the same bar
    //  therefore produces the same steps in the same places, and a loop, a
    //  scrub or a jump lands correctly with no resynchronisation state at all.
    //  When the transport is stopped the sequencer free-runs on an internal
    //  beat counter at the host tempo, which is kept level with the song while
    //  the transport runs so that stopping continues from where the song was.
    //  Pulse and the synced LFOs already establish both halves of this.
    //
    //  ---------------------------------------------------------------------
    //  A GATE OF 0 HOLDS, IT DOES NOT ZERO.  A silent step means "nothing new
    //  happens here", so the lane keeps outputting the value of the most recent
    //  gated step.  Zeroing instead would make a half-gated pattern on a cutoff
    //  slam the filter shut on every rest, which is a defect dressed up as a
    //  feature.
    //
    //  The hold is resolved by SEARCHING BACKWARDS through the pattern - the
    //  most recent gated step at or before the current one, wrapping inside the
    //  lane's own length - rather than by remembering what was played.  That
    //  keeps the sequencer a pure function of transport position: a lane
    //  entered halfway through a bar holds exactly what it would have held had
    //  it been running since the start of it.  A lane with no gates at all
    //  holds nothing and writes nothing.
    //
    //  ---------------------------------------------------------------------
    //  THE UNITS.  A step value is 0..1 and it is ABSOLUTE: it names a position
    //  across the target parameter's whole range, not an offset from where the
    //  knob is.  The step well draws a bar whose height is the value, and a
    //  user who drags that bar to the top means "put the cutoff at the top",
    //  not "add a bit".  There is no per-lane depth control on the page and one
    //  must not be invented here - see section 64.
    //
    //  NacarEngine turns that into the overlay's units: the offset it writes is
    //  `value - normalisedUserValue (target)`, so the parameter lands exactly on
    //  the step, and any matrix routing to the same parameter then offsets from
    //  there.  A lane sets the position; the matrix moves around it.
    //
    //  ---------------------------------------------------------------------
    //  ONE LANE PER PARAMETER.  Two absolute values cannot be summed into one
    //  position, so when two lanes name the same target the LOWEST-NUMBERED one
    //  owns it and the other is ignored for that block.  First writer wins
    //  rather than last, so adding a lane 4 later cannot quietly take lane 1's
    //  parameter away from it.
    //
    //  Realtime: beginBlock allocates nothing, locks nothing and builds no
    //  juce::String.  It is O(lanes x steps), which is sixty-four compares.
    // =======================================================================
    class SequencerEngine
    {
    public:
        static constexpr int numLanes = seq::numLanes;
        static constexpr int numSteps = seq::numSteps;

        /** One lane, resolved off the message thread into plain values. */
        struct Lane
        {
            PID   target  = PID::count;     ///< PID::count means unresolved
            bool  enabled = false;
            int   length  = numSteps;       ///< 1..16
            float values[numSteps] {};      ///< 0..1, absolute
            bool  gates[numSteps] {};
        };

        struct Pattern
        {
            std::array<Lane, (size_t) numLanes> lanes {};
            double beatsPerStep = 0.25;     ///< the shared division, in beats
        };

        /** What one lane is asking of one parameter this block. */
        struct Applied
        {
            PID   target = PID::count;
            float value  = 0.0f;            ///< 0..1, absolute over the range
            int   lane   = -1;
        };

        SequencerEngine();

        /** Message thread, before audio starts.  Allocates nothing; it only
            clears the free-running position so a fresh transport starts at the
            top of a pattern. */
        void prepare (const EngineSpec&) noexcept;

        void reset() noexcept;

        /** Re-reads the SEQUENCER tree and publishes the result.  Message
            thread only: it splits juce::Strings and resolves parameter IDs.
            An invalid or empty tree publishes an empty pattern, which is how
            "the user has never opened the SEQ page" reaches the audio thread. */
        void rebuildFromTree (const juce::ValueTree& sequencerTree);

        /** Clears every lane and publishes that.  Message thread. */
        void clear();

        /** Advances every lane to this block's position and picks up any newly
            published pattern.  Audio thread, once per block, before any query. */
        void beginBlock (int numSamples, const mod::Clock&) noexcept;

        /** The parameters lanes are driving this block, after the one-lane-per-
            parameter rule.  Audio thread, valid after beginBlock. */
        int numApplied() const noexcept { return numAppliedTargets; }
        const Applied& applied (int index) const noexcept;

        /** The step each lane is on, for every lane whether it is enabled or
            not, so the page's playhead reads correctly while a lane is being
            edited.  Written by the audio thread, read by the message thread:
            one relaxed store and one relaxed load, which is all a playhead
            needs and all it may cost. */
        int currentStep (int lane) const noexcept;

        /** The pattern the audio thread is currently using.  For tests. */
        const Pattern& livePattern() const noexcept { return live; }

        /** Beats per step for the published pattern.  For tests. */
        double beatsPerStep() const noexcept { return live.beatsPerStep; }

    private:
        void publish (const Pattern&) noexcept;
        void refreshLive() noexcept;

        /** Which step a lane of this length is on at this song position.  A
            pure function: no state, no history, and correct for a negative
            position, which hosts report during a count-in. */
        static int stepAtBeats (double beats, double beatsPerStep, int length) noexcept;

        /** The most recent gated step at or before `step`, wrapping inside the
            lane's length.  -1 when the lane has no gates at all. */
        static int heldStep (const Lane&, int step) noexcept;

        // -- published across the thread boundary ---------------------------
        Pattern stage[2];
        std::atomic<int> published { 0 };
        std::atomic<juce::uint32> generation { 0 };

        // -- audio thread only ----------------------------------------------
        Pattern live {};
        juce::uint32 seenGeneration = 0;

        double freeBeats = 0.0;         ///< keeps running when the transport stops

        Applied appliedTargets[numLanes] {};
        int numAppliedTargets = 0;

        // -- audio thread writes, message thread reads ----------------------
        std::atomic<int> stepDisplay[numLanes];

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerEngine)
    };
}
