#include "GenerationModels.h"

namespace nacar::memory
{
    // =======================================================================
    //  The character tables
    //
    //  Row n describes what the *nth* pass through the medium did, not an
    //  intensity of one effect.  They get progressively less forgiving, and the
    //  last two rows carry behaviour the first two never reach at any setting:
    //  aliased decimation, one-channel dropouts, a wandering spectral hole.
    //  Generation III is therefore the first two rows *plus* this one, applied
    //  to their output - including to the artefacts they left behind.
    // =======================================================================
    static const CopyCharacter kPrimary[kMaxCopies] =
    {
        //  hf     lf   wand  tran  drive  bias   div   pre   hold  cents  hz   diff  decor  even  odd   tilt  absorb noise  ntilt drop  hole
        { 13000.f, 24.f, 0.10f, 0.18f, 0.35f,  0.06f, 1.15f, 0.92f, 0.05f,  3.0f, 0.80f, 0.22f, 0.06f, 0.35f, 0.10f, -0.05f, 0.20f, 0.00035f,  0.45f, 0.00f, 0.00f },
        {  8500.f, 34.f, 0.16f, 0.34f, 0.60f, -0.09f, 2.45f, 0.62f, 0.22f,  5.0f, 0.62f, 0.42f, 0.18f, 0.25f, 0.30f, -0.10f, 0.34f, 0.00090f,  0.20f, 0.00f, 0.00f },
        {  5200.f, 46.f, 0.24f, 0.52f, 0.95f,  0.14f, 3.70f, 0.34f, 0.45f,  7.0f, 0.54f, 0.66f, 0.36f, 0.18f, 0.48f, -0.16f, 0.50f, 0.00220f, -0.10f, 0.15f, 0.18f },
        {  3300.f, 60.f, 0.34f, 0.72f, 1.40f, -0.20f, 5.40f, 0.12f, 0.70f,  9.0f, 0.47f, 0.85f, 0.55f, 0.30f, 0.62f, -0.24f, 0.68f, 0.00500f, -0.35f, 0.60f, 0.45f }
    };

    //  The alternate lineage, reached through macros.alterAmount: a sampler
    //  chain rather than a tape chain.  It keeps far more top, so it stays
    //  bright, and pays for that with harder decimation, more aliasing, a
    //  quantised-sounding reconstruction and a brighter noise floor.  The shape
    //  of the degradation differs; the amount does not.
    static const CopyCharacter kAlternate[kMaxCopies] =
    {
        { 16000.f, 18.f, 0.06f, 0.12f, 0.25f, -0.04f, 1.60f, 0.70f, 0.20f,  2.0f, 1.05f, 0.14f, 0.10f, 0.20f, 0.22f,  0.04f, 0.12f, 0.00040f,  0.75f, 0.00f, 0.00f },
        { 12500.f, 26.f, 0.10f, 0.22f, 0.45f,  0.07f, 3.20f, 0.42f, 0.45f,  3.5f, 0.88f, 0.26f, 0.26f, 0.14f, 0.40f,  0.02f, 0.20f, 0.00110f,  0.55f, 0.00f, 0.06f },
        {  9000.f, 34.f, 0.16f, 0.34f, 0.70f, -0.11f, 5.00f, 0.20f, 0.68f,  5.0f, 0.74f, 0.40f, 0.44f, 0.10f, 0.55f, -0.02f, 0.30f, 0.00260f,  0.35f, 0.28f, 0.30f },
        {  6200.f, 44.f, 0.24f, 0.50f, 1.05f,  0.16f, 7.60f, 0.06f, 0.88f,  7.0f, 0.66f, 0.58f, 0.66f, 0.16f, 0.70f, -0.06f, 0.42f, 0.00560f,  0.10f, 0.85f, 0.62f }
    };

    const CopyCharacter& primaryCopy (int i) noexcept
    {
        return kPrimary[juce::jlimit (0, kMaxCopies - 1, i)];
    }

    const CopyCharacter& alternateCopy (int i) noexcept
    {
        return kAlternate[juce::jlimit (0, kMaxCopies - 1, i)];
    }

