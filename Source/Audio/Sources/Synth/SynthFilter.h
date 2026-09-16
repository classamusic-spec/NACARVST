#pragma once

#include "SynthCommon.h"

namespace nacar::synth
{
    /** Matches the MASS|HAZE|COMB|FORMANT choice. */
    enum class FilterModel { mass = 0, haze, comb, formant };

    /** Matches LP|HP|BP|NOTCH (and, on the creative filter, COMB|FORMANT). */
    enum class FilterType { lowpass = 0, highpass, bandpass, notch };

    // =======================================================================
    //  LADDER  -  the MASS filter
    //
    //  Four one-pole TPT sections inside a single zero-delay feedback loop,
    //  with a saturator in the feedback path.
    //
    //  Zero-delay matters here more than anywhere else in the engine.  A naive
    //  ladder inserts one sample of delay into the feedback loop, which is a
    //  frequency-dependent phase error; at high cutoffs it detunes the
    //  resonance, and at high resonance it makes the filter's self-oscillation
    //  sharp instead of round.  Solving the loop algebraically costs one
    //  division and removes the error entirely.
    //
    //  The nonlinearity is in the feedback, not on the output.  That is what
    //  makes drive interact with resonance the way the specification asks for:
    //  driving the input of a linear filter only makes it louder, but driving
    //  its feedback path compresses the resonant peak as it gets louder, which
    //  is why a hard-driven ladder gets fatter rather than shriller.
    // =======================================================================
    class LadderFilter
    {
    public:
        void prepare (double sampleRate) noexcept;
        void reset() noexcept;

        void setCutoff (float hz) noexcept;
        void setResonance (float normalised) noexcept;   ///< 0..1
        void setDrive (float normalised) noexcept;       ///< 0..1

        float process (float x) noexcept;

    private:
        double sr = 48000.0;
        float g = 0.1f, G = 0.1f;
        float k = 0.0f;               ///< feedback depth, 0..4
        float drive = 1.0f, makeup = 1.0f;
        float s[4] {};
    };

    // =======================================================================
    //  STATE VARIABLE  -  the HAZE filter
    //
    //  Topology-preserving transform SVF.  All four responses fall out of the
    //  same two integrators, which is why morphing between them is continuous
    //  and why the notch is genuinely a notch rather than a subtraction of two
    //  slightly mistimed filters.
    // =======================================================================
    class StateVariableFilter
    {
    public:
        void prepare (double sampleRate) noexcept;
        void reset() noexcept;

        void setCutoff (float hz) noexcept;
        void setResonance (float normalised) noexcept;
        void setDrive (float normalised) noexcept;

        float process (float x, FilterType type) noexcept;

    private:
        double sr = 48000.0;
        float g = 0.1f, k = 1.4f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float ic1 = 0.0f, ic2 = 0.0f;
        float drive = 1.0f;
    };

    // =======================================================================
    //  CREATIVE  -  comb and formant, for MIRAGE
    // =======================================================================
    class CreativeFilter
    {
    public:
        void prepare (double sampleRate) noexcept;
        void reset() noexcept;

        /** Comb: cutoff sets the delay's pitch, resonance sets the feedback. */
        float processComb (float x, float hz, float resonance) noexcept;

        /** Formant: three resonant peaks, morphed across a vowel sequence. */
        float processFormant (float x, float hz, float resonance, float morph) noexcept;

    private:
        static constexpr int kCombSize = 4096;

        double sr = 48000.0;
        std::array<float, (size_t) kCombSize> comb {};
        int combWrite = 0;
        float combState = 0.0f;

        struct Biquad
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
            float z1 = 0.0f, z2 = 0.0f;

            void setBandpass (float hz, float q, double sampleRate) noexcept;
            void reset() noexcept { z1 = z2 = 0.0f; }

            forcedinline float process (float x) noexcept
            {
                const float y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                z1 = flush (z1);
                z2 = flush (z2);
                return y;
            }
        };

        Biquad formants[3];
    };

    // =======================================================================
    //  SYNTH FILTER
    //
    //  The facade a voice actually talks to.  Owns all four models, routes to
    //  the selected one, and - the part that matters most - guarantees that
    //  whatever comes out is finite.
    //
    //  Specification section 28 makes stability a hard requirement at every
    //  sample rate, under high drive, rapid modulation and extreme cutoffs.
    //  Two things enforce it: the cutoff is clamped to 0.45 of Nyquist before
    //  it ever reaches a coefficient, and every sample is checked on the way
    //  out.  A filter that has blown up resets its own state and emits silence
    //  for one sample instead of poisoning the mix for the rest of the session.
    // =======================================================================
    class SynthFilter
    {
    public:
        void prepare (double sampleRate) noexcept;
        void reset() noexcept;

        void setModel (FilterModel) noexcept;
        void setType (FilterType) noexcept;

        /** Cutoff in Hz, clamped internally to [20, 0.45 * fs]. */
        void setCutoff (float hz) noexcept;
        void setResonance (float normalised) noexcept;
        void setDrive (float normalised) noexcept;
        void setMorph (float normalised) noexcept;

        float process (float x) noexcept;

        float getCutoff() const noexcept { return cutoffHz; }

    private:
        /** Recomputes only the selected model's coefficients. */
        void updateActiveModel() noexcept;

        double sr = 48000.0;
        FilterModel model = FilterModel::haze;
        FilterType type = FilterType::lowpass;
        bool dirty = true;

        float cutoffHz = 8000.0f, resonance = 0.0f, driveNorm = 0.0f, morph = 0.0f;

        LadderFilter ladder;
        StateVariableFilter svf;
        CreativeFilter creative;
    };
}
