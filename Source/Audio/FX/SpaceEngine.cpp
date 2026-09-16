#include "SpaceEngine.h"

#include <cmath>

/*
    =======================================================================
    SPACE  -  specification section 90
    =======================================================================

    TOPOLOGY

        in ->  air absorption ------------------------------------> direct
          \
           -> subsonic + low cut -> PRE-DELAY -+-> 8 early taps --> early
                                               |
                                               +-> 4 series allpass
                                                    diffusers
                                                        |
                                                        v
                                     +-------- 8-line feedback delay network
                                     |         Hadamard mix, damping and a
                                     |         subsonic cut inside the loop
                                     +---------------------+
                                                           v
                    early * earlyGain + late * lateGain -> tilt -> three-band
                                                                   width stage
                                                                       |
                       out = direct * dryGain  +  wet * wetGain  <-----+

    The wet path is entirely separate from the direct path.  The only thing
    the direct path has done to it is the air-absorption filter that DISTANCE
    needs, and that is scaled by how present the wet is, so at MIX = 0 the
    engine is bit-exact bypass and returns before any of this runs.

    DELAY LENGTHS, AND WHY

        23.52  28.48  33.77  38.98  44.65  49.40  55.15  60.48  ms

    These are the primes 1129, 1367, 1621, 1871, 2143, 2371, 2647 and 2903
    expressed as milliseconds at 48 kHz.  Two properties matter and only one
    of them is the primality.

      - The ratios are mutually incommensurate (the closest pair is 1.10:1).
        Delay lengths in simple ratios share modes; shared modes are what a
        metallic tail is.  Primes are the cheapest way to guarantee no shared
        factors, and scaling all eight by the same size factor preserves the
        ratios, so the property survives at every SIZE and every sample rate.

      - The spread is 2.6:1 and the mean is about 42 ms.  Too narrow a spread
        and the network behaves like one delay; too wide and the shortest line
        colours the tail with a comb before the network has filled in.

    Everything is expressed in milliseconds and converted with the actual
    sample rate, so a room is the same size in a 96 kHz session as in a 44.1
    kHz one.  At sample rates other than 48 kHz the lengths in samples are no
    longer integers or prime, which does not matter: the reads are fractional
    and interpolated, and it was the ratios that were doing the work.

    SIZE multiplies all eight by 0.32 to 2.6 (exponentially, so the default of
    0.55 lands almost exactly on 1.0), and each character multiplies that
    again.  The product is clamped to [0.28, 3.0], which is also what the
    delay lines were dimensioned for.

    DAMPING IS IN THE FEEDBACK PATH

    Each line's output is low-passed and high-passed *before* the mixing
    matrix, so the filter is inside the loop and applies once per circulation.
    That is the whole difference between a reverb that gets darker as it
    decays - which is what a real room does, because air and surfaces absorb
    high frequencies on every bounce - and a reverb that is simply dull, which
    is what damping the output gives you.  The per-line cutoffs are spread
    +-12 % so the eight lines do not all darken in lock step.

    The subsonic cut in the same place is not tone shaping.  It is what stops
    a network running at high feedback from accumulating DC and sub-20 Hz
    energy that nothing else in the chain would remove.

    HOW DISTANCE CREATES DEPTH  (specification section 92: it must be real)

    Distance is not wet level.  In rough order of how much each cue matters:

      1. The early reflections rise against the direct sound.  The early
         level runs 0.42 to 1.0 of the character's early level across the
         control; this is the single strongest distance cue the ear has.
      2. The direct sound loses high frequencies.  A first-order 3.8 kHz low
         pass is blended into the direct path, up to 85 %, which is air
         absorption - the reason a far-away sound is dull as well as quiet.
      3. The pre-delay SHORTENS, by up to 75 %.  This is the cue that is
         usually implemented backwards.  A source at the far end of a room is
         nearly as far from you as its reflections are, so they arrive close
         behind it; a source right in front of you is much closer than the
         walls, so its reflections arrive late.  Long pre-delay reads as
         near, not far.
      4. The image narrows, by up to 28 %.  Distant sources subtend a smaller
         angle.
      5. The tail damping darkens, by up to 35 %, for the same reason as (2).
      6. Only then does the wet/dry ratio move, and only by 14 % of the
         remaining headroom, and the direct level falls by up to 2 dB.

    Cues 2 and 6 touch the direct path, so they are scaled by how present the
    wet signal is (min(1, 2 x the equal-power wet gain)).  At MIX = 0 that
    factor is 0 and the engine has already returned untouched; there is no
    discontinuity as MIX leaves zero.

    THE FIVE CHARACTERS

    Genuinely different configurations of the network, not one reverb with an
    EQ.  See kCharacters below for the numbers.

      ROOM      half size, only two of the four diffusers so the early field
                stays discrete, bright damping, short decay, the strongest
                early reflections and the least modulation.
      CHAMBER   the reference: full diffusion, moderate damping, balanced
                early and late.
      DARK      2.4 kHz damping inside the loop, low early level, slow
                modulation.  Darkens as it decays rather than starting dull.
      DISTANT   largest size, widest early spread, deepest modulation, early
                and late both lifted - the character that is already partly
                "far away" before DISTANCE is touched.
      INFINITE  feedback held at 0.99997 regardless of DECAY, input trimmed to
                half so the network fills rather than floods, the lowest
                subsonic cut and the slowest modulation.

    STABILITY BOUNDS

      - The mixing matrix is an orthonormal Hadamard (scaled by 1/sqrt(8)), so
        the mixing stage itself can neither add nor remove energy.  All of the
        loop gain is in the explicit per-line gain, which is what makes the
        bound below a bound and not a hope.
      - Per-line gain is exp(-3 ln10 T/RT60), clamped to 0.9992, except
        INFINITE which is pinned at 0.99997.  RT60 is clamped to [0.08, 28] s.
      - A soft ceiling sits inside the loop after the gain.  It is exactly
        transparent below 1.5 (about +3.5 dBFS on a single line) and
        asymptotes to 3.0, so it never engages in normal use and turns
        INFINITE from something that accumulates without bound into something
        that fills up and holds.
      - Damping and the subsonic cut are one-poles with gain <= 1 at every
        frequency, so neither can lift the loop gain.
      - The input is guarded before it enters the network, and the wet sum is
        checked for finiteness every sample.  A network that has blown up
        zeroes itself and emits silence rather than poisoning the session.
      - Every delay read is clamped inside fx::DelayLine, and the lines are
        dimensioned in prepare() for the maximum scale at the highest
        supported sample rate, so a read cannot leave its buffer even with
        SIZE and modulation at maximum.
      - Changing SIZE while INFINITE is frozen changes the read positions of
        lines whose loop gain is 1.  That is safe: the matrix is orthonormal
        and the ceiling is in the loop, so the energy already circulating is
        redistributed, not multiplied.

    TAIL LENGTH

    NacarProcessor promises 32 seconds.  RT60 is clamped to 28 s so the tail
    is 60 dB down inside the promise with the pre-delay and the early field
    accounted for.  INFINITE is the documented exception: it is meant to hold,
    and it will hold for hours.

    MACRO RESPONSE  (added to the parameters, never replacing them)

      macros.scale     +0.30 on SIZE.        World makes the room bigger.
      macros.distance  +0.35 on DISTANCE.    World moves the source back - and
                                             because DISTANCE is the cue set
                                             above, not a wet control, World
                                             is not mapped to wet level, which
                                             section 73 explicitly forbids.
      macros.wetBias   +0.25 on MIX.         An offset, not a replacement.
      macros.movement  +0.10 ms of delay modulation depth.  Motion lives on
                                             the tail's movement.
      macros.widthScale multiplies the tail width.  World only: Pulse's
                       own width duck is per-sample and is applied by the
                       chain's output stage, not here.
      macros.pulseToSpace * pulseAt(i) ducks the wet up to 85 %: on every
                                             pulse the sound gets drier, which
                                             is section 83's "a kick makes it
                                             quieter, darker, narrower and
                                             drier at once" seen from here.
      macros.breathAt(i) drifts every delay length by +-0.4 %, slowly and
                                             without repeating.  Breath is
                                             only ever allowed near slow
                                             parameters; at this depth it is a
                                             room that will not sit perfectly
                                             still, not a pitch modulation.

      macros.age, grit and alterAmount are deliberately unused.  Space is not
      an ageing engine and has no alternate identity of its own.

    KNOWN LIMITATIONS

      - Nobody has listened to this.  Every claim above is a claim about the
        structure, not about the sound.
      - The wet level relative to the dry has not been measured against any
        reference reverb.  The injection gain (0.45) and the output tap
        normalisation (0.5) are reasoned, not calibrated.
      - Changing SIZE or PRE-DELAY sweeps the delay reads rather than
        crossfading between two of them, so a fast move glides the tail's
        pitch.  Slow moves are fine and the smoothers are set for that.  A
        crossfading size change is the fix and is not implemented.
      - The early reflection pattern is a fixed synthetic tap list, not an
        image-source model of a real geometry, and it changes with character
        only in level and spread.
      - Eight lines is a modest modal density at the largest sizes.  Above
        about 2.5x scale the tail thins; a real hall setting would want
        sixteen lines or a second diffusion stage after the network.
      - The quality parameter (ECO / STUDIO / ULTRA) is ignored: this engine
        costs the same in all three.
      - CPU has not been measured.
*/

