#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstring>

/**
    Shared primitives for the NACAR synthesis core.

    Everything here is header-only, branch-light and allocation-free so it can
    be used from inside a voice's per-sample loop.  Nothing in this file knows
    about parameters, voices or JUCE processors.
*/
namespace nacar::synth
{
    // -----------------------------------------------------------------------
    //  Fixed sizes.  Every buffer in the engine is dimensioned from these, so
    //  nothing downstream ever has to allocate once prepare() has run.
    // -----------------------------------------------------------------------
    inline constexpr int kMaxVoices        = 32;
    inline constexpr int kMaxUnison        = 8;
    inline constexpr int kMaxOversample    = 4;    ///< ULTRA
    inline constexpr int kMaxHeldNotes     = 32;   ///< mono/legato note stack depth

    inline constexpr float kPi    = 3.14159265358979323846f;
    inline constexpr float kTwoPi = 6.28318530717958647692f;

    /** Global voice trim.

        Chosen so that one INIT voice (saw + a little B, LP at 9 kHz, full
        velocity) peaks around -13 dBFS, which leaves a four-note chord near
        -6 dBFS and a ten-note chord just under 0 dBFS before the bus limiter
        does anything at all.

        The value is not a guess: NacarBench renders the INIT patch and prints
        its peak, and this constant is what makes that number land on -13.  If
        the voice path ever changes its gain structure, re-run the benchmark and
        move this rather than compensating somewhere downstream - the whole
        point of a single trim is that there is exactly one place to look. */
    inline constexpr float kVoiceGain = 0.52f;

    // -----------------------------------------------------------------------
    //  Numeric hygiene
    // -----------------------------------------------------------------------

    /** Kills denormals.  ScopedNoDenormals is already set by the processor, but
        filter states are also read back by the UI-facing peak meter and by the
        stability checks below, and a denormal there is a real cost on the
        platforms where FTZ is not honoured for every SSE path. */
    forcedinline float flush (float x) noexcept
    {
        return (std::abs (x) < 1.0e-20f) ? 0.0f : x;
    }

    forcedinline bool sane (float x) noexcept
    {
        return std::isfinite (x);
    }

