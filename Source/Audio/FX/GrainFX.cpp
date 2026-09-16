#include "GrainFX.h"

#include <algorithm>
#include <initializer_list>

/*
    ======================================================================
    GRAIN
    ======================================================================

    Specification section 89, slot five of the signature path in section 91,
    with the pitch behaviour from sections 108 and 119.

    ----------------------------------------------------------------------
    THE ALGORITHM
    ----------------------------------------------------------------------

    One stereo circular history is written with the input every sample,
    unconditionally, for the same reason Rewind does it: a granulator that only
    starts remembering when you switch it on has nothing to granulate for its
    first half second.

    A scheduler fires a grain every `sampleRate / DENSITY` samples, with JITTER
    applied to the interval.  Each grain is taken from a fixed pool of 96 and
    is completely described at the instant it is spawned - start position,
    signed increment, length, window shape, left and right gain.  Nothing about
    a sounding grain is ever revisited, which is what makes it safe to change
    any parameter at any time: the change reaches the *next* grain, and the
    ones already in flight finish the way they started.

    A grain's read span is computed at spawn and clamped so that it fits
    entirely inside the buffer without crossing the write head:

        forward  (increment > 0)  offset >= length * increment + 8
        backward (increment < 0)  offset <= usable - length * |increment| - 8

    A forward grain that started too close to the write head would overtake it
    and read the oldest material in the ring - a hard discontinuity in the
    middle of a grain.  If the requested span cannot fit at all, the grain is
    dropped.

    INTEGRATION NOTE.  Grain must be given every block, including blocks in
    which grainfx_on is false or the slot is bypassed, or its history goes
    stale.  The engine bypasses itself internally and is bit-exact when idle,
    so calling it unconditionally costs one circular write per sample.

    ----------------------------------------------------------------------
    POOL EXHAUSTION
    ----------------------------------------------------------------------

    The pool is 96, against a worst case of DENSITY 80/s times SIZE 0.5 s = 40
    simultaneous grains, plus headroom for jitter clustering and for the
    density that the Motion macro can add.  When it is exhausted the new grain
    is dropped.  Stealing a sounding grain would truncate a window part way
    through its envelope, which is a click; dropping one is silence that was
    never scheduled.

    ----------------------------------------------------------------------
    WINDOWS
    ----------------------------------------------------------------------

    Five 1024-point tables plus a guard point, computed once in prepare() and
    read with linear interpolation.  Evaluating a window function per sample
    per grain would be forty evaluations of exp() per sample at full density.

        HANN        raised cosine.  The default, and the one whose overlap-add
                    is flattest.
        TUKEY       a 25 % cosine taper at each end and a flat middle, so a
                    grain keeps more of the material's own envelope.
        GAUSS       sigma = 0.16, shifted and rescaled so that it reaches
                    exactly zero at both ends.  An unshifted Gaussian is about
                    0.0001 at the edges, and a grain that starts at a non-zero
                    value starts with a step.
        EXPO        a 4 % attack and an exponential decay: a plucked grain.
        PERCUSSIVE  a 1.5 % attack and a much steeper decay.  This is what lets
                    a granulator make rhythm rather than only clouds - at a
                    500 ms grain the attack is 7.5 ms and reads as a soft
                    transient, at a 5 ms grain it is 75 microseconds and reads
                    as a tick.  That is the point of it.

    Every table is forced to exactly zero at both endpoints; anything else is
    a click per grain.  Each table's mean and RMS are measured at the same time
    and used by the gain law below.

    ----------------------------------------------------------------------
    PITCH - THE WEIGHTED DISTRIBUTION
    ----------------------------------------------------------------------

    Section 119 is explicit that the choice is weighted, never uniform:

        SAFE grain pitch: very common 0 - common +-12 - moderate fifth -
        contextual chord tones - rare other scale tones

    buildPitchTable() writes that out literally over the 25 semitones from -12
    to +12.  Each candidate gets a base weight from what it *is*:

        0 semitones              100      very common
        +12 / -12                 30 / 26 common
        +7  / -7                  13 /  9 moderate
        other chord tones          6 /  4 contextual
        other scale tones        1.6 / 1.2 rare
        non-scale semitones      0.5      (FREE tier only)

    Downward intervals are weighted slightly below their upward twins because
    transposing granular material down thickens the low mids faster than
    transposing it up thins them.

    Three things then multiply those weights:

      grain_pitch_mode   selects which candidates are allowed at all.
                         ROOT {0}; OCTAVE {0, +-12}; FIFTH {0, +-7, +-12};
                         THIRD {0, +-third, +-12} with the third taken from
                         the scale, so it is a minor third in a minor scale;
                         SCALE every scale tone; CHROMATIC all 25; FREE is not
                         quantised at all and bypasses the table entirely,
                         using grain_pitch plus up to +-50 cents of microtonal
                         spread (section 108's FREE tier).

      harmony_mode       caps how far the draw may stray, matching section
                         108's SAFE / COLOR / FREE tiers.  Each candidate
                         carries a tier - safe (unison, octave, fifth, chord
                         tones), colour (other scale tones), free (non-scale) -
                         and a candidate above the allowed tier is multiplied
                         by alterAmount^2 rather than by zero, so Alter can
                         open the door without the control being a switch.

      grain_scatter      multiplies every non-unison weight by scatter^1.5.
                         At scatter zero the distribution collapses onto 0
                         semitones and Grain becomes a straight granular
                         re-reading of the material; at scatter one the whole
                         allowed set is in play.  That is what makes SCATTER
                         the module's primary control rather than a fourth
                         kind of depth.

    The draw itself is a cumulative-weight search over 25 entries with one
    uniform deviate from the engine's deterministic Rng.

    AUTO.  root_note and scale_type both have AUTO at index 0 and there is no
    analysis engine yet, so AUTO is resolved here as C and as MINOR - the
    major/minor ambiguity resolved to minor, because a minor third drawn over
    major material is a colour and a major third drawn over minor material is
    a mistake.

    root_note is read and then deliberately not used, and that is worth being
    explicit about: a grain's transposition is *relative* to whatever the
    material already is, so the absolute pitch class of the tonic cannot change
    which intervals are in key - only the scale's interval pattern can.  The
    parameter is read here so that the AUTO resolution lives in one place for
    the version that has pitch tracking.

    The honest limitation is the same one: a fixed transposition cannot be
    diatonic for every note it is applied to.  Transposing by four semitones
    maps a major root to its third, but maps the second degree to a raised
    fourth.  The sets above are chosen so that material centred on the tonic
    lands on chord tones, and the weighting keeps the ambiguous intervals rare.
    Real key-aware quantisation needs per-grain pitch detection.

    ----------------------------------------------------------------------
    GAIN STAGING
    ----------------------------------------------------------------------

    Section 148: do not use loudness to fake quality.  Density must mean
    density, not volume.  Overlapping grains sum, so the expected overlap is

        overlap = DENSITY * SIZE          (grains sounding at once)

    and the engine divides by what that overlap is expected to produce.  The
    two limits are the easy part and they disagree: N identical grains sum
    coherently to N * windowMean, and N independent grains sum in power to
    sqrt(N) * windowRms.  This is exactly the problem the unison normalisation
    in the synth core solves, so it is solved the same way:

        gain = 1 / ( overlap^(0.5 + 0.5c) * windowRms^(1-c) * windowMean^c )

    with c the coherence of the grain cloud, 0 independent to 1 identical.
    Here c is not guessed - it falls out of the engine's own state:

        c = P(0 semitones) * (1 - jitter) * directionCoherence

    P(0 semitones) is the unison share of the pitch distribution that was just
    built, so a ROOT-mode cloud at low jitter is correctly treated as coherent
    and a CHROMATIC one is not.  directionCoherence is 1 when every grain runs
    the same way and 0 at an even forward/backward split.

    overlap is clamped to at least 1: below one grain at a time there is
    nothing to normalise, and raising a fractional overlap to a negative power
    would boost sparse grains instead of leaving them alone.

    ----------------------------------------------------------------------
    FREEZE
    ----------------------------------------------------------------------

    FREEZE must hold one slice indefinitely and must not click on either edge.
    Three approaches were considered:

      * stop writing the history.  The slice survives, but resuming leaves a
        content seam at the write head that any grain spanning it will click
        on.
      * write the slice back into itself in a loop.  Self-sustaining, but the
        loop seam gets baked into the buffer and accumulates.
      * copy the slice somewhere else and read grains from there.

    The third is the only one with no seam at all, because a grain never
    changes source mid-flight: engaging or releasing FREEZE changes only which
    buffer the *next* grain reads, and every grain is windowed from silence to
    silence, so the switch is inaudible by construction.

    The copy is 2.5 seconds of stereo, made incrementally at 32 samples of
    store per sample of audio, so it is bounded work per block - about
    2 x 32 x blockSize floats - and no allocation.  It completes in
    2.5 / 32 = 78 ms, which is the latency between pressing FREEZE and the
    freeze taking effect, and is far inside the 2.9 s it would take the live
    write head to reach the region being copied.  Until it completes, grains
    keep coming from the live history, so nothing stops.

    While frozen, POSITION scans within the frozen slice instead of scanning
    backwards from the write head, which makes it a scrub control over the
    held material.

    ----------------------------------------------------------------------
    FEEDBACK
    ----------------------------------------------------------------------

    grain_feedback feeds the grain output back into the history, and a granular
    feedback loop with pitch shifting is an excellent runaway generator: every
    upward-transposed pass adds energy at the top and every pass re-windows
    material that was already windowed.  Four things bound it, and all four are
    needed:

      1. the parameter maxes at 0.95, and the engine takes a further 0.95
         factor, so the nominal loop gain is at most 0.9025.
      2. a one-pole low pass at 6.5 kHz inside the loop.  Upward transposition
         moves energy up; without a spectral leak the loop converges on a
         whistle at Nyquist.
      3. a DC blocker inside the loop.  Window asymmetry - PERCUSSIVE and EXPO
         are strongly asymmetric - rectifies slightly, and DC in a feedback
         loop integrates.
      4. a peak-following limiter on the fed-back signal, 5 ms attack and
         400 ms release, holding it at or below 1.0 before the feedback gain is
         applied.  A soft clip alone would let the loop sit permanently against
         the clipper; the limiter lets it come back down.

    The feedback path is one sample delayed, which it has to be: the history is
    written before the grains are read, so the grain output available when
    sample i is written is the one from sample i-1.

    ----------------------------------------------------------------------
    THE LOW END
    ----------------------------------------------------------------------

    Sections 38, 40 and 43, and NacarBench measures it on every patch.  Grain's
    SPREAD pans individual grains, which decorrelates whatever is in them,
    including the bass.  So the summed grain output is split with fx::ThreeBand
    (120 Hz / 3 kHz, complementary TPT one-poles) and the low band is collapsed
    to mono before the three bands are summed back.  Spread, pan and width
    therefore act on the mid and high bands only, and the low band's L/R
    correlation out of Grain is 1.0 by construction whatever SPREAD says.

    ----------------------------------------------------------------------
    MACRO RESPONSE  (added to the parameters, never replacing them)
    ----------------------------------------------------------------------

      macros.movement     += 0.35 * movement on JITTER, and scales the Breath
                          position drift below.  Section 72 names Grain
                          explicitly as a Motion destination.
      macros.scale        += 0.30 * scale on SPREAD.
      macros.distance     += 0.15 * distance on SPREAD.  World makes a grain
                          cloud wider and further away, which is the same
                          gesture from two directions.
      macros.widthScale   multiplies every grain's pan position.  The low band
                          is mono regardless, so this cannot reach the bass.
      macros.alterAmount  opens the harmony tier above what harmony_mode allows
                          (weighted by alterAmount^2, so it is a fade and not a
                          switch), and gives each grain a 0.6 * alter chance of
                          using the alternate window - PERCUSSIVE when the
                          patch asks for anything else, HANN when it asks for
                          PERCUSSIVE.  Both are section 89's "advanced"
                          controls being reached from the macro layer rather
                          than being overridden.
      macros.breathAt(i)  drifts the read position by up to +-8 % of the
                          history, scaled by movement.  Breath is organic and
                          non-repeating and the read position is a slow
                          parameter, which is the only kind it is allowed near.

    grainfx_on is read here as well as by the chain, so that the history keeps
    being written while the module is switched off.

    Deliberately unused: age and grit (Grain is not a wear effect - Retro,
    Crush and Patina are), wetBias (World's offset belongs on atmospheric
    mixes, and Grain is not one), the Pulse destinations (Pulse's width and
    volume ducks are applied by the chain's output stage, per the note in
    EngineContext.h, and a Pulse-triggered grain burst would duplicate what
    Rewind already does with it), memoryGeneration, weightMode and the LFOs.

    ----------------------------------------------------------------------
    MEMORY BUDGET
    ----------------------------------------------------------------------

    4 seconds of history requested, which fx::HistoryBuffer rounds up to a
    power of two: 5.46 s at 96 kHz (4.2 MB), 5.46 s at 48 kHz (2.1 MB), 5.94 s
    at 44.1 kHz (2.1 MB).  Plus a 2.5 s freeze store (1.9 MB at 96 kHz,
    0.96 MB at 48 kHz) and 5 KB of window tables.  Worst case, 96 kHz: 6.1 MB.

    ----------------------------------------------------------------------
    KNOWN LIMITATIONS
    ----------------------------------------------------------------------

      * Nobody has listened to this.
      * Grain reads are linearly interpolated, via fx::HistoryBuffer::readAt.
        At transpositions above about +12 semitones that is audible as a dull
        top end.  Hermite would cost one more multiply-add per sample per
        grain and is the obvious upgrade; the delay line in DspCommon.h
        already has it, the history buffer does not.
      * Transposition is resampling, so it changes grain duration in the
        source material but not in the output - the window length is fixed in
        output samples.  That is the normal granular convention but it does
        mean SIZE and PITCH are not independent in what they read.
      * FREEZE takes 78 ms to engage, for the reason given above.  Releasing
        it is immediate.
      * The pitch quantisation is relative, not key-aware.  See the note under
        PITCH.
      * The scheduler is not sample-accurate across a block boundary in one
        respect: a grain always starts on a sample boundary, so at very high
        densities the timing quantises to 1 sample.  That is inaudible at any
        density this module offers.
      * The gain law assumes every scheduled grain sounds.  Grains that are
        dropped - an exhausted pool, or a span that will not fit in the buffer
        at an extreme of SIZE and PITCH, which the 2.5 s freeze store reaches
        sooner than the 5.5 s history does - make the texture quieter than the
        normalisation expects.  With a pool of 96 against a worst case of 40
        that is rare, but it is not impossible.
      * The 2x clamp on the normalisation gain binds for PERCUSSIVE and EXPO at
        low density, as described under GAIN STAGING.
      * There is no grain-level envelope follower, so FEEDBACK at maximum with
        a very short SIZE will sound compressed rather than exploding - the
        limiter is doing exactly its job, but it is doing it audibly.
      * CPU is proportional to the number of sounding grains, which is
        DENSITY x SIZE.  At the top of both controls that is 40 grains per
        sample, and nothing here is SIMD.
*/

