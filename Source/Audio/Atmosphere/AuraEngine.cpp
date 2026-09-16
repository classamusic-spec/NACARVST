#include "AuraEngine.h"

#include <cmath>

/*
    =======================================================================
    AURA  -  specification section 92
    =======================================================================

    WHAT MAKES IT DIFFERENT FROM SPACE

    Space is the room the sound is in.  Aura is the atmosphere the room is in.
    The table in AuraEngine.h lists where that lands in the code; the reasoning
    is this.

    A reverb is a model of reflection.  It has an early field, because the
    first few bounces off real surfaces are what tell you the shape of a place,
    and it has a mix, because it is an effect you decide how much of.  Space is
    that, and it is positioned in the chain because reverberating before or
    after a distortion are different decisions a producer is entitled to make.

    Aura is a model of the air, not the surfaces.  It sits after everything,
    including after Space, so what it is reverberating is the finished object
    rather than the raw source.  It has no early reflections at all - there is
    no near surface in an atmosphere - and its diffusion is long and moving
    rather than short and fixed.  Its network runs at three to four times
    Space's delay lengths with six to twelve times the modulation depth, which
    is the difference between a tail and weather.  And its tail is read back
    twice: directly, and through a cloud of overlapping grains that FOG fades
    in.  That granular layer is the specification's own hint, and it is what
    makes Aura able to smear rather than merely reverberate.

    Aura has no mix parameter.  That is not an omission in the parameter list,
    it is what tells you the engine is meant to be always present: its amount
    is DISTANCE, and its switch is aura_on.

    TOPOLOGY

        in -> air absorption ------------------------------------> direct
          \
           -> low cut 130 Hz -> pre-delay (shortens with DISTANCE)
                                     |
                                     v
                          3 long modulated allpasses per channel
                                     |
                                     v
                          4-line FDN, 97-229 ms, Hadamard mix,
                          damping and a subsonic cut in the loop
                                     |
                      +--------------+---------------+
                      |                              |
                      v                              v
                 tail, direct                  tail history -> 4 grains
                      |                              |
                      +---- FOG crossfades ----------+
                                     |
                              tilt -> three-band width stage
                                     |
        out = direct * dryGain + wet * wetGain  <----+

    DELAY LENGTHS, AND WHY

        97.15   131.23   174.52   229.23  ms

    The primes 4663, 6299, 8377 and 11003 at 48 kHz, for the same reason
    Space's eight are prime: mutually incommensurate lengths do not share
    modes, and a shared mode is a ring.  They are four times longer than
    Space's because an atmosphere is not a room, and there are four of them
    rather than eight because the density that a reverb gets from having many
    lines, Aura gets from the modulated allpasses in front of the network and
    the grains behind it.  Four long lines also cost about a third of what
    eight short ones do, which matters for an engine that is always on.

    SIZE scales them by 0.45 to 1.9, so the longest line runs from 103 ms to
    436 ms.  Everything is derived from the actual sample rate.

    DAMPING IS IN THE FEEDBACK PATH, for the same reason as in Space: it is
    what makes the atmosphere darken as it fades instead of being dull from
    the start.  FOG closes it, LIGHT opens or closes it by up to 1.2 octaves,
    DISTANCE closes it.

    HOW DISTANCE CREATES DEPTH

    DISTANCE is Aura's amount control, which is exactly the trap section 92
    warns about, so the amount is the *last* thing it does.  In order:

      1. The direct sound loses high frequencies - a first-order 3.6 kHz low
         pass blended in up to 80 %.
      2. The internal pre-delay SHORTENS, 35 ms down to 6 ms, because a
         distant source's environment arrives close behind it.
      3. The smear deepens: the allpass coefficients rise, so the further away
         it is, the more scattered it has been by the time it arrives.
      4. The image narrows by up to 30 %.
      5. The tail darkens by up to 30 %.
      6. Only then the wet level, on a curve that starts slowly
         (0.85 * a * (0.45 + 0.55a)), so the first third of the control is
         almost entirely cues 1 to 5 and almost no level.

    Cues 1 and the direct trim are scaled by how present the wet is, so they
    reach zero exactly as the wet does.

    THE GRANULAR TAIL

    The network's output is written to a 1.2 s history and read back by four
    overlapping Hann grains of 70-310 ms, each starting 40-640 ms behind the
    write head with a playback rate within +-0.15 % of unity.  Because the
    rate is within a thousandth of unity the gap between a grain's read
    pointer and the write head cannot close during the grain's life, so a
    grain can never overtake the writer and never reads more than 640 ms of
    history - the buffer is 1.2 s, so neither bound is ever near.

    The grains are feed-forward only.  Feeding them back into the network
    would make a much more interesting engine and its loop gain would not be
    boundable by anything I can write down, so it is not done.

    STABILITY BOUNDS

      - Orthonormal Hadamard mixing, so all loop gain is in the explicit
        per-line gain, which is exp(-3 ln10 T/RT60) clamped to 0.9992.
      - RT60 runs 1.2 to 14 s, inside the 32 s tail promise with room to
        spare.  Aura has no INFINITE and never approaches unity.
      - A soft ceiling in the loop, transparent below 1.5, asymptotic to 3.0.
      - Damping and subsonic cut are one-poles with gain <= 1 everywhere.
      - Allpass coefficients are clamped to 0.86 (fx::Allpass clamps at 0.95
        in any case), so the diffusers cannot self-oscillate however deeply
        they are modulated.
      - Input is guarded on the way in; the wet sum is checked for finiteness
        every sample and the network zeroes itself if it has blown up.
      - Every delay read is clamped inside fx::DelayLine, and every line is
        dimensioned in prepare() for the maximum scale at the highest
        supported sample rate.

    MACRO RESPONSE

      macros.scale     +0.30 on SIZE.
      macros.distance  +0.30 on DISTANCE, which means World reaches the whole
                       cue set above rather than a wet control - section 73's
                       "do not map World only to wet level".
      macros.wetBias   +0.20 on the amount.  An offset on an atmospheric mix,
                       which is exactly what the field is for.
      macros.movement  +0.60 ms on the tail modulation and +0.5 ms on the
                       smear modulation.  Motion is what makes weather move.
      macros.widthScale multiplies the width of the wet.
      macros.pulseToSpace * pulseAt(i) ducks the wet by up to 85 %.
      macros.breathAt(i) drifts the delay lengths by +-0.6 % and the grain
                       cloud's offsets with it.  Slow parameters only.

      macros.age, grit and alterAmount are unused: Aura is not an ageing
      engine and has no alternate identity.

      The parameter is the gate.  If aura_distance is zero the engine returns
      the buffer untouched however far World is turned up - World may colour
      an effect, it may not summon one.

    KNOWN LIMITATIONS

      - Nobody has listened to this.
      - Four grains is a thin cloud.  With the longest grains it is dense
        enough; with the shortest ones at 70 ms the overlap drops to about 2x
        and the granular layer becomes audible as individual events rather
        than as texture.  Eight grains would fix it and costs eight more
        interpolated reads per sample.
      - The grains do not transpose.  A shimmer layer is one line of code from
        here and is deliberately not taken, because an octave-up grain cloud
        is a specific and very recognisable effect rather than an atmosphere.
      - Four lines is sparse at the largest sizes.  The allpasses and the
        grains cover it, but a 436 ms tail from four lines is a wash, not a
        hall - which is the intent, but it does mean SIZE at maximum is not
        "a bigger room", it is "a vaguer one".
      - Changing SIZE sweeps the delay reads rather than crossfading them.
      - The wet level has not been calibrated against anything.
      - The quality parameter is ignored; CPU has not been measured.
      - INTEGRATION NOTE.  NacarEngine gates this engine on aura_on and does
        not call it at all while the module is switched off, so the engine
        never sees the block in which it was turned off and cannot flush its
        tail there.  Turning Aura off and on again therefore resumes the tail
        that was in the delay lines rather than starting clean.  The in-engine
        flush covers the other route (DISTANCE reaching zero) because that one does
        reach process().  The fix belongs in the chain - reset() the module
        when its enable goes false, or drop the gate and let the engine's own
        exact early-out do the work - and is one line either way.
*/

