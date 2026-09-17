#include "SynthVoice.h"

namespace nacar::synth
{
    // -----------------------------------------------------------------------
    void SynthVoice::prepare (double sampleRate, int index) noexcept
    {
        sr = juce::jmax (1.0, sampleRate);
        voiceIndex = index;

        for (auto* o : { &oscA, &oscB, &oscC })
            for (auto& s : o->sub)
                s.prepare (sr);

        subOsc.prepare (sr);
        noise.prepare (sr, (juce::uint32) (index * 7919 + 13));

        driftA.prepare (sr, (juce::uint32) (index * 2411 + 101));
        driftB.prepare (sr, (juce::uint32) (index * 2411 + 307));
        driftFilter.prepare (sr, (juce::uint32) (index * 2411 + 977));

        // Everything inside the oversampled block is prepared at a multiple of
        // the sample rate; everything outside it at the real one.  A filter's
        // coefficients are tied to the rate it runs at, so the factor cannot be
        // changed under a sounding note - see retuneCore().  The voice always
        // starts at 2x, which is what ECO and STUDIO use, so an instrument that
        // is never switched to ULTRA is prepared exactly as it always was.
        osFactor = 2;

        for (int ch = 0; ch < 2; ++ch)
        {
            body[ch].prepare (sr);
            density[ch].prepare (sr);
            outputDc[ch].prepare (sr);
        }

        retuneCore (2);

        ampEnv.prepare (sr);
        modEnv1.prepare (sr);
        modEnv2.prepare (sr);

        panSmoothL.setTime (0.005f, sr);
        panSmoothR.setTime (0.005f, sr);

        // -------------------------------------------------------------------
        //  Per-voice variation.
        //
        //  Deterministic from the voice index, bounded, and applied to nine
        //  different things by small amounts rather than to one thing by a
        //  large amount.  That is the difference between organic and out of
        //  tune: no single deviation is big enough to hear on its own, but
        //  together they stop eight held notes from being eight copies of the
        //  same waveform.
        // -------------------------------------------------------------------
        Rng rng ((juce::uint32) (index * 6151 + 17));

        varTuning      = rng.nextBipolar();                    // scaled by Variation later
        varCutoff      = 1.0f + rng.nextBipolar() * 0.10f;
        varEnvTime     = 1.0f + rng.nextBipolar() * 0.07f;
        varPan         = rng.nextBipolar();
        varDrive       = 1.0f + rng.nextBipolar() * 0.12f;
        varWtPos       = rng.nextBipolar() * 0.05f;
        varPulse       = rng.nextBipolar() * 0.04f;
        varOscBalance  = 1.0f + rng.nextBipolar() * 0.08f;
        varFilterEnv   = 1.0f + rng.nextBipolar() * 0.12f;

        reset();
    }

    // -----------------------------------------------------------------------
    //  THE OVERSAMPLING FACTOR, AND WHY IT IS LATCHED AT NOTE-ON.
    //
    //  ULTRA runs the nonlinear core at four times the sample rate instead of
    //  twice.  Every filter inside that core derives its coefficients from the
    //  rate it was prepared at, so the factor cannot simply be swapped: the
    //  filters have to be re-prepared, and re-preparing zeroes their state.
    //
    //  Doing that under a sounding note is a step discontinuity in the middle
    //  of a resonant filter - a thump, not a click, and a long one at low
    //  cutoffs.  Three ways out were available:
    //
    //    * prepare both factors and cross-fade between them.  Correct, but it
    //      runs both cores at once for the length of the fade, on every
    //      sounding voice at the same instant.  This build is already at 137 %
    //      of one core at 32 voices with 8x unison, so the fade would buy a
    //      clean transition with a dropout.
    //    * fade the voice to silence, switch, fade back.  A hole in a held
    //      note, which is not obviously better than a thump.
    //    * change the factor only where the voice is already starting from
    //      silence, which is note-on.
    //
    //  The third is what this does, and it is the one that costs nothing:
    //  note-on already clears every filter, saturator and converter in the
    //  core, so re-preparing them there adds coefficient arithmetic to work
    //  that was happening anyway.  A sounding note keeps the factor it started
    //  with; the next note played uses the new one.  Quality is a CPU budget,
    //  not a musical control, and the parameter is live from the next note
    //  rather than dead.
    //
    //  Realtime-safe: prepare() on a filter or a saturator sets coefficients
    //  and fills fixed-size member arrays.  It allocates nothing, locks
    //  nothing and logs nothing.
    // -----------------------------------------------------------------------
    void SynthVoice::retuneCore (int factor) noexcept
    {
        osFactor = (factor >= 4) ? 4 : 2;

        const double oversampled = sr * (double) osFactor;

        for (int ch = 0; ch < 2; ++ch)
        {
            saturator[ch].prepare (oversampled);
            filter1[ch].prepare (oversampled);
            filter2[ch].prepare (oversampled);

            upsampler[ch].reset();
            downsampler[ch].reset();
            upsampler4[ch].reset();
            downsampler4[ch].reset();
        }
    }

