#pragma once

#include "../DspCommon.h"

#include <limits>

namespace nacar
{
    /**
        Small things the modulation layer shares and nothing else needs.

        They live in this header rather than in a seventh file because the
        modulation directory's file set is fixed by its contract, and the clock
        is first needed by the first thing that locks to tempo.
    */
    namespace mod
    {
        /** Hermite's 3t^2 - 2t^3.  Zero slope at both ends, so two segments
            joined with it have no visible corner.  Used by the two random LFO
            shapes and by Breath's jitter interpolation. */
        forcedinline float smoothStep (float t) noexcept
        {
            const float x = juce::jlimit (0.0f, 1.0f, t);
            return x * x * (3.0f - 2.0f * x);
        }

        /**
            Where this block sits in the song.

            Built once per block from MacroState.  `ppqPosition` is the position
            of the block's *first* sample; everything that locks to tempo derives
            its phase from that plus a per-sample increment, which is what makes
            a synced modulator land in the same place every time the same bar is
            played rather than merely running at the right speed.
        */
        struct Clock
        {
            double sampleRate  = 48000.0;
            double bpm         = 120.0;
            double ppqPosition = 0.0;
            bool   playing     = false;

            /** Beats advanced by one sample.  The tempo is clamped to the same
                20..300 range `fx::beatsToSeconds` uses, so a host that reports
                0 bpm while it works out what it is doing cannot divide by zero
                or stall every synced modulator at DC. */
            double beatsPerSample() const noexcept
            {
                return juce::jlimit (20.0, 300.0, bpm)
                     / (60.0 * juce::jmax (1.0, sampleRate));
            }
        };
    }

    // =======================================================================
    //  LFO  -  specification section 56
    //
    //  Seven shapes, free or tempo-synced, with a start-phase offset.
    //
    //  Two decisions are worth knowing before reading the code.
    //
    //  SONG LOCK.  When the transport is running and SYNC is on, the phase is
    //  computed from the host's ppq position, not accumulated.  A synced LFO
    //  that free-runs at the right rate drifts against the song the moment the
    //  user loops, scrubs or drops the playhead, and the drift is silent until
    //  it is the reason a bar does not sound like it did yesterday.  Deriving
    //  the phase from song position makes a render bit-identical from any start
    //  point.  The accumulator is still kept up to date every sample, so
    //  stopping the transport continues from exactly where the song left it.
    //
    //  SLEW.  SQUARE, both SAWs and RANDOM contain genuine discontinuities.  An
    //  LFO does not alias the way an audio oscillator does - it is not summed
    //  into the signal, it moves a parameter - but a parameter that steps is a
    //  filter cutoff that clicks, so the discontinuous shapes are passed through
    //  a 1.2 ms one-pole.  At the maximum 40 Hz rate that is 4.8 % of a cycle
    //  and a square's plateau still settles to better than 0.01 %; at musical
    //  rates it is inaudible as anything but the absence of a click.  The three
    //  continuous shapes bypass the filter but keep its state primed, so
    //  switching shape while the LFO runs does not jump.
    // =======================================================================
    class LFO
    {
    public:
        /** Everything the LFO is told, read from the registry once per block. */
        struct Settings
        {
            int   shape       = 0;      ///< 0 SINE .. 6 SMOOTH RANDOM
            bool  sync        = false;
            int   division    = 8;      ///< index into fx::kLfoDivisionBeats
            float rateHz      = 1.0f;   ///< used when sync is off
            float depth       = 0.0f;   ///< 0..1, output scale
            float phaseOffset = 0.0f;   ///< 0..1 turns, added at evaluation
        };

        /** Allocates nothing.  `seed` fixes the two random shapes for the life
            of the instance, so a session recalls identically. */
        void prepare (double sampleRate, juce::uint32 seed) noexcept;

        /** Returns to the start of a cycle and forgets the held random value. */
        void reset() noexcept;

        /** Writes `numSamples` of -1..1, already scaled by depth.  Realtime. */
        void process (float* dest, int numSamples,
                      const Settings&, const mod::Clock&) noexcept;

    private:
        /** The held value for cycle `step`.  A pure function of the seed and
            the index, so a synced RANDOM LFO holds the same values on every
            pass over the same bar and needs no state to do it. */
        static float randomAt (juce::uint32 seed, juce::int64 step) noexcept;

        float shapeValue (int shape, float phase, juce::int64 step) noexcept;

        static constexpr float kSlewSeconds = 0.0012f;

        double sampleRate = 48000.0;
        juce::uint32 seed = 0x5EEDu;

        double cycles = 0.0;        ///< free-running position, in cycles
        float  lastDepth = 0.0f;

        juce::int64 cachedStep = std::numeric_limits<juce::int64>::min();
        float cachedA = 0.0f, cachedB = 0.0f;

        fx::OnePole slew;
    };
}
