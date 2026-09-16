#pragma once

#include "SynthCharacter.h"
#include "SynthCommon.h"

namespace nacar::synth
{
    /** One unison group's distribution, recomputed only when the topology, the
        voice count or the detune amount changes. */
    struct UnisonLayout
    {
        int   count = 1;

        float detune [kMaxUnison] {};   ///< -1..1, scaled later by the cents range
        float pan    [kMaxUnison] {};   ///< -1..1, scaled later by Spread
        float phase  [kMaxUnison] {};   ///< 0..1 start phase offsets

        float maxOffsetNorm = 0.0f;     ///< largest |detune| in the group
    };

    /**
        UNISON.

        Duplicating an oscillator and turning it up is not unison; it is one
        oscillator that is 6 dB louder.  What makes unison a richness system is
        the five things below, and all five have to be right at once:

          detune       what the voices are tuned to, relative to each other
          stereo       where they sit
          phase        where in the cycle they start
          level        how the group is normalised as the count changes
          variation    how far they are allowed to differ in anything else

        The topologies are behavioural, not cosmetic.  TIGHT keeps a bass note
        one object.  DENSE keeps it one object while making it bigger.  WIDE is
        the modern supersaw.  HAZE breathes because its spread is asymmetric.
        CLOUD stops being detune and becomes weather.
    */
    class UnisonEngine
    {
    public:
        /** Deterministic for a given (topology, count, detune, seed): the same
            patch always produces the same unison group, in every render and on
            every machine. */
        static void buildLayout (UnisonLayout& layout,
                                 UnisonTopology topology,
                                 int count,
                                 float detuneNorm,
                                 juce::uint32 seed) noexcept;

        /**
            AMPLITUDE NORMALISATION.

                gain(N) = N ^ -(0.5 + 0.5 * c)

            with c the correlation between the sub-voices, 0 (independent) to
            1 (identical).

            The two limits are the easy part.  N identical voices sum to N
            times one voice, so they need 1/N.  N statistically independent
            voices sum in power, so they need 1/sqrt(N).  Every real unison
            group sits somewhere between, and using either law alone is
            audible: 1/sqrt(N) makes a tightly detuned group jump 9 dB when you
            go from one voice to eight, and 1/N makes a widely detuned group
            sag by the same amount.

            What decides where a group sits is how fast the voices beat against
            each other, and that is a frequency in Hz, not a number of cents -
            ten cents at 50 Hz is a 0.3 Hz beat, which the ear integrates as one
            louder note, while the same ten cents at 2 kHz is a 12 Hz flutter
            that the ear integrates as two separate ones.  So:

                spreadHz = f0 * (2^(cents/1200) - 1)
                c        = 1 / (1 + (spreadHz * tau)^2)

            tau is the ear's loudness integration window.  0.08 s is used here
            rather than the textbook 0.03 s because the question this formula
            answers is "does the level jump", and a beat slower than about 12 Hz
            is heard as level while a faster one is heard as texture.

            The remaining case is detune exactly zero, where the voices really
            are identical: c goes to 1, the formula gives 1/N, and unison
            correctly becomes a no-op instead of an 18 dB boost.
        */
        static float normalisation (int count, float maxDetuneCents, float fundamentalHz) noexcept;
    };
}
