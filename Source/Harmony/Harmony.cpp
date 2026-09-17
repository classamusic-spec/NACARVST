/*
    THE HARMONY ENGINE.

    Two jobs, and they are deliberately separate:

      detectKey()   twelve chroma energies in, a key out - or an honest
                    admission that there is no key.
      Context       the constraint a mutation works inside: given a key and
                    the user's HARMONY control, which pitches are allowed and
                    where does a forbidden one go.

    Everything the numbers came from is written down in README.md next to this
    file, including what this method is bad at.  The one rule that matters more
    than any of it: when the key is not known, nothing is constrained.  An
    instrument that transposes a drum loop into E flat minor because it guessed
    is broken in a way the user cannot diagnose.
*/

#include "Harmony.h"
#include "UncertainMode.h"

#include <cmath>

namespace nacar::harmony
{

namespace
{
    constexpr int kScaleCount = (int) Scale::count;      // 9
    constexpr int kSearchable = kScaleCount - 1;         // CHROMATIC is never a detection result

    // ======================================================================
    //  The scales.
    //
    //  Bit 0 is the root.  Every mask has it: the tonic is a member of its own
    //  scale, and snapCents() relies on that to bracket any input.
    // ======================================================================
    constexpr juce::uint16 kMask[kScaleCount] =
    {
        0x0AB5,     // MAJOR            0 2 4 5 7 9 11
        0x05AD,     // MINOR            0 2 3 5 7 8 10   (natural)
        0x06AD,     // DORIAN           0 2 3 5 7 9 10
        0x05AB,     // PHRYGIAN         0 1 3 5 7 8 10
        0x0AD5,     // LYDIAN           0 2 4 6 7 9 11
        0x06B5,     // MIXOLYDIAN       0 2 4 5 7 9 10
        0x09AD,     // HARMONIC MINOR   0 2 3 5 7 8 11
        0x0AAD,     // MELODIC MINOR    0 2 3 5 7 9 11   (ascending form)
        0x0FFF      // CHROMATIC
    };

    /*  COLOUR - the borrowed tones, and the whole argument for them.

        COLOR is parallel modal interchange, one step in each direction: the
        scale plus the two tones that the modes immediately either side of it on
        the brightness continuum have and it does not.  Brightness order is the
        familiar one, each step flattening exactly one degree:

            lydian - ionian - mixolydian - dorian - aeolian - phrygian - locrian

        Three consequences, and they are why this rule was chosen over a list:

          * every borrowed tone is a semitone neighbour of a degree the scale
            already has, so it behaves as a chromatic alteration of a known
            degree rather than as a foreign note;
          * the tonic and the fifth are never borrowed against - no interchange
            moves them, so the key keeps its floor and its ceiling;
          * the third is only ever doubled, never replaced.  DORIAN gains the
            natural third and MIXOLYDIAN the flat one, which is the blues third
            in both directions; the original third stays permitted, so nothing
            here can turn a minor key major.

        The two scales that are not diatonic modes do not sit on that continuum,
        so they take the equivalent step by practice rather than by rotation:
        harmonic and melodic minor borrow each other's sixth and seventh, which
        is literally how the minor scale is used - one form ascending, another
        descending.
    */
    constexpr juce::uint16 kBorrowed[kScaleCount] =
    {
        0x0440,     // MAJOR           + #4 (lydian), b7 (mixolydian)
        0x0202,     // MINOR           + natural 6 (dorian), b2 (phrygian)
        0x0110,     // DORIAN          + natural 3 (mixolydian), b6 (aeolian)
        0x0044,     // PHRYGIAN        + natural 2 (aeolian), b5 (locrian)
        0x0420,     // LYDIAN          + natural 4 (ionian), b7 (lydian dominant)
        0x0808,     // MIXOLYDIAN      + natural 7 (ionian), b3 (dorian)
        0x0600,     // HARMONIC MINOR  + natural 6 (melodic), b7 (natural minor)
        0x0500,     // MELODIC MINOR   + b6, b7 - the descending form
        0x0000      // CHROMATIC       + nothing left to borrow
    };

