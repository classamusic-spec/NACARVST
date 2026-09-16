#include "FilterFX.h"

/**
    ======================================================================
    FILTER - the chain filter.  Specification section 87.
    ======================================================================

    WHAT IS REUSED, AND WHY

    Every response this module offers already exists, measured and guarded, in
    nacar::synth::SynthFilter: LP, HP, BP and NOTCH from the TPT state
    variable, plus the comb and the formant bank.  Writing a seventh filter
    would have given the instrument two filter characters, and the specification
    is explicit that it should have one.  So this engine owns four SynthFilter
    instances - two per channel - and adds only what a chain filter needs that a
    voice does not.

    The one deliberate difference: the four standard responses use the HAZE
    model (the state variable), not MASS (the ladder).  Two reasons, both
    mechanical.  First, all four responses come out of the same two
    integrators, so morphing between them is continuous by construction.
    Second, the ladder always has a saturator in its feedback path - even at
    drive zero - and NacarBench measured that saturator at +9.4 dB of
    inharmonic energy, which is why the synth voice runs it at 2x.  A chain
    filter with a dry/wet control cannot be oversampled without either putting
    nine samples of latency into the dry path or comb-filtering the mix, so the
    honest answer is to use the response that is exactly linear when its drive
    is zero.

    THE ALGORITHM

        input
          -> drive          blended input saturation, unit small-signal gain
          -> filter A       the selected response
          -> filter B       the next response along, only when MORPH > 0
          -> equal-power crossfade between them
          -> equal-power dry/wet

    MORPH blends between adjacent responses in the order LP, HP, BP, NOTCH,
    COMB, FORMANT, wrapping from FORMANT back to LP, so the control always does
    something.  The two filters are fed the same input and run identical
    coefficients, so crossfading their outputs is the same operation as
    crossfading two taps of one filter - for the state variable the two really
    are taps of the same pair of integrators - and it also works for the comb
    and the formant bank, which are genuinely different structures.  The
    crossfade is equal power rather than linear: adjacent responses are
    partially complementary, and a linear crossfade of a complementary pair
    loses 6 dB in the middle, which is the hole every mix control is supposed
    not to have.

    MOTION is self-modulation of the cutoff, and it is deliberately not an LFO.
    Four sources are summed, none of them dominant:

        macros.breathAt (i)   organic, non-repeating, and the contract reserves
                              it for exactly this kind of slow parameter
        two slow sines        0.063 Hz and 0.101 Hz - incommensurate, so their
                              sum has no audible period
        a random walk         a new target every 64 samples, reached through a
                              0.9 s one-pole
        the filter's own      a 30 ms follower on the previous output opens the
        output                filter a little as the material gets louder

    That last one is a feedback path and it is bounded on purpose: the follower
    output is clamped to [0, 1] and scaled to at most +0.6 of an octave, the
    total cutoff is clamped to [20 Hz, 20 kHz] before it reaches a coefficient,
    and the map from level to cutoff is monotone and saturating.  The loop has
    one bounded fixed point; it cannot run away, and the worst it can do is
    move slowly.

    STEREO.  Two filter instances per channel, with identical coefficients and
    independent state.  The modulation - motion, the envelope follower, the
    crossfade gains - is computed once and shared, so the two channels are
    always processed identically and the filter cannot decorrelate them at any
    frequency.  What this must never be is one filter on the mono sum with the
    result re-applied as a gain ratio; that was a real bug in the synth voice,
    it was caught by measurement, and SynthVoice.cpp now runs a genuine filter
    per channel.

    ----------------------------------------------------------------------
    PARAMETER MAPPING

      MODE   fxfilter_mode    LP | HP | BP | NOTCH | COMB | FORMANT.
      CUTOFF fxfilter_cutoff  20 Hz .. 20 kHz, smoothed in the log domain so a
                              sweep is musical rather than linear in Hz.  In
                              COMB it is the comb's pitch; in FORMANT it is the
                              base the vowel ratios are taken from.
      RES    fxfilter_res     Q 0.5 .. 14 on the state variable, feedback up to
                              0.97 on the comb, 3 .. 21 on the formant peaks.
                              Every one of those bounds is inside SynthFilter,
                              and none of them reaches unity loop gain.
      DRIVE  fxfilter_drive   blended input saturation.  Applied here rather
                              than through SynthFilter::setDrive because the
                              state variable's internal drive switches on at a
                              threshold (drive > 1.001) and therefore steps as
                              the control leaves zero; blending from the clean
                              signal is continuous.  The curve is the same
                              tanh, and the blend has unit small-signal gain,
                              so DRIVE cannot add level.
      MORPH  fxfilter_morph   the crossfade described above.
      MOTION fxfilter_motion  depth of the self-modulation, up to +-1.6 octaves
                              from the combined source plus 0.6 from the
                              envelope.  It also drives the vowel position when
                              the response is FORMANT, so a formant patch moves
                              through vowels instead of sitting on one.
      MIX    fxfilter_mix     equal-power dry/wet.  Checked here, not just by
                              the chain.

      fxfilter_on is read here as well as by the chain, so the engine is still
      correct when it is driven directly by a test.

      Every parameter in the FILTER FX group is read and used.  None is ignored.

    MACRO RESPONSE  (added to the parameters, never replacing them)

      macros.movement                       -> +0.30 MOTION.
      macros.breathAt (i)                   -> one of the four motion sources.
      macros.pulseToFilter * macros.pulseAt (i)
                                            -> up to -1.8 octaves of cutoff on
                                               each pulse.  A kick makes the
                                               sound darker - specification 83.

      The filter ignores age, grit, scale, distance, wetBias, widthScale and
      alterAmount: none of them describes a filter, and Retro, Crush and the
      atmosphere modules already answer to them.

    ----------------------------------------------------------------------
    THE LOW END  (specification 38, 40, 43)

    Both channels see identical coefficients at every sample, so identical
    input gives identical output and nothing here can decorrelate them.  A high
    pass or a notch removes low end from both channels equally, which is the
    control doing its job, not width bought with the bass.

    REALTIME

    Sized in prepare().  No allocation, no locks, no logging, no juce::String
    after that.  The motion random walk comes from a deterministic fx::Rng
    re-seeded in reset(), so an offline render reproduces the realtime pass.
    The module has no latency.

    KNOWN LIMITATIONS

      * MORPH wraps from FORMANT to LP.  Sweeping MORPH to 1 on the last mode
        lands on the first, which is consistent but not obvious.
      * Filter B runs only while MORPH is above 1e-4, so it is silent and
        stale below that.  Its crossfade gain there is sin (0) = 0, so nothing
        of the stale state can be heard; it does mean the first samples after
        MORPH leaves zero are a filter starting from rest.
      * SynthFilter::CreativeFilter's comb buffer is 4096 samples, which at
        96 kHz limits the comb's lowest pitch to about 23.5 Hz rather than the
        20 Hz the cutoff control offers.  The clamp inside CreativeFilter keeps
        it safe; it is a range limit, not a bug, and it lives in the synth
        filter rather than here.
      * Each SynthFilter carries a 16 KB comb buffer whether or not the comb is
        selected, so this engine is about 66 KB of object.  That is fine on the
        heap and would not be fine on a small stack.
      * MOTION at full depth with RES near maximum sweeps a resonant peak
        across two octaves.  Nothing is unstable - the guards are in
        SynthFilter - but it is loud, and the specification's "controlled" is
        doing less work here than it does in Crush.
      * Nobody has listened to this.  Everything above describes what the code
        does, not how it sounds.
*/

