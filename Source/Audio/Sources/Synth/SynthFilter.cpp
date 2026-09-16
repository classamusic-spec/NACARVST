#include "SynthFilter.h"

namespace nacar::synth
{
    // =======================================================================
    //  LadderFilter
    // =======================================================================
    void LadderFilter::prepare (double sampleRate) noexcept
    {
        sr = juce::jmax (1.0, sampleRate);
        setCutoff (8000.0f);
        setResonance (0.0f);
        setDrive (0.0f);
        reset();
    }

    void LadderFilter::reset() noexcept
    {
        for (auto& v : s)
            v = 0.0f;
    }

    void LadderFilter::setCutoff (float hz) noexcept
    {
        const float normalised = (float) (juce::jlimit (20.0, sr * 0.45, (double) hz) / sr);
        G = tanPrewarp (normalised);
        g = G / (1.0f + G);
    }

    void LadderFilter::setResonance (float normalised) noexcept
    {
        // Four poles, so self-oscillation arrives at a feedback depth of 4.
        // Stopping at 3.95 leaves the peak enormous without letting the loop
        // gain reach unity, which at extreme drive would ring indefinitely.
        k = juce::jlimit (0.0f, 1.0f, normalised) * 3.95f;

        // Resonance in a ladder steals the passband, because the feedback is
        // subtracted from the input.  Putting a little of it back keeps the
        // body of a bass intact as the resonance opens.
        makeup = 1.0f + k * 0.22f;
    }

    void LadderFilter::setDrive (float normalised) noexcept
    {
        drive = 1.0f + juce::jlimit (0.0f, 1.0f, normalised) * 3.5f;
    }

    float LadderFilter::process (float x) noexcept
    {
        // Zero-delay solution.  Each stage contributes g to the loop, so four
        // cascaded stages contribute g^4; the feedback that would arrive with
        // no input is the sum of the stage states weighted by how much of each
        // survives to the output.
        const float g2 = g * g;
        const float g3 = g2 * g;
        const float g4 = g2 * g2;

        const float S = g3 * (1.0f - g) * s[0]
                      + g2 * (1.0f - g) * s[1]
                      + g  * (1.0f - g) * s[2]
                      +      (1.0f - g) * s[3];

        const float denom = 1.0f + k * g4;

        // The input is saturated, and so is the feedback, and they are
        // saturated separately: a single saturator on their sum would make the
        // resonance fold the input rather than compress itself.
        const float in = tanhFast (x * drive * makeup);
        const float u  = (in - k * S) / juce::jmax (0.05f, denom);

        float v = u;

        for (int i = 0; i < 4; ++i)
        {
            const float w = (v - s[i]) * g;
            const float y = w + s[i];
            s[i] = flush (y + w);
            v = y;
        }

        // Guard.  The division above is bounded and tanhFast clamps, so this
        // should be unreachable - but "should be" is not a guarantee at 96 kHz
        // with the cutoff being swept under full drive, and one NaN here would
        // otherwise persist for the lifetime of the voice.
        if (! sane (v))
        {
            reset();
            return 0.0f;
        }

        return v / drive;
    }

    // =======================================================================
    //  StateVariableFilter
    // =======================================================================
    void StateVariableFilter::prepare (double sampleRate) noexcept
    {
        sr = juce::jmax (1.0, sampleRate);
        setCutoff (8000.0f);
        setResonance (0.0f);
        setDrive (0.0f);
        reset();
    }

    void StateVariableFilter::reset() noexcept
    {
        ic1 = ic2 = 0.0f;
    }

