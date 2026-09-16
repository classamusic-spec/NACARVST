#include "GrainFX.h"

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
}