namespace nacar
{
    namespace
    {
        constexpr int kControlInterval = 64;

        // The cutoff is assembled in the log domain and clamped there, before
        // it ever becomes a coefficient.  4.32 is 20 Hz, 14.29 is 20 kHz;
        // SynthFilter clamps again to 0.45 of Nyquist, which is what makes the
        // whole range safe at 44.1 kHz.
        constexpr float kMinCutoffLog2 = 4.32f;
        constexpr float kMaxCutoffLog2 = 14.29f;

        // The two slow internal sources, in Hz.  Incommensurate, so their sum
        // does not repeat on any period a listener could learn.
        constexpr float kSlow1Hz = 0.063f;
        constexpr float kSlow2Hz = 0.101f;

        constexpr float kMotionOctaves = 1.6f;   ///< from the combined source
        constexpr float kSelfOctaves   = 0.6f;   ///< from the envelope follower
        constexpr float kPulseOctaves  = 1.8f;   ///< a kick makes it darker
    }

    FilterFX::Response FilterFX::responseFor (int mode) noexcept
    {
        switch (juce::jlimit (0, kNumModes - 1, mode))
        {
            case 1:  return { synth::FilterModel::haze,    synth::FilterType::highpass };
            case 2:  return { synth::FilterModel::haze,    synth::FilterType::bandpass };
            case 3:  return { synth::FilterModel::haze,    synth::FilterType::notch    };
            case 4:  return { synth::FilterModel::comb,    synth::FilterType::lowpass  };
            case 5:  return { synth::FilterModel::formant, synth::FilterType::lowpass  };
            case 0:
            default: return { synth::FilterModel::haze,    synth::FilterType::lowpass  };
        }
    }

