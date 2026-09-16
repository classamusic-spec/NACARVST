#include "CrushEngine.h"

/**
    ======================================================================
    CRUSH - bit and rate reduction.  Specification section 86.
    ======================================================================

    THE ALGORITHM

        input
          -> dry line                     (latency compensation, see below)
          -> 2x upsample
          -> drive shaper                 blended, gain compensated
          -> 2x downsample
          -> sample and hold at the reduced rate, with jitter
               the held value is quantised at the instant it is taken
          -> makeup
          -> tilt (TONE)
          -> equal-power dry/wet

    WHY THE QUANTISER SITS INSIDE THE HOLD.  A converter quantises when it
    samples, not continuously, so the word-length reduction happens once per
    take and the hold repeats the value it produced.  That also makes the error
    feedback correct: it shapes the error between *consecutive taken samples*,
    which is the rate the error is actually generated at.  Quantising at the
    full rate on a held signal would feed the same error back repeatedly and
    turn a noise shaper into a slow integrator.

    BITS is a mid-tread quantiser with TPDF dither and first-order error
    feedback.  A bare round() produces error that is correlated with the signal,
    which at low bit depths is the difference between "lo-fi" and "broken" -
    the harmonics move with the pitch and it reads as a fault rather than a
    texture.  The dither decorrelates it and the error feedback pushes what is
    left towards the top of the spectrum.  Dither is scaled down below 8 bits
    and off below 3, because one LSB of dither at 2 bits is a quarter of full
    scale of white noise and would simply be louder than the music.  Mid-tread
    rather than mid-riser matters for one specific reason: it has a zero level,
    so silence stays silence.

    RATE is a sample and hold on the reduced grid.  IT IS NOT OVERSAMPLED AND
    MUST NOT BE.  A sample-and-hold is a deliberate undersampler; its images
    fold back into the audible band and that folding *is* the effect.  Putting
    a halfband around it would remove exactly the thing the control is for.
    This comment is here because a later reader will otherwise try to "fix" it.

    JITTER makes the hold period irregular.  Each take draws a new period from
    the deterministic fx::Rng, so the clock wobbles rather than dividing
    cleanly; the aliases smear instead of landing on fixed frequencies.

    DRIVE goes into the quantiser, which is the only place it makes sense: it
    pushes low-level material further up a scale whose steps are absolute, so
    quiet passages cross many more steps and actually sound quantised instead
    of disappearing between two of them.  It is compensated *after* the
    quantiser and the hold, by exactly the reciprocal of its small-signal gain,
    so turning it up never adds level - specification 148.  It is the one
    nonlinear stage here that is not supposed to alias, so it runs at 2x
    through the verified polyphase halfband from the synth core.

    CRUSH is the master intensity, and it scales the other four: bits towards
    24, rate towards the host rate in the log domain, jitter and drive towards
    zero.  At CRUSH = 0 the module is transparent, which is what makes the FX
    card's single knob mean something.

    ----------------------------------------------------------------------
    PARAMETER MAPPING

      CRUSH  crush_amount  master intensity; scales BITS, RATE, JITTER, DRIVE.
      BITS   crush_bits    1 .. 24, effective word length.
      RATE   crush_rate    500 Hz .. 48 kHz, effective sample rate, interpolated
                           logarithmically because that is how it is heard.
      JITTER crush_jitter  +-85 % of the hold period, irregular.
      DRIVE  crush_drive   up to x5 into the quantiser, exactly compensated.
      TONE   crush_tone    fx::Tilt after crushing, -1 dark .. +1 bright.
      MIX    crush_mix     equal-power dry/wet.  Checked here, not just by the
                           chain.

      crush_on is read here as well as by the chain, so the engine is still
      correct when it is driven directly by a test.

      Every parameter in the CRUSH group is read and used.  None is ignored.

    MACRO RESPONSE  (added to the parameter, never replacing it)

      macros.grit -> +0.25 CRUSH.  Character is the macro that means
                     "saturation and noise", and a bitcrusher is both.
      macros.age  -> +0.10 CRUSH.  Smaller, because age is a property of a
                     medium and a converter does not wear out.

      Crush ignores movement, scale, distance, wetBias, widthScale,
      alterAmount, Breath and every Pulse destination.  A converter does not
      drift, and a crusher that breathed would be a different effect.

    ----------------------------------------------------------------------
    THE LOW END  (specification 38, 40, 43)

    One clock and one dither generator serve both channels, and both channels
    run identical coefficients.  Identical input therefore gives identical
    output, so nothing here can decorrelate the two channels at any frequency,
    let alone below 130 Hz.  Independent per-channel clocks would have been the
    obvious mistake: they sound wider and they destroy mono compatibility.

    LATENCY

    The halfband round trip is exactly (taps - 1) / 2 = 9 samples at the base
    rate: the upsampler delays by `centre` = 9 high-rate samples and the
    downsampler by another 9, which is 18 high-rate samples, which is 9 low-rate
    samples.  It is an integer, so the dry path is delayed by the same integer
    and the two are sample-aligned; no comb, no fractional interpolation on the
    dry signal.  The module therefore has 9 samples - 0.19 ms at 48 kHz - of
    latency, which NacarEngine adds to the chain total and NacarProcessor
    reports to the host.

    REALTIME

    Sized in prepare().  No allocation, no locks, no logging, no juce::String
    after that.  The RNG is deterministic and re-seeded in reset(), so an
    offline render reproduces the realtime pass exactly.

    KNOWN LIMITATIONS

      * The oversampled drive path runs even when DRIVE is zero, so that the
        latency does not change with a parameter.  The cost is the halfband
        round trip's own error, which the synth's round-trip test measures at
        -85 dB, and about twenty multiplies per sample per channel.
      * The bit depth is a block-rate constant, so a fast BITS automation moves
        in block-sized steps.  They are steps in the *step size*, not in the
        signal, so they do not click, but a very fast sweep is not smooth.
      * DRIVE at maximum costs between 3 and 8 dB of level on loud material.
        The compensation is exact at small signal and tanh compresses above
        that, so a driven stage that does not get louder has to get quieter.
        That is the trade section 148 asks for, but it does mean DRIVE is not
        a level control and cannot be used as one.
      * At 1 bit the quantiser has three levels (-1, 0, +1) rather than two.
        A true two-level mid-riser quantiser has no zero and would make
        silence hiss.
      * Nobody has listened to this.  Everything above describes what the code
        does, not how it sounds.
*/

