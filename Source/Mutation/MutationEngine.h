#pragma once

#include "MutationRecipe.h"
#include "MutationOps.h"

#include <vector>

/**
    THE MUTATION ENGINE.

    A recipe, a decoded sample, what the analyser concluded about it and a
    harmony context go in; a new sample comes out, with a sentence per
    operation and a score.  Nothing else: the engine does not write to the
    session, does not touch a parameter and does not publish anything.  The
    caller decides what to do with what it gets back, which is what lets a
    mutation be generated on a background thread, looked at, and thrown away.

    OFFLINE.  It allocates, it takes time, and at FAR it will run several
    seconds of audio through half a dozen operations.  It must not be called
    from `processBlock`.

    DETERMINISM IS THE CONTRACT.  The same recipe, source and analysis produce
    bit-identical audio, every time, in any order, on this build.  That is what
    makes AGAIN reproducible and what makes a mutation worth storing as four
    numbers.  It is enforced by construction rather than by hope:

      * every RNG is derived from `Recipe::seed` through `ops::streamSeed`,
        and no RNG anywhere in the engine or the operation library is seeded
        from a clock, an address, a thread id or a counter that survives a
        render;
      * the engine holds no state.  `render` is static, every buffer it uses is
        a local, and nothing in `Source/Mutation` has a mutable global;
      * the plan is built before any audio is touched, so a step's random
        stream does not depend on what the previous step decided to do;
      * every per-channel decision is taken once for all channels, so the same
        recipe on a mono and a stereo source takes the same random path.

    `MutationTests` renders the same recipe twice and compares sample for
    sample, then renders a third time with a different recipe in between, which
    is what would catch shared mutable state if any were introduced.
*/
namespace nacar::mutation
{
    /** Bumped when a change in here would make an old seed render differently.
        A stored recipe carries the version it was made under; the interface is
        expected to say so rather than silently render something else. */
    inline constexpr int currentEngineVersion = 1;

    /** The operation vocabulary.  Each entry is implemented in `MutationOps`;
        nothing here is a stub, and an operation that cannot run on a given
        source is dropped from the plan and said so, never faked. */
    enum class Op
    {
        granularCloud,      ///< rebuild the sound out of overlapping grains
        timeStretch,        ///< SOLA stretch or compress
        transpose,          ///< move the whole sample, pitch decided by harmony
        octaveLayer,        ///< a transposed copy underneath
        shimmerLayer,       ///< a transposed copy above, quieter and darker
        sliceShuffle,       ///< re-order against the transient grid
        sliceReverse,       ///< reverse the material between onsets
        reverseWhole,       ///< reverse everything
        stutter,            ///< repeat the head of a slice
        dropouts,           ///< holes, as a failing medium makes them
        degrade,            ///< generational loss: bandwidth, resampling, noise
        spectralBlur,       ///< smear each partial across time
        spectralGate,       ///< keep the strongest partials, drop the rest
        diffuse,            ///< allpass diffusion into a damped tail
        rhythmicGate,       ///< gate against the grid
        filterSweep,        ///< a moving low pass
        transientSoften,    ///< round the attacks off
        transientSharpen,   ///< bring the attacks forward
        decayExtend,        ///< lengthen what follows the attack
        harmonicReinforce,  ///< resonate the scale tones
        darken,             ///< tilt and roll the top off
        widen,              ///< move the stereo image outward
        swellReverse,       ///< a reversed swell arriving into the onset
        loopStabilise,      ///< find the steadiest region and make it sustain
        count
    };

    const char* nameOf (Op) noexcept;

    /**
        WHAT THE ENGINE DECIDED, BEFORE IT TOUCHED ANY AUDIO.

        The plan is separated from the render for three reasons: the sentence
        the user reads comes from it, a test can assert a property of the
        engine's pitch decisions without rendering thirty seconds of audio, and
        a plan that depends only on the recipe and the analysis is much easier
        to prove deterministic than one that emerges as the audio is processed.
    */
    struct Plan
    {
        struct Step
        {
            Op    op = Op::degrade;
            float depth = 0.5f;         ///< 0..1, already scaled by distance
            int   semitones = 0;        ///< pitch decisions, ALREADY through harmony::Context
            juce::uint32 seed = 0;      ///< this step's own stream, derived from the recipe's
            juce::String sentence;      ///< what Result::operations will say
        };

        std::vector<Step> steps;

        /** 0..1: how far the plan is allowed to travel from the source.  NEAR
            and FAR pin it; UNKNOWN takes it from the seed, which is what makes
            UNKNOWN a different question rather than a third fixed setting. */
        float travel = 0.3f;

        /** What the plan intends to do to the duration.  1.0 when the length
            is locked, always. */
        double lengthFactor = 1.0;

        /** Seconds of tail the plan wants after the source ends.  Zero when the
            length is locked. */
        double tailSeconds = 0.0;
    };

    class MutationEngine
    {
    public:
        /** Message thread or background thread; pure, and cheap. */
        static Plan makePlan (const Recipe&, const AnalysisResult&, const harmony::Context&);

        /** Background thread.  Allocates, takes time, never throws. */
        static Result render (const Recipe&, const SampleBuffer& source,
                              const AnalysisResult&, const harmony::Context&);

        /** Renders a plan that was made earlier.  `render` above is this, with
            `makePlan` called first; the split exists so a caller that has
            already shown the user what it is about to do renders exactly that. */
        static Result render (const Plan&, const Recipe&, const SampleBuffer& source,
                              const AnalysisResult&, const harmony::Context&);

        /** The band the low-end lock protects, and the band the engine splices
            back when it is set.  Public because the test that proves the lock
            must measure the same band the engine promised. */
        static constexpr float lowEndCornerHz = 260.0f;
        static constexpr int   lowEndPoles = 4;

        /** The ceiling on how long a result may be, as a multiple of the
            source plus a fixed allowance for tails.  A mutation that turned a
            four-minute sample into half an hour would be a bug, not a feature. */
        static constexpr double maxLengthFactor = 6.0;
        static constexpr double maxTailSeconds = 12.0;
    };
}