namespace nacar
{
    namespace
    {
        // -- delay lengths, ms at scale 1 (primes at 48 kHz; see above) ------
        constexpr float kLineMs[8] =
        {
            23.5208f, 28.4792f, 33.7708f, 38.9792f,
            44.6458f, 49.3958f, 55.1458f, 60.4792f
        };

        /** Per-line damping cutoff spread.  Eight lines that darken at exactly
            the same rate sound like one filter; +-12 % is enough to stop that
            and small enough not to unbalance the decay. */
        constexpr float kDampSpread[8] =
        { 1.00f, 0.92f, 1.08f, 0.96f, 1.12f, 0.88f, 1.04f, 0.94f };

        /** Modulation rates in Hz, mutually incommensurate and all well below
            the rate at which movement becomes chorus. */
        constexpr float kModHz[8] =
        { 0.071f, 0.093f, 0.117f, 0.139f, 0.163f, 0.187f, 0.211f, 0.239f };

        /** Early reflection times, ms at scale 1, one pattern per channel.
            An early field that is identical on both sides collapses to a point
            and tells the ear nothing about where the walls are; these two
            patterns share no tap time. */
        constexpr float kEarlyMs[2][8] =
        {
            {  8.71f, 14.33f, 21.11f, 29.77f, 38.33f, 47.91f, 58.13f, 67.31f },
            { 10.93f, 16.71f, 24.31f, 32.93f, 41.71f, 51.13f, 61.97f, 71.53f }
        };

