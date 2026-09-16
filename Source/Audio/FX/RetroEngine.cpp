#include "RetroEngine.h"

/**
    ======================================================================
    RETRO - the medium and the playback machine.  Specification section 85.
    ======================================================================

    THE ALGORITHM

    One signal path, ordered the way a real machine orders it.  Everything
    downstream of the transport is per channel; the transport itself is shared,
    because a machine has one capstan and one converter clock.

        input
          -> transport delay line                write
          -> dry tap at the nominal delay         (see LATENCY below)
          -> wet tap at nominal + wow + flutter + scrape    speed error
          -> low cut                              the medium's LF limit
          -> saturation                           era curve, gain compensated
          -> dynamic low pass                     bandwidth, level dependent
          -> resampling grid                      only near the digital era
          -> three-band split
               low   : common-mode dropout gain only
               mid   : per-channel dropout, partial mono collapse
               high  : per-channel dropout, mono collapse, azimuth trim
          -> medium noise + common-mode rumble
          -> tilt (TONE)
          -> DC blocker, guard
          -> equal-power dry/wet

    ERA is the primary control and it selects *which machine*, morphing
    continuously across four named characters.  Each has its own bandwidth,
    noise colour, wow and flutter rates and depths, saturation curve and
    channel behaviour:

        0.00  ACETATE 1948    direct-cut lacquer / optical film.  Narrow
                              (130 Hz - 4.5 kHz), heavy asymmetric saturation,
                              deep slow wow from a disc that is not quite
                              concentric, impulsive surface noise, turntable
                              rumble, and an image that is very nearly mono
                              because the medium was.
        0.33  VALVE TAPE 1958 quarter-inch at 15 ips through a tube machine.
                              Wide and warm (45 Hz - 11 kHz), soft symmetric
                              compression, gentle wow, low flutter, pink hiss.
        0.67  CASSETTE 1979   ferric C90, Dolby off.  60 Hz - 8 kHz, strong
                              capstan flutter and scrape, bright hiss, and the
                              one real azimuth problem in the set: the high end
                              of the two channels drifts apart.
        1.00  SAMPLER 1987    12-bit, 26 kHz converter.  Stable transport, hard
                              converter clipping, a resampling grid that
                              aliases on purpose, quiet dither-like noise and a
                              rock-solid stereo image.

    WOW AND FLUTTER ARE DIFFERENT MECHANISMS and are built as such.  Wow is the
    slow one (0.65 - 1.6 Hz here), made from two incommensurate oscillators so
    it does not repeat audibly, with its own rate wandering slowly; flutter is
    the fast one (7 - 14 Hz); scrape is filtered noise, which is what stops the
    result sounding like two sine waves on a delay line.  All three are
    specified as a peak *fractional pitch deviation* and converted to a delay
    excursion using the real sample rate, so the same setting means the same
    musical amount at 44.1 and at 96 kHz.  The transport modulation is
    identical in both channels - one capstan - which is also why it cannot
    decorrelate the low end.

    DROPOUTS AND CHANNEL INSTABILITY (WEAR) are a memoryless scheduler running
    at control rate.  An event takes a channel's level and top end down for
    8 - 200 ms; a little over half of them hit one channel only.  The gain and
    the high-frequency trim are both reached through a 6 ms one-pole, so an
    event can never click.  The rate is wear^1.7 * 4.5 events per second, so at
    WEAR = 0.2 that is one every three or four seconds - character - and at
    WEAR = 1 it is a machine that needs servicing.  Underneath it, a slow
    random walk per channel pulls the high end of one channel against the
    other: azimuth.

    NOISE IS NOT A BED.  It takes the colour of the ERA - the band-pass corners
    around it are interpolated with everything else in the profile - and it is
    modulated by the material through the one mechanism that is physically
    real: tape's modulation noise, which rises with the recorded level.  It is
    deliberately not ducked when the music plays.  Ducking a noise floor is a
    gate, a medium has no gate, and the reason a real medium's noise is most
    audible in the gaps is masking, which happens in the listener rather than
    in the code.  Impulsive surface events near the acetate end are generated,
    not looped - there is no vinyl sample anywhere in this file, and no static
    loop.

    ----------------------------------------------------------------------
    PARAMETER MAPPING

      ERA    retro_era    morphs the whole profile above.  Not a mix, not an
                          amount.
      AGE    retro_age    saturation drive and dynamic HF loss: how used-up the
                          medium is.
      DRIFT  retro_drift  wow, flutter and scrape depth together.  At 0 the
                          read head sits exactly on the nominal delay and the
                          transport is perfectly steady.
      WEAR   retro_wear   dropout rate and depth, and azimuth wander.
      TONE   retro_tone   fx::Tilt, -1 dark .. +1 bright, level-neutral.
      NOISE  retro_noise  the medium's noise floor, its crackle and its rumble.
      MIX    retro_mix    equal-power dry/wet.  Checked here, not just by the
                          chain.

      retro_on is read here as well as by the chain, so the engine is still
      correct when it is driven directly by a test.

      Every parameter in the RETRO group is read and used.  None is ignored.

    MACRO RESPONSE  (added to the parameters, never replacing them)

      macros.age         -> +0.25 AGE and +0.30 WEAR.  Character and Memory
                            together mean "how used-up", which for a machine is
                            saturation, bandwidth and the state of the medium.
      macros.grit        -> +0.25 NOISE and a little extra saturation drive.
      macros.movement    -> +0.30 DRIFT.  Motion is what makes a transport less
                            steady.
      macros.alterAmount -> shifts ERA by up to +0.34, one whole era, so a
                            patch's alternate identity can be a different
                            machine.
      macros.breathAt(i) -> +-18 % on the wow depth.  Slow, organic and
                            non-repeating, which is what a speed error wants
                            and what the contract reserves Breath for.

      Retro deliberately ignores macros.scale, distance, wetBias, widthScale
      and every Pulse destination: none of them describes a playback machine.

    ----------------------------------------------------------------------
    THE LOW END  (specification 38, 40, 43)

    Nothing in this engine is allowed to buy width with the low end.

      * the transport modulation is common to both channels, so it cannot
        decorrelate anything;
      * dropouts apply their *common* part (the smaller of the two channel
        gains) to the low band on both channels identically, and only the
        per-channel difference reaches mid and high;
      * the mono collapse and the azimuth trim touch mid and high only;
      * the medium's noise is high-passed twice above the low crossover, and
        its low-frequency component - rumble - is generated once and added
        identically to both channels.

    LATENCY

    Wow and flutter are a pitch modulation, and a pitch modulation needs a
    delay line with a nominal offset to wander around.  The dry path is taken
    from the *same* delay line at exactly that nominal offset, so dry and wet
    stay time-aligned and mixing them does not comb.  The cost is 4 ms of
    latency through the module whenever it is not bypassed.  That is a real
    number and it is listed under KNOWN LIMITATIONS.

    REALTIME

    Everything is sized in prepare().  Nothing here allocates, locks, logs or
    touches a juce::String.  Every random quantity comes from one deterministic
    fx::Rng seeded in prepare() and re-seeded in reset(), so a session and an
    offline render produce identical audio.

    KNOWN LIMITATIONS

      * 4 ms of latency when the module is active.  NacarEngine adds it to the
        chain total and NacarProcessor reports it to the host, but the figure
        moves when the module is switched on or off, so a host that reads
        latency only once will be wrong until it re-reads.
      * The ON edge moves the dry path in time by those same 4 ms, because the
        bypassed path is the undelayed input and the active path's dry tap is
        the input delayed by the nominal offset.  Ramping the mix does not help;
        it is the dry path itself that moves.
      * The resampling grid near the SAMPLER era aliases and is not
        oversampled.  That is deliberate - it is what a 26 kHz converter did -
        but it means ERA near 1 is not a clean stage.
      * The mono collapse at the ACETATE end reduces stereo width.  That is the
        medium, and reducing width is always safe under 38/40/43, but a patch
        that needs its width back has to leave ERA below about 0.2.
      * The module outputs 4 ms of silence after prepare() or reset(), because
        both the dry tap and the wet tap read from a transport that has not
        been written yet.  It is the same 4 ms as the latency and it only
        happens once per rate change.
      * The dropout scheduler is memoryless, so two events can overlap.  In
        practice that reads as one longer event; it is not modelled as such.
      * The resampling grid crossfades with the un-held signal as ERA morphs
        into it, rather than the converter's rate itself sliding.  A partial
        blend of held and live signal is not something any real machine does.
      * Nobody has listened to this.  Every claim above is a claim about what
        the code does, not about how it sounds.
*/