    constexpr const char* kName[kScaleCount] =
    {
        "MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN",
        "MIXOLYDIAN", "HARMONIC MINOR", "MELODIC MINOR", "CHROMATIC"
    };

    constexpr juce::uint16 kAllTwelve = 0x0FFF;
    constexpr juce::uint16 kBothThirds = 0x0018;   // bits 3 and 4

    /*  KNOWING THE ROOT IS NOT KNOWING THE MODE.

        A bare triad pins its tonic and says almost nothing about the mode:
        C-E-G is the first, third and fifth of C major, C lydian and C
        mixolydian alike, and detectKey() reports exactly that - a root
        confidence of 0.79 against a scale confidence of 0.20.  Acting on the
        winner of that coin flip means snapping a third, which is the one
        degree that decides whether a key is major or minor and so the single
        most audible way to be wrong.

        So a context whose root is usable but whose mode is not permits BOTH
        thirds.  It is the same rule COLOR already follows one level down -
        double the third, never replace it - applied to the mode itself.

        Context has no field for that, and Harmony.h is frozen, so the state is
        carried in the one field it is a statement about.  A `scale` value of
        `count + s` is scale s, believed but not confirmed.  Nothing outside
        this file has to know that: maskOf() and nameOf() are total over both
        ranges, and UncertainMode.h declares the two predicates that turn the
        encoding into an API instead of a secret. */
    constexpr int kUncertainBase = kScaleCount;

    inline bool encodesUncertainty (Scale s) noexcept
    {
        const int i = (int) s;
        return i >= kUncertainBase && i < kUncertainBase + kScaleCount;
    }

    inline int scaleIndex (Scale s) noexcept
    {
        int i = (int) s;

        if (encodesUncertainty (s))
            i -= kUncertainBase;

        return (i >= 0 && i < kScaleCount) ? i : (int) Scale::minor;
    }

    /*  The mask a context actually enforces.  kAllTwelve means "no constraint",
        and that is the single place the sharp edge lives: FREE never constrains,
        and neither does anything else when the key is not known. */
    inline juce::uint16 activeMask (const Context& c) noexcept
    {
        if (c.mode == Mode::free || ! c.keyKnown)
            return kAllTwelve;

        const int i = scaleIndex (c.scale);

        const juce::uint16 base = (juce::uint16) (c.mode == Mode::colour
                                                      ? (kMask[i] | kBorrowed[i])
                                                      : kMask[i]);

        // The mode is a guess: do not let it decide the quality of the key.
        return (juce::uint16) (encodesUncertainty (c.scale) ? (base | kBothThirds) : base);
    }

    inline int pitchClass (int semitone, int root) noexcept
    {
        const int pc = (semitone - root) % 12;
        return pc < 0 ? pc + 12 : pc;
    }

    // ======================================================================
    //  Detection profiles.
    //
    //  See README.md.  In short: Temperley's revision of Krumhansl-Schmuckler
    //  turns out to be one degree-weight vector laid on two different scales,
    //  so it generalises to all nine of ours without inventing a single number.
    // ======================================================================
    constexpr float kDegreeWeight[7] = { 5.0f, 3.5f, 4.5f, 4.0f, 4.5f, 3.5f, 4.0f };
    constexpr float kOutOfScale      = 2.0f;

    using Profile  = std::array<float, 12>;
    using Profiles = std::array<Profile, (size_t) kSearchable>;

    constexpr Profiles buildProfiles()
    {
        Profiles p {};

        for (int s = 0; s < kSearchable; ++s)
        {
            int degree = 0;

            for (int pc = 0; pc < 12; ++pc)
            {
                const bool member = (kMask[s] & (1u << pc)) != 0;
                p[(size_t) s][(size_t) pc] = member ? kDegreeWeight[degree++] : kOutOfScale;
            }
        }

        return p;
    }

    constexpr Profiles kProfile = buildProfiles();

