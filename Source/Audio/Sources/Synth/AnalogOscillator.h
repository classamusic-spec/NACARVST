#pragma once

#include "Oscillator.h"
#include "WavetableOscillator.h"

namespace nacar::synth
{
    /**
        One oscillator - one phase accumulator, one waveform, one sub-voice of a
        unison group.

        Named for the four analogue shapes it generates directly, but it also
        owns the phase for the WAVETABLE setting, because all five waveforms
        advance identically and a voice must be able to change waveform without
        the phase jumping.

        Anti-aliasing:
          SAW and PULSE     PolyBLEP on every discontinuity, including the
                            second one a pulse has at its trailing edge.
          TRIANGLE          a leaky integration of the band-limited square,
                            which is band-limited by construction and costs two
                            operations more than the square it came from.
          SINE              nothing to do; a sine has no harmonics to fold.
          WAVETABLE         the mip pyramid in WavetableBank.
          HARD SYNC         a second, time-domain BLEP at the reset instant.
    */
    class AnalogOscillator
    {
    public:
        void prepare (double sampleRate) noexcept;
        void reset (float startPhase) noexcept;

        /** Renders one sample and advances the phase.

            @param wave        which of the five shapes
            @param increment   cycles per sample; clamped internally
            @param pulseWidth  0.02 .. 0.98, ignored except by PULSE
            @param phaseMod    phase modulation in cycles, added at read time so
                               it cannot accumulate into a pitch error
            @param syncFrac    where in this sample the master oscillator
                               wrapped, 0 .. 1, or a negative value for no sync
            @param wt          only read when wave == wavetable
        */
        float process (Waveform wave, float increment, float pulseWidth,
                       float phaseMod, float syncFrac,
                       const WavetableContext& wt) noexcept;

        float getPhase() const noexcept { return phase; }

    private:
        float shape (Waveform wave, float readPhase, float increment,
                     float pulseWidth, float levelF, const WavetableContext& wt) const noexcept;

        float phase       = 0.0f;
        float triState    = 0.0f;
        float triLeak     = 0.9995f;
        float pendingBlep = 0.0f;

        // The integrator's near-DC gain is enormous, so its output is
        // high-passed.  Without this, anything that puts a low-frequency term
        // into the square - phase modulation above all - comes back out as
        // subsonic energy large enough to swamp the note itself.
        DcBlocker triDc;
    };

    /**
        THE SAME OSCILLATOR, EIGHT AT A TIME.

        A unison group is N copies of one oscillator that share everything
        except a phase and an increment: the same waveform, the same pulse
        width, the same phase modulation, the same sync instant, the same
        sample rate.  Rendered one at a time - which is what an array of
        AnalogOscillator forces - that is N calls through a function pointer's
        worth of prologue for arithmetic that is four or eight lanes wide on
        every CPU this plugin will ever run on.

        This renders the whole group at once, and it renders it to the SAME
        BITS.  Every operation below is the scalar oscillator's operation in
        the scalar oscillator's order; every branch is a select, except the
        two that are uniform across the group (hard sync, and whether any lane
        at all is within one increment of a discontinuity) and can stay
        branches.  Tests/SimdTests.cpp asserts the agreement sample for sample,
        for every waveform, every unison count from 1 to 8, and a spread of
        detune, pulse width and sample rate.

        Two cases deliberately stay scalar, and take a path here that is a
        transcription of AnalogOscillator::process.

        WAVETABLE, because reading it is four table lookups per lane at indices
        that differ per lane - a gather, which SIMDRegister does not offer and
        which SSE2, the baseline this project compiles for, cannot do at all.

        And a group smaller than half a register, because a vector whose lanes
        are mostly arithmetic nobody reads is not free: measured with unison
        off, the vector path cost about 4 % MORE than the scalar one, and a
        great many patches have unison off.  Making that case take the scalar
        path turned a 3 % regression into a 2 % gain.

        §146: no allocation, no lock, no IO.  Every buffer is a fixed-size
        member or a stack array, sized from kMaxUnison at compile time, and
        aligned because SIMDRegister can only load from memory aligned to its
        own width.
    */
    class UnisonOscillatorBank
    {
    public:
        // ------------------------------------------------------------------
        //  Per-block inputs.
        //
        //  They live in the bank rather than in the voice because a vector
        //  load needs its address aligned to the register width, and a plain
        //  member array in the caller is not.  The voice writes elements
        //  [0, count) of each once per block, exactly where it used to write
        //  its own arrays.
        // ------------------------------------------------------------------
        alignas (64) float detuneRatio [kUnisonPadded] {};
        alignas (64) float panL        [kUnisonPadded] {};
        alignas (64) float panR        [kUnisonPadded] {};
        alignas (64) float panLStep    [kUnisonPadded] {};
        alignas (64) float panRStep    [kUnisonPadded] {};

        void prepare (double sampleRate) noexcept;

        /** Resets one sub-voice's phase and its shaping state. */
        void reset (int index, float startPhase) noexcept;

        float getPhase (int index) const noexcept { return phase[index]; }

        /**
            One sample of a whole unison group.

            `mono`, `left` and `right` are ACCUMULATED INTO, in ascending
            sub-voice order, so that the sums are the sums the scalar loop
            produced - float addition is not associative and a horizontal
            vector sum would reassociate them.

            @param wave            the shared waveform
            @param count           how many sub-voices are sounding, 1..kMaxUnison
            @param fundamentalHz   the group's centre frequency
            @param invSampleRate   1 / fs, as the caller already holds it
            @param pulseWidth      shared, already offset by voice variation
            @param phaseMod        shared phase modulation, in cycles
            @param syncFrac        where in this sample the master wrapped, or
                                   negative for no sync
            @param wt              only read when wave == wavetable
            @param sampleIndex     position in the block, for the pan ramp
        */
        void render (Waveform wave, int count,
                     float fundamentalHz, float invSampleRate,
                     float pulseWidth, float phaseMod, float syncFrac,
                     const WavetableContext& wt, float sampleIndex,
                     float& mono, float& left, float& right) noexcept;

    private:
        template <Waveform W>
        void renderVector (int count, float fundamentalHz, float invSampleRate,
                           float pulseWidth, float phaseMod, float syncFrac,
                           float* y) noexcept;

        template <Waveform W>
        void renderScalar (int count, float fundamentalHz, float invSampleRate,
                           float pulseWidth, float phaseMod, float syncFrac,
                           const WavetableContext& wt, float* y) noexcept;

        alignas (64) float phase       [kUnisonPadded] {};
        alignas (64) float triState    [kUnisonPadded] {};
        alignas (64) float pendingBlep [kUnisonPadded] {};
        alignas (64) float dcX1        [kUnisonPadded] {};
        alignas (64) float dcY1        [kUnisonPadded] {};

        float triLeak = 0.9995f;
        float dcR     = 0.9995f;
    };
}