    void SynthVoice::reset() noexcept
    {
        for (auto* o : { &oscA, &oscB, &oscC })
        {
            for (auto& s : o->sub)
                s.reset (0.0f);

            o->builtCount = 0;
            o->builtDetune = -1.0f;
            o->normalisation = 1.0f;
        }

        subOsc.reset();
        noise.reset();

        for (int ch = 0; ch < 2; ++ch)
        {
            body[ch].reset();
            density[ch].reset();
            saturator[ch].reset();
            filter1[ch].reset();
            filter2[ch].reset();
            outputDc[ch].reset();
            upsampler[ch].reset();
            downsampler[ch].reset();
            upsampler4[ch].reset();
            downsampler4[ch].reset();
        }

        ampEnv.reset();
        modEnv1.reset();
        modEnv2.reset();

        lastOscB = 0.0f;
        masterPhase = 0.0f;
        vibratoPhase = 0.0f;
        currentNote = -1;
        stealing = false;

        panSmoothL.reset();
        panSmoothR.reset();
    }

    // -----------------------------------------------------------------------
    void SynthVoice::noteOn (int midiNote, float vel, bool legatoContinuation,
                             const SynthBlockParams& p) noexcept
    {
        currentNote = midiNote;
        targetNote  = (float) midiNote;
        velocity    = juce::jlimit (0.0f, 1.0f, vel);
        stealing    = false;

        if (! legatoContinuation)
        {
            glidingNote = (p.glideTimeSeconds > 0.0f && ampEnv.isActive())
                              ? glidingNote : targetNote;

            // Start phases.  FREE leaves them wherever they were, which is what
            // makes a pad's attack different every time; the other three are
            // repeatable, which is what a bass and a pluck need.
            Rng rng ((juce::uint32) (voiceIndex * 104729 + midiNote * 31 + 7));

            auto startPhaseFor = [&] (const OscSettings& s, int subIndex, float layoutPhase)
            {
                switch (s.phaseMode)
                {
                    case PhaseMode::reset:  return s.phaseOffset;
                    case PhaseMode::random: return rng.next01();
                    case PhaseMode::controlled:
                        // The unison group's own distribution, plus a small
                        // repeatable offset: coherent enough for a firm attack,
                        // scattered enough not to click.
                        return std::fmod (layoutPhase + s.phaseOffset
                                          + (float) subIndex * 0.013f + 1.0f, 1.0f);
                    case PhaseMode::free:
                    default:
                        return -1.0f;      // leave the phase alone
                }
            };

            auto applyPhases = [&] (OscState& st, const OscSettings& s)
            {
                for (int i = 0; i < kMaxUnison; ++i)
                {
                    const float ph = startPhaseFor (s, i, st.layout.phase[i]);

                    if (ph >= 0.0f)
                        st.sub[i].reset (ph);
                }
            };

            applyPhases (oscA, p.oscA);
            applyPhases (oscB, p.oscB);
            applyPhases (oscC, p.oscC);

            if (p.oscA.phaseMode != PhaseMode::free)
            {
                subOsc.reset();
                masterPhase = 0.0f;
            }

            // The only place the core's rate can change.  See retuneCore().
            const int wanted = (p.quality == Quality::ultra) ? 4 : 2;

            if (wanted != osFactor)
                retuneCore (wanted);

            for (int ch = 0; ch < 2; ++ch)
            {
                filter1[ch].reset();
                filter2[ch].reset();
                body[ch].reset();
                density[ch].reset();
                saturator[ch].reset();
                outputDc[ch].reset();
                upsampler[ch].reset();
                downsampler[ch].reset();
                upsampler4[ch].reset();
                downsampler4[ch].reset();
            }
        }
        else
        {
            glidingNote = (p.glideTimeSeconds > 0.0f) ? glidingNote : targetNote;
        }

        ampEnv.noteOn (legatoContinuation);
        modEnv1.noteOn (legatoContinuation);
        modEnv2.noteOn (legatoContinuation);
    }