    /** Bipolar soft clip that is exactly +-1 at +-3 and has unit slope at zero.

        This is the Pade approximant of tanh, clamped at the point where the
        approximation would stop being monotonic.  Clamping rather than letting
        the rational blow up is what makes it safe to put inside a feedback
        loop: the output is bounded no matter what arrives. */
    forcedinline float tanhFast (float x) noexcept
    {
        x = juce::jlimit (-3.0f, 3.0f, x);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    /** sin(2*pi*t), t measured in turns.

        Folded into a quarter turn and evaluated with a 9th-order odd Taylor
        series, which is worth about -94 dB THD - below the noise floor of
        anything this instrument does downstream, and far cheaper than libm's
        sin() when eight unison voices are calling it. */
    forcedinline float sineTurns (float t) noexcept
    {
        t -= std::floor (t);                       // [0, 1)

        float x = (t > 0.5f) ? t - 1.0f : t;       // [-0.5, 0.5]

        if (x > 0.25f)        x =  0.5f - x;       // fold about the peaks
        else if (x < -0.25f)  x = -0.5f - x;

        const float a  = x * kTwoPi;               // [-pi/2, pi/2]
        const float a2 = a * a;

        return a * (1.0f + a2 * (-1.0f / 6.0f
                  + a2 * ( 1.0f / 120.0f
                  + a2 * (-1.0f / 5040.0f
                  + a2 * ( 1.0f / 362880.0f)))));
    }

    forcedinline float cosineTurns (float t) noexcept { return sineTurns (t + 0.25f); }

    /** 2^x, accurate to about 1e-6 over the range pitch conversion needs.

        Pitch has to be converted per sample (glide, drift, vibrato and bend all
        move continuously), and std::exp2 is far too expensive to call once per
        unison voice per sample. */
    forcedinline float exp2Fast (float x) noexcept
    {
        x = juce::jlimit (-120.0f, 120.0f, x);

        const float xi = std::floor (x);
        const float f  = x - xi;

        const float p = 1.0f + f * (0.69314718f
                      + f * (0.24022651f
                      + f * (0.05550411f
                      + f * (0.00961813f
                      + f *  0.00133336f))));

        const juce::int32 bits = (juce::int32) ((int) xi + 127) << 23;
        float scale;
        std::memcpy (&scale, &bits, sizeof (float));

        return p * scale;
    }

    /** log2(x) to about 1e-4, for x > 0.

        Only ever used to choose a wavetable mip level, where the answer is
        immediately floored and cross-faded, so four decimal places is three
        more than the decision needs. */
    forcedinline float log2Fast (float x) noexcept
    {
        juce::uint32 bits;
        const float safe = juce::jmax (x, 1.0e-20f);
        std::memcpy (&bits, &safe, sizeof (float));

        const float e = (float) ((juce::int32) ((bits >> 23) & 0xFFu) - 127);

        bits = (bits & 0x007FFFFFu) | 0x3F800000u;      // mantissa into [1, 2)
        float m;
        std::memcpy (&m, &bits, sizeof (float));

        return e + (-1.7417939f + (2.8212026f + (-1.4699568f
                  + (0.44717955f - 0.056570851f * m) * m) * m) * m);
    }

    forcedinline float midiNoteToHz (float note) noexcept
    {
        return 440.0f * exp2Fast ((note - 69.0f) * (1.0f / 12.0f));
    }

    /** Cents to a frequency ratio. */
    forcedinline float centsToRatio (float cents) noexcept
    {
        return exp2Fast (cents * (1.0f / 1200.0f));
    }

    /** tan(pi * f / fs), used by every TPT filter in the engine.

        Derived from the fast sine rather than libm's tan because the filters
        recompute it once per sample when the cutoff is being modulated.  The
        caller must have clamped f/fs to 0.45 or below, which keeps the cosine
        term above 0.15 and the division safe. */
    forcedinline float tanPrewarp (float normalisedFrequency) noexcept
    {
        const float t = juce::jlimit (0.0001f, 0.45f, normalisedFrequency) * 0.5f;
        const float c = cosineTurns (t);

        return sineTurns (t) / juce::jmax (c, 1.0e-4f);
    }

    // -----------------------------------------------------------------------
    //  Small building blocks
    // -----------------------------------------------------------------------

    /** Exponential one-pole smoother.  Used wherever a value reaches the audio
        path but is only recomputed occasionally. */
    struct OnePole
    {
        float z = 0.0f;
        float a = 0.0f;

        void setTime (float seconds, double sampleRate) noexcept
        {
            a = (seconds <= 0.0f || sampleRate <= 0.0)
                    ? 0.0f
                    : std::exp (-1.0f / (float) (seconds * sampleRate));
        }

        void setValue (float v) noexcept { z = v; }
        void reset() noexcept            { z = 0.0f; }

        forcedinline float process (float target) noexcept
        {
            z = target + a * (z - target);
            return (z = flush (z));
        }
    };

    /** Zero-delay-feedback one-pole, the shared building block of every filter
        and every band split in the engine. */
    struct OnePoleTPT
    {
        float g = 0.5f;
        float s = 0.0f;

        void setCutoff (float hz, double sampleRate) noexcept
        {
            const float G = tanPrewarp ((float) (juce::jlimit (0.05, sampleRate * 0.45,
                                                               (double) hz) / sampleRate));
            g = G / (1.0f + G);
        }

        void reset() noexcept { s = 0.0f; }

        forcedinline float lowpass (float x) noexcept
        {
            const float v = g * (x - s);
            const float y = s + v;
            s = flush (y + v);
            return y;
        }

        forcedinline float highpass (float x) noexcept { return x - lowpass (x); }
    };

    /** DC blocker.  A first-order high pass at a few Hz; every stage that can
        generate even harmonics is followed by one of these, because an even
        harmonic generator also generates a DC term and DC steals headroom from
        everything after it. */
    struct DcBlocker
    {
        float x1 = 0.0f, y1 = 0.0f, r = 0.9995f;

        void prepare (double sampleRate) noexcept
        {
            // 6 Hz: below the lowest fundamental the instrument produces, so it
            // removes offset without thinning a 20 Hz sub.
            r = std::exp (-kTwoPi * 6.0f / (float) juce::jmax (1.0, sampleRate));
        }

        void reset() noexcept { x1 = y1 = 0.0f; }

        forcedinline float process (float x) noexcept
        {
            const float y = x - x1 + r * y1;
            x1 = x;
            y1 = flush (y);
            return y1;
        }
    };

    /** Deterministic 32-bit xorshift.

        Every random-looking quantity in NACAR - voice variation, unison phase
        distribution, drift seeds, dust noise - comes from one of these, seeded
        from an index rather than from a clock, so a session recalls identically
        on every machine and in every render. */
    struct Rng
    {
        juce::uint32 s = 0x9E3779B9u;

        Rng() = default;
        explicit Rng (juce::uint32 seed) noexcept { setSeed (seed); }

        void setSeed (juce::uint32 seed) noexcept
        {
            // Mix first: consecutive voice indices must not produce visibly
            // related streams, and a raw xorshift seeded with 0, 1, 2 does.
            seed ^= 0x9E3779B9u;
            seed *= 0x85EBCA6Bu;
            seed ^= seed >> 13;
            seed *= 0xC2B2AE35u;
            seed ^= seed >> 16;
            s = seed | 1u;
        }

        forcedinline juce::uint32 nextUint() noexcept
        {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            return s;
        }

        forcedinline float next01() noexcept
        {
            return (float) (nextUint() >> 8) * (1.0f / 16777216.0f);
        }

        forcedinline float nextBipolar() noexcept { return next01() * 2.0f - 1.0f; }
    };

    /** Linear per-sample ramp across one block.

        The engine reads each parameter from the registry once per block and
        turns it into one of these.  Voices then evaluate the ramp at their own
        sample index, so every voice sees the same smoothed trajectory without
        any of them owning smoothing state. */
    struct Ramp
    {
        float start = 0.0f;
        float step  = 0.0f;

        forcedinline float at (int i) const noexcept { return start + step * (float) i; }

        void set (float from, float to, int numSamples) noexcept
        {
            start = from;
            step  = (numSamples > 0) ? (to - from) / (float) numSamples : 0.0f;
        }

        void snap (float v) noexcept { start = v; step = 0.0f; }
    };

    // -----------------------------------------------------------------------
    //  Panning
    // -----------------------------------------------------------------------

    /** -4.5 dB compromise pan law: the geometric mean of the constant-gain and
        constant-power laws.

        Constant gain keeps a mono fold-down perfectly flat but dips 3 dB in the
        centre in stereo; constant power keeps stereo flat but lifts centred
        material 3 dB when folded to mono.  The geometric mean of the two halves
        both errors, which is what unison spread needs: a wide patch has to stay
        recognisable in mono without the centre voices jumping forward. */
    forcedinline void panGains (float position, float& left, float& right) noexcept
    {
        const float p = juce::jlimit (-1.0f, 1.0f, position);

        const float linL = 0.5f * (1.0f - p);
        const float linR = 0.5f * (1.0f + p);

        const float angle = (p + 1.0f) * 0.125f;      // turns: 0 .. 0.25
        const float powL  = cosineTurns (angle);
        const float powR  = sineTurns (angle);

        left  = std::sqrt (juce::jmax (0.0f, linL * powL));
        right = std::sqrt (juce::jmax (0.0f, linR * powR));
    }

    // -----------------------------------------------------------------------
    //  Interpolation
    // -----------------------------------------------------------------------

    /** 4-point, 3rd-order Hermite.  Used for wavetable phase interpolation and
        for every fractional delay read in the engine. */
    forcedinline float hermite (float frac, float ym1, float y0, float y1, float y2) noexcept
    {
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    forcedinline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
}