    /** Written out field by field rather than walked as an array of floats:
        the struct is all floats today, but a reinterpreting loop would become
        silently wrong the first time someone adds an int to it. */
    static void blendCharacter (const CopyCharacter& a, const CopyCharacter& b,
                                float t, CopyCharacter& out) noexcept
    {
        out.hfCorner          = fx::lerp (a.hfCorner,          b.hfCorner,          t);
        out.lfCorner          = fx::lerp (a.lfCorner,          b.lfCorner,          t);
        out.bandwidthWander   = fx::lerp (a.bandwidthWander,   b.bandwidthWander,   t);
        out.transient         = fx::lerp (a.transient,         b.transient,         t);
        out.satDrive          = fx::lerp (a.satDrive,          b.satDrive,          t);
        out.satBias           = fx::lerp (a.satBias,           b.satBias,           t);
        out.resampleDivisor   = fx::lerp (a.resampleDivisor,   b.resampleDivisor,   t);
        out.resamplePreFilter = fx::lerp (a.resamplePreFilter, b.resamplePreFilter, t);
        out.resampleHold      = fx::lerp (a.resampleHold,      b.resampleHold,      t);
        out.wobbleCents       = fx::lerp (a.wobbleCents,       b.wobbleCents,       t);
        out.wobbleHz          = fx::lerp (a.wobbleHz,          b.wobbleHz,          t);
        out.diffusion         = fx::lerp (a.diffusion,         b.diffusion,         t);
        out.decorrelation     = fx::lerp (a.decorrelation,     b.decorrelation,     t);
        out.colourEven        = fx::lerp (a.colourEven,        b.colourEven,        t);
        out.colourOdd         = fx::lerp (a.colourOdd,         b.colourOdd,         t);
        out.tilt              = fx::lerp (a.tilt,              b.tilt,              t);
        out.absorption        = fx::lerp (a.absorption,        b.absorption,        t);
        out.noise             = fx::lerp (a.noise,             b.noise,             t);
        out.noiseTilt         = fx::lerp (a.noiseTilt,         b.noiseTilt,         t);
        out.dropout           = fx::lerp (a.dropout,           b.dropout,           t);
        out.hole              = fx::lerp (a.hole,              b.hole,              t);
    }

    // -----------------------------------------------------------------------
    //  Fixed geometry
    // -----------------------------------------------------------------------

    /** The three common diffusion delays, in seconds: 7, 13 and 23 samples at
        48 kHz.  Mutually prime at that rate so the chain's echoes do not line
        up into a pitched resonance, and short enough that the whole chain adds
        under a millisecond of onset delay. */
    static constexpr float kDiffuseSeconds[3] = { 7.0f / 48000.0f, 13.0f / 48000.0f, 23.0f / 48000.0f };

    /** Relative coefficient weights down the diffusion chain: the first stage
        scatters hardest, the last one only tidies up. */
    static constexpr float kDiffuseWeights[3] = { 1.0f, 0.86f, 0.72f };

    static constexpr float kDecorWeights[2] = { 1.0f, 0.78f };

    /** Which way each copy leans.  The partial sums of this pattern never
        exceed one, so however many copies are running, the accumulated channel
        asymmetry stays a character rather than becoming a pull on the image. */
    static constexpr float kAsymPattern[kMaxCopies] = { 1.0f, -1.0f, -1.0f, 1.0f };

    /** cents = 1200/ln2 * (delta samples) * 2 * pi * rate / sampleRate, so the
        delay depth a given peak deviation needs is the inverse of that. */
    static constexpr float kCentsPerOctaveScale = 1731.234f;

    static constexpr float kNoiseColourHz = 1800.0f;