    void SynthVoice::noteOff() noexcept
    {
        ampEnv.noteOff();
        modEnv1.noteOff();
        modEnv2.noteOff();
    }

    void SynthVoice::glideTo (int midiNote) noexcept
    {
        currentNote = midiNote;
        targetNote = (float) midiNote;
    }

    void SynthVoice::steal() noexcept
    {
        stealing = true;
        ampEnv.fastRelease (sr);
        modEnv1.noteOff();
        modEnv2.noteOff();
    }

    // -----------------------------------------------------------------------
    void SynthVoice::rebuildUnison (OscState& st, const OscSettings& s,
                                    const SynthBlockParams& p, juce::uint32 salt) noexcept
    {
        const auto topology = topologyFor (p.character, s.unisonCount);

        // Only rebuild when something that shapes the group actually moved.
        // Rebuilding every block would re-randomise the phase distribution
        // continuously, which would be audible as a restless attack.
        const bool same = st.builtCount == s.unisonCount
                       && std::abs (st.builtDetune - s.detune) < 1.0e-4f
                       && st.builtTopology == topology;

        if (same)
            return;

        UnisonEngine::buildLayout (st.layout, topology, s.unisonCount, s.detune,
                                   salt + (juce::uint32) (voiceIndex * 37));

        st.builtCount = s.unisonCount;
        st.builtDetune = s.detune;
        st.builtTopology = topology;
    }

    void SynthVoice::prepareOscBlock (OscState& st, const OscSettings& s,
                                      const SynthBlockParams& p) noexcept
    {
        const int n = juce::jlimit (1, kMaxUnison, st.layout.count);

        st.pitchRatio = exp2Fast (s.pitchOffsetSemitones * (1.0f / 12.0f));

        const float detuneCents = p.character.detuneCentsMax;
        const float spread = juce::jlimit (0.0f, 1.0f, s.spread) * p.character.spreadScale;

        // Pan is evaluated at both ends of the block and interpolated across
        // it, so automating a pan is still smooth while the two square roots
        // and two trig calls behind panGains() happen twice per block instead
        // of once per sample.
        const int numSamples = juce::jmax (1, p.numSamples);
        const float panStart = juce::jlimit (-1.0f, 1.0f, s.pan.at (0));
        const float panEnd   = juce::jlimit (-1.0f, 1.0f, s.pan.at (numSamples));

        for (int v = 0; v < n; ++v)
        {
            st.detuneRatio[v] = centsToRatio (st.layout.detune[v] * detuneCents);

            const float offset = st.layout.pan[v] * spread;

            float l0, r0, l1, r1;
            panGains (juce::jlimit (-1.0f, 1.0f, panStart + offset), l0, r0);
            panGains (juce::jlimit (-1.0f, 1.0f, panEnd   + offset), l1, r1);

            st.panL[v] = l0;
            st.panR[v] = r0;
            st.panLStep[v] = (l1 - l0) / (float) numSamples;
            st.panRStep[v] = (r1 - r0) / (float) numSamples;
        }
    }

    void SynthVoice::updateBlock (const SynthBlockParams& p) noexcept
    {
        rebuildUnison (oscA, p.oscA, p, 0x51ED2701u);
        rebuildUnison (oscB, p.oscB, p, 0x9E3779B1u);
        rebuildUnison (oscC, p.oscC, p, 0xC2B2AE35u);

        prepareOscBlock (oscA, p.oscA, p);
        prepareOscBlock (oscB, p.oscB, p);
        prepareOscBlock (oscC, p.oscC, p);

        const float envScale = lerp (1.0f, varEnvTime, p.variation * p.character.variationScale);

        ampEnv.setShape (p.character.attackCurve, p.character.decayCurve);
        modEnv1.setShape (p.character.attackCurve, p.character.decayCurve);
        modEnv2.setShape (p.character.attackCurve, p.character.decayCurve);

        ampEnv.setParameters (p.ampAttack * envScale, p.ampDecay * envScale,
                              p.ampSustain, p.ampRelease * envScale);
        modEnv1.setParameters (p.env1Attack * envScale, p.env1Decay * envScale,
                               p.env1Sustain, p.env1Release * envScale);
        modEnv2.setParameters (p.env2Attack * envScale, p.env2Decay * envScale,
                               p.env2Sustain, p.env2Release * envScale);

        for (int ch = 0; ch < 2; ++ch)
        {
            filter1[ch].setModel (p.filterModel);
            filter1[ch].setType (p.filterType);
            filter2[ch].setModel (p.filter2Model);
            filter2[ch].setType (p.filter2Type);
        }

        if (! stealing)
            ampEnv.setParameters (p.ampAttack * envScale, p.ampDecay * envScale,
                                  p.ampSustain, p.ampRelease * envScale);
    }

