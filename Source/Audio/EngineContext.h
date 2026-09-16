#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "../Plugin/ParameterRegistry.h"

namespace nacar
{
    /** What every engine is told once, on the message thread, before it runs. */
    struct EngineSpec
    {
        double sampleRate = 48000.0;
        int    maxBlockSize = 512;
        int    numChannels = 2;
    };

    /**
        THE MACRO AND MODULATION STATE.

        Built once per block by the modulation engine and the macro resolver,
        then handed to every engine by const reference.  It carries three
        different kinds of thing and they are worth keeping straight:

          raw macros        exactly what the five knobs on the left panel say
          derived influence what those knobs *mean* to a downstream engine
          modulation        the per-sample output of the LFOs, Breath and Pulse

        The derived influences exist because the specification describes each
        macro as touching many engines at once - Memory reaches bandwidth,
        transients, saturation, pitch stability, phase, stereo and noise; World
        reaches stereo scale, early reflections, predelay, diffusion, distance
        and spectral softness.  Encoding that as one number per *concept* rather
        than one number per *destination* is what stops each engine having to
        know about the macro panel, and stops the macro panel having to know
        about each engine.

        Every engine documents its own macro response next to its code.  There
        is deliberately no central table of "macro X moves parameter Y by Z":
        the response is part of each engine's character, not a routing matrix.
    */
    struct MacroState
    {
        // -- raw macro positions --------------------------------------------
        float memory      = 0.0f;    ///< 0..1
        float character   = 0.0f;    ///< 0 clean .. 1 worn
        float motion      = 0.0f;    ///< 0 still .. 1 alive
        float world       = 0.0f;    ///< 0 intimate .. 1 expansive
        float weight      = 0.0f;    ///< 0..1
        float alter       = 0.0f;    ///< 0 primary identity .. 1 alternate
        float randomAmount = 0.0f;   ///< how far Random may wander

        int   memoryGeneration = 0;  ///< 0..3, the I/II/III/IV selector
        int   weightMode       = 1;  ///< 0 SUB, 1 BODY, 2 AIR

        // -- derived influences ---------------------------------------------
        //  All 0..1 unless noted.  An engine adds these to its own parameters
        //  rather than being driven by them, so a patch that sets a control
        //  explicitly still wins.

        float age        = 0.0f;   ///< Memory + Character: how used-up it sounds
        float grit       = 0.0f;   ///< Character alone: saturation and noise
        float movement   = 0.0f;   ///< Motion: how much anything may drift
        float scale      = 0.0f;   ///< World: size of the environment
        float distance   = 0.0f;   ///< World: how far away the source sits
        float wetBias    = 0.0f;   ///< World: an offset on atmospheric mixes
        float widthScale = 1.0f;   ///< World: multiplies stereo width. Pulse's
                                   ///< width duck is per-sample and is applied
                                   ///< by the chain's output stage, not here.
        float alterAmount = 0.0f;  ///< Alter, straight through

        // -- Pulse destination depths ---------------------------------------
        //  Already multiplied by the master Pulse Depth, so an engine only has
        //  to multiply by the envelope.
        float pulseToVolume = 0.0f;
        float pulseToFilter = 0.0f;
        float pulseToSpace  = 0.0f;
        float pulseToWidth  = 0.0f;
        float pulseToMemory = 0.0f;

        // -- per-sample modulation ------------------------------------------
        //  Each points at `numSamples` floats owned by the modulation engine
        //  and valid for the duration of this block only.  Never null once
        //  prepare() has run; an engine may index them without checking.
        const float* lfo1   = nullptr;   ///< -1..1
        const float* lfo2   = nullptr;   ///< -1..1
        const float* breath = nullptr;   ///< -1..1, organic and non-repeating
        const float* pulse  = nullptr;   ///< 0..1, 1 = fully ducked

        int numSamples = 0;

        // -- transport -------------------------------------------------------
        double sampleRate = 48000.0;
        double hostBpm    = 120.0;
        double ppqPosition = 0.0;
        bool   transportPlaying = false;

        forcedinline float lfo1At   (int i) const noexcept { return lfo1   != nullptr ? lfo1  [i] : 0.0f; }
        forcedinline float lfo2At   (int i) const noexcept { return lfo2   != nullptr ? lfo2  [i] : 0.0f; }
        forcedinline float breathAt (int i) const noexcept { return breath != nullptr ? breath[i] : 0.0f; }
        forcedinline float pulseAt  (int i) const noexcept { return pulse  != nullptr ? pulse [i] : 0.0f; }
    };

    /**
        Fills the derived fields of a MacroState from the raw macro parameters.

        Lives on its own so that the mapping from "what the knob says" to "what
        the engines are told" is in one readable place, and so that changing it
        does not mean touching six engines.
    */
    void resolveMacros (MacroState&, const ParameterRegistry&) noexcept;

    /**
        THE ENGINE CONVENTION.

        Every processing engine in NACAR - Memory, each FX slot, each atmosphere
        module, Weight - offers exactly these three methods:

            void prepare (const EngineSpec&);
            void reset();
            void process (juce::AudioBuffer<float>&, const ParameterRegistry&,
                          const MacroState&);

        `process` works in place on a stereo buffer whose length is
        `macros.numSamples`, and obeys the realtime contract: after prepare(),
        no allocation, no locks, no file IO, no logging, no juce::String.

        There is no base class.  A virtual call per engine per block would cost
        nothing, but the uniformity is worth more as a rule people follow than
        as an interface the compiler enforces - and it keeps each engine's
        header free of anything but that engine.
    */
}
