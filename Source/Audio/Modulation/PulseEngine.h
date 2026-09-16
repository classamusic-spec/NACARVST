#pragma once

#include "LFO.h"

#include <atomic>

namespace nacar
{
    // =======================================================================
    //  PULSE  -  specification sections 82 and 83
    //
    //  Section 83 is the reason this exists, and the MOD page already prints it
    //  under the destination knobs, so the interface is promising the user this
    //  behaviour:
    //
    //      "A kick event may simultaneously make a sound quieter, darker,
    //       narrower and drier.  This is more sophisticated than volume-only
    //       sidechain."
    //
    //  WHY FIVE SHAPES AND NOT ONE.  When something loud happens nearby, the
    //  ear does not simply turn the other sound down.  Three things happen at
    //  different speeds.  High-frequency detail is masked first and recovers
    //  first, because forward masking is shortest at high frequencies.  The
    //  stereo image collapses towards the centre and springs back fastest of
    //  all, because localisation is re-established as soon as the interaural
    //  cues are audible again.  Reverb is masked longest, because a tail is
    //  quieter than the direct sound that produced it and therefore stays under
    //  the masker after the direct sound has re-emerged.
    //
    //  So one trigger drives five envelopes with different time scalings and
    //  different curvature, not one envelope scaled five ways:
    //
    //    destination  attack  release  curve      why
    //    VOLUME        1.00    1.00    reference  the duck everyone knows
    //    FILTER        0.80    0.70    sharper    HF masked first, back first
    //    WIDTH         0.85    0.60    sharper    the image recovers fastest
    //    SPACE         1.20    1.60    softer     tails stay masked longest
    //    MEMORY        1.00    1.30    softer     a texture change, not a gate
    //
    //  Scalings multiply the user's ATTACK and RELEASE, so the relationship
    //  between the five is fixed while the absolute speed stays the user's.
    //  The curve column is a bias added to SMOOTH before it is clamped.
    //
    //  THE CURVES.  Both stages run on an exact phase ramp rather than on a
    //  one-pole aimed at an asymptote, so a 2 ms attack is 2 ms at every sample
    //  rate and the stage ends at exactly 1.  SMOOTH morphs the attack from
    //  concave (straight down into the duck) to an S, and the release from
    //  convex (out quickly, then easing in) to an S that holds the duck a
    //  moment longer before letting go.  Both functions are monotone and both
    //  end points are exact, so the output is in 0..1 by construction.
    //
    //  RETRIGGER.  A trigger during the release restarts the attack from the
    //  current level rather than from zero, so a fast division with a long
    //  release never steps.
    //
    //  THE THREE SOURCES.
    //    CLOCK      the host grid.  The exact sample inside the block is
    //               computed from ppq, not rounded to the block boundary.
    //    MIDI       noteTriggered().
    //    SIDECHAIN  setSidechainInput() plus the transient detector below.
    //               NOTHING IN NACAR CALLS IT: the instrument has no side
    //               input.  Selecting SIDECHAIN therefore falls back to CLOCK
    //               for any block in which no input arrived, because silently
    //               doing nothing when a user selects a source is worse than
    //               doing something defensible.
    // =======================================================================
    class PulseEngine
    {
    public:
        enum class Destination { volume = 0, filter, space, width, memory };

        static constexpr int numDestinations = 5;
        static constexpr int maxTriggersPerBlock = 32;

        struct Settings
        {
            bool  enabled    = false;
            int   source     = 0;       ///< 0 CLOCK, 1 SIDECHAIN, 2 MIDI
            int   division   = 6;       ///< index into fx::kPulseDivisionBeats
            float attackSec  = 0.002f;
            float releaseSec = 0.22f;
            float smooth     = 0.40f;
        };

        /** Allocates the five envelope buffers.  Message thread. */
        void prepare (double sampleRate, int maxBlockSize);

        void reset() noexcept;

        /** A note-on.  `sampleOffset` is where in the coming block it lands;
            the no-argument form puts it at the top of the block. */
        void noteTriggered (int sampleOffset) noexcept;

        /** Hands the follower the block that should duck this one.  The pointer
            is used during the next process() call and never retained.  NOTHING
            CALLS THIS YET - see the class comment. */
        void setSidechainInput (const float* mono, int numSamples) noexcept;

        /** Fills all five envelope buffers with `numSamples` of 0..1.  When
            disabled it writes silence and returns the envelopes to idle. */
        void process (int numSamples, const Settings&, const mod::Clock&) noexcept;

        /** Valid for `numSamples` after process(). Never null after prepare(). */
        const float* envelope (Destination) const noexcept;

    private:
        struct Duck
        {
            enum class Stage { idle, attack, release };

            Stage stage = Stage::idle;
            float pos = 0.0f, from = 0.0f, level = 0.0f;

            void trigger() noexcept { from = level; pos = 0.0f; stage = Stage::attack; }
            void clear() noexcept   { stage = Stage::idle; pos = from = level = 0.0f; }

            float advance (float attackInc, float releaseInc, float smooth) noexcept;
        };

        struct Triggers
        {
            int offsets[maxTriggersPerBlock] {};
            int count = 0;

            void add (int o) noexcept
            {
                if (count < maxTriggersPerBlock)
                    offsets[count++] = o;
            }
        };

        /** Advances the grid.  Fires into `t` when it is non-null; passing
            null advances the clock without triggering, which is what keeps the
            grid current while another source is selected. */
        void collectClock (Triggers* t, int numSamples, int division, const mod::Clock&) noexcept;
        void collectMidi (Triggers&, int numSamples) noexcept;
        bool collectSidechain (Triggers&, int numSamples) noexcept;

        static constexpr int kMidiRing = 32;

        double sampleRate = 48000.0;
        int    capacity = 0;

        std::vector<float> buffers[numDestinations];
        Duck ducks[numDestinations];

        // -- clock ----------------------------------------------------------
        double freeBeats = 0.0;     ///< keeps running when the transport stops

        // -- midi -----------------------------------------------------------
        int midiOffsets[kMidiRing] {};
        std::atomic<int> midiWrite { 0 };
        int midiRead = 0;

        // -- sidechain ------------------------------------------------------
        const float* sidechain = nullptr;
        int sidechainSamples = 0;

        float fastEnv = 0.0f, slowEnv = 0.0f;
        float fastAttack = 0.0f, fastRelease = 0.0f, slowCoef = 0.0f;
        bool  armed = true;
    };
}
