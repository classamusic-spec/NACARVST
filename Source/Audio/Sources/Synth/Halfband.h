#pragma once

#include "SynthCommon.h"

#include <array>

namespace nacar::synth
{
    /**
        2x POLYPHASE HALFBAND, for the voice's nonlinear core.

        Specification section 59 asks for selective oversampling around
        waveshaping, drive and nonlinear filters, and for it to be justified by
        profiling rather than applied blindly.  NacarBench measures where the
        aliasing actually comes from, one stage at a time:

            bare saw at C7                       -33.6 dB
            + Body at 50 %                       -33.7 dB   (no contribution)
            + post-filter saturation at 50 %     -36.8 dB   (no contribution)
            + pre-filter drive at 50 %           -29.8 dB   (+3.8 dB)
            + ladder filter drive at 60 %        -24.2 dB   (+9.4 dB)

        Body is band-split to 90-700 Hz, so its products land nowhere near
        Nyquist; the post saturator is gain-compensated and blended, so it
        removes more top than it adds.  The drive stage and the ladder's
        feedback saturator are the two that matter, and they sit either side of
        the filter - so the block that gets oversampled is exactly

            pre-filter drive -> primary filter -> creative filter -> saturator

        and nothing else in the voice.

        ---------------------------------------------------------------------
        DESIGN

        A halfband FIR has every second tap equal to zero apart from the centre
        one, which is exactly one half.  That makes the polyphase split free:

          upsampling    the odd output phase is the input, delayed - no
                        multiplies at all - and the even phase is the remaining
                        taps.
          downsampling  the same, summed.

        The taps are computed here from a windowed sinc rather than pasted in
        as a table of magic numbers, so the design is auditable and the length
        can be changed without hunting for a new set of coefficients.
    */
    template <int NumTaps>
    class HalfbandDesign
    {
    public:
        static_assert (NumTaps % 4 == 3, "A halfband FIR needs 4k+3 taps");

        static constexpr int length = NumTaps;
        static constexpr int centre = (NumTaps - 1) / 2;       ///< always odd
        static constexpr int numEven = (NumTaps + 1) / 2;      ///< taps at even indices

        /** The even-indexed taps, which are the only ones that cost anything. */
        static const std::array<float, (size_t) numEven>& evenTaps()
        {
            static const std::array<float, (size_t) numEven> taps = []
            {
                std::array<float, (size_t) NumTaps> h {};

                // Ideal halfband: h[n] = 0.5 * sinc (0.5 * (n - centre)),
                // windowed.  Blackman is used because its -74 dB sidelobes are
                // well below anything the nonlinear stages produce, and because
                // it needs no shape parameter to tune.
                for (int n = 0; n < NumTaps; ++n)
                {
                    const double d = (double) (n - centre);

                    double ideal;

                    if (std::abs (d) < 1.0e-9)
                        ideal = 0.5;
                    else
                        ideal = std::sin (juce::MathConstants<double>::pi * 0.5 * d)
                                    / (juce::MathConstants<double>::pi * d);

                    const double t = (double) n / (double) (NumTaps - 1);
                    const double w = 0.42
                                   - 0.5  * std::cos (2.0 * juce::MathConstants<double>::pi * t)
                                   + 0.08 * std::cos (4.0 * juce::MathConstants<double>::pi * t);

                    h[(size_t) n] = (float) (ideal * w);
                }

                // Normalise to unity gain at DC, so the stage is transparent.
                // Unity, not a half: a halfband lowpass sums to one and has a
                // centre tap of exactly one half, and it is the interpolator's
                // factor of two that restores the level afterwards.
                double sum = 0.0;
                for (auto v : h)
                    sum += (double) v;

                if (sum > 1.0e-12)
                    for (auto& v : h)
                        v = (float) ((double) v / sum);

                std::array<float, (size_t) numEven> even {};

                for (int j = 0; j < numEven; ++j)
                    even[(size_t) j] = h[(size_t) (j * 2)];

                return even;
            }();

            return taps;
        }
    };

