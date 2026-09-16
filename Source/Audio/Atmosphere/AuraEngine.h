#pragma once

#include "../DspCommon.h"
#include "../EngineContext.h"

namespace nacar
{
    /**
        AURA  -  the environment.  Specification section 92.

        SPACE IS THE ROOM THE SOUND IS IN.  AURA IS THE ATMOSPHERE THE ROOM
        IS IN.

        That sentence is the whole design.  Space and Aura overlap on paper -
        both are reverberant, both have SIZE, DISTANCE, FOG, DECAY and LIGHT -
        and the only thing that stops them being the same engine twice is a
        decision about what each one is for.

        Space is a reverb: it lives in the FX chain wherever the user has put
        it, it has a MIX, it is built out of reflections, and its early field
        is half of its character.  Aura is not a reverb and has no mix: it sits
        after the entire chain, it is always in the signal path, its amount is
        DISTANCE, and it is built out of smear rather than reflection.  Where
        Space has a tail, Aura has weather.

        Concretely, and these are the differences in the code rather than
        adjectives:

                              SPACE                    AURA
          position            in the chain             after everything
          amount              MIX                      DISTANCE
          early reflections   8 taps per channel        none at all
          diffusion           4 short static allpasses  3 long modulated ones
          network             8 lines, 23-60 ms         4 lines, 97-229 ms
          modulation          0.03-0.17 ms              0.35-2.15 ms
          decay               0.1-30 s, a parameter     1.2-14 s, a proportion
          granular layer      none                      4 overlapping grains
          low band            high-passed at 72 Hz      high-passed at 130 Hz

        The specification's own hint - "granular tail" - is what tips Aura
        away from being a second reverb.  It is the engine that is allowed to
        be less literal, so the tail is read back by a small cloud of
        overlapping windowed grains as well as directly, and FOG decides how
        much of what you hear is the grains.

        The full rationale, the delay lengths, the stability bounds and the
        macro response are at the top of AuraEngine.cpp.
    */
    class AuraEngine
    {
    public:
        AuraEngine();
        ~AuraEngine();

        void prepare (const EngineSpec&);
        void reset();
        void process (juce::AudioBuffer<float>&, const ParameterRegistry&, const MacroState&);

    private:
        static constexpr int kLines  = 4;   ///< delay lines in the network
        static constexpr int kSmear  = 3;   ///< modulated allpasses per channel
        static constexpr int kGrains = 4;   ///< overlapping grains in the tail

        /** See the identical comment in SpaceEngine.h: block-rate one-pole
            into a per-sample ramp, so nothing steps at a block boundary. */
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

        /** One grain of the tail cloud.  Everything it needs is here, so a
            grain can be restarted inside the audio loop without touching
            anything that allocates. */
        struct Grain
        {
            float position  = 0.0f;   ///< absolute index into the tail history
            float increment = 1.0f;   ///< playback rate, within +-0.15 %
            float age       = 0.0f;   ///< samples since it started
            float length    = 1.0f;   ///< samples it will live for
            float invLength = 1.0f;   ///< 1 / length, so the window costs no divide
            float gainL     = 0.7f;
            float gainR     = 0.7f;
        };

        void restartGrain (Grain&) noexcept;
        void flushNetwork() noexcept;

        double sampleRate  = 48000.0;
        float  msToSamples = 48.0f;
        bool   prepared    = false;
        bool   running     = false;

        // -- the network ------------------------------------------------------
        fx::DelayLine  lines   [kLines];
        fx::OnePoleTPT damping [kLines];
        fx::OnePoleTPT loopCut [kLines];
        float          lineBase[kLines] {};
        float          modPhase[kLines] {};
        float          modInc  [kLines] {};

        // -- input path --------------------------------------------------------
        fx::DelayLine  preLine   [2];
        fx::Allpass    smear     [2][kSmear];
        float          smearBase [2][kSmear] {};
        float          smearPhase[2][kSmear] {};
        float          smearInc  [2][kSmear] {};

        fx::OnePoleTPT airLp  [2];
        fx::OnePoleTPT feedCut[2];

        // -- the granular tail -------------------------------------------------
        fx::HistoryBuffer tailHistory;
        Grain             grains[kGrains];
        fx::Rng           rng;

        float grainLength    = 4800.0f;   ///< samples, updated per block
        float grainOffsetMin = 2000.0f;
        float grainOffsetMax = 20000.0f;
        float historySize    = 1.0f;

        // -- output path --------------------------------------------------------
        fx::ThreeBand wetSplit[2];
        fx::Tilt      tilt    [2];

        // -- smoothed block values ----------------------------------------------
        Smoothed sScale, sSmearScale, sPre, sDry, sWet, sDirect, sAir;
        Smoothed sLate, sGrain, sWidth, sWidthHigh;
        Smoothed sGain[kLines];

        float tiltAmount = 0.0f;
        float modDepth   = 0.0f;
        float smearDepth = 0.0f;
        float duckDepth  = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuraEngine)
    };
}
