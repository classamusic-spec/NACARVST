#pragma once

#include "../Audio/DspCommon.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

/**
    THE OPERATION LIBRARY.

    Every transformation the mutation engine can apply to a sample lives here,
    as a free function over a `juce::AudioBuffer<float>`.  Nothing in this file
    knows what an intent is: `MutationEngine` chooses which of these to run, in
    what order and how hard, and this file only knows how to do them.

    THREE RULES, and they are what the engine's guarantees are built on.

    1.  OFFLINE.  These allocate, they read the whole buffer more than once,
        and some of them are quadratic in a search window.  Nothing here may be
        called from `processBlock`.  That is the whole reason the mutation
        engine is not an audio engine.

    2.  DETERMINISTIC.  No function here reads a clock, an address or a global.
        Randomness arrives as an `fx::Rng` the caller seeded from the recipe,
        and a function given the same buffer, the same settings and the same
        RNG state produces the same samples every time.

    3.  CHANNEL-SYMMETRIC UNLESS TOLD OTHERWISE.  Every per-channel decision is
        taken once and applied to all channels unless a setting explicitly says
        the stereo image may move (`spread`, `stereoFree`, `widen`).  That is
        what makes `Preserve::stereo` demonstrable rather than approximate: with
        the stereo-moving settings at zero, a source whose channels are
        identical comes out with its channels still identical, sample for
        sample.
*/
namespace nacar::mutation::ops
{
    using Buffer = juce::AudioBuffer<float>;
    using Rng    = fx::Rng;

    /** Derives one independent RNG stream from the recipe's seed and a salt.
        Every RNG in the engine is born here; none is seeded from the clock, an
        address or a counter that survives a render. */
    juce::uint32 streamSeed (juce::uint32 seed, juce::uint32 salt) noexcept;

    // =======================================================================
    //  Measurement and hygiene
    // =======================================================================

    float peakOf (const Buffer&) noexcept;
    float rmsOf  (const Buffer&) noexcept;
    bool  isFinite (const Buffer&) noexcept;

    /** Replaces every non-finite sample with silence.  Returns how many it
        found, so a caller can say so rather than hide it. */
    int   sanitise (Buffer&) noexcept;

    void  applyGain (Buffer&, float) noexcept;
    void  mixInto (Buffer& destination, const Buffer& source, float gain) noexcept;

    /** Trims or zero-pads to exactly this many samples. */
    void  setLengthExactly (Buffer&, int samples);

    /** Equal-power fade at both ends, so no operation can leave a click at the
        edge of the file. */
    void  fadeEdges (Buffer&, float milliseconds, double rate);

    // =======================================================================
    //  Zero-phase band splitting
    //
    //  A cascade of one-pole lowpasses run forwards and then backwards.  The
    //  backward pass cancels the forward pass's phase exactly, so the band
    //  that comes out is not delayed relative to the input - which is the only
    //  reason the low-end lock can be stated as "the result's low band is the
    //  source's low band" rather than "something like it, a few samples late".
    //
    //  Offline only: it needs the whole buffer, twice.
    // =======================================================================

    void lowBandInto (const Buffer& in, Buffer& out, float hz, double rate, int poles);
    void removeLowBand (Buffer&, float hz, double rate, int poles);

    /**
        REPLACES EVERYTHING BELOW `hz` IN `target` WITH THE SAME BAND OF
        `source`, and this is what makes the low-end lock demonstrable.

        It is done on the whole buffer's spectrum, not with a filter: below
        `hz * 0.8` the result's Fourier coefficients ARE the source's, bin for
        bin, so a measurement at any frequency down there returns the source's
        own magnitude and phase rather than something close to it.  Between
        `hz * 0.8` and `hz * 1.25` the two are blended with a raised cosine, so
        there is no step to ring.

        A filter cannot make that promise.  A one-pole cascade at 260 Hz still
        passes about 3% of the mutated signal at 80 Hz, and when the mutation
        is loud down there and the source is not, 3% is a decibel or more - the
        leak is set by where the corner is, not by how many poles are behind
        it.  Measured, not assumed: that is exactly how the first version of
        this failed its own test.

        Both buffers must be the same length.  Returns false and does nothing
        for a buffer too long to transform (see `kMaxSpliceOrder`), which is
        the caller's cue to say so rather than pretend.
    */
    bool spliceLowBand (Buffer& target, const Buffer& source, float hz, double rate);