    // =======================================================================
    //  CopyStage
    // =======================================================================
    void CopyStage::prepare (double sampleRate, int copyIndex)
    {
        sr          = juce::jmax (8000.0, sampleRate);
        index       = juce::jlimit (0, kMaxCopies - 1, copyIndex);
        rateScale   = (float) (sr / 48000.0);
        controlRate = (float) sr / (float) kControlInterval;

        // One seed per copy, mixed from the index: the same copy behaves
        // identically on every machine and in every render.
        fx::Rng seed ((juce::uint32) (0x4D454D01u + 0x9E37u * (juce::uint32) (index + 1)));

        for (auto& p : seedPhase)
            p = seed.next01();

        driftRate = 0.85f + 0.30f * seed.next01();

        wobbleBase     = juce::jmax (16.0f, 0.0015f * (float) sr);
        wobbleMaxDepth = wobbleBase - 8.0f;
        driftBase        = juce::jmax (8.0f,  0.00067f * (float) sr);
        driftMaxDepth    = driftBase * 0.70f;

        int diffuseSamples[3] {};
        int diffuseTotal = 0;

        for (int k = 0; k < 3; ++k)
        {
            diffuseSamples[k] = juce::jmax (2, (int) std::lround (kDiffuseSeconds[k] * sr));
            diffuseTotal += diffuseSamples[k];
        }

        // Detectors.  The three fast ones run per sample; the three slow ones
        // are fed from the fast peak follower once per control block, which is
        // a decimation of an already smooth signal rather than of the audio.
        fastEnv      .prepare (0.0005f, 0.005f, sr);
        slowEnv      .prepare (0.0400f, 0.120f, sr);
        transientGain.prepare (0.0250f, 0.000f, sr);   // instant down, 25 ms back up
        absorbEnv    .prepare (0.0200f, 0.080f, controlRate);
        duckEnv      .prepare (0.0500f, 0.400f, controlRate);
        activityEnv  .prepare (0.0000f, 2.500f, controlRate);

        for (auto* s : { &sDepth, &sBandwidth, &sWobble, &sDiffusion,
                         &sAsym, &sAge, &sMovement, &sAlter })
            s->setTime (0.030f, controlRate);

        noiseColourCommon.setCutoff (kNoiseColourHz, sr);
        noiseRngCommon.setSeed ((juce::uint32) (0x4E4F4953u + (juce::uint32) index));

        // A one-pole low pass has a noise-equivalent bandwidth of (pi/2)*fc, so
        // its output carries only pi*fc/fs of white noise's power.  Without
        // these two normalisers the noise floor would change level as its
        // colour changed, and would change again at 96 kHz.
        const float powerFraction = juce::jlimit (0.01f, 0.95f,
                                                  fx::kPi * kNoiseColourHz / (float) sr);
        noiseLpNorm = 1.0f / std::sqrt (powerFraction);
        noiseHpNorm = 1.0f / std::sqrt (1.0f - powerFraction);

        sideHpA.setCutoff (kLowSplitHz, sr);
        sideHpB.setCutoff (kLowSplitHz, sr);

        // 5 to 14 samples at 48 kHz: short enough that the phase difference the
        // decorrelation creates is confined to the top of the spectrum, which is
        // the only place it is wanted.
        float decorDelays[2];

        for (auto& d : decorDelays)
            d = juce::jmax (2.0f, (5.0f + 9.0f * seed.next01()) * rateScale);

        for (int c = 0; c < 2; ++c)
        {
            auto& ch = channel[c];

            ch.wobbleDelay.prepare ((int) (wobbleBase + wobbleMaxDepth) + 8);
            ch.driftDelay   .prepare ((int) (driftBase + driftMaxDepth) + 8);

            ch.lowCut.setCutoff (20.0f,    sr);
            ch.hfA   .setCutoff (18000.0f, sr);
            ch.hfB   .setCutoff (18000.0f, sr);
            ch.absorb.setCutoff (18000.0f, sr);
            ch.resPre.setCutoff ((float) sr * 0.4f, sr);
            ch.dc.prepare (sr);

            for (int k = 0; k < 3; ++k)
            {
                ch.diffuse[k].prepare (diffuseSamples[k] + 4);
                ch.diffuse[k].setDelay ((float) diffuseSamples[k]);
                ch.diffuse[k].setCoefficient (0.0f);
            }

            ch.split.prepare (kLowSplitHz, kHighSplitHz, sr);
            ch.tilt .prepare (sr, 700.0f);

            // The two channels get the *same* delays and differ only in the
            // sign of the coefficient.  Different delays would offset the
            // channels by several samples even at zero decorrelation, because
            // an allpass with a coefficient of zero is a plain delay - which is
            // exactly the bug this comment exists to stop coming back.
            for (int k = 0; k < 2; ++k)
            {
                ch.decor[k].prepare ((int) decorDelays[k] + 4);
                ch.decor[k].setDelay (decorDelays[k]);
                ch.decor[k].setCoefficient (0.0f);
            }

            ch.colourEnv.setTime (0.030f, sr);
            ch.colourDc.prepare (sr);
            ch.holeLo.setCutoff (700.0f,  sr);
            ch.holeHi.setCutoff (1400.0f, sr);

            ch.noiseColour.setCutoff (kNoiseColourHz, sr);
            ch.noiseGuard .setCutoff (kLowSplitHz,    sr);
            ch.noiseRng.setSeed ((juce::uint32) (0x484953u + 0x61u * (juce::uint32) index
                                                 + 0x1Fu * (juce::uint32) c));

            ch.wobbleRate = (0.19f + 0.20f * seed.next01()) * driftRate;
            ch.startPhase = seed.next01();
        }

        // Onset delay: the wobble delay's base, the diffusion chain, the mid
        // band's alignment delay and the resampler's single sample.  Constant
        // for the life of the object, which is what lets the engine report it.
        latency = (int) wobbleBase + diffuseTotal + (int) driftBase + 1;

        reset();
    }