namespace nacar
{
    namespace
    {
        // The halfband round trip, in base-rate samples.  Derived in the
        // LATENCY note above; it is an integer only because the two phases of
        // a halfband are symmetric.
        constexpr int kOversampleLatency = (synth::kHalfbandTaps - 1) / 2;

        // fx::DelayLine::readInt (d) returns the sample written d - 1 samples
        // ago, because write() advances the index before anyone reads.  One
        // more, therefore, to land on a delay of exactly kOversampleLatency.
        constexpr int kDryTap = kOversampleLatency + 1;

        // Drive range.  x5 is +14 dB into the quantiser, which at 4 bits is
        // more than two extra octaves of step crossing, and it is as far as the
        // stage can go before the exact small-signal compensation starts
        // costing an unreasonable amount of level on loud material - see the
        // note about that under KNOWN LIMITATIONS.
        constexpr float kMaxDriveGain = 5.0f;

        /** Mid-tread quantiser with TPDF dither and first-order error
            feedback.  `error` is the state carried between takes. */
        forcedinline float quantise (float x, float step, float dither, float& error) noexcept
        {
            // Bound the input so the number of steps, and therefore the size of
            // the error, is bounded whatever arrives.
            const float clamped = juce::jlimit (-1.05f, 1.05f, x);

            const float v = clamped - error;
            const float d = v + dither;

            const float y = std::floor (d / step + 0.5f) * step;

            // The error is bounded to one step.  Without this an unstable
            // combination of a big dither and a huge step could let the
            // feedback grow; with it the loop cannot.
            error = juce::jlimit (-step, step, y - d);

            return juce::jlimit (-1.2f, 1.2f, y);
        }
    }

    // =======================================================================
    //  Channel
    // =======================================================================
    void CrushEngine::Channel::prepare (double sampleRate)
    {
        dry.prepare (kDryTap + 8);
        tilt.prepare (sampleRate);
        wetDc.prepare (sampleRate);
        reset();
    }

    void CrushEngine::Channel::reset() noexcept
    {
        up.reset();
        down.reset();
        dry.reset();
        tilt.reset();
        wetDc.reset();

        error = 0.0f;
        held  = 0.0f;
    }

    // =======================================================================
    //  CrushEngine
    // =======================================================================
    CrushEngine::CrushEngine() = default;
    CrushEngine::~CrushEngine() = default;

    int CrushEngine::getLatencySamples() const noexcept
    {
        return (synth::kHalfbandTaps - 1) / 2;
    }

    void CrushEngine::prepare (const EngineSpec& spec)
    {
        sr = juce::jmax (8000.0, spec.sampleRate);

        for (auto& c : channels)
            c.prepare (sr);

        reset();
    }

