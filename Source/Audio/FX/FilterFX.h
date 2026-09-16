#pragma once

#include "../DspCommon.h"
#include "../EngineContext.h"
#include "../Sources/Synth/SynthFilter.h"

namespace nacar
{
    /**
        FILTER - the chain filter.  Specification section 87.

        It does not contain a filter.  It contains four instances of
        nacar::synth::SynthFilter, which is the same filter family the synth
        voice uses: the zero-delay ladder, the TPT state variable, the comb and
        the formant bank, with the stability guards already in them.  The
        instrument has one filter character, and this is it.

        What this engine adds on top is the three things the chain needs and a
        voice does not: a continuous MORPH between adjacent responses, a MOTION
        source that is alive rather than an LFO, and a stereo pair that runs two
        filters with identical coefficients rather than one filter on the mono
        sum.

        The algorithm, the parameter mapping, the macro response and the known
        limitations are documented at the top of FilterFX.cpp.
    */
    class FilterFX
    {
    public:
        FilterFX();
        ~FilterFX();

        void prepare (const EngineSpec&);
        void reset();
        void process (juce::AudioBuffer<float>&, const ParameterRegistry&, const MacroState&);

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
            void reset (float v) noexcept  { current = v; primed = false; ramp.snap (v); }
        };

        /** One of the six responses the mode selector offers, expressed in the
            synth filter's own vocabulary. */
        struct Response
        {
            synth::FilterModel model;
            synth::FilterType  type;
        };

        static Response responseFor (int mode) noexcept;

        static constexpr int kNumModes = 6;

        double sr = 48000.0;

        // [channel][0 = the selected response, 1 = the next one along]
        synth::SynthFilter filters[2][2];

        // -- MOTION ----------------------------------------------------------
        //  All of these are shared by the two channels on purpose: the two
        //  filters must always see identical coefficients, or the modulation
        //  itself would decorrelate the channels.
        float slowPhase1 = 0.0f, slowPhase2 = 0.0f;
        fx::OnePole motionWalk;
        float motionTarget = 0.0f;
        fx::OnePole selfEnv;        ///< the filter listening to its own output
        float lastOutMono = 0.0f;
        fx::Rng rng;
        int controlCounter = 0;

        Smoothed cutoffSm;          ///< log2 (Hz), so a sweep is musical
        Smoothed resSm, driveSm, morphSm, motionSm, mixSm;

        fx::Ramp gainARamp, gainBRamp;
        fx::Ramp dryRamp, wetRamp;

        int  modeA = 0, modeB = 1;
        bool responsesDirty = true;   ///< forces the first block to set them

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterFX)
    };
}
