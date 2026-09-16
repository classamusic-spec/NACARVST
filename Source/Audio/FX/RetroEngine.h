#pragma once

#include "../DspCommon.h"
#include "../EngineContext.h"

namespace nacar
{
    /**
        RETRO - the playback machine.

        Specification section 85.  Section 95 draws the line this engine has to
        respect: MEMORY is historical identity, PATINA is surface texture and
        age, and RETRO is *the medium and the transport the sound is playing
        back through*.  Everything below is therefore a property of a machine -
        its bandwidth, its speed stability, its noise floor, its saturation, its
        channel behaviour - and nothing below is a generational copy or a
        surface.

        ERA is the primary control.  It does not set an amount; it selects which
        machine, morphing continuously across four named characters.  The other
        six controls then shape whatever ERA selected.

        The algorithm, the parameter mapping, the macro response and the known
        limitations are documented at the top of RetroEngine.cpp.
    */
    class RetroEngine
    {
    public:
        RetroEngine();
        ~RetroEngine();

        void prepare (const EngineSpec&);
        void reset();
        void process (juce::AudioBuffer<float>&, const ParameterRegistry&, const MacroState&);

    private:
        // ===================================================================
        //  ERA
        //
        //  One playback medium, described as a set of measurable behaviours.
        //  The four entries of the table are in RetroEngine.cpp; ERA morphs
        //  continuously between adjacent ones, so every field below is
        //  interpolated rather than switched.
        // ===================================================================
        struct EraProfile
        {
            const char* name;

            float lowCutHz;        ///< the medium's low-frequency limit
            float bandwidthHz;     ///< its nominal high-frequency limit
            float hfLoss;          ///< how far bandwidth collapses with level and age

            float satAmount;       ///< how hard it saturates
            float satBias;         ///< asymmetry, which is what makes even harmonics
            float digitalness;     ///< 0 soft knee .. 1 converter-style hard clip

            float wowHz;           ///< slow speed error, 0.5 .. 6 Hz
            float wowDeviation;    ///< peak fractional pitch error from wow
            float flutterHz;       ///< fast speed error, 6 .. 30 Hz
            float flutterDeviation;///< peak fractional pitch error from flutter
            float scrapeAmount;    ///< irregular, noise-like transport error

            float noiseLowHz;      ///< colour of the medium's own noise
            float noiseHighHz;
            float noiseLevel;
            float cracklePerSecond;///< impulsive surface events at full AGE
            float rumbleLevel;     ///< common-mode LF, identical in both channels

            float monoAmount;      ///< how far the medium collapses mid/high to mono
            float azimuthAmount;   ///< channel-to-channel high-frequency instability

            float resampleHz;      ///< resampling grid, 0 = none
            float resampleBits;    ///< converter word length, 0 = none
            float outputTrim;      ///< keeps the four eras level-matched
        };

        /** Block-rate exponential smoothing, interpolated linearly inside the
            block - the same shape as the Smoother in SynthEngine.cpp.  It is
            repeated here rather than shared because DspCommon.h is being
            extended by other work in flight and this is parameter glue, not a
            DSP primitive. */
        struct Smoothed
        {
            fx::Ramp ramp;
            float current = 0.0f;
            bool  primed = false;

            void set (float target, int numSamples, float coef) noexcept
            {
                if (! primed)
                {
                    current = target;       // first block: jump, do not fade in
                    primed = true;
                }

                float next = target + coef * (current - target);

                if (std::abs (next - target) < 1.0e-6f)
                    next = target;          // settle exactly, so bypass can be exact

                ramp.set (current, next, numSamples);
                current = next;
            }

            forcedinline float at (int i) const noexcept { return ramp.at (i); }

            void holdAt (float v) noexcept { current = v; primed = true; ramp.snap (v); }
            void reset() noexcept          { current = 0.0f; primed = false; ramp.snap (0.0f); }
        };

        /** Everything that exists once per channel. */
        struct Channel
        {
            fx::DelayLine  line;        ///< the transport: wow, flutter and scrape live here
            fx::OnePoleTPT lowCut;      ///< the medium's low-frequency limit
            fx::OnePoleTPT bandLimit;   ///< its high-frequency limit, modulated by level
            fx::OnePole    levelEnv;    ///< fast: dynamic HF loss and transient rounding
            fx::OnePole    gapEnv;      ///< slow: drives the modulation-noise term
            fx::ThreeBand  bands;       ///< so channel behaviour never touches the low end
            fx::OnePoleTPT noiseHp1, noiseHp2, noiseLp;
            fx::OnePole    dropGain;    ///< smoothed, because a dropout must never click
            fx::OnePole    dropHf;
            fx::OnePole    azimuth;     ///< slow per-channel high-frequency wander
            fx::Tilt       tilt;
            fx::DcBlocker  dc;

            float dropTargetGain = 1.0f;
            float dropTargetHf   = 1.0f;
            int   dropRemaining  = 0;

            float azimuthTarget = 0.0f;
            float held = 0.0f;          ///< the resampling grid's held sample

            void prepare (double sampleRate, int maxDelaySamples);
            void reset() noexcept;
        };

        static EraProfile eraAt (float position) noexcept;

        double sr = 48000.0;

        Channel channels[2];

        // -- the transport, which is shared: one capstan, one tape -----------
        float wowPhase = 0.0f, wowPhase2 = 0.0f, flutterPhase = 0.0f;
        fx::OnePoleTPT scrapeLp1, scrapeLp2;
        fx::OnePole    wowRateWander;
        float wowRateTarget = 0.0f;

        fx::OnePoleTPT rumbleLp1, rumbleLp2;

        // -- the resampling grid, also shared: one converter, one clock ------
        float resamplePhase = 0.0f;

        fx::Rng rng;
        int controlCounter = 0;

        Smoothed eraSm, satSm, noiseSm, monoSm, trimSm, digitalSm, resampleSm,
                 rumbleSm, hfLossSm, mixSm, wowExcSm, flutterExcSm, scrapeExcSm,
                 crackleSm;

        fx::Ramp dryRamp, wetRamp;   ///< equal-power at both ends of the block

        int   nominalDelay = 192;   ///< the transport's centre tap, in samples
        float maxExcursion = 168.0f;///< the furthest the read head may wander
        float noiseRateNorm = 1.0f; ///< keeps every noise level equal at every rate

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RetroEngine)
    };
}
