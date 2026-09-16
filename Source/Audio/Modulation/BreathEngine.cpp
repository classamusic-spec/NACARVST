#include "BreathEngine.h"

#include <algorithm>
#include <cmath>

namespace nacar
{
    namespace
    {
        /** Ratios at RANDOM = 0.  Deliberately *near* integers rather than
            integers: exact 2:3:5:8 would give the sum a period of one cycle of
            the slowest component, which is the periodicity Breath exists to
            avoid.  These repeat after a few hundred cycles, which is what
            "almost periodic" means. */
        constexpr float kNearRational[5] = { 1.0f, 2.013f, 3.027f, 5.041f, 7.963f };

        /** Ratios at RANDOM = 1.  Mutually irrational, so no two components
            share a period and the sum has none: phi, e, Feigenbaum's delta and
            e squared, all against a slowest component of 1. */
        constexpr float kIrrational[5] = { 1.0f, 1.618034f, 2.718282f, 4.669202f, 7.389056f };

        /** Weights sum to exactly 1.0, which is the whole boundedness argument.
            The slowest component carries most of the energy because that is
            what makes the movement read as one drifting thing rather than as
            five things beating. */
        constexpr float kWeights[5] = { 0.40f, 0.24f, 0.16f, 0.12f, 0.08f };

        constexpr float kJitterDepth      = 0.6f;    ///< rate stays in 0.4 .. 1.6
        constexpr float kJitterRateFactor = 0.5f;    ///< jitter clock, x SPEED
        constexpr float kStepRateFactor   = 1.5f;    ///< step clock, x SPEED
        constexpr float kStepIrregularity = 0.75f;   ///< interval spread at RANDOM 1
        constexpr float kGlideFraction    = 0.28f;   ///< glide time / step interval
    }

    // -----------------------------------------------------------------------
    void BreathEngine::prepare (double sr, juce::uint32 s) noexcept
    {
        sampleRate = juce::jmax (1.0, sr);
        seed = s | 1u;

        // Start phases are spread by the generator rather than set to zero, so
        // the five components begin uncorrelated.
        fx::Rng init (seed);

        for (int k = 0; k < kNumComponents; ++k)
            startPhase[k] = init.next01();

        reset();
    }

    void BreathEngine::reset() noexcept
    {
        jitterRng.setSeed (seed ^ 0x51ED2701u);
        stepRng  .setSeed (seed ^ 0x2F1B3C4Du);

        for (int k = 0; k < kNumComponents; ++k)
        {
            phase[k] = startPhase[k];
            jitterPrev[k] = jitterRng.nextBipolar();
            jitterNext[k] = jitterRng.nextBipolar();
        }

        jitterPhase = 0.0f;
        stepPhase   = 0.0f;
        stepScale   = 1.0f;
        held        = 0.0f;
        glided      = 0.0f;

        current = 0.0f;
        slope   = 0.0f;
        counter = 0;

        lastAmount = 0.0f;
    }

    // -----------------------------------------------------------------------
    void BreathEngine::advanceControlTick (float speed, float random, float shape,
                                           float glidePerTick) noexcept
    {
        const float dt = (float) kControlPeriod / (float) sampleRate;

        // -- jitter clock ---------------------------------------------------
        jitterPhase += speed * kJitterRateFactor * dt;

        while (jitterPhase >= 1.0f)
        {
            jitterPhase -= 1.0f;

            for (int k = 0; k < kNumComponents; ++k)
            {
                jitterPrev[k] = jitterNext[k];
                jitterNext[k] = jitterRng.nextBipolar();
            }
        }

        const float jt = mod::smoothStep (jitterPhase);

        // -- the five sines --------------------------------------------------
        float sum = 0.0f;

        for (int k = 0; k < kNumComponents; ++k)
        {
            const float ratio = fx::lerp (kNearRational[k], kIrrational[k], random);
            const float j     = fx::lerp (jitterPrev[k], jitterNext[k], jt);

            // 0.4 .. 1.6 of nominal: always forward, never stalled.
            const float rate = speed * ratio * (1.0f + kJitterDepth * random * j);

            phase[k] += rate * dt;
            phase[k] -= std::floor (phase[k]);

            sum += kWeights[k] * fx::sineTurns (phase[k]);
        }

        // sum is in -1..1 because the weights sum to 1.  No clamp is applied
        // and none is needed; if one ever were, the construction would be wrong.

        // -- step clock and glide -------------------------------------------
        stepPhase += speed * kStepRateFactor * stepScale * dt;

        while (stepPhase >= 1.0f)
        {
            stepPhase -= 1.0f;

            held = sum;

            // The next interval is drawn now, so RANDOM makes the decisions
            // irregular in time as well as in value.  0.25 .. 1.75 of nominal.
            stepScale = 1.0f + kStepIrregularity * random * (stepRng.next01() * 2.0f - 1.0f);
            stepScale = juce::jmax (0.2f, stepScale);
        }

        // One pole towards a target that is itself in -1..1: no overshoot is
        // possible, so the glided signal is bounded by the same argument.
        glided += (held - glided) * glidePerTick;

        const float target = fx::lerp (sum, glided, shape);

        current = fx::guard (current);
        slope = (target - current) * (1.0f / (float) kControlPeriod);
    }

    // -----------------------------------------------------------------------
    void BreathEngine::process (float* dest, int numSamples, const Settings& s) noexcept
    {
        if (dest == nullptr || numSamples <= 0)
            return;

        if (! s.enabled)
        {
            // Frozen rather than reset: turning Breath back on resumes the same
            // trajectory, which keeps a render deterministic across an automated
            // on/off without the generator having to remember anything extra.
            juce::FloatVectorOperations::clear (dest, numSamples);
            lastAmount = 0.0f;
            return;
        }

        const float speed  = juce::jlimit (0.001f, 8.0f, s.speedHz);
        const float random = juce::jlimit (0.0f, 1.0f, s.random);
        const float shape  = juce::jlimit (0.0f, 1.0f, s.shape);
        const float amount = juce::jlimit (0.0f, 1.0f, s.amount);

        // Glide coefficient, computed once per block rather than per tick: it
        // is the only transcendental in the generator and it only changes when
        // SPEED does.
        const float dt        = (float) kControlPeriod / (float) sampleRate;
        const float stepRate  = juce::jmax (1.0e-4f, speed * kStepRateFactor);
        const float glideTime = juce::jmax (0.002f, kGlideFraction / stepRate);
        const float glidePerTick = juce::jlimit (0.0f, 1.0f,
                                                 1.0f - std::exp (-dt / glideTime));

        fx::Ramp amountRamp;
        amountRamp.set (lastAmount, amount, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            if (counter <= 0)
            {
                counter = kControlPeriod;
                advanceControlTick (speed, random, shape, glidePerTick);
            }

            --counter;
            current += slope;

            dest[i] = juce::jlimit (-1.0f, 1.0f, fx::guard (current * amountRamp.at (i)));
        }

        lastAmount = amount;
    }
}
