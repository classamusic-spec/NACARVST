#pragma once

#include "LFO.h"

namespace nacar
{
    // =======================================================================
    //  BREATH  -  specification sections 57 and 94
    //
    //  "BREATH is not another LFO.  BREATH creates organic non-repeating
    //   movement."
    //
    //  That is the whole brief, and it rules out the two obvious answers.  A
    //  slow LFO has a period, and a listener finds a period in a few seconds.
    //  A bounded random walk has no period but has to be clamped, and a clamped
    //  walk spends most of its life against the rails, which reads as a signal
    //  that keeps hitting the ends rather than as something alive.
    //
    //  The synth's DriftGenerator already solved the small version of this with
    //  two sines at an irrational ratio: bounded by construction, no clamp, no
    //  period inside any musically interesting span.  Breath is the capable
    //  version of the same idea, and it is genuinely more than two sines.
    //
    //  FIVE INTERACTING SOURCES.  Five sines whose weights sum to exactly 1, so
    //  the sum is in -1..1 by construction and never needs a limiter.  The
    //  slowest runs at SPEED; the other four run at SPEED times a ratio.
    //
    //  RANDOM does two separate things, which is what gives the control its
    //  range rather than just its depth:
    //
    //    - it morphs the four ratios from *near* rational (2.013, 3.027, 5.041,
    //      7.963 - a repeat every few hundred cycles, so it reads as almost
    //      periodic without actually being periodic) to mutually irrational
    //      (phi, e, Feigenbaum's delta, e squared), which have no common period
    //      at all;
    //
    //    - it opens a rate jitter.  Each component's instantaneous rate is
    //      multiplied by 1 + 0.6 * RANDOM * j, where j is a deterministic
    //      random value interpolated smoothly between draws.  Because the
    //      jitter modulates the *rate*, its effect on phase is an integral of
    //      a random sequence: the trajectory is not a closed form, it does not
    //      repeat until the generator's own 2^32-state cycle does, and the
    //      output is still a weighted sum of sines, so it is still bounded with
    //      no clamp anywhere.  The multiplier stays in 0.4 .. 1.6, so no
    //      component can stall or reverse.
    //
    //  SHAPE gives Breath its second personality.  A step clock - regular at
    //  RANDOM 0, increasingly irregular above it - samples the continuous sum
    //  and glides to that value with a one-pole whose time constant is 28 % of
    //  the mean step interval, so it always arrives before the next decision.
    //  SHAPE crossfades the continuous wander into that sample-and-glide.  A
    //  one-pole cannot overshoot, its target is a sample of a signal already in
    //  -1..1, and a crossfade of two signals in -1..1 is in -1..1: bounded, end
    //  to end, without a single clamp.
    //
    //  AMOUNT is the output depth, ramped across the block so that turning the
    //  knob does not step at a buffer boundary.
    //
    //  DETERMINISTIC BUT NOT PERIODIC.  Those are different properties and both
    //  are required.  Every random quantity comes from an fx::Rng seeded from a
    //  constant in prepare(), and draws happen only when one of the two clocks
    //  wraps - timing that depends on elapsed samples and the parameter values,
    //  never on the block size.  Two renders of the same session are identical;
    //  neither of them repeats.
    //
    //  COST.  The chain runs once every 16 samples and is linearly interpolated
    //  in between, exactly as DriftGenerator does: at 44.1 kHz that is a
    //  2756 Hz control rate for a modulator whose fastest component tops out
    //  near 28 Hz.  The interpolation is a convex combination of two bounded
    //  values, so it cannot break the bound either.
    // =======================================================================
    class BreathEngine
    {
    public:
        struct Settings
        {
            bool  enabled = true;
            float amount  = 0.35f;   ///< 0..1 output depth
            float speedHz = 0.22f;   ///< base rate of the slowest component
            float random  = 0.45f;   ///< 0 almost periodic .. 1 unpredictable
            float shape   = 0.50f;   ///< 0 drifting .. 1 stepped and glided
        };

        /** Allocates nothing.  The seed fixes this instance for the session. */
        void prepare (double sampleRate, juce::uint32 seed) noexcept;

        /** Returns to the seeded start state, not to zero: five sines all
            starting in phase would produce one large excursion and then settle,
            which is exactly the artefact this generator exists to avoid. */
        void reset() noexcept;

        /** Writes `numSamples` of -1..1.  Realtime. */
        void process (float* dest, int numSamples, const Settings&) noexcept;

    private:
        void advanceControlTick (float speed, float random, float shape,
                                 float glidePerTick) noexcept;

        static constexpr int kNumComponents = 5;
        static constexpr int kControlPeriod = 16;

        double sampleRate = 48000.0;
        juce::uint32 seed = 0xB4EA71u;

        fx::Rng jitterRng, stepRng;

        float phase     [kNumComponents] {};
        float jitterPrev[kNumComponents] {};
        float jitterNext[kNumComponents] {};
        float startPhase[kNumComponents] {};

        float jitterPhase = 0.0f;
        float stepPhase   = 0.0f;
        float stepScale   = 1.0f;
        float held        = 0.0f;
        float glided      = 0.0f;

        float current = 0.0f, slope = 0.0f;
        int   counter = 0;

        float lastAmount = 0.0f;
    };
}
