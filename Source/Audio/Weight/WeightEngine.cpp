#include "WeightEngine.h"

#include "../DspCommon.h"

#include <array>
#include <cmath>

/*
    =======================================================================
    WEIGHT                                          specification section 74
    =======================================================================

    The last stage in the instrument.  Everything else has already had its say,
    so this one is deliberately macro-neutral: it reads macro_weight and
    weight_mode and nothing else from the macro system.

    -----------------------------------------------------------------------
    SUB - the bottom, without the bottom
    -----------------------------------------------------------------------

    Adding level below 60 Hz does nothing on most playback systems and eats all
    the headroom, so SUB works by harmonic reinforcement: it generates the
    harmonics of the fundamental that let the ear infer a bass note the speaker
    cannot reproduce.  The second harmonic of a 40 Hz fundamental is 80 Hz,
    which is squarely where a phone speaker lives.

    The generator is a CHEBYSHEV PAIR on a level-normalised copy of the low
    band:

        T2(x) = 2x^2 - 1                    the 2nd harmonic, exactly
        T4(x) = 8x^4 - 8x^2 + 1             the 4th, with some 2nd

    For a sinusoidal fundamental, T2(cos t) = cos 2t and T4(cos t) = cos 4t:
    exact harmonics, at exactly the right phase, with no delay anywhere in the
    generator.  `weight_harmonics` sets how much is generated and slides the
    mix from pure T2 towards T2 + T4.

    PHASE AWARENESS, which the specification asks for by name, is a property of
    the structure rather than a correction applied afterwards:

      1  Both polynomials are EVEN.  An even function of a signal contains only
         even-order products, so the generator emits nothing at the fundamental
         at all - at any amplitude, for any input.  It therefore cannot cancel
         or thin the note it is reinforcing.  (This is why T3 is not used: the
         odd Chebyshev polynomials only stay free of a fundamental term when the
         input is exactly unit amplitude, and a generator that emits a
         negative-going fundamental when its normalisation is off by a decibel
         is exactly the failure mode being guarded against.)
      2  The generator is memoryless - no delay, no allpass, no filter inside
         it - so its harmonics leave it phase-locked to the waveform that
         produced them.
      3  What happens to them afterwards is three first-order filters: a 6 Hz
         DC blocker, a 55 Hz highpass and a 600 Hz lowpass.  At the 2nd
         harmonic of a 40 Hz note those rotate it by about 4, 35 and 8 degrees
         respectively - 46 degrees in total, for 1.9 dB of loss.  That
         rotation is accepted rather than avoided, and it is worth being clear
         about why: because the generator emits nothing at the fundamental,
         NOTHING DOWNSTREAM OF IT CAN CREATE A COMPONENT AT THE FUNDAMENTAL
         EITHER.  No amount of filtering the generated signal can make it
         cancel the note.  What the rotation changes is how the harmonic's
         peaks line up with the fundamental's, which moves the crest factor of
         the sum a little; it does not move the pitch the ear infers, because
         the ear resolves these harmonics separately and is insensitive to
         their relative phase when it does.
      4  The 55 Hz highpass is there on its own merits as well as for phase.
         An even nonlinearity produces a DC term whose size depends on how well
         the normalisation is tracking, so a note's attack puts an
         envelope-rate thump into the generated signal.  Two poles of highpass
         (6 Hz and 55 Hz) put a 10 Hz artefact 16 dB down and mean that what
         SUB adds lands between 55 and 600 Hz - which is where the harmonics
         were supposed to go in the first place.  Adding energy below 55 Hz is
         the thing this mode exists to avoid.
      5  The generator runs on the MONO SUM of the two low bands and its output
         is added equally to both channels, so what it adds is perfectly
         correlated and survives a mono fold-down intact.  The existing low band
         is never collapsed - it is only ever gained, and identically in both
         channels, so Weight cannot change the low end's stereo image in either
         direction.

    The normalisation is a 1 ms / 150 ms peak follower rather than an averager,
    so it reaches the level of a new note within a fraction of one cycle and
    holds it through the decay.  That matters: when the follower lags, the
    normalised copy clips, and a clipped even polynomial is a constant - which
    is to say, a thump.  A fast peak follower bounds that to the first part of
    the first cycle, and the highpass pair removes what is left.

    On top of that, SUB applies at most +2.3 dB of linear lift to the low band
    and runs the band compressor inside it.  The lift is small on purpose: the
    weight is supposed to come from the harmonics.

    -----------------------------------------------------------------------
    BODY - where physical size lives
    -----------------------------------------------------------------------

    150 - 700 Hz, split out with fx::ThreeBand and shaped on its own.  The voice
    engine already solved a version of this (synth::SynthBody) and the lesson it
    encodes is that a waveshaper on the full band makes a sound bigger and
    dirtier at the same rate, so the band that carries size gets split out and
    shaped by itself while the top of the spectrum - where distortion is audible
    AS distortion - is never touched.

    This is not that class reused.  SynthBody is tuned for one voice's raw
    oscillator sum and splits at 90 Hz with up to 3.6x of drive; this runs on a
    finished mix, which has far more energy below 150 Hz and far more spectral
    density everywhere, so:

        split at 150 Hz, not 90     a mix's bass fundamentals must stay out of
                                    the shaper, or size becomes mud
        drive at most 2.1x, not 3.6 a dense mix reaches the same curvature with
                                    much less drive
        linked dynamics             one gain from both channels, so the stereo
                                    image cannot move
        harmonics-controlled tilt   weight_harmonics slides the shaper from
                                    symmetric (odd, firm) to asymmetric (even,
                                    warm) and sets how much drive there is at
                                    all, so at 0 the mode is a dynamic band
                                    lift and at 1 it is harmonic saturation

    -----------------------------------------------------------------------
    AIR - detail, not level
    -----------------------------------------------------------------------

    The failure mode at the top is harshness, and boosting 12 kHz on a signal
    with nothing at 12 kHz only raises noise.  So AIR does two things: a gentle
    shelf above 6 kHz (at most +3.2 dB), and a generator that MAKES the top.

    The generator takes the 2 - 5.5 kHz band, squares it against its own
    envelope and high-passes the result twice at 6 kHz.  Squaring a band whose
    content lies below fs/4 produces sum and difference products that are all
    below Nyquist by construction, which is why the source band stops at
    5.5 kHz: at 44.1 kHz, fs/4 is 11 kHz.  The band's filters are two poles
    each, so leakage above 11 kHz is attenuated by at least 16 dB before being
    squared - see the limitations for what that does and does not prove.

    The band compressor runs on the high band, which is also what keeps the
    generated air from turning into a sibilant edge.

    -----------------------------------------------------------------------
    weight_compress - dynamic control INSIDE the band
    -----------------------------------------------------------------------

    One compressor design, used by all three modes on their own band:

        threshold   half of a 300 ms envelope of the band itself, so the
                    compressor always works around the material's own level
                    rather than an absolute one.  A weight stage should behave
                    the same way at -20 dBFS as at -6.
        ratio       1:1 to 2.5:1 across weight_compress.  Gentle, by design:
                    this is a weight stage, not a limiter.
        detection   program-dependent, feed-forward, no lookahead.  8 ms up,
                    120 ms down, linked across the two channels.
        makeup      normalised at twice the threshold, which is roughly the
                    average operating point.  Peaks come down, the average
                    stays put, quiet passages come up: density at constant
                    average level, which is what makes weight feel solid
                    rather than peaky.

    Because the threshold is derived from the input band and never from the
    output, there is no feedback path anywhere in this engine.

    -----------------------------------------------------------------------
    GAIN COMPENSATION - specification 148, "do not use loudness to fake quality"
    -----------------------------------------------------------------------

    Three parts, in order of how much work they do:

      1  STRUCTURAL.  Each stage divides out the gain it knows it added.  The
         BODY shaper divides by its own drive, so driving it harder changes the
         curvature and not the level.  The compressor's makeup is normalised at
         its operating point rather than at its threshold.

      2  FEED-FORWARD ESTIMATE.  Each mode reports the broadband power ratio it
         expects to have produced, from its own controls and a fixed assumption
         about how much of a mix's power lives in its band (35 % below 110 Hz,
         40 % in the low mids, 12 % above 6 kHz).  The reciprocal square root of
         that is applied as a static trim.  It is an estimate about a spectrum
         this engine cannot see, so it is deliberately conservative.

      3  MEASURED RESIDUAL.  Two loudness meters, one on the input and one on
         the output, each a K-weighting-like pre-filter (two poles of highpass
         at 70 Hz, plus 4 dB of shelf above 1.5 kHz) into a 1.2 s mean square.
         The output is trimmed by sqrt(inputMeanSquare / outputMeanSquare),
         clamped to +-3 dB and updated once per block, ramped across it.

         The weighting matters: an unweighted meter would measure SUB's new low
         end as loudness and pull the whole instrument down for adding exactly
         the thing it was asked to add.  The time constant matters too - at
         1.2 s this corrects a setting, not a transient, and because both meters
         see the same programme through the same filters their ratio is steady
         for steady material.

    Everything above is multiplied into a single gain that is itself faded in
    by macro_weight, so at zero the gain is exactly 1.0.

    MACRO RESPONSE.  Deliberately none beyond macro_weight and weight_mode, for
    the reason given at the top: Weight is last, and every other engine has
    already applied its share of age, grit, movement and scale.  MacroState's
    own `weight` and `weightMode` copies are ignored in favour of the registry,
    which is the authoritative source and is guaranteed to be populated.

    BYPASS.  With macro_weight at zero and the fade run out, process() returns
    before touching the buffer.  Even before that point the path is exact by
    construction: every mode is written as `in + amount * (something)` over
    complementary band splits that reconstruct their input, and the output gain
    is `1 + amount * (trim - 1)`, so amount = 0 is the identity.

    KNOWN LIMITATIONS - read these before believing anything above.

      - Nobody has listened to this.  Every claim here is about the algorithm.
      - SUB's generator is a nonlinearity on a summed low band.  With two bass
        notes sounding at once it produces intermodulation products as well as
        harmonics - a difference tone below both fundamentals.  Every harmonic
        bass exciter has this property; the 110 Hz split and the 600 Hz lowpass
        bound where the products can land, but they do not remove them.
      - SUB still has a residual low-frequency artefact at a note's onset.  An
        even nonlinearity generates DC, the amount of DC depends on how well
        the normalisation is tracking, and no follower tracks the first part of
        the first cycle.  The peak follower and the two highpasses reduce it by
        roughly 16 dB at 10 Hz and put the rest above 55 Hz where it reads as
        part of the attack, but it is a reduction, not a removal, and it has
        not been measured.
      - Nothing here is oversampled.  BODY's shaper runs on a band limited to
        700 Hz by a one-pole, so its own products are low, but the leakage above
        that corner is shaped by a nonlinearity at the base rate.  AIR's
        generator is safe by construction for content inside its source band and
        unproven for the leakage above it: two poles at 5.5 kHz put the leakage
        at 11 kHz about 16 dB down, so its squared products land about 32 dB
        below the generated air before the depth control, which is a
        calculation and not a measurement.
      - The feed-forward trim assumes a fixed spectral balance.  A patch that is
        all sub, or all air, will be mis-estimated; the measured residual then
        has to do the work, and it is clamped at +-3 dB.
      - The measured residual cannot distinguish "Weight made this louder" from
        "the player played louder", over a 1.2 s window, when the two coincide.
        In practice both meters see the same programme, so the ratio is stable,
        but a step change in playing level will produce a slow gain drift of up
        to 3 dB until it settles.
      - A mode that is not sounding keeps its filter state frozen.  It is woken
        with its envelopes seeded from the measured programme level and faded in
        from silence over 12 ms, which covers it, but the first few milliseconds
        after a mode change are running filters whose state is older than the
        crossfade.
      - The band compressor has no lookahead and no soft knee in the classical
        sense; the knee comes from the relative threshold, not from the curve.
*/

