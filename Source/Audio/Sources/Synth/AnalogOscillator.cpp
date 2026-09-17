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

    // =======================================================================
    //  UnisonOscillatorBank
    // =======================================================================
    namespace
    {
        /** The bank's per-lane shape, chosen once per block rather than per
            sample: the waveform is a block constant, so the switch that
            AnalogOscillator::shape() performs on every sample of every
            sub-voice does not need to exist here at all. */
        template <Waveform W>
        forcedinline Vec shapeVector (Vec readPhase, Vec inc, float pulseWidth) noexcept
        {
            if constexpr (W == Waveform::sine)
                return vSineTurns (readPhase);
            else if constexpr (W == Waveform::triangle)
                return vBlepPulse (readPhase, inc, 0.5f);
            else if constexpr (W == Waveform::saw)
                return vBlepSaw (readPhase, inc);
            else
                return vBlepPulse (readPhase, inc, pulseWidth);
        }

        forcedinline VecMask allLanesSet() noexcept
        {
            return VecMask::expand (0xFFFFFFFFu);
        }
    }

    void UnisonOscillatorBank::prepare (double sampleRate) noexcept
    {
        // The same two coefficients AnalogOscillator::prepare and
        // DcBlocker::prepare compute, from the same expressions.
        triLeak = std::exp (-kTwoPi * 5.0f / (float) juce::jmax (1.0, sampleRate));
        dcR     = std::exp (-kTwoPi * 6.0f / (float) juce::jmax (1.0, sampleRate));

        for (int i = 0; i < kUnisonPadded; ++i)
            reset (i, 0.0f);
    }

    void UnisonOscillatorBank::reset (int index, float startPhase) noexcept
    {
        if (index < 0 || index >= kUnisonPadded)
            return;

        phase[index]       = startPhase - std::floor (startPhase);
        triState[index]    = 0.0f;
        pendingBlep[index] = 0.0f;
        dcX1[index]        = 0.0f;
        dcY1[index]        = 0.0f;
    }

    template <Waveform W>
    void UnisonOscillatorBank::renderVector (int count, float fundamentalHz, float invSampleRate,
                                             float pulseWidth, float phaseMod, float syncFrac,
                                             float* y) noexcept
    {
        const Vec f0v  = Vec::expand (fundamentalHz);
        const Vec isrv = Vec::expand (invSampleRate);
        const Vec pmv  = Vec::expand (phaseMod);
        const Vec zero = Vec::expand (0.0f);
        const Vec loV  = zero;
        const Vec hiV  = Vec::expand (0.45f);

        // Hard sync is uniform across the group - one master resets all of it -
        // so it stays a branch, and the residual pair is computed once.
        const bool syncing = (syncFrac >= 0.0f);

        float residualNow = 0.0f, residualNext = 0.0f;

        if (syncing)
            blepSplit (syncFrac, residualNow, residualNext);

        // `after` is the read phase immediately following the reset, and it
        // depends only on the shared phase modulation.
        float afterScalar = phaseMod;
        afterScalar -= std::floor (afterScalar);

        const Vec afterV     = Vec::expand (afterScalar);
        const Vec syncFracV  = Vec::expand (syncFrac);
        const Vec rNowV      = Vec::expand (residualNow);
        const Vec rNextV     = Vec::expand (residualNext);
        const Vec postSyncV  = Vec::expand (1.0f - syncFrac);

        const int chunks = (count + kVecWidth - 1) / kVecWidth;

        for (int c = 0; c < chunks; ++c)
        {
            const int base = c * kVecWidth;

            // The increment the voice used to compute per sub-voice:
            //     f0 * detuneRatio[v] * invSr,  clamped to 0 .. 0.45
            // min/max rather than a pair of selects because SSE, AVX and NEON
            // all return the second operand of a min or max when a comparison
            // is unordered, which is what jlimit does with a NaN.
            const Vec ratio = Vec::fromRawArray (detuneRatio + base);
            const Vec inc   = Vec::min (hiV, Vec::max (loV, (f0v * ratio) * isrv));

            const Vec ph = Vec::fromRawArray (phase + base);

            Vec readPhase = ph + pmv;
            readPhase -= vfloor (readPhase);

            Vec out  = shapeVector<W> (readPhase, inc, pulseWidth)
                         + Vec::fromRawArray (pendingBlep + base);
            Vec pend = zero;

            if (syncing)
            {
                Vec before = (ph + syncFracV * inc) + pmv;
                before -= vfloor (before);

                const Vec jump = shapeVector<W> (afterV, inc, pulseWidth)
                                   - shapeVector<W> (before, inc, pulseWidth);

                const Vec halfJump = Vec::expand (0.5f) * jump;

                out  += halfJump * rNowV;
                pend  = halfJump * rNextV;
            }

            Vec tri = zero, dcIn = zero, dcOut = zero;

            if constexpr (W == Waveform::triangle)
            {
                tri = Vec::expand (triLeak) * Vec::fromRawArray (triState + base)
                        + (Vec::expand (4.0f) * inc) * out;
                tri = vflush (tri);

                // DcBlocker::process, lane by lane: y = x - x1 + r * y1.
                dcIn  = tri;
                dcOut = vflush ((tri - Vec::fromRawArray (dcX1 + base))
                                  + Vec::expand (dcR) * Vec::fromRawArray (dcY1 + base));
                out   = dcOut;
            }

            Vec next = syncing ? (postSyncV * inc) : (ph + inc);
            next -= vfloor (next);

            // The oscillator's own defence: a NaN arriving through phase
            // modulation silences that sub-voice and clears its state rather
            // than poisoning the phase for the rest of the session.
            const VecMask ok = vsane (next) & vsane (out);

            out  = out  & ok;
            next = next & ok;
            pend = pend & ok;

            next.copyToRawArray (phase + base);
            pend.copyToRawArray (pendingBlep + base);
            out.copyToRawArray (y + base);

            if constexpr (W == Waveform::triangle)
            {
                (tri   & ok).copyToRawArray (triState + base);
                (dcIn  & ok).copyToRawArray (dcX1 + base);
                (dcOut & ok).copyToRawArray (dcY1 + base);
            }
            else
            {
                // Nothing above touched the triangle state, so it is only
                // written when a lane has gone insane - which is never, in any
                // render that is working.
                if (! (ok == allLanesSet()))
                {
                    (Vec::fromRawArray (triState + base) & ok).copyToRawArray (triState + base);
                    (Vec::fromRawArray (dcX1 + base)     & ok).copyToRawArray (dcX1 + base);
                    (Vec::fromRawArray (dcY1 + base)     & ok).copyToRawArray (dcY1 + base);
                }
            }
        }
    }

    void UnisonOscillatorBank::renderWavetable (int count, float fundamentalHz, float invSampleRate,
                                                float phaseMod, float syncFrac,
                                                const WavetableContext& wt, float* y) noexcept
    {
        // A transcription of AnalogOscillator::process for the one waveform
        // whose inner loop is four table reads at four different indices.
        for (int v = 0; v < count; ++v)
        {
            const float inc = juce::jlimit (0.0f, 0.45f,
                                            fundamentalHz * detuneRatio[v] * invSampleRate);

            const float levelF = WavetableBank::levelForIncrement (inc);

            float readPhase = phase[v] + phaseMod;
            readPhase -= std::floor (readPhase);

            float out = WavetableOscillator::read (wt, readPhase, levelF) + pendingBlep[v];
            pendingBlep[v] = 0.0f;

            if (syncFrac >= 0.0f)
            {
                float before = phase[v] + syncFrac * inc + phaseMod;
                before -= std::floor (before);

                float after = phaseMod;
                after -= std::floor (after);

                const float jump = WavetableOscillator::read (wt, after,  levelF)
                                 - WavetableOscillator::read (wt, before, levelF);

                float residualNow = 0.0f, residualNext = 0.0f;
                blepSplit (syncFrac, residualNow, residualNext);

                out              += 0.5f * jump * residualNow;
                pendingBlep[v]    = 0.5f * jump * residualNext;
            }

            float next = (syncFrac >= 0.0f) ? (1.0f - syncFrac) * inc
                                            : phase[v] + inc;
            next -= std::floor (next);

            if (! sane (next) || ! sane (out))
            {
                phase[v]       = 0.0f;
                triState[v]    = 0.0f;
                pendingBlep[v] = 0.0f;
                dcX1[v]        = 0.0f;
                dcY1[v]        = 0.0f;
                y[v]           = 0.0f;
                continue;
            }

            phase[v] = next;
            y[v]     = out;
        }
    }

    void UnisonOscillatorBank::render (Waveform wave, int count,
                                       float fundamentalHz, float invSampleRate,
                                       float pulseWidth, float phaseMod, float syncFrac,
                                       const WavetableContext& wt, float sampleIndex,
                                       float& mono, float& left, float& right) noexcept
    {
        const int n = juce::jlimit (1, kMaxUnison, count);

        alignas (64) float y  [kUnisonPadded] {};
        alignas (64) float lv [kUnisonPadded] {};
        alignas (64) float rv [kUnisonPadded] {};

        switch (wave)
        {
            case Waveform::sine:
                renderVector<Waveform::sine> (n, fundamentalHz, invSampleRate,
                                              pulseWidth, phaseMod, syncFrac, y);
                break;
            case Waveform::triangle:
                renderVector<Waveform::triangle> (n, fundamentalHz, invSampleRate,
                                                  pulseWidth, phaseMod, syncFrac, y);
                break;
            case Waveform::saw:
                renderVector<Waveform::saw> (n, fundamentalHz, invSampleRate,
                                             pulseWidth, phaseMod, syncFrac, y);
                break;
            case Waveform::pulse:
                renderVector<Waveform::pulse> (n, fundamentalHz, invSampleRate,
                                               pulseWidth, phaseMod, syncFrac, y);
                break;
            case Waveform::wavetable:
                renderWavetable (n, fundamentalHz, invSampleRate, phaseMod, syncFrac, wt, y);
                break;
        }

        // The pan ramp, four or eight sub-voices at a time.  Same expression
        // the scalar loop used: y * (pan + panStep * sampleIndex).
        {
            const Vec fi = Vec::expand (sampleIndex);
            const int chunks = (n + kVecWidth - 1) / kVecWidth;

            for (int c = 0; c < chunks; ++c)
            {
                const int base = c * kVecWidth;
                const Vec yv = Vec::fromRawArray (y + base);

                (yv * (Vec::fromRawArray (panL + base)
                         + Vec::fromRawArray (panLStep + base) * fi)).copyToRawArray (lv + base);
                (yv * (Vec::fromRawArray (panR + base)
                         + Vec::fromRawArray (panRStep + base) * fi)).copyToRawArray (rv + base);
            }
        }

        // Summed in sub-voice order, because float addition is not associative
        // and the three running totals have to be the totals the scalar loop
        // produced, not a reassociation of them.
        for (int v = 0; v < n; ++v)
        {
            mono  += y[v];
            left  += lv[v];
            right += rv[v];
        }
    }
}
