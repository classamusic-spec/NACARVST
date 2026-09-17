#include "SynthEngine.h"

#include "SynthVoice.h"
#include "WavetableBank.h"

#include <algorithm>

namespace nacar
{
    using namespace synth;

    namespace
    {
        /** Block-rate exponential smoothing, evaluated linearly within a block.

            The engine reads each parameter once per block.  Handing that value
            straight to the voices would step it at every block boundary, which
            is audible on anything that reaches the audio path - so each one is
            approached exponentially at block granularity and interpolated
            linearly across the samples in between. */
        struct Smoother
        {
            float current = 0.0f;
            bool  primed = false;

            void set (Ramp& ramp, float target, int numSamples, float coef) noexcept
            {
                if (! primed)
                {
                    // First block after prepare(): jump, do not glide up from
                    // zero, or every patch would fade in.
                    current = target;
                    primed = true;
                }

                const float next = target + coef * (current - target);
                ramp.set (current, next, numSamples);
                current = next;
            }

            void reset() noexcept { primed = false; current = 0.0f; }
        };

        struct SmootherSet
        {
            Smoother oscALevel, oscBLevel, oscCLevel;
            Smoother oscAPan, oscBPan, oscCPan;
            Smoother oscAWt, oscBWt, oscCWt;
            Smoother oscAPw, oscBPw;
            Smoother subLevel, subHarmonics, noiseLevel;
            Smoother sync, fm, pm, ring;
            Smoother body, bodyTilt, density, preDrive, postSat;
            Smoother cutoff, resonance, filterDrive;
            Smoother cutoff2, resonance2, mix2;
            Smoother bend, wheel, pressure;

            void reset() noexcept
            {
                for (auto* s : { &oscALevel, &oscBLevel, &oscCLevel, &oscAPan, &oscBPan,
                                 &oscCPan, &oscAWt, &oscBWt, &oscCWt, &oscAPw, &oscBPw,
                                 &subLevel, &subHarmonics, &noiseLevel, &sync, &fm, &pm,
                                 &ring, &body, &bodyTilt, &density, &preDrive, &postSat,
                                 &cutoff, &resonance, &filterDrive, &cutoff2, &resonance2,
                                 &mix2, &bend, &wheel, &pressure })
                    s->reset();
            }
        };

        /**
            FREQUENCY-DEPENDENT STEREO.

            Three bands, one rule each:

              low    collapsed to mono.  A bass that is wide on a monitor is a
                     bass that partially cancels on a club system, and the
                     specification is explicit that width must never be bought
                     at the cost of the low end.
              mid    the Width control.
              high   the High Width control, which is allowed to exceed 1.

            The splits are TPT one-poles, which are complementary by
            construction: low + mid + high reconstructs the input exactly when
            all three widths are 1, so the stage is transparent when it is not
            doing anything.
        */
        class StereoStage
        {
        public:
            void prepare (double sampleRate) noexcept
            {
                sr = juce::jmax (1.0, sampleRate);
                setCrossovers (130.0f, 2600.0f);
                reset();
            }

            void reset() noexcept
            {
                for (auto* f : { &lowL, &lowR, &midL, &midR })
                    f->reset();
            }

            void setCrossovers (float lowHz, float highHz) noexcept
            {
                const float lo = (float) juce::jlimit (40.0, 400.0, (double) lowHz);
                const float hi = (float) juce::jlimit (800.0, 6000.0, (double) highHz);

                lowL.setCutoff (lo, sr);
                lowR.setCutoff (lo, sr);
                midL.setCutoff (hi, sr);
                midR.setCutoff (hi, sr);
            }

            void process (float* left, float* right, int numSamples,
                          float width, float highWidth) noexcept
            {
                const float w  = juce::jlimit (0.0f, 2.0f, width);
                const float hw = juce::jlimit (0.0f, 2.0f, highWidth);

                for (int i = 0; i < numSamples; ++i)
                {
                    const float l = left[i];
                    const float r = right[i];

                    const float lowLeft  = lowL.lowpass (l);
                    const float lowRight = lowR.lowpass (r);

                    const float restLeft  = l - lowLeft;
                    const float restRight = r - lowRight;

                    const float midLeft  = midL.lowpass (restLeft);
                    const float midRight = midR.lowpass (restRight);

                    const float hiLeft  = restLeft  - midLeft;
                    const float hiRight = restRight - midRight;

                    // Low band: mid only, side discarded.
                    const float lowMono = (lowLeft + lowRight) * 0.5f;

                    const float midMid  = (midLeft + midRight) * 0.5f;
                    const float midSide = (midLeft - midRight) * 0.5f * w;

                    const float hiMid  = (hiLeft + hiRight) * 0.5f;
                    const float hiSide = (hiLeft - hiRight) * 0.5f * hw;

                    left[i]  = lowMono + midMid + midSide + hiMid + hiSide;
                    right[i] = lowMono + midMid - midSide + hiMid - hiSide;
                }
            }