namespace nacar
{
    using namespace fx;

    namespace
    {
        // -- crossovers -----------------------------------------------------
        constexpr float kSubXoverHz   = 110.0f;
        constexpr float kSubGeneratorLowHz  = 55.0f;    // what SUB adds starts here
        constexpr float kSubGeneratorHighHz = 600.0f;   // and stops here
        constexpr float kBodyLowHz    = 150.0f;
        constexpr float kBodyHighHz   = 700.0f;
        constexpr float kAirShelfHz   = 6000.0f;
        constexpr float kAirSrcLowHz  = 2000.0f;
        constexpr float kAirSrcHighHz = 5500.0f;

        // -- depths ---------------------------------------------------------
        constexpr float kSubLift        = 0.30f;   // +2.3 dB of low band at full
        constexpr float kSubHarmonic    = 0.45f;   // generated level, times env
        constexpr float kBodyLowLift    = 0.12f;
        constexpr float kBodyMakeup     = 1.10f;
        constexpr float kAirShelfDepth  = 0.45f;   // +3.2 dB at full
        constexpr float kAirGenDepth    = 0.50f;

        /** Assumed share of a mix's power in each mode's band.  Used only by
            the feed-forward half of the gain compensation. */
        constexpr float kLowShare  = 0.35f;
        constexpr float kMidShare  = 0.40f;
        constexpr float kHighShare = 0.12f;