namespace nacar
{
    namespace
    {
        constexpr double kHistorySeconds = 4.0;
        constexpr double kFreezeSeconds  = 2.5;
        constexpr int    kFreezeCopyRate = 32;    ///< store samples per audio sample

        constexpr float kLowSplitHz  = 120.0f;
        constexpr float kHighSplitHz = 3000.0f;
        constexpr float kFeedbackLpHz = 6500.0f;
        constexpr float kFeedbackCeiling = 1.0f;
        constexpr float kFeedbackTrim = 0.95f;    ///< on top of the 0.95 parameter max

        constexpr float kMinGrainMs = 5.0f;
        constexpr float kMaxGrainMs = 500.0f;
        constexpr float kMaxDensity = 80.0f;

        /** Scale interval masks, in the order of the scale_type choice list.
            Bit n set means "n semitones above the tonic is in the scale".
            Index 0 is AUTO, resolved to MINOR - see the note above. */
        constexpr int scaleMask (std::initializer_list<int> degrees)
        {
            int m = 0;
            for (int d : degrees) m |= (1 << d);
            return m;
        }

        constexpr int kMajor        = scaleMask ({ 0, 2, 4, 5, 7, 9, 11 });
        constexpr int kMinor        = scaleMask ({ 0, 2, 3, 5, 7, 8, 10 });
        constexpr int kDorian       = scaleMask ({ 0, 2, 3, 5, 7, 9, 10 });
        constexpr int kPhrygian     = scaleMask ({ 0, 1, 3, 5, 7, 8, 10 });
        constexpr int kLydian       = scaleMask ({ 0, 2, 4, 6, 7, 9, 11 });
        constexpr int kMixolydian   = scaleMask ({ 0, 2, 4, 5, 7, 9, 10 });
        constexpr int kHarmonicMin  = scaleMask ({ 0, 2, 3, 5, 7, 8, 11 });
        constexpr int kMelodicMin   = scaleMask ({ 0, 2, 3, 5, 7, 9, 11 });
        constexpr int kChromatic    = 0x0FFF;