namespace nacar
{
    namespace
    {
        // Control rate for the scheduler and the slow random walks.  32 samples
        // is 0.67 ms at 48 kHz and 0.33 ms at 96 kHz; every probability below
        // is scaled by the real interval in seconds, so the event *rate* is the
        // same at every sample rate.
        constexpr int kControlInterval = 32;

        // The transport's centre tap.  It has to be further out than the read
        // head can wander, and small enough that the latency it costs is not a
        // musical problem.
        constexpr float kNominalDelaySeconds = 0.004f;
        constexpr float kExcursionHeadroom   = 0.875f;   ///< fraction of the nominal

        // Amplitude of the medium's noise at NOISE = 1 on the noisiest era.
        // The white source is uniform (RMS 0.577) and the era's band pass
        // costs about 12 dB more, so 0.0125 lands the hiss near -49 dBFS RMS:
        // clearly audible in a gap, nowhere near loud enough to be a level.
        // The crackle figure is in the noise source's own units, before that
        // band pass, and peaks around -32 dBFS.
        constexpr float kNoiseScale   = 0.0125f;
        constexpr float kCrackleScale = 8.0f;
        constexpr float kRumbleScale  = 0.25f;   ///< about -45 dBFS RMS at full

        // White noise through a one-pole has an RMS proportional to
        // sqrt (cutoff / fs), so the hiss, the rumble and the scrape would all
        // be 3 dB quieter at 96 kHz than at 48.  Everything generated from
        // noise is scaled by this, which is 1 at 48 kHz.
        constexpr double kReferenceRate = 48000.0;

