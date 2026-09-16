#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

namespace nacar
{
    /**
        WHAT THE INSTRUMENT KNOWS ABOUT A SAMPLE.

        Phase 19 produces this; phases 20 and 21 consume it. Every field below
        already has an identifier reserved in StateManager, because the session
        tree was written to carry this result before there was anything to put
        in it.

        CONFIDENCE IS NOT DECORATION. Every estimate carries one, and a consumer
        must use it. The mutation engine behaves differently when it knows the
        key than when it is guessing, and an instrument that treats a 0.2-
        confidence key detection as fact will transpose a drum loop into E flat
        minor and sound broken. A confidence of 0 means "I could not tell", and
        that is a legitimate and common answer - one-shot percussion has no key
        and no tempo, and saying so is the correct result, not a failure.
    */
    struct AnalysisResult
    {
        bool analysed = false;      ///< false until an analysis has actually run

        // -- harmony ---------------------------------------------------------
        int   root = -1;            ///< 0..11, C = 0. -1 means undetermined.
        float rootConfidence = 0.0f;

        /** Index into the scaleType parameter's choice list, minus the AUTO
            entry: 0 = MAJOR, 1 = MINOR, 2 = DORIAN, ... -1 means undetermined. */
        int   scale = -1;
        float scaleConfidence = 0.0f;

        // -- time ------------------------------------------------------------
        double tempo = 0.0;         ///< BPM. 0 means undetermined.
        float  tempoConfidence = 0.0f;

        /** Transient positions in samples, ascending, source-rate. */
        std::vector<int> transients;

        // -- level and spectrum ----------------------------------------------
        float peakLevel = 0.0f;         ///< linear 0..1
        float loudness  = -144.0f;      ///< LUFS-like, weighted
        float spectralCentroid = 0.0f;  ///< Hz
        float spectralRolloff  = 0.0f;  ///< Hz at 85% of energy
        float lowEnergy  = 0.0f;        ///< 0..1 fraction below 200 Hz
        float highEnergy = 0.0f;        ///< 0..1 fraction above 4 kHz

        // -- character -------------------------------------------------------
        float percussiveRatio = 0.0f;       ///< 0 sustained .. 1 percussive
        float polyphonicLikelihood = 0.0f;  ///< 0 monophonic .. 1 chordal
        float loopability = 0.0f;           ///< 0..1, how well the ends meet
        float silenceRatio = 0.0f;          ///< 0..1 fraction below the noise floor

        /** Writes into the session's ANALYSIS branch, using the reserved
            identifiers. Message thread. */
        void writeTo (juce::ValueTree& analysisBranch, juce::UndoManager* = nullptr) const;

        /** Reads one back. Message thread. */
        static AnalysisResult readFrom (const juce::ValueTree& analysisBranch);

        /** True when the key estimate is strong enough to act on. The number
            lives here rather than at each call site so that "do we know the
            key?" has one answer everywhere in the instrument. */
        bool keyIsUsable() const noexcept
        {
            return root >= 0 && rootConfidence >= 0.55f;
        }

        bool tempoIsUsable() const noexcept
        {
            return tempo > 0.0 && tempoConfidence >= 0.50f;
        }

        /**
            Whether the MODE is known, as opposed to the root.

            These are two different questions and conflating them is a real
            musical error. A bare triad pins its root confidently and says
            almost nothing about the mode - C-E-G is the first, third and fifth
            of C major, C lydian and C mixolydian alike. An engine that reads
            `keyIsUsable()` and then constrains to whichever mode won a coin
            flip will snap a minor third to a major one, which is the single
            most audible way to be wrong about a key.

            So a consumer that knows the root but not the mode should permit
            BOTH thirds rather than choose. `harmony::Context` does this.
        */
        bool scaleIsUsable() const noexcept
        {
            return keyIsUsable() && scale >= 0 && scaleConfidence >= 0.45f;
        }
    };
}