        constexpr int kScaleMasks[10] = {
            kMinor,          // AUTO -> minor
            kMajor, kMinor, kDorian, kPhrygian, kLydian,
            kMixolydian, kHarmonicMin, kMelodicMin, kChromatic
        };

        /** The scale degrees that form the tonic seventh chord: 1, 3, 5, 7. */
        inline int chordMask (int mask) noexcept
        {
            int chord = 0;
            int degree = 0;

            for (int s = 0; s < 12; ++s)
            {
                if ((mask & (1 << s)) != 0)
                {
                    if (degree == 0 || degree == 2 || degree == 4 || degree == 6)
                        chord |= (1 << s);

                    ++degree;
                }
            }

            return chord;
        }

        /** The scale's third, in semitones.  Minor scales give 3, major give
            4, and a scale without either falls back to 4. */
        inline int scaleThird (int mask) noexcept
        {
            if ((mask & (1 << 3)) != 0) return 3;
            if ((mask & (1 << 4)) != 0) return 4;
            return 4;
        }

        forcedinline float powApprox (float x, float p) noexcept
        {
            return fx::exp2Fast (p * fx::log2Fast (juce::jmax (1.0e-6f, x)));
        }
    }

    GrainFX::GrainFX() { rng.setSeed (0x4752'414Eu); }     // 'GRAN'
    GrainFX::~GrainFX() = default;

    void GrainFX::buildWindows()
    {
        constexpr int N = kWindowTableSize;
        const float inv = 1.0f / (float) N;

        for (int w = 0; w < kNumWindows; ++w)
        {
            auto& table = windows[(size_t) w];

            for (int i = 0; i <= N; ++i)
            {
                const float t = (float) i * inv;          // 0 .. 1
                float v = 0.0f;

                switch (w)
                {
                    case kHann:
                        v = 0.5f - 0.5f * fx::cosineTurns (t);
                        break;

                    case kTukey:
                    {
                        constexpr float taper = 0.25f;     // each end
                        if (t < taper)
                            v = 0.5f - 0.5f * fx::cosineTurns (0.5f * t / taper);
                        else if (t > 1.0f - taper)
                            v = 0.5f - 0.5f * fx::cosineTurns (0.5f * (1.0f - t) / taper);
                        else
                            v = 1.0f;
                        break;
                    }

                    case kGauss:
                    {
                        // Shifted and rescaled so the ends are exactly zero.
                        constexpr float sigma = 0.16f;
                        const float d = (t - 0.5f) / sigma;
                        const float edge = std::exp (-0.5f * (0.5f / sigma) * (0.5f / sigma));
                        v = (std::exp (-0.5f * d * d) - edge) / juce::jmax (1.0e-6f, 1.0f - edge);
                        v = juce::jmax (0.0f, v);
                        break;
                    }

                    case kExpo:
                    {
                        constexpr float attack = 0.04f;
                        const float a = (t < attack) ? (t / attack) : 1.0f;
                        const float d = std::exp (-5.0f * t) - std::exp (-5.0f);
                        v = a * juce::jmax (0.0f, d) / (1.0f - std::exp (-5.0f));
                        break;
                    }

                    case kPercussive:
                    default:
                    {
                        // A sharp attack and a steep decay: this is the window
                        // that lets a granulator make rhythm.
                        constexpr float attack = 0.015f;
                        const float a = (t < attack) ? (t / attack) : 1.0f;
                        const float d = std::exp (-9.0f * t) - std::exp (-9.0f);
                        v = a * juce::jmax (0.0f, d) / (1.0f - std::exp (-9.0f));
                        break;
                    }
                }

                table[(size_t) i] = juce::jlimit (0.0f, 1.0f, v);
            }

            // Both endpoints exactly zero.  A grain that starts or ends on a
            // non-zero window value starts or ends with a step, and at 80
            // grains per second that is a buzz, not a click.
            table[0] = 0.0f;
            table[(size_t) N] = 0.0f;

            double sum = 0.0, sumSq = 0.0;

            for (int i = 0; i < N; ++i)
            {
                const double v = (double) table[(size_t) i];
                sum += v;
                sumSq += v * v;
            }

            windowMean[(size_t) w] = juce::jmax (0.02f, (float) (sum / (double) N));
            windowRms [(size_t) w] = juce::jmax (0.02f, (float) std::sqrt (sumSq / (double) N));
        }
    }

    void GrainFX::prepare (const EngineSpec& spec)
    {
        sampleRate = juce::jmax (8000.0, spec.sampleRate);
        maxBlock   = juce::jmax (1, spec.maxBlockSize);

        history.prepare (sampleRate, kHistorySeconds);

        // Sixteen samples of margin: HistoryBuffer::read() reaches one sample
        // further back than the delay it is given, and a grain must never read
        // across the write head.
        historySamples = (float) juce::jmax (64, history.capacity() - 16);

        freezeLength = juce::jmax (1024, (int) (sampleRate * kFreezeSeconds));
        freezeL.assign ((size_t) freezeLength + 2, 0.0f);
        freezeR.assign ((size_t) freezeLength + 2, 0.0f);

        buildWindows();

        bandL.prepare (kLowSplitHz, kHighSplitHz, sampleRate);
        bandR.prepare (kLowSplitHz, kHighSplitHz, sampleRate);

        fbLpL.setCutoff (kFeedbackLpHz, sampleRate);
        fbLpR.setCutoff (kFeedbackLpHz, sampleRate);
        fbDcL.prepare (sampleRate);
        fbDcR.prepare (sampleRate);

        // The house pan law is the -4.5 dB compromise, which puts a centred
        // source at 0.5946 per channel.  Trimming by its own centre value is
        // what makes SPREAD at zero exactly transparent; the 1.5 dB that full
        // spread then adds is the compromise law behaving as designed, and is
        // the same behaviour unison spread has in the synth core.
        float centreL = 1.0f, centreR = 1.0f;
        fx::panGains (0.0f, centreL, centreR);
        panTrim = 1.0f / juce::jmax (0.1f, centreL);

        fbAttack  = 1.0f - std::exp (-1.0f / (float) (0.005 * sampleRate));
        fbRelease = 1.0f - std::exp (-1.0f / (float) (0.400 * sampleRate));

        reset();
    }

    void GrainFX::reset()
    {
        history.reset();

        std::fill (freezeL.begin(), freezeL.end(), 0.0f);
        std::fill (freezeR.begin(), freezeR.end(), 0.0f);

        for (auto& g : grains)
            g = Grain {};

        activeGrains = 0;
        untilNextGrain = 0.0f;

        freezeHeld = false;
        freezeReady = false;
        freezeFill = 0;
        freezeSourceStart = 0;

        fbL = fbR = 0.0f;
        fbPeak = 0.0f;
        fbLpL.reset(); fbLpR.reset();
        fbDcL.reset(); fbDcR.reset();

        bandL.reset(); bandR.reset();

        prevMix = 0.0f;

        pitchTable = PitchTable {};
        pitchTable.cumulative[PitchTable::kSpan] = 1.0f;

        for (int k = PitchTable::kSpan + 1; k < PitchTable::kSize; ++k)
            pitchTable.cumulative[k] = 1.0f;
    }

    void GrainFX::readFreeze (float pos, float& l, float& r) const noexcept
    {
        const float p = juce::jlimit (0.0f, (float) (freezeLength - 2), pos);
        const int   i = (int) p;
        const float f = p - (float) i;

        l = fx::lerp (freezeL[(size_t) i], freezeL[(size_t) (i + 1)], f);
        r = fx::lerp (freezeR[(size_t) i], freezeR[(size_t) (i + 1)], f);
    }

    // =======================================================================
    //  The weighted transposition distribution.  Section 119: weighted, never
    //  uniform.  Rebuilt once per block, which is 25 iterations of arithmetic
    //  and no allocation.
    // =======================================================================
    void GrainFX::buildPitchTable (int pitchMode, int scaleType, int harmonyMode,
                                   float scatter, float alter) noexcept
    {
        pitchTable.unquantised = (pitchMode >= 6);      // FREE

        const int mask  = kScaleMasks[juce::jlimit (0, 9, scaleType)];
        const int chord = chordMask (mask);
        const int third = scaleThird (mask);

        // SCATTER collapses the distribution onto unison as it closes.
        const float scatterPow = powApprox (juce::jlimit (0.0f, 1.0f, scatter), 1.5f);

        // MACRO: Alter opens the tiers harmony_mode has closed, as a fade
        // rather than as a switch.
        const float a = juce::jlimit (0.0f, 1.0f, alter);
        const float alterGain = a * a;

        const int harmonyTier = juce::jlimit (0, 2, harmonyMode);

        float total = 0.0f;
        float unison = 0.0f;

        for (int k = 0; k < PitchTable::kSize; ++k)
        {
            const int semis = k - PitchTable::kSpan;            // -12 .. +12
            const int pc = ((semis % 12) + 12) % 12;            // interval class
            const bool inScale = (mask  & (1 << pc)) != 0;
            const bool isChord = (chord & (1 << pc)) != 0;

            float weight = 0.0f;
            int   tier = 2;

            // Section 119, literally: very common 0, common +-12, moderate
            // fifth, contextual chord tones, rare other scale tones.
            if      (semis ==   0) { weight = 100.0f; tier = 0; }
            else if (semis ==  12) { weight =  30.0f; tier = 0; }
            else if (semis == -12) { weight =  26.0f; tier = 0; }
            else if (semis ==   7) { weight =  13.0f; tier = 0; }
            else if (semis ==  -7) { weight =   9.0f; tier = 0; }
            else if (isChord)      { weight = (semis > 0 ? 6.0f : 4.0f);  tier = 0; }
            else if (inScale)      { weight = (semis > 0 ? 1.6f : 1.2f);  tier = 1; }
            else                   { weight =   0.5f; tier = 2; }

            // Which candidates this pitch mode allows at all.
            const int absSemis = std::abs (semis);
            bool allowed = false;

            switch (pitchMode)
            {
                case 0:  allowed = (semis == 0); break;                              // ROOT
                case 1:  allowed = (semis == 0 || absSemis == 12); break;            // OCTAVE
                case 2:  allowed = (semis == 0 || absSemis == 7 || absSemis == 12); break;   // FIFTH
                case 3:  allowed = (semis == 0 || absSemis == third || absSemis == 12); break; // THIRD
                case 4:  allowed = inScale; break;                                   // SCALE
                case 5:  allowed = true; break;                                      // CHROMATIC
                default: allowed = (semis == 0); break;                              // FREE: unused
            }

            if (! allowed)
                weight = 0.0f;

            if (tier > harmonyTier)
                weight *= alterGain;

            if (semis != 0)
                weight *= scatterPow;
            else
                unison = weight;

            total += weight;
            pitchTable.cumulative[k] = total;
        }

        if (total <= 1.0e-6f)
        {
            // Nothing survived - ROOT mode with scatter at zero reaches this,
            // as does a harmony cap that closed everything.  Fall back to
            // unison so a grain always has a defined pitch.
            for (int k = 0; k < PitchTable::kSize; ++k)
                pitchTable.cumulative[k] = (k >= PitchTable::kSpan) ? 1.0f : 0.0f;

            total = 1.0f;
            unison = 1.0f;
        }

        pitchTable.unisonShare = juce::jlimit (0.0f, 1.0f, unison / total);

        if (pitchTable.unquantised)
        {
            // FREE draws a continuous microtonal spread rather than a set, so
            // the table's unison share says nothing useful about it.  The
            // grains all sit within half a semitone of the same transposition,
            // which is coherent when the spread is narrow and beats when it is
            // wide - and SCATTER is what sets the width.
            pitchTable.unisonShare = juce::jlimit (0.0f, 1.0f,
                                                   1.0f - 0.5f * juce::jlimit (0.0f, 1.0f, scatter));
        }
    }

    float GrainFX::drawSemitones (float freeSemis, float scatter) noexcept
    {
        if (pitchTable.unquantised)
        {
            // Section 108's FREE tier: unquantised and microtonal.  SCATTER
            // sets how far off the nominal transposition a grain may sit, up
            // to half a semitone either way.
            return freeSemis + rng.nextBipolar() * 0.5f * juce::jlimit (0.0f, 1.0f, scatter);
        }

        const float total = pitchTable.cumulative[PitchTable::kSize - 1];
        const float r = rng.next01() * total;

        for (int k = 0; k < PitchTable::kSize; ++k)
            if (r < pitchTable.cumulative[k])
                return (float) (k - PitchTable::kSpan);

        return 0.0f;
    }

    void GrainFX::spawnGrain (float sizeSamples, float positionOffset, float spread,
                              float direction, float widthScale, int windowIndex,
                              float freeSemis, float scatter, float alter) noexcept
    {
        // Find a free slot.  An exhausted pool drops the grain: a dropped
        // grain is silence that was never scheduled, a stolen one is a window
        // cut off part way through its envelope, which is a click.
        int slot = -1;

        for (int g = 0; g < kMaxGrains; ++g)
        {
            if (! grains[(size_t) g].active)
            {
                slot = g;
                break;
            }
        }

        if (slot < 0)
            return;

        const float length = juce::jmax (8.0f, sizeSamples);

        float rate = fx::exp2Fast (drawSemitones (freeSemis, scatter) * (1.0f / 12.0f));
        rate = juce::jlimit (0.2f, 5.0f, rate);

        if (rng.next01() < juce::jlimit (0.0f, 1.0f, direction))
            rate = -rate;

        // MACRO: Alter swaps in the alternate window for some grains.
        int window = juce::jlimit (0, kNumWindows - 1, windowIndex);

        if (rng.next01() < 0.6f * juce::jlimit (0.0f, 1.0f, alter))
            window = (window == kPercussive) ? kHann : kPercussive;

        Grain& g = grains[(size_t) slot];

        if (freezeReady)
        {
            // The freeze store is static, so the cursor is a plain index and
            // moves at the read rate.
            const float span = length * std::abs (rate);
            const float top = (float) (freezeLength - 2);

            const float lo = (rate > 0.0f) ? 1.0f : 1.0f + span;
            const float hi = (rate > 0.0f) ? top - span : top;

            if (hi <= lo)
                return;                     // will not fit: drop it

            // POSITION scrubs the held slice: 0 is its most recent end.
            const float wanted = juce::jlimit (0.0f, 1.0f, positionOffset);
            const float start = top - wanted * top;

            g.cursor = juce::jlimit (lo, hi, start);
            g.cursorDelta = rate;
        }
        else
        {
            // A live grain's cursor is a delay behind the write head, and the
            // head moves one sample per sample, so the delay changes at
            // (1 - rate).  Both ends of the grain's travel have to stay inside
            // the buffer and behind the head.
            const float drift = 1.0f - rate;
            const float travel = length * drift;        // signed
            const float top = historySamples;

            const float lo = (travel < 0.0f) ? 2.0f - travel : 2.0f;
            const float hi = (travel > 0.0f) ? top - travel : top;

            if (hi <= lo)
                return;                     // will not fit: drop it

            g.cursor = juce::jlimit (lo, hi, positionOffset);
            g.cursorDelta = drift;
        }

        // Stereo.  SPREAD is a per-grain pan, multiplied by the World macro's
        // width scale.  It cannot reach the low band, which is collapsed to
        // mono after the grains are summed.
        const float pan = juce::jlimit (-1.0f, 1.0f,
                                        rng.nextBipolar() * juce::jlimit (0.0f, 1.0f, spread)
                                            * juce::jlimit (0.0f, 2.0f, widthScale));

        fx::panGains (pan, g.gainL, g.gainR);
        g.gainL *= panTrim;
        g.gainR *= panTrim;

        g.phase = 0.0f;
        g.phaseInc = 1.0f / length;
        g.window = window;
        g.fromFreeze = freezeReady;
        g.active = true;

        ++activeGrains;
    }

    void GrainFX::process (juce::AudioBuffer<float>& buffer,
                           const ParameterRegistry& p,
                           const MacroState& macros)
    {
        const int n = juce::jmin (macros.numSamples, buffer.getNumSamples());
        const int numCh = juce::jmin (2, buffer.getNumChannels());

        if (n <= 0 || numCh <= 0)
            return;

        float* left  = buffer.getWritePointer (0);
        float* right = (numCh > 1) ? buffer.getWritePointer (1) : left;

        // -- the registry, read exactly once ---------------------------------
        //  grainfx_on is read as well as being checked by the chain, because
        //  Grain must keep writing its history while it is switched off.  An
        //  engine that stopped remembering when it was bypassed would have
        //  nothing to granulate for the first seconds after being switched on.
        const bool  enabled   = p.flag (PID::grainFxOn);
        const float scatter   = juce::jlimit (0.0f, 1.0f, p.raw (PID::grainScatter));
        const float sizeMs    = juce::jlimit (kMinGrainMs, kMaxGrainMs, p.raw (PID::grainSize));
        const float densityP  = juce::jlimit (0.5f, kMaxDensity, p.raw (PID::grainDensity));
        const float position  = juce::jlimit (0.0f, 1.0f, p.raw (PID::grainPosition));
        const int   pitchMode = juce::jlimit (0, 6, p.choice (PID::grainPitchMode));
        const float freeSemis = juce::jlimit (-24.0f, 24.0f, p.raw (PID::grainPitch));
        const float spreadP   = juce::jlimit (0.0f, 1.0f, p.raw (PID::grainSpread));
        const float jitterP   = juce::jlimit (0.0f, 1.0f, p.raw (PID::grainJitter));
        const float direction = juce::jlimit (0.0f, 1.0f, p.raw (PID::grainDirection));
        const int   windowIx  = juce::jlimit (0, kNumWindows - 1, p.choice (PID::grainWindow));
        const float feedback  = juce::jlimit (0.0f, 0.95f, p.raw (PID::grainFeedback));
        const bool  freeze    = p.flag (PID::grainFreeze);
        const float mixParam  = juce::jlimit (0.0f, 1.0f, p.raw (PID::grainMix));

        const int scaleTypeIx = juce::jlimit (0, 9, p.choice (PID::scaleType));
        const int harmonyIx   = juce::jlimit (0, 2, p.choice (PID::harmonyMode));

        // root_note is read and then deliberately not used.  A grain's
        // transposition is relative to whatever the material already is, so
        // the tonic's absolute pitch class cannot change which intervals are
        // in key - only the scale's interval pattern can.  It is read here so
        // that the AUTO resolution has one home when analysis arrives.
        const int rootNoteIx = p.choice (PID::rootNote);
        juce::ignoreUnused (rootNoteIx);

        // -- macro response, added to the controls ---------------------------
        const float jitter = juce::jlimit (0.0f, 1.0f, jitterP + 0.35f * macros.movement);
        const float spread = juce::jlimit (0.0f, 1.0f, spreadP
                                           + 0.30f * macros.scale
                                           + 0.15f * macros.distance);
        const float widthScale = juce::jlimit (0.0f, 2.0f, macros.widthScale);
        const float alter = juce::jlimit (0.0f, 1.0f, macros.alterAmount);

        // SCATTER is the module's primary control, so it reaches density as
        // well as the pitch distribution and the position spread.
        const float density = juce::jlimit (0.25f, kMaxDensity * 1.25f,
                                            densityP * (0.35f + 0.65f * scatter));

        buildPitchTable (pitchMode, scaleTypeIx, harmonyIx, scatter, alter);

        // -- gain staging: density must mean density, not volume -------------
        const float sizeSeconds = sizeMs * 0.001f;
        const float sizeSamples = sizeSeconds * (float) sampleRate;

        const float overlap = juce::jmax (1.0f, density * sizeSeconds);
        const float dirCoherence = 1.0f - 2.0f * juce::jmin (direction, 1.0f - direction);
        const float coherence = juce::jlimit (0.0f, 1.0f, pitchTable.unisonShare
                                              * (1.0f - jitter) * dirCoherence);

        const float wMean = windowMean[(size_t) windowIx];
        const float wRms  = windowRms [(size_t) windowIx];

        // gain = 1 / ( overlap^(0.5 + 0.5c) * windowRms^(1-c) * windowMean^c )
        const float logGain = (0.5f + 0.5f * coherence) * fx::log2Fast (overlap)
                            + (1.0f - coherence) * fx::log2Fast (wRms)
                            + coherence * fx::log2Fast (wMean);

        //  Bounded at 2x.  The law is a loudness match, and loudness-matching
        //  a sparse stream of a high-crest window - PERCUSSIVE is 89 % silence
        //  and its RMS is 0.236 - would ask for nine times gain and produce
        //  peaks nine times the source's.  The clamp binds there, so
        //  PERCUSSIVE at low density is quieter than the source rather than
        //  peakier than it.  Section 148 cuts both ways: not using loudness to
        //  fake quality includes not manufacturing headroom problems.
        const float grainGain = juce::jlimit (0.02f, 2.0f, fx::exp2Fast (-logGain));

        // -- freeze ----------------------------------------------------------
        if (freeze && ! freezeHeld)
        {
            // Engaged: start copying the last kFreezeSeconds of history into
            // the store.  Grains keep coming from the live history until it is
            // full, so nothing stops while the copy runs.
            freezeFill = 0;
            freezeReady = false;
            freezeSourceStart = history.getWriteIndex() - freezeLength;
        }
        else if (! freeze && freezeHeld)
        {
            // Released.  New grains read the live history again; grains
            // already in flight finish on the store they started on, which is
            // why neither edge of FREEZE can click.
            freezeReady = false;
            freezeFill = 0;
        }

        freezeHeld = freeze;

        if (freeze && ! freezeReady)
        {
            // Bounded work: 32 store samples per audio sample, so the copy is
            // proportional to the block size and never a spike.
            const int mask = history.getMask();
            //  jmin against maxBlock so that a host handing over a block
            //  larger than the one it promised in prepare() cannot turn this
            //  into an unbounded burst.
            const int budget = juce::jmin (n, juce::jmax (1, maxBlock)) * kFreezeCopyRate;
            const int end = juce::jmin (freezeLength, freezeFill + budget);

            for (int k = freezeFill; k < end; ++k)
            {
                const int idx = (freezeSourceStart + k) & mask;

                float l = 0.0f, r = 0.0f;
                history.readAt ((float) idx, l, r);

                freezeL[(size_t) k] = l;
                freezeR[(size_t) k] = r;
            }

            freezeFill = end;

            if (freezeFill >= freezeLength)
            {
                // Two guard points past the end so the interpolator in
                // readFreeze() never reads an uninitialised sample.
                freezeL[(size_t) freezeLength]     = freezeL[(size_t) (freezeLength - 1)];
                freezeR[(size_t) freezeLength]     = freezeR[(size_t) (freezeLength - 1)];
                freezeL[(size_t) freezeLength + 1] = freezeL[(size_t) (freezeLength - 1)];
                freezeR[(size_t) freezeLength + 1] = freezeR[(size_t) (freezeLength - 1)];

                freezeReady = true;
            }
        }

        // -- BYPASS: at zero mix nothing is audible, so nothing is run --------
        //  The history is still written, because a granulator that only starts
        //  remembering when its mix opens has nothing to granulate.  The
        //  buffer is not touched at all, so the output is the input sample for
        //  sample rather than merely close to it.
        if (! enabled || (mixParam <= 0.0f && prevMix <= 0.0f))
        {
            for (auto& g : grains)
                g.active = false;

            activeGrains = 0;
            untilNextGrain = 0.0f;
            fbL = fbR = 0.0f;
            fbPeak = 0.0f;

            for (int i = 0; i < n; ++i)
                history.write (left[i], (numCh > 1) ? right[i] : left[i]);

            // The block that just went out was pure dry, so the next one has
            // to ramp its mix up from zero rather than from whatever the
            // parameter happens to say.  Without this, re-enabling Grain at a
            // high mix would step straight to full wet.
            prevMix = 0.0f;
            return;
        }

        fx::Ramp mixRamp;
        mixRamp.set (prevMix, mixParam, n);

        const float fbAmount = feedback * kFeedbackTrim;

        // POSITION reaches back over 60 % of the history, which leaves room
        // for the grain's own travel at the far end.
        const float baseDelay = (0.01f + position * (float) (kHistorySeconds * 0.6))
                                    * (float) sampleRate;
        const float scatterRange = scatter * (float) sampleRate * 0.75f;

        for (int i = 0; i < n; ++i)
        {
            const float inL = left[i];
            const float inR = (numCh > 1) ? right[i] : inL;

            // Input plus the previous sample's limited feedback.  The one
            // sample of delay is unavoidable and harmless: the history has to
            // be written before the grains read it.
            history.write (fx::guard (inL + fbAmount * fbL),
                           fx::guard (inR + fbAmount * fbR));

            // ---- scheduling -------------------------------------------------
            untilNextGrain -= 1.0f;

            if (untilNextGrain <= 0.0f)
            {
                float offset;

                if (freezeReady)
                {
                    // POSITION scrubs the held slice; SCATTER and JITTER widen
                    // the region grains are drawn from.
                    offset = juce::jlimit (0.0f, 1.0f, position
                                + rng.nextBipolar() * 0.35f * scatter
                                    * (0.25f + 0.75f * jitter));
                }
                else
                {
                    // MACRO: Breath drifts the read position, scaled by Motion.
                    // Section 72 names Grain as a Motion destination, and the
                    // read position is slow enough for Breath to be near.
                    const float drift = macros.breathAt (i) * macros.movement
                                            * 0.08f * historySamples;

                    const float scatterOffset = rng.nextBipolar() * scatterRange
                                                    * (0.25f + 0.75f * jitter);

                    offset = juce::jmax (2.0f, baseDelay + scatterOffset + drift);
                }

                spawnGrain (sizeSamples, offset, spread, direction, widthScale,
                            windowIx, freeSemis, scatter, alter);

                const float interval = (float) sampleRate / density;

                untilNextGrain = juce::jmax (4.0f,
                    interval * (1.0f + 0.9f * jitter * rng.nextBipolar()));
            }

            // ---- render the pool ---------------------------------------------
            float wetL = 0.0f, wetR = 0.0f;

            for (int gi = 0; gi < kMaxGrains; ++gi)
            {
                Grain& g = grains[(size_t) gi];

                if (! g.active)
                    continue;

                float sl = 0.0f, sr = 0.0f;

                if (g.fromFreeze)
                    readFreeze (g.cursor, sl, sr);
                else
                    history.read (g.cursor, sl, sr);

                const float wpos = g.phase * (float) kWindowTableSize;
                const int   wi = juce::jlimit (0, kWindowTableSize - 1, (int) wpos);
                const float wf = juce::jlimit (0.0f, 1.0f, wpos - (float) wi);

                const auto& table = windows[(size_t) g.window];
                const float w = fx::lerp (table[(size_t) wi], table[(size_t) (wi + 1)], wf);

                wetL += sl * w * g.gainL;
                wetR += sr * w * g.gainR;

                g.cursor += g.cursorDelta;
                g.phase  += g.phaseInc;

                if (g.phase >= 1.0f)
                {
                    g.active = false;
                    --activeGrains;
                }
            }

            wetL *= grainGain;
            wetR *= grainGain;

            // ---- the low band stays put --------------------------------------
            //  Sections 38/40/43.  SPREAD pans individual grains, which
            //  decorrelates whatever is in them.  Splitting and collapsing the
            //  low band to mono keeps the bass at correlation 1.0 whatever
            //  SPREAD and widthScale say, and the splits are complementary so
            //  the stage is transparent when the two channels already agree.
            float lowL, midL, highL;
            float lowR, midR, highR;

            bandL.split (wetL, lowL, midL, highL);
            bandR.split (wetR, lowR, midR, highR);

            const float lowMono = 0.5f * (lowL + lowR);

            wetL = fx::guard (lowMono + midL + highL);
            wetR = fx::guard (lowMono + midR + highR);

            // ---- feedback, bounded four ways ----------------------------------
            {
                const float pl = fbDcL.process (fbLpL.lowpass (wetL));
                const float pr = fbDcR.process (fbLpR.lowpass (wetR));

                const float peak = juce::jmax (std::abs (pl), std::abs (pr));

                fbPeak += (peak - fbPeak) * (peak > fbPeak ? fbAttack : fbRelease);

                const float limit = (fbPeak > kFeedbackCeiling)
                    ? kFeedbackCeiling / juce::jmax (1.0e-6f, fbPeak)
                    : 1.0f;

                fbL = fx::guard (fx::tanhFast (pl * limit));
                fbR = fx::guard (fx::tanhFast (pr * limit));
            }

            // ---- mix ----------------------------------------------------------
            float dryGain, wetGain;
            fx::dryWetGains (mixRamp.at (i), dryGain, wetGain);

            const float outL = fx::guard (dryGain * inL + wetGain * wetL);
            const float outR = fx::guard (dryGain * inR + wetGain * wetR);

            left[i] = outL;

            if (numCh > 1)
                right[i] = outR;
        }

        prevMix = mixParam;
    }
}