        constexpr float kLowCrossoverHz  = 130.0f;   ///< matches the synth's stereo stage
        constexpr float kHighCrossoverHz = 2500.0f;

        // Where in the ERA morph the converter's resampling grid appears.  It
        // is its own quantity rather than a reuse of `digitalness`, because
        // digitalness is the shape of the saturation curve and the grid is a
        // different mechanism that only the last machine has.
        constexpr float kResampleStart = 0.70f;
    }

    // =======================================================================
    //  The four machines
    //
    //  Every number is a behaviour, not a taste: a bandwidth, a pitch
    //  deviation, a noise corner.  ERA interpolates between adjacent rows, so
    //  a value halfway between CASSETTE and SAMPLER really is a machine
    //  halfway between them rather than a crossfade of two outputs.
    // =======================================================================
    RetroEngine::EraProfile RetroEngine::eraAt (float position) noexcept
    {
        //                      name          lowCut   bandwidth  hfLoss  sat    bias   digital
        //                      wowHz  wowDev   flutHz  flutDev  scrape
        //                      nLow    nHigh    nLevel  crackle  rumble
        //                      mono    azimuth  resampleHz  bits   trim
        static constexpr EraProfile table[4] =
        {
            { "ACETATE 1948",    130.0f,  4500.0f, 0.55f, 0.55f, 0.28f, 0.00f,
                                   0.90f, 0.0180f,  7.0f, 0.0040f, 1.00f,
                                 300.0f,  5000.0f, 1.00f,   9.0f,  0.90f,
                                   0.85f, 0.10f,       0.0f,  0.0f, 1.10f },

            { "VALVE TAPE 1958",  45.0f, 11000.0f, 0.30f, 0.70f, 0.12f, 0.00f,
                                   0.65f, 0.0100f,  9.0f, 0.0025f, 0.55f,
                                 200.0f,  9000.0f, 0.65f,   0.5f,  0.35f,
                                   0.25f, 0.20f,       0.0f,  0.0f, 1.00f },

            { "CASSETTE 1979",    60.0f,  8000.0f, 0.45f, 0.50f, 0.06f, 0.15f,
                                   1.60f, 0.0120f, 11.0f, 0.0060f, 0.90f,
                                 500.0f, 14000.0f, 1.00f,   0.8f,  0.20f,
                                   0.10f, 0.75f,       0.0f,  0.0f, 1.02f },

            { "SAMPLER 1987",     25.0f, 11000.0f, 0.08f, 0.35f, 0.00f, 0.95f,
                                   0.20f, 0.0008f, 14.0f, 0.0004f, 0.10f,
                                 900.0f, 16000.0f, 0.30f,   0.0f,  0.00f,
                                   0.00f, 0.05f,   26040.0f, 12.0f, 1.00f }
        };

        const float p  = juce::jlimit (0.0f, 1.0f, position) * 3.0f;
        const int   i0 = juce::jlimit (0, 2, (int) p);
        const float f  = juce::jlimit (0.0f, 1.0f, p - (float) i0);

        const EraProfile& a = table[i0];
        const EraProfile& b = table[i0 + 1];

        EraProfile e {};

        e.name = (f < 0.5f) ? a.name : b.name;

        auto mix = [f] (float x, float y) noexcept { return fx::lerp (x, y, f); };

        e.lowCutHz         = mix (a.lowCutHz,         b.lowCutHz);
        e.bandwidthHz      = mix (a.bandwidthHz,      b.bandwidthHz);
        e.hfLoss           = mix (a.hfLoss,           b.hfLoss);
        e.satAmount        = mix (a.satAmount,        b.satAmount);
        e.satBias          = mix (a.satBias,          b.satBias);
        e.digitalness      = mix (a.digitalness,      b.digitalness);
        e.wowHz            = mix (a.wowHz,            b.wowHz);
        e.wowDeviation     = mix (a.wowDeviation,     b.wowDeviation);
        e.flutterHz        = mix (a.flutterHz,        b.flutterHz);
        e.flutterDeviation = mix (a.flutterDeviation, b.flutterDeviation);
        e.scrapeAmount     = mix (a.scrapeAmount,     b.scrapeAmount);
        e.noiseLowHz       = mix (a.noiseLowHz,       b.noiseLowHz);
        e.noiseHighHz      = mix (a.noiseHighHz,      b.noiseHighHz);
        e.noiseLevel       = mix (a.noiseLevel,       b.noiseLevel);
        e.cracklePerSecond = mix (a.cracklePerSecond, b.cracklePerSecond);
        e.rumbleLevel      = mix (a.rumbleLevel,      b.rumbleLevel);
        e.monoAmount       = mix (a.monoAmount,       b.monoAmount);
        e.azimuthAmount    = mix (a.azimuthAmount,    b.azimuthAmount);
        e.outputTrim       = mix (a.outputTrim,       b.outputTrim);

        // The converter's rate and word length are not interpolated towards
        // zero: a machine either has a grid or it has not.  How much of it is
        // heard is decided separately, by kResampleStart.
        e.resampleHz   = table[3].resampleHz;
        e.resampleBits = table[3].resampleBits;

        return e;
    }