    void CopyStage::reset() noexcept
    {
        fastEnv.reset();
        slowEnv.reset();
        absorbEnv.reset();
        duckEnv.reset();
        activityEnv.reset();
        transientGain.setValue (1.0f);

        tGain = 1.0f;
        tAmount = 0.0f;

        commonPhase  = seedPhase[0];
        commonPhase2 = seedPhase[1];
        wanderPhase  = seedPhase[2];
        holePhase    = seedPhase[3];

        resAcc = 0.0f;
        resDivisor = 1.0f;
        resIncrement = 1.0f;
        resPreMix = 1.0f;
        holdBlend = 0.0f;

        sideHpA.reset();
        sideHpB.reset();
        noiseColourCommon.reset();

        primed = false;

        for (auto& ch : channel)
        {
            ch.wobbleDelay.reset();
            ch.driftDelay.reset();

            ch.lowCut.reset();
            ch.hfA.reset();
            ch.hfB.reset();
            ch.absorb.reset();
            ch.resPre.reset();
            ch.dc.reset();

            for (auto& a : ch.diffuse) a.reset();
            for (auto& a : ch.decor)   a.reset();

            ch.split.reset();
            ch.tilt.reset();
            ch.colourEnv.reset();
            ch.colourDc.reset();
            ch.holeLo.reset();
            ch.holeHi.reset();
            ch.noiseColour.reset();
            ch.noiseGuard.reset();

            ch.resPrev = ch.resCur = 0.0f;
            ch.wobblePhase = ch.startPhase;
            ch.dropGain = ch.dropTarget = 1.0f;
            ch.dropTicks = 0;
            ch.driftSamples = 0.0f;
            ch.noiseGain = 0.0f;
        }
    }

    void CopyStage::setSettings (const StageSettings& s) noexcept
    {
        pending = s;
    }

