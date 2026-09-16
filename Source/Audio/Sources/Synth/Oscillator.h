#pragma once

#include "SynthCommon.h"

namespace nacar::synth
{
    /** Matches the SINE|TRIANGLE|SAW|PULSE|WAVETABLE choice in ParameterList.h. */
    enum class Waveform { sine = 0, triangle, saw, pulse, wavetable };

    inline Waveform waveformFromIndex (int i) noexcept
    {
        return (Waveform) juce::jlimit (0, 4, i);
    }

    /** Matches FREE|RESET|RANDOM|CONTROLLED. */
    enum class PhaseMode { free = 0, reset, random, controlled };

    inline PhaseMode phaseModeFromIndex (int i) noexcept
    {
        return (PhaseMode) juce::jlimit (0, 3, i);
    }

    // -----------------------------------------------------------------------
    //  PolyBLEP
    //
    //  A naive saw is a step function sampled without any band limiting, so
    //  every harmonic above Nyquist folds back into the audible band; play one
    //  at MIDI 96 and the aliases sit a perfect fourth away from the harmonics
    //  they came from and the result sounds broken.
    //
    //  PolyBLEP replaces the ideal step with the 2-point polynomial residual of
    //  a band-limited step and adds it to the naive waveform around each
    //  discontinuity.  It is not as clean as a full BLEP table, but it costs
    //  four arithmetic operations, it is exact at DC, and the residual aliasing
    //  sits roughly 20 dB below the naive case at the top of the keyboard
    //  while being free at the bottom, where almost every note actually lives.
    //
    //  `t`  is the oscillator phase in [0, 1)
    //  `dt` is the phase increment per sample, i.e. f / fs
    // -----------------------------------------------------------------------
    forcedinline float polyBlep (float t, float dt) noexcept
    {
        if (dt <= 0.0f)
            return 0.0f;

        if (t < dt)                       // just after the discontinuity
        {
            const float x = t / dt;
            return x + x - x * x - 1.0f;
        }

        if (t > 1.0f - dt)                // just before the next one
        {
            const float x = (t - 1.0f) / dt;
            return x * x + x + x + 1.0f;
        }

        return 0.0f;
    }

    /** The same 2-point residual, for a discontinuity whose position is known
        in time rather than in phase.

        Hard sync needs this: the reset is driven by another oscillator, so the
        two-branch form above cannot anticipate it from the slave's own phase.
        Given a jump that happens `frac` of a sample after the sample now being
        written, this returns the residual sampled at this sample and at the
        next one.

        The caller scales both by `0.5 * (valueAfterJump - valueBeforeJump)`,
        which is the same convention the periodic form above uses: a sawtooth
        wrap is a jump of -2 and ends up subtracting one whole residual. */
    forcedinline void blepSplit (float frac, float& residualNow, float& residualNext) noexcept
    {
        const float f = juce::jlimit (0.0f, 0.99999f, frac);

        // Residual r(tau), tau in samples relative to the jump:
        //   r(tau) =  (tau + 1)^2   for tau in [-1, 0)
        //   r(tau) = -(1 - tau)^2   for tau in [ 0, 1)
        // This sample sits at tau = -f, the next at tau = 1 - f.
        const float a = 1.0f - f;
        residualNow  = a * a;
        residualNext = -f * f;
    }

    /** Naive-plus-BLEP sawtooth, in [-1, 1]. */
    forcedinline float blepSaw (float phase, float inc) noexcept
    {
        return (2.0f * phase - 1.0f) - polyBlep (phase, inc);
    }

    /** Naive-plus-BLEP pulse with DC removed.

        The DC term of a pulse of width w is (2w - 1); leaving it in means that
        modulating the width modulates DC, which thumps through every following
        stage and eats headroom.  Subtracting it costs one operation and makes
        PWM behave. */
    forcedinline float blepPulse (float phase, float inc, float width) noexcept
    {
        const float w = juce::jlimit (0.02f, 0.98f, width);

        float v = (phase < w) ? 1.0f : -1.0f;

        v += polyBlep (phase, inc);

        float t2 = phase + 1.0f - w;
        t2 -= std::floor (t2);
        v -= polyBlep (t2, inc);

        return v - (2.0f * w - 1.0f);
    }
}