    FilterFX::FilterFX() = default;
    FilterFX::~FilterFX() = default;

    void FilterFX::prepare (const EngineSpec& spec)
    {
        sr = juce::jmax (8000.0, spec.sampleRate);

        for (auto& channel : filters)
            for (auto& f : channel)
                f.prepare (sr);

        // 0.9 s: the random walk drifts, it does not wobble.
        motionWalk.setTime (0.900f, sr);

        // 30 ms on the self-listening follower.  Fast enough to follow a
        // phrase, far too slow to follow a waveform - which is what keeps the
        // feedback path out of the audio band.
        selfEnv.setTime (0.030f, sr);

        reset();
    }

    void FilterFX::reset()
    {
        for (auto& channel : filters)
            for (auto& f : channel)
                f.reset();

        motionWalk.setValue (0.0f);
        selfEnv.setValue (0.0f);

        slowPhase1 = 0.0f;
        slowPhase2 = 0.41f;
        motionTarget = 0.0f;
        lastOutMono = 0.0f;
        controlCounter = 0;

        rng.setSeed (0x464C5452u);      // 'FLTR'

        cutoffSm.reset (12.55f);        // log2 (6 kHz), a neutral place to start
        resSm.reset (0.0f);
        driveSm.reset (0.0f);
        morphSm.reset (0.0f);
        motionSm.reset (0.0f);
        mixSm.reset (0.0f);

        gainARamp.snap (1.0f);
        gainBRamp.snap (0.0f);
        dryRamp.snap (1.0f);
        wetRamp.snap (0.0f);

        modeA = 0;
        modeB = 1;

        // The synth filter's own defaults are HAZE / low pass, which happens to
        // be response A of mode 0 - but response B never is, so the first block
        // always programs both.
        responsesDirty = true;
    }

