#pragma once

#include "../DspCommon.h"
#include "../EngineContext.h"

namespace nacar
{
    /**
        CRUSH - bit and rate reduction.  Specification section 86.

        Low settings are a texture; high settings are destruction that is still
        meant to be usable in a record.  The word the specification uses is
        "controlled", and every decision in CrushEngine.cpp is made against it:
        the quantiser is dithered and noise-shaped rather than bare, the drive
        is gain-compensated so nothing gets louder, and the one stage that is
        allowed to alias - the rate reducer - is the one stage where aliasing
        *is* the effect.

        The algorithm, the parameter mapping, the macro response and the known
        limitations are documented at the top of CrushEngine.cpp.
    */
    class CrushEngine
    {
    public:
        CrushEngine();
        ~CrushEngine();

        void prepare (const EngineSpec&);
        void reset();
        void process (juce::AudioBuffer<float>&, const ParameterRegistry&, const MacroState&);

        /** Samples of latency the module adds while it is active: the round
            trip through the oversampling halfband, which the dry path is
            delayed to match.  Zero while bypassed. */
        int getLatencySamples() const noexcept;

    private:
        /** Block-rate exponential smoothing, interpolated linearly inside the
            block - the same shape as the Smoother in SynthEngine.cpp, kept
            local for the reason given in RetroEngine.h. */
        struct Smoothed
        {
            fx::Ramp ramp;
            float current = 0.0f;
            bool  primed = false;

            void set (float target, int numSamples, float coef) noexcept
            {
                if (! primed)
                {
                    current = target;
                    primed = true;
                }

                float next = target + coef * (current - target);

                if (std::abs (next - target) < 1.0e-6f)
                    next = target;

                ramp.set (current, next, numSamples);
                current = next;
            }

            forcedinline float at (int i) const noexcept { return ramp.at (i); }

            void holdAt (float v) noexcept { current = v; primed = true; ramp.snap (v); }
            void reset() noexcept          { current = 0.0f; primed = false; ramp.snap (0.0f); }
        };

        struct Channel
        {
            fx::VoiceUpsampler   up;      ///< 2x, around the drive shaper only
            fx::VoiceDownsampler down;
            fx::DelayLine        dry;     ///< compensates the halfband's latency
            fx::Tilt             tilt;

            float error = 0.0f;           ///< first-order quantiser error feedback
            float held  = 0.0f;           ///< the sample-and-hold's current value

            void prepare (double sampleRate);
            void reset() noexcept;
        };

        double sr = 48000.0;

        Channel channels[2];

        // One converter has one clock and one dither generator, so both of
        // these are shared by the two channels.  Independent clocks would
        // decorrelate the channels - including the low band, which
        // specification 38, 40 and 43 forbid.
        float holdPhase  = 0.0f;
        float holdPeriod = 1.0f;

        fx::Rng rng;

        Smoothed crushSm, bitsSm, rateSm, jitterSm, driveSm, toneSm, mixSm;

        fx::Ramp driveRamp;    ///< the shaper's gain
        fx::Ramp blendRamp;    ///< how much of the shaped signal is used
        fx::Ramp makeupRamp;   ///< exactly the reciprocal of the small-signal gain
        fx::Ramp dryRamp, wetRamp;

        float quantStep   = 0.0f;   ///< block constant: 2 / 2^bits
        float ditherScale = 0.0f;
        float basePeriod  = 1.0f;   ///< samples between takes, before jitter
        float jitterDepth = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrushEngine)
    };
}