        /** Tap gains.  Decaying, with signs that do not alternate regularly -
            a regular alternation is a comb, which is exactly the colouration
            the pattern exists to avoid. */
        constexpr float kEarlyGain[2][8] =
        {
            { 1.00f, -0.82f,  0.67f,  0.55f, -0.45f,  0.37f, -0.30f, -0.25f },
            { 0.93f,  0.78f, -0.64f,  0.52f, -0.43f, -0.35f,  0.29f, -0.23f }
        };

        /** Input diffuser lengths, ms at scale 1.  Short, and different per
            channel for the same reason the early taps are. */
        constexpr float kDiffuserMs[2][4] =
        { {  4.77f,  6.91f, 10.13f, 14.83f },
          {  5.39f,  7.73f, 11.29f, 16.51f } };

        constexpr float kMaxScale     = 3.0f;    ///< what the lines are sized for
        constexpr float kMaxDiffScale = 1.8f;    ///< diffusers scale with sqrt(size)
        constexpr float kPreLineMs    = 520.0f;  ///< pre-delay (250) + longest early tap
        constexpr float kAirHz        = 3800.0f; ///< air absorption corner

        /** -3 ln(10) / ln(2): converts T/RT60 into a power of two, so the
            per-line gain can use the fast exp2 rather than std::exp. */
        constexpr float kDecayExponent = -9.965784f;