        constexpr float kModeFadeSeconds = 0.012f;

        // ===================================================================
        //  Local helpers.  Everything shared lives in DspCommon; these two are
        //  the block-rate smoother pattern SynthEngine.cpp keeps privately and
        //  an asymmetric envelope follower.
        // ===================================================================

        struct Smoothed
        {
            float current = 0.0f;
            bool  primed  = false;
            Ramp  ramp;

            void set (float target, int numSamples, float coef) noexcept
            {
                if (! primed)
                {
                    current = target;
                    primed = true;
                }

                const float next = target + coef * (current - target);
                ramp.set (current, next, numSamples);
                current = next;
            }

            forcedinline float at (int i) const noexcept { return ramp.at (i); }
            float value() const noexcept { return current; }

            void hold (float v) noexcept { current = v; primed = true; ramp.snap (v); }
            void reset() noexcept { current = 0.0f; primed = false; ramp.snap (0.0f); }
        };

        struct Follower
        {
            float z = 0.0f, aUp = 0.0f, aDown = 0.0f;

            static float coefficient (float seconds, double sampleRate) noexcept
            {
                const double s = juce::jmax (1.0e-5, (double) seconds);
                return (float) std::exp (-1.0 / (s * juce::jmax (1.0, sampleRate)));
            }