    // -----------------------------------------------------------------------
    float SynthVoice::renderOscillator (OscState& st, const OscSettings& s,
                                        const SynthBlockParams& p,
                                        float baseHz, int i,
                                        float phaseMod, float syncFrac,
                                        float& outLeft, float& outRight) noexcept
    {
        const int n = juce::jlimit (1, kMaxUnison, st.layout.count);

        if (s.wave == Waveform::wavetable && p.bank == nullptr)
            return 0.0f;

        WavetableContext wt;
        wt.bank = p.bank;
        wt.family = s.wtFamily;
        wt.framePosition = juce::jlimit (0.0f, 1.0f, s.wtPosition.at (i) + varWtPos);
        wt.hermitePhase = (p.quality != Quality::eco);

        const float f0 = baseHz * st.pitchRatio;

        // Level normalisation depends on how fast the group beats, which
        // depends on the note - so it is recomputed when the pitch moves
        // materially, not once at note-on.
        if (std::abs (f0 - st.lastFundamental) > st.lastFundamental * 0.02f)
        {
            st.lastFundamental = f0;
            st.normalisation = UnisonEngine::normalisation (
                n, st.layout.maxOffsetNorm * p.character.detuneCentsMax, f0);
        }

        const float pw = juce::jlimit (0.02f, 0.98f, s.pulseWidth.at (i) + varPulse);
        const float invSr = 1.0f / (float) sr;
        const float fi = (float) i;

        float mono = 0.0f, l = 0.0f, r = 0.0f;

        for (int v = 0; v < n; ++v)
        {
            const float inc = f0 * st.detuneRatio[v] * invSr;
            const float y = st.sub[v].process (s.wave, inc, pw, phaseMod, syncFrac, wt);

            mono += y;
            l += y * (st.panL[v] + st.panLStep[v] * fi);
            r += y * (st.panR[v] + st.panRStep[v] * fi);
        }

        const float g = st.normalisation;

        // The mono sum is what feeds FM, PM and ring modulation: a modulator
        // that differed between channels would make those effects a stereo
        // process, which they are not.
        outLeft  += l * g;
        outRight += r * g;

        return mono * g;
    }