        /**
            THE FIVE CHARACTERS.

            sizeMult, diffusion, diffusers, dampHz, lowCutHz, modMs, modRate,
            earlyLevel, earlySpread, lateLevel, decayMult, width, infinite
        */
        constexpr SpaceEngine::Config kCharacters[5] =
        {
            // ROOM - small, discrete, bright, short.  Two diffusers only: a
            // small room's early field is heard as separate reflections.
            { 0.50f, 0.62f, 2, 9000.0f, 120.0f, 0.030f, 1.30f, 1.00f, 0.75f, 0.68f, 0.55f, 0.85f, false },

            // CHAMBER - the reference character and the default.
            { 1.00f, 0.70f, 4, 6200.0f,  90.0f, 0.075f, 1.00f, 0.72f, 1.00f, 1.00f, 1.00f, 1.00f, false },

            // DARK - the damping does the work, inside the loop, so it gets
            // darker as it decays instead of starting dull.
            { 1.12f, 0.74f, 4, 2400.0f,  70.0f, 0.045f, 0.70f, 0.50f, 1.00f, 1.00f, 0.95f, 0.95f, false },

            // DISTANT - big, wide early spread, deep slow modulation.
            { 1.35f, 0.78f, 4, 4000.0f, 105.0f, 0.130f, 0.80f, 0.88f, 1.30f, 1.00f, 1.25f, 1.15f, false },

            // INFINITE - holds.  Input is trimmed in process() so the network
            // fills over a few seconds rather than flooding in one note.
            { 1.20f, 0.76f, 4, 7000.0f,  60.0f, 0.170f, 0.55f, 0.32f, 1.20f, 1.00f, 1.00f, 1.10f, true  }
        };

        /** The table above is indexed by the raw choice value, so its order is
            part of the parameter contract rather than a detail of this file. */
        static_assert ((int) SpaceEngine::Character::room == 0
                           && (int) SpaceEngine::Character::infinite == 4,
                       "kCharacters must stay in the order of the space_char choice list");

        /** In-place 8-point Walsh-Hadamard, scaled to be orthonormal.

            24 adds and 8 multiplies for a full 8x8 mix: every line reaches
            every other line on every circulation, which is what makes the tail
            dense.  Orthonormality is the load-bearing property - it means the
            matrix cannot change the loop's energy, so the decay gain is the
            only thing that does. */
        forcedinline void hadamard8 (float* v) noexcept
        {
            for (int stride = 1; stride < 8; stride <<= 1)
                for (int i = 0; i < 8; i += stride * 2)
                    for (int j = i; j < i + stride; ++j)
                    {
                        const float a = v[j];
                        const float b = v[j + stride];
                        v[j]          = a + b;
                        v[j + stride] = a - b;
                    }

            for (int i = 0; i < 8; ++i)
                v[i] *= 0.35355339f;              // 1 / sqrt(8)
        }

        /** Soft ceiling inside the feedback loop.

            Exactly transparent below 1.5 - about +3.5 dBFS on one line, which
            normal material never reaches - and asymptotic to 3.0 above it.
            It exists for INFINITE: a network at unity gain that is still being
            fed grows without bound, and the choice is between muting the input
            (which makes freeze a different effect) and bounding the loop.  It
            also means no combination of parameters can drive the network past
            +9.5 dBFS however hard it is hit. */
        forcedinline float softCeiling (float x) noexcept
        {
            constexpr float threshold = 1.5f;
            constexpr float headroom  = 1.5f;

            const float a = std::abs (x);

            if (a <= threshold)
                return x;

            const float y = threshold + headroom * fx::tanhFast ((a - threshold) / headroom);

            return x < 0.0f ? -y : y;
        }
    }

    // =======================================================================
    //  Construction
    // =======================================================================
    SpaceEngine::SpaceEngine() = default;
    SpaceEngine::~SpaceEngine() = default;