namespace nacar
{
    namespace
    {
        /** Primes 4663, 6299, 8377, 11003 at 48 kHz, as milliseconds. */
        constexpr float kLineMs[4] = { 97.1458f, 131.2292f, 174.5208f, 229.2292f };

        constexpr float kDampSpread[4] = { 1.00f, 0.90f, 1.10f, 0.95f };

        /** Tail modulation rates, Hz.  Slower than Space's, and incommensurate
            with each other so the four lines never line up. */
        constexpr float kModHz[4] = { 0.031f, 0.047f, 0.073f, 0.113f };

        /** Long input diffusers - the opposite decision from Space, where they
            are short and static.  These are what turn an impulse into a smear
            before the network has seen it. */
        constexpr float kSmearMs[2][3] =
        { { 23.3f, 37.9f, 58.1f },
          { 29.7f, 43.7f, 67.3f } };

        constexpr float kSmearHz[2][3] =
        { { 0.043f, 0.067f, 0.091f },
          { 0.053f, 0.079f, 0.107f } };

        constexpr float kMaxScale      = 1.90f;
        constexpr float kMaxSmearScale = 1.45f;
        constexpr float kPreLineMs     = 60.0f;    ///< internal pre-delay only
        constexpr float kHistorySec    = 1.20f;
        constexpr float kAirHz         = 3600.0f;
        constexpr float kDecayExponent = -9.965784f;   // -3 ln10 / ln2