    void SynthVoice::render (float* left, float* right, int numSamples,
                             const SynthBlockParams& p) noexcept
    {
        if (! ampEnv.isActive())
            return;

        const auto& ch = p.character;

        const float variation = juce::jlimit (0.0f, 1.0f, p.variation * ch.variationScale);
        const float driftDepth = juce::jlimit (0.0f, 1.0f, p.driftAmount * ch.driftScale);

        // Glide coefficient.  Constant time reaches the target in roughly the
        // stated time whatever the interval; constant rate takes longer for a
        // wider interval, which is what a portamento on a real instrument does.
        const float glideCoef = (p.glideTimeSeconds <= 0.0001f)
            ? 0.0f
            : std::exp (-1.0f / (float) (p.glideTimeSeconds * sr));

        // A source whose level is zero at both ends of the block is not going
        // to be heard during it, so it is not rendered.  Most patches leave the
        // auxiliary oscillator and the noise silent, and the third oscillator
        // alone is a fifth of the voice's oscillator cost.
        //
        // The threshold is far below audibility and the levels are smoothed
        // over six milliseconds, so a source always resumes while it is still
        // inaudible - its phase never jumps into anything you could hear.
        constexpr float audible = 1.0e-4f;

        auto sounds = [numSamples] (const Ramp& r)
        {
            return r.at (0) > audible || r.at (numSamples) > audible;
        };

        const bool renderA     = sounds (p.oscA.level);
        // B is rendered when it is AUDIBLE or when it is a MODULATOR, which is
        // why its own level is only the first of these tests: a tine piano sets
        // B's level to zero and hears it only through the PM index.
        //
        // The envelope into that index belongs in this list for the same
        // reason. A preset whose index is zero at rest and opened only by
        // envelope 2 - which is exactly what a decaying tine is - would
        // otherwise skip the modulator entirely and produce a plain sine.
        const bool renderB     = sounds (p.oscB.level)
                                 || p.pm.at (0) > audible || p.fm.at (0) > audible
                                 || p.ringMod.at (0) > audible || p.sync.at (0) > audible
                                 || std::abs (p.pmEnvAmount) > audible;
        const bool renderC     = sounds (p.oscC.level);
        const bool renderSub   = sounds (p.subLevel);
        const bool renderNoise = sounds (p.noiseLevel);

        for (int i = 0; i < numSamples; ++i)
        {
            // -- pitch ------------------------------------------------------
            if (glideCoef > 0.0f)
            {
                if (p.glideConstantRate)
                {
                    // One semitone per glideTime seconds.
                    const float step = (float) (1.0 / (p.glideTimeSeconds * sr));
                    const float diff = targetNote - glidingNote;

                    glidingNote += juce::jlimit (-step, step, diff);
                }
                else
                {
                    glidingNote = targetNote + glideCoef * (glidingNote - targetNote);
                }
            }
            else
            {
                glidingNote = targetNote;
            }

            const float driftSemis = driftA.process (p.driftRate) * driftDepth * 0.09f;
            const float tuningVar  = varTuning * variation * 0.06f * (2.0f - ch.tuningPrecision);

            vibratoPhase += p.vibratoRate / (float) sr;
            vibratoPhase -= std::floor (vibratoPhase);

            const float wheel = juce::jlimit (0.0f, 1.0f, p.modWheel.at (i)) * p.modWheelDepth;
            const float press = juce::jlimit (0.0f, 1.0f, p.aftertouch.at (i)) * p.aftertouchDepth;

            const float vibrato = sineTurns (vibratoPhase) * p.vibratoDepth
                                  * juce::jmax (wheel, press) * 0.6f;

            // -- envelopes --------------------------------------------------
            // Advanced before the pitch is assembled, because envelope 2 is one
            // of the things that sets it.
            const float amp  = ampEnv.process();
            const float env1 = modEnv1.process();
            const float env2 = modEnv2.process();

            // The note as played: glide, bend, drift, unit variation, vibrato.
            // The filter tracks THIS one.
            const float note = glidingNote + p.pitchBendSemitones.at (i)
                             + driftSemis + tuningVar + vibrato;

            // The note as sounded.  Envelope 2 into pitch is what makes a kick
            // fall on its attack and a tom bend; it is deliberately NOT part of
            // `note` above, because key tracking follows the key that was
            // pressed.  Letting a 40-semitone attack transient sweep the filter
            // by three and a half octaves as well is not what anyone means by
            // "the filter follows the note".
            const float soundingNote = note + p.pitchEnvAmount * env2;

            // One pitch conversion for the whole voice: each oscillator's own
            // octave, semitone and fine offset became a constant ratio when the
            // block was prepared.
            const float baseHz = midiNoteToHz (soundingNote);

            // -- oscillators ------------------------------------------------
            // B is rendered first: it is the modulator for FM, PM, sync and
            // ring, and a modulator that lagged its carrier by a sample would
            // detune the sidebands.
            float bl = 0.0f, br = 0.0f;
            const float bMono = renderB
                ? renderOscillator (oscB, p.oscB, p, baseHz, i, 0.0f, -1.0f, bl, br)
                : 0.0f;

            const float fmAmount = p.fm.at (i);

            // Envelope 2 into the PM index.  A tine electric piano is a fixed
            // ratio whose index falls away while the note rings, which is a
            // different sound from a fixed index behind a closing filter - and
            // the filter version was all this instrument could do before.
            // Clamped to the knob's own range so an envelope cannot drive the
            // index somewhere the control could not reach.
            const float pmAmount = juce::jlimit (0.0f, 1.0f,
                                                 p.pm.at (i) + p.pmEnvAmount * env2);
            const float syncAmount = p.sync.at (i);

            // Hard sync: A is reset by a master running at B's pitch.  The
            // fractional position of the wrap within the sample is what the
            // oscillator's time-domain BLEP needs in order to band-limit it.
            float syncFrac = -1.0f;

            if (syncAmount > 0.001f)
            {
                const float masterInc = baseHz * oscB.pitchRatio / (float) sr
                                        * (1.0f + syncAmount * 2.0f);

                const float next = masterPhase + juce::jlimit (0.0f, 0.45f, masterInc);

                if (next >= 1.0f)
                    syncFrac = juce::jlimit (0.0f, 0.9999f,
                                             (next - 1.0f) / juce::jmax (1.0e-6f, masterInc));

                masterPhase = next - std::floor (next);
            }

            // FM adds to the increment, PM adds to the read phase.  Keeping
            // them separate is what stops FM accumulating into a pitch error:
            // PM never touches the accumulator at all.
            const float phaseMod = bMono * pmAmount * 0.5f
                                 + lastOscB * fmAmount * 0.35f;

            float al = 0.0f, ar = 0.0f;
            const float aMono = renderA
                ? renderOscillator (oscA, p.oscA, p, baseHz, i, phaseMod, syncFrac, al, ar)
                : 0.0f;

            float cl = 0.0f, cr = 0.0f;

            if (renderC)
                renderOscillator (oscC, p.oscC, p, baseHz, i, 0.0f, -1.0f, cl, cr);

            lastOscB = bMono;

            // -- sub and noise ----------------------------------------------
            // The sub is always an exact octave or two below, so its increment
            // is the note's own, halved or quartered - no second pitch
            // conversion is needed.
            const float subInc = renderSub
                ? baseHz * (p.subOctave == 1 ? 0.25f : 0.5f) / (float) sr
                : 0.0f;

            const float subOut = renderSub
                ? subOsc.process (p.subWave, subInc, p.subHarmonics.at (i))
                : 0.0f;

            float noiseOut = renderNoise ? noise.process (p.noiseType) : 0.0f;

            if (renderNoise && p.noiseAttackOnly > 0.001f)
            {
                // Exciter mode: the noise follows mod envelope 1 rather than
                // the amp envelope, so it can be gone long before the note is.
                const float gate = lerp (1.0f, env1, juce::jlimit (0.0f, 1.0f, p.noiseAttackOnly));
                noiseOut *= gate;
            }

            // -- voice mixer ------------------------------------------------
            const float balance = lerp (1.0f, varOscBalance, variation);

            const float aLevel = p.oscA.level.at (i) * balance;
            const float bLevel = p.oscB.level.at (i) * (2.0f - balance);
            const float cLevel = p.oscC.level.at (i);

            const float ring = p.ringMod.at (i);
            const float ringSignal = aMono * bMono;

            float mixL = al * aLevel + bl * bLevel + cl * cLevel;
            float mixR = ar * aLevel + br * bLevel + cr * cLevel;

            if (ring > 0.001f)
            {
                const float dryScale = 1.0f - ring * 0.6f;
                mixL = mixL * dryScale + ringSignal * ring * 1.2f;
                mixR = mixR * dryScale + ringSignal * ring * 1.2f;
            }

            // Sub and noise are added in mono, after the stereo sources.  The
            // sub especially: widening it is the fastest way to lose a bass on
            // a club system.
            const float monoAdd = subOut * p.subLevel.at (i) * ch.subReinforce
                                + noiseOut * p.noiseLevel.at (i) * 0.5f;

            mixL += monoAdd;
            mixR += monoAdd;

            // -- body, density, drive ---------------------------------------
            const float bodyAmount = p.body.at (i);
            const float bodyTilt   = juce::jlimit (-1.0f, 1.0f,
                                                   p.bodyTilt.at (i) + ch.bodyEvenBias);

            const float densityAmount = juce::jlimit (0.0f, 1.0f,
                p.density.at (i) * ch.densityScale + env2 * ch.densityBreath);

            mixL = density[0].process (body[0].process (mixL, bodyAmount, bodyTilt,
                                                        ch.bodyLowMidGain), densityAmount);
            mixR = density[1].process (body[1].process (mixR, bodyAmount, bodyTilt,
                                                        ch.bodyLowMidGain), densityAmount);

            const float drive = 1.0f + p.preDrive.at (i) * ch.preFilterDriveScale
                                       * lerp (1.0f, varDrive, variation) * 3.0f;

            // -- filter ------------------------------------------------------
            // Cutoff is assembled in the log domain, so key tracking, envelope
            // depth and velocity all behave as musical intervals rather than as
            // frequency offsets that mean different things at different pitches.
            const float keyTrackOctaves = (note - 60.0f) * (1.0f / 12.0f) * p.filterKeyTrack;

            const float envOctaves = p.filterEnvAmount * env1
                                       * lerp (1.0f, varFilterEnv, variation) * 6.0f;

            const float velOctaves = p.filterVelAmount * (velocity - 0.5f) * 3.0f;

            const float wheelOctaves = wheel * 2.0f + press * 1.5f;

            const float driftOctaves = driftFilter.process (p.driftRate * 0.7f)
                                       * driftDepth * 0.12f;

            const float varOctaves = log2Fast (juce::jmax (0.2f,
                                        lerp (1.0f, varCutoff, variation)));

            const float cutoffLog2 = p.filterCutoffLog2.at (i) + keyTrackOctaves
                                   + envOctaves + velOctaves + wheelOctaves
                                   + driftOctaves + varOctaves;

            const float cutoffHz = exp2Fast (juce::jlimit (4.3f, 14.3f, cutoffLog2));
            const float resonance = p.filterResonance.at (i);
            const float fDrive = p.filterDrive.at (i);
            const float morph = juce::jlimit (0.0f, 1.0f, env2);

            const float cutoff2Hz = p.filter2On
                ? exp2Fast (juce::jlimit (4.3f, 14.3f, p.filter2CutoffLog2.at (i)))
                : 1000.0f;
            const float res2 = p.filter2Resonance.at (i);
            const float mix2 = p.filter2Mix.at (i);

            const float satAmount = p.postSat.at (i);

            float* channels[2] = { &mixL, &mixR };

            for (int c = 0; c < 2; ++c)
            {
                auto& f1 = filter1[c];

                f1.setCutoff (cutoffHz);
                f1.setResonance (resonance);
                f1.setDrive (fDrive);
                f1.setMorph (morph);

                if (p.filter2On)
                {
                    filter2[c].setCutoff (cutoff2Hz);
                    filter2[c].setResonance (res2);
                }

                // ---- the oversampled nonlinear core ------------------------
                //
                // Drive, both filters and the saturator run at twice the rate,
                // or four times it under ULTRA.  NacarBench measured the
                // ladder's feedback saturator at +9.4 dB of inharmonic energy
                // and the drive stage at +3.8 dB; Body and the post saturator
                // contributed nothing, so they stay outside at every quality.
                //
                // One expression, used by both branches.  It is written once so
                // that raising the factor cannot quietly change the arithmetic
                // at the factor that was already shipping.
                const auto stage = [&] (float x) noexcept
                {
                    float y = tanhFast (x * drive) / drive;

                    y = f1.process (y);

                    if (p.filter2On)
                        y = lerp (y, filter2[c].process (y), mix2);

                    return saturator[c].process (y, satAmount, p.postSatMode, ch.satBias);
                };

                // Every stage above can generate an even-harmonic term, and an
                // even-harmonic generator also generates DC.  One blocker per
                // channel at the end of the voice keeps that out of the bus,
                // where it would quietly eat headroom from every other voice.
                if (osFactor == 4)
                {
                    float quarter[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    upsampler4[c].process (*channels[c], quarter);

                    for (auto& sample : quarter)
                        sample = stage (sample);

                    *channels[c] = outputDc[c].process (downsampler4[c].process (quarter));
                }
                else
                {
                    float half[2] = { 0.0f, 0.0f };
                    upsampler[c].process (*channels[c], half[0], half[1]);

                    for (auto& sample : half)
                        sample = stage (sample);

                    *channels[c] = outputDc[c].process (downsampler[c].process (half[0], half[1]));
                }
            }

            // -- amp and pan -------------------------------------------------
            const float velGain = lerp (1.0f, velocity, p.ampVelocity);
            const float g = amp * velGain * kVoiceGain;

            float gl, gr;
            panGains (varPan * variation * 0.25f, gl, gr);

            float outL = mixL * g * panSmoothL.process (gl * 1.41421356f);
            float outR = mixR * g * panSmoothR.process (gr * 1.41421356f);

            if (! sane (outL) || ! sane (outR))
            {
                outL = outR = 0.0f;

                for (int c = 0; c < 2; ++c)
                {
                    filter1[c].reset();
                    filter2[c].reset();
                    outputDc[c].reset();
                    upsampler[c].reset();
                    downsampler[c].reset();
                    upsampler4[c].reset();
                    downsampler4[c].reset();
                }
            }

            left[i]  += outL;
            right[i] += outR;
        }
    }
}