    /*  Every profile is a permutation of the same twelve numbers, so they all
        share a mean and a standard deviation.  Computed once here rather than
        96 times per call. */
    constexpr float kProfileMean = (5.0f + 3.5f + 4.5f + 4.0f + 4.5f + 3.5f + 4.0f
                                        + 5.0f * kOutOfScale) / 12.0f;

    inline float profileSigma() noexcept
    {
        float acc = 0.0f;

        for (int pc = 0; pc < 12; ++pc)
        {
            const float d = kProfile[0][(size_t) pc] - kProfileMean;
            acc += d * d;
        }

        return std::sqrt (acc / 12.0f);
    }

    // ---- confidence shaping ------------------------------------------------
    //  Every constant here was chosen by measuring the three populations that
    //  matter - synthetic key material, independent noise, and near-flat
    //  percussive chroma - and is justified in README.md.
    constexpr float kSparsityLo = 0.03f;   // below this the chroma is flat: no key
    constexpr float kSparsityHi = 0.14f;   // at this it is as peaked as real tonal material
    constexpr float kFitLo      = 0.45f;   // correlation below this is not a fit
    constexpr float kFitHi      = 0.80f;
    constexpr float kRootMargin = 0.10f;   // correlation lead over the best rival root
    constexpr float kScaleMargin= 0.10f;   // ... and over the best rival scale at that root
    constexpr float kRootFloor  = 0.30f;   // what a perfect fit with no lead is worth
    constexpr float kScaleFloor = 0.25f;
    constexpr float kTieEpsilon = 1.0e-6f; // ties go to the earlier scale: MAJOR, then MINOR

    inline float clamp01 (float x) noexcept
    {
        return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    }

    inline float ramp (float x, float lo, float hi) noexcept
    {
        return clamp01 ((x - lo) / (hi - lo));
    }