    void StateVariableFilter::setCutoff (float hz) noexcept
    {
        g = tanPrewarp ((float) (juce::jlimit (20.0, sr * 0.45, (double) hz) / sr));
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void StateVariableFilter::setResonance (float normalised) noexcept
    {
        // k is 1/Q.  Q runs from 0.5 (gently damped, no peak at all) to 14,
        // which rings without ever self-oscillating - the HAZE filter is the
        // smooth one, and a self-oscillating pad filter is a different
        // instrument.
        const float q = 0.5f + juce::jlimit (0.0f, 1.0f, normalised) * 13.5f;
        k = 1.0f / q;

        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void StateVariableFilter::setDrive (float normalised) noexcept
    {
        drive = 1.0f + juce::jlimit (0.0f, 1.0f, normalised) * 2.2f;
    }

    float StateVariableFilter::process (float x, FilterType type) noexcept
    {
        const float in = drive > 1.001f ? tanhFast (x * drive) / drive : x;

        const float v3 = in - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;

        ic1 = flush (2.0f * v1 - ic1);
        ic2 = flush (2.0f * v2 - ic2);

        float y;

        switch (type)
        {
            case FilterType::highpass: y = in - k * v1 - v2;  break;
            case FilterType::bandpass: y = v1;                break;
            case FilterType::notch:    y = in - k * v1;       break;
            case FilterType::lowpass:
            default:                   y = v2;                break;
        }

        if (! sane (y))
        {
            reset();
            return 0.0f;
        }

        return y;
    }

    // =======================================================================
    //  CreativeFilter
    // =======================================================================
    void CreativeFilter::Biquad::setBandpass (float hz, float q, double sampleRate) noexcept
    {
        const double f = juce::jlimit (20.0, sampleRate * 0.45, (double) hz);
        const float w = (float) (kTwoPi * f / sampleRate);
        const float sn = sineTurns ((float) (f / sampleRate));
        const float cs = cosineTurns ((float) (f / sampleRate));
        const float alpha = sn / (2.0f * juce::jmax (0.1f, q));

        const float a0 = 1.0f + alpha;
        const float inv = 1.0f / a0;

        b0 =  alpha * inv;
        b1 =  0.0f;
        b2 = -alpha * inv;
        a1 = -2.0f * cs * inv;
        a2 = (1.0f - alpha) * inv;

        juce::ignoreUnused (w);
    }

    void CreativeFilter::prepare (double sampleRate) noexcept
    {
        sr = juce::jmax (1.0, sampleRate);
        reset();
    }

    void CreativeFilter::reset() noexcept
    {
        comb.fill (0.0f);
        combWrite = 0;
        combState = 0.0f;

        for (auto& f : formants)
            f.reset();
    }

    float CreativeFilter::processComb (float x, float hz, float resonance) noexcept
    {
        const float f = juce::jlimit (20.0f, (float) (sr * 0.45), hz);
        const float delaySamples = juce::jlimit (2.0f, (float) (kCombSize - 4), (float) sr / f);

        const int   d0   = (int) delaySamples;
        const float frac = delaySamples - (float) d0;

        const int i0 = (combWrite - d0     + kCombSize) % kCombSize;
        const int i1 = (combWrite - d0 - 1 + kCombSize) % kCombSize;

        const float delayed = lerp (comb[(size_t) i0], comb[(size_t) i1], frac);

        // The feedback is damped and saturated. Damping stops the comb turning
        // into a metallic ring at high feedback; saturating bounds it even if
        // the damping coefficient and the feedback conspire.
        combState = lerp (delayed, combState, 0.35f);
        combState = flush (combState);

        const float fb = juce::jlimit (0.0f, 0.97f, resonance);
        const float y  = x + combState * fb;

        comb[(size_t) combWrite] = tanhFast (y);
        combWrite = (combWrite + 1) % kCombSize;

        if (! sane (y))
        {
            reset();
            return 0.0f;
        }

        return y * (1.0f - fb * 0.4f);
    }

    float CreativeFilter::processFormant (float x, float hz, float resonance, float morph) noexcept
    {
        // Five vowels, morphed through.  The ratios are relative to the filter
        // cutoff rather than absolute, so the formant character tracks the
        // control instead of only working at one pitch.
        static constexpr float vowelRatios[5][3] = {
            { 1.00f,  2.30f,  5.40f },   // A
            { 0.55f,  4.10f,  6.00f },   // E
            { 0.42f,  6.20f,  7.40f },   // I
            { 0.70f,  1.30f,  5.00f },   // O
            { 0.45f,  1.10f,  4.60f }    // U
        };

        const float m  = juce::jlimit (0.0f, 1.0f, morph) * 4.0f;
        const int   v0 = juce::jlimit (0, 4, (int) m);
        const int   v1 = juce::jmin (4, v0 + 1);
        const float vf = m - (float) v0;

        const float q = 3.0f + juce::jlimit (0.0f, 1.0f, resonance) * 18.0f;

        float y = 0.0f;
        static constexpr float formantGain[3] = { 1.0f, 0.65f, 0.35f };

        for (int i = 0; i < 3; ++i)
        {
            const float ratio = lerp (vowelRatios[v0][i], vowelRatios[v1][i], vf);
            formants[i].setBandpass (hz * ratio, q, sr);
            y += formants[i].process (x) * formantGain[i];
        }

        if (! sane (y))
        {
            reset();
            return 0.0f;
        }

        // Three bandpasses in parallel sum to less than their input; the trim
        // brings a formant patch back to the level of the other models.
        return y * 1.8f;
    }

    // =======================================================================
    //  SynthFilter
    // =======================================================================
    void SynthFilter::prepare (double sampleRate) noexcept
    {
        sr = juce::jmax (1.0, sampleRate);

        ladder.prepare (sr);
        svf.prepare (sr);
        creative.prepare (sr);

        dirty = true;
        updateActiveModel();
    }

    void SynthFilter::reset() noexcept
    {
        ladder.reset();
        svf.reset();
        creative.reset();
    }

    void SynthFilter::setModel (FilterModel m) noexcept
    {
        if (m != model) { model = m; dirty = true; }
    }

    void SynthFilter::setType (FilterType t) noexcept
    {
        if (t != type) { type = t; dirty = true; }
    }

    // Two things happen in every setter below, and both are about cost rather
    // than behaviour.
    //
    //  * Only the model that is actually selected gets new coefficients.  The
    //    voice calls these once per sample, per channel, and the block runs at
    //    twice the sample rate - so updating the model that is switched off was
    //    four wasted trig pairs and four wasted divides per sample.
    //
    //  * A value that has not moved does not recompute at all.  A held note
    //    with no filter modulation then costs nothing here, while a sweep still
    //    updates on every sample that actually differs.  The threshold is one
    //    twentieth of a cent, far below anything audible.

    void SynthFilter::updateActiveModel() noexcept
    {
        switch (model)
        {
            case FilterModel::mass:
                ladder.setCutoff (cutoffHz);
                ladder.setResonance (resonance);
                ladder.setDrive (driveNorm);

                // MASS builds its band-pass and notch from the state variable,
                // so that one needs coefficients too - but only for those two.
                if (type == FilterType::bandpass || type == FilterType::notch)
                {
                    svf.setCutoff (cutoffHz);
                    svf.setResonance (resonance);
                    svf.setDrive (driveNorm);
                }
                break;

            case FilterModel::haze:
                svf.setCutoff (cutoffHz);
                svf.setResonance (resonance);
                svf.setDrive (driveNorm);
                break;

            case FilterModel::comb:
            case FilterModel::formant:
                // Both read cutoffHz and resonance directly at process time.
                break;
        }

        dirty = false;
    }

    void SynthFilter::setCutoff (float hz) noexcept
    {
        // One clamp, here, before any coefficient is computed.  Every model
        // trusts that it will never be handed a cutoff at or above Nyquist,
        // which is what lets tanPrewarp stay division-safe.
        const float clamped = (float) juce::jlimit (20.0, sr * 0.45, (double) hz);

        if (std::abs (clamped - cutoffHz) > cutoffHz * 3.0e-5f)
        {
            cutoffHz = clamped;
            dirty = true;
        }
    }

    void SynthFilter::setResonance (float normalised) noexcept
    {
        const float clamped = juce::jlimit (0.0f, 1.0f, normalised);

        if (std::abs (clamped - resonance) > 1.0e-5f)
        {
            resonance = clamped;
            dirty = true;
        }
    }

    void SynthFilter::setDrive (float normalised) noexcept
    {
        const float clamped = juce::jlimit (0.0f, 1.0f, normalised);

        if (std::abs (clamped - driveNorm) > 1.0e-5f)
        {
            driveNorm = clamped;
            dirty = true;
        }
    }

    void SynthFilter::setMorph (float normalised) noexcept
    {
        morph = juce::jlimit (0.0f, 1.0f, normalised);
    }

    float SynthFilter::process (float x) noexcept
    {
        if (dirty)
            updateActiveModel();

        if (! sane (x))
            x = 0.0f;

        float y;

        switch (model)
        {
            case FilterModel::mass:
                // The ladder is a low pass by construction.  The other three
                // responses are built from it so that switching type does not
                // also switch character: a MASS high pass is still a MASS.
                switch (type)
                {
                    case FilterType::highpass: y = x - ladder.process (x); break;
                    case FilterType::bandpass: y = svf.process (x, FilterType::bandpass)
                                                   * 0.5f + ladder.process (x) * 0.5f; break;
                    case FilterType::notch:    y = x - svf.process (x, FilterType::bandpass); break;
                    case FilterType::lowpass:
                    default:                   y = ladder.process (x); break;
                }
                break;

            case FilterModel::comb:
                y = creative.processComb (x, cutoffHz, resonance);
                break;

            case FilterModel::formant:
                y = creative.processFormant (x, cutoffHz, resonance, morph);
                break;

            case FilterModel::haze:
            default:
                y = svf.process (x, type);
                break;
        }

        if (! sane (y))
        {
            reset();
            return 0.0f;
        }

        // Nothing downstream should ever see more than this from one voice.
        // It is far above any level the voice mixer produces, so it never
        // colours normal playing; it exists so that a pathological combination
        // of resonance, drive and modulation cannot hand the bus an impulse.
        return juce::jlimit (-8.0f, 8.0f, y);
    }
}