    void SpaceEngine::prepare (const EngineSpec& spec)
    {
        sampleRate  = juce::jmax (8000.0, spec.sampleRate);
        msToSamples = (float) (sampleRate / 1000.0);

        // Dimension every buffer for the highest rate the instrument supports
        // rather than for this one.  A delay line that was sized at 44.1 kHz
        // and re-prepared at 96 kHz would be re-prepared anyway, but sizing
        // for the maximum means the allocation is the same in every session
        // and nothing downstream depends on the order prepare() is called in.
        const double designRate = juce::jmax (sampleRate, 96000.0);
        const float  designMs   = (float) (designRate / 1000.0);

        for (int k = 0; k < kLines; ++k)
        {
            lineBase[k] = kLineMs[k] * msToSamples;

            // 2 % over the maximum scale so the Breath drift and the
            // modulator can never push a read against the clamp.
            lines[k].prepare ((int) (kLineMs[k] * kMaxScale * 1.02f * designMs) + 64);

            // Spread the modulator phases so the eight lines do not all move
            // in the same direction at once, which would be a size wobble
            // rather than a diffusion of the modes.
            modPhase[k] = (float) k * 0.137f;
            modInc[k]   = 0.0f;
        }

        float energy = 0.0f;

        for (int c = 0; c < 2; ++c)
        {
            preLine[c].prepare ((int) (kPreLineMs * designMs) + 64);

            for (int k = 0; k < kDiffusers; ++k)
            {
                diffuserBase[c][k] = kDiffuserMs[c][k] * msToSamples;
                diffuser[c][k].prepare ((int) (kDiffuserMs[c][k] * kMaxDiffScale * designMs) + 64);
                diffuser[c][k].setDelay (diffuserBase[c][k]);
                diffuser[c][k].setCoefficient (0.7f);
            }

            for (int t = 0; t < kTaps; ++t)
            {
                tapBase[c][t] = kEarlyMs[c][t] * msToSamples;
                energy += kEarlyGain[c][t] * kEarlyGain[c][t];
            }

            airLp[c].setCutoff (kAirHz, sampleRate);
            feedCut[c].setCutoff (72.0f, sampleRate);

            // 150 Hz and 2.6 kHz: the low band is everything that must stay
            // correlated, the high band is where extra width is free.
            wetSplit[c].prepare (150.0f, 2600.0f, sampleRate);
            tilt[c].prepare (sampleRate, 700.0f);
        }

        // Normalise the early field so its level does not depend on how many
        // taps the pattern happens to have.  Energy summing (rather than
        // amplitude summing) is right because the taps are at unrelated times
        // and therefore uncorrelated.
        earlyNorm = 1.0f / juce::jmax (1.0e-3f, std::sqrt (energy * 0.5f));

        prepared = true;

        reset();
    }

    void SpaceEngine::flushNetwork() noexcept
    {
        for (int k = 0; k < kLines; ++k)
        {
            lines[k].reset();
            damping[k].reset();
            loopCut[k].reset();
        }

        for (int c = 0; c < 2; ++c)
        {
            preLine[c].reset();
            airLp[c].reset();
            feedCut[c].reset();
            wetSplit[c].reset();
            tilt[c].reset();

            for (int k = 0; k < kDiffusers; ++k)
                diffuser[c][k].reset();
        }
    }

    void SpaceEngine::reset()
    {
        flushNetwork();

        for (int k = 0; k < kLines; ++k)
            modPhase[k] = (float) k * 0.137f;

        // Leaving this false makes the next block snap its smoothers to the
        // parameters rather than gliding up from whatever was there before.
        running = false;
    }

