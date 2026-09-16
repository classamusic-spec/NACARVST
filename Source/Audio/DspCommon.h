#pragma once

#include "Sources/Synth/SynthCommon.h"
#include "Sources/Synth/Halfband.h"

#include <array>
#include <vector>

/**
    Shared building blocks for Memory, the FX chain, Atmosphere and Weight.

    The synth core already owns a careful set of realtime primitives - fast
    transcendentals, TPT one-poles, DC blockers, a deterministic RNG, the
    per-block ramp, the pan law, the halfband.  Rather than write a second set
    that would drift from the first, they are pulled into this namespace by
    name, and the pieces that only the effects need are added underneath.
*/
namespace nacar::fx
{
    // -- borrowed from the synth core ---------------------------------------
    using synth::OnePole;
    using synth::OnePoleTPT;
    using synth::DcBlocker;
    using synth::Rng;
    using synth::Ramp;

    using synth::flush;
    using synth::sane;
    using synth::tanhFast;
    using synth::sineTurns;
    using synth::cosineTurns;
    using synth::exp2Fast;
    using synth::log2Fast;
    using synth::lerp;
    using synth::hermite;
    using synth::panGains;
    using synth::midiNoteToHz;
    using synth::centsToRatio;
    using synth::tanPrewarp;

    using synth::VoiceUpsampler;
    using synth::VoiceDownsampler;

    inline constexpr float kPi    = synth::kPi;
    inline constexpr float kTwoPi = synth::kTwoPi;

    // =======================================================================
    //  Delay line
    //
    //  One channel, power-of-two length, Hermite-interpolated fractional read.
    //  Everything in the effects chain that remembers anything is built on
    //  this: the reverb's delays, the chorus in Shadow, the comb in Retro, the
    //  history Rewind and Grain read from.
    // =======================================================================
    class DelayLine
    {
    public:
        /** Allocates. Message thread only. */
        void prepare (int maxSamples)
        {
            int size = 2;
            while (size < juce::jmax (4, maxSamples + 4))
                size <<= 1;

            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            writeIndex = 0;
        }

        void reset() noexcept
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writeIndex = 0;
        }

        int capacity() const noexcept { return (int) buffer.size(); }

        forcedinline void write (float x) noexcept
        {
            buffer[(size_t) writeIndex] = x;
            writeIndex = (writeIndex + 1) & mask;
        }

        /** Reads `delay` samples back. Hermite interpolated. */
        forcedinline float read (float delay) const noexcept
        {
            const float d = juce::jlimit (1.0f, (float) (mask - 2), delay);
            const int   i = (int) d;
            const float f = d - (float) i;

            const int base = (writeIndex - i + (int) buffer.size()) & mask;

            const float ym1 = buffer[(size_t) ((base + 1) & mask)];
            const float y0  = buffer[(size_t) base];
            const float y1  = buffer[(size_t) ((base - 1 + (int) buffer.size()) & mask)];
            const float y2  = buffer[(size_t) ((base - 2 + (int) buffer.size()) & mask)];

            return hermite (f, ym1, y0, y1, y2);
        }

        /** Cheaper linear read, for tails where interpolation error is masked. */
        forcedinline float readLinear (float delay) const noexcept
        {
            const float d = juce::jlimit (1.0f, (float) (mask - 2), delay);
            const int   i = (int) d;
            const float f = d - (float) i;

            const int a = (writeIndex - i + (int) buffer.size()) & mask;
            const int b = (a - 1 + (int) buffer.size()) & mask;

            return lerp (buffer[(size_t) a], buffer[(size_t) b], f);
        }

        forcedinline float readInt (int delay) const noexcept
        {
            const int d = juce::jlimit (0, mask, delay);
            return buffer[(size_t) ((writeIndex - d + (int) buffer.size()) & mask)];
        }

