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

        STUDIO and ECO run that block at 2x.  ULTRA runs the same block - not a
        wider one - at 4x, by putting a second halfband in series with the
        first; see Halfband4xUp below.  Widening the block instead would cost
        several times more for stages the table above measures at zero.

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

        /** Circular-buffer size for a history of `numEven` samples: the next
            power of two STRICTLY above it, so the oldest tap can never read the
            slot the newest write just took.  It is 32 for every length up to 61
            taps, which is why the 19-tap converters index exactly as they
            always have. */
        static constexpr int historySize = (numEven < 32) ? 32 : ((numEven < 64) ? 64 : 128);

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
        static constexpr int size = Design::historySize;
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
        static constexpr int size = Design::historySize;
        static constexpr int mask = size - 1;

        std::array<float, (size_t) size> evenHistory {};
        std::array<float, (size_t) size> oddHistory {};
        int write = 0;
    };

    /**
        4x UPSAMPLER, as two halfbands in series.

        A halfband always cuts at a quarter of its own output rate, which is
        half of its input rate, so the same derivation serves both steps:

            stage 1   sr  -> 2sr,  passes 0 .. sr/2
            stage 2   2sr -> 4sr,  passes 0 .. sr

        THE TWO STAGES ARE NOT EQUALLY IMPORTANT, and they are not the same
        length.  Everything that folds into the audible band on the way back
        down is folded there by stage 1: content at f between sr/2 and sr
        arrives at sr - f, attenuated by nothing but stage 1's stopband.  Stage
        2 only has to keep content between 1.5sr and 2sr out, which is far from
        its own transition and which 19 taps already attenuates by 50 dB or
        more - and its own weak point, just above sr, is fed by the part of the
        band stage 1 has already emptied.

        That asymmetry is measured, not assumed.  With both stages at 19 taps a
        4x core is no better than the 2x one it replaced and on a driven ladder
        it is measurably worse, because the images stage 2 lets through are
        intermodulated by the nonlinearity and come back as new products.  With
        stage 1 sharp it is 8 to 13 dB cleaner.  See kUltraHalfbandTaps.

        Time order is preserved throughout.  Halfband2xUp emits the even
        high-rate phase first and the odd one second, so feeding stage 2 with
        those two in that order produces the four 4x samples in order.
    */
    template <int Stage1Taps, int Stage2Taps>
    class Halfband4xUp
    {
    public:
        void reset() noexcept { stage1.reset(); stage2.reset(); }

        forcedinline void process (float x, float (&out)[4]) noexcept
        {
            float a = 0.0f, b = 0.0f;
            stage1.process (x, a, b);
            stage2.process (a, out[0], out[1]);
            stage2.process (b, out[2], out[3]);
        }

    private:
        Halfband2xUp<Stage1Taps> stage1;    ///< sr  -> 2sr
        Halfband2xUp<Stage2Taps> stage2;    ///< 2sr -> 4sr
    };

    /**
        4x DOWNSAMPLER.  The mirror image: decimate 4sr to 2sr twice over, in
        the reverse order to the upsampler's stages.
    */
    template <int Stage1Taps, int Stage2Taps>
    class Halfband4xDown
    {
    public:
        void reset() noexcept { stage1.reset(); stage2.reset(); }

        forcedinline float process (const float (&in)[4]) noexcept
        {
            const float a = stage2.process (in[0], in[1]);
            const float b = stage2.process (in[2], in[3]);
            return stage1.process (a, b);
        }

    private:
        Halfband2xDown<Stage1Taps> stage1;  ///< 2sr -> sr
        Halfband2xDown<Stage2Taps> stage2;  ///< 4sr -> sr
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

    /** ULTRA's pair.

        LATENCY.  Each halfband's group delay is `centre` samples at its own
        rate, so a round trip costs centre1 samples of the voice's rate plus
        centre2 / 2 of it:

            STUDIO   2x, 19 taps           9.0 samples
            ULTRA    4x, 43 then 19 taps  25.5 samples

        16.5 samples of difference, 344 microseconds at 48 kHz.  Two things
        follow.  The half sample is real and unavoidable - `centre` is always
        odd for a halfband, so the second stage always contributes a half - but
        a constant fractional delay is latency, not distortion, and the
        round-trip test in Tests/QualityTests.cpp fits the tone rather than
        shifting it for exactly that reason.  And the 16.5 samples mean the
        synth sits that much further behind the sample engine under ULTRA, and
        that a note started under ULTRA is that much behind one still sounding
        from STUDIO.  Neither is reported to the host as latency, because the
        figure would change with a parameter. */
    /** ULTRA's first stage: 43 taps, 22 multiplies per phase.

        19 taps is the wrong length for the stage that does the final
        decimation.  Its stopband only reaches -15 dB at 0.6 of the voice's
        sample rate and -33 dB at 0.7, so a nonlinear product landing at 0.65sr
        comes back at 0.35sr only 23 dB down.  Raising the core's rate does not
        touch that: the same filter does the same folding either way, which is
        why 4x with 19-tap converters measures no better than 2x - and on a
        driven ladder at C7 measures 4.4 dB worse.

        43 taps attenuates the same band by 43 dB at 0.6sr and 79 dB at 0.65sr.
        With that in place the extra rate pays: measured with a sine into the
        core, so that the oscillator contributes no inharmonic energy of its
        own, ULTRA is 7.5 to 12.7 dB cleaner than STUDIO across 44.1, 48 and
        96 kHz at MIDI 72, 84 and 96.

        It is not free, and not only in arithmetic.  A sharper filter is also a
        flatter one: the 2x round trip costs -3.2 dB at 0.4 of the sample rate
        and -6.6 dB at 0.45, and ULTRA's costs -0.13 and -2.4.  ULTRA therefore
        has a little more top octave than STUDIO as well as less aliasing.  That
        is the correct direction - the roll-off is an artefact of a cheap
        converter, not a voicing decision - but it does mean the two tiers are
        not identical above about 15 kHz. */
    inline constexpr int kUltraHalfbandTaps = 43;

    /** ULTRA's second stage, at four times the rate.  19 taps, because its job
        is the easy one and it runs twice as often as the first. */
    inline constexpr int kUltraHalfbandTaps2 = kHalfbandTaps;

    using VoiceUpsampler4x   = Halfband4xUp<kUltraHalfbandTaps, kUltraHalfbandTaps2>;
    using VoiceDownsampler4x = Halfband4xDown<kUltraHalfbandTaps, kUltraHalfbandTaps2>;
}