            void prepare (float upSeconds, float downSeconds, double sampleRate) noexcept
            {
                aUp   = coefficient (upSeconds, sampleRate);
                aDown = coefficient (downSeconds, sampleRate);
                z = 0.0f;
            }

            void reset() noexcept { z = 0.0f; }
            void set (float v) noexcept { z = v; }

            forcedinline float process (float rectified) noexcept
            {
                const float a = (rectified > z) ? aUp : aDown;
                z = rectified + a * (z - rectified);
                return (z = flush (z));
            }
        };

        // ===================================================================
        //  BAND COMPRESSOR
        //
        //  See the header for the design.  Feed-forward, linked, relative
        //  threshold, gentle ratio, makeup normalised at the operating point.
        // ===================================================================
        struct BandCompressor
        {
            Follower fast, slow;

            void prepare (double sampleRate) noexcept
            {
                fast.prepare (0.008f, 0.120f, sampleRate);
                slow.prepare (0.200f, 0.500f, sampleRate);
            }

            void reset() noexcept { fast.reset(); slow.reset(); }
            void seed (float level) noexcept { fast.set (level); slow.set (level); }

            /** @param rectified  linked band detector, |L| + |R| halved
                @param depth      0..1, weight_compress scaled by the amount */
            forcedinline float gain (float rectified, float depth) noexcept
            {
                const float e = fast.process (rectified);
                const float s = slow.process (rectified);

                if (depth <= 0.0005f)
                    return 1.0f;

                // Relative threshold: half of the band's own slow level.  The
                // jmax keeps it strictly positive, so the division below is
                // safe at every parameter extreme and in silence.
                const float thr = juce::jmax (1.0e-5f, 0.5f * s);

                // gain = over^(1/ratio - 1), ratio = 1 .. 2.5
                const float expo   = 1.0f / (1.0f + depth * 1.5f) - 1.0f;   // <= 0
                const float makeup = exp2Fast (-expo);                      // >= 1

                if (e <= thr)
                    return makeup;

                return exp2Fast (expo * log2Fast (e / thr)) * makeup;
            }
        };

        // ===================================================================
        //  SUB
        // ===================================================================
        struct SubMode
        {
            TwoBand        splitL, splitR;
            Follower       env;
            DcBlocker      dc;
            OnePoleTPT     harmonicHp, harmonicLp;
            BandCompressor comp;

            void prepare (double sampleRate) noexcept
            {
                splitL.prepare (kSubXoverHz, sampleRate);
                splitR.prepare (kSubXoverHz, sampleRate);

                // Peak follower, not an averager: see the header.  1 ms up so
                // a new note is normalised within a fraction of a cycle,
                // 150 ms down so it holds through the decay.
                env.prepare (0.001f, 0.150f, sampleRate);

                dc.prepare (sampleRate);
                harmonicHp.setCutoff (kSubGeneratorLowHz, sampleRate);
                harmonicLp.setCutoff (kSubGeneratorHighHz, sampleRate);
                comp.prepare (sampleRate);
                reset();
            }