        /** In-place orthonormal 4-point Hadamard. */
        forcedinline void hadamard4 (float* v) noexcept
        {
            const float a = v[0] + v[1];
            const float b = v[0] - v[1];
            const float c = v[2] + v[3];
            const float d = v[2] - v[3];

            v[0] = (a + c) * 0.5f;
            v[1] = (b + d) * 0.5f;
            v[2] = (a - c) * 0.5f;
            v[3] = (b - d) * 0.5f;
        }

        /** Transparent below 1.5, asymptotic to 3.0.  Insurance only: Aura's
            loop gain never approaches unity, so this should never engage. */
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
    AuraEngine::AuraEngine() = default;
    AuraEngine::~AuraEngine() = default;

    void AuraEngine::prepare (const EngineSpec& spec)
    {
        sampleRate  = juce::jmax (8000.0, spec.sampleRate);
        msToSamples = (float) (sampleRate / 1000.0);

        const double designRate = juce::jmax (sampleRate, 96000.0);
        const float  designMs   = (float) (designRate / 1000.0);

        for (int k = 0; k < kLines; ++k)
        {
            lineBase[k] = kLineMs[k] * msToSamples;
            lines[k].prepare ((int) (kLineMs[k] * kMaxScale * 1.02f * designMs) + 64);

            modPhase[k] = (float) k * 0.211f;
            modInc[k]   = 0.0f;
        }

        for (int c = 0; c < 2; ++c)
        {
            preLine[c].prepare ((int) (kPreLineMs * designMs) + 64);

            for (int k = 0; k < kSmear; ++k)
            {
                smearBase[c][k] = kSmearMs[c][k] * msToSamples;
                smear[c][k].prepare ((int) (kSmearMs[c][k] * kMaxSmearScale * designMs) + 256);
                smear[c][k].setDelay (smearBase[c][k]);
                smear[c][k].setCoefficient (0.6f);

                smearPhase[c][k] = (float) (c * kSmear + k) * 0.17f;
                smearInc[c][k]   = 0.0f;
            }

            airLp[c].setCutoff (kAirHz, sampleRate);
            feedCut[c].setCutoff (130.0f, sampleRate);

            wetSplit[c].prepare (160.0f, 2800.0f, sampleRate);
            tilt[c].prepare (sampleRate, 700.0f);
        }

        tailHistory.prepare (designRate, kHistorySec);
        historySize = (float) tailHistory.capacity();

        // Seeded from a constant, never from a clock: the same session must
        // render identically on every machine and in every offline render.
        rng.setSeed (0x4155524Au);

        prepared = true;

        reset();
    }

    void AuraEngine::flushNetwork() noexcept
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

            for (int k = 0; k < kSmear; ++k)
                smear[c][k].reset();
        }