    void FilterFX::process (juce::AudioBuffer<float>& buffer,
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

        const bool enabled = params.flag (PID::fxFilterOn);

        // Switching off ramps to zero mix rather than jumping to it.  A
        // resonant filter's wet signal can be far from its dry one, and the
        // whole point of the mix smoother is wasted if only one edge uses it.
        const float mixP = enabled ? juce::jlimit (0.0f, 1.0f, params.raw (PID::fxFilterMix))
                                   : 0.0f;

        // BYPASS IS EXACT, ONCE THE RAMP HAS ARRIVED.  Nothing to keep warm
        // here - the module has no latency and no delayed dry path - so the
        // buffer is simply left alone.
        if (mixP <= 0.0f && mixSm.current <= 1.0e-4f)
        {
            mixSm.holdAt (0.0f);
            return;
        }

        // -- parameters, read once per block --------------------------------
        const int mode = juce::jlimit (0, kNumModes - 1, params.choice (PID::fxFilterMode));

        const float cutoffHz = juce::jlimit (20.0f, 20000.0f,
                                             params.raw (PID::fxFilterCutoff));
        const float resP    = juce::jlimit (0.0f, 1.0f, params.raw (PID::fxFilterRes));
        const float driveP  = juce::jlimit (0.0f, 1.0f, params.raw (PID::fxFilterDrive));
        const float morphP  = juce::jlimit (0.0f, 1.0f, params.raw (PID::fxFilterMorph));
        const float motionP = juce::jlimit (0.0f, 1.0f, params.raw (PID::fxFilterMotion)
                                                          + macros.movement * 0.30f);

        const float coef = std::exp (-(float) n / (float) (0.020 * sr));

        cutoffSm.set (juce::jlimit (kMinCutoffLog2, kMaxCutoffLog2,
                                    fx::log2Fast (cutoffHz)), n, coef);
        resSm.set (resP, n, coef);
        driveSm.set (driveP, n, coef);
        morphSm.set (morphP, n, coef);
        motionSm.set (motionP, n, coef);
        mixSm.set (mixP, n, coef);

        // -- responses --------------------------------------------------------
        if (responsesDirty || mode != modeA)
        {
            responsesDirty = false;

            modeA = mode;
            modeB = (mode + 1) % kNumModes;

            const Response a = responseFor (modeA);
            const Response b = responseFor (modeB);

            for (auto& channel : filters)
            {
                channel[0].setModel (a.model);
                channel[0].setType (a.type);
                channel[1].setModel (b.model);
                channel[1].setType (b.type);
            }
        }

        // -- the morph crossfade ---------------------------------------------
        {
            float a0, b0, a1, b1;
            fx::dryWetGains (juce::jlimit (0.0f, 1.0f, morphSm.ramp.at (0)), a0, b0);
            fx::dryWetGains (juce::jlimit (0.0f, 1.0f, morphSm.ramp.at (n)), a1, b1);

            gainARamp.set (a0, a1, n);
            gainBRamp.set (b0, b1, n);
        }

        const bool useB = morphSm.ramp.at (0) > 1.0e-4f || morphSm.ramp.at (n) > 1.0e-4f;

        // -- dry / wet ---------------------------------------------------------
        {
            float d0, w0, d1, w1;
            fx::dryWetGains (juce::jlimit (0.0f, 1.0f, mixSm.ramp.at (0)), d0, w0);
            fx::dryWetGains (juce::jlimit (0.0f, 1.0f, mixSm.ramp.at (n)), d1, w1);

            dryRamp.set (d0, d1, n);
            wetRamp.set (w0, w1, n);
        }

        const float slowInc1 = kSlow1Hz / (float) sr;
        const float slowInc2 = kSlow2Hz / (float) sr;

        // ===================================================================
        //  The sample loop
        // ===================================================================
        for (int i = 0; i < n; ++i)
        {
            if (--controlCounter <= 0)
            {
                controlCounter = kControlInterval;
                motionTarget = rng.nextBipolar();
            }

            // -- MOTION --------------------------------------------------------
            slowPhase1 += slowInc1;
            slowPhase2 += slowInc2;

            if (slowPhase1 >= 1.0f) slowPhase1 -= 1.0f;
            if (slowPhase2 >= 1.0f) slowPhase2 -= 1.0f;

            const float slow = 0.6f * fx::sineTurns (slowPhase1)
                             + 0.4f * fx::sineTurns (slowPhase2);

            const float walk = motionWalk.process (motionTarget);

            // Four sources, none of them dominant.  One sine here would read
            // as an LFO, which is precisely what section 87 says this is not.
            const float motionSignal = juce::jlimit (-1.0f, 1.0f,
                                            0.42f * macros.breathAt (i)
                                          + 0.33f * slow
                                          + 0.25f * walk);

            // The filter listening to itself.  Bounded: the follower is
            // clamped into [0, 1] and scaled to at most kSelfOctaves, so this
            // path has a single bounded fixed point.
            const float selfLevel = juce::jlimit (0.0f, 1.0f,
                                        selfEnv.process (std::abs (lastOutMono)) * 3.0f);

            const float depth = motionSm.at (i);

            // A kick makes the sound darker - specification 83.
            const float pulseDark = macros.pulseToFilter * macros.pulseAt (i) * kPulseOctaves;

            const float cutoffLog2 = juce::jlimit (kMinCutoffLog2, kMaxCutoffLog2,
                                          cutoffSm.at (i)
                                        + depth * motionSignal * kMotionOctaves
                                        + depth * selfLevel * kSelfOctaves
                                        - pulseDark);

            const float hz = fx::exp2Fast (cutoffLog2);
            const float res = resSm.at (i);

            // The vowel position for the formant bank, if that is what is
            // selected: it moves with MOTION, so a formant patch travels
            // through vowels instead of sitting on one.
            const float vowel = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * motionSignal);

            // -- drive ----------------------------------------------------------
            // Blended rather than switched, so the control is continuous as it
            // leaves zero, and the blend's small-signal gain is exactly 1.
            const float dNorm = driveSm.at (i);
            const float dGain = 1.0f + dNorm * 3.2f;

            const float ga = gainARamp.at (i);
            const float gb = gainBRamp.at (i);
            const float dryGain = dryRamp.at (i);
            const float wetGain = wetRamp.at (i);

            const float in[2] = { left[i], right[i] };
            float out[2] = { 0.0f, 0.0f };

            for (int c = 0; c < 2; ++c)
            {
                const float x = in[c];

                const float shaped = dNorm > 0.0f
                    ? fx::lerp (x, fx::tanhFast (x * dGain) / dGain, dNorm)
                    : x;

                auto& fa = filters[c][0];

                fa.setCutoff (hz);
                fa.setResonance (res);
                fa.setMorph (vowel);

                float y = fa.process (shaped) * ga;

                if (useB)
                {
                    auto& fb = filters[c][1];

                    fb.setCutoff (hz);
                    fb.setResonance (res);
                    fb.setMorph (vowel);

                    y += fb.process (shaped) * gb;
                }

                out[c] = fx::guard (x * dryGain + y * wetGain);
            }

            lastOutMono = 0.5f * (out[0] + out[1]);

            if (numCh > 1)
            {
                left[i]  = out[0];
                right[i] = out[1];
            }
            else
            {
                left[i] = out[0];
            }
        }
    }
}