            void reset() noexcept
            {
                splitL.reset(); splitR.reset();
                env.reset(); dc.reset();
                harmonicHp.reset(); harmonicLp.reset();
                comp.reset();
            }

            void wake (float level) noexcept { env.set (level); comp.seed (level); }

            /** Expected broadband power ratio, for the feed-forward trim. */
            static float powerRatio (float amount, float harmonics) noexcept
            {
                const float lift = 1.0f + kSubLift * amount;
                const float gen  = kSubHarmonic * amount * harmonics;

                return 1.0f + kLowShare * (lift * lift - 1.0f + gen * gen);
            }

            forcedinline void process (float inL, float inR, float& outL, float& outR,
                                       float amount, float harmonics, float compress) noexcept
            {
                float lowL, highL, lowR, highR;
                splitL.split (inL, lowL, highL);
                splitR.split (inR, lowR, highR);

                // The generator sees the mono sum and its output goes back to
                // both channels equally: correlated, and mono-safe.
                const float lowM = 0.5f * (lowL + lowR);
                const float e = env.process (std::abs (lowM));

                // Normalise, so the Chebyshev identities hold and the generated
                // level tracks the note rather than the gain staging.
                const float xn = juce::jlimit (-1.0f, 1.0f, lowM / (e + 1.0e-6f));
                const float x2 = xn * xn;

                const float t2 = 2.0f * x2 - 1.0f;                        // 2nd
                const float t4 = 8.0f * x2 * x2 - 8.0f * x2 + 1.0f;       // 4th

                // Both even, so neither contains anything at the fundamental.
                const float even = lerp (t2, 0.55f * t2 + 0.75f * t4, harmonics);

                // Everything this mode adds lives between 55 and 600 Hz.
                float h = dc.process (even * e);
                h = harmonicLp.lowpass (harmonicHp.highpass (h));
                h *= kSubHarmonic * amount * harmonics;

                const float g = comp.gain (std::abs (lowM), compress * amount);
                const float lowGain = lerp (1.0f, (1.0f + kSubLift) * g, amount);

                outL = lowL * lowGain + highL + h;
                outR = lowR * lowGain + highR + h;
            }
        };

        // ===================================================================
        //  BODY
        // ===================================================================
        struct BodyMode
        {
            ThreeBand      bandsL, bandsR;
            DcBlocker      dcL, dcR;
            BandCompressor comp;

            void prepare (double sampleRate) noexcept
            {
                bandsL.prepare (kBodyLowHz, kBodyHighHz, sampleRate);
                bandsR.prepare (kBodyLowHz, kBodyHighHz, sampleRate);
                dcL.prepare (sampleRate);
                dcR.prepare (sampleRate);
                comp.prepare (sampleRate);
                reset();
            }

            void reset() noexcept
            {
                bandsL.reset(); bandsR.reset();
                dcL.reset(); dcR.reset(); comp.reset();
            }

            void wake (float level) noexcept { comp.seed (level); }

            static float powerRatio (float amount, float /*harmonics*/) noexcept
            {
                const float band = 1.0f + amount * (kBodyMakeup - 1.0f);
                const float low  = 1.0f + kBodyLowLift * amount;

                return 1.0f + kMidShare * (band * band - 1.0f)
                            + kLowShare * (low * low - 1.0f);
            }

            forcedinline float shape (float mid, float drive, float bias, float tilt,
                                      DcBlocker& dc) noexcept
            {
                const float d = mid * drive;

                const float sym  = tanhFast (d);
                const float asym = tanhFast (d + bias) - tanhFast (bias);

                // Divide the small-signal gain back out: driving the shaper
                // harder has to change the curvature, not the level.
                return dc.process (lerp (sym, asym, tilt)) / drive;
            }