        tailHistory.reset();
    }

    void AuraEngine::reset()
    {
        flushNetwork();

        for (int k = 0; k < kLines; ++k)
            modPhase[k] = (float) k * 0.211f;

        // Stagger the grains evenly so the cloud is continuous from the first
        // sample rather than starting with all four windows opening at once.
        for (int g = 0; g < kGrains; ++g)
        {
            restartGrain (grains[g]);
            grains[g].age = grains[g].length * (float) g * (1.0f / (float) kGrains);
        }

        running = false;
    }

    void AuraEngine::restartGrain (Grain& grain) noexcept
    {
        grain.length    = juce::jmax (64.0f, grainLength * (0.7f + 0.6f * rng.next01()));
        grain.invLength = 1.0f / grain.length;
        grain.age       = 0.0f;

        // +-0.15 %: enough that the four grains drift out of phase with each
        // other and with the writer, far too little to be heard as pitch.
        grain.increment = 1.0f + rng.nextBipolar() * 0.0015f;

        const float offset = grainOffsetMin
                                + rng.next01() * juce::jmax (1.0f, grainOffsetMax - grainOffsetMin);

        float position = (float) tailHistory.getWriteIndex() - offset;

        if (position < 0.0f)
            position += historySize;

        // jmax, not a bare subtraction: reset() may run before prepare() has
        // sized the history, and a limit whose minimum exceeds its maximum is
        // an assertion rather than a clamp.
        grain.position = juce::jlimit (0.0f, juce::jmax (1.0f, historySize - 2.0f), position);

        fx::panGains (rng.nextBipolar() * 0.8f, grain.gainL, grain.gainR);
    }

    // =======================================================================
    //  Process
    // =======================================================================
    void AuraEngine::process (juce::AudioBuffer<float>& buffer,
                              const ParameterRegistry& registry,
                              const MacroState& macros)
    {
        const int numSamples = juce::jmin (macros.numSamples, buffer.getNumSamples());

        if (! prepared || numSamples <= 0 || buffer.getNumChannels() < 1)
            return;

        const float distParam = registry.raw (PID::auraDistance);

        // -- exact bypass -----------------------------------------------------
        if (! registry.flag (PID::auraOn) || distParam <= 1.0e-5f)
        {
            if (running)
            {
                flushNetwork();
                running = false;
            }

            return;
        }

        const bool restart = ! running;
        running = true;

        // -- the registry, read exactly once ----------------------------------
        const float sizeP  = juce::jlimit (0.0f, 1.0f, registry.raw (PID::auraSize) + 0.30f * macros.scale);
        const float fogP   = juce::jlimit (0.0f, 1.0f, registry.raw (PID::auraFog));
        const float decayP = juce::jlimit (0.0f, 1.0f, registry.raw (PID::auraDecay));
        const float lightP = juce::jlimit (-1.0f, 1.0f, registry.raw (PID::auraLight));

        const float amount = juce::jlimit (0.0f, 1.0f, distParam
                                                           + 0.30f * macros.distance
                                                           + 0.20f * macros.wetBias);

        const float nf = (float) numSamples;
        const float aFast  = 1.0f - std::exp (-nf / (float) (0.020 * sampleRate));
        const float aSlow  = 1.0f - std::exp (-nf / (float) (0.150 * sampleRate));
        const float aCreep = 1.0f - std::exp (-nf / (float) (0.350 * sampleRate));

        // -- geometry -----------------------------------------------------------
        const float scaleTarget = juce::jlimit (0.45f, kMaxScale, 0.45f * fx::exp2Fast (sizeP * 2.078f));
        const float smearTarget = juce::jlimit (0.60f, kMaxSmearScale, std::sqrt (scaleTarget));

        // DISTANCE cue 2: the environment arrives closer behind a distant source.
        const float preTarget = (35.0f - 29.0f * amount) * msToSamples;

        // -- decay ---------------------------------------------------------------
        //  1.2 s to 14 s.  Long, because this is an atmosphere, and well inside
        //  the 32 s tail the processor promises the host.
        const float rt60 = 1.2f * fx::exp2Fast (decayP * 3.544f);

        for (int k = 0; k < kLines; ++k)
        {
            const float seconds = (lineBase[k] * scaleTarget) / (float) sampleRate;
            const float g = juce::jmin (0.99920f, fx::exp2Fast (kDecayExponent * seconds / rt60));

            sGain[k].set (g, aFast, numSamples, restart);
        }

        // -- damping, inside the loop ---------------------------------------------
        float dampHz = 5200.0f * fx::exp2Fast (lightP * 1.2f)
                          * (1.0f - 0.50f * fogP)
                          * (1.0f - 0.30f * amount);          // DISTANCE cue 5
        dampHz = juce::jlimit (300.0f, (float) (sampleRate * 0.45), dampHz);

        for (int k = 0; k < kLines; ++k)
        {
            damping[k].setCutoff (dampHz * kDampSpread[k], sampleRate);
            loopCut[k].setCutoff (55.0f, sampleRate);
        }

        tiltAmount = lightP * 0.5f;

        // -- smear ------------------------------------------------------------------
        //  DISTANCE cue 3: the further away, the more scattered it arrives.
        const float smearCoeff = juce::jlimit (0.30f, 0.86f, 0.55f + 0.30f * fogP + 0.10f * amount);

        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kSmear; ++k)
            {
                smear[c][k].setCoefficient (smearCoeff);
                smearInc[c][k] = kSmearHz[c][k] / (float) sampleRate;
            }

        smearDepth = (0.35f + 1.20f * fogP + 0.50f * macros.movement) * msToSamples;
        modDepth   = (0.35f + 1.20f * fogP + 0.60f * macros.movement) * msToSamples;

        for (int k = 0; k < kLines; ++k)
            modInc[k] = kModHz[k] / (float) sampleRate;

        // -- the grain cloud -----------------------------------------------------------
        grainLength    = (70.0f + 180.0f * sizeP + 60.0f * fogP) * msToSamples;
        grainOffsetMin = 40.0f * msToSamples;
        grainOffsetMax = (40.0f + 120.0f + 480.0f * sizeP) * msToSamples;

        // FOG decides how much of what you hear is the grains rather than the
        // network - which is what "how much the atmosphere obscures" means here.
        const float grainTarget = 0.18f + 0.55f * fogP;
        const float lateTarget  = 1.0f - 0.45f * fogP;

        // -- levels -------------------------------------------------------------------
        //  Cue 6, and it is last on purpose: the curve is deliberately slow at
        //  the bottom so the first third of DISTANCE is almost entirely the
        //  five cues above and almost no wet level.
        const float mixEff = juce::jlimit (0.0f, 1.0f, 0.85f * amount * (0.45f + 0.55f * amount));

        float dryGain = 1.0f, wetGain = 0.0f;
        fx::dryWetGains (mixEff, dryGain, wetGain);

        const float presence     = juce::jmin (1.0f, wetGain * 2.0f);
        const float airTarget    = 0.80f * amount * presence;          // cue 1
        const float directTarget = 1.0f - 0.22f * amount * presence;

        const float widthTarget = juce::jlimit (0.0f, 2.0f,
                                                macros.widthScale * (1.05f - 0.30f * amount)); // cue 4

        duckDepth = juce::jlimit (0.0f, 1.0f, macros.pulseToSpace) * 0.85f;

        sScale     .set (scaleTarget,          aSlow,  numSamples, restart);
        sSmearScale.set (smearTarget,          aSlow,  numSamples, restart);
        sPre       .set (preTarget,            aCreep, numSamples, restart);
        sDry       .set (dryGain,              aFast,  numSamples, restart);
        sWet       .set (wetGain,              aFast,  numSamples, restart);
        sDirect    .set (directTarget,         aFast,  numSamples, restart);
        sAir       .set (airTarget,            aFast,  numSamples, restart);
        sLate      .set (lateTarget,           aFast,  numSamples, restart);
        sGrain     .set (grainTarget,          aFast,  numSamples, restart);
        sWidth     .set (widthTarget,          aFast,  numSamples, restart);
        sWidthHigh .set (widthTarget * 1.10f,  aFast,  numSamples, restart);

        // -- the loop ---------------------------------------------------------------------
        float* chL = buffer.getWritePointer (0);
        float* chR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
        const bool stereo = chR != nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = fx::guard (chL[i]);
            const float inR = stereo ? fx::guard (chR[i]) : inL;

            const float duck  = 1.0f - duckDepth * macros.pulseAt (i);
            const float drift = 1.0f + macros.breathAt (i) * 0.006f;
            const float scale = sScale.at (i) * drift;
            const float air   = sAir.at (i);

            // ---- direct path ------------------------------------------------
            const float dirL = inL + (airLp[0].lowpass (inL) - inL) * air;
            const float dirR = inR + (airLp[1].lowpass (inR) - inR) * air;

            // ---- feed and pre-delay --------------------------------------------
            //  130 Hz, higher than Space's 72 Hz: an atmosphere has no bass in
            //  it at all, and that is the cheapest possible way to obey the
            //  low-end rule in an engine that is always switched on.
            preLine[0].write (feedCut[0].highpass (inL));
            preLine[1].write (feedCut[1].highpass (inR));

            const float preS = sPre.at (i);

            float sL = preLine[0].readLinear (preS);
            float sR = preLine[1].readLinear (preS);

            // ---- long modulated diffusion ---------------------------------------
            const float smearScale = sSmearScale.at (i);

            for (int k = 0; k < kSmear; ++k)
            {
                smearPhase[0][k] += smearInc[0][k];
                smearPhase[1][k] += smearInc[1][k];

                if (smearPhase[0][k] >= 1.0f) smearPhase[0][k] -= 1.0f;
                if (smearPhase[1][k] >= 1.0f) smearPhase[1][k] -= 1.0f;

                smear[0][k].setDelay (smearBase[0][k] * smearScale);
                smear[1][k].setDelay (smearBase[1][k] * smearScale);

                sL = smear[0][k].process (sL, fx::sineTurns (smearPhase[0][k]) * smearDepth);
                sR = smear[1][k].process (sR, fx::sineTurns (smearPhase[1][k]) * smearDepth);
            }

            // ---- the network -------------------------------------------------------
            float v[kLines];

            for (int k = 0; k < kLines; ++k)
            {
                modPhase[k] += modInc[k];

                if (modPhase[k] >= 1.0f)
                    modPhase[k] -= 1.0f;

                const float readPos = lineBase[k] * scale
                                        + fx::sineTurns (modPhase[k]) * modDepth;

                float s = lines[k].readLinear (readPos);

                s = damping[k].lowpass (s);
                s = loopCut[k].highpass (s);

                v[k] = s;
            }

            const float tailL = (v[0] + v[2]) * 0.70710678f;
            const float tailR = (v[1] + v[3]) * 0.70710678f;

            for (int k = 0; k < kLines; ++k)
                v[k] = softCeiling (v[k] * sGain[k].at (i));

            hadamard4 (v);

            const float injL = sL * 0.5f;
            const float injR = sR * 0.5f;

            lines[0].write (v[0] + injL);
            lines[1].write (v[1] + injR);
            lines[2].write (v[2] + injL);
            lines[3].write (v[3] + injR);

            // ---- the granular tail ----------------------------------------------------
            tailHistory.write (tailL, tailR);

            float grainL = 0.0f;
            float grainR = 0.0f;

            for (int g = 0; g < kGrains; ++g)
            {
                Grain& grain = grains[g];

                // Hann.  Zero at both ends, so a grain fades in and out rather
                // than clicking, and four of them staggered sum to about 2.
                const float window = 0.5f - 0.5f * fx::cosineTurns (grain.age * grain.invLength);

                float gl = 0.0f, gr = 0.0f;
                tailHistory.readAt (grain.position, gl, gr);

                grainL += window * gl * grain.gainL;
                grainR += window * gr * grain.gainR;

                grain.position += grain.increment;

                if (grain.position >= historySize)
                    grain.position -= historySize;

                grain.age += 1.0f;

                if (grain.age >= grain.length)
                    restartGrain (grain);
            }

            grainL *= 0.5f;
            grainR *= 0.5f;

            // ---- wet assembly ------------------------------------------------------------
            const float late  = sLate.at (i);
            const float cloud = sGrain.at (i);

            float wetL = tailL * late + grainL * cloud;
            float wetR = tailR * late + grainR * cloud;

            wetL = tilt[0].process (wetL, tiltAmount);
            wetR = tilt[1].process (wetR, tiltAmount);

            // ---- the low end stays put -------------------------------------------------
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