    // =======================================================================
    //  Process
    // =======================================================================
    void SpaceEngine::process (juce::AudioBuffer<float>& buffer,
                               const ParameterRegistry& registry,
                               const MacroState& macros)
    {
        const int numSamples = juce::jmin (macros.numSamples, buffer.getNumSamples());

        if (! prepared || numSamples <= 0 || buffer.getNumChannels() < 1)
            return;

        const float mixParam = registry.raw (PID::spaceMix);

        // -- exact bypass ---------------------------------------------------
        //  The parameter is the gate, not the parameter plus the macro.  A
        //  patch that asks for no reverb gets none however far World is turned
        //  up: World may colour an effect, it may not summon one.  Returning
        //  here leaves the buffer untouched, sample for sample.
        if (! registry.flag (PID::spaceOn) || mixParam <= 1.0e-5f)
        {
            if (running)
            {
                // Drop the tail on the way out so that switching back on does
                // not replay audio from minutes ago.  One memset, only on the
                // transition, never per block.
                flushNetwork();
                running = false;
            }

            return;
        }

        const bool restart = ! running;
        running = true;

        // -- the registry, read exactly once --------------------------------
        const Config& cfg = kCharacters[juce::jlimit (0, 4, registry.choice (PID::spaceCharacter))];

        const float sizeP  = juce::jlimit (0.0f, 1.0f, registry.raw (PID::spaceSize)     + 0.30f * macros.scale);
        const float distP  = juce::jlimit (0.0f, 1.0f, registry.raw (PID::spaceDistance) + 0.35f * macros.distance);
        const float fogP   = juce::jlimit (0.0f, 1.0f, registry.raw (PID::spaceFog));
        const float lightP = juce::jlimit (-1.0f, 1.0f, registry.raw (PID::spaceLight));
        const float decayP = juce::jmax (0.05f, registry.raw (PID::spaceDecay));
        const float preP   = juce::jlimit (0.0f, 250.0f, registry.raw (PID::spacePreDelay));
        const float mixP   = juce::jlimit (0.0f, 1.0f, mixParam + 0.25f * macros.wetBias);

        // -- block-rate smoothing coefficients ------------------------------
        const float nf = (float) numSamples;
        const float aFast  = 1.0f - std::exp (-nf / (float) (0.020 * sampleRate));
        const float aSlow  = 1.0f - std::exp (-nf / (float) (0.120 * sampleRate));
        const float aCreep = 1.0f - std::exp (-nf / (float) (0.300 * sampleRate));

        // -- geometry -------------------------------------------------------
        //  0.32 * 8.125^size, so size 0.55 (the default) is scale 1.0.
        const float scaleTarget = juce::jlimit (0.28f, kMaxScale,
                                                cfg.sizeMult * 0.32f * fx::exp2Fast (sizeP * 3.0225f));

        // The diffusers model scattering at the surfaces rather than the
        // room's dimensions, so they follow size only as its square root.
        const float diffTarget = juce::jlimit (0.5f, kMaxDiffScale, std::sqrt (scaleTarget));

        // The early field follows size at 70 %: a large hall's first
        // reflections are later, but not in proportion, because the source and
        // the listener are not both in the middle of it.
        const float earlyScaleTarget = juce::jlimit (0.40f, 2.40f, 1.0f + (scaleTarget - 1.0f) * 0.70f)
                                           * cfg.earlySpread;

        // DISTANCE cue 3: a distant source's reflections arrive close behind
        // it, so the pre-delay shortens.  The 1 ms floor keeps the read off
        // the write head.
        const float preTarget = (1.0f + preP * (1.0f - 0.75f * distP)) * msToSamples;

        // -- decay ----------------------------------------------------------
        const float rt60 = juce::jlimit (0.08f, 28.0f, decayP * cfg.decayMult);
        const float maxGain = cfg.infinite ? 0.99997f : 0.99920f;

        for (int k = 0; k < kLines; ++k)
        {
            const float seconds = (lineBase[k] * scaleTarget) / (float) sampleRate;

            const float g = cfg.infinite
                                ? maxGain
                                : juce::jmin (maxGain, fx::exp2Fast (kDecayExponent * seconds / rt60));

            sGain[k].set (g, aFast, numSamples, restart);
        }

        // -- damping, inside the loop ---------------------------------------
        //  FOG closes it (a foggier room absorbs more), DISTANCE closes it
        //  (cue 5), LIGHT opens or closes it by up to an octave - so LIGHT
        //  changes how the tail evolves, not just how it is equalised.
        float dampHz = cfg.dampHz * (1.0f - 0.45f * fogP) * (1.0f - 0.35f * distP)
                           * fx::exp2Fast (lightP);
        dampHz = juce::jlimit (350.0f, (float) (sampleRate * 0.45), dampHz);

        for (int k = 0; k < kLines; ++k)
        {
            damping[k].setCutoff (dampHz * kDampSpread[k], sampleRate);
            loopCut[k].setCutoff (cfg.lowCutHz, sampleRate);
        }

        // The rest of LIGHT is a level-preserving tilt on the wet output.  Two
        // thirds of the control is in the loop above and one third here, so
        // the extremes are usable rather than merely dark or merely hissy.
        tiltAmount = lightP * 0.6f;

        // -- diffusion ------------------------------------------------------
        const float apCoeff = juce::jlimit (0.25f, 0.84f, cfg.diffusion - 0.18f + 0.36f * fogP);

        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kDiffusers; ++k)
                diffuser[c][k].setCoefficient (apCoeff);

        // A character with fewer diffusers leaves the unused ones holding old
        // audio.  Clear them on the change, or switching back to a character
        // that uses them would replay whatever was last in their buffers.
        const int wantDiffusers = juce::jlimit (1, kDiffusers, cfg.diffusers);