    // =======================================================================
    //  Channel
    // =======================================================================
    void RetroEngine::Channel::prepare (double sampleRate, int maxDelaySamples)
    {
        line.prepare (maxDelaySamples);

        lowCut.setCutoff (60.0f, sampleRate);
        bandLimit.setCutoff (8000.0f, sampleRate);

        // 3 ms: fast enough to follow an attack, slow enough that the
        // bandwidth does not modulate at audio rate and make sidebands.
        levelEnv.setTime (0.003f, sampleRate);

        // 90 ms: the window over which "is anything playing" is decided for the
        // noise.  Shorter than this and the noise pumps.
        gapEnv.setTime (0.090f, sampleRate);

        bands.prepare (kLowCrossoverHz, kHighCrossoverHz, sampleRate);

        noiseHp1.setCutoff (200.0f, sampleRate);
        noiseHp2.setCutoff (200.0f, sampleRate);
        noiseLp.setCutoff (8000.0f, sampleRate);

        // 6 ms on both dropout smoothers.  This is the fade section 85 asks
        // for: a dropout that steps is a click, and a click is the one thing a
        // dropout must never be.
        dropGain.setTime (0.006f, sampleRate);
        dropHf.setTime (0.006f, sampleRate);

        // 0.4 s: azimuth drifts, it does not jump.
        azimuth.setTime (0.400f, sampleRate);

        tilt.prepare (sampleRate);
        dc.prepare (sampleRate);

        reset();
    }

    void RetroEngine::Channel::reset() noexcept
    {
        line.reset();
        lowCut.reset();
        bandLimit.reset();
        levelEnv.reset();
        gapEnv.reset();
        bands.reset();
        noiseHp1.reset();
        noiseHp2.reset();
        noiseLp.reset();

        dropGain.setValue (1.0f);
        dropHf.setValue (1.0f);
        azimuth.setValue (0.0f);

        tilt.reset();
        dc.reset();

        dropTargetGain = 1.0f;
        dropTargetHf   = 1.0f;
        dropRemaining  = 0;
        azimuthTarget  = 0.0f;
        held           = 0.0f;
    }

    // =======================================================================
    //  RetroEngine
    // =======================================================================
    RetroEngine::RetroEngine() = default;
    RetroEngine::~RetroEngine() = default;

    int RetroEngine::getLatencySamples() const noexcept
    {
        return nominalDelay;
    }

    void RetroEngine::prepare (const EngineSpec& spec)
    {
        sr = juce::jmax (8000.0, spec.sampleRate);

        nominalDelay = juce::jmax (8, (int) (kNominalDelaySeconds * sr));
        maxExcursion = (float) nominalDelay * kExcursionHeadroom;

        // Capacity: the furthest read is nominal + excursion, plus the samples
        // the Hermite interpolator needs either side of it.
        const int maxDelaySamples = nominalDelay + (int) maxExcursion + 8;

        for (auto& c : channels)
            c.prepare (sr, maxDelaySamples);

        // Scrape is filtered noise.  Two 120 Hz one-poles give it a spectrum
        // that is dense but has no tone of its own, which is the point.
        scrapeLp1.setCutoff (120.0f, sr);
        scrapeLp2.setCutoff (120.0f, sr);

        // Rumble sits below everything the instrument plays.
        rumbleLp1.setCutoff (32.0f, sr);
        rumbleLp2.setCutoff (32.0f, sr);

        wowRateWander.setTime (1.500f, sr);

        noiseRateNorm = (float) std::sqrt (sr / kReferenceRate);

        reset();
    }

    void RetroEngine::reset()
    {
        for (auto& c : channels)
            c.reset();

        scrapeLp1.reset();
        scrapeLp2.reset();
        rumbleLp1.reset();
        rumbleLp2.reset();
        wowRateWander.setValue (0.0f);

        wowPhase       = 0.0f;
        wowPhase2      = 0.37f;     // never in phase with the first
        flutterPhase   = 0.61f;
        wowRateTarget  = 0.0f;
        resamplePhase  = 0.0f;
        controlCounter = 0;

        // Deterministic and re-seeded here, so an offline render of a session
        // produces the same samples the realtime pass did.
        rng.setSeed (0x52455452u);      // 'RETR'

        for (auto* s : { &engageSm, &eraSm, &satSm, &noiseSm, &monoSm, &trimSm,
                         &digitalSm, &resampleSm, &rumbleSm, &hfLossSm, &mixSm,
                         &wowExcSm, &flutterExcSm, &scrapeExcSm, &crackleSm })
            s->reset();

        dryRamp.snap (1.0f);
        wetRamp.snap (0.0f);
    }