    /** 2^22 samples: 95 seconds at 44.1 kHz, and 32 MB of transform per
        channel.  Beyond this the engine says the band could not be held rather
        than allocating half a gigabyte behind the user's back. */
    inline constexpr int kMaxSpliceOrder = 22;

    /** Zero-phase high pass at a few Hz.  Removes offset without touching the
        bottom octave the low-end lock protects. */
    void removeDc (Buffer&, double rate);

    // =======================================================================
    //  Time and pitch
    // =======================================================================

    /** Resamples by `ratio`: 2.0 is an octave up and half as long. Hermite. */
    void resampleBy (const Buffer& in, Buffer& out, double ratio);

    /** SOLA time stretch.  `factor` is the output length over the input
        length, so 2.0 is twice as long at the same pitch.  Alignment is
        searched on the mono sum and applied to every channel, so a stretch
        cannot decorrelate a stereo pair. */
    void timeStretch (const Buffer& in, Buffer& out, double factor, double rate);

    /** Stretch, then resample: the pitch moves and the length does not. */
    void pitchShiftSemitones (const Buffer& in, Buffer& out, double semitones, double rate);

    // =======================================================================
    //  Granular reconstruction
    // =======================================================================

    struct GrainSettings
    {
        float grainMs            = 90.0f;
        float overlap            = 4.0f;    ///< grains alive at once
        float sizeJitter         = 0.3f;    ///< 0..1 of the grain length
        float scatterMs          = 40.0f;   ///< how far a grain may wander in time
        float reverseProbability = 0.0f;
        float spread             = 0.0f;    ///< 0 keeps every grain centred; 1 scatters them
        float pitchProbability   = 0.0f;    ///< chance a grain takes a pitch other than 0
        std::vector<int> pitches { 0 };     ///< semitones, ALREADY snapped by harmony::Context
        int   outputLength       = 0;       ///< 0 means "the same as the input"
    };

    void granulate (const Buffer& in, Buffer& out, const GrainSettings&, Rng&, double rate);

    // =======================================================================
    //  Spectral
    //
    //  One STFT, hann in and hann out at 75% overlap, magnitudes modified and
    //  the source's own phases kept.  Keeping the phases is what stops these
    //  two from sounding like a vocoder: they thin and smear the spectrum
    //  without re-synthesising it.
    // =======================================================================

    /** Smears each bin's magnitude across time.  `amount` 0..1 is how much of
        the previous frame survives into this one. */
    void spectralBlur (Buffer&, float amount, double rate);

    /** Keeps the loudest `keepFraction` of the spectrum in each frame and
        attenuates the rest by `depth`.  What is left is the skeleton of the
        sound: the partials that were carrying it, and nothing else. */
    void spectralGate (Buffer&, float depth, float keepFraction, double rate);

    // =======================================================================
    //  Slicing, against the transient grid where there is one
    // =======================================================================

    /** Builds a slice grid.  Transients first; failing that the tempo; failing
        that a fixed division, because a sample with no detected onsets and no
        tempo still has to be sliceable. `timeScale` maps the analysis's own
        sample positions onto the buffer as it is now. */
    std::vector<int> gridFrom (const std::vector<int>& transients, int length,
                               double rate, double tempo, double timeScale,
                               float minSliceMs);

    struct SliceSettings
    {
        float shuffle     = 0.5f;   ///< chance a slot plays a slice other than its own
        float repeat      = 0.0f;   ///< chance a slot repeats the previous slice
        float reverse     = 0.0f;   ///< chance a slice plays backwards
        float drop        = 0.0f;   ///< chance a slot is silent
        float crossfadeMs = 4.0f;
    };

    /** Re-orders slices in place.  The slot boundaries are the source's own, so
        the total length never changes: what moves is which slice is in which
        slot. */
    void sliceShuffle (Buffer&, const std::vector<int>& grid, const SliceSettings&,
                       Rng&, double rate);

    void reverseWhole (Buffer&);
    void reverseSlices (Buffer&, const std::vector<int>& grid, float probability,
                        Rng&, double rate);

    // =======================================================================
    //  Space
    // =======================================================================