        private:
            double sr = 48000.0;
            OnePoleTPT lowL, lowR, midL, midR;
        };
    }

    // =======================================================================
    //  Impl
    // =======================================================================
    struct SynthEngine::Impl
    {
        bool prepared = false;
        bool offline = false;
        double sampleRate = 48000.0;
        int maxBlock = 512;

        std::array<SynthVoice, (size_t) kMaxVoices> voices;
        juce::uint64 noteCounter = 0;

        SynthBlockParams params;
        SmootherSet smoothers;
        StereoStage stereo;

        const WavetableBank* bank = nullptr;

        juce::AudioBuffer<float> scratch;

        // -- performance state ----------------------------------------------
        float pitchBendNormalised = 0.0f;     ///< -1..1
        float modWheel = 0.0f;
        float aftertouch = 0.0f;
        bool  sustainPedal = false;

        std::array<int, (size_t) kMaxHeldNotes> heldNotes {};
        int numHeld = 0;

        std::array<bool, 128> sustained {};

        std::atomic<int> activeVoices { 0 };
        std::atomic<float> lastPeak { 0.0f };

        // -------------------------------------------------------------------
        void prepare (double sr, int blockSize, int numChannels)
        {
            sampleRate = juce::jmax (1.0, sr);
            maxBlock = juce::jmax (1, blockSize);

            bank = &WavetableBank::shared();

            for (int i = 0; i < kMaxVoices; ++i)
                voices[(size_t) i].prepare (sampleRate, i);

            stereo.prepare (sampleRate);
            smoothers.reset();

            scratch.setSize (2, maxBlock, false, true, true);
            scratch.clear();

            numHeld = 0;
            sustained.fill (false);
            sustainPedal = false;

            juce::ignoreUnused (numChannels);
            prepared = true;
        }

        void reset()
        {
            for (auto& v : voices)
                v.reset();

            stereo.reset();
            smoothers.reset();

            numHeld = 0;
            sustained.fill (false);
            sustainPedal = false;
            pitchBendNormalised = 0.0f;
            modWheel = 0.0f;
            aftertouch = 0.0f;

            activeVoices.store (0, std::memory_order_relaxed);
            lastPeak.store (0.0f, std::memory_order_relaxed);
        }

        // -------------------------------------------------------------------
        //  Parameter snapshot
        // -------------------------------------------------------------------
        static Waveform waveOf (const ParameterRegistry& p, PID pid) noexcept
        {
            return waveformFromIndex (p.choice (pid));
        }

        void readOsc (OscSettings& s, const ParameterRegistry& p,
                      PID wave, PID level, PID oct, PID semi, PID fine, PID pan,
                      PID phaseMode, PID phaseOff, PID wtPos, PID wtTable,
                      PID pulseWidth, PID unison, PID detune, PID spread,
                      Smoother& levelS, Smoother& panS, Smoother& wtS, Smoother* pwS,
                      int numSamples, float coef) noexcept
        {
            s.wave = waveOf (p, wave);
            s.phaseMode = phaseModeFromIndex (p.choice (phaseMode));
            s.phaseOffset = p.raw (phaseOff);

            s.pitchOffsetSemitones = p.raw (oct) * 12.0f
                                   + p.raw (semi)
                                   + p.raw (fine) * 0.01f;

            s.unisonCount = juce::jlimit (1, kMaxUnison, (int) std::round (p.raw (unison)));
            s.detune = p.raw (detune);
            s.spread = p.raw (spread);
            s.wtFamily = p.choice (wtTable);

            levelS.set (s.level, p.raw (level), numSamples, coef);
            panS.set (s.pan, p.raw (pan), numSamples, coef);
            wtS.set (s.wtPosition, p.raw (wtPos), numSamples, coef);

            if (pwS != nullptr)
                pwS->set (s.pulseWidth, p.raw (pulseWidth), numSamples, coef);
            else
                s.pulseWidth.snap (0.5f);
        }