    void CrushEngine::reset()
    {
        for (auto& c : channels)
            c.reset();

        holdPhase  = 0.0f;
        holdPeriod = 1.0f;

        rng.setSeed (0x43525348u);      // 'CRSH'

        for (auto* s : { &engageSm, &crushSm, &bitsSm, &rateSm, &jitterSm, &driveSm,
                         &toneSm, &mixSm })
            s->reset();

        driveRamp.snap (1.0f);
        blendRamp.snap (0.0f);
        makeupRamp.snap (1.0f);
        dryRamp.snap (1.0f);
        wetRamp.snap (0.0f);

        quantStep   = 2.0f / 16777216.0f;
        ditherScale = 1.0f;
        basePeriod  = 1.0f;
        jitterDepth = 0.0f;
    }

    void CrushEngine::process (juce::AudioBuffer<float>& buffer,
                               const ParameterRegistry& params,
                               const MacroState& macros)
    {
        const int available = buffer.getNumSamples();
        const int n = macros.numSamples > 0 ? juce::jmin (macros.numSamples, available)
                                            : available;

        if (n <= 0 || buffer.getNumChannels() < 1)
            return;

        const int numCh = juce::jmin (2, buffer.getNumChannels());

        float* left  = buffer.getWritePointer (0);
        float* right = numCh > 1 ? buffer.getWritePointer (1) : left;

        const bool  enabled = params.flag (PID::crushOn);
        const float mixP    = juce::jlimit (0.0f, 1.0f, params.raw (PID::crushMix));

        // Switching off is a crossfade against the live input, not a ramp on
        // MIX.  At MIX 0 this module's output is its own dry tap, which is the
        // input delayed by nine samples; the bypassed output is the input.
        // Ramping the gain between two signals that are apart in TIME leaves
        // the step exactly where it was.
        const float engageTarget = enabled ? 1.0f : 0.0f;

        // -------------------------------------------------------------------
        //  BYPASS IS EXACT, ONCE THE CROSSFADE HAS RUN OUT.  The buffer is
        //  untouched.  The dry lines are still written, because the dry path is
        //  delayed by the halfband's latency: if the line went stale while the
        //  module was off, the first samples after it came back on would be
        //  nine samples of whatever was playing when it was switched off.
        // -------------------------------------------------------------------
        if (engageTarget <= 0.0f && engageSm.current <= 1.0e-4f)
        {
            for (int i = 0; i < n; ++i)
            {
                channels[0].dry.write (left[i]);
                channels[1].dry.write (right[i]);
            }

            engageSm.holdAt (0.0f);
            mixSm.holdAt (mixP);
            return;
        }

        // -- parameters, read once per block --------------------------------
        const float crushP = juce::jlimit (0.0f, 1.0f, params.raw (PID::crushAmount)
                                                        + macros.grit * 0.25f
                                                        + macros.age * 0.10f);
        const float bitsP   = juce::jlimit (1.0f, 24.0f, params.raw (PID::crushBits));
        const float rateP   = juce::jlimit (500.0f, 48000.0f, params.raw (PID::crushRate));
        const float jitterP = juce::jlimit (0.0f, 1.0f, params.raw (PID::crushJitter));
        const float driveP  = juce::jlimit (0.0f, 1.0f, params.raw (PID::crushDrive));
        const float toneP   = juce::jlimit (-1.0f, 1.0f, params.raw (PID::crushTone));

        const float coef = std::exp (-(float) n / (float) (0.020 * sr));

        crushSm.set (crushP, n, coef);

        const float crush = juce::jlimit (0.0f, 1.0f, crushSm.current);

        // CRUSH scales the others.  Bits interpolate linearly towards 24; the
        // rate interpolates in the log domain, because an octave of sample rate
        // is what is heard rather than a number of Hz; jitter and drive scale
        // straight down.
        const float effBits   = fx::lerp (24.0f, bitsP, crush);
        const float effJitter = jitterP * crush;
        const float effDrive  = driveP * crush;

        const float cleanRate = (float) sr;
        const float effRate = fx::exp2Fast (fx::lerp (fx::log2Fast (cleanRate),
                                                      fx::log2Fast (rateP), crush));

        bitsSm.set (effBits, n, coef);
        rateSm.set (effRate, n, coef);
        jitterSm.set (effJitter, n, coef);
        driveSm.set (effDrive, n, coef);
        toneSm.set (toneP, n, coef);

        // -- quantiser, block constants --------------------------------------
        const float bits = juce::jlimit (1.0f, 24.0f, bitsSm.current);
        quantStep = 2.0f / std::exp2 (bits);

        // One LSB of TPDF is right at 16 bits and absurd at 2: it would be a
        // quarter of full scale of white noise.  Fade it out below 8 bits and
        // switch it off below 3, where the error feedback alone is doing the
        // decorrelating.
        ditherScale = juce::jlimit (0.0f, 1.0f, (bits - 3.0f) / 5.0f);

        // -- the clock, block constants --------------------------------------
        basePeriod  = juce::jmax (1.0f, (float) sr / juce::jmax (20.0f, rateSm.current));
        jitterDepth = juce::jlimit (0.0f, 1.0f, jitterSm.current);

        // -- drive, per-sample ramps ------------------------------------------
        // The shaper is blended in by the drive amount rather than switched on,
        // so DRIVE = 0 is exactly the input: tanh is not the identity for a
        // signal near full scale, and a shaper that engages at a threshold
        // would step.  The makeup is exactly the reciprocal of the blend's
        // small-signal gain, which is lerp (1, g, d) because tanhFast has unit
        // slope at zero - so the stage can never add level.
        {
            const float d0 = juce::jlimit (0.0f, 1.0f, driveSm.ramp.at (0));
            const float d1 = juce::jlimit (0.0f, 1.0f, driveSm.ramp.at (n));

            const float g0 = 1.0f + d0 * (kMaxDriveGain - 1.0f);
            const float g1 = 1.0f + d1 * (kMaxDriveGain - 1.0f);

            driveRamp.set (g0, g1, n);
            blendRamp.set (d0, d1, n);
            makeupRamp.set (1.0f / fx::lerp (1.0f, g0, d0),
                            1.0f / fx::lerp (1.0f, g1, d1), n);
        }

        // -- dry / wet --------------------------------------------------------
        mixSm.set (mixP, n, coef);
        engageSm.set (engageTarget, n, coef);

        {
            float dg0, wg0, dg1, wg1;
            fx::dryWetGains (juce::jlimit (0.0f, 1.0f, mixSm.ramp.at (0)), dg0, wg0);
            fx::dryWetGains (juce::jlimit (0.0f, 1.0f, mixSm.ramp.at (n)), dg1, wg1);

            dryRamp.set (dg0, dg1, n);
            wetRamp.set (wg0, wg1, n);
        }

        // ===================================================================
        //  The sample loop
        // ===================================================================
        for (int i = 0; i < n; ++i)
        {
            const float g     = driveRamp.at (i);
            const float blend = blendRamp.at (i);
            const float make  = makeupRamp.at (i);
            const float tone  = toneSm.at (i);

            // -- the clock, shared by both channels --------------------------
            bool take = true;

            if (holdPeriod > 1.0f)
            {
                holdPhase += 1.0f;

                if (holdPhase >= holdPeriod)
                    holdPhase -= holdPeriod;
                else
                    take = false;
            }

            float dither = 0.0f;

            if (take)
            {
                // A new period for every take: the clock wobbles rather than
                // dividing cleanly.  Bounded to [1, 4 x base] so it can never
                // stop taking samples altogether.
                holdPeriod = juce::jlimit (1.0f, basePeriod * 4.0f,
                                           basePeriod * (1.0f + jitterDepth
                                                                * rng.nextBipolar() * 0.85f));

                // TPDF: the difference of two uniform draws.  One generator for
                // both channels - see THE LOW END above.
                if (ditherScale > 0.0f)
                    dither = (rng.next01() - rng.next01()) * quantStep * ditherScale;
            }

            // Both inputs are taken before either output is written, because a
            // mono buffer aliases left and right onto the same storage.
            const float in[2] = { left[i], right[i] };
            float out[2] = { 0.0f, 0.0f };

            for (int c = 0; c < 2; ++c)
            {
                auto& ch = channels[c];

                const float x = in[c];

                ch.dry.write (x);

                // -- drive, at 2x ---------------------------------------------
                float h0 = 0.0f, h1 = 0.0f;
                ch.up.process (x, h0, h1);

                h0 = fx::lerp (h0, fx::tanhFast (h0 * g), blend);
                h1 = fx::lerp (h1, fx::tanhFast (h1 * g), blend);

                const float shaped = ch.down.process (h0, h1);

                // -- quantise, then hold.  NOT OVERSAMPLED: see the note at the
                //    top of this file.  The aliasing is the effect.
                if (take)
                    ch.held = quantise (shaped, quantStep, dither, ch.error);

                float y = ch.held * make;

                y = ch.tilt.process (y, tone);
                y = ch.wetDc.process (y);

                const float dry = ch.dry.readInt (kDryTap);

                out[c] = fx::guard (dry * dryRamp.at (i) + y * wetRamp.at (i));

                // The engage crossfade, against the live input.
                const float engage = juce::jlimit (0.0f, 1.0f, engageSm.at (i));

                if (engage < 1.0f)
                    out[c] = fx::guard (fx::lerp (in[c], out[c], engage));
            }

            if (numCh > 1)
            {
                left[i]  = out[0];
                right[i] = out[1];
            }
            else
            {
                left[i] = 0.5f * (out[0] + out[1]);
            }
        }
    }
}
