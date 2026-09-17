#include "MemoryEngine.h"

namespace nacar
{
    MemoryEngine::MemoryEngine() = default;
    MemoryEngine::~MemoryEngine() = default;

    void MemoryEngine::prepare (const EngineSpec& spec)
    {
        sampleRate = juce::jmax (8000.0, spec.sampleRate);
        maxBlock   = juce::jmax (1, spec.maxBlockSize);

        // Channel 0 and 1 hold the dry signal during a crossfade; channel 2 is
        // the mirror that lets a mono buffer be processed as a stereo pair
        // without the stages needing a second code path.
        scratch.setSize (3, maxBlock, false, true, false);

        for (int i = 0; i < memory::kMaxCopies; ++i)
            stage[i].prepare (sampleRate, i);

        for (auto& d : outputDc)
            d.prepare (sampleRate);

        fadeLength = juce::jmax (32, (int) (0.012 * sampleRate));

        // One time constant for the level match, expressed once per maximum
        // block so the audio thread never calls exp().
        trimAlpha = 1.0f - std::exp (-(float) ((double) maxBlock / sampleRate) / 0.25f);

        reset();
    }

    void MemoryEngine::reset()
    {
        for (auto& s : stage)
            s.reset();

        for (auto& d : outputDc)
            d.reset();

        scratch.clear();

        trim = trimTarget = 1.0f;
        fadeRemaining = 0;
        bypassed = true;
        lastGeneration = -1;
    }

    int MemoryEngine::latencySamples (int generation) const noexcept
    {
        const int copies = juce::jlimit (0, memory::kMaxCopies - 1, generation) + 1;

        int total = 0;

        for (int i = 0; i < copies; ++i)
            total += stage[i].latencySamples();

        return total;
    }

    int MemoryEngine::getLatencySamples() const noexcept
    {
        return bypassed ? 0 : latencySamples (lastGeneration);
    }

    // -----------------------------------------------------------------------
    //  MACRO RESPONSE
    //
    //  Nothing here is a routing table: each line is a decision about what this
    //  engine should do when the performance surface moves, and it *adds* to
    //  the four Memory controls rather than replacing them, so a patch that
    //  sets memory_bandwidth explicitly still wins.
    //
    //    macro_memory   the depth of everything.  Zero means the block returns
    //                   untouched, so Memory is genuinely absent, not quiet.
    //    memory_gen     how many copies deep.  Structural, not a depth.
    //    macros.age     deepens the *character* of each copy rather than its
    //                   depth: more saturation, more bandwidth loss, more
    //                   colouration, a darker tilt, a louder floor.
    //    macros.movement widens and speeds up the two slow instabilities - the
    //                   pitch drift and the bandwidth wander.  A still patch
    //                   has a stable copy; an alive one has a wandering one.
    //    macros.pulseToMemory x pulse  momentarily deepens the artefacts:
    //                   saturation, decimation, reconstruction error, noise and
    //                   transient softening, for as long as the pulse lasts.
    //    macros.alterAmount  blends the whole character table toward the
    //                   alternate lineage - a sampler chain rather than a tape
    //                   chain.  The amount of damage is unchanged; its shape is
    //                   not.
    //    macros.breath  rides the slow parameters only: the drift, the wander
    //                   and the spectral hole.  Never the per-sample ones.
    // -----------------------------------------------------------------------
    void MemoryEngine::process (juce::AudioBuffer<float>& buffer,
                                const ParameterRegistry& params,
                                const MacroState& macros)
    {
        const int numChannels = buffer.getNumChannels();

        // prepare() has not run, so nothing downstream has any storage.  Every
        // engine in the instrument is entitled to assume this cannot happen; it
        // costs one comparison to be sure, and the alternative is reading a
        // delay line that does not exist yet.
        if (numChannels <= 0 || scratch.getNumChannels() < 3)
            return;

        const int requested = (macros.numSamples > 0) ? macros.numSamples : buffer.getNumSamples();
        const int total = juce::jmin (requested, buffer.getNumSamples());

        if (total <= 0)
            return;

        const float depth = juce::jlimit (0.0f, 1.0f, params.raw (PID::macroMemory));

        // Exact bypass.  Not "the chain at zero depth" - the chain at zero
        // depth still has delay lines in it, and a memory instrument whose
        // Memory control does nothing audible still has to do nothing at all.
        if (depth <= 0.0f)
        {
            bypassed = true;
            return;
        }

        const int generation = juce::jlimit (0, memory::kMaxCopies - 1, params.choice (PID::memoryGen));
        const int numStages  = generation + 1;

        // Engaging Memory, or changing generation, changes how much delay the
        // chain contains, so neither can be done by simply switching.  Both
        // restart the copies from silence behind a 12 ms crossfade from the dry
        // signal: audibly a re-print, which is the honest thing for a control
        // that says "this is now a different number of copies".
        if (bypassed || generation != lastGeneration)
        {
            reset();
            fadeRemaining = fadeLength;
            bypassed = false;
        }

        lastGeneration = generation;

        memory::StageSettings settings;
        settings.depth     = depth;
        settings.bandwidth = juce::jlimit (0.0f, 1.0f, params.raw (PID::memoryBandwidth));
        settings.wobble    = juce::jlimit (0.0f, 1.0f, params.raw (PID::memoryWobble));
        settings.diffusion = juce::jlimit (0.0f, 1.0f, params.raw (PID::memoryDiffusion));
        settings.asymmetry = juce::jlimit (0.0f, 1.0f, params.raw (PID::memoryAsymmetry));
        settings.age       = juce::jlimit (0.0f, 1.0f, macros.age);
        settings.movement  = juce::jlimit (0.0f, 1.0f, macros.movement);
        settings.alter     = juce::jlimit (0.0f, 1.0f, macros.alterAmount);

        for (int i = 0; i < numStages; ++i)
            stage[i].setSettings (settings);

        memory::StageMod mod;
        // MEMORY's own envelope: a 1.30 release against the user's, because
        // this is a texture change rather than a gate and it should let go
        // more slowly than the level does.
        mod.pulse       = macros.pulseMemory;
        mod.pulseDepth  = juce::jlimit (0.0f, 1.0f, macros.pulseToMemory);
        mod.breath      = macros.breath;
        mod.breathDepth = 0.5f + 0.5f * settings.movement;

        // The scratch is sized for one maximum block, so a host that hands over
        // more than it promised is served in pieces rather than trusted.
        int offset = 0;

        while (offset < total)
        {
            const int n = juce::jmin (maxBlock, total - offset);

            memory::StageMod chunkMod = mod;

            if (chunkMod.pulse != nullptr)  chunkMod.pulse  += offset;
            if (chunkMod.breath != nullptr) chunkMod.breath += offset;

            float* left  = buffer.getWritePointer (0) + offset;
            float* right = (numChannels > 1) ? buffer.getWritePointer (1) + offset
                                             : scratch.getWritePointer (2);

            if (numChannels == 1)
                juce::FloatVectorOperations::copy (right, left, n);

            processChunk (left, right, n, chunkMod, numStages);

            offset += n;
        }

        // A buffer with more than two channels keeps its extra channels
        // untouched.  Memory is a stereo idea; there is nothing sensible it
        // could do to a third channel it knows nothing about.
    }

