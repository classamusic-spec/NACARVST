#pragma once

#include "../DspCommon.h"

/**
    WHAT ONE COPY DOES.

    Specification section 70 asks for four generations - I subtle recorded
    character, II noticeable resampling, III sample-of-a-sample, IV deep
    artefact - and says that generation changes internal processing behaviour.

    The decision this file encodes is that a generation is *a number of copies*,
    not a depth control.  Copying is a cascade: one pass band-limits a little,
    softens a transient a little, saturates a little, smears phase a little,
    drifts in pitch a little and leaves a little noise behind - and the next
    pass does the same thing again to the result, including to the artefacts the
    previous pass added.  That compounding is the entire difference between a
    third-generation copy and a first-generation copy turned up, and it is what
    "sample of a sample" means.

    So `CopyStage` is one copy, complete, with all twelve dimensions of section
    69 in it, and `MemoryEngine` runs N of them in series.  The four entries in
    the character table below are not four intensities of the same thing: each
    describes what the *nth* pass through the medium did, and they get
    progressively less forgiving, so generation IV's last pass has behaviour -
    aliased decimation, one-channel dropouts, spectral holes - that generation
    I's single pass never reaches at any setting.

    The alternate table is the same twelve dimensions with a different lineage:
    a digital sampler rather than a tape chain.  `macros.alterAmount` blends
    between the two, which is what "shift the generation character toward the
    alternate identity" means here.
*/
namespace nacar::memory
{
    inline constexpr int kMaxCopies = 4;

    /** Coefficients are recomputed, and level-dependent filters retuned, once
        every this many samples.  At 48 kHz that is a 1.5 kHz control rate:
        faster than any envelope in here (the quickest is 20 ms) and eight times
        cheaper than retuning a filter per sample. */
    inline constexpr int kControlInterval = 32;

    /** The band split used by every stereo-affecting stage.

        165 Hz sits deliberately above the 130 Hz at which the synth already
        collapses to mono, so the band Memory is forbidden to touch is strictly
        wider than the band the source made mono.  2.6 kHz separates "body" from
        "air" for the harmonic colouration and the decorrelation weighting. */
    inline constexpr float kLowSplitHz  = 165.0f;
    inline constexpr float kHighSplitHz = 2600.0f;

    // =======================================================================
    //  CopyCharacter - the description of one pass through the medium
    // =======================================================================
    struct CopyCharacter
    {
        // 1  bandwidth
        float hfCorner;          ///< Hz the top is pulled down to at full depth
        float lfCorner;          ///< Hz the bottom is pulled up to at full depth
        float bandwidthWander;   ///< 0..1, how much that corner moves over time

        // 2  transient softness
        float transient;         ///< 0..1 depth of the attack-only gain dip

        // 3  saturation
        float satDrive;          ///< added to a drive of 1 at full depth
        float satBias;           ///< signed asymmetry of the curve

        // 4  resampling
        float resampleDivisor;   ///< effective rate divisor at full depth, at 48 kHz
        float resamplePreFilter; ///< 1 clean decimation .. 0 fully aliased
        float resampleHold;      ///< 0 linear reconstruction .. 1 zero-order hold

        // 5  pitch instability
        float wobbleCents;       ///< peak deviation of this copy at full depth
        float wobbleHz;          ///< base drift rate

        // 6  phase diffusion
        float diffusion;         ///< 0..1 scaling of the common allpass chain

        // 7  stereo coherence
        float decorrelation;     ///< 0..1, mid and high only

        // 8  harmonic colouration
        float colourEven;        ///< second-harmonic weight
        float colourOdd;         ///< third-harmonic weight

        // 9  spectral aging
        float tilt;              ///< -1 dark .. +1 bright, fed to fx::Tilt

        // 10 HF absorption
        float absorption;        ///< 0..1 level-driven loss of top

        // 12 noise interaction
        float noise;             ///< linear amplitude of the floor at full depth
        float noiseTilt;         ///< -1 rumble .. +1 hiss

