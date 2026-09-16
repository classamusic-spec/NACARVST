#include "AnalogOscillator.h"

namespace nacar::synth
{
    void AnalogOscillator::prepare (double sampleRate) noexcept
    {
        // 5 Hz leak on the triangle integrator.  Low enough that a 20 Hz
        // triangle only loses about 3% of its amplitude, high enough that the
        // residual DC of the BLEP corrections never accumulates.
        triLeak = std::exp (-kTwoPi * 5.0f / (float) juce::jmax (1.0, sampleRate));
        triDc.prepare (sampleRate);
        reset (0.0f);
    }

    void AnalogOscillator::reset (float startPhase) noexcept
    {
        phase = startPhase - std::floor (startPhase);
        triState = 0.0f;
        pendingBlep = 0.0f;
        triDc.reset();
    }

    float AnalogOscillator::shape (Waveform wave, float readPhase, float increment,
                                   float pulseWidth, float levelF,
                                   const WavetableContext& wt) const noexcept
    {
        switch (wave)
        {
            case Waveform::sine:
                return sineTurns (readPhase);

            case Waveform::triangle:
                // The caller integrates this into a triangle; the width is
                // forced symmetric because Pulse Width belongs to PULSE.
                return blepPulse (readPhase, increment, 0.5f);

            case Waveform::saw:
                return blepSaw (readPhase, increment);

            case Waveform::pulse:
                return blepPulse (readPhase, increment, pulseWidth);

            case Waveform::wavetable:
            default:
                return WavetableOscillator::read (wt, readPhase, levelF);
        }
    }

    float AnalogOscillator::process (Waveform wave, float increment, float pulseWidth,
                                     float phaseMod, float syncFrac,
                                     const WavetableContext& wt) noexcept
    {
        // A negative or absurd increment would break every BLEP branch below,
        // and linear FM can produce both.  Clamping here rather than at each
        // call site is what makes the oscillator safe to modulate at audio
        // rate; the cost is that FM is not through-zero.
        const float inc = juce::jlimit (0.0f, 0.45f, increment);

        const float levelF = (wave == Waveform::wavetable)
                                ? WavetableBank::levelForIncrement (inc)
                                : 0.0f;

        float readPhase = phase + phaseMod;
        readPhase -= std::floor (readPhase);

        float y = shape (wave, readPhase, inc, pulseWidth, levelF, wt) + pendingBlep;
        pendingBlep = 0.0f;

        if (syncFrac >= 0.0f)
        {
            // Hard sync.  The jump is aperiodic, so the phase-domain PolyBLEP
            // above cannot see it coming; instead measure the actual step the
            // waveform takes at the reset instant and spread the 2-point
            // residual across this sample and the next.
            float before = phase + syncFrac * inc + phaseMod;
            before -= std::floor (before);

            float after = phaseMod;
            after -= std::floor (after);

            const float jump = shape (wave, after,  inc, pulseWidth, levelF, wt)
                             - shape (wave, before, inc, pulseWidth, levelF, wt);

            float residualNow = 0.0f, residualNext = 0.0f;
            blepSplit (syncFrac, residualNow, residualNext);

            y           += 0.5f * jump * residualNow;
            pendingBlep  = 0.5f * jump * residualNext;
        }

        if (wave == Waveform::triangle)
        {
            // Integrating a band-limited square gives a band-limited triangle:
            // the corners are rounded by exactly the amount the BLEP rounded
            // the edges.  4 * inc per sample is the slope that makes half a
            // period travel the full 2.0 of the output range.
            triState = triLeak * triState + 4.0f * inc * y;
            triState = flush (triState);
            y = triDc.process (triState);
        }

        if (syncFrac >= 0.0f)
            phase = (1.0f - syncFrac) * inc;   // phase accrued since the reset
        else
            phase += inc;

        phase -= std::floor (phase);

        // Defensive: a NaN arriving through phaseMod would otherwise poison the
        // phase permanently, and one silent oscillator is a far smaller problem
        // than one voice stuck at NaN for the rest of the session.
        if (! sane (phase) || ! sane (y))
        {
            phase = 0.0f;
            triState = 0.0f;
            pendingBlep = 0.0f;
            triDc.reset();
            return 0.0f;
        }

        return y;
    }
}
