#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

#include "../Harmony/Harmony.h"
#include "../Analysis/AnalysisResult.h"
#include "../Audio/Sources/Sample/SampleBuffer.h"

namespace nacar { class ParameterRegistry; }

namespace nacar::mutation
{
    /** The ten intents, in the order the MUTATE panel lists them. */
    enum class Intent
    {
        memory = 0, cloud, broken, reverse, distant,
        rhythmic, dark, ghost, playable, cinematic, count
    };

    const char* nameOf (Intent) noexcept;

    /** How far the result may travel from the source timbrally. Independent of
        harmony: a NEAR mutation in FREE harmony is a small change that may go
        anywhere tonally, and a FAR one in SAFE is a large change that stays in
        the key. The two controls are orthogonal on purpose. */
    enum class Distance { near_ = 0, far, unknown };

    /**
        WHAT THE USER HAS LOCKED.

        These are not hints. A preserve lock is a promise, and the engine must
        be able to demonstrate it kept it - `Tests` asserts each one by
        comparing the result against the source on the property it names. An
        engine that "mostly" preserves the low end has not preserved it.
    */
    struct Preserve
    {
        bool pitch = false, key = false, rhythm = false, transients = false;
        bool stereo = false, length = false, lowEnd = false;

        /** The master lock sets all seven. */
        static Preserve all() noexcept;
        bool any() const noexcept;
    };

    /**
        A MUTATION, WRITTEN DOWN.

        The seed is the whole of the randomness. Given the same recipe, the same
        source and the same analysis, the engine must produce bit-identical
        audio - that is what makes AGAIN reproducible, what makes a mutation
        worth saving as four numbers instead of a wave file, and what makes a
        regression test possible at all.

        Nothing in here is a pointer or a handle: a recipe outlives the session
        that made it and is stored in the session tree under RECIPE.
    */
    struct Recipe
    {
        juce::uint32 seed = 0;
        Intent       intent = Intent::memory;
        harmony::Mode harmonyMode = harmony::Mode::safe;
        Distance     distance = Distance::near_;
        Preserve     preserve;

        /** Bumped when a change to the engine would make an old seed render
            differently. A recipe whose version does not match the engine's is
            still loadable - it is history, and history is not re-renderable -
            but the interface must say so rather than silently producing
            different audio under the same name. */
        int engineVersion = 1;

        void writeTo (juce::ValueTree& recipeNode, juce::UndoManager* = nullptr) const;
        static Recipe readFrom (const juce::ValueTree& recipeNode);

        /** Message thread. Reads the four controls and the seven locks out of
            the registry, leaving the seed to the caller. */
        static Recipe fromParameters (const ParameterRegistry&);
    };

    /**
        WHAT A MUTATION PRODUCED.

        The engine does not write into the session or touch a parameter: it
        returns this, and the caller decides what to do with it. That is what
        lets a mutation be generated on a background thread, scored, discarded
        and regenerated without anything downstream noticing.
    */
    struct Result
    {
        bool ok = false;
        juce::String failure;         ///< why not, when ok is false. Shown to the user.

        SampleBuffer::Ptr audio;      ///< the mutated sample, ready to publish

        /** What actually happened, in the engine's own words, for the interface
            to show. Not a log: a short human sentence per operation applied,
            so a user can see what "BROKEN at FAR" did to their sound. */
        juce::StringArray operations;

        /** 0..1. The engine's own estimate of how well this one came out,
            used to rank candidates when several are generated. It is a
            heuristic and it is documented as one - it is not a claim that the
            instrument knows what sounds good. */
        float score = 0.0f;
    };
}