    /**
        Upsampler.  One low-rate sample in, two high-rate samples out.

        The odd phase really is the input delayed by (centre - 1) / 2 low-rate
        samples: the centre tap is exactly one half, and interpolation doubles
        it back to one.  The even phase is the remaining taps.
    */
    template <int NumTaps>
    class Halfband2xUp
    {
    public:
        using Design = HalfbandDesign<NumTaps>;

        void reset() noexcept { history.fill (0.0f); write = 0; }

        forcedinline void process (float x, float& out0, float& out1) noexcept
        {
            // A circular index rather than a shift: this runs once per sample
            // per channel per voice, and moving sixteen floats to make room for
            // one was costing more than the sixteen multiplies that follow.
            write = (write - 1) & mask;
            history[(size_t) write] = x;

            const auto& taps = Design::evenTaps();

            float even = 0.0f;

            for (int j = 0; j < Design::numEven; ++j)
                even += taps[(size_t) j] * history[(size_t) ((write + j) & mask)];

            out0 = even * 2.0f;
            out1 = history[(size_t) ((write + (Design::centre - 1) / 2) & mask)];
        }

    private:
        static constexpr int size = 32;          ///< next power of two above numEven
        static constexpr int mask = size - 1;

        std::array<float, (size_t) size> history {};
        int write = 0;
    };

    /**
        Downsampler.  Two high-rate samples in, one low-rate sample out.

            y[n] = sum_k h[k] * xh[2n - k]

        For even k = 2j the term reaches xh[2n - 2j], which is the even high-
        rate phase j steps ago.  The only odd-indexed tap is the centre one, and
        it reaches xh[2n - centre]; that index is odd, and the odd phase at step
        m is xh[2m + 1], so it is the odd phase (centre + 1) / 2 steps ago.

        Getting that last index wrong by one still produces plausible audio - it
        is a half-sample misalignment between the two phases, which reads as a
        gentle high-frequency roll-off plus a surviving image.  It is why the
        round-trip test exists.
    */
    template <int NumTaps>
    class Halfband2xDown
    {
    public:
        using Design = HalfbandDesign<NumTaps>;

        void reset() noexcept { evenHistory.fill (0.0f); oddHistory.fill (0.0f); write = 0; }

        forcedinline float process (float in0, float in1) noexcept
        {
            write = (write - 1) & mask;
            evenHistory[(size_t) write] = in0;
            oddHistory [(size_t) write] = in1;

            const auto& taps = Design::evenTaps();

            float y = 0.0f;

            for (int j = 0; j < Design::numEven; ++j)
                y += taps[(size_t) j] * evenHistory[(size_t) ((write + j) & mask)];

            // The single odd-indexed tap is the centre one, which is one half.
            // See the derivation above the class: the odd phase is read
            // (centre + 1) / 2 steps back, one later than the even phase's
            // (centre - 1) / 2 that the upsampler uses.
            y += 0.5f * oddHistory[(size_t) ((write + (Design::centre + 1) / 2) & mask)];

            return y;
        }

    private:
        static constexpr int size = 32;
        static constexpr int mask = size - 1;

        std::array<float, (size_t) size> evenHistory {};
        std::array<float, (size_t) size> oddHistory {};
        int write = 0;
    };

    /** 19 taps: 10 multiplies per phase.

        The stopband only has to sit below the aliasing the oscillators already
        produce, which NacarBench measures at about -40 dB.  A 31-tap design
        reaches -74 dB, which is thirty decibels of protection nobody can hear,
        paid for on every sample of every voice; 19 taps still clears the bar
        with room to spare, and the benchmark is what confirms that rather than
        the arithmetic. */
    inline constexpr int kHalfbandTaps = 19;

    using VoiceUpsampler   = Halfband2xUp<kHalfbandTaps>;
    using VoiceDownsampler = Halfband2xDown<kHalfbandTaps>;
}