        // generation IV behaviour, absent from the earlier passes
        float dropout;           ///< 0..1 likelihood and depth of one-channel drops
        float hole;              ///< 0..1 depth of the wandering spectral hole
    };

    /** The primary lineage: a tape-to-tape chain that gets darker and dirtier. */
    const CopyCharacter& primaryCopy (int index) noexcept;

    /** The alternate lineage: a sampler chain that stays brighter and gets
        grainier instead.  Reached through `macros.alterAmount`. */
    const CopyCharacter& alternateCopy (int index) noexcept;

    // =======================================================================
    //  Per-block settings
    // =======================================================================
    struct StageSettings
    {
        float depth      = 0.0f;   ///< 0..1, macro_memory after the macro additions
        float bandwidth  = 0.5f;   ///< memory_bandwidth
        float wobble     = 0.35f;  ///< memory_wobble
        float diffusion  = 0.3f;   ///< memory_diffusion
        float asymmetry  = 0.25f;  ///< memory_asymmetry
        float age        = 0.0f;   ///< macros.age
        float movement   = 0.0f;   ///< macros.movement
        float alter      = 0.0f;   ///< macros.alterAmount
    };

    /** The per-sample modulation a stage is allowed to read.  Both pointers may
        be null; the depths are already multiplied by their master amounts. */
    struct StageMod
    {
        const float* pulse  = nullptr;   ///< 0..1, 1 = fully ducked
        float        pulseDepth = 0.0f;  ///< macros.pulseToMemory
        const float* breath = nullptr;   ///< -1..1
        float        breathDepth = 0.0f;
    };

    // =======================================================================
    //  Follower
    //
    //  An envelope follower with separate rise and fall times.  DspCommon's
    //  OnePole is symmetrical, and every detector in this engine - transient,
    //  absorption, noise ducking, dropout recovery - needs the two times to
    //  differ, so this one lives here rather than being pushed upstream.
    // =======================================================================
    struct Follower
    {
        float z = 0.0f, aUp = 0.0f, aDown = 0.0f;

        void prepare (float riseSeconds, float fallSeconds, double rate) noexcept
        {
            const double r = juce::jmax (1.0, rate);

            aUp   = (riseSeconds <= 0.0f) ? 0.0f
                  : std::exp (-1.0f / (float) (riseSeconds * r));
            aDown = (fallSeconds <= 0.0f) ? 0.0f
                  : std::exp (-1.0f / (float) (fallSeconds * r));
        }

        void reset() noexcept { z = 0.0f; }
        void setValue (float v) noexcept { z = v; }

        forcedinline float process (float x) noexcept
        {
            const float a = (x > z) ? aUp : aDown;
            z = x + a * (z - x);
            return (z = fx::flush (z));
        }
    };

    // =======================================================================
    //  CopyStage - one complete pass through the medium
    // =======================================================================
    class CopyStage
    {
    public:
        CopyStage() = default;

        /** Allocates.  Message thread only.  `copyIndex` is 0..3 and selects
            both the character table entry and the deterministic seed, so the
            same copy always behaves identically on every machine. */
        void prepare (double sampleRate, int copyIndex);

        void reset() noexcept;

        /** Constant after prepare().  The onset delay this copy adds: the
            wobble delay's base, the diffusion chain's shortest path, the
            mid-band alignment delay and the resampler's one sample. */
        int latencySamples() const noexcept { return latency; }

        /** Once per block, before process(). */
        void setSettings (const StageSettings&) noexcept;

        /** In place on two channels of `numSamples`.  Realtime safe. */
        void process (float* left, float* right, int numSamples, const StageMod&) noexcept;

    private:
        struct Channel
        {
            // 5  pitch instability, common component (identical read on both
            //    channels, so the low end cannot decorrelate here)
            fx::DelayLine wobbleDelay;

            // 1  bandwidth, 10 HF absorption
            fx::OnePoleTPT lowCut, hfA, hfB, absorb;

            // 3  saturation
            fx::DcBlocker dc;

            // 6  phase diffusion: identical coefficients on both channels, so
            //    this is one LTI filter applied twice and cannot change the
            //    L/R correlation at any frequency
            fx::Allpass diffuse[3];

