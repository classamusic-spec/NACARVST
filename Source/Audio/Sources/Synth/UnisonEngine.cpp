#include "UnisonEngine.h"

namespace nacar::synth
{
    // -----------------------------------------------------------------------
    //  Detune distributions
    //
    //  Each topology returns offsets in -1..1, which the caller scales by the
    //  character's detuneCentsMax and by the Detune parameter.  The shape of
    //  the distribution is what distinguishes the topologies; the width is not.
    // -----------------------------------------------------------------------
    static void symmetricSpread (UnisonLayout& l, int n, float curve) noexcept
    {
        // Voices laid out either side of centre.  `curve` bends the spacing:
        // 1.0 is even, above 1.0 pushes the outer voices further out, which
        // keeps the inner pair close enough to read as one thick note while
        // still giving the group an audible edge.
        if (n == 1)
        {
            l.detune[0] = 0.0f;
            return;
        }

        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) (n - 1) * 2.0f - 1.0f;   // -1..1
            l.detune[i] = (t < 0.0f ? -1.0f : 1.0f) * std::pow (std::abs (t), curve);
        }
    }

    static void asymmetricSpread (UnisonLayout& l, int n, Rng& rng, float bias) noexcept
    {
        // HAZE.  The group is deliberately not a mirror image of itself: the
        // sharp side is spread wider than the flat side, so the beating never
        // settles into a single symmetrical pulse the way an even detune does.
        symmetricSpread (l, n, 1.0f);

        for (int i = 0; i < n; ++i)
        {
            const float side = l.detune[i] >= 0.0f ? (1.0f + bias) : (1.0f - bias);
            l.detune[i] *= side;
            l.detune[i] += rng.nextBipolar() * 0.08f;     // a little untidiness
        }
    }

    static void irregularSpread (UnisonLayout& l, int n, Rng& rng) noexcept
    {
        // CLOUD.  Stratified rather than uniform: one voice per band, jittered
        // within it.  Purely uniform draws clump, and a clump of three voices a
        // cent apart is a phaser, not weather.
        for (int i = 0; i < n; ++i)
        {
            const float lo = (float) i / (float) n * 2.0f - 1.0f;
            const float hi = (float) (i + 1) / (float) n * 2.0f - 1.0f;
            l.detune[i] = lerp (lo, hi, rng.next01());
        }
    }

    void UnisonEngine::buildLayout (UnisonLayout& layout, UnisonTopology topology,
                                    int count, float detuneNorm, juce::uint32 seed) noexcept
    {
        const int n = juce::jlimit (1, kMaxUnison, count);
        layout.count = n;

        Rng rng (seed);

        switch (topology)
        {
            case UnisonTopology::tight: symmetricSpread (layout, n, 1.0f);       break;
            case UnisonTopology::dense: symmetricSpread (layout, n, 1.6f);       break;
            case UnisonTopology::wide:  symmetricSpread (layout, n, 0.8f);       break;
            case UnisonTopology::haze:  asymmetricSpread (layout, n, rng, 0.35f); break;
            case UnisonTopology::cloud: irregularSpread (layout, n, rng);        break;
        }

        // -------------------------------------------------------------------
        //  Stereo placement
        //
        //  Pan is correlated with detune, not independent of it, and that is
        //  the single decision that makes a unison group sound like one wide
        //  instrument rather than several narrow ones: the sharp voices sit to
        //  one side and the flat voices to the other, so the beating sweeps
        //  across the image instead of pulsing in place.
        //
        //  TIGHT and DENSE compress that mapping hard, because their job is to
        //  stay one object; CLOUD decorrelates it, because its job is not.
        // -------------------------------------------------------------------
        const float panFromDetune = (topology == UnisonTopology::tight) ? 0.25f
                                  : (topology == UnisonTopology::dense) ? 0.40f
                                  : (topology == UnisonTopology::cloud) ? 0.35f
                                                                        : 1.0f;

        for (int i = 0; i < n; ++i)
        {
            float p = layout.detune[i] * panFromDetune;

            if (topology == UnisonTopology::cloud)
                p += rng.nextBipolar() * 0.65f;

            // A single sub-voice is always centred: one voice panned hard right
            // because its detune index happened to be positive would be a bug,
            // not a feature.
            layout.pan[i] = (n == 1) ? 0.0f : juce::jlimit (-1.0f, 1.0f, p);
        }

        // -------------------------------------------------------------------
        //  Start phase
        //
        //  Sub-voices that all start at phase zero sum to N times one voice for
        //  the first few milliseconds, which is a click, and then drift apart -
        //  so the attack of a unison patch is louder and brighter than its
        //  body. Distributing the start phases evenly and then jittering fixes
        //  the transient without making the attack unrepeatable.
        //
        //  TIGHT keeps its voices closer together in phase on purpose: a MASS
        //  pluck wants a firm, coherent front edge, and 8 voices scattered
        //  across the cycle soften exactly the thing that makes it a pluck.
        // -------------------------------------------------------------------
        const float phaseScatter = (topology == UnisonTopology::tight) ? 0.18f
                                 : (topology == UnisonTopology::dense) ? 0.35f
                                                                       : 1.0f;

        for (int i = 0; i < n; ++i)
        {
            const float even = (n == 1) ? 0.0f : (float) i / (float) n;
            layout.phase[i] = std::fmod (even * phaseScatter + rng.next01() * 0.05f + 1.0f, 1.0f);
        }

        // -------------------------------------------------------------------
        //  Normalise the detune shape so that "Detune at 100%" means the same
        //  audible width whatever the topology, and record the widest offset so
        //  the gain law below can ask how fast the group beats.
        // -------------------------------------------------------------------
        float maxOffset = 0.0f;
        for (int i = 0; i < n; ++i)
            maxOffset = juce::jmax (maxOffset, std::abs (layout.detune[i]));

        if (maxOffset > 1.0e-6f)
        {
            const float scale = 1.0f / maxOffset;
            for (int i = 0; i < n; ++i)
                layout.detune[i] *= scale;
        }

        for (int i = 0; i < n; ++i)
            layout.detune[i] *= juce::jlimit (0.0f, 1.0f, detuneNorm);

        layout.maxOffsetNorm = juce::jlimit (0.0f, 1.0f, detuneNorm);

        for (int i = n; i < kMaxUnison; ++i)
        {
            layout.detune[i] = 0.0f;
            layout.pan[i]    = 0.0f;
            layout.phase[i]  = 0.0f;
        }
    }

    float UnisonEngine::normalisation (int count, float maxDetuneCents,
                                       float fundamentalHz) noexcept
    {
        const int n = juce::jlimit (1, kMaxUnison, count);

        if (n == 1)
            return 1.0f;

        // How fast the outermost pair beats against the centre, in Hz.
        const float f0       = juce::jlimit (20.0f, 20000.0f, fundamentalHz);
        const float spreadHz = f0 * (centsToRatio (std::abs (maxDetuneCents)) - 1.0f);

        // Correlation: 1 when the voices are effectively identical, falling
        // towards 0 once they beat faster than the ear integrates.
        constexpr float tau = 0.08f;
        const float wt = spreadHz * tau;
        const float c  = 1.0f / (1.0f + wt * wt);

        // Exponent runs from -0.5 (independent, power sum) to -1.0 (identical,
        // amplitude sum).
        const float exponent = -(0.5f + 0.5f * juce::jlimit (0.0f, 1.0f, c));

        return exp2Fast (exponent * log2Fast ((float) n));
    }
}
