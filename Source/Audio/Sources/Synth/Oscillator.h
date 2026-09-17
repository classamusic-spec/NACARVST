#pragma once

#include "SynthCommon.h"

#include <juce_dsp/juce_dsp.h>

#include <bit>
#include <limits>

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

    // -----------------------------------------------------------------------
    //  THE SAME OSCILLATOR, A VECTOR AT A TIME
    //
    //  Unison is the one place in this engine where the same arithmetic is
    //  performed on several numbers that have nothing to do with each other:
    //  eight detuned copies of one oscillator share a waveform, a pulse width,
    //  a phase-modulation term and a sample rate, and differ only in their
    //  phase and their increment.  That is the shape SIMD exists for.
    //
    //  Everything below is written against juce::dsp::SIMDRegister rather than
    //  intrinsics, because this has to keep compiling for NEON and for the
    //  scalar fallback, neither of which anyone here can test.
    //
    //  The rule these helpers are written to is that the vector path must
    //  produce the SAME BITS as the scalar one, lane for lane.  That means
    //  every helper performs the scalar routine's operations in the scalar
    //  routine's order, replaces its branches with selects rather than with
    //  a cheaper formula, and reproduces its behaviour for infinities and
    //  NaNs as well as for ordinary numbers - the oscillator's own defence
    //  against a NaN arriving through phase modulation depends on one.
    // -----------------------------------------------------------------------
    using Vec     = juce::dsp::SIMDRegister<float>;
    using VecMask = Vec::vMaskType;

    inline constexpr int kVecWidth = (int) Vec::SIMDNumElements;

    /** kMaxUnison rounded up to a whole number of vectors, so a group of five
        can be rendered as vectors without reading past the end of anything. */
    inline constexpr int kUnisonPadded = ((kMaxUnison + kVecWidth - 1) / kVecWidth) * kVecWidth;

    /** Reinterprets a float vector as the mask vector with the same bits.

        SIMDRegister will bit-and a float vector with a mask and bit-or a float
        vector with a mask, but it will not bit-or two float vectors, which is
        the one operation a branch-free select needs.  This supplies the missing
        step without reaching for an intrinsic: std::bit_cast between two
        trivially copyable types of the same size is ordinary C++20 and says
        exactly what is meant. */
    forcedinline VecMask vecBitsAsMask (Vec v) noexcept
    {
        static_assert (sizeof (Vec::vSIMDType) == sizeof (VecMask::vSIMDType),
                       "a float vector and its mask vector must be the same width");

        return VecMask::fromNative (std::bit_cast<VecMask::vSIMDType> (v.value));
    }

    /** Branch-free `mask ? a : b`, bit by bit.

        Bitwise rather than arithmetic on purpose: selecting by adding the two
        masked halves would turn a selected -0.0 into +0.0, and the point of
        this whole file is that the vector path returns the scalar path's own
        bits. */
    forcedinline Vec vselect (VecMask mask, Vec a, Vec b) noexcept
    {
        return (b & ~mask) | vecBitsAsMask (a & mask);
    }

    /** std::floor(), lane by lane.

        SIMDRegister offers truncation towards zero but not rounding towards
        minus infinity, so the negative case costs one compare and one
        subtraction.  Exact wherever truncation is - |x| < 2^31 - which covers
        every phase and every phase-modulation term the engine produces.

        A NaN truncates to INT_MIN and comes back as a large negative float,
        but the subtraction that follows turns it straight back into a NaN, so
        the oscillator's stability check still sees it. */
    forcedinline Vec vfloor (Vec x) noexcept
    {
        const Vec t = Vec::truncate (x);
        return t - (Vec::expand (1.0f) & Vec::greaterThan (t, x));
    }

    /** The vector form of flush(): zero below 1e-20, and - like the scalar
        one - leave a NaN alone so that the check downstream can catch it. */
    forcedinline Vec vflush (Vec x) noexcept
    {
        return x & ~Vec::lessThan (Vec::abs (x), Vec::expand (1.0e-20f));
    }

    /** The vector form of sane(): set where the lane is finite. */
    forcedinline VecMask vsane (Vec x) noexcept
    {
        return Vec::lessThan (Vec::abs (x), Vec::expand (std::numeric_limits<float>::infinity()));
    }

    /** sin(2*pi*t) for four or eight turns at once.  Same fold, same 9th-order
        odd series, same order of evaluation as sineTurns(). */
    forcedinline Vec vSineTurns (Vec t) noexcept
    {
        t -= vfloor (t);

        const Vec half = Vec::expand (0.5f);
        const Vec quarter = Vec::expand (0.25f);

        Vec x = vselect (Vec::greaterThan (t, half), t - Vec::expand (1.0f), t);

        // The two folds are mutually exclusive over [-0.5, 0.5], so applying
        // them one after the other is the scalar if / else-if.
        x = vselect (Vec::greaterThan (x, quarter), half - x, x);
        x = vselect (Vec::lessThan (x, Vec::expand (-0.25f)), Vec::expand (-0.5f) - x, x);

        const Vec a  = x * Vec::expand (kTwoPi);
        const Vec a2 = a * a;

        return a * (Vec::expand (1.0f) + a2 * (Vec::expand (-1.0f / 6.0f)
                  + a2 * (Vec::expand ( 1.0f / 120.0f)
                  + a2 * (Vec::expand (-1.0f / 5040.0f)
                  + a2 *  Vec::expand ( 1.0f / 362880.0f)))));
    }

    /** PolyBLEP for a whole vector.

        Two things make this the one helper that is not straight-line vector
        code.  SIMDRegister has no divide - NEON has no divide instruction, so
        JUCE does not offer one - and the residual's `t / dt` has to be exactly
        the division the scalar routine performs, not a multiplication by a
        reciprocal, or the two paths stop agreeing bit for bit.

        Both have the same answer.  The branch is only taken within one phase
        increment of a discontinuity, which at any playable pitch is a percent
        or two of samples; so the vector code asks whether ANY lane is near one
        at all, and when none is - the overwhelmingly common case - returns a
        zero vector without dividing anything.  When one is, it hands that
        vector to the scalar routine, unchanged, and gets the scalar routine's
        own bits back.

        Everything here is stack-resident and branch-uniform: no allocation, no
        lock, nothing that §146 forbids. */
    forcedinline Vec vPolyBlep (Vec t, Vec dt) noexcept
    {
        const VecMask nearEdge = Vec::lessThan (t, dt)
                               | Vec::greaterThan (t, Vec::expand (1.0f) - dt);

        if (nearEdge == (juce::uint32) 0)
            return Vec::expand (0.0f);

        alignas (64) float ts  [(size_t) kVecWidth] {};
        alignas (64) float ds  [(size_t) kVecWidth] {};
        alignas (64) float out [(size_t) kVecWidth] {};

        t.copyToRawArray (ts);
        dt.copyToRawArray (ds);

        for (int i = 0; i < kVecWidth; ++i)
            out[(size_t) i] = polyBlep (ts[(size_t) i], ds[(size_t) i]);

        return Vec::fromRawArray (out);
    }

    /** Naive-plus-BLEP sawtooth, a vector at a time. */
    forcedinline Vec vBlepSaw (Vec phase, Vec inc) noexcept
    {
        return (Vec::expand (2.0f) * phase - Vec::expand (1.0f)) - vPolyBlep (phase, inc);
    }

    /** Naive-plus-BLEP pulse with DC removed, a vector at a time.

        The width is a scalar because it is: one pulse width serves the whole
        unison group, so the clamp and the DC term are computed once, exactly
        as the scalar routine computes them. */
    forcedinline Vec vBlepPulse (Vec phase, Vec inc, float width) noexcept
    {
        const float w = juce::jlimit (0.02f, 0.98f, width);
        const Vec   wv = Vec::expand (w);

        Vec v = vselect (Vec::lessThan (phase, wv), Vec::expand (1.0f), Vec::expand (-1.0f));

        v += vPolyBlep (phase, inc);

        Vec t2 = (phase + Vec::expand (1.0f)) - wv;
        t2 -= vfloor (t2);

        v -= vPolyBlep (t2, inc);

        return v - Vec::expand (2.0f * w - 1.0f);
    }
}