    void MemoryEngine::processChunk (float* left, float* right, int numSamples,
                                     const memory::StageMod& mod, int numStages)
    {
        // -- what went in ---------------------------------------------------
        double inEnergy = 0.0;

        for (int i = 0; i < numSamples; ++i)
            inEnergy += (double) left[i] * left[i] + (double) right[i] * right[i];

        const bool fading = (fadeRemaining > 0);

        if (fading)
        {
            juce::FloatVectorOperations::copy (scratch.getWritePointer (0), left,  numSamples);
            juce::FloatVectorOperations::copy (scratch.getWritePointer (1), right, numSamples);
        }

        // -- the copies, in series ------------------------------------------
        for (int s = 0; s < numStages; ++s)
            stage[s].process (left, right, numSamples, mod);

        // -- gain staging ----------------------------------------------------
        // Specification section 148: do not use loudness to fake quality.  The
        // correction is measured on the previous chunk and moves with a quarter
        // second time constant, so it restores an average, never a transient.
        //
        // Its bounds are deliberately lopsided: up to +6 dB of make-up, but no
        // more than -3 dB of cut.  A chain of band limiters and soft clippers
        // only ever removes energy, so the make-up is a restoration and the cut
        // exists only to catch the saturation's own small lift.  Nothing here
        // can turn Memory into a loudness switch, because the loud direction is
        // the one it is not allowed to invent.
        const float gStart = trim;
        const float alpha  = juce::jlimit (0.0f, 1.0f,
                                           trimAlpha * (float) numSamples / (float) juce::jmax (1, maxBlock));
        const float gEnd   = gStart + (trimTarget - gStart) * alpha;
        const float gStep  = (gEnd - gStart) / (float) juce::jmax (1, numSamples);

        const float* dryL = scratch.getReadPointer (0);
        const float* dryR = scratch.getReadPointer (1);

        double outEnergy = 0.0;
        float  g = gStart;

        for (int i = 0; i < numSamples; ++i, g += gStep)
        {
            float l = outputDc[0].process (left[i]  * g);
            float r = outputDc[1].process (right[i] * g);

            outEnergy += (double) l * l + (double) r * r;

            if (fadeRemaining > 0)
            {
                const float t = 1.0f - (float) fadeRemaining / (float) fadeLength;

                l = fx::lerp (dryL[i], l, t);
                r = fx::lerp (dryR[i], r, t);

                --fadeRemaining;
            }

            left[i]  = fx::guard (l);
            right[i] = fx::guard (r);
        }

        trim = gEnd;

        // Silence is not evidence.  Below the floor the ratio is meaningless
        // and the noise the engine itself added would drive the trim to zero.
        const double floorEnergy = 1.0e-12 * (double) juce::jmax (1, numSamples);

        if (inEnergy > floorEnergy && outEnergy > floorEnergy)
        {
            const float ratio = juce::jlimit (0.25f, 4.0f, (float) std::sqrt (inEnergy / outEnergy));

            trimTarget = juce::jlimit (0.708f, 2.0f, gEnd * ratio);
        }
    }
}
