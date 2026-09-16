#include "EngineContext.h"

namespace nacar
{
    /**
        THE MACRO MAPPING.

        Five knobs on the left panel reach most of the instrument.  The
        specification describes each of them as touching many engines at once,
        and deliberately does not say which parameter each moves - because the
        answer differs per engine and is part of that engine's character.

        So this function does not route.  It converts "what the knob says" into
        a handful of named *concepts* that an engine can consult and add to its
        own settings.  Each engine then decides what, say, `age` means to it,
        next to its own code, where that decision can be read and tuned.

        The alternative - a central table mapping macro to parameter - was
        rejected on purpose.  It would put six engines' worth of voicing
        decisions in one file that nobody who works on those engines ever opens.
    */
    void resolveMacros (MacroState& m, const ParameterRegistry& p) noexcept
    {
        m.memory       = p.raw (PID::macroMemory);
        m.character    = p.raw (PID::macroCharacter);
        m.motion       = p.raw (PID::macroMotion);
        m.world        = p.raw (PID::macroWorld);
        m.weight       = p.raw (PID::macroWeight);
        m.alter        = p.raw (PID::macroAlter);
        m.randomAmount = p.raw (PID::randomAmount);

        m.memoryGeneration = juce::jlimit (0, 3, p.choice (PID::memoryGen));
        m.weightMode       = juce::jlimit (0, 2, p.choice (PID::weightMode));

        // -------------------------------------------------------------------
        //  AGE  -  how used-up it sounds.
        //
        //  Memory and Character both age a sound but they mean different
        //  things by it: Memory is how long ago and how many times, Character
        //  is how roughly it has been handled.  Memory leads because it is the
        //  instrument's title idea; Character adds to it rather than competing.
        // -------------------------------------------------------------------
        m.age = juce::jlimit (0.0f, 1.0f, m.memory * 0.65f + m.character * 0.4f);

        // GRIT is Character alone: saturation, noise, harmonic density.  Kept
        // separate from age so an engine can be dirty without being old, which
        // is the difference between a driven sound and a worn one.
        m.grit = m.character;

        // MOVEMENT passes straight through.  Motion is already the single
        // concept "how much may anything drift", and every engine that reads it
        // scales it to its own range.
        m.movement = m.motion;

        // -------------------------------------------------------------------
        //  WORLD  -  and the one thing the specification forbids.
        //
        //  Section 73: "Do not map World only to wet level."  So World produces
        //  four separate things, and wetness is the smallest of them:
        //
        //    scale     how big the environment is
        //    distance  how far away the source sits inside it
        //    wetBias   a bounded *offset* on atmospheric mixes
        //    width     intimate is narrower, expansive is wider
        //
        //  distance rises faster than linearly because the first part of the
        //  World control is mostly about size, and it is only once the room is
        //  large that stepping back from the source starts to mean anything.
        // -------------------------------------------------------------------
        m.scale = m.world;

        const float w = juce::jlimit (0.0f, 1.0f, m.world);
        m.distance = w * w * 0.9f;

        // Capped at a quarter: World can push a dry patch towards wet, but it
        // can never make one wet on its own, and it can never override a mix
        // the user set.
        m.wetBias = w * 0.25f;

        m.widthScale = 0.85f + w * 0.45f;

        m.alterAmount = m.alter;
    }
}