            forcedinline void process (float inL, float inR, float& outL, float& outR,
                                       float amount, float harmonics, float compress) noexcept
            {
                float lowL, midL, highL, lowR, midR, highR;
                bandsL.split (inL, lowL, midL, highL);
                bandsR.split (inR, lowR, midR, highR);

                const float drive = 1.0f + amount * (0.5f + 0.6f * harmonics);
                const float bias  = 0.30f * amount * harmonics;

                const float shapedL = shape (midL, drive, bias, harmonics, dcL);
                const float shapedR = shape (midR, drive, bias, harmonics, dcR);

                // Linked, so the stereo image cannot move with the dynamics.
                const float g = comp.gain (0.5f * (std::abs (midL) + std::abs (midR)),
                                           compress * amount);

                const float midOutL = lerp (midL, shapedL * g * kBodyMakeup, amount);
                const float midOutR = lerp (midR, shapedR * g * kBodyMakeup, amount);

                // The low band gets a small, purely linear lift and nothing
                // else: mass down there is amplitude, not harmonics.
                const float lowGain = 1.0f + kBodyLowLift * amount;

                outL = lowL * lowGain + midOutL + highL;
                outR = lowR * lowGain + midOutR + highR;
            }
        };

        // ===================================================================
        //  AIR
        // ===================================================================
        struct AirMode
        {
            struct Side
            {
                TwoBand    shelf;
                OnePoleTPT srcHpA, srcHpB, srcLpA, srcLpB;
                OnePoleTPT genHpA, genHpB;
                Follower   srcEnv;

                void prepare (double sampleRate) noexcept
                {
                    shelf.prepare (kAirShelfHz, sampleRate);
                    srcHpA.setCutoff (kAirSrcLowHz, sampleRate);
                    srcHpB.setCutoff (kAirSrcLowHz, sampleRate);
                    srcLpA.setCutoff (kAirSrcHighHz, sampleRate);
                    srcLpB.setCutoff (kAirSrcHighHz, sampleRate);
                    genHpA.setCutoff (kAirShelfHz, sampleRate);
                    genHpB.setCutoff (kAirShelfHz, sampleRate);
                    srcEnv.prepare (0.006f, 0.080f, sampleRate);
                    reset();
                }

                void reset() noexcept
                {
                    shelf.reset();
                    srcHpA.reset(); srcHpB.reset(); srcLpA.reset(); srcLpB.reset();
                    genHpA.reset(); genHpB.reset();
                    srcEnv.reset();
                }

                /** The generated top: the 2 - 5.5 kHz band squared against its
                    own envelope, then high-passed twice at the shelf corner so
                    that only the new content survives. */
                forcedinline float generate (float x) noexcept
                {
                    const float band = srcLpB.lowpass (srcLpA.lowpass (
                                           srcHpB.highpass (srcHpA.highpass (x))));

                    const float e = srcEnv.process (std::abs (band));
                    const float squared = band * band / (e + 1.0e-5f);

                    return genHpB.highpass (genHpA.highpass (squared));
                }
            };

            std::array<Side, 2> side;
            BandCompressor comp;

            void prepare (double sampleRate) noexcept
            {
                for (auto& s : side)
                    s.prepare (sampleRate);

                comp.prepare (sampleRate);
                reset();
            }

            void reset() noexcept
            {
                for (auto& s : side)
                    s.reset();

                comp.reset();
            }

            void wake (float level) noexcept { comp.seed (level); }

            static float powerRatio (float amount, float harmonics) noexcept
            {
                const float shelf = 1.0f + kAirShelfDepth * amount;
                const float gen   = kAirGenDepth * amount * harmonics;

                return 1.0f + kHighShare * (shelf * shelf - 1.0f + gen * gen);
            }

            forcedinline void process (float inL, float inR, float& outL, float& outR,
                                       float amount, float harmonics, float compress) noexcept
            {
                float lowL, highL, lowR, highR;
                side[0].shelf.split (inL, lowL, highL);
                side[1].shelf.split (inR, lowR, highR);

                const float genL = side[0].generate (inL);
                const float genR = side[1].generate (inR);

                const float g = comp.gain (0.5f * (std::abs (highL) + std::abs (highR)),
                                           compress * amount);

                const float shelfGain = 1.0f + kAirShelfDepth * amount;
                const float genGain   = kAirGenDepth * amount * harmonics;

                // The shelf is faded in by amount; the compressor only acts on
                // what the shelf added, so at amount 0 the high band is exactly
                // the high band.
                const float highOutL = highL * lerp (1.0f, shelfGain * g, amount);
                const float highOutR = highR * lerp (1.0f, shelfGain * g, amount);

                outL = lowL + highOutL + genL * genGain;
                outR = lowR + highOutR + genR * genGain;
            }
        };