    // -----------------------------------------------------------------------
    //  Control rate: every coefficient in the stage, recomputed once per
    //  kControlInterval samples from smoothed settings.
    // -----------------------------------------------------------------------
    void CopyStage::updateControl (const StageSettings& in, float pulse, float breath) noexcept
    {
        if (! primed)
        {
            sDepth    .setValue (in.depth);
            sBandwidth.setValue (in.bandwidth);
            sWobble   .setValue (in.wobble);
            sDiffusion.setValue (in.diffusion);
            sAsym     .setValue (in.asymmetry);
            sAge      .setValue (in.age);
            sMovement .setValue (in.movement);
            sAlter    .setValue (in.alter);
            primed = true;
        }

        const float depth     = juce::jlimit (0.0f, 1.0f, sDepth    .process (in.depth));
        const float bandwidth = juce::jlimit (0.0f, 1.0f, sBandwidth.process (in.bandwidth));
        const float wobble    = juce::jlimit (0.0f, 1.0f, sWobble   .process (in.wobble));
        const float diffusion = juce::jlimit (0.0f, 1.0f, sDiffusion.process (in.diffusion));
        const float asym      = juce::jlimit (0.0f, 1.0f, sAsym     .process (in.asymmetry));
        const float age       = juce::jlimit (0.0f, 1.0f, sAge      .process (in.age));
        const float movement  = juce::jlimit (0.0f, 1.0f, sMovement .process (in.movement));
        const float alter     = juce::jlimit (0.0f, 1.0f, sAlter    .process (in.alter));

        blendCharacter (primaryCopy (index), alternateCopy (index), alter, blended);
        const CopyCharacter& c = blended;

        const float sign = kAsymPattern[index];
        const float pulseUp = juce::jlimit (0.0f, 1.0f, pulse);

        // -- 1  bandwidth ---------------------------------------------------
        // Geometric, not linear: halfway between 20 kHz and 3.3 kHz has to be
        // an octave count, or the control does almost nothing until the end.
        const float loss = juce::jlimit (0.0f, 1.0f,
                                         depth * (0.35f + 0.65f * bandwidth + 0.20f * age));

        const float hfBase = fx::exp2Fast (fx::lerp (fx::log2Fast (20000.0f),
                                                     fx::log2Fast (juce::jmax (500.0f, c.hfCorner)),
                                                     loss));
        const float lfBase = fx::exp2Fast (fx::lerp (fx::log2Fast (15.0f),
                                                     fx::log2Fast (juce::jmax (15.0f, c.lfCorner)),
                                                     loss));

        // The corner is never still: Motion widens the wander, Breath moves it
        // organically, the Pulse deepens it for as long as the pulse lasts.
        const float wanderDepth = c.bandwidthWander * (0.40f + 1.60f * movement)
                                * (1.0f + 0.5f * pulseUp);

        wanderPhase += (0.11f * driftRate * (1.0f + 2.0f * movement)) / controlRate;
        wanderPhase -= std::floor (wanderPhase);

        const float wanderValue = 0.6f * fx::sineTurns (wanderPhase) + 0.4f * breath;
        const float hfWander    = fx::exp2Fast (0.30f * wanderDepth * wanderValue);

        // -- 10 HF absorption ------------------------------------------------
        const float absorbLevel = absorbEnv.process (fastEnv.z);
        const float absorbNorm  = absorbLevel / (absorbLevel + 0.25f);
        const float absorbDepth = c.absorption * depth;

        // -- 2  transient softness -------------------------------------------
        tAmount = juce::jlimit (0.0f, 1.2f,
                                c.transient * depth * (0.70f + 0.50f * age) * (1.0f + 0.6f * pulseUp));

        // -- 3  saturation ----------------------------------------------------
        const float drive = juce::jlimit (0.25f, 6.0f,
                                          1.0f + c.satDrive * depth * (1.0f + 0.35f * age)
                                                             * (1.0f + 0.5f * pulseUp));

        // -- 4  resampling -----------------------------------------------------
        // rateScale keeps the artefact at a fixed absolute rate: a third
        // generation copy has to sound the same at 96 kHz as at 48 kHz.
        resDivisor   = juce::jlimit (1.0f, 64.0f,
                                     1.0f + rateScale * depth * (c.resampleDivisor - 1.0f)
                                                      * (1.0f + 0.5f * pulseUp));
        resIncrement = 1.0f / resDivisor;
        resPreMix    = juce::jlimit (0.0f, 1.0f,
                                     c.resamplePreFilter + (1.0f - depth) * (1.0f - c.resamplePreFilter));
        holdBlend    = juce::jlimit (0.0f, 1.0f, c.resampleHold * depth * (1.0f + 0.6f * pulseUp));

        // -- 5  pitch instability ---------------------------------------------
        const float wobbleAmount = juce::jlimit (0.0f, 1.0f, (wobble + 0.25f * movement) * depth);
        const float driftHz = juce::jmax (0.05f, c.wobbleHz * driftRate * (1.0f + 1.5f * movement));

        commonPhase  += driftHz / controlRate;
        commonPhase2 += driftHz * 0.37f / controlRate;
        commonPhase  -= std::floor (commonPhase);
        commonPhase2 -= std::floor (commonPhase2);

        const float drift = juce::jlimit (-1.0f, 1.0f,
                                          0.68f * fx::sineTurns (commonPhase)
                                        + 0.32f * fx::sineTurns (commonPhase2)
                                        + 0.45f * breath);

        // Delay depth for a wanted peak deviation in cents.  Clamped by the
        // delay line, so a very slow drift is shallower than its nominal cents.
        const float wanted = c.wobbleCents * wobbleAmount * (float) sr
                           / (kCentsPerOctaveScale * fx::kTwoPi * driftHz);

        wobbleSamples = juce::jmin (wobbleMaxDepth, juce::jmax (0.0f, wanted)) * drift;

        // -- 6  phase diffusion -------------------------------------------------
        const float diffuseG = juce::jlimit (0.0f, 0.72f,
                                             c.diffusion * diffusion * depth * (0.80f + 0.40f * age));

        // -- 7  stereo coherence ------------------------------------------------
        const float decor = juce::jlimit (0.0f, 0.75f,
                                          c.decorrelation * depth * (0.45f + 0.90f * asym));

        // The side high pass engages with anything that could decorrelate, and
        // there are two such things: the allpass pair above and the differential
        // half of the drift, which is a per-channel delay difference and so acts
        // at every frequency.  Whichever is larger decides.
        const float differential = wobbleAmount * (0.25f + 0.55f * asym);

        sideMix = juce::jlimit (0.0f, 1.0f, 4.0f * juce::jmax (decor, differential));

        // -- 8  harmonic colouration ---------------------------------------------
        colourEvenGain = c.colourEven * depth * 0.20f * (1.0f + 0.4f * age);
        colourOddGain  = c.colourOdd  * depth * 0.16f * (1.0f + 0.4f * age);

        // -- 9  spectral aging ---------------------------------------------------
        tiltAmount = juce::jlimit (-1.0f, 1.0f, c.tilt * depth * (1.0f + 0.5f * age));

        // -- 12 noise interaction -------------------------------------------------
        const float duck = duckEnv.process (fastEnv.z);
        const float duckNorm = duck / (duck + 0.18f);
        const float activity = activityEnv.process (fastEnv.z);
        const float gate = activity / (activity + 0.0035f);

        const float noiseLevel = c.noise * depth * (1.0f + 0.6f * age)
                               * (1.0f - 0.70f * duckNorm)          // ducks under loud material
                               * (0.55f + 0.75f * (1.0f - duckNorm))// comes forward in the gaps
                               * gate                               // and leaves after true silence
                               * (1.0f + 1.2f * pulseUp);

        noiseCommonGain = noiseLevel * 0.6f;

        const float bright = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * c.noiseTilt);
        noiseHpBright = bright * noiseHpNorm;
        noiseLpDark   = (1.0f - bright) * noiseLpNorm;

