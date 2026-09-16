#include "ShadowEngine.h"

#include <cmath>

/*
    =======================================================================
    SHADOW  -  specification section 93
    =======================================================================

    TOPOLOGY

        in -----------------------------------------------------> dry (untouched)
          \
           mono sum -> LENGTH delay line -> pitch shifter (two windowed,
                                            crossfaded read pointers)
                                                  |
                                            envelope lag
                                                  |
                              +-------------------+-------------------+
                              |                                       |
                         4 modulated allpasses (L)             4 modulated (R)
                              |                                       |
                              +----- cross-coupled smear tank --------+
                                                  |
                                    high pass, low pass, narrow
                                                  |
                                       three-band width stage
                                                  |
                        out = dry + shadow * LEVEL * pulse duck

    The dry path has nothing at all done to it.  Shadow adds; it never mixes.
    That is a different decision from Space, which shapes the direct sound
    because it is the room that sound is standing in - a shadow is an extra
    object in the picture, not a property of the picture, so the original must
    come out the way it went in.  It also makes bypass trivially exact.

    THE PITCH SHIFTER

    Overlap-add with two read pointers on one delay line, which is the
    standard time-domain shifter and is what section 93 asks for.

      - The read pointers are at base + p*W and base + frac(p + 0.5)*W, where
        p advances by (1 - ratio)/W per sample and W is the window length.  A
        pointer whose delay changes at rate (1 - ratio) plays back at ratio
        times the original speed, which is the transposition; when p wraps,
        that pointer jumps a whole window and the other one is at the middle
        of its window, carrying the signal.
      - The crossfade is a raised cosine on each pointer.  The two windows are
        half a period apart, so they sum to exactly 1.0 at every position -
        there is no amplitude ripple from the crossfade itself.  What there is
        instead is a comb between two copies of the same signal at different
        delays during the overlap, which is inherent to time-domain shifting
        and is why the result is a smeared duplicate rather than a clean
        transposition.  For a shadow that is the right side of the trade.

      - WINDOW LENGTH: 60 ms at small shifts, rising linearly to 120 ms at a
        full octave and staying there to +-24 semitones.

        The trade-off is the whole design of a shifter.  A long window smears
        transients, because a transient inside the window is heard twice, half
        a window apart.  A short window warbles, because the pointers wrap
        more often: the artefact rate is |1 - ratio| / W, which at +12
        semitones and a 60 ms window is 16.7 Hz - squarely in the range the
        ear hears as roughness - and at the same shift with a 120 ms window is
        8.3 Hz, which reads as slow movement instead.  At the extremes, +24
        semitones gives 25 Hz and -24 gives 6.3 Hz.

        Shadow is allowed to choose the long window because it is a blurred
        object by definition, and BLUR usually further masks the artefact.  A
        shifter for a lead line would choose the opposite.

      - At PITCH = 0 the two pointers would sit at fixed different delays and
        comb-filter the duplicate permanently, which is exactly what must not
        happen at the parameter's default.  So the shifter is crossfaded
        against a single unshifted tap over the first semitone: below 0.15
        semitones the duplicate is a clean delay, above 1.0 it is entirely the
        shifter, and between them it is a smoothed blend.  This also dodges
        the worst artefact case, which is a very small shift: at 1.03 ratio
        the wrap rate is 0.25 Hz and the result is a slow flange.

    THE ENVELOPE LAG

    Two envelope followers on the duplicate: one with a 1 ms attack and a
    60 ms release, one with a 55 ms attack and a 500 ms release.  Their ratio
    is 1 in the steady state, dips towards 0 at an onset and is clamped at 1
    during a decay.  The gain applied is

        1 - lagDepth * (1 - min(1, slow/fast))

    with lagDepth running 0.45 to 0.80 with DISTANCE.  So a transient is
    softened by 5 to 14 dB and swells back over about 55 ms, and the decay is
    untouched.  It is deliberately not a full duck: a shadow that vanishes
    completely on every attack reads as a broken gate, and a shadow with no
    lag at all reads as a chorus.  Percussive material is what makes the
    difference audible.

    BLUR SMEARS IN TIME

    Four allpasses per channel, 13.7 to 53.3 ms, different lengths on the two
    channels (this is also where the duplicate's stereo width comes from,
    since it was built from the mono sum), with delays that lengthen and
    coefficients that rise with BLUR, and each one modulated by its own slow
    LFO.  After them, a cross-coupled tank: each channel's feedback comes from
    the other channel's delay line through an allpass and a low pass.  At full
    blur the tank's feedback is 0.60 and its delays are 43 and 58 ms, which is
    a decay of roughly 800 ms - long enough to be a genuine smear in time
    rather than a filter, short enough that Shadow is not a second reverb.

    SITTING BEHIND

    DISTANCE is not a level control here either - LEVEL is the level control -
    so what DISTANCE does is:

      1. Low pass from 9 kHz down to 1.4 kHz.
      2. High pass from 120 Hz up to 320 Hz.  A distant small object has no
         bottom, and this is also what keeps Shadow out of the low end.
      3. Narrows the image by up to 55 %.
      4. Deepens the envelope lag from 0.45 to 0.80, so a distant shadow is
         also a slower, lazier one.
      5. Trims the level by up to 25 %, last and least.

    STABILITY BOUNDS

      - The only feedback path is the smear tank.  Its gain is 0.60 at most,
        and the cross-coupling matrix is a permutation, so the loop gain of
        the pair is also 0.60.  The allpass in the loop has unit magnitude at
        every frequency and the low pass has less, so neither can lift it.
      - No path in this engine can reach unity.  There is no freeze mode and
        nothing that approaches one.
      - The input is guarded on the way in, the shadow is checked for
        finiteness every sample, and a non-finite result zeroes the engine's
        state rather than poisoning the session.
      - Every delay read is clamped inside fx::DelayLine.  The source line is
        dimensioned for the longest LENGTH plus the longest window at the
        highest supported sample rate, so the shifter cannot read outside it
        at any combination of LENGTH and PITCH.
      - The envelope ratio's divisor has 1e-5 added to it and the result is
        clamped, so no parameter setting can divide by zero.

    TAIL LENGTH

    Worst case is LENGTH at 900 ms plus the tank's 800 ms decay plus the
    allpasses, so under two seconds.  Nothing here threatens the processor's
    32 second promise.

    MACRO RESPONSE

      macros.movement  +0.25 on BLUR and +0.5 ms on the blur modulation.
                       Section 73 puts Motion on Shadow's blur explicitly.
      macros.distance  +0.30 on DISTANCE, so World reaches the five cues
                       above rather than a wet level.
      macros.scale     +0.20 on LENGTH.  A bigger world puts the shadow
                       further behind.
      macros.wetBias   +0.15 on LEVEL, as an offset on an atmospheric mix.
      macros.widthScale multiplies the duplicate's width.
      macros.pulseToSpace * pulseAt(i) ducks the duplicate by up to 85 %, so
                       every pulse makes the sound drier.
      macros.breathAt(i) drifts LENGTH by +-0.3 %, which detunes the duplicate
                       by a few cents as it moves and is most of what stops it
                       sounding welded to the original.

      macros.age, grit and alterAmount are unused.

      The parameter is the gate: at shadow_level zero the engine returns the
      buffer untouched however far World is turned up.

    KNOWN LIMITATIONS

      - Nobody has listened to this.
      - The pitch shifter is a two-pointer time-domain shifter, so it smears
        transients and combs during the crossfade.  That is the technique, not
        a bug, but it does mean PITCH at small non-zero values is the least
        convincing part of the engine even with the crossfade-to-direct.
      - Changing LENGTH glides the read position rather than crossfading, so a
        fast LENGTH move sweeps the duplicate's pitch.  The smoother is set at
        400 ms to make that a feature rather than a click.
      - The duplicate is mono-sourced, so material that is already wide loses
        its image in the shadow.  Deliberate, but it does mean Shadow cannot
        duplicate a stereo field, only a sound.
      - There is no tempo sync on LENGTH.  The parameter list has no division
        for it, so adding one would mean adding a parameter.
      - The blur tank's delays are fixed rather than scaled by anything, so
        BLUR changes the density and decay of the smear but not its
        fundamental spacing.
      - CPU has not been measured.
      - INTEGRATION NOTE, resolved.  NacarEngine calls this engine on every
        block whether or not shadow_on is true, so the engine does see the
        block in which it was turned off and its own flush runs on that
        transition - on both routes, the power ring and LEVEL reaching zero.
        Nothing here depends on the chain gating, and the chain must not
        start.
*/