        // ===================================================================
        //  LOUDNESS METER
        //
        //  A K-weighting-like pre-filter into a long mean square.  It exists
        //  only so the output trim measures perceived level rather than energy:
        //  an unweighted meter hears SUB's new low end as loudness and pulls
        //  the instrument down for doing its job.
        // ===================================================================
        struct Loudness
        {
            OnePoleTPT hpA, hpB;
            TwoBand    shelf;
            float meanSquare = 0.0f;
            float coef = 0.999f;

            void prepare (double sampleRate) noexcept
            {
                hpA.setCutoff (70.0f, sampleRate);
                hpB.setCutoff (70.0f, sampleRate);
                shelf.prepare (1500.0f, sampleRate);

                // 1.2 s: this corrects a setting, not a transient.
                coef = Follower::coefficient (1.2f, sampleRate);
                meanSquare = 0.0f;
            }

            void reset() noexcept
            {
                hpA.reset(); hpB.reset(); shelf.reset();
                meanSquare = 0.0f;
            }

            forcedinline void push (float mono) noexcept
            {
                float w = hpB.highpass (hpA.highpass (mono));

                float low, high;
                shelf.split (w, low, high);
                w = low + high * 1.6f;              // about +4 dB above 1.5 kHz

                const float sq = w * w;
                meanSquare = flush (sq + coef * (meanSquare - sq));
            }
        };

        inline float blockCoefficient (float seconds, int numSamples, double sampleRate) noexcept
        {
            const double blockSeconds = (double) juce::jmax (1, numSamples)
                                      / juce::jmax (1.0, sampleRate);
            return (float) std::exp (-blockSeconds / juce::jmax (1.0e-4, (double) seconds));
        }
    }

    // =======================================================================
    //  Impl
    // =======================================================================
    struct WeightEngine::Impl
    {
        double sampleRate = 48000.0;

        SubMode  sub;
        BodyMode body;
        AirMode  air;

        std::array<float, 3> modeGain { { 0.0f, 1.0f, 0.0f } };   // BODY is the default
        bool  modePrimed = false;
        float modeStep = 0.01f;

        Loudness inMeter, outMeter;
        float trim = 1.0f;
        Ramp  trimRamp;

        Smoothed amount, harmonics, compress;

        void prepare (const EngineSpec& spec)
        {
            sampleRate = juce::jmax (8000.0, spec.sampleRate);

            sub .prepare (sampleRate);
            body.prepare (sampleRate);
            air .prepare (sampleRate);

            inMeter .prepare (sampleRate);
            outMeter.prepare (sampleRate);

            modeStep = 1.0f / juce::jmax (1.0f, (float) (sampleRate * kModeFadeSeconds));

            reset();
        }

        void reset() noexcept
        {
            sub.reset();
            body.reset();
            air.reset();

            inMeter.reset();
            outMeter.reset();

            modeGain = { { 0.0f, 0.0f, 0.0f } };
            modeGain[1] = 1.0f;
            modePrimed = false;

            trim = 1.0f;
            trimRamp.snap (1.0f);

            amount.reset();
            harmonics.reset();
            compress.reset();
        }

