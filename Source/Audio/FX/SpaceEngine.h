#pragma once

#include "../DspCommon.h"
#include "../EngineContext.h"

namespace nacar
{
    /**
        SPACE  -  the production reverb.  Specification section 90.

        The brief for this engine is unusually blunt: Space has to be good
        enough that somebody reaches for it instead of the reverb they already
        own.  That rules out the two cheap answers - a short algorithmic
        utility, and a wash that only works on pads - and it rules out the
        usual failure of an instrument's built-in reverb, which is that its
        "distance" control is a wet-level control wearing a different name.

        WHAT IT IS

        A feedback delay network.  A stereo pre-delay feeds a tapped early
        reflection pattern and, in parallel, a chain of series allpass
        diffusers; the diffusers feed eight delay lines mixed by an 8x8
        Hadamard matrix, with damping and a subsonic cut inside the feedback
        path.  Early and late are balanced against each other by DISTANCE, not
        by MIX.

        The five characters are five different configurations of that network -
        different diffusion, different damping, different modulation, different
        early/late balance, different size multiplier and different decay
        scaling - rather than one reverb with a filter across it.

        WHAT IT IS NOT

        It is not the atmosphere layer.  Space is the room the sound is in:
        it sits in the FX chain where the user puts it, it has a MIX control,
        and it is built out of reflections.  Aura is the atmosphere that room
        sits in, is always on, has no mix of its own and is built out of
        smear.  The two are deliberately different engines and the difference
        is written down at the top of AuraEngine.cpp.

        Everything about the topology, the delay lengths, the damping
        placement, the stability bounds and the macro response is documented at
        the top of SpaceEngine.cpp.
    */
    class SpaceEngine
    {
    public:
        SpaceEngine();
        ~SpaceEngine();

        void prepare (const EngineSpec&);
        void reset();
        void process (juce::AudioBuffer<float>&, const ParameterRegistry&, const MacroState&);

        /** The five characters, in the order of the space_char choice list. */
        enum class Character { room = 0, chamber, dark, distant, infinite };

        /**
            One character's configuration of the network.

            Public only so the table itself can live in the .cpp next to the
            commentary that explains each row.  Nothing outside the engine
            reads it.
        */
        struct Config
        {
            float sizeMult;      ///< multiplies the delay-length scale
            float diffusion;     ///< base input allpass coefficient
            int   diffusers;     ///< how many of the four series allpasses run
            float dampHz;        ///< damping cutoff inside the feedback path
            float lowCutHz;      ///< subsonic cut inside the feedback path
            float modMs;         ///< delay modulation depth
            float modRate;       ///< multiplies the per-line modulation rates
            float earlyLevel;    ///< early reflection level
            float earlySpread;   ///< multiplies the early tap times
            float lateLevel;     ///< tail level
            float decayMult;     ///< multiplies the DECAY parameter
            float width;         ///< base stereo width of the tail
            bool  infinite;      ///< feedback held at unity, DECAY ignored
        };

    private:
        static constexpr int kLines     = 8;   ///< delay lines in the network
        static constexpr int kDiffusers = 4;   ///< series allpasses per channel
        static constexpr int kTaps      = 8;   ///< early reflection taps per channel

        /**
            A block-rate one-pole smoother that hands the audio loop a
            per-sample ramp.

            Reading a parameter once per block and using it directly makes a
            reverb whose size and gains step at block boundaries, which clicks.
            Two stages fix it: the target is approached with a time constant so
            a knob flick becomes a glide, and the move within the block is a
            linear ramp so there is no step at the boundary either.

            (This is the one thing in these three engines that arguably belongs
            in DspCommon.h.  It is kept private here because that header is
            being edited by other engines in parallel.)
        */
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

        /** Zeroes every piece of state that can hold energy.  memset only:
            no allocation, so it is safe from the audio thread. */
        void flushNetwork() noexcept;

        double sampleRate  = 48000.0;
        float  msToSamples = 48.0f;
        bool   prepared    = false;
        bool   running     = false;   ///< was the engine audible last block

        // -- the network ----------------------------------------------------
        fx::DelayLine  lines   [kLines];
        fx::OnePoleTPT damping [kLines];
        fx::OnePoleTPT loopCut [kLines];
        float          lineBase[kLines] {};   ///< samples at scale 1
        float          modPhase[kLines] {};
        float          modInc  [kLines] {};

        // -- input path ------------------------------------------------------
        fx::DelayLine  preLine     [2];
        fx::Allpass    diffuser    [2][kDiffusers];
        float          diffuserBase[2][kDiffusers] {};
        float          tapBase     [2][kTaps] {};
        float          earlyNorm = 1.0f;

        fx::OnePoleTPT airLp  [2];    ///< air absorption on the direct path
        fx::OnePoleTPT feedCut[2];    ///< keeps the low band out of the tail

        // -- output path -----------------------------------------------------
        fx::ThreeBand  wetSplit[2];
        fx::Tilt       tilt    [2];

        // -- smoothed block values -------------------------------------------
        Smoothed sScale, sDiffScale, sEarlyScale, sPre;
        Smoothed sEarly, sLate, sDry, sWet, sDirect, sAir;
        Smoothed sWidth, sWidthHigh;
        Smoothed sGain[kLines];

        int   activeDiffusers = 4;
        float tiltAmount = 0.0f;
        float modDepth   = 0.0f;
        float injection  = 0.45f;
        float duckDepth  = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceEngine)
    };
}