        /** Reads the whole synth section. Called once per sub-block. */
        void buildParams (const ParameterRegistry& p, int numSamples)
        {
            params.sampleRate = sampleRate;
            params.numSamples = numSamples;
            params.bank = bank;

            params.quality = offline ? Quality::ultra
                                     : (Quality) juce::jlimit (0, 2, p.choice (PID::qualityMode));

            params.character = characterProfile (characterFromIndex (p.choice (PID::synthCharacter)));

            // 6 ms: fast enough that a knob feels immediate, slow enough that a
            // host sending coarse automation does not step.
            const float coef = std::exp (-(float) numSamples / (float) (sampleRate * 0.006));

            readOsc (params.oscA, p,
                     PID::oscAWave, PID::oscALevel, PID::oscAOctave, PID::oscASemi,
                     PID::oscAFine, PID::oscAPan, PID::oscAPhase, PID::oscAPhaseOffset,
                     PID::oscAWtPos, PID::oscAWtTable, PID::oscAPulseWidth,
                     PID::oscAUnison, PID::oscADetune, PID::oscASpread,
                     smoothers.oscALevel, smoothers.oscAPan, smoothers.oscAWt,
                     &smoothers.oscAPw, numSamples, coef);

            readOsc (params.oscB, p,
                     PID::oscBWave, PID::oscBLevel, PID::oscBOctave, PID::oscBSemi,
                     PID::oscBFine, PID::oscBPan, PID::oscBPhase, PID::oscBPhaseOffset,
                     PID::oscBWtPos, PID::oscBWtTable, PID::oscBPulseWidth,
                     PID::oscBUnison, PID::oscBDetune, PID::oscBSpread,
                     smoothers.oscBLevel, smoothers.oscBPan, smoothers.oscBWt,
                     &smoothers.oscBPw, numSamples, coef);

            // The auxiliary oscillator has no unison, no phase mode and no
            // pulse width of its own: it is a quiet harmonic layer, and giving
            // it the full set would cost eight more sub-voices per voice for
            // something that sits 18 dB down.
            params.oscC.wave = waveOf (p, PID::oscCWave);
            params.oscC.phaseMode = PhaseMode::controlled;
            params.oscC.phaseOffset = 0.0f;
            params.oscC.pitchOffsetSemitones = p.raw (PID::oscCOctave) * 12.0f
                                             + p.raw (PID::oscCSemi)
                                             + p.raw (PID::oscCFine) * 0.01f;
            params.oscC.unisonCount = 1;
            params.oscC.detune = 0.0f;
            params.oscC.spread = 0.0f;
            params.oscC.wtFamily = p.choice (PID::oscCWtTable);
            params.oscC.pulseWidth.snap (0.5f);

            smoothers.oscCLevel.set (params.oscC.level, p.raw (PID::oscCLevel), numSamples, coef);
            smoothers.oscCPan.set (params.oscC.pan, p.raw (PID::oscCPan), numSamples, coef);
            smoothers.oscCWt.set (params.oscC.wtPosition, p.raw (PID::oscCWtPos), numSamples, coef);

            params.subWave = p.choice (PID::subWave);
            params.subOctave = p.choice (PID::subOctave);
            smoothers.subLevel.set (params.subLevel, p.raw (PID::subLevel), numSamples, coef);
            smoothers.subHarmonics.set (params.subHarmonics, p.raw (PID::subHarmonics), numSamples, coef);

            params.noiseType = p.choice (PID::noiseType);
            params.noiseAttackOnly = p.raw (PID::noiseAttackOnly);
            smoothers.noiseLevel.set (params.noiseLevel, p.raw (PID::noiseLevel), numSamples, coef);

            smoothers.sync.set (params.sync, p.raw (PID::oscSync), numSamples, coef);
            smoothers.fm.set   (params.fm,   p.raw (PID::oscFmAmount), numSamples, coef);
            smoothers.pm.set   (params.pm,   p.raw (PID::oscPmAmount), numSamples, coef);
            smoothers.ring.set (params.ringMod, p.raw (PID::oscRingMod), numSamples, coef);

            params.pitchEnvAmount = p.raw (PID::pitchEnvAmount);
            params.pmEnvAmount    = p.raw (PID::pmEnvAmount);

            smoothers.body.set     (params.body,     p.raw (PID::bodyAmount), numSamples, coef);
            smoothers.bodyTilt.set (params.bodyTilt, p.raw (PID::bodyTilt), numSamples, coef);
            smoothers.density.set  (params.density,  p.raw (PID::densityAmount), numSamples, coef);
            smoothers.preDrive.set (params.preDrive, p.raw (PID::preFilterDrive), numSamples, coef);
            smoothers.postSat.set  (params.postSat,  p.raw (PID::postSaturation), numSamples, coef);
            params.postSatMode = p.choice (PID::postSatMode);

            params.filterModel = (FilterModel) juce::jlimit (0, 3, p.choice (PID::filterModel));
            params.filterType  = (FilterType)  juce::jlimit (0, 3, p.choice (PID::filterType));

            smoothers.cutoff.set (params.filterCutoffLog2,
                                  log2Fast (juce::jmax (20.0f, p.raw (PID::filterCutoff))),
                                  numSamples, coef);
            smoothers.resonance.set (params.filterResonance, p.raw (PID::filterResonance), numSamples, coef);
            smoothers.filterDrive.set (params.filterDrive, p.raw (PID::filterDrive), numSamples, coef);

            params.filterKeyTrack  = p.raw (PID::filterKeyTrack);
            params.filterEnvAmount = p.raw (PID::filterEnvAmount);
            params.filterVelAmount = p.raw (PID::filterVelAmount);

            params.filter2On = p.flag (PID::filter2On);

            // The creative filter's six responses collapse onto the model plus
            // type pair the SynthFilter understands: the first four are the
            // HAZE state variable, the last two are its own models.
            const int f2 = p.choice (PID::filter2Type);
            params.filter2Model = (f2 == 4) ? FilterModel::comb
                                : (f2 == 5) ? FilterModel::formant
                                            : FilterModel::haze;
            params.filter2Type = (FilterType) juce::jlimit (0, 3, f2);

            smoothers.cutoff2.set (params.filter2CutoffLog2,
                                   log2Fast (juce::jmax (20.0f, p.raw (PID::filter2Cutoff))),
                                   numSamples, coef);
            smoothers.resonance2.set (params.filter2Resonance, p.raw (PID::filter2Res), numSamples, coef);
            smoothers.mix2.set (params.filter2Mix, p.raw (PID::filter2Mix), numSamples, coef);

            params.ampAttack  = p.raw (PID::ampAttack);
            params.ampDecay   = p.raw (PID::ampDecay);
            params.ampSustain = p.raw (PID::ampSustain);
            params.ampRelease = p.raw (PID::ampRelease);
            params.ampVelocity = p.raw (PID::ampVelocity);

            params.env1Attack  = p.raw (PID::env1Attack);
            params.env1Decay   = p.raw (PID::env1Decay);
            params.env1Sustain = p.raw (PID::env1Sustain);
            params.env1Release = p.raw (PID::env1Release);

            params.env2Attack  = p.raw (PID::env2Attack);
            params.env2Decay   = p.raw (PID::env2Decay);
            params.env2Sustain = p.raw (PID::env2Sustain);
            params.env2Release = p.raw (PID::env2Release);

            params.variation   = p.raw (PID::voiceVariation);
            params.driftAmount = p.raw (PID::driftAmount);
            params.driftRate   = p.raw (PID::driftRate);

            params.modWheelDepth   = p.raw (PID::modWheelDepth);
            params.aftertouchDepth = p.raw (PID::aftertouchDepth);
            params.vibratoRate     = p.raw (PID::vibratoRate);
            params.vibratoDepth    = p.raw (PID::vibratoDepth);

            params.glideTimeSeconds = p.raw (PID::glideTime);
            params.glideConstantRate = p.choice (PID::glideMode) == 1;

            smoothers.bend.set (params.pitchBendSemitones,
                                pitchBendNormalised * p.raw (PID::pitchBendRange),
                                numSamples, coef);
            smoothers.wheel.set (params.modWheel, modWheel, numSamples, coef);
            smoothers.pressure.set (params.aftertouch, aftertouch, numSamples, coef);

            stereo.setCrossovers (p.raw (PID::lowMonoFreq) * params.character.lowMonoScale,
                                  2600.0f);
        }