    void RetroEngine::process (juce::AudioBuffer<float>& buffer,
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

        const bool  enabled = params.flag (PID::retroOn);
        const float mixP    = juce::jlimit (0.0f, 1.0f, params.raw (PID::retroMix));

        // -------------------------------------------------------------------
        //  SWITCHING OFF IS A CROSSFADE, NOT A JUMP.
        //
        //  A ramp on MIX is not sufficient here and it took a second review to
        //  see why.  At MIX 0 this module's output is its own dry tap, which is
        //  the input delayed by nominalDelay; the bypassed output is the input
        //  itself.  They are four milliseconds apart in TIME, so however gently
        //  the wet is removed, the moment the early-out engages the output
        //  jumps four milliseconds - and it jumps back on the way in.
        //
        //  So the fade is between the whole module and the live input, at the
        //  very end.  Crossfading two copies of a signal four milliseconds
        //  apart is what a tape splice is, and it sounds like one.
        // -------------------------------------------------------------------
        const float engageTarget = enabled ? 1.0f : 0.0f;

        // -------------------------------------------------------------------
        //  BYPASS IS EXACT, ONCE THE CROSSFADE HAS RUN OUT.  The output buffer
        //  is not touched at all - but the transport keeps being written, so
        //  that un-bypassing does not play back whatever happened to be in the
        //  delay line when it was switched off.  Two stores per sample is a
        //  cheap way to make the module safe to automate.
        // -------------------------------------------------------------------
        if (engageTarget <= 0.0f && engageSm.current <= 1.0e-4f)
        {
            for (int i = 0; i < n; ++i)
            {
                channels[0].line.write (left[i]);
                channels[1].line.write (right[i]);
            }

            engageSm.holdAt (0.0f);
            mixSm.holdAt (mixP);
            return;
        }

        // -- parameters, read once per block --------------------------------
        const float eraP   = juce::jlimit (0.0f, 1.0f, params.raw (PID::retroEra)
                                                         + macros.alterAmount * 0.34f);
        const float ageP   = juce::jlimit (0.0f, 1.0f, params.raw (PID::retroAge)
                                                         + macros.age * 0.25f);
        const float driftP = juce::jlimit (0.0f, 1.0f, params.raw (PID::retroDrift)
                                                         + macros.movement * 0.30f);
        const float wearP  = juce::jlimit (0.0f, 1.0f, params.raw (PID::retroWear)
                                                         + macros.age * 0.30f);
        const float toneP  = juce::jlimit (-1.0f, 1.0f, params.raw (PID::retroTone));
        const float noiseP = juce::jlimit (0.0f, 1.0f, params.raw (PID::retroNoise)
                                                         + macros.grit * 0.25f);

        // 25 ms of block-rate smoothing on everything that reaches a gain.
        const float coef = std::exp (-(float) n / (float) (0.025 * sr));

        eraSm.set (eraP, n, coef);

        // The profile is evaluated once per block from the smoothed ERA.  The
        // quantities that are *gains* are then ramped inside the block; the
        // quantities that are filter *coefficients* are set once, because a
        // one-pole's state is continuous across a coefficient change and only a
        // gain step can click.
        const EraProfile era = eraAt (eraSm.current);

        const float satDrive = 1.0f + (ageP * 0.70f + macros.grit * 0.30f)
                                        * era.satAmount * 3.0f;

        const float resampleMix = juce::jlimit (0.0f, 1.0f,
                                    (eraSm.current - kResampleStart)
                                        / (1.0f - kResampleStart));

        satSm.set (satDrive, n, coef);
        noiseSm.set (noiseP * era.noiseLevel * kNoiseScale * noiseRateNorm, n, coef);
        monoSm.set (era.monoAmount, n, coef);
        trimSm.set (era.outputTrim, n, coef);
        digitalSm.set (era.digitalness, n, coef);
        resampleSm.set (resampleMix, n, coef);
        rumbleSm.set (noiseP * era.rumbleLevel * kRumbleScale * noiseRateNorm, n, coef);
        hfLossSm.set (era.hfLoss * (0.35f + 0.65f * ageP), n, coef);
        crackleSm.set (era.cracklePerSecond * (0.25f + 0.75f * ageP)
                         * juce::jlimit (0.0f, 1.0f, noiseP * 1.5f), n, coef);

        // -- transport depth -------------------------------------------------
        // A peak pitch deviation is converted to a delay excursion using the
        // real sample rate:  x = dev * fs / (2 pi f).  Specifying the deviation
        // rather than the excursion is what makes DRIFT mean the same musical
        // amount at 44.1 and at 96 kHz.
        const float wowHz     = juce::jmax (0.05f, era.wowHz);
        const float flutterHz = juce::jmax (1.0f,  era.flutterHz);

        const float wowExc = juce::jmin (maxExcursion,
                                era.wowDeviation * driftP * (float) sr
                                  / (fx::kTwoPi * wowHz));

        const float flutterExc = juce::jmin (maxExcursion * 0.25f,
                                era.flutterDeviation * driftP * (float) sr
                                  / (fx::kTwoPi * flutterHz));

        // Scrape is the smallest of the three by a long way: about 0.05 % of
        // peak pitch deviation at DRIFT = 1, which is roughly what a real
        // transport does.  It is expressed as a multiplier on the filtered
        // noise rather than as an excursion, and carries the same sample-rate
        // normalisation as the rest of the noise.
        const float scrapeExc = juce::jmin (maxExcursion * 0.06f,
                                era.scrapeAmount * driftP * (float) sr
                                  * 1.8e-5f * noiseRateNorm);

        wowExcSm.set (wowExc, n, coef);
        flutterExcSm.set (flutterExc, n, coef);
        scrapeExcSm.set (scrapeExc, n, coef);

        const float wowInc     = wowHz / (float) sr;
        const float flutterInc = flutterHz / (float) sr;

        // -- filter coefficients, block rate ---------------------------------
        for (auto& c : channels)
        {
            c.lowCut.setCutoff (juce::jlimit (20.0f, 400.0f, era.lowCutHz), sr);

            // The noise is high-passed twice, above the low crossover, so the
            // medium's hiss cannot decorrelate the low band.  Its
            // low-frequency component is rumble, which is common-mode.
            const float nLow = juce::jmax (kLowCrossoverHz + 10.0f, era.noiseLowHz);
            c.noiseHp1.setCutoff (nLow, sr);
            c.noiseHp2.setCutoff (nLow, sr);
            c.noiseLp.setCutoff (juce::jlimit (1000.0f, (float) (sr * 0.45),
                                               era.noiseHighHz), sr);
        }

        // -- dropout scheduling ----------------------------------------------
        // Events per second.  wear^1.7 keeps the bottom of the control sparse:
        // at WEAR 0.2 this is 0.29/s, one event every three or four seconds.
        const float eventsPerSecond  = std::pow (wearP, 1.7f) * 4.5f;
        const float controlSeconds   = (float) kControlInterval / (float) sr;
        const float eventProbability = juce::jlimit (0.0f, 0.5f,
                                                     eventsPerSecond * controlSeconds);

        const float azimuthDepth = era.azimuthAmount * (0.25f + 0.75f * wearP);

        // -- the resampling grid ---------------------------------------------
        const float resamplePeriod = juce::jmax (1.0f, (float) sr / juce::jmax (1000.0f,
                                                                               era.resampleHz));
        const float resampleStep = 2.0f / std::exp2 (juce::jlimit (4.0f, 24.0f,
                                                                   era.resampleBits));

        // -- dry / wet --------------------------------------------------------
        // The mix is smoothed, then turned into equal-power gains at each end
        // of the block and interpolated.  Interpolating the two gains rather
        // than the angle costs two trig evaluations per block instead of two
        // per sample, and the deviation from equal power inside one block is
        // far below anything audible.
        mixSm.set (mixP, n, coef);
        engageSm.set (engageTarget, n, coef);

        {
            float d0, w0, d1, w1;
            fx::dryWetGains (juce::jlimit (0.0f, 1.0f, mixSm.ramp.at (0)), d0, w0);
            fx::dryWetGains (juce::jlimit (0.0f, 1.0f, mixSm.ramp.at (n)), d1, w1);

            dryRamp.set (d0, d1, n);
            wetRamp.set (w0, w1, n);
        }

        // ===================================================================
        //  The sample loop
        // ===================================================================
        for (int i = 0; i < n; ++i)
        {
            // -- control rate ------------------------------------------------
            if (--controlCounter <= 0)
            {
                controlCounter = kControlInterval;

                // Wow's own rate wanders, because a transport that is slightly
                // out is slightly out by a slightly different amount every
                // revolution.
                wowRateTarget = rng.nextBipolar() * 0.12f;

                for (auto& c : channels)
                {
                    c.azimuthTarget = rng.nextBipolar() * azimuthDepth;

                    if (c.dropRemaining > 0)
                    {
                        c.dropRemaining -= kControlInterval;

                        if (c.dropRemaining <= 0)
                        {
                            c.dropRemaining  = 0;
                            c.dropTargetGain = 1.0f;
                            c.dropTargetHf   = 1.0f;
                        }
                    }
                }

                if (eventProbability > 0.0f && rng.next01() < eventProbability)
                {
                    const float depth = (0.20f + 0.75f * rng.next01())
                                            * (0.30f + 0.70f * wearP);

                    const float seconds = 0.008f + rng.next01()
                                            * (0.060f + wearP * 0.140f);
                    const int   length  = (int) (seconds * sr);

                    // A little over half of them take one channel only.  That
                    // asymmetry is the channel instability section 85 asks for;
                    // the band split downstream is what keeps it out of the low
                    // end.
                    const float which = rng.next01();
                    const int   first = (which < 0.45f) ? 0 : ((which < 0.725f) ? 0 : 1);
                    const int   last  = (which < 0.45f) ? 1 : first;

                    for (int c = first; c <= last; ++c)
                    {
                        auto& ch = channels[c];
                        ch.dropTargetGain = juce::jlimit (0.0f, 1.0f, 1.0f - depth);
                        ch.dropTargetHf   = juce::jlimit (0.0f, 1.0f, 1.0f - depth * 0.9f);
                        ch.dropRemaining  = juce::jmax (ch.dropRemaining, length);
                    }
                }
            }

            // -- the transport, shared by both channels ----------------------
            const float rateWander = wowRateWander.process (wowRateTarget);

            wowPhase     += wowInc * (1.0f + rateWander);
            wowPhase2    += wowInc * 1.37f * (1.0f - rateWander * 0.5f);
            flutterPhase += flutterInc;

            if (wowPhase     >= 1.0f) wowPhase     -= 1.0f;
            if (wowPhase2    >= 1.0f) wowPhase2    -= 1.0f;
            if (flutterPhase >= 1.0f) flutterPhase -= 1.0f;
            if (wowPhase     <  0.0f) wowPhase     += 1.0f;
            if (wowPhase2    <  0.0f) wowPhase2    += 1.0f;

            // Two incommensurate oscillators, not one: a single sine on a delay
            // line is the shortcut section 85 forbids, and it is audible as a
            // perfectly periodic warble.
            const float wow = 0.72f * fx::sineTurns (wowPhase)
                            + 0.28f * fx::sineTurns (wowPhase2);

            const float flutter = fx::sineTurns (flutterPhase);

            const float scrape = scrapeLp2.lowpass (scrapeLp1.lowpass (rng.nextBipolar()));

            // Breath moves the wow *depth*, not the wow itself: the contract
            // reserves Breath for slow parameters, and a speed error's depth is
            // exactly that.
            const float breathDepth = 1.0f + macros.breathAt (i) * 0.18f;

            float offset = wowExcSm.at (i) * wow * breathDepth
                         + flutterExcSm.at (i) * flutter
                         + scrapeExcSm.at (i) * scrape;

            offset = juce::jlimit (-maxExcursion, maxExcursion, offset);

            const float readDelay = (float) nominalDelay + offset;

            // -- the resampling grid: one converter, one clock ----------------
            const float resampleAmount = resampleSm.at (i);
            bool takeSample = true;

            if (resamplePeriod > 1.0f)
            {
                resamplePhase += 1.0f;

                if (resamplePhase >= resamplePeriod)
                    resamplePhase -= resamplePeriod;
                else
                    takeSample = false;
            }

            const float digital = digitalSm.at (i);
            const float drive   = satSm.at (i);
            const float bias    = era.satBias;
            const float hfLoss  = hfLossSm.at (i);

            float lowBand[2]  = { 0.0f, 0.0f };
            float midBand[2]  = { 0.0f, 0.0f };
            float highBand[2] = { 0.0f, 0.0f };
            float dryTap[2]   = { 0.0f, 0.0f };
            float wet[2]      = { 0.0f, 0.0f };

            // Captured before anything writes to the buffer: this is the
            // undelayed signal the bypassed path would have produced, and the
            // engage crossfade at the bottom needs it.
            const float liveL = left[i];
            const float liveR = numCh > 1 ? right[i] : left[i];

            for (int c = 0; c < 2; ++c)
            {
                auto& ch = channels[c];

                const float x = (c == 0) ? left[i] : right[i];

                ch.line.write (x);

                // The dry tap comes from the same line at the nominal delay, so
                // dry and wet are time-aligned and the mix cannot comb.
                dryTap[c] = ch.line.readInt (nominalDelay);

                float y = ch.line.read (readDelay);

                y = ch.lowCut.highpass (y);

                // -- saturation ---------------------------------------------
                // Asymmetric at the acetate end (even harmonics), symmetric in
                // the middle, converter-hard at the digital end.  The static
                // offset the bias introduces is removed here and the residual
                // by the DC blocker at the end of the chain.  Dividing by the
                // drive gives unit small-signal gain, so AGE cannot make the
                // module louder - section 148.
                {
                    const float d    = y * drive + bias;
                    const float soft = fx::tanhFast (d) - fx::tanhFast (bias);
                    const float hard = juce::jlimit (-1.0f, 1.0f, d)
                                         - juce::jlimit (-1.0f, 1.0f, bias);

                    y = fx::lerp (soft, hard, digital) / drive;
                }

                // -- bandwidth, dynamic --------------------------------------
                // One mechanism doing two of the jobs section 85 lists: the
                // corner falls as the signal gets louder, which is dynamic HF
                // loss on sustained material and transient rounding on an
                // attack, because the envelope in front of it is 3 ms.
                const float envFast  = ch.levelEnv.process (std::abs (y));
                const float collapse = hfLoss * juce::jmin (1.0f, envFast * 3.0f);

                const float corner = juce::jlimit (400.0f, (float) (sr * 0.45),
                                                   era.bandwidthHz * (1.0f - collapse));

                ch.bandLimit.setCutoff (corner, sr);
                y = ch.bandLimit.lowpass (y);

                // -- resampling colouration ----------------------------------
                // Sample and hold on the converter's grid, plus its word
                // length.  THIS STAGE ALIASES AND IS NOT OVERSAMPLED, ON
                // PURPOSE: a 26 kHz 12-bit sampler aliased, and that is what
                // the SAMPLER era is.  Do not "fix" it.
                if (resampleAmount > 0.001f)
                {
                    if (takeSample)
                        ch.held = std::floor (juce::jlimit (-1.5f, 1.5f, y) / resampleStep
                                                + 0.5f) * resampleStep;

                    y = fx::lerp (y, ch.held, resampleAmount);
                }

                ch.bands.split (y, lowBand[c], midBand[c], highBand[c]);
            }

            // -- channel behaviour: mid and high only ------------------------
            const float gainL = channels[0].dropGain.process (channels[0].dropTargetGain);
            const float gainR = channels[1].dropGain.process (channels[1].dropTargetGain);
            const float hfL   = channels[0].dropHf.process (channels[0].dropTargetHf);
            const float hfR   = channels[1].dropHf.process (channels[1].dropTargetHf);

            const float azL = channels[0].azimuth.process (channels[0].azimuthTarget);
            const float azR = channels[1].azimuth.process (channels[1].azimuthTarget);

            // The common part of a dropout is all the low band is allowed to
            // hear.  Everything the two channels differ by stays above the
            // crossover, so a one-channel dropout can never decorrelate the low
            // end - specification 38, 40, 43.
            const float commonGain = juce::jmin (gainL, gainR);

            const float mono = monoSm.at (i);

            const float midSum  = (midBand[0] + midBand[1]) * 0.5f;
            const float highSum = (highBand[0] + highBand[1]) * 0.5f;

            const float midL = fx::lerp (midBand[0], midSum, mono);
            const float midR = fx::lerp (midBand[1], midSum, mono);
            const float hiL  = fx::lerp (highBand[0], highSum, mono);
            const float hiR  = fx::lerp (highBand[1], highSum, mono);

            // Azimuth only ever *loses* high end - a misaligned head gap
            // cannot add any - so it is a one-sided trim.  The two channels
            // wander independently, which is the instability; neither can get
            // louder, which is section 148.
            const float azTrimL = juce::jlimit (0.15f, 1.0f, 1.0f - std::abs (azL));
            const float azTrimR = juce::jlimit (0.15f, 1.0f, 1.0f - std::abs (azR));

            wet[0] = lowBand[0] * commonGain
                   + midL * gainL
                   + hiL * gainL * hfL * azTrimL;

            wet[1] = lowBand[1] * commonGain
                   + midR * gainR
                   + hiR * gainR * hfR * azTrimR;

            // -- the medium's own noise --------------------------------------
            const float noiseGain          = noiseSm.at (i);
            const float cracklePerSample   = crackleSm.at (i) / (float) sr;

            // Rumble is generated once and added to both channels identically:
            // it is the only low-frequency thing this engine introduces, and it
            // stays perfectly correlated.
            const float rumble = rumbleLp2.lowpass (rumbleLp1.lowpass (rng.nextBipolar()))
                                    * rumbleSm.at (i);

            const float trim = trimSm.at (i);

            for (int c = 0; c < 2; ++c)
            {
                auto& ch = channels[c];

                float source = rng.nextBipolar();

                // Surface events: generated impulses, coloured by the same
                // filters as the hiss, never a loop.
                if (cracklePerSample > 0.0f && rng.next01() < cracklePerSample)
                    source += rng.nextBipolar() * kCrackleScale;

                source = ch.noiseHp2.highpass (ch.noiseHp1.highpass (source));
                source = ch.noiseLp.lowpass (source);

                // How the noise interacts with the material.  It does NOT
                // duck when the music starts: a medium's noise floor is not a
                // gate, and ducking it would be exactly the cheat that makes
                // an effect sound like a plug-in.  What is modelled instead is
                // the part that is real - tape's modulation noise, which rises
                // with the recorded level.  The noise still ends up "most
                // audible in the gaps", because that is what masking does, and
                // masking needs no help from the code.
                const float envSlow  = ch.gapEnv.process (std::abs (wet[c]));
                const float presence = juce::jmin (1.0f, envSlow * 4.0f);
                const float noiseFactor = 1.0f + presence * 0.35f;

                float y = wet[c] * trim + source * noiseGain * noiseFactor + rumble;

                y = ch.tilt.process (y, toneP);
                y = ch.dc.process (y);

                wet[c] = fx::guard (y);
            }

            const float dry     = dryRamp.at (i);
            const float wetGain = wetRamp.at (i);

            float outL = fx::guard (dryTap[0] * dry + wet[0] * wetGain);
            float outR = fx::guard (dryTap[1] * dry + wet[1] * wetGain);

            // The engage crossfade, against the LIVE input rather than the
            // delayed dry tap.  See the note at the top of this function.
            const float engage = juce::jlimit (0.0f, 1.0f, engageSm.at (i));

            if (engage < 1.0f)
            {
                outL = fx::guard (fx::lerp (liveL, outL, engage));
                outR = fx::guard (fx::lerp (liveR, outR, engage));
            }

            if (numCh > 1)
            {
                left[i]  = outL;
                right[i] = outR;
            }
            else
            {
                // A mono buffer is not a configuration the instrument ships,
                // but running both chains and summing is the only answer that
                // does not silently drop half the channel behaviour.
                left[i] = 0.5f * (outL + outR);
            }
        }
    }
}
