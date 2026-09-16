#include "LFO.h"

namespace nacar
{
    void LFO::prepare (double sr, juce::uint32 s) noexcept
    {
        sampleRate = juce::jmax (1.0, sr);
        seed = s | 1u;

        slew.setTime (kSlewSeconds, sampleRate);
        reset();
    }

    void LFO::reset() noexcept
    {
        cycles = 0.0;
        lastDepth = 0.0f;
        cachedStep = std::numeric_limits<juce::int64>::min();
        cachedA = cachedB = 0.0f;

        slew.reset();
    }

    // -----------------------------------------------------------------------
    float LFO::randomAt (juce::uint32 s, juce::int64 step) noexcept
    {
        // Both halves of the index are folded in so that a very long session
        // cannot alias two different cycles onto the same value, and the Rng's
        // own seed mixer does the work of decorrelating adjacent indices - a
        // raw xorshift seeded with n and n+1 produces visibly related streams.
        const auto low  = (juce::uint32) ((juce::uint64) step & 0xFFFFFFFFull);
        const auto high = (juce::uint32) (((juce::uint64) step >> 32) & 0xFFFFFFFFull);

        fx::Rng rng (s ^ low ^ (high * 0x85EBCA6Bu));
        return rng.nextBipolar();
    }

    float LFO::shapeValue (int shape, float p, juce::int64 step) noexcept
    {
        switch (shape)
        {
            case 1:     // TRIANGLE - rises from zero, exactly the shape the
                        // MOD page's preview draws, so the preview is not a lie
                return p < 0.25f ? p * 4.0f
                     : (p < 0.75f ? 2.0f - p * 4.0f : p * 4.0f - 4.0f);

            case 2:     return p * 2.0f - 1.0f;                 // SAW UP
            case 3:     return 1.0f - p * 2.0f;                 // SAW DOWN
            case 4:     return p < 0.5f ? 1.0f : -1.0f;         // SQUARE

            case 5:     // RANDOM - sample and hold, one new value per cycle
            case 6:     // SMOOTH RANDOM - the same values, interpolated
            {
                if (step != cachedStep)
                {
                    cachedStep = step;
                    cachedA = randomAt (seed, step);
                    cachedB = randomAt (seed, step + 1);
                }

                return shape == 5 ? cachedA
                                  : fx::lerp (cachedA, cachedB, mod::smoothStep (p));
            }

            case 0:
            default:    return fx::sineTurns (p);               // SINE
        }
    }

    // -----------------------------------------------------------------------
    void LFO::process (float* dest, int numSamples,
                       const Settings& s, const mod::Clock& clock) noexcept
    {
        if (dest == nullptr || numSamples <= 0)
            return;

        const int shape = juce::jlimit (0, 6, s.shape);
        const float depth = juce::jlimit (0.0f, 1.0f, s.depth);
        const float offset = juce::jlimit (0.0f, 1.0f, s.phaseOffset);

        // Cycles advanced per sample.
        //
        //   synced   beats-per-sample / beats-per-cycle
        //   free     Hz / sample rate
        //
        // The two expressions are the same quantity in different units, which
        // is why a synced LFO with the transport stopped needs no second code
        // path: 1 / (beatsToSeconds(beats, bpm) * fs) is identical to
        // beatsPerSample / beats.
        const float beats = fx::kLfoDivisionBeats[juce::jlimit (0, 13, s.division)];

        const double cycleInc = s.sync
            ? clock.beatsPerSample() / (double) juce::jmax (1.0e-3f, beats)
            : (double) juce::jlimit (0.001f, 100.0f, s.rateHz) / sampleRate;

        // Song lock.  Only when the transport is actually running: a stopped
        // transport reports a ppq position that does not move, and deriving
        // phase from it would freeze the LFO on a still parameter page.
        double c = (s.sync && clock.playing)
            ? clock.ppqPosition / (double) juce::jmax (1.0e-3f, beats)
            : cycles;

        const bool discontinuous = (shape == 2 || shape == 3 || shape == 4 || shape == 5);

        fx::Ramp depthRamp;
        depthRamp.set (lastDepth, depth, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const double withOffset = c + (double) offset;
            const double whole = std::floor (withOffset);

            const float p = (float) (withOffset - whole);
            const auto  step = (juce::int64) whole;

            float y = shapeValue (shape, p, step);

            // The continuous shapes keep the slew filter primed rather than
            // bypassing it cold, so changing shape mid-flight cannot step.
            if (discontinuous)
                y = slew.process (y);
            else
                slew.setValue (y);

            dest[i] = juce::jlimit (-1.0f, 1.0f, fx::guard (y * depthRamp.at (i)));

            c += cycleInc;
        }

        // Whichever way the phase was derived, the accumulator ends the block
        // holding the position of the next sample.  That is what lets a synced
        // LFO carry on from the song position when the transport stops.
        cycles = c;
        lastDepth = depth;

        // A double accumulating 40 Hz for a week is still exact to about a
        // billionth of a cycle, but wrapping it costs one branch per block and
        // removes the question entirely.
        if (cycles > 1.0e9 || cycles < -1.0e9)
            cycles -= std::floor (cycles);
    }
}