        // -------------------------------------------------------------------
        //  Voice allocation
        // -------------------------------------------------------------------
        int polyphonyLimit (const ParameterRegistry& p) const noexcept
        {
            return juce::jlimit (1, kMaxVoices, (int) std::round (p.raw (PID::polyphony)));
        }

        /** Preference order: a free voice, then a released one, then the
            quietest, then the oldest.  A voice that is still in its attack is
            never the first choice - stealing the note someone just played is
            the most audible mistake an allocator can make. */
        int findVoiceToUse (int limit) noexcept
        {
            for (int i = 0; i < limit; ++i)
                if (! voices[(size_t) i].isActive())
                    return i;

            int bestReleasing = -1;
            float quietestReleasing = 2.0f;

            int quietest = -1;
            float quietestLevel = 2.0f;

            int oldest = -1;
            juce::uint64 oldestOrder = std::numeric_limits<juce::uint64>::max();

            for (int i = 0; i < limit; ++i)
            {
                auto& v = voices[(size_t) i];
                const float level = v.getEnvelopeLevel();

                if (v.isReleasing() && level < quietestReleasing)
                {
                    quietestReleasing = level;
                    bestReleasing = i;
                }

                if (level < quietestLevel)
                {
                    quietestLevel = level;
                    quietest = i;
                }

                if (v.getStartOrder() < oldestOrder)
                {
                    oldestOrder = v.getStartOrder();
                    oldest = i;
                }
            }

            if (bestReleasing >= 0) return bestReleasing;
            if (quietest >= 0 && quietestLevel < 0.25f) return quietest;
            return oldest >= 0 ? oldest : 0;
        }

