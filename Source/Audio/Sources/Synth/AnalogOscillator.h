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
    };
}
