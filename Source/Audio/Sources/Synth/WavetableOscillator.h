#pragma once

#include "Oscillator.h"
#include "WavetableBank.h"

namespace nacar::synth
{
    /** Everything an oscillator needs in order to read the shared bank.  Held
        by value in the voice and refreshed once per block. */
    struct WavetableContext
    {
        const WavetableBank* bank = nullptr;
        int   family              = 0;
        float framePosition       = 0.0f;   ///< 0..1 across the family's frames
        bool  hermitePhase        = true;   ///< false in ECO
    };

    /**
        Reads the shared bank.  Holds no state of its own - the phase belongs to
        the oscillator that owns it - so a voice can switch an oscillator
        between an analogue waveform and a table without anything to keep in
        sync.

        Three interpolations happen on every read, and all three matter:

          * within a frame, on phase, cubic Hermite (linear in ECO).  Linear
            interpolation of a 2048-point table is a first-order low pass whose
            error rises with frequency, which reads as a dull, slightly gritty
            top octave.
          * between the two frames either side of Position, linear.  Anything
            higher order would overshoot between frames that were never meant to
            be neighbours.
          * between the two mip levels either side of the note's pitch, linear.
            Without this a rising glissando steps from one bandwidth to the next
            and you hear a click as the top harmonics vanish.
    */
    struct WavetableOscillator
    {
        static float read (const WavetableContext& ctx, float phase01, float levelF) noexcept
        {
            if (ctx.bank == nullptr)
                return 0.0f;

            const auto& bank = *ctx.bank;

            const float fp = juce::jlimit (0.0f, 1.0f, ctx.framePosition)
                                * (float) (WavetableBank::kNumFrames - 1);
            const int   f0 = (int) fp;
            const int   f1 = juce::jmin (WavetableBank::kNumFrames - 1, f0 + 1);
            const float ff = fp - (float) f0;

            const float lvl = juce::jlimit (0.0f, (float) (WavetableBank::kNumLevels - 1), levelF);
            const int   l0  = (int) lvl;
            const int   l1  = juce::jmin (WavetableBank::kNumLevels - 1, l0 + 1);
            const float lf  = lvl - (float) l0;

            const float a = readLevel (bank, ctx, l0, f0, f1, ff, phase01);

            if (lf <= 0.0001f || l1 == l0)
                return a;

            const float b = readLevel (bank, ctx, l1, f0, f1, ff, phase01);
            return lerp (a, b, lf);
        }

    private:
        static float readLevel (const WavetableBank& bank, const WavetableContext& ctx,
                                int level, int f0, int f1, float ff, float phase01) noexcept
        {
            const int   length = bank.lengthOf (level);
            const int   mask   = length - 1;
            const float x      = phase01 * (float) length;
            const int   i0     = (int) x;
            const float frac   = x - (float) i0;

            const float* t0 = bank.frame (ctx.family, f0, level);
            const float* t1 = bank.frame (ctx.family, f1, level);

            // Power-of-two lengths, so wrapping is a mask and a negative index
            // wraps correctly under two's complement.
            const int im1 = (i0 - 1) & mask;
            const int ia  =  i0      & mask;
            const int ip1 = (i0 + 1) & mask;
            const int ip2 = (i0 + 2) & mask;

            if (ctx.hermitePhase)
            {
                const float a = hermite (frac, t0[im1], t0[ia], t0[ip1], t0[ip2]);
                const float b = hermite (frac, t1[im1], t1[ia], t1[ip1], t1[ip2]);
                return lerp (a, b, ff);
            }

            const float a = lerp (t0[ia], t0[ip1], frac);
            const float b = lerp (t1[ia], t1[ip1], frac);
            return lerp (a, b, ff);
        }
    };
}