namespace nacar
{
    namespace
    {
        /** Diffusion allpass lengths, ms.  The two channels share no length:
            that difference is the only thing giving a mono-sourced duplicate
            any width at all. */
        constexpr float kBlurMs[2][4] =
        { { 13.7f, 21.3f, 31.9f, 47.1f },
          { 17.3f, 25.9f, 37.1f, 53.3f } };

        /** Blur modulation rates, Hz.  Slow, and mutually incommensurate. */
        constexpr float kBlurHz[2][4] =
        { { 0.061f, 0.089f, 0.127f, 0.173f },
          { 0.071f, 0.101f, 0.139f, 0.191f } };

        constexpr float kTankMs [2] = { 11.3f, 14.9f };   ///< allpass inside the tank
        constexpr float kSmearMs[2] = { 43.1f, 57.7f };   ///< the tank's delay lines

        constexpr float kMinLengthMs = 25.0f;
        constexpr float kMaxLengthMs = 900.0f;
        constexpr float kLengthRange = 5.1699f;    ///< log2 (900 / 25)

        constexpr float kMinWindowMs = 60.0f;
        constexpr float kMaxWindowMs = 120.0f;

        constexpr float kMaxBlurScale = 1.30f;
    }

    // =======================================================================
    //  Construction
    // =======================================================================
    ShadowEngine::ShadowEngine() = default;
    ShadowEngine::~ShadowEngine() = default;

