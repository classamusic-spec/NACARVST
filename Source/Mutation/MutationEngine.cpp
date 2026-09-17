#include "MutationEngine.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace nacar::mutation
{
    // =======================================================================
    //  Names
    // =======================================================================
    const char* nameOf (Op op) noexcept
    {
        switch (op)
        {
            case Op::granularCloud:     return "GRANULAR";
            case Op::timeStretch:       return "STRETCH";
            case Op::transpose:         return "TRANSPOSE";
            case Op::octaveLayer:       return "OCTAVE LAYER";
            case Op::shimmerLayer:      return "SHIMMER";
            case Op::sliceShuffle:      return "SLICE SHUFFLE";
            case Op::sliceReverse:      return "SLICE REVERSE";
            case Op::reverseWhole:      return "REVERSE";
            case Op::stutter:           return "STUTTER";
            case Op::dropouts:          return "DROPOUTS";
            case Op::degrade:           return "GENERATIONAL LOSS";
            case Op::spectralBlur:      return "SPECTRAL BLUR";
            case Op::spectralGate:      return "SPECTRAL GATE";
            case Op::diffuse:           return "DIFFUSION";
            case Op::rhythmicGate:      return "GATE";
            case Op::filterSweep:       return "FILTER SWEEP";
            case Op::transientSoften:   return "SOFTEN ATTACKS";
            case Op::transientSharpen:  return "SHARPEN ATTACKS";
            case Op::decayExtend:       return "EXTEND DECAY";
            case Op::harmonicReinforce: return "HARMONIC REINFORCEMENT";
            case Op::darken:            return "DARKEN";
            case Op::widen:             return "WIDEN";
            case Op::swellReverse:      return "REVERSE SWELL";
            case Op::loopStabilise:     return "STABILISE";
            case Op::count:
            default:                    break;
        }

        return "OPERATION";
    }

    // =======================================================================
    //  Planning
    // =======================================================================
    namespace
    {
        // Distinct salts, so no two RNG streams in the engine can ever be the
        // same stream by accident.
        constexpr juce::uint32 kPlanSalt = 0x5Eu;
        constexpr juce::uint32 kStepSalt = 0x1000u;

        struct Candidate
        {
            Op    op;
            bool  core;          ///< always present: this is what the intent IS
            float weight;        ///< 0..1, chance of being chosen when it is not core
            float depthNear;     ///< depth at travel 0
            float depthFar;      ///< depth at travel 1
        };

        /** The ten intents as op selections and weights.  The core entries are
            what makes an intent recognisable at any distance: DISTANT always
            diffuses, GHOST always gates the spectrum, and a MEMORY that did
            not degrade would not be a MEMORY. */
        std::vector<Candidate> paletteFor (Intent intent)
        {
            switch (intent)
            {
                case Intent::memory: return {
                    { Op::degrade,          true,  1.00f, 0.30f, 0.95f },
                    { Op::transientSoften,  false, 0.70f, 0.20f, 0.60f },
                    { Op::darken,           false, 0.60f, 0.15f, 0.55f },
                    { Op::dropouts,         false, 0.35f, 0.10f, 0.55f },
                    { Op::diffuse,          false, 0.30f, 0.10f, 0.35f },
                    { Op::timeStretch,      false, 0.20f, 0.05f, 0.35f } };

                case Intent::cloud: return {
                    { Op::granularCloud,    true,  1.00f, 0.35f, 1.00f },
                    { Op::spectralBlur,     false, 0.80f, 0.25f, 0.85f },
                    { Op::diffuse,          false, 0.70f, 0.20f, 0.70f },
                    { Op::timeStretch,      false, 0.60f, 0.10f, 0.80f },
                    { Op::transientSoften,  false, 0.50f, 0.20f, 0.70f },
                    { Op::shimmerLayer,     false, 0.30f, 0.10f, 0.45f } };

                case Intent::broken: return {
                    { Op::sliceShuffle,     true,  1.00f, 0.25f, 0.95f },
                    { Op::stutter,          false, 0.70f, 0.20f, 0.75f },
                    { Op::dropouts,         false, 0.70f, 0.20f, 0.80f },
                    { Op::degrade,          false, 0.60f, 0.25f, 0.85f },
                    { Op::sliceReverse,     false, 0.50f, 0.15f, 0.60f },
                    { Op::transientSharpen, false, 0.40f, 0.20f, 0.60f } };

                case Intent::reverse: return {
                    { Op::reverseWhole,     true,  1.00f, 1.00f, 1.00f },
                    { Op::sliceReverse,     false, 0.55f, 0.20f, 0.70f },
                    { Op::swellReverse,     false, 0.60f, 0.25f, 0.80f },
                    { Op::diffuse,          false, 0.50f, 0.15f, 0.55f },
                    { Op::transientSoften,  false, 0.40f, 0.20f, 0.55f },
                    { Op::timeStretch,      false, 0.25f, 0.05f, 0.40f } };

                case Intent::distant: return {
                    { Op::diffuse,          true,  1.00f, 0.40f, 1.00f },
                    { Op::darken,           false, 0.85f, 0.25f, 0.75f },
                    { Op::spectralBlur,     false, 0.60f, 0.20f, 0.70f },
                    { Op::transientSoften,  false, 0.60f, 0.25f, 0.70f },
                    { Op::widen,            false, 0.50f, 0.15f, 0.60f },
                    { Op::decayExtend,      false, 0.40f, 0.20f, 0.60f } };

                case Intent::rhythmic: return {
                    { Op::rhythmicGate,     true,  1.00f, 0.35f, 0.95f },
                    { Op::stutter,          false, 0.60f, 0.15f, 0.65f },
                    { Op::filterSweep,      false, 0.60f, 0.20f, 0.70f },
                    { Op::transientSharpen, false, 0.55f, 0.20f, 0.65f },
                    { Op::sliceShuffle,     false, 0.35f, 0.10f, 0.45f },
                    { Op::degrade,          false, 0.25f, 0.10f, 0.40f } };

                case Intent::dark: return {
                    { Op::darken,           true,  1.00f, 0.40f, 0.95f },
                    { Op::octaveLayer,      false, 0.80f, 0.25f, 0.70f },
                    { Op::transpose,        false, 0.45f, 0.20f, 0.60f },
                    { Op::harmonicReinforce,false, 0.55f, 0.15f, 0.50f },
                    { Op::transientSoften,  false, 0.55f, 0.20f, 0.60f },
                    { Op::degrade,          false, 0.40f, 0.15f, 0.55f },
                    { Op::decayExtend,      false, 0.35f, 0.15f, 0.55f } };

                case Intent::ghost: return {
                    { Op::spectralGate,     true,  1.00f, 0.45f, 0.95f },
                    { Op::spectralBlur,     false, 0.75f, 0.25f, 0.80f },
                    { Op::transpose,        false, 0.35f, 0.15f, 0.50f },
                    { Op::shimmerLayer,     false, 0.55f, 0.15f, 0.55f },
                    { Op::diffuse,          false, 0.65f, 0.25f, 0.70f },
                    { Op::transientSoften,  false, 0.60f, 0.30f, 0.80f },
                    { Op::timeStretch,      false, 0.30f, 0.05f, 0.45f } };

                case Intent::playable: return {
                    { Op::loopStabilise,    true,  1.00f, 0.30f, 0.90f },
                    { Op::transientSoften,  false, 0.85f, 0.35f, 0.75f },
                    { Op::harmonicReinforce,false, 0.50f, 0.15f, 0.45f },
                    { Op::diffuse,          false, 0.40f, 0.10f, 0.40f },
                    { Op::darken,           false, 0.35f, 0.10f, 0.35f },
                    { Op::spectralBlur,     false, 0.30f, 0.10f, 0.40f } };

                case Intent::cinematic: return {
                    { Op::diffuse,          true,  1.00f, 0.45f, 0.95f },
                    { Op::octaveLayer,      true,  1.00f, 0.30f, 0.70f },
                    { Op::swellReverse,     false, 0.70f, 0.25f, 0.80f },
                    { Op::timeStretch,      false, 0.60f, 0.10f, 0.70f },
                    { Op::shimmerLayer,     false, 0.55f, 0.15f, 0.55f },
                    { Op::harmonicReinforce,false, 0.45f, 0.15f, 0.50f },
                    { Op::decayExtend,      false, 0.45f, 0.20f, 0.65f } };

                case Intent::count:
                default: break;
            }

            return { { Op::degrade, true, 1.00f, 0.30f, 0.80f } };
        }

        /** Which operations a lock forbids outright.  A lock is not a hint, so
            this is a filter and not a weighting: an operation that could break
            the promise is never in the plan at all. */
        bool permittedBy (Op op, const Preserve& p, const AnalysisResult& analysis,
                          const harmony::Context& ctx)
        {
            const bool movesTime = (op == Op::timeStretch || op == Op::sliceShuffle
                                    || op == Op::reverseWhole || op == Op::stutter
                                    || op == Op::loopStabilise || op == Op::swellReverse
                                    || op == Op::sliceReverse);

            const bool movesPitch = (op == Op::transpose || op == Op::octaveLayer
                                     || op == Op::shimmerLayer);

            const bool eatsAttacks = (op == Op::dropouts || op == Op::transientSoften
                                      || op == Op::rhythmicGate || op == Op::spectralGate
                                      || op == Op::granularCloud);

            // A gate or a dropout does not move the grid, but it removes events
            // from it, and a grid with holes in it is not the grid the source
            // had.  The lock is a promise, so it takes the strict reading.
            const bool removesEvents = (op == Op::dropouts || op == Op::rhythmicGate);

            const bool changesLength = (op == Op::timeStretch || op == Op::loopStabilise
                                        || op == Op::swellReverse);

            if (p.pitch && movesPitch)                  return false;
            if (p.rhythm && (movesTime || removesEvents)) return false;
            if (p.transients && (movesTime || eatsAttacks)) return false;
            if (p.length && changesLength)              return false;
            if (p.stereo && op == Op::widen)            return false;

            // The low-end lock splices the source's own low band back in, and
            // that only means anything if the two timelines still line up.
            if (p.lowEnd && changesLength)              return false;

            // A key that was never detected cannot be reinforced.  Inventing
            // one and resonating it would be the exact failure AnalysisResult
            // warns about.
            if (op == Op::harmonicReinforce && ! ctx.keyKnown) return false;

            // Granular reconstruction of a single grain is not granular.
            if (op == Op::granularCloud && analysis.silenceRatio > 0.98f) return false;

            return true;
        }

        /** EVERY pitch decision in the engine comes through here, and this is
            the only place in `Source/Mutation` that talks to `harmony::Context`.

            `Context::snap` and `Context::permits` take an ABSOLUTE semitone -
            they subtract the root themselves - so an interval is offered to
            them as `root + interval` and the root is taken back off the answer.
            Getting that backwards would silently transpose everything by the
            distance of the key from C, which is exactly the kind of bug that
            sounds plausible.

            The interval is split into its octave and its residue; the residue
            is snapped and the octave is put back, so an octave stays an octave
            in every scale.  Under SAFE with no detected key the context
            deliberately does not snap - so the engine restricts itself to whole
            octaves, which are the only move that cannot leave a key it does not
            know. */
        int chooseInterval (const harmony::Context& ctx, harmony::Mode mode,
                            const std::vector<int>& candidates, ops::Rng& rng,
                            float polyphonic)
        {
            if (candidates.empty())
                return 0;

            int pick = candidates[(size_t) (rng.nextUint() % (juce::uint32) candidates.size())];

            const auto toOctaves = [] (int semitones)
            {
                const int rounded = (int) std::llround ((double) semitones / 12.0);

                // Never round a move down to no move at all: an octave is the
                // smallest transposition that is always safe, so that is where
                // a rounded interval lands.
                if (semitones > 0) return 12 * juce::jmax (1, rounded);
                if (semitones < 0) return 12 * juce::jmin (-1, rounded);

                return 0;
            };

            // Chordal material transposed by anything but an octave changes
            // key, whatever the scale says about the interval itself.  The more
            // the analyser thinks it is hearing chords, the more the engine
            // leans on octaves.
            if (rng.next01() < juce::jlimit (0.0f, 1.0f, polyphonic) * 0.8f)
                pick = toOctaves (pick);

            if (mode == harmony::Mode::safe && ! ctx.keyKnown)
                return toOctaves (pick);

            const int octave = (int) std::floor ((double) pick / 12.0);
            const int residue = pick - octave * 12;

            return octave * 12 + (ctx.snap (ctx.root + residue) - ctx.root);
        }

        /** THE KEY LOCK OUTRANKS THE HARMONY CONTROL.

            `preserve_key` says the mutation must stay in the detected key.
            That has to hold even when the HARMONY control says FREE, so every
            pitch decision is taken against a context whose mode is SAFE when
            the lock is set.  The context is a value type with no state, so
            copying it and changing the mode is exactly what it is for. */
        harmony::Context pitchContextFor (const harmony::Context& ctx, const Preserve& preserve)
        {
            harmony::Context copy = ctx;

            if (preserve.key)
                copy.mode = harmony::Mode::safe;

            return copy;
        }

        harmony::Mode pitchModeFor (const Recipe& recipe)
        {
            return recipe.preserve.key ? harmony::Mode::safe : recipe.harmonyMode;
        }

        juce::String secondsText (double seconds)
        {
            return juce::String (seconds, seconds < 10.0 ? 2 : 1) + " s";
        }

        juce::String describe (const Plan::Step& step, const Plan& plan,
                               const harmony::Context& ctx, harmony::Mode mode)
        {
            const int depthPercent = (int) std::lround (100.0f * step.depth);

            const auto interval = [&step]
            {
                const int s = std::abs (step.semitones);

                juce::String name = juce::String (s) + (s == 1 ? " semitone" : " semitones")
                                    + (step.semitones < 0 ? " down" : " up");

                if (s == 12)  name = "an octave " + juce::String (step.semitones < 0 ? "down" : "up");
                if (s == 24)  name = "two octaves " + juce::String (step.semitones < 0 ? "down" : "up");

                return name;
            };

            const juce::String keyNote = (mode == harmony::Mode::safe && ctx.keyKnown)
                                             ? ", held in the key" : juce::String();

            switch (step.op)
            {
                case Op::granularCloud:
                    return "Rebuilt out of overlapping grains, "
                           + juce::String ((int) std::lround (fx::lerp (140.0f, 35.0f, step.depth)))
                           + " ms each.";

                case Op::timeStretch:
                    return plan.lengthFactor >= 1.0
                        ? "Stretched to " + juce::String (plan.lengthFactor, 2) + " times its length."
                        : "Compressed to " + juce::String (plan.lengthFactor, 2) + " of its length.";

                case Op::transpose:
                    return "Transposed " + interval() + keyNote + ".";

                case Op::octaveLayer:
                    return "A copy " + interval() + " underneath it" + keyNote + ".";

                case Op::shimmerLayer:
                    return "A thinner copy " + interval() + " over the top" + keyNote + ".";

                case Op::sliceShuffle:
                    return "Cut at the attacks and re-ordered, " + juce::String (depthPercent)
                           + "% of the slices moved.";

                case Op::sliceReverse:
                    return "The material between the attacks plays backwards.";

                case Op::reverseWhole:
                    return "Played backwards.";

                case Op::stutter:
                    return "Slices repeat their own first fragment.";

                case Op::dropouts:
                    return "Holes punched through it, the way a failing tape makes them.";

                case Op::degrade:
                    return "Copied and re-copied: bandwidth gone, a noise floor left behind.";

                case Op::spectralBlur:
                    return "Every partial smeared across time until it hangs in the air.";

                case Op::spectralGate:
                    return "Only the strongest partials are left; the rest is gone.";

                case Op::diffuse:
                    return "Moved into a space " + secondsText (0.6 + 5.0 * (double) step.depth)
                           + " deep.";

                case Op::rhythmicGate:
                    return "Gated against the grid the attacks describe.";

                case Op::filterSweep:
                    return "A low pass moving underneath it.";

                case Op::transientSoften:
                    return "The attacks rounded off.";

                case Op::transientSharpen:
                    return "The attacks brought forward.";

                case Op::decayExtend:
                    return "The decay holding on longer than it should.";

                case Op::harmonicReinforce:
                    return "The tones of the key resonated back into it.";

                case Op::darken:
                    return "Darkened: the top rolled away.";

                case Op::widen:
                    return "The stereo image pushed outward.";

                case Op::swellReverse:
                    return "A reversed swell arriving into the first attack.";

                case Op::loopStabilise:
                    return "Its steadiest passage taken out and made to sustain.";

                case Op::count:
                default: break;
            }

            return juce::String (nameOf (step.op)) + " applied.";
        }
    }

    Plan MutationEngine::makePlan (const Recipe& recipe, const AnalysisResult& analysis,
                                   const harmony::Context& ctx)
    {
        Plan plan;

        ops::Rng rng (ops::streamSeed (recipe.seed, kPlanSalt));

        // -- how far may this travel -------------------------------------
        switch (recipe.distance)
        {
            case Distance::near_:   plan.travel = 0.30f; break;
            case Distance::far:     plan.travel = 0.88f; break;
            case Distance::unknown: plan.travel = 0.15f + rng.next01() * 0.80f; break;
            default:                plan.travel = 0.30f; break;
        }

        int wanted = 2;

        switch (recipe.distance)
        {
            case Distance::near_:   wanted = 1 + (int) (rng.nextUint() % 2u); break;
            case Distance::far:     wanted = 3 + (int) (rng.nextUint() % 3u); break;
            case Distance::unknown: wanted = 1 + (int) (rng.nextUint() % 5u); break;
            default: break;
        }

        auto palette = paletteFor (recipe.intent);

        // UNKNOWN is allowed to bring one operation in from somewhere else.
        // That is the whole difference between it and a third fixed distance:
        // it is a question rather than a setting.
        if (recipe.distance == Distance::unknown && rng.next01() < 0.45f)
        {
            const auto other = paletteFor ((Intent) (rng.nextUint() % (juce::uint32) Intent::count));

            if (! other.empty())
            {
                auto borrowed = other[(size_t) (rng.nextUint() % (juce::uint32) other.size())];
                borrowed.core = false;
                borrowed.weight = 1.0f;

                palette.push_back (borrowed);
            }
        }

        std::vector<Candidate> chosen;

        for (const auto& c : palette)
            if (c.core && permittedBy (c.op, recipe.preserve, analysis, ctx))
                chosen.push_back (c);

        for (const auto& c : palette)
        {
            if ((int) chosen.size() >= wanted)
                break;

            if (c.core || ! permittedBy (c.op, recipe.preserve, analysis, ctx))
                continue;

            if (rng.next01() < c.weight * (0.45f + 0.55f * plan.travel))
                chosen.push_back (c);
        }

        // If every core operation was locked out, the intent cannot be itself.
        // Rather than return the source and call it a mutation, fall back to
        // the operations the locks do allow - and the sentence the user reads
        // says which intent could not be honoured.
        if (chosen.empty())
        {
            for (const auto& c : palette)
                if (permittedBy (c.op, recipe.preserve, analysis, ctx))
                {
                    chosen.push_back (c);
                    break;
                }
        }

        if (chosen.empty())
        {
            static const Op fallbacks[] { Op::degrade, Op::darken, Op::spectralBlur,
                                          Op::harmonicReinforce, Op::diffuse };

            for (Op op : fallbacks)
                if (permittedBy (op, recipe.preserve, analysis, ctx))
                {
                    chosen.push_back ({ op, true, 1.0f, 0.25f, 0.7f });
                    break;
                }
        }

        // -- length ------------------------------------------------------
        const bool hasStretch = std::any_of (chosen.begin(), chosen.end(),
                                             [] (const Candidate& c) { return c.op == Op::timeStretch; });
        const bool hasLoop = std::any_of (chosen.begin(), chosen.end(),
                                          [] (const Candidate& c) { return c.op == Op::loopStabilise; });

        if ((hasStretch || hasLoop) && ! recipe.preserve.length && ! recipe.preserve.lowEnd)
        {
            const float amount = 0.15f + 0.85f * plan.travel;

            plan.lengthFactor = hasLoop
                ? 1.0 + (double) amount * 1.8                    // a sustaining tone wants room
                : (double) fx::lerp (1.0f, rng.next01() < 0.25f ? 0.55f : 3.4f, amount);

            plan.lengthFactor = juce::jlimit (0.35, maxLengthFactor, plan.lengthFactor);
        }

        const bool wantsTail = std::any_of (chosen.begin(), chosen.end(),
                                            [] (const Candidate& c)
                                            {
                                                return c.op == Op::diffuse || c.op == Op::decayExtend;
                                            });

        if (wantsTail && ! recipe.preserve.length && ! recipe.preserve.lowEnd)
            plan.tailSeconds = juce::jlimit (0.0, maxTailSeconds,
                                             0.4 + 4.5 * (double) plan.travel);

        // -- depths, pitches and sentences -------------------------------
        const float polyphonic = juce::jlimit (0.0f, 1.0f, analysis.polyphonicLikelihood);

        const harmony::Context pitchCtx = pitchContextFor (ctx, recipe.preserve);
        const harmony::Mode pitchMode = pitchModeFor (recipe);

        int index = 0;

        for (const auto& c : chosen)
        {
            Plan::Step step;

            step.op = c.op;
            step.seed = ops::streamSeed (recipe.seed, kStepSalt + (juce::uint32) index * 7919u);

            const float base = fx::lerp (c.depthNear, c.depthFar, plan.travel);
            step.depth = juce::jlimit (0.02f, 1.0f, base * (0.85f + 0.30f * rng.next01()));

            if (c.op == Op::transpose)
                step.semitones = chooseInterval (pitchCtx, pitchMode,
                                                 { -12, -7, -5, -3, 3, 5, 7, 12 }, rng, polyphonic);
            else if (c.op == Op::octaveLayer)
                step.semitones = chooseInterval (pitchCtx, pitchMode,
                                                 { -12, -12, -24 }, rng, polyphonic);
            else if (c.op == Op::shimmerLayer)
                step.semitones = chooseInterval (pitchCtx, pitchMode,
                                                 { 12, 19, 24 }, rng, polyphonic);

            step.sentence = describe (step, plan, pitchCtx, pitchMode);

            plan.steps.push_back (step);
            ++index;
        }

        return plan;
    }

    // =======================================================================
    //  Rendering
    // =======================================================================
    namespace
    {
        /** Five band energies, in one pass, for the scoring heuristic. */
        std::array<float, 5> bandProfile (const ops::Buffer& b, double rate)
        {
            std::array<double, 5> sums { 0.0, 0.0, 0.0, 0.0, 0.0 };

            const int n = b.getNumSamples();

            if (n <= 0)
                return { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

            for (int c = 0; c < b.getNumChannels(); ++c)
            {
                fx::OnePoleTPT split[4];

                static constexpr float corners[4] { 200.0f, 800.0f, 3000.0f, 8000.0f };

                for (int i = 0; i < 4; ++i)
                    split[i].setCutoff (corners[i], rate);

                const auto* d = b.getReadPointer (c);

                for (int i = 0; i < n; ++i)
                {
                    float rest = d[i];

                    for (int k = 0; k < 4; ++k)
                    {
                        const float low = split[k].lowpass (rest);

                        sums[(size_t) k] += (double) low * low;
                        rest -= low;
                    }

                    sums[4] += (double) rest * rest;
                }
            }

            std::array<float, 5> out {};

            for (size_t i = 0; i < 5; ++i)
                out[i] = (float) std::sqrt (sums[i] / (double) n);

            return out;
        }

        /** How far the result actually travelled from the source: the mean
            log distance between their band profiles, plus what happened to the
            duration.  0 is "the same sound", 1 is "nothing in common". */
        float measuredTravel (const ops::Buffer& source, const ops::Buffer& result, double rate)
        {
            const auto a = bandProfile (source, rate);
            const auto b = bandProfile (result, rate);

            double total = 0.0;

            for (size_t i = 0; i < 5; ++i)
            {
                const double x = juce::jmax (1.0e-6, (double) a[i]);
                const double y = juce::jmax (1.0e-6, (double) b[i]);

                total += juce::jlimit (0.0, 1.0, std::abs (std::log10 (y / x)) / 1.5);
            }

            double travel = total / 5.0;

            const double lengthRatio = (double) juce::jmax (1, result.getNumSamples())
                                     / (double) juce::jmax (1, source.getNumSamples());

            travel = 0.75 * travel + 0.25 * juce::jlimit (0.0, 1.0,
                                                          std::abs (std::log2 (lengthRatio)) / 2.0);

            return (float) juce::jlimit (0.0, 1.0, travel);
        }

        float meanOf (const ops::Buffer& b) noexcept
        {
            const int n = b.getNumSamples();

            if (n <= 0)
                return 0.0f;

            double sum = 0.0;

            for (int c = 0; c < b.getNumChannels(); ++c)
            {
                const auto* d = b.getReadPointer (c);

                for (int i = 0; i < n; ++i)
                    sum += (double) d[i];
            }

            return (float) (sum / ((double) n * (double) juce::jmax (1, b.getNumChannels())));
        }

        /** 1 inside [low, high], falling to 0 over `fade` outside it. */
        float window (float value, float low, float high, float fade) noexcept
        {
            if (value >= low && value <= high)
                return 1.0f;

            const float distance = value < low ? low - value : value - high;

            return juce::jlimit (0.0f, 1.0f, 1.0f - distance / juce::jmax (1.0e-6f, fade));
        }

        void computePeaks (SampleBuffer& sample)
        {
            const int n = sample.lengthSamples();
            const int ch = juce::jmax (1, sample.numChannels());

            auto& peaks = sample.peaks;

            peaks.numChannels = ch;
            peaks.bucketSamples = juce::jmax (1, n / 2048);
            peaks.numBuckets = n > 0 ? (n - 1) / peaks.bucketSamples + 1 : 0;

            peaks.minimum.assign ((size_t) (peaks.numBuckets * ch), 0.0f);
            peaks.maximum.assign ((size_t) (peaks.numBuckets * ch), 0.0f);

            for (int c = 0; c < ch; ++c)
            {
                const auto* d = sample.audio.getReadPointer (juce::jmin (c, sample.numChannels() - 1));

                for (int b = 0; b < peaks.numBuckets; ++b)
                {
                    const int start = b * peaks.bucketSamples;
                    const int count = juce::jmin (peaks.bucketSamples, n - start);

                    float lo = 0.0f, hi = 0.0f;

                    for (int i = 0; i < count; ++i)
                    {
                        lo = juce::jmin (lo, d[start + i]);
                        hi = juce::jmax (hi, d[start + i]);
                    }

                    peaks.minimum[(size_t) (b * ch + c)] = lo;
                    peaks.maximum[(size_t) (b * ch + c)] = hi;
                }
            }
        }
    }

    Result MutationEngine::render (const Recipe& recipe, const SampleBuffer& source,
                                   const AnalysisResult& analysis, const harmony::Context& ctx)
    {
        return render (makePlan (recipe, analysis, ctx), recipe, source, analysis, ctx);
    }

    Result MutationEngine::render (const Plan& plan, const Recipe& recipe,
                                   const SampleBuffer& source, const AnalysisResult& analysis,
                                   const harmony::Context& ctx)
    {
        Result result;

        const int sourceLength = source.lengthSamples();
        const int channels = juce::jmax (1, source.numChannels());
        const double rate = source.sourceRate > 0.0 ? source.sourceRate : 44100.0;

        if (sourceLength <= 0 || source.numChannels() <= 0)
        {
            result.failure = "There is no sample to mutate.";
            return result;
        }

        ops::Buffer original;
        original.makeCopyOf (source.audio);
        ops::sanitise (original);

        ops::Buffer work;
        work.makeCopyOf (original);

        const float sourcePeak = ops::peakOf (original);
        const float sourceRms  = ops::rmsOf (original);

        const int maxSamples = (int) juce::jlimit (1.0,
                                                   600.0 * rate,
                                                   (double) sourceLength * maxLengthFactor
                                                       + maxTailSeconds * rate);

        const harmony::Context pitchCtx = pitchContextFor (ctx, recipe.preserve);
        const harmony::Mode pitchMode = pitchModeFor (recipe);

        double timeScale = 1.0;
        bool gridFollowsSource = true;
        bool tailAdded = false;

        const auto currentGrid = [&] (float minSliceMs)
        {
            static const std::vector<int> none;

            return ops::gridFrom (gridFollowsSource ? analysis.transients : none,
                                  work.getNumSamples(), rate,
                                  analysis.tempoIsUsable() ? analysis.tempo : 0.0,
                                  timeScale, minSliceMs);
        };

        const auto addTail = [&]
        {
            if (tailAdded || plan.tailSeconds <= 0.0 || recipe.preserve.length)
                return;

            const double allowed = juce::jmin (plan.tailSeconds,
                                               (double) (maxSamples - work.getNumSamples()) / rate);

            if (allowed > 0.01)
                ops::appendSilence (work, allowed, rate);

            tailAdded = true;
        };

        const auto harmonicFrequencies = [&]
        {
            std::vector<float> frequencies;

            if (! pitchCtx.keyKnown)
                return frequencies;

            // C2 as the reference octave: low enough to reinforce a body, high
            // enough that the resonator is not chasing a 30 Hz partial.
            const float rootHz = 65.406f * std::pow (2.0f, (float) juce::jlimit (0, 11, pitchCtx.root) / 12.0f);

            static const int degrees[] { 0, 3, 4, 7, 10, 12 };

            for (int degree : degrees)
            {
                // Absolute, not relative: `permits` subtracts the root itself.
                if (! pitchCtx.permits (pitchCtx.root + degree))
                    continue;

                frequencies.push_back (rootHz * std::pow (2.0f, (float) degree / 12.0f));

                if (frequencies.size() >= 4)
                    break;
            }

            return frequencies;
        };

        // -- run the plan -------------------------------------------------
        for (const auto& step : plan.steps)
        {
            ops::Rng rng (step.seed);

            const int length = work.getNumSamples();

            // An operation that cannot run on this source is dropped and said
            // so.  It is never replaced by something that does nothing and
            // reported as though it had happened.
            bool skipped = false;

            const auto skip = [&result, &skipped] (const juce::String& text)
            {
                result.operations.add (text);
                skipped = true;
            };

            const auto tooShort = [&] (int minimum)
            {
                if (length >= minimum)
                    return false;

                skip (juce::String (nameOf (step.op)) + " was left out: the source is only "
                      + juce::String ((int) std::lround (1000.0 * (double) length / rate))
                      + " ms long.");

                return true;
            };

            switch (step.op)
            {
                case Op::granularCloud:
                {
                    if (tooShort (512))
                        break;

                    ops::GrainSettings gs;

                    // A grain that wanders is what makes a cloud a cloud, and
                    // it is also the one thing about it that moves events in
                    // time.  With the grid or the attacks locked the grains
                    // stay where they came from: still a cloud, still dense,
                    // but time-aligned.
                    const bool timeLocked = recipe.preserve.rhythm || recipe.preserve.transients;

                    gs.grainMs = fx::lerp (140.0f, 35.0f, step.depth);
                    gs.overlap = fx::lerp (2.5f, 8.0f, step.depth);
                    gs.sizeJitter = 0.15f + 0.5f * step.depth;
                    gs.scatterMs = timeLocked ? 3.0f : fx::lerp (5.0f, 220.0f, step.depth);
                    gs.reverseProbability = timeLocked ? 0.0f : step.depth * 0.25f;
                    gs.spread = recipe.preserve.stereo ? 0.0f : step.depth * 0.8f;
                    gs.pitchProbability = recipe.preserve.pitch ? 0.0f : step.depth * 0.35f;
                    gs.outputLength = length;

                    if (! recipe.preserve.pitch)
                    {
                        gs.pitches.clear();

                        for (int candidate : { -12, -5, 0, 7, 12 })
                            gs.pitches.push_back (chooseInterval (pitchCtx, pitchMode,
                                                                  { candidate }, rng,
                                                                  analysis.polyphonicLikelihood));
                    }

                    ops::Buffer out;
                    ops::granulate (work, out, gs, rng, rate);
                    work = std::move (out);

                    gridFollowsSource = false;
                    break;
                }

                case Op::timeStretch:
                {
                    if (tooShort (256))
                        break;

                    const double factor = juce::jlimit (0.35, maxLengthFactor, plan.lengthFactor);

                    if (std::abs (factor - 1.0) < 0.01)
                    {
                        skip ("STRETCH was left out: the locks left the duration fixed.");
                        break;
                    }

                    ops::Buffer out;
                    ops::timeStretch (work, out, factor, rate);
                    work = std::move (out);

                    ops::setLengthExactly (work, juce::jmin (work.getNumSamples(), maxSamples));

                    timeScale *= factor;
                    break;
                }

                case Op::transpose:
                {
                    if (tooShort (256))
                        break;

                    if (step.semitones == 0)
                    {
                        skip ("TRANSPOSE was left out: the harmony allowed no interval to move by.");
                        break;
                    }

                    ops::Buffer out;
                    ops::pitchShiftSemitones (work, out, (double) step.semitones, rate);
                    work = std::move (out);
                    break;
                }

                case Op::octaveLayer:
                case Op::shimmerLayer:
                {
                    if (tooShort (256))
                        break;

                    if (step.semitones == 0)
                    {
                        skip (juce::String (nameOf (step.op))
                              + " was left out: the harmony allowed no interval to move by.");
                        break;
                    }

                    ops::Buffer layer;
                    ops::pitchShiftSemitones (work, layer, (double) step.semitones, rate);

                    if (step.op == Op::shimmerLayer)
                    {
                        ops::highPass (layer, 500.0f, rate, 2);
                        ops::lowPass (layer, 7000.0f, rate, 1);
                        ops::mixInto (work, layer, 0.25f + 0.35f * step.depth);
                    }
                    else
                    {
                        ops::lowPass (layer, 1800.0f, rate, 2);
                        ops::mixInto (work, layer, 0.35f + 0.45f * step.depth);
                    }

                    break;
                }

                case Op::sliceShuffle:
                {
                    const auto grid = currentGrid (40.0f);

                    if (grid.size() < 3)
                    {
                        skip ("SLICE SHUFFLE was left out: there is nothing long enough to cut.");
                        break;
                    }

                    ops::SliceSettings ss;

                    ss.shuffle = 0.15f + 0.75f * step.depth;
                    ss.repeat = 0.10f * step.depth;
                    ss.reverse = 0.15f * step.depth;
                    ss.drop = 0.08f * step.depth;

                    ops::sliceShuffle (work, grid, ss, rng, rate);

                    gridFollowsSource = false;
                    break;
                }

                case Op::sliceReverse:
                {
                    const auto grid = currentGrid (60.0f);

                    if (grid.size() < 3)
                    {
                        skip ("SLICE REVERSE was left out: there is nothing long enough to cut.");
                        break;
                    }

                    ops::reverseSlices (work, grid, 0.25f + 0.6f * step.depth, rng, rate);
                    break;
                }

                case Op::reverseWhole:
                    ops::reverseWhole (work);
                    gridFollowsSource = false;
                    break;

                case Op::stutter:
                {
                    const auto grid = currentGrid (80.0f);

                    if (grid.size() < 3)
                    {
                        skip ("STUTTER was left out: there is nothing long enough to repeat.");
                        break;
                    }

                    ops::stutter (work, grid, 0.2f + 0.6f * step.depth, rng, rate);
                    break;
                }

                case Op::dropouts:
                    ops::dropouts (work, 0.3f + 1.7f * step.depth, 0.5f + 0.5f * step.depth,
                                   rng, rate);
                    break;

                case Op::degrade:
                {
                    ops::DegradeSettings ds;

                    ds.bandwidth = 0.25f + 0.65f * step.depth;
                    ds.decimation = 0.15f + 0.7f * step.depth;
                    ds.bits = step.depth > 0.6f ? (step.depth - 0.6f) * 1.5f : 0.0f;
                    ds.noise = 0.15f + 0.55f * step.depth;
                    ds.saturation = 0.15f + 0.45f * step.depth;
                    ds.driftCents = (recipe.preserve.pitch || recipe.preserve.rhythm
                                     || recipe.preserve.transients)
                                        ? 0.0f : 2.0f + 10.0f * step.depth;
                    ds.tiltDark = 0.15f + 0.4f * step.depth;
                    ds.stereoFree = ! recipe.preserve.stereo;

                    ops::degrade (work, ds, rng, rate);
                    break;
                }

                case Op::spectralBlur:
                    if (tooShort (4096))
                        break;

                    ops::spectralBlur (work, 0.35f + 0.6f * step.depth, rate);
                    break;

                case Op::spectralGate:
                    if (tooShort (4096))
                        break;

                    ops::spectralGate (work, 0.55f + 0.44f * step.depth,
                                       fx::lerp (0.30f, 0.04f, step.depth), rate);
                    break;

                case Op::diffuse:
                case Op::decayExtend:
                {
                    addTail();

                    ops::DiffuseSettings ds;

                    // A predelay moves the whole sound later, and a wet-dominant
                    // mix moves where its energy lands.  Both are timing, so
                    // both go when the grid or the attacks are locked.
                    const bool timeLocked = recipe.preserve.rhythm || recipe.preserve.transients;

                    ds.sizeMs = fx::lerp (35.0f, 150.0f, step.depth);
                    ds.amount = step.op == Op::diffuse ? 0.25f + 0.55f * step.depth
                                                       : 0.20f + 0.30f * step.depth;

                    if (timeLocked)
                        ds.amount = juce::jmin (ds.amount, 0.45f);

                    ds.damping = 0.35f + 0.5f * step.depth;
                    ds.predelayMs = (step.op == Op::diffuse && ! timeLocked)
                                        ? 10.0f + 70.0f * step.depth : 0.0f;
                    ds.decaySeconds = 0.6f + 5.0f * step.depth;
                    ds.stereoFree = ! recipe.preserve.stereo;

                    ops::diffuse (work, ds, rng, rate);

                    if (step.op == Op::decayExtend)
                        ops::transientShape (work, 0.0f, 0.35f + 0.4f * step.depth, rate);

                    break;
                }

                case Op::rhythmicGate:
                {
                    const auto grid = currentGrid (90.0f);

                    if (grid.size() < 3)
                    {
                        skip ("GATE was left out: there is no grid to gate against.");
                        break;
                    }

                    ops::rhythmicGate (work, grid, 0.45f + 0.5f * step.depth,
                                       fx::lerp (0.8f, 0.45f, step.depth), rng, rate);
                    break;
                }

                case Op::filterSweep:
                    ops::filterSweep (work, 0.3f + 0.6f * step.depth,
                                      analysis.tempoIsUsable()
                                          ? (float) (analysis.tempo / 60.0 * 0.5)
                                          : 0.7f,
                                      2200.0f, rate);
                    break;

                case Op::transientSoften:
                    ops::transientShape (work, -0.3f - 0.65f * step.depth, 0.0f, rate);
                    break;

                case Op::transientSharpen:
                    ops::transientShape (work, 0.25f + 0.6f * step.depth, -0.15f * step.depth, rate);
                    break;

                case Op::harmonicReinforce:
                {
                    const auto frequencies = harmonicFrequencies();

                    if (frequencies.empty())
                    {
                        skip ("HARMONIC REINFORCEMENT was left out: the key was never detected.");
                        break;
                    }

                    ops::harmonicReinforce (work, frequencies, 0.15f + 0.45f * step.depth, rate);
                    break;
                }

                case Op::darken:
                    ops::tilt (work, -(0.25f + 0.6f * step.depth), rate);
                    ops::lowPass (work, fx::lerp (9000.0f, 900.0f, step.depth), rate, 2);
                    break;

                case Op::widen:
                    if (work.getNumChannels() < 2)
                    {
                        skip ("WIDEN was left out: the source is mono.");
                        break;
                    }

                    ops::widen (work, 0.2f + 0.7f * step.depth, rate);
                    break;

                case Op::swellReverse:
                {
                    if (tooShort (1024))
                        break;

                    const int head = juce::jlimit (256, juce::jmax (256, length),
                                                   (int) (rate * (0.4 + 2.2 * (double) step.depth)));

                    ops::Buffer swell (work.getNumChannels(), head);
                    swell.clear();

                    for (int c = 0; c < work.getNumChannels(); ++c)
                        swell.copyFrom (c, 0, work, c, 0, juce::jmin (head, length));

                    ops::reverseWhole (swell);
                    ops::applySwell (swell, 2.2f);
                    ops::lowPass (swell, 5000.0f, rate, 1);

                    if (! recipe.preserve.length && work.getNumSamples() + head <= maxSamples)
                    {
                        ops::prependSilence (work, (double) head / rate, rate);
                        gridFollowsSource = false;
                    }

                    ops::mixInto (work, swell, 0.5f + 0.4f * step.depth);
                    break;
                }

                case Op::loopStabilise:
                {
                    if (tooShort (2048))
                        break;

                    const int windowSamples = juce::jlimit (256, length,
                                                            (int) (rate * (0.35 + 1.4 * (double) step.depth)));

                    const int start = ops::mostStableWindow (work, windowSamples, rate);

                    const int outLength = recipe.preserve.length
                                              ? length
                                              : juce::jlimit (256, maxSamples,
                                                              (int) ((double) sourceLength * plan.lengthFactor));

                    ops::Buffer out (work.getNumChannels(), outLength);
                    ops::loopRegion (work, out, start, windowSamples, 70.0f, rate);

                    work = std::move (out);

                    ops::transientShape (work, -0.5f, 0.2f, rate);
                    ops::fadeEdges (work, 25.0f, rate);

                    timeScale = (double) outLength / (double) juce::jmax (1, sourceLength);
                    gridFollowsSource = false;
                    break;
                }

                case Op::count:
                default:
                    break;
            }

            ops::sanitise (work);

            if (work.getNumSamples() > maxSamples)
                ops::setLengthExactly (work, maxSamples);

            if (! skipped)
                result.operations.add (step.sentence);
        }

        // -- the locks -----------------------------------------------------
        //
        //  Everything above is what the plan was allowed to do.  Everything
        //  below is the engine demonstrating that it did what it promised: a
        //  lock is enforced here as well as being respected in the plan, so
        //  that a future operation that forgets about a lock cannot quietly
        //  break it.

        if (recipe.preserve.length && work.getNumSamples() != sourceLength)
        {
            ops::setLengthExactly (work, sourceLength);
            result.operations.add ("Held to its original length, exactly.");
        }

        ops::removeDc (work, rate);
        ops::sanitise (work);

        // Level.  Matched to the source rather than normalised to full scale:
        // a mutation that is louder than its source is not a better mutation.
        {
            const float workPeak = ops::peakOf (work);

            if (sourcePeak > 1.0e-6f && workPeak > 1.0e-7f)
            {
                const float target = juce::jmin (sourcePeak, 0.891f);      // -1 dBFS ceiling

                ops::applyGain (work, juce::jlimit (0.03125f, 32.0f, target / workPeak));
            }
            else if (sourcePeak > 1.0e-4f)
            {
                result.ok = false;
                result.failure = "Those operations left nothing audible. Try a nearer distance.";
                return result;
            }
        }

        if (recipe.preserve.stereo && channels >= 2 && work.getNumChannels() >= 2)
        {
            // Match the side-to-mid ratio the source had.  A source whose
            // channels are identical has no side at all, and comes out with
            // none: the two channels are then identical sample for sample,
            // which is the strongest form this promise can take.
            const auto sideMid = [] (const ops::Buffer& b)
            {
                double mid = 0.0, side = 0.0;

                const auto* l = b.getReadPointer (0);
                const auto* r = b.getReadPointer (1);

                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const double m = 0.5 * ((double) l[i] + (double) r[i]);
                    const double s = 0.5 * ((double) l[i] - (double) r[i]);

                    mid += m * m;
                    side += s * s;
                }

                return std::pair<double, double> { std::sqrt (mid), std::sqrt (side) };
            };

            const auto sourceRatio = sideMid (original);
            const auto workRatio = sideMid (work);

            const double wanted = sourceRatio.first > 1.0e-9
                                      ? sourceRatio.second / sourceRatio.first : 0.0;
            const double have = workRatio.first > 1.0e-9
                                      ? workRatio.second / workRatio.first : 0.0;

            if (have > 1.0e-9 || wanted <= 1.0e-9)
            {
                const float sideGain = wanted <= 1.0e-9
                                           ? 0.0f
                                           : (float) juce::jlimit (0.0, 8.0, wanted / juce::jmax (1.0e-9, have));

                auto* l = work.getWritePointer (0);
                auto* r = work.getWritePointer (1);

                for (int i = 0; i < work.getNumSamples(); ++i)
                {
                    const float mid = 0.5f * (l[i] + r[i]);
                    const float side = 0.5f * (l[i] - r[i]) * sideGain;

                    l[i] = mid + side;
                    r[i] = mid - side;
                }

                result.operations.add ("The stereo image is the source's own.");
            }
        }

        if (recipe.preserve.transients && ! analysis.transients.empty()
            && work.getNumSamples() > 0)
        {
            // The source's own attacks, put back where they were.  The window
            // is held at the source for its first few milliseconds and then
            // crossfaded back into the mutation, so an onset is at its original
            // sample position with its original shape - and the test measures
            // exactly that rather than taking the engine's word for it.
            const int hold = juce::jmax (8, (int) (0.006 * rate));
            const int fade = juce::jmax (8, (int) (0.014 * rate));
            const int lead = juce::jmax (2, (int) (0.001 * rate));

            const float workRms = ops::rmsOf (work);
            const float match = (sourceRms > 1.0e-7f && workRms > 1.0e-7f)
                                    ? juce::jlimit (0.125f, 8.0f, workRms / sourceRms)
                                    : 1.0f;

            for (int onset : analysis.transients)
            {
                const int start = juce::jlimit (0, work.getNumSamples() - 1, onset - lead);
                const int available = juce::jmin (work.getNumSamples() - start,
                                                  original.getNumSamples() - start);

                if (available < 16)
                    continue;

                for (int c = 0; c < work.getNumChannels(); ++c)
                {
                    const auto* s = original.getReadPointer (juce::jmin (c, original.getNumChannels() - 1));
                    auto* d = work.getWritePointer (c);

                    for (int i = 0; i < juce::jmin (available, hold + fade); ++i)
                    {
                        const float blend = i < hold
                                                ? 1.0f
                                                : 1.0f - (float) (i - hold) / (float) fade;

                        // Equal power, so the join does not dip.
                        const float sourceGain = std::sqrt (juce::jlimit (0.0f, 1.0f, blend));
                        const float workGain = std::sqrt (juce::jlimit (0.0f, 1.0f, 1.0f - blend));

                        d[start + i] = s[start + i] * match * sourceGain + d[start + i] * workGain;
                    }
                }
            }

            result.operations.add ("Every attack is where it was, with the shape it had.");
        }

        if (recipe.preserve.lowEnd)
        {
            if (work.getNumSamples() != original.getNumSamples())
            {
                result.operations.add ("The low end could not be held: the timeline moved.");
            }
            else
            {
                // Reserve the headroom the source's own bottom end needs BEFORE
                // putting it back, so that nothing after this has to scale the
                // band the lock is protecting.
                ops::Buffer low;
                ops::lowBandInto (original, low, lowEndCornerHz, rate, lowEndPoles);

                const float lowPeak = ops::peakOf (low);
                const float allowed = 0.94f - lowPeak;
                const float workPeak = ops::peakOf (work);

                if (allowed > 0.05f && workPeak > allowed)
                    ops::applyGain (work, allowed / workPeak);

                if (ops::spliceLowBand (work, original, lowEndCornerHz, rate))
                    result.operations.add ("Below " + juce::String ((int) (lowEndCornerHz * 0.8f))
                                           + " Hz this is the source, bin for bin.");
                else
                    result.operations.add ("The low end could not be held: the sample is longer "
                                           "than the engine can transform in one piece.");
            }
        }

        // -- it must never produce garbage ---------------------------------
        ops::sanitise (work);

        // The offset was already taken out before the locks were applied.  This
        // is the second look, and it is conditional on there actually being
        // one, because a DC blocker is a high pass and running one over a low
        // end this engine has just promised not to disturb would break that
        // promise to fix a problem that is not there.
        if (std::abs (meanOf (work)) > 0.0005f)
        {
            if (recipe.preserve.lowEnd)
                result.operations.add ("The source's own bottom end carries an offset, and the "
                                       "low-end lock means it is still there.");
            else
                ops::removeDc (work, rate);
        }

        {
            const float peak = ops::peakOf (work);

            if (peak > 0.98f)
            {
                ops::applyGain (work, 0.98f / peak);

                if (recipe.preserve.lowEnd)
                    result.operations.add ("The source's own low end left no headroom, so the "
                                           "whole result was brought down "
                                           + juce::String (20.0f * std::log10 (0.98f / peak), 1)
                                           + " dB.");
            }
        }

        ops::sanitise (work);

        if (sourcePeak > 1.0e-4f && ops::peakOf (work) < 1.0e-5f)
        {
            result.ok = false;
            result.failure = "Those operations left nothing audible. Try a nearer distance.";
            return result;
        }

        // -- score ----------------------------------------------------------
        //
        //  A HEURISTIC, AND ONLY A HEURISTIC.  It measures three things it can
        //  actually measure - whether the render is technically well formed,
        //  whether the amount of change matches the distance that was asked
        //  for, and whether the locks came out intact - and it combines them
        //  with fixed weights.  It is used to rank several candidates from the
        //  same source against each other.  It is not a claim that the
        //  instrument knows what sounds good, and nothing in the engine acts on
        //  it.
        {
            const float peak = ops::peakOf (work);
            const float rms = ops::rmsOf (work);
            const float dc = std::abs (meanOf (work));

            const float crestDb = (rms > 1.0e-9f && peak > 1.0e-9f)
                                      ? 20.0f * std::log10 (peak / rms) : 0.0f;

            const float levelDb = (rms > 1.0e-9f && sourceRms > 1.0e-9f)
                                      ? 20.0f * std::log10 (rms / sourceRms) : -60.0f;

            const float technical = 0.30f * window (peak, 0.15f, 0.95f, 0.15f)
                                  + 0.25f * window (crestDb, 5.0f, 22.0f, 10.0f)
                                  + 0.25f * window (levelDb, -10.0f, 6.0f, 12.0f)
                                  + 0.20f * window (dc, 0.0f, 0.001f, 0.01f);

            const float travelled = measuredTravel (original, work, rate);
            const float travelMatch = 1.0f - juce::jlimit (0.0f, 1.0f,
                                                           std::abs (travelled - plan.travel));

            float compliance = 1.0f;

            if (recipe.preserve.length && work.getNumSamples() != sourceLength)
                compliance -= 0.5f;

            if (recipe.preserve.lowEnd && work.getNumSamples() != sourceLength)
                compliance -= 0.5f;

            result.score = juce::jlimit (0.0f, 1.0f,
                                         0.45f * technical
                                         + 0.30f * travelMatch
                                         + 0.25f * juce::jmax (0.0f, compliance));
        }

        // -- publish ---------------------------------------------------------
        auto mutated = new SampleBuffer();

        mutated->audio = std::move (work);
        mutated->sourceRate = rate;
        mutated->file = source.file;

        mutated->displayName = (source.displayName.isNotEmpty() ? source.displayName
                                                                : juce::String ("SAMPLE"))
                             + " / " + juce::String (nameOf (recipe.intent))
                             + " " + juce::String (recipe.seed);

        computePeaks (*mutated);

        result.audio = mutated;
        result.ok = true;

        if (result.operations.isEmpty())
            result.operations.add ("Nothing could be applied: every operation this intent "
                                   "uses is locked out.");

        return result;
    }
}
