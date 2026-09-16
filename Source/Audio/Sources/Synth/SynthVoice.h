#pragma once

#include "AnalogOscillator.h"
#include "Halfband.h"
#include "SynthFilter.h"
#include "SynthParams.h"
#include "UnisonEngine.h"
#include "VoiceModules.h"

namespace nacar::synth
{
    /**
        ONE NACAR VOICE.

        Owns the whole per-voice path from section 10 of the specification:

            OSC A + OSC B + OSC C + SUB + NOISE
              -> VOICE MIXER -> BODY -> PRE-FILTER DRIVE -> PRIMARY FILTER
              -> CREATIVE FILTER -> POST-FILTER SATURATION
              -> AMP ENVELOPE -> PAN

        Everything the voice needs is allocated in prepare().  render() does no
        allocation, takes no locks, and reads its settings from a const
        SynthBlockParams the engine built before the block started.

        The voice is deterministic.  Every random-looking quantity in it -
        unison distribution, phase scatter, per-voice variation, drift seeds -
        comes from an Rng seeded with the voice index, so the same patch
        produces the same sound in every render and on every machine.
    */
    class SynthVoice
    {
    public:
        void prepare (double sampleRate, int voiceIndex) noexcept;
        void reset() noexcept;

        void noteOn (int midiNote, float velocity, bool legatoContinuation,
                     const SynthBlockParams&) noexcept;
        void noteOff() noexcept;

        /** Retunes a sounding voice without restarting it - mono glide. */
        void glideTo (int midiNote) noexcept;

        /** Short fade to silence, for voice stealing. */
        void steal() noexcept;

        bool  isActive() const noexcept     { return ampEnv.isActive(); }
        bool  isReleasing() const noexcept  { return ampEnv.isReleasing(); }
        int   getNote() const noexcept      { return currentNote; }
        float getEnvelopeLevel() const noexcept { return ampEnv.getValue(); }
        juce::uint64 getStartOrder() const noexcept { return startOrder; }
        void  setStartOrder (juce::uint64 order) noexcept { startOrder = order; }

        /** Called once per block, before render(). */
        void updateBlock (const SynthBlockParams&) noexcept;

        /** Adds this voice's output into the two buffers. */
        void render (float* left, float* right, int numSamples,
                     const SynthBlockParams&) noexcept;

    private:
        struct OscState
        {
            AnalogOscillator sub[kMaxUnison];
            UnisonLayout layout;

            // Everything below is constant across a block and is computed once,
            // in prepareOscBlock().  Recomputing them per sample cost more than
            // the oscillators themselves.
            float detuneRatio[kMaxUnison] {};   ///< cents converted to a ratio
            float panL[kMaxUnison] {};
            float panR[kMaxUnison] {};
            float panLStep[kMaxUnison] {};      ///< so pan still moves smoothly
            float panRStep[kMaxUnison] {};

            float pitchRatio = 1.0f;            ///< the oscillator's own offset
            float normalisation = 1.0f;
            float lastFundamental = 0.0f;

            int   builtCount = 0;
            float builtDetune = -1.0f;
            UnisonTopology builtTopology = UnisonTopology::tight;
        };

        void rebuildUnison (OscState&, const OscSettings&, const SynthBlockParams&,
                            juce::uint32 salt) noexcept;

        void prepareOscBlock (OscState&, const OscSettings&,
                              const SynthBlockParams&) noexcept;

        float renderOscillator (OscState&, const OscSettings&, const SynthBlockParams&,
                                float baseHz, int sampleIndex,
                                float phaseMod, float syncFrac,
                                float& outLeft, float& outRight) noexcept;

        // -- identity -------------------------------------------------------
        int voiceIndex = 0;
        juce::uint64 startOrder = 0;
        double sr = 48000.0;

        // -- note state -----------------------------------------------------
        int   currentNote = -1;
        float targetNote = 60.0f;
        float glidingNote = 60.0f;
        float velocity = 1.0f;
        bool  stealing = false;

        // -- per-voice variation (deterministic) ----------------------------
        float varTuning = 0.0f, varCutoff = 1.0f, varEnvTime = 1.0f;
        float varPan = 0.0f, varDrive = 1.0f, varWtPos = 0.0f, varPulse = 0.0f;
        float varOscBalance = 1.0f, varFilterEnv = 1.0f;

        // -- sources --------------------------------------------------------
        OscState oscA, oscB, oscC;
        SubOscillator subOsc;
        NoiseExciter noise;

        DriftGenerator driftA, driftB, driftFilter;

        // -- shaping --------------------------------------------------------
        //
        //  One instance per channel, driven from identical coefficients.  Two
        //  TPT filters given the same cutoff and resonance are the same filter,
        //  so this cannot phase-shift one channel against the other - and it
        //  avoids applying the filter as a gain ratio taken from the mono sum,
        //  which spikes wherever that sum crosses zero.
        SynthBody body[2];
        DensityEngine density[2];
        VoiceSaturator saturator[2];
        SynthFilter filter1[2], filter2[2];
        DcBlocker outputDc[2];

        // The nonlinear core - drive, both filters, the saturator - runs at
        // twice the sample rate.  See Halfband.h for the measurements that
        // decided which stages go inside it and which stay outside.
        VoiceUpsampler upsampler[2];
        VoiceDownsampler downsampler[2];

        // -- envelopes ------------------------------------------------------
        Envelope ampEnv, modEnv1, modEnv2;

        // -- inter-oscillator state -----------------------------------------
        float lastOscB = 0.0f;
        float masterPhase = 0.0f;
        float vibratoPhase = 0.0f;

        // -- smoothing ------------------------------------------------------
        OnePole panSmoothL, panSmoothR;
    };
}