    void ShadowEngine::prepare (const EngineSpec& spec)
    {
        sampleRate  = juce::jmax (8000.0, spec.sampleRate);
        msToSamples = (float) (sampleRate / 1000.0);

        const double designRate = juce::jmax (sampleRate, 96000.0);
        const float  designMs   = (float) (designRate / 1000.0);

        // The longest LENGTH plus the longest window, at the highest supported
        // rate: the shifter's second pointer reads a full window behind the
        // first, so this is the real worst case and not the delay alone.
        sourceLine.prepare ((int) ((kMaxLengthMs * 1.01f + kMaxWindowMs) * designMs) + 64);

        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < kBlur; ++k)
            {
                blurBase[c][k] = kBlurMs[c][k] * msToSamples;
                blur[c][k].prepare ((int) (kBlurMs[c][k] * kMaxBlurScale * designMs) + 256);
                blur[c][k].setDelay (blurBase[c][k]);
                blur[c][k].setCoefficient (0.5f);

                blurPhase[c][k] = (float) (c * kBlur + k) * 0.19f;
                blurInc[c][k]   = 0.0f;
            }

            tank[c].prepare ((int) (kTankMs[c] * designMs) + 64);
            tank[c].setDelay (kTankMs[c] * msToSamples);
            tank[c].setCoefficient (0.68f);

            smearLine[c].prepare ((int) (kSmearMs[c] * designMs) + 64);
            smearDelay[c] = kSmearMs[c] * msToSamples;

            smearLp[c].setCutoff (3200.0f, sampleRate);
            toneLp[c].setCutoff (9000.0f, sampleRate);
            toneHp[c].setCutoff (120.0f, sampleRate);

