#pragma once

#include "../DspCommon.h"
#include "../EngineContext.h"

namespace nacar
{
    /**
        SHADOW  -  an atmospheric duplicate behind the original signal.
        Specification section 93.

        The specification's own line - "not simply chorus, delay or reverb" -
        is the entire brief, and it is a brief about behaviour rather than
        about topology.  A duplicate that arrives later, sits further away, is
        smeared, is transposed and is quieter can be built out of a delay, a
        pitch shifter and some allpasses; whether it reads as an effect on the
        sound or as something following the sound around is decided by four
        decisions, and those are the ones worth naming here:

          - It is derived from the mono sum, then re-widened by giving the two
            channels different diffusion.  A shadow is never wider than the
            thing casting it, and it is never a point either.
          - It is filtered to sit behind: high-passed so it does not compete
            for the low end, low-passed by DISTANCE, narrowed by DISTANCE.  It
            is not allowed to be as present as the original at any setting.
          - BLUR smears it in time, not just in frequency: four modulated
            allpasses followed by a cross-coupled feedback tank, so at high
            blur a single note becomes several hundred milliseconds of
            unresolvable texture.
          - Its envelope lags the original's.  A duplicate that tracks the
            source's dynamics exactly is heard as part of the source; one
            whose attacks are softened and whose decays linger is heard as a
            separate object.  This is the one that does the most work and it
            is four lines of code.

        It is additive.  The dry path is never touched, at any setting, so the
        original signal leaves this engine bit-exact and LEVEL at zero is a
        true bypass rather than a very quiet effect.

        The pitch shifter, the window length, the stability bounds and the
        macro response are documented at the top of ShadowEngine.cpp.
    */
    class ShadowEngine
    {
    public:
        ShadowEngine();
        ~ShadowEngine();

        void prepare (const EngineSpec&);
        void reset();
        void process (juce::AudioBuffer<float>&, const ParameterRegistry&, const MacroState&);

    private:
        static constexpr int kBlur = 4;   ///< diffusion allpasses per channel

        /** See the identical comment in SpaceEngine.h. */
        struct Smoothed
        {
            float    value = 0.0f;
            fx::Ramp ramp;

            forcedinline float at (int i) const noexcept { return ramp.at (i); }

            void snap (float v) noexcept { value = v; ramp.snap (v); }

            void set (float target, float coeff, int numSamples, bool immediate) noexcept
            {
                if (immediate)
                {
                    snap (target);
                    return;
                }

                const float next = value + (target - value) * coeff;
                ramp.set (value, next, numSamples);
                value = next;
            }
        };

        void flushState() noexcept;

        double sampleRate  = 48000.0;
        float  msToSamples = 48.0f;
        bool   prepared    = false;
        bool   running     = false;

        // -- the duplicate ------------------------------------------------------
        fx::DelayLine sourceLine;          ///< mono, read by the pitch shifter
        float         shiftPhase = 0.0f;
        float         shiftInc   = 0.0f;

        // -- its envelope, which lags the original's ------------------------------
        float envFast = 0.0f, envSlow = 0.0f;
        float attackFast = 0.5f, releaseFast = 0.02f;
        float attackSlow = 0.02f, releaseSlow = 0.004f;
        float lagDepth = 0.5f;

        // -- blur ------------------------------------------------------------------
        fx::Allpass    blur     [2][kBlur];
        float          blurBase [2][kBlur] {};
        float          blurPhase[2][kBlur] {};
        float          blurInc  [2][kBlur] {};
        float          blurDepth = 0.0f;

        fx::Allpass    tank      [2];
        fx::DelayLine  smearLine [2];
        fx::OnePoleTPT smearLp   [2];
        float          smearDelay[2] {};

        // -- sitting behind ---------------------------------------------------------
        fx::OnePoleTPT toneLp[2], toneHp[2];
        fx::ThreeBand  split [2];

        // -- smoothed block values ----------------------------------------------------
        Smoothed sLength, sWindow, sBlend, sBlurScale, sSmear, sFeedback;
        Smoothed sLevel, sWidth, sWidthHigh;

        bool  shifting  = false;
        float duckDepth = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShadowEngine)
    };
}