        // -- generation IV: the wandering spectral hole ----------------------------
        holeAmount = c.hole * depth * 0.80f;

        if (holeAmount > 0.0001f)
        {
            holePhase += (0.07f * driftRate * (1.0f + 2.0f * movement)) / controlRate;
            holePhase -= std::floor (holePhase);

            const float centre = 900.0f * fx::exp2Fast (1.6f * fx::sineTurns (holePhase) + 0.5f * breath);

            for (auto& ch : channel)
            {
                ch.holeLo.setCutoff (centre, sr);
                ch.holeHi.setCutoff (centre * 1.9f, sr);
            }
        }

        // -- per channel -----------------------------------------------------------
        const float dropChance = c.dropout * depth;

        for (int i = 0; i < 2; ++i)
        {
            auto& ch = channel[i];
            const float lean = sign * (i == 0 ? 1.0f : -1.0f);

            // 1 + 11: the two channels lost their top at different rates, and
            // only their top - the corner below the split is shared, so no
            // asymmetry here can reach the low end.
            const float corner = hfBase * hfWander * fx::exp2Fast (lean * asym * 0.35f * depth);

            ch.hfA.setCutoff (corner, sr);
            ch.hfB.setCutoff (corner * 2.2f, sr);
            ch.lowCut.setCutoff (lfBase, sr);

            const float absorbCorner = juce::jmax (350.0f,
                                                   hfBase * 1.6f
                                                 * (1.0f - absorbDepth * absorbNorm
                                                         * (1.0f + 0.25f * lean * asym)));
            ch.absorb.setCutoff (absorbCorner, sr);

            // 3: drive and bias both lean, so the two channels build different
            // harmonic signatures rather than the same one at two levels.
            const float d = juce::jlimit (0.25f, 6.0f, drive * (1.0f + 0.12f * lean * asym));
            const float b = c.satBias * depth * (1.0f + 0.35f * lean * asym);

            ch.satDrive  = d;
            ch.satBias   = juce::jlimit (-0.45f, 0.45f, b);
            ch.satOffset = fx::tanhFast (ch.satBias);
            ch.satScale  = 1.0f / juce::jmax (0.05f, d * (1.0f - ch.satOffset * ch.satOffset));

            ch.resPre.setCutoff (juce::jmin ((float) sr * 0.45f, (float) sr * 0.42f / resDivisor), sr);

            for (int k = 0; k < 3; ++k)
                ch.diffuse[k].setCoefficient (diffuseG * kDiffuseWeights[k]);

            // Opposite signs: the two channels get phase responses that differ
            // as much as possible while both stay magnitude-flat, so the image
            // opens without either channel being filtered.
            for (int k = 0; k < 2; ++k)
                ch.decor[k].setCoefficient ((i == 0 ? 1.0f : -1.0f) * decor * kDecorWeights[k]);

            // 5b: the differential part of the drift.  Mid and high only.
            ch.wobblePhase += (ch.wobbleRate * (1.0f + 1.5f * movement)) / controlRate;
            ch.wobblePhase -= std::floor (ch.wobblePhase);

            ch.driftSamples = driftMaxDepth * fx::sineTurns (ch.wobblePhase) * differential;

            ch.asymGain = 1.0f + 0.12f * lean * asym * depth;

            const float channelNoise = noiseLevel * 0.8f * (1.0f + 0.5f * lean * asym);
            ch.noiseGain = juce::jmax (0.0f, channelNoise);

            // Generation IV only: a momentary drop in one channel.  Rolled
            // independently per channel, so the two almost never coincide.
            if (dropChance > 0.001f)
            {
                if (--ch.dropTicks <= 0)
                {
                    if (ch.dropTarget < 0.999f)
                    {
                        ch.dropTarget = 1.0f;
                        ch.dropTicks  = (int) (controlRate * (0.30f + 2.0f * ch.noiseRng.next01()));
                    }
                    else if (ch.noiseRng.next01() < dropChance * 0.0035f)
                    {
                        ch.dropTarget = 1.0f - (0.35f + 0.50f * ch.noiseRng.next01()) * dropChance;
                        ch.dropTicks  = (int) (controlRate * (0.02f + 0.06f * ch.noiseRng.next01()));
                    }
                    else
                    {
                        ch.dropTicks = 8;
                    }
                }
            }
            else
            {
                ch.dropTarget = 1.0f;
                ch.dropTicks  = 0;
            }

            ch.dropGain += (ch.dropTarget - ch.dropGain) * 0.25f;
        }
    }

    // -----------------------------------------------------------------------
    //  Audio rate
    // -----------------------------------------------------------------------
    void CopyStage::process (float* left, float* right, int numSamples, const StageMod& mod) noexcept
    {
        for (int base = 0; base < numSamples; base += kControlInterval)
        {
            const int end = juce::jmin (numSamples, base + kControlInterval);

            const float pulse  = (mod.pulse  != nullptr ? mod.pulse [base] : 0.0f) * mod.pulseDepth;
            const float breath = (mod.breath != nullptr ? mod.breath[base] : 0.0f) * mod.breathDepth;

            updateControl (pending, pulse, breath);

            for (int i = base; i < end; ++i)
            {
                const float mono = 0.5f * (left[i] + right[i]);

                // -- 2  transient detection, linked across the channels ------
                const float a = std::abs (mono);
                const float fast = fastEnv.process (a);
                const float slow = slowEnv.process (a);
                const float excess = juce::jlimit (0.0f, 4.0f, fast / (slow + 1.0e-5f) - 1.0f);

                tGain = transientGain.process (1.0f / (1.0f + excess * tAmount));

                // -- 12 the correlated half of the noise ----------------------
                const float wc  = noiseRngCommon.nextBipolar();
                const float lpc = noiseColourCommon.lowpass (wc);
                const float nc  = (wc - lpc) * noiseHpBright + lpc * noiseLpDark;

                // -- 4  one decimation clock for both channels ----------------
                resAcc += resIncrement;
                const bool grab = (resAcc >= 1.0f);
                if (grab)
                    resAcc -= 1.0f;

                float out[2];

                for (int cIdx = 0; cIdx < 2; ++cIdx)
                {
                    auto& ch = channel[cIdx];
                    float x = (cIdx == 0) ? left[i] : right[i];

                    // 5  pitch instability, common component.  The read is
                    //    identical on both channels, so this cannot decorrelate.
                    ch.wobbleDelay.write (x);
                    x = ch.wobbleDelay.read (wobbleBase + wobbleSamples);

                    // 2  transient softness
                    x *= tGain;

                    // 1  bandwidth: bottom first, then two poles off the top
                    x = ch.lowCut.highpass (x);
                    x = ch.hfA.lowpass (x);
                    x = ch.hfB.lowpass (x);

                    // 10 HF absorption
                    x = ch.absorb.lowpass (x);

                    // 3  saturation
                    x = (fx::tanhFast (x * ch.satDrive + ch.satBias) - ch.satOffset) * ch.satScale;
                    x = ch.dc.process (x);

                    // 12 noise injection, before the rest of the copy, so this
                    //    copy's noise is resampled and coloured by this copy and
                    //    copied again by every copy after it
                    const float w  = ch.noiseRng.nextBipolar();
                    const float lp = ch.noiseColour.lowpass (w);
                    const float nch = ch.noiseGuard.highpass ((w - lp) * noiseHpBright + lp * noiseLpDark);

                    x += noiseCommonGain * nc + ch.noiseGain * nch;

                    // 4  resampling: decimate, then reconstruct badly
                    const float pre = fx::lerp (x, ch.resPre.lowpass (x), resPreMix);

                    if (grab)
                    {
                        ch.resPrev = ch.resCur;
                        ch.resCur  = pre;
                    }

                    x = fx::lerp (fx::lerp (ch.resPrev, ch.resCur, resAcc), ch.resCur, holdBlend);

                    // 6  phase diffusion, identical on both channels
                    x = ch.diffuse[0].process (x);
                    x = ch.diffuse[1].process (x);
                    x = ch.diffuse[2].process (x);

                    // 8  harmonic colouration, on the body band only and
                    //    normalised by that band's own envelope so it stays a
                    //    signature rather than becoming another saturator.  The
                    //    three bands sum back to the input exactly, so this is
                    //    the only thing the split changes.
                    float low, mid, high;
                    ch.split.split (x, low, mid, high);

                    const float env = ch.colourEnv.process (std::abs (mid));
                    const float u   = juce::jlimit (-2.0f, 2.0f, mid / (env + 0.02f));
                    const float u2  = u * u;

                    // The squared term has a non-zero mean for anything but a
                    // unit sine, and that mean is a DC offset proportional to
                    // the envelope.  It is removed here rather than left for
                    // the next copy's input blocker, because the last copy in
                    // the chain does not have a next copy.
                    const float colour = colourEvenGain * (u2 - 0.5f)
                                       + colourOddGain  * (u2 * u - 0.75f * u);

                    float y = low + mid + high + ch.colourDc.process (colour * env);

                    // 5b the differential half of the drift, and 7 the stereo
                    //    decorrelation.  Both are full band on purpose: an
                    //    allpass and a delay are magnitude-flat, so running them
                    //    on the whole signal cannot ripple a crossover, and the
                    //    side high pass below removes the low frequency
                    //    difference they create far more exactly than a band
                    //    split would have avoided it.
                    ch.driftDelay.write (y);
                    y = ch.driftDelay.read (driftBase + ch.driftSamples);
                    y = ch.decor[0].process (y);
                    y = ch.decor[1].process (y);

                    // 11 channel asymmetry, and generation IV's dropouts
                    y *= ch.asymGain * ch.dropGain;

                    if (holeAmount > 0.0001f)
                        y -= holeAmount * (ch.holeHi.lowpass (y) - ch.holeLo.lowpass (y));

                    // 9  spectral aging
                    out[cIdx] = ch.tilt.process (y, tiltAmount);
                }

                // The low end stays put.  Everything above happened per channel;
                // this makes the result mono below the split by construction,
                // whatever any of it did.
                const float m = 0.5f * (out[0] + out[1]);
                const float s = 0.5f * (out[0] - out[1]);
                const float sHp = sideHpB.highpass (sideHpA.highpass (s));
                const float side = fx::lerp (s, sHp, sideMix);

                left[i]  = fx::guard (m + side);
                right[i] = fx::guard (m - side);
            }
        }
    }
}