    struct DiffuseSettings
    {
        float sizeMs       = 60.0f;
        float amount       = 0.6f;    ///< how much of the output is diffused
        float damping      = 0.5f;    ///< 0 bright tail, 1 dark tail
        float predelayMs   = 0.0f;
        float decaySeconds = 1.2f;
        bool  stereoFree   = true;    ///< false: both channels get identical delays
    };

    /** Allpass diffusion into a small damped feedback network.  This is the
        "it happened somewhere else" operation, and the only one in the file
        that adds energy after the source has stopped. */
    void diffuse (Buffer&, const DiffuseSettings&, Rng&, double rate);

    /** Appends `seconds` of silence so a tail has somewhere to go.  Callers
        that are holding the length lock do not call this. */
    void appendSilence (Buffer&, double seconds, double rate);

    /** Prepends `seconds` of silence, for a swell that arrives before the
        onset it belongs to. */
    void prependSilence (Buffer&, double seconds, double rate);

    // =======================================================================
    //  Degradation
    // =======================================================================

    struct DegradeSettings
    {
        float bandwidth  = 0.5f;   ///< 0 untouched, 1 telephone
        float decimation = 0.3f;   ///< sample-rate reduction with imperfect reconstruction
        float bits       = 0.0f;   ///< 0 none, 1 four bits
        float noise      = 0.2f;   ///< the floor a copy leaves behind
        float saturation = 0.3f;
        float driftCents = 6.0f;   ///< slow pitch instability
        float tiltDark   = 0.3f;
        bool  stereoFree = true;
    };

    /** A copy of a copy.  This is NOT `MemoryEngine` - that engine is realtime,
        parameter-driven and twelve-dimensional, and none of it can be called
        from here.  This is the same idea at a tenth of the size, and the
        README says so. */
    void degrade (Buffer&, const DegradeSettings&, Rng&, double rate);

    // =======================================================================
    //  Dynamics and rhythm
    // =======================================================================

    /** Fast envelope against slow.  `attack` below zero rounds the attacks off,
        above zero sharpens them; `sustain` lengthens or shortens what follows.
        The detector is the mono sum and the gain goes to every channel, so this
        cannot move the image. */
    void transientShape (Buffer&, float attack, float sustain, double rate);

    /** Gates against the grid.  `depth` is how far down the closed steps go. */
    void rhythmicGate (Buffer&, const std::vector<int>& grid, float depth,
                       float dutyCycle, Rng&, double rate);

    /** Short holes, as a failing medium makes them. */
    void dropouts (Buffer&, float density, float depth, Rng&, double rate);

    /** Repeats a short piece of a slice several times in place. */
    void stutter (Buffer&, const std::vector<int>& grid, float amount, Rng&, double rate);

    /** A lowpass whose corner moves on a deterministic oscillator. */
    void filterSweep (Buffer&, float depth, float lfoHz, float centreHz, double rate);

    /** Tilt: negative darkens, positive brightens, level-neutral by
        construction.  `fx::Tilt`, applied with one shared coefficient so it
        cannot move the stereo image. */
    void tilt (Buffer&, float amount, double rate);

    /** Cascaded one-pole low and high passes.  Causal, because a mutation is
        allowed to smear time - the zero-phase pair above exists for the
        low-end lock, where a delayed band would break the promise. */
    void lowPass (Buffer&, float hz, double rate, int poles);
    void highPass (Buffer&, float hz, double rate, int poles);

    /** Resonant bandpasses at the given frequencies, mixed back in.  The
        frequencies come from `harmony::Context`; this function does not choose
        them and does not know what a scale is. */
    void harmonicReinforce (Buffer&, const std::vector<float>& frequencies,
                            float amount, double rate);

    /** Mid/side width.  Refuses to run on anything but a stereo pair. */
    void widen (Buffer&, float amount, double rate);

    /** A rising envelope over the whole buffer.  `curve` above 1 holds it back
        longer before it arrives. */
    void applySwell (Buffer&, float curve);

    /** Multiplies by a falling exponential whose -60 dB point is `seconds`. */
    void applyDecay (Buffer&, float seconds, double rate);

    /** Picks the most stable window in the buffer: the one whose short-term
        energy and spectral flux move least.  Returns the start sample. */
    int  mostStableWindow (const Buffer&, int windowSamples, double rate);

    /** Crossfade-loops `windowSamples` from `start` until `out` is full. */
    void loopRegion (const Buffer& in, Buffer& out, int start, int windowSamples,
                     float crossfadeMs, double rate);
}