    private:
        std::vector<float> buffer;
        int mask = 0;
        int writeIndex = 0;
    };

    // =======================================================================
    //  Allpass diffuser
    //
    //  The unit every plate and hall reverb is built from: it scatters energy
    //  in time without colouring the magnitude response, which is what lets a
    //  tail become dense without becoming ringy.
    // =======================================================================
    class Allpass
    {
    public:
        void prepare (int maxSamples) { delay.prepare (maxSamples); }
        void reset() noexcept { delay.reset(); }

        void setDelay (float samples) noexcept { delaySamples = juce::jmax (1.0f, samples); }
        void setCoefficient (float g) noexcept { coeff = juce::jlimit (-0.95f, 0.95f, g); }

        forcedinline float process (float x) noexcept
        {
            const float delayed = delay.readLinear (delaySamples);
            const float v = x - coeff * delayed;

            delay.write (v);

            return flush (coeff * v + delayed);
        }

        /** Modulated read, for tails that would otherwise sit still. */
        forcedinline float process (float x, float modSamples) noexcept
        {
            const float delayed = delay.readLinear (juce::jmax (1.0f, delaySamples + modSamples));
            const float v = x - coeff * delayed;

            delay.write (v);

            return flush (coeff * v + delayed);
        }

    private:
        DelayLine delay;
        float delaySamples = 100.0f;
        float coeff = 0.5f;
    };

    // =======================================================================
    //  Two-band and three-band splits
    //
    //  TPT one-poles, complementary by construction: the bands always sum back
    //  to the input, so a split-band stage is transparent when it is doing
    //  nothing.  Used by Weight, Memory and Patina.
    // =======================================================================
    struct TwoBand
    {
        OnePoleTPT lp;

        void prepare (float hz, double sampleRate) noexcept { lp.setCutoff (hz, sampleRate); }
        void reset() noexcept { lp.reset(); }

        forcedinline void split (float x, float& low, float& high) noexcept
        {
            low = lp.lowpass (x);
            high = x - low;
        }
    };

    struct ThreeBand
    {
        OnePoleTPT lowSplit, midSplit;

        void prepare (float lowHz, float highHz, double sampleRate) noexcept
        {
            lowSplit.setCutoff (lowHz, sampleRate);
            midSplit.setCutoff (highHz, sampleRate);
        }

        void reset() noexcept { lowSplit.reset(); midSplit.reset(); }

        forcedinline void split (float x, float& low, float& mid, float& high) noexcept
        {
            low = lowSplit.lowpass (x);
            const float rest = x - low;
            mid = midSplit.lowpass (rest);
            high = rest - mid;
        }
    };

    // =======================================================================
    //  Tilt
    //
    //  One control that darkens or brightens without changing perceived level:
    //  a shelf pair pivoting about the midrange.  -1 dark, 0 flat, +1 bright.
    //  Memory, Retro, Crush, Patina and Space all want exactly this.
    // =======================================================================
    class Tilt
    {
    public:
        void prepare (double sampleRate, float pivotHz = 700.0f) noexcept
        {
            split.prepare (pivotHz, sampleRate);
            reset();
        }

        void reset() noexcept { split.reset(); }

        forcedinline float process (float x, float amount) noexcept
        {
            if (std::abs (amount) < 0.001f)
                return x;

            const float a = juce::jlimit (-1.0f, 1.0f, amount);

            float low, high;
            split.split (x, low, high);

            // +-9 dB at the extremes, mirrored, so the midrange stays put.
            const float g = exp2Fast (a * 1.5f);

            return low / g + high * g;
        }

    private:
        TwoBand split;
    };

    // =======================================================================
    //  Stereo history buffer
    //
    //  The circular store the signature Rewind -> Grain -> Space path reads
    //  from (specification section 91).
    //
    //  Rewind and Grain own ONE EACH rather than sharing a single buffer.  The
    //  chain is reorderable, so "recent history" is not one thing: what reached
    //  Rewind and what reached Grain differ by whatever sits between them, and
    //  a shared buffer would give one of the two the wrong signal in every
    //  order but one.
    // =======================================================================
    class HistoryBuffer
    {
    public:
        void prepare (double sampleRate, double seconds)
        {
            const int wanted = (int) (sampleRate * juce::jmax (0.5, seconds));

            int size = 2;
            while (size < wanted + 8)
                size <<= 1;

            left.assign ((size_t) size, 0.0f);
            right.assign ((size_t) size, 0.0f);

            mask = size - 1;
            writeIndex = 0;
            written = 0;
        }

        void reset() noexcept
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);
            writeIndex = 0;
            written = 0;
        }

        int capacity() const noexcept { return (int) left.size(); }
        int available() const noexcept { return written; }

        forcedinline void write (float l, float r) noexcept
        {
            left [(size_t) writeIndex] = l;
            right[(size_t) writeIndex] = r;

            writeIndex = (writeIndex + 1) & mask;

            if (written < (int) left.size())
                ++written;
        }

        /** Reads `delay` samples back from the write head, interpolated. */
        forcedinline void read (float delay, float& l, float& r) const noexcept
        {
            const float d = juce::jlimit (1.0f, (float) (mask - 2), delay);
            const int   i = (int) d;
            const float f = d - (float) i;

            const int a = (writeIndex - i + (int) left.size()) & mask;
            const int b = (a - 1 + (int) left.size()) & mask;

            l = lerp (left [(size_t) a], left [(size_t) b], f);
            r = lerp (right[(size_t) a], right[(size_t) b], f);
        }

        /** Absolute index read, for granular grains that walk their own path. */
        forcedinline void readAt (float position, float& l, float& r) const noexcept
        {
            const int   i = (int) position;
            const float f = position - (float) i;

            const int a = i & mask;
            const int b = (i + 1) & mask;

            l = lerp (left [(size_t) a], left [(size_t) b], f);
            r = lerp (right[(size_t) a], right[(size_t) b], f);
        }

        int getWriteIndex() const noexcept { return writeIndex; }
        int getMask() const noexcept { return mask; }

    private:
        std::vector<float> left, right;
        int mask = 0;
        int writeIndex = 0;
        int written = 0;
    };

    // =======================================================================
    //  Tempo
    // =======================================================================

    /** Note divisions, in beats.  Shared by Rewind, Pulse and the LFOs so that
        a division never means two different things in two places. */
    inline float beatsForDivision (int index, int numDivisions,
                                   const float* table) noexcept
    {
        return table[juce::jlimit (0, numDivisions - 1, index)];
    }

    /** 1/32 .. 2/1 with triplets and dotted values, in the order of the
        lfo1_div choice list. */
    inline constexpr float kLfoDivisionBeats[14] = {
        0.125f, 1.0f / 6.0f, 0.25f, 1.0f / 3.0f, 0.375f, 0.5f,
        2.0f / 3.0f, 0.75f, 1.0f, 4.0f / 3.0f, 1.5f, 2.0f, 4.0f, 8.0f
    };

    /** 1/16 .. 1/1, in the order of the pulse_div choice list. */
    inline constexpr float kPulseDivisionBeats[9] = {
        0.25f, 1.0f / 3.0f, 0.375f, 0.5f, 2.0f / 3.0f, 0.75f, 1.0f, 2.0f, 4.0f
    };

    /** 1/16 .. 2 BAR, in the order of the rewind_div choice list. */
    inline constexpr float kRewindDivisionBeats[6] = {
        0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f
    };

    inline float beatsToSeconds (float beats, double bpm) noexcept
    {
        return (float) (beats * 60.0 / juce::jlimit (20.0, 300.0, bpm));
    }

    // =======================================================================
    //  Gain helpers
    // =======================================================================

    /** Equal-power dry/wet.  A linear crossfade dips 3 dB in the middle, which
        makes every Mix control in the instrument feel like it has a hole. */
    forcedinline void dryWetGains (float mix, float& dry, float& wet) noexcept
    {
        const float m = juce::jlimit (0.0f, 1.0f, mix);
        const float angle = m * 0.25f;          // turns

        dry = cosineTurns (angle);
        wet = sineTurns (angle);
    }

    /** Bounds a sample and replaces anything non-finite with silence.  Every
        effect's output goes through this: one NaN escaping into the chain would
        otherwise persist for the rest of the session. */
    forcedinline float guard (float x) noexcept
    {
        return sane (x) ? juce::jlimit (-8.0f, 8.0f, x) : 0.0f;
    }
}
