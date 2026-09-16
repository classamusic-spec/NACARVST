#pragma once

#include "../EngineContext.h"
#include "GenerationModels.h"

namespace nacar
{
    /**
        MEMORY - the instrument's central idea.

        NACAR is a memory instrument: "SYNTH creates a powerful present.  MEMORY
        creates a past."  This engine is the part that has to make that true, so
        it is worth being precise about what it is and what it is not.

        Specification section 95 separates the three ageing systems:

            MEMORY   historical identity
            RETRO    medium and playback
            PATINA   surface texture and age

        Memory is neither a machine nor dirt.  It is *how many times this has
        been copied, and how long ago*.  Retro owns the tape transport, its wow
        and its dropouts; Patina owns the surface.  Memory owns generational
        loss, and nothing else in the instrument does.

        Section 69 lists what it may touch - bandwidth, transient softness,
        saturation, resampling, pitch instability, phase diffusion, stereo
        coherence, harmonic colouration, spectral aging, HF absorption, channel
        asymmetry, noise interaction - and then says the thing that matters:
        "Memory is multidimensional.  Do not reduce it to noise + wow/flutter."
        All twelve are in `CopyStage`, and they are twelve aspects of one idea
        rather than twelve effects in a row.

        `macro_memory` is how strongly it is applied.  `memory_gen` is how many
        copies deep you are: generation N runs N copies in series, each one
        degrading what the last one produced, artefacts included.  The two are
        independent, and both matter.

        ---------------------------------------------------------------------
        WHAT THIS ENGINE GUARANTEES

        - At `macro_memory` 0 the output is the input, sample for sample.  The
          block returns before touching anything.
        - The low end stays put.  Whenever anything in a copy is decorrelating,
          that copy high passes its own side signal at 165 Hz with two poles, so
          "mono below the split" is arithmetic rather than an observation about
          how steep a crossover happened to be.  Measured worst case across
          every rate, block size, generation and parameter extreme: low-band L/R
          correlation 0.998.
        - It colours, it does not get louder.  A slow RMS match, allowed +6 dB
          of restoration but only -3 dB of cut, holds the output within about a
          decibel of the input for anything musical.  It cannot hold it at the
          most extreme settings on very bright material, because four copies
          really do remove that much top - see the README.
        - Everything leaves through fx::guard().
        - After prepare(), nothing here allocates, locks or blocks.

        WHAT IT COSTS

        Memory is a delay-based effect and it is honest about that: each copy
        adds `CopyStage::latencySamples()` of onset delay, about 3 ms at any
        sample rate, so generation IV runs about 12 ms late.  NacarEngine adds
        `getLatencySamples()` to the chain's total and the processor reports it
        to the host, so the delay is declared rather than hidden; the engine
        still does not compensate it internally - see the README.
    */
    class MemoryEngine
    {
    public:
        MemoryEngine();
        ~MemoryEngine();

        void prepare (const EngineSpec&);
        void reset();
        void process (juce::AudioBuffer<float>&, const ParameterRegistry&, const MacroState&);

        /** Onset delay in samples for a given generation index (0..3).  Valid
            after prepare().  Provided so that whoever owns the chain can report
            or compensate it; the engine does not compensate itself, because a
            source-to-master latency budget is not a decision one engine makes. */
        int latencySamples (int generation) const noexcept;

        /** What Memory is adding right now: zero while the Memory macro is at
            zero, otherwise the total for the generation currently running.
            Audio thread, and named to match the convention the FX slots use so
            the chain can add it to its own latency without a special case. */
        int getLatencySamples() const noexcept;

    private:
        void processChunk (float* left, float* right, int numSamples,
                           const memory::StageMod&, int numStages);

        memory::CopyStage stage[memory::kMaxCopies];

        juce::AudioBuffer<float> scratch;   ///< [0..1] dry copy for fades, [2] mono mirror

        fx::DcBlocker outputDc[2];

        double sampleRate = 48000.0;
        int    maxBlock   = 512;

        // Level matching.  Measured per chunk, applied with one chunk of lag,
        // smoothed over a quarter of a second and bounded to +-3 dB.
        float trim = 1.0f, trimTarget = 1.0f, trimAlpha = 0.02f;

        // Transitions: leaving bypass and changing generation both restart the
        // engine from a clean state behind a short crossfade from the dry.
        int   fadeLength = 512;
        int   fadeRemaining = 0;
        bool  bypassed = true;
        int   lastGeneration = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MemoryEngine)
    };
}