            // band work
            fx::ThreeBand  split;
            fx::DelayLine  lowAlign;    ///< keeps the low band level with the mid path
            fx::DelayLine  midDelay;    ///< 5b differential drift, mid and high only
            fx::Allpass    decor[2];    ///< 7 stereo decorrelation, mid and high only
            fx::OnePole    colourEnv;   ///< 8 normalises the harmonic ratio
            fx::OnePoleTPT holeLo, holeHi;   ///< generation IV spectral hole
            fx::Tilt       tilt;        ///< 9 spectral aging

            // 12 noise, the part that differs between channels
            fx::Rng        noiseRng;
            fx::OnePoleTPT noiseColour, noiseGuard;

            // 4  resampling: the phase is shared, the held samples are not
            fx::OnePoleTPT resPre;
            float resPrev = 0.0f, resCur = 0.0f;

            // per-channel drift and dropout state
            float wobblePhase = 0.0f, wobbleRate = 0.0f, startPhase = 0.0f;
            float dropGain = 1.0f, dropTarget = 1.0f;
            int   dropTicks = 0;

            // control-rate coefficients
            float satDrive = 1.0f, satBias = 0.0f, satOffset = 0.0f, satScale = 1.0f;
            float noiseGain = 0.0f, asymGain = 1.0f, midDelaySamples = 0.0f;

            float decorDelay[2] { 7.0f, 11.0f };
        };

        void updateControl (const StageSettings&, float pulse, float breath) noexcept;

        double sr = 48000.0;
        int    index = 0;
        int    latency = 0;

        CopyCharacter blended {};

        Channel channel[2];

        // shared detectors: linked across the channels on purpose, so that
        // nothing here can move the stereo image
        Follower fastEnv, slowEnv, transientGain, absorbEnv, duckEnv, activityEnv;

        // shared drift and wander oscillators.  Their starting phases are drawn
        // once in prepare() from a seed derived from the copy index and are
        // restored by reset(), so a render always recalls identically.
        float commonPhase = 0.0f, commonPhase2 = 0.0f;
        float wanderPhase = 0.0f;
        float holePhase   = 0.0f;
        float seedPhase[4] { 0.0f, 0.0f, 0.0f, 0.0f };
        float driftRate = 1.0f;          ///< per-copy multiplier on the drift rate

        // shared resampler phase, so decimation can never decorrelate anything
        float resAcc = 0.0f, resDivisor = 1.0f, resIncrement = 1.0f, resPreMix = 1.0f;

        // the low-end guarantee: a 12 dB/oct high pass on the side signal only,
        // which makes "the low end stays mono" true by construction rather than
        // by hoping the band split was steep enough
        fx::OnePoleTPT sideHpA, sideHpB;

        // 12 noise, the part both channels share
        fx::Rng        noiseRngCommon;
        fx::OnePoleTPT noiseColourCommon;
        float noiseLpNorm = 1.0f, noiseHpNorm = 1.0f;
        float noiseHpBright = 1.0f, noiseLpDark = 0.0f;

        // smoothed block settings
        fx::OnePole sDepth, sBandwidth, sWobble, sDiffusion, sAsym, sAge, sMovement, sAlter;

        // control-rate values shared by both channels
        float wobbleSamples = 0.0f;
        float tAmount = 0.0f;
        float tGain = 1.0f;
        float tiltAmount = 0.0f;
        float holdBlend = 0.0f;
        float colourEvenGain = 0.0f, colourOddGain = 0.0f;
        float holeAmount = 0.0f;
        float sideMix = 0.0f;
        float noiseCommonGain = 0.0f;

        // constants derived in prepare()
        float wobbleBase = 48.0f, wobbleMaxDepth = 40.0f;
        float midBase = 32.0f, midMaxDepth = 24.0f;
        float rateScale = 1.0f;          ///< sampleRate / 48000, for the decimator
        float controlRate = 1500.0f;     ///< sampleRate / kControlInterval

        StageSettings pending {};
        bool primed = false;             ///< false until the smoothers have been snapped
    };
}