        if (wantDiffusers != activeDiffusers)
        {
            for (int c = 0; c < 2; ++c)
                for (int k = juce::jmin (wantDiffusers, activeDiffusers); k < kDiffusers; ++k)
                    diffuser[c][k].reset();

            activeDiffusers = wantDiffusers;
        }

        // -- modulation -----------------------------------------------------
        //  0.03 to 0.17 ms at 0.07 to 0.31 Hz.  At the deepest setting that is
        //  a peak pitch deviation of about 1 cent, which breaks the network's
        //  fixed modes up without the tail audibly chorusing.  Motion adds to
        //  it; nothing else does.
        modDepth = (cfg.modMs + 0.10f * macros.movement) * msToSamples;

        for (int k = 0; k < kLines; ++k)
            modInc[k] = kModHz[k] * cfg.modRate / (float) sampleRate;

        // -- levels ---------------------------------------------------------
        const float earlyTarget = cfg.earlyLevel * (0.42f + 0.58f * distP);   // DISTANCE cue 1
        const float lateTarget  = cfg.lateLevel  * (0.62f + 0.38f * distP);

        const float mixEff = juce::jlimit (0.0f, 1.0f, mixP + 0.14f * distP * (1.0f - mixP)); // cue 6

        float dryGain = 1.0f, wetGain = 0.0f;
        fx::dryWetGains (mixEff, dryGain, wetGain);

        // How much the wet is actually present, used to scale the two cues
        // that touch the direct path so that they vanish as MIX does.
        const float presence = juce::jmin (1.0f, wetGain * 2.0f);

        const float airTarget    = 0.85f * distP * presence;                  // cue 2
        const float directTarget = 1.0f - 0.25f * distP * presence;           // cue 6

        const float widthTarget = juce::jlimit (0.0f, 2.0f,
                                                cfg.width * macros.widthScale
                                                    * (1.0f - 0.28f * distP));                // cue 4

        injection = cfg.infinite ? 0.22f : 0.45f;
        duckDepth = juce::jlimit (0.0f, 1.0f, macros.pulseToSpace) * 0.85f;

        sScale     .set (scaleTarget,      aSlow,  numSamples, restart);
        sDiffScale .set (diffTarget,       aSlow,  numSamples, restart);
        sEarlyScale.set (earlyScaleTarget, aSlow,  numSamples, restart);
        sPre       .set (preTarget,        aCreep, numSamples, restart);
        sEarly     .set (earlyTarget,      aFast,  numSamples, restart);
        sLate      .set (lateTarget,       aFast,  numSamples, restart);
        sDry       .set (dryGain,          aFast,  numSamples, restart);
        sWet       .set (wetGain,          aFast,  numSamples, restart);
        sDirect    .set (directTarget,     aFast,  numSamples, restart);
        sAir       .set (airTarget,        aFast,  numSamples, restart);
        sWidth     .set (widthTarget,      aFast,  numSamples, restart);
        sWidthHigh .set (widthTarget * 1.12f, aFast, numSamples, restart);

        // -- the loop -------------------------------------------------------
        float* chL = buffer.getWritePointer (0);
        float* chR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
        const bool stereo = chR != nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = fx::guard (chL[i]);
            const float inR = stereo ? fx::guard (chR[i]) : inL;

            const float duck   = 1.0f - duckDepth * macros.pulseAt (i);
            const float drift  = 1.0f + macros.breathAt (i) * 0.004f;
            const float scale  = sScale.at (i) * drift;
            const float dScale = sDiffScale.at (i);
            const float eScale = sEarlyScale.at (i);
            const float air    = sAir.at (i);

            // ---- direct path: air absorption (DISTANCE cue 2) -------------
            const float dirL = inL + (airLp[0].lowpass (inL) - inL) * air;
            const float dirR = inR + (airLp[1].lowpass (inR) - inR) * air;

            // ---- the feed --------------------------------------------------
            //  High-passed before it reaches anything that remembers.  This is
            //  the first half of the low-end rule: the bass is not in the tail
            //  at all, so it cannot be smeared, widened or held by it.
            preLine[0].write (feedCut[0].highpass (inL));
            preLine[1].write (feedCut[1].highpass (inR));

            const float preS = sPre.at (i);

            // ---- early reflections ----------------------------------------
            float earlyL = 0.0f;
            float earlyR = 0.0f;