            split[c].prepare (170.0f, 2800.0f, sampleRate);
        }

        // Envelope follower coefficients.  1 ms / 60 ms against 55 ms / 500 ms:
        // the gap between the two attacks is the lag the ear reads as a second
        // object, and the gap between the releases is what makes it linger.
        attackFast  = 1.0f - std::exp (-1.0f / (float) (0.001 * sampleRate));
        releaseFast = 1.0f - std::exp (-1.0f / (float) (0.060 * sampleRate));
        attackSlow  = 1.0f - std::exp (-1.0f / (float) (0.055 * sampleRate));
        releaseSlow = 1.0f - std::exp (-1.0f / (float) (0.500 * sampleRate));

        prepared = true;

        reset();
    }

    void ShadowEngine::flushState() noexcept
    {
        sourceLine.reset();

        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < kBlur; ++k)
                blur[c][k].reset();

            tank[c].reset();
            smearLine[c].reset();
            smearLp[c].reset();
            toneLp[c].reset();
            toneHp[c].reset();
            split[c].reset();
        }

        envFast = 0.0f;
        envSlow = 0.0f;
    }

    void ShadowEngine::reset()
    {
        flushState();

        shiftPhase = 0.0f;

        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kBlur; ++k)
                blurPhase[c][k] = (float) (c * kBlur + k) * 0.19f;

        running = false;
    }

    // =======================================================================
    //  Process
    // =======================================================================
    void ShadowEngine::process (juce::AudioBuffer<float>& buffer,
                                const ParameterRegistry& registry,
                                const MacroState& macros)
    {
        const int numSamples = juce::jmin (macros.numSamples, buffer.getNumSamples());

        if (! prepared || numSamples <= 0 || buffer.getNumChannels() < 1)
            return;

        const float levelParam = registry.raw (PID::shadowLevel);

        // -- exact bypass -------------------------------------------------------
        //  The parameter is the gate.  Shadow is additive, so returning here
        //  leaves the buffer exactly as it arrived.
        if (! registry.flag (PID::shadowOn) || levelParam <= 1.0e-5f)
        {
            if (running)
            {
                flushState();
                running = false;
            }

            return;
        }

        const bool restart = ! running;
        running = true;

        // -- the registry, read exactly once --------------------------------------
        const float lengthP = juce::jlimit (0.0f, 1.0f, registry.raw (PID::shadowLength) + 0.20f * macros.scale);
        const float distP   = juce::jlimit (0.0f, 1.0f, registry.raw (PID::shadowDistance) + 0.30f * macros.distance);
        const float blurP   = juce::jlimit (0.0f, 1.0f, registry.raw (PID::shadowBlur) + 0.25f * macros.movement);
        const float pitchP  = juce::jlimit (-24.0f, 24.0f, registry.raw (PID::shadowPitch));
        const float levelP  = juce::jlimit (0.0f, 1.0f, levelParam + 0.15f * macros.wetBias);

        const float nf = (float) numSamples;
        const float aFast  = 1.0f - std::exp (-nf / (float) (0.020 * sampleRate));
        const float aSlow  = 1.0f - std::exp (-nf / (float) (0.120 * sampleRate));
        const float aCreep = 1.0f - std::exp (-nf / (float) (0.400 * sampleRate));

        // -- how far behind ---------------------------------------------------------
        const float lengthMs = kMinLengthMs * fx::exp2Fast (lengthP * kLengthRange);
        const float lengthTarget = lengthMs * msToSamples;

        // -- transposition ------------------------------------------------------------
        const float semitones = pitchP;
        const float ratio     = fx::exp2Fast (semitones * (1.0f / 12.0f));
        const float absSemis  = std::abs (semitones);

        const float windowMs = kMinWindowMs
                                  + (kMaxWindowMs - kMinWindowMs) * juce::jmin (1.0f, absSemis / 12.0f);
        const float windowTarget = windowMs * msToSamples;

        // The wrap rate.  Negative for an upward shift, which walks the read
        // pointers towards the write head.
        shiftInc = (1.0f - ratio) / juce::jmax (16.0f, windowTarget);

        const float blendTarget = juce::jlimit (0.0f, 1.0f, (absSemis - 0.15f) / 0.85f);

        // Skip the shifter's two reads entirely while it is inaudible.  The
        // smoothed blend, not the target, decides - otherwise the branch would
        // cut the shifter off before the crossfade had finished.
        shifting = blendTarget > 1.0e-4f || sBlend.value > 1.0e-4f;

        // -- blur ---------------------------------------------------------------------
        //  The coefficient starts near zero rather than at a comfortable 0.45,
        //  because BLUR at zero has to mean an unsmeared duplicate.  The cost
        //  is that a clean shadow is also a narrow one - the two channels only
        //  differ in their allpass lengths, so with the allpasses nearly
        //  transparent there is nothing left to decorrelate them.  That is the
        //  right way round: a sharp shadow is a single object, a blurred one
        //  spreads.
        const float blurCoeff = juce::jlimit (0.10f, 0.84f, 0.15f + 0.66f * blurP);

        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kBlur; ++k)
            {
                blur[c][k].setCoefficient (blurCoeff);
                blurInc[c][k] = kBlurHz[c][k] / (float) sampleRate;
            }

        blurDepth = (0.20f + 0.80f * blurP + 0.50f * macros.movement) * msToSamples;

        const float blurScaleTarget = juce::jlimit (0.50f, kMaxBlurScale, 0.55f + 0.70f * blurP);
        const float smearTarget     = blurP;                 // how much of the tank is heard
        const float feedbackTarget  = 0.60f * blurP;         // bounded well below unity

        // -- sitting behind --------------------------------------------------------------
        for (int c = 0; c < 2; ++c)
        {
            toneLp[c].setCutoff (9000.0f * fx::exp2Fast (-2.70f * distP), sampleRate);
            toneHp[c].setCutoff (120.0f + 200.0f * distP, sampleRate);
        }

        lagDepth = 0.45f + 0.35f * distP;

        const float widthTarget = juce::jlimit (0.0f, 2.0f,
                                                macros.widthScale * (1.0f - 0.55f * distP));

        const float levelTarget = levelP * 0.90f * (1.0f - 0.25f * distP);

        duckDepth = juce::jlimit (0.0f, 1.0f, macros.pulseToSpace) * 0.85f;

        sLength   .set (lengthTarget,         aCreep, numSamples, restart);
        sWindow   .set (windowTarget,         aSlow,  numSamples, restart);
        sBlend    .set (blendTarget,          aSlow,  numSamples, restart);
        sBlurScale.set (blurScaleTarget,      aSlow,  numSamples, restart);
        sSmear    .set (smearTarget,          aFast,  numSamples, restart);
        sFeedback .set (feedbackTarget,       aFast,  numSamples, restart);
        sLevel    .set (levelTarget,          aFast,  numSamples, restart);
        sWidth    .set (widthTarget,          aFast,  numSamples, restart);
        sWidthHigh.set (widthTarget * 1.10f,  aFast,  numSamples, restart);

        // -- the loop -----------------------------------------------------------------------
        float* chL = buffer.getWritePointer (0);
        float* chR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
        const bool stereo = chR != nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = fx::guard (chL[i]);
            const float inR = stereo ? fx::guard (chR[i]) : inL;

            const float duck  = 1.0f - duckDepth * macros.pulseAt (i);
            const float drift = 1.0f + macros.breathAt (i) * 0.003f;

            // ---- the duplicate is taken from the mono sum --------------------
            sourceLine.write (0.5f * (inL + inR));

            const float base = sLength.at (i) * drift;

            // ---- pitch shift --------------------------------------------------
            float voice = sourceLine.read (base);

            if (shifting)
            {
                shiftPhase += shiftInc;

                if (shiftPhase >= 1.0f)       shiftPhase -= 1.0f;
                else if (shiftPhase < 0.0f)   shiftPhase += 1.0f;

                const float window = sWindow.at (i);

                const float pA = shiftPhase;
                const float pB = pA >= 0.5f ? pA - 0.5f : pA + 0.5f;

                // Raised cosine on each pointer, half a period apart, so the
                // two windows sum to exactly one at every phase.
                const float gA = 0.5f - 0.5f * fx::cosineTurns (pA);
                const float gB = 1.0f - gA;

                const float shifted = gA * sourceLine.read (base + pA * window)
                                        + gB * sourceLine.read (base + pB * window);

                voice += (shifted - voice) * sBlend.at (i);
            }

            // ---- the envelope lags the original's ------------------------------
            const float rect = std::abs (voice);

            envFast += (rect - envFast) * (rect > envFast ? attackFast : releaseFast);
            envSlow += (rect - envSlow) * (rect > envSlow ? attackSlow : releaseSlow);

            const float ratioSlowFast = juce::jmin (1.0f, envSlow / (envFast + 1.0e-5f));

            voice *= 1.0f - lagDepth * (1.0f - ratioSlowFast);

            // ---- blur: smear in time, not just in frequency ---------------------
            float shL = voice;
            float shR = voice;

            const float blurScale = sBlurScale.at (i);

            for (int k = 0; k < kBlur; ++k)
            {
                blurPhase[0][k] += blurInc[0][k];
                blurPhase[1][k] += blurInc[1][k];

                if (blurPhase[0][k] >= 1.0f) blurPhase[0][k] -= 1.0f;
                if (blurPhase[1][k] >= 1.0f) blurPhase[1][k] -= 1.0f;

                blur[0][k].setDelay (blurBase[0][k] * blurScale);
                blur[1][k].setDelay (blurBase[1][k] * blurScale);

                shL = blur[0][k].process (shL, fx::sineTurns (blurPhase[0][k]) * blurDepth);
                shR = blur[1][k].process (shR, fx::sineTurns (blurPhase[1][k]) * blurDepth);
            }

            // Cross-coupled tank.  Reading the other channel's line is what
            // makes the smear wander across the image instead of building up
            // twice in the same place.
            const float fb = sFeedback.at (i);

            const float fbL = smearLp[0].lowpass (smearLine[1].readLinear (smearDelay[1])) * fb;
            const float fbR = smearLp[1].lowpass (smearLine[0].readLinear (smearDelay[0])) * fb;

            const float tankL = tank[0].process (shL + fbL);
            const float tankR = tank[1].process (shR + fbR);

            smearLine[0].write (tankL);
            smearLine[1].write (tankR);

            const float smear = sSmear.at (i);

            shL += (tankL - shL) * smear;
            shR += (tankR - shR) * smear;

            // ---- sit it behind ---------------------------------------------------
            shL = toneLp[0].lowpass (toneHp[0].highpass (shL));
            shR = toneLp[1].lowpass (toneHp[1].highpass (shR));

            // ---- the low end stays put ---------------------------------------------
            //  The high pass above has already taken most of it; this makes
            //  whatever is left perfectly correlated, so a shadow can never
            //  cancel the original's bottom when the mix is summed to mono.
            float lowL, midL, highL, lowR, midR, highR;
            split[0].split (shL, lowL, midL, highL);
            split[1].split (shR, lowR, midR, highR);

            const float lowMono = 0.5f * (lowL + lowR);

            const float midM = 0.5f * (midL + midR);
            const float midS = 0.5f * (midL - midR) * sWidth.at (i);
            const float hiM  = 0.5f * (highL + highR);
            const float hiS  = 0.5f * (highL - highR) * sWidthHigh.at (i);

            shL = lowMono + (midM + midS) + (hiM + hiS);
            shR = lowMono + (midM - midS) + (hiM - hiS);

            if (! fx::sane (shL + shR))
            {
                flushState();
                shL = 0.0f;
                shR = 0.0f;
            }

            const float g = sLevel.at (i) * duck;

            chL[i] = fx::guard (inL + shL * g);

            if (stereo)
                chR[i] = fx::guard (inR + shR * g);
        }
    }
}