        void pushHeld (int note) noexcept
        {
            for (int i = 0; i < numHeld; ++i)
                if (heldNotes[(size_t) i] == note)
                    return;

            if (numHeld < kMaxHeldNotes)
                heldNotes[(size_t) numHeld++] = note;
        }

        void removeHeld (int note) noexcept
        {
            for (int i = 0; i < numHeld; ++i)
            {
                if (heldNotes[(size_t) i] == note)
                {
                    for (int j = i; j < numHeld - 1; ++j)
                        heldNotes[(size_t) j] = heldNotes[(size_t) (j + 1)];

                    --numHeld;
                    return;
                }
            }
        }

        void noteOn (int note, float velocity, const ParameterRegistry& p) noexcept
        {
            const int mode = p.choice (PID::voiceMode);       // 0 POLY, 1 MONO, 2 LEGATO

            if (mode != 0)
            {
                const bool wasHeld = numHeld > 0;
                pushHeld (note);

                auto& v = voices[0];
                const bool legato = (mode == 2) && wasHeld && v.isActive() && ! v.isReleasing();

                if (legato)
                    v.glideTo (note);
                else
                    v.setStartOrder (++noteCounter);

                v.noteOn (note, velocity, legato, params);
                v.updateBlock (params);
                return;
            }

            const int limit = polyphonyLimit (p);
            const int index = findVoiceToUse (limit);
            auto& v = voices[(size_t) index];

            if (v.isActive() && ! v.isReleasing())
                v.steal();

            v.setStartOrder (++noteCounter);
            v.noteOn (note, velocity, false, params);
            v.updateBlock (params);
        }

        void noteOff (int note, const ParameterRegistry& p) noexcept
        {
            if (sustainPedal)
            {
                sustained[(size_t) juce::jlimit (0, 127, note)] = true;
                return;
            }

            const int mode = p.choice (PID::voiceMode);

            if (mode != 0)
            {
                removeHeld (note);

                auto& v = voices[0];

                if (numHeld > 0)
                {
                    // Fall back to the most recently held note that is still
                    // down, which is what a player expects from a trill.
                    v.glideTo (heldNotes[(size_t) (numHeld - 1)]);

                    if (mode == 1)
                        v.noteOn (heldNotes[(size_t) (numHeld - 1)], 0.8f, true, params);
                }
                else
                {
                    v.noteOff();
                }

                return;
            }

            for (auto& v : voices)
                if (v.getNote() == note && v.isActive() && ! v.isReleasing())
                    v.noteOff();
        }

        void releaseSustained (const ParameterRegistry& p) noexcept
        {
            for (int n = 0; n < 128; ++n)
            {
                if (sustained[(size_t) n])
                {
                    sustained[(size_t) n] = false;
                    noteOff (n, p);
                }
            }
        }

        void allNotesOff() noexcept
        {
            for (auto& v : voices)
                if (v.isActive())
                    v.noteOff();

            numHeld = 0;
            sustained.fill (false);
        }

