#pragma once

#include "../EngineContext.h"

#include "LFO.h"
#include "BreathEngine.h"
#include "PulseEngine.h"
#include "ModMatrix.h"

namespace nacar
{
    /**
        THE MODULATION LAYER.

        Everything downstream reads what this produces, so its output contract
        matters more than any algorithm inside it.

        Once per block it fills a MacroState's modulation fields:

            lfo1, lfo2     -1..1, per sample
            breath         -1..1, per sample, organic and non-repeating
            pulse           0..1, per sample, 1 = fully ducked
            pulseTo*        the five destination depths, already multiplied by
                            the master pulse_depth, so an engine only has to
                            multiply by the envelope

        PER-SAMPLE, NOT PER-BLOCK.  Pulse is a fast envelope - half a millisecond
        of attack at the short end - and a block-rate version of it would step at
        every buffer boundary, which is audible as a click on exactly the
        transient the duck exists to make room for.  The LFOs and Breath are slow
        enough that a block-rate value would survive, but they cost almost
        nothing per sample and a consumer should not have to know which of the
        four is which.

        The buffers are allocated in prepare() at EngineSpec::maxBlockSize and
        are never null afterwards.  They are valid until the next updateBlock().

        MACRO RESPONSE.  One, deliberately: Motion lifts Breath's depth towards
        full, `amount + 0.25 * movement * (1 - amount)`, so a patch that is
        "alive" breathes more without Motion ever being able to take breathing
        away from a patch that asked for it.  The LFOs have no macro response at
        all - they are explicit controls and the user's rate and depth are the
        whole statement.  Pulse has none either: it is a rhythmic decision, not
        an amount of life.

        WHAT THIS DOES NOT DO.  `MacroState::widthScale` is documented as
        "World + Pulse", but it is left exactly as the macro resolver wrote it.
        Pulse's width destination is a per-sample envelope; folding a block-rate
        version of it into widthScale would both lose the shape and double-count
        against the chain's output stage, which already applies
        `widthScale * (1 - pulse * pulseToWidth)` per sample.

        Realtime: after prepare(), nothing here allocates, locks, logs or builds
        a juce::String.  The registry is read once per block.
    */
    class ModulationEngine
    {
    public:
        ModulationEngine();
        ~ModulationEngine();

        void prepare (const EngineSpec&);
        void reset();

        /** Fills the modulation and Pulse fields of a MacroState for this
            block: lfo1, lfo2, breath, pulse and the five pulse depths.  The
            pointers it sets stay valid until the next call.

            The caller has already filled the raw macros and the derived
            influences, and has set numSamples, sampleRate, hostBpm,
            ppqPosition and transportPlaying. */
        void updateBlock (MacroState&, const ParameterRegistry&);

        /** A note-on, so MIDI-triggered Pulse works. Audio thread. */
        void noteTriggered() noexcept;

        /** The same, with the note's offset inside the coming block.  Prefer it
            where the offset is known: the no-argument form quantises the duck
            to the top of the block, which is up to 21 ms at 48 kHz. */
        void noteTriggeredAt (int sampleOffset) noexcept;

        /** Sidechain-triggered Pulse: hand it the block that should duck it.
            Called before updateBlock when a sidechain source exists.

            NOTHING CALLS THIS.  NACAR is an instrument with no side input, so
            there is nowhere for the signal to come from yet.  The method and
            the transient detector behind it are implemented and tested by
            inspection only; selecting SIDECHAIN in the interface falls back to
            CLOCK for any block in which no input arrived. */
        void setSidechainInput (const float* mono, int numSamples) noexcept;

        // -- beyond the MacroState contract ---------------------------------

        /** The four Pulse envelopes MacroState has no room for.

            MacroState carries one `pulse` pointer; this engine generates five
            differently shaped envelopes from the same trigger because a duck
            that is only a volume duck is the thing specification section 83
            says not to build.  `MacroState::pulse` is the VOLUME envelope - the
            reference shape - and an engine that wants the filter, space, width
            or memory envelope asks for it here.  The chain's output stage uses
            the WIDTH envelope; the other three are unclaimed. */
        const float* pulseEnvelope (PulseEngine::Destination) const noexcept;

        /** Live performance controllers, for the matrix's MOD WHEEL and
            AFTERTOUCH sources.  NacarEngine::process calls these from the
            MidiBuffer: CC 1 for the wheel, and channel pressure or polyphonic
            aftertouch, whichever the controller sends, for the other. */
        void setModWheel (float zeroToOne) noexcept;
        void setAftertouch (float zeroToOne) noexcept;

        /** Rebuilds the routing array from the session tree and publishes it to
            the audio thread.  Message thread only.  NacarProcessor calls this
            whenever the MODMATRIX branch of the session changes. */
        void rebuildModMatrix (const juce::ValueTree& modMatrixTree);

        /** Total modulation offset for a parameter this block, in normalised
            units, -1..1.  Audio thread, valid after updateBlock().

            NacarEngine turns these into ParameterRegistry's modulation overlay
            once per block, which is why no individual engine has to know the
            matrix exists: they read `p.raw()` and get the modulated value. */
        float modulationFor (PID) const noexcept;

        const ModMatrix& matrix() const noexcept { return modMatrix; }

    private:
        EngineSpec spec;
        bool prepared = false;
        int  capacity = 0;

        LFO lfo[2];
        BreathEngine breath;

        /** ORGANIC RANDOM, the matrix source, is a second Breath at fixed
            settings and a different seed rather than a smooth-random LFO: an
            LFO in SMOOTH RANDOM is a shape the user can already select on
            either LFO, so making the matrix source the same thing would leave
            the instrument with fifteen sources and fourteen behaviours. */
        BreathEngine organic;

        PulseEngine pulse;
        ModMatrix modMatrix;

        std::vector<float> lfoBuffer[2];
        std::vector<float> breathBuffer;
        std::vector<float> organicBuffer;

        std::atomic<float> modWheel { 0.0f };
        std::atomic<float> aftertouch { 0.0f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModulationEngine)
    };
}