        void process (juce::AudioBuffer<float>& buffer, const ParameterRegistry& p,
                      const MacroState& m) noexcept
        {
            const int n = juce::jmin (m.numSamples, buffer.getNumSamples());

            if (n <= 0 || buffer.getNumChannels() < 1)
                return;

            const float amountTarget = juce::jlimit (0.0f, 1.0f, p.raw (PID::macroWeight));

            // -- exact bypass -----------------------------------------------
            if (amountTarget <= 0.0f && amount.value() <= 1.0e-4f)
            {
                amount.hold (0.0f);
                return;
            }

            const int   mode = juce::jlimit (0, 2, p.choice (PID::weightMode));
            const float harmTarget = juce::jlimit (0.0f, 1.0f, p.raw (PID::weightHarmonics));
            const float compTarget = juce::jlimit (0.0f, 1.0f, p.raw (PID::weightCompress));

            const float coef = blockCoefficient (0.020f, n, sampleRate);

            amount   .set (amountTarget, n, coef);
            harmonics.set (harmTarget,   n, coef);
            compress .set (compTarget,   n, coef);

            // -- wake a mode that is about to come back ----------------------
            //  Seeded from the measured programme level, which is at or above
            //  any single band's level, so a woken compressor errs towards
            //  doing nothing rather than towards a dip.
            const float programme = std::sqrt (juce::jmax (0.0f, inMeter.meanSquare));

            // The first block after prepare() or reset() snaps to the selected
            // mode.  Crossfading into it from whatever was last selected would
            // mean 12 ms of the wrong mode every time a preset loads.
            if (! modePrimed)
            {
                modeGain = { { 0.0f, 0.0f, 0.0f } };
                modeGain[(size_t) mode] = 1.0f;
                modePrimed = true;
            }

            if (modeGain[(size_t) mode] <= 0.0f)
            {
                if (mode == 0) sub.wake (programme);
                if (mode == 1) body.wake (programme);
                if (mode == 2) air.wake (programme);
            }

            // -- the gain that keeps this stage honest ------------------------
            const float a = amount.value();
            const float h = harmonics.value();

            const float ratio = modeGain[0] * SubMode ::powerRatio (a, h)
                              + modeGain[1] * BodyMode::powerRatio (a, h)
                              + modeGain[2] * AirMode ::powerRatio (a, h);

            const float sumGain = juce::jmax (1.0e-6f, modeGain[0] + modeGain[1] + modeGain[2]);
            const float staticTrim = 1.0f / std::sqrt (juce::jmax (0.05f, ratio / sumGain));

            const float measured = std::sqrt ((inMeter.meanSquare + 1.0e-10f)
                                            / (outMeter.meanSquare + 1.0e-10f));

            const float wanted = juce::jlimit (0.7f, 1.41f, staticTrim * measured);

            trimRamp.set (trim, wanted, n);
            trim = wanted;

            // -- the block ----------------------------------------------------
            const bool  stereo = buffer.getNumChannels() > 1;
            float* const l = buffer.getWritePointer (0);
            float* const r = stereo ? buffer.getWritePointer (1) : nullptr;

            for (int i = 0; i < n; ++i)
            {
                const float dryL = l[i];
                const float dryR = stereo ? r[i] : dryL;

                inMeter.push (0.5f * (dryL + dryR));

                // -- mode crossfade -----------------------------------------
                for (int mIdx = 0; mIdx < 3; ++mIdx)
                {
                    const float want = (mIdx == mode) ? 1.0f : 0.0f;
                    const float delta = juce::jlimit (-modeStep, modeStep,
                                                      want - modeGain[(size_t) mIdx]);
                    modeGain[(size_t) mIdx] += delta;
                }

                const float g0 = modeGain[0], g1 = modeGain[1], g2 = modeGain[2];
                const float inv = 1.0f / juce::jmax (1.0e-6f, g0 + g1 + g2);

                const float amt  = juce::jlimit (0.0f, 1.0f, amount.at (i));
                const float harm = juce::jlimit (0.0f, 1.0f, harmonics.at (i));
                const float comp = juce::jlimit (0.0f, 1.0f, compress.at (i));

                float wetL = 0.0f, wetR = 0.0f;

                if (g0 > 0.0f)
                {
                    float a0, b0;
                    sub.process (dryL, dryR, a0, b0, amt, harm, comp);
                    wetL += g0 * a0;
                    wetR += g0 * b0;
                }

                if (g1 > 0.0f)
                {
                    float a1, b1;
                    body.process (dryL, dryR, a1, b1, amt, harm, comp);
                    wetL += g1 * a1;
                    wetR += g1 * b1;
                }

                if (g2 > 0.0f)
                {
                    float a2, b2;
                    air.process (dryL, dryR, a2, b2, amt, harm, comp);
                    wetL += g2 * a2;
                    wetR += g2 * b2;
                }

                wetL *= inv;
                wetR *= inv;

                // Faded in by the amount, so amount = 0 is exactly unity.
                const float gain = 1.0f + amt * (trimRamp.at (i) - 1.0f);

                const float yL = guard (wetL * gain);
                const float yR = guard (wetR * gain);

                outMeter.push (0.5f * (yL + yR));

                l[i] = yL;

                if (stereo)
                    r[i] = yR;
            }
        }
    };

    // =======================================================================
    WeightEngine::WeightEngine() : impl (std::make_unique<Impl>()) {}
    WeightEngine::~WeightEngine() = default;

    void WeightEngine::prepare (const EngineSpec& spec) { impl->prepare (spec); }
    void WeightEngine::reset()                          { impl->reset(); }

    void WeightEngine::process (juce::AudioBuffer<float>& buffer, const ParameterRegistry& p,
                                const MacroState& m)
    {
        impl->process (buffer, p, m);
    }
}