    /*  How far the chroma is from flat, as 1 - normalised entropy.  Zero for
        twelve equal bins whatever their level, 1 for all the energy in one bin.
        This is what makes a noise floor, a cymbal or a kick return no key at
        all: they light every bin about equally, and no correlation should be
        allowed to read a tonal centre out of that. */
    inline float sparsity (const std::array<float, 12>& chroma) noexcept
    {
        float sum = 0.0f;

        for (auto v : chroma)
            sum += v > 0.0f ? v : 0.0f;

        if (! (sum > 1.0e-12f))
            return 0.0f;

        float h = 0.0f;

        for (auto v : chroma)
        {
            const float p = (v > 0.0f ? v : 0.0f) / sum;

            if (p > 1.0e-12f)
                h -= p * std::log (p);
        }

        constexpr float logTwelve = 2.4849066497880004f;   // ln 12

        return clamp01 (1.0f - h / logTwelve);
    }
}

// ===========================================================================
//  The vocabulary
// ===========================================================================
juce::uint16 maskOf (Scale s) noexcept
{
    const juce::uint16 m = kMask[scaleIndex (s)];

    return (juce::uint16) (encodesUncertainty (s) ? (m | kBothThirds) : m);
}

/*  The name of the scale behind the value, uncertain or not: a readout should
    show the best guess rather than a blank, and the confidence that goes with
    it is the analysis's to report, not this function's. */
const char* nameOf (Scale s) noexcept
{
    return kName[scaleIndex (s)];
}

// ===========================================================================
//  Mode confidence - see UncertainMode.h
// ===========================================================================
Scale uncertainMode (Scale s) noexcept
{
    return (Scale) (scaleIndex (s) + kUncertainBase);
}

Scale scaleOf (const Context& c) noexcept
{
    return (Scale) scaleIndex (c.scale);
}

bool modeIsUncertain (const Context& c) noexcept
{
    return c.keyKnown && encodesUncertainty (c.scale);
}

// ===========================================================================
//  Detection
// ===========================================================================
Detection detectKey (const std::array<float, 12>& chroma) noexcept
{
    Detection d;

    // -- centre the chroma once ---------------------------------------------
    float mean = 0.0f;

    for (auto v : chroma)
        mean += v;

    mean /= 12.0f;

    std::array<float, 12> centred {};
    float variance = 0.0f;

    for (int pc = 0; pc < 12; ++pc)
    {
        centred[(size_t) pc] = chroma[(size_t) pc] - mean;
        variance += centred[(size_t) pc] * centred[(size_t) pc];
    }

    variance /= 12.0f;

    const float peak = ramp (sparsity (chroma), kSparsityLo, kSparsityHi);

    // A chroma with no contrast in it correlates with nothing, and a Pearson
    // correlation against it is a division by zero.  There is no key here.
    if (peak <= 0.0f || variance <= 1.0e-18f)
        return d;

    const float norm = 1.0f / (std::sqrt (variance) * profileSigma() * 12.0f);

    // -- correlate against all 12 x 8 candidates ----------------------------
    float score[12][kSearchable];
    float best = -2.0f;
    int   bestRoot = 0, bestScale = 0;

    for (int s = 0; s < kSearchable; ++s)
    {
        for (int root = 0; root < 12; ++root)
        {
            float acc = 0.0f;

            for (int pc = 0; pc < 12; ++pc)
            {
                const int degree = pc - root < 0 ? pc - root + 12 : pc - root;
                acc += centred[(size_t) pc] * (kProfile[(size_t) s][(size_t) degree] - kProfileMean);
            }

            const float r = acc * norm;
            score[root][s] = r;

            // Strictly greater, with a tolerance: an exact tie keeps the
            // earlier scale, and the enum lists MAJOR and MINOR first because
            // they are overwhelmingly the more likely answer.
            if (r > best + kTieEpsilon)
            {
                best = r;
                bestRoot = root;
                bestScale = s;
            }
        }
    }

    // -- the two rivals that decide whether the answer means anything -------
    float rivalRoot = -2.0f, rivalScale = -2.0f;

    for (int s = 0; s < kSearchable; ++s)
    {
        for (int root = 0; root < 12; ++root)
        {
            const float r = score[root][s];

            if (root != bestRoot)
                rivalRoot = juce::jmax (rivalRoot, r);
            else if (s != bestScale)
                rivalScale = juce::jmax (rivalScale, r);
        }
    }

    const float fit        = ramp (best, kFitLo, kFitHi);
    const float rootLead   = ramp (best - rivalRoot,  0.0f, kRootMargin);
    const float scaleLead  = ramp (best - rivalScale, 0.0f, kScaleMargin);

    const float rootConf   = peak * fit * (kRootFloor + (1.0f - kRootFloor) * rootLead);

    if (! (rootConf > 0.0f))
        return d;                       // no root, no answer, and no invented key

    d.root            = bestRoot;
    d.scale           = (Scale) bestScale;
    d.rootConfidence  = rootConf;

    // The scale can never be more certain than the root it is built on: a mode
    // without a tonic is not an answer.
    d.scaleConfidence = rootConf * (kScaleFloor + (1.0f - kScaleFloor) * scaleLead);

    return d;
}

// ===========================================================================
//  The constraint
// ===========================================================================
Context Context::from (const AnalysisResult& analysis, Mode mode, int forcedScale)
{
    Context c;
    c.mode = mode;

    const bool rootUsable  = analysis.keyIsUsable();
    const bool forced      = forcedScale >= 0 && forcedScale < kScaleCount;
    const bool analysedScale = analysis.scale >= 0 && analysis.scale < kScaleCount;

    if (forced)
        // The user named the mode.  That is knowledge, not an estimate, and it
        // is never widened - forcing MINOR and getting a major third back would
        // make the control a suggestion.
        c.scale = (Scale) forcedScale;
    else if (analysedScale)
        // A mode the analysis is not sure of is still the best candidate there
        // is; it is kept, and only the third is left open.  The threshold is
        // AnalysisResult's, so "do we know the mode?" has one answer everywhere.
        c.scale = analysis.scaleIsUsable() ? (Scale) analysis.scale
                                           : uncertainMode ((Scale) analysis.scale);
    // else: the struct's own default, and keyKnown below will be false, so it
    // is never consulted.

    c.root = rootUsable ? ((analysis.root % 12) + 12) % 12 : 0;

    /*  A key is a root AND a candidate mode.  Forcing the scale supplies the
        mode, but nothing supplies a tonic except the analysis, so a forced
        scale over an unusable root is still not a key.

        Note what is NOT here: an unconfident mode does not cost us the key.  A
        weak candidate is widened above and still constrains; only the absence
        of a root, or of any candidate at all, leaves pitch alone.  Widening
        needs something to widen, and with no candidate the only honest set is
        all twelve - which is the identity, which is what keyKnown = false
        already gives. */
    c.keyKnown = rootUsable && (forced || analysedScale);

    return c;
}

bool Context::permits (int semitone) const noexcept
{
    const juce::uint16 mask = activeMask (*this);

    return (mask & (1u << pitchClass (semitone, root))) != 0;
}

int Context::snap (int semitone) const noexcept
{
    const juce::uint16 mask = activeMask (*this);

    if (mask == kAllTwelve)
        return semitone;

    const int pc = pitchClass (semitone, root);

    if (mask & (1u << pc))
        return semitone;

    // Outward from the input, downward first: a tie between two equally distant
    // scale tones resolves to the lower one, so snapping never raises a pitch
    // the caller did not ask to raise.  No gap in any of these scales is wider
    // than three semitones, so this always finishes within one step.
    for (int d = 1; d <= 6; ++d)
    {
        const int down = (pc - d + 12) % 12;

        if (mask & (1u << down))
            return semitone - d;

        const int up = (pc + d) % 12;

        if (mask & (1u << up))
            return semitone + d;
    }

    return semitone;    // unreachable: every mask has at least the root in it
}

float Context::snapCents (float cents) const noexcept
{
    const juce::uint16 mask = activeMask (*this);

    if (mask == kAllTwelve || ! std::isfinite (cents))
        return 0.0f;

    /*  A continuous quantiser, not a staircase.  snap() may jump because it
        deals in whole semitones; this one is asked for a glide, and a glide that
        jumps is a defect you cannot mix out.

        Inside each gap between two neighbouring scale tones the map is: pinned
        to the lower tone over the first quarter, pinned to the upper tone over
        the last quarter, and a smootherstep across the middle half.  So it is
        continuous, its slope is continuous, it is monotonic, and it lands
        exactly on a scale tone for half of every gap.  The furthest it ever
        moves a pitch is 0.2984 of the gap it sits in - the extremum of
        smootherstep(u) - t, not the quarter the deadzone might suggest - which
        is 29.8 cents inside a semitone, 59.7 inside a whole tone and 89.5 at the
        augmented second in harmonic minor.  The cost is the middle: a glide
        crosses it at up to 3.75 times the rate it went in.  That is the trade a
        continuous quantiser has to make somewhere, and making it here keeps it
        away from the scale tones, where the ear is listening.

        A pitch exactly half way between two scale tones does not move at all.
        Moving it would mean choosing a direction, and that choice is exactly
        what would put a jump in the curve. */
    const float semis = cents * 0.01f - (float) root;
    const float floorOct = std::floor (semis / 12.0f);
    const float x = semis - floorOct * 12.0f;           // 0 .. 12

    int lower = (int) std::floor (x);

    while (! (mask & (1u << (((lower % 12) + 12) % 12))))
        --lower;

    int upper = lower + 1;

    while (! (mask & (1u << (((upper % 12) + 12) % 12))))
        ++upper;

    const float a = (float) lower;
    const float b = (float) upper;
    const float t = (x - a) / (b - a);

    constexpr float kDead = 0.25f;      // of the gap, pinned at each end

    float w;

    if (t <= kDead)
        w = 0.0f;
    else if (t >= 1.0f - kDead)
        w = 1.0f;
    else
    {
        const float u = (t - kDead) / (1.0f - 2.0f * kDead);
        w = u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);   // smootherstep
    }

    return ((a + (b - a) * w) - x) * 100.0f;
}

}