        void handleMidi (const juce::MidiMessage& m, const ParameterRegistry& p) noexcept
        {
            if (m.isNoteOn())
            {
                noteOn (m.getNoteNumber(), m.getFloatVelocity(), p);
            }
            else if (m.isNoteOff())
            {
                noteOff (m.getNoteNumber(), p);
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
            {
                sustainPedal = false;
                allNotesOff();
            }
            else if (m.isPitchWheel())
            {
                pitchBendNormalised = ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f;
            }
            else if (m.isChannelPressure())
            {
                aftertouch = (float) m.getChannelPressureValue() / 127.0f;
            }
            else if (m.isAftertouch())
            {
                aftertouch = (float) m.getAfterTouchValue() / 127.0f;
            }
            else if (m.isController())
            {
                const int cc = m.getControllerNumber();

                if (cc == 1)
                {
                    modWheel = (float) m.getControllerValue() / 127.0f;
                }
                else if (cc == 64)
                {
                    const bool down = m.getControllerValue() >= 64;

                    if (sustainPedal && ! down)
                    {
                        sustainPedal = false;
                        releaseSustained (p);
                    }
                    else
                    {
                        sustainPedal = down;
                    }
                }
            }
        }

        // -------------------------------------------------------------------
        void renderSubBlock (juce::AudioBuffer<float>& buffer, int startSample,
                             int numSamples, const ParameterRegistry& p)
        {
            if (numSamples <= 0)
                return;

            buildParams (p, numSamples);

            scratch.clear (0, numSamples);
            scratch.clear (1, numSamples);

            auto* l = scratch.getWritePointer (0);
            auto* r = scratch.getWritePointer (1);

            const int limit = polyphonyLimit (p);
            const int mode = p.choice (PID::voiceMode);
            const int used = (mode == 0) ? limit : 1;

            int active = 0;

            for (int i = 0; i < used; ++i)
            {
                auto& v = voices[(size_t) i];

                if (! v.isActive())
                    continue;

                v.updateBlock (params);
                v.render (l, r, numSamples, params);
                ++active;
            }

            activeVoices.store (active, std::memory_order_relaxed);

            stereo.process (l, r, numSamples,
                            p.raw (PID::synthWidth) * params.character.widthScale,
                            p.raw (PID::highWidth));

            float peak = 0.0f;
            const int outChannels = buffer.getNumChannels();

            for (int i = 0; i < numSamples; ++i)
            {
                peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));

                buffer.addSample (0, startSample + i, l[i]);

                if (outChannels > 1)
                    buffer.addSample (1, startSample + i, r[i]);
            }

            lastPeak.store (peak, std::memory_order_relaxed);
        }

        void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                      const ParameterRegistry& p)
        {
            if (! prepared)
                return;

            const int total = buffer.getNumSamples();
            int position = 0;

            // Sample-accurate MIDI: render up to each event, apply it, carry on.
            for (const auto metadata : midi)
            {
                const int eventTime = juce::jlimit (0, total, metadata.samplePosition);
                const int span = eventTime - position;

                if (span > 0)
                {
                    renderSubBlock (buffer, position, span, p);
                    position = eventTime;
                }

                handleMidi (metadata.getMessage(), p);
            }

            if (position < total)
                renderSubBlock (buffer, position, total - position, p);
        }
    };

    // =======================================================================
    //  SynthEngine
    // =======================================================================
    SynthEngine::SynthEngine() : impl (std::make_unique<Impl>()) {}
    SynthEngine::~SynthEngine() = default;

    void SynthEngine::prepare (double sampleRate, int maximumBlockSize, int numChannels)
    {
        impl->prepare (sampleRate, maximumBlockSize, numChannels);
    }

    void SynthEngine::reset()
    {
        impl->reset();
    }

    void SynthEngine::setOfflineRendering (bool shouldRenderOffline) noexcept
    {
        impl->offline = shouldRenderOffline;
    }

    void SynthEngine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                               const ParameterRegistry& params, double hostBpm)
    {
        juce::ignoreUnused (hostBpm);     // the synth is not tempo-synced; Pulse will be
        impl->process (buffer, midi, params);
    }

    void SynthEngine::allNotesOff()
    {
        impl->allNotesOff();
    }

    int SynthEngine::getActiveVoiceCount() const noexcept
    {
        return impl->activeVoices.load (std::memory_order_relaxed);
    }

    float SynthEngine::getLastPeak() const noexcept
    {
        return impl->lastPeak.load (std::memory_order_relaxed);
    }

    bool SynthEngine::isPrepared() const noexcept
    {
        return impl->prepared;
    }
}