            for (int t = 0; t < kTaps; ++t)
            {
                earlyL += kEarlyGain[0][t] * preLine[0].readLinear (preS + tapBase[0][t] * eScale);
                earlyR += kEarlyGain[1][t] * preLine[1].readLinear (preS + tapBase[1][t] * eScale);
            }

            earlyL *= earlyNorm;
            earlyR *= earlyNorm;

            // ---- input diffusion -------------------------------------------
            float diffL = preLine[0].readLinear (preS);
            float diffR = preLine[1].readLinear (preS);

            for (int k = 0; k < activeDiffusers; ++k)
            {
                diffuser[0][k].setDelay (diffuserBase[0][k] * dScale);
                diffuser[1][k].setDelay (diffuserBase[1][k] * dScale);

                diffL = diffuser[0][k].process (diffL);
                diffR = diffuser[1][k].process (diffR);
            }

            // ---- the network ------------------------------------------------
            float v[kLines];

            for (int k = 0; k < kLines; ++k)
            {
                modPhase[k] += modInc[k];

                if (modPhase[k] >= 1.0f)
                    modPhase[k] -= 1.0f;

                const float readPos = lineBase[k] * scale
                                        + fx::sineTurns (modPhase[k]) * modDepth;

                float s = lines[k].readLinear (readPos);

                // Damping and the subsonic cut are here, before the matrix,
                // which is what puts them inside the feedback loop.
                s = damping[k].lowpass (s);
                s = loopCut[k].highpass (s);

                v[k] = s;
            }

            // Output taps are read before the decay gain is applied, and the
            // two channels take disjoint sets of lines.  Taking the same lines
            // with opposite signs would give a tail that is anti-correlated
            // rather than decorrelated, and anti-correlated is what disappears
            // when somebody sums to mono.
            const float lateL = (v[0] + v[2] + v[4] + v[6]) * 0.5f;
            const float lateR = (v[1] + v[3] + v[5] + v[7]) * 0.5f;

            for (int k = 0; k < kLines; ++k)
                v[k] = softCeiling (v[k] * sGain[k].at (i));

            hadamard8 (v);

            const float injL = diffL * injection;
            const float injR = diffR * injection;

            lines[0].write (v[0] + injL);
            lines[1].write (v[1] + injR);
            lines[2].write (v[2] + injL);
            lines[3].write (v[3] + injR);
            lines[4].write (v[4] + injL);
            lines[5].write (v[5] + injR);
            lines[6].write (v[6] + injL);
            lines[7].write (v[7] + injR);

            // ---- wet assembly ------------------------------------------------
            float wetL = earlyL * sEarly.at (i) + lateL * sLate.at (i);
            float wetR = earlyR * sEarly.at (i) + lateR * sLate.at (i);

            wetL = tilt[0].process (wetL, tiltAmount);
            wetR = tilt[1].process (wetR, tiltAmount);

            // ---- the low end stays put ----------------------------------------
            //  Second half of the rule: whatever low frequency did survive the
            //  input high-pass leaves this engine perfectly correlated, so a
            //  reverberated bass note cannot cancel when the mix is summed.
            float lowL, midL, highL, lowR, midR, highR;
            wetSplit[0].split (wetL, lowL, midL, highL);
            wetSplit[1].split (wetR, lowR, midR, highR);

            const float lowMono = 0.5f * (lowL + lowR);

            const float midM = 0.5f * (midL + midR);
            const float midS = 0.5f * (midL - midR) * sWidth.at (i);
            const float hiM  = 0.5f * (highL + highR);
            const float hiS  = 0.5f * (highL - highR) * sWidthHigh.at (i);

            wetL = lowMono + (midM + midS) + (hiM + hiS);
            wetR = lowMono + (midM - midS) + (hiM - hiS);

            // A network that has blown up zeroes itself rather than poisoning
            // the session.  The input was guarded above, so it cannot
            // immediately blow up again.
            if (! fx::sane (wetL + wetR))
            {
                flushNetwork();
                wetL = 0.0f;
                wetR = 0.0f;
            }

            const float wg = sWet.at (i) * duck;
            const float dg = sDry.at (i) * sDirect.at (i);

            chL[i] = fx::guard (dirL * dg + wetL * wg);

            if (stereo)
                chR[i] = fx::guard (dirR * dg + wetR * wg);
        }
    }
}
