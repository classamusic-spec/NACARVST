#include "WavetableBank.h"

namespace nacar::synth
{
    // -----------------------------------------------------------------------
    //  Spectral recipes
    //
    //  A family is a function (frame position, harmonic number) -> (amplitude,
    //  phase in turns).  Nothing else distinguishes the eight families: the
    //  synthesis, the band limiting and the normalisation below are identical
    //  for all of them.
    //
    //  Phases default to -0.25 turns, which turns a 1/k amplitude series into a
    //  sawtooth rather than into an impulse.  That matters: cosine-phase
    //  additive synthesis piles every harmonic peak on top of the same sample
    //  and produces a waveform with a crest factor in the tens, which then
    //  normalises down to something inaudibly quiet.  Sine phase keeps the
    //  crest factor near 2 and the tables usefully loud.
    // -----------------------------------------------------------------------
    namespace
    {
        constexpr float kSinePhase = -0.25f;

        /** Fixed, family-specific pseudo-random tables.

            METALLIC and ORGANIC both want per-harmonic irregularity that is the
            same in every frame - if it changed frame to frame, sweeping the
            Position control would sound like noise modulation instead of like
            one instrument opening up. */
        struct FixedNoise
        {
            explicit FixedNoise (juce::uint32 seed)
            {
                Rng rng (seed);
                for (auto& v : values)
                    v = rng.next01();
            }

            float at (int k) const noexcept { return values[(size_t) (k & 1023)]; }

            std::array<float, 1024> values {};
        };

        const FixedNoise metallicAmp   { 0x4D45'5441u };
        const FixedNoise metallicPhase { 0x4C4C'4943u };
        const FixedNoise organicAmp    { 0x4F52'4741u };
        const FixedNoise organicPhase  { 0x4E49'4321u };

        struct Partial { float amplitude; float phaseTurns; };

        Partial softHarmonic (float p, int k)
        {
            const float kf    = (float) k;
            const float slope = 2.30f - 1.30f * p;
            const float roll  = std::exp (-kf / (3.0f + 70.0f * p));
            const float even  = (k % 2 == 0) ? (0.55f + 0.45f * p) : 1.0f;

            return { std::pow (kf, -slope) * roll * even, kSinePhase };
        }

        Partial darkDigital (float p, int k)
        {
            const float kf   = (float) k;
            const float odd  = (k % 2 == 1) ? 1.0f : (0.22f + 0.50f * p);
            const float knee = 5.0f + 35.0f * p;
            const float roll = std::exp (-std::pow (kf / knee, 1.6f));

            // Flipping every fourth harmonic hollows the waveform out without
            // changing its spectrum, which is exactly what "digital" reads as.
            const float flip = (k % 4 == 0) ? 0.5f * p : 0.0f;

            return { std::pow (kf, -1.10f) * odd * roll, kSinePhase + flip };
        }

        Partial vocal (float p, int k)
        {
            // Five vowels morphed in order U - O - A - E - I, i.e. dark to
            // bright, with the formants expressed as harmonic numbers of a
            // nominal 130 Hz voice.
            static constexpr float f1[5] { 2.31f, 4.38f, 5.62f, 4.08f, 2.08f };
            static constexpr float f2[5] { 6.69f, 6.46f, 8.38f, 14.15f, 17.62f };
            static constexpr float f3[5] { 17.2f, 18.5f, 18.8f, 19.1f, 23.2f };
            static constexpr float g2[5] { 0.42f, 0.50f, 0.62f, 0.55f, 0.40f };
            static constexpr float g3[5] { 0.12f, 0.14f, 0.17f, 0.22f, 0.26f };

            const float x  = juce::jlimit (0.0f, 3.9999f, p * 4.0f);
            const int   i0 = (int) x;
            const int   i1 = juce::jmin (4, i0 + 1);
            const float t  = x - (float) i0;

            const float c1 = lerp (f1[i0], f1[i1], t);
            const float c2 = lerp (f2[i0], f2[i1], t);
            const float c3 = lerp (f3[i0], f3[i1], t);
            const float a2 = lerp (g2[i0], g2[i1], t);
            const float a3 = lerp (g3[i0], g3[i1], t);

            const float kf = (float) k;

            auto bump = [kf] (float centre, float width, float gain)
            {
                const float d = (kf - centre) / width;
                return gain * std::exp (-0.5f * d * d);
            };

            // A glottal 1/k floor underneath the formants: three resonances on
            // their own sound like three sine waves, not like a voice.
            const float floorTerm = 0.28f * std::pow (kf, -1.45f) * std::exp (-kf / 26.0f);

            return { bump (c1, 1.7f, 1.0f) + bump (c2, 2.6f, a2) + bump (c3, 3.6f, a3) + floorTerm,
                     kSinePhase };
        }

        Partial metallic (float p, int k)
        {
            const float kf = (float) k;

            // Prime-ish harmonics carry a bell's inharmonic impression as far
            // as a strictly harmonic table can.
            const bool  chime = (k == 1 || k == 3 || k == 5 || k == 7 || k == 11
                                 || k == 13 || k == 17 || k == 19 || k == 23);

            const float base  = std::pow (kf, -0.70f) * std::exp (-kf / (18.0f + 70.0f * p));
            const float mask  = 0.18f + 0.82f * metallicAmp.at (k);

            return { base * mask * (chime ? 2.0f : 1.0f),
                     kSinePhase + p * metallicPhase.at (k) };
        }

        Partial asymmetric (float p, int k)
        {
            const float kf = (float) k;

            // Quadratic phase is a chirp, and a chirp is the most asymmetric
            // thing you can build out of a symmetric amplitude spectrum.  It is
            // what makes this family throw even harmonics the moment Body or
            // the saturator touches it.
            const float phase = kSinePhase + p * 0.013f * kf * kf;

            return { std::pow (kf, -1.20f) * std::exp (-kf / (8.0f + 50.0f * p)),
                     phase - std::floor (phase) };
        }

        Partial hollow (float p, int k)
        {
            // A pulse train of duty d has amplitude |sin(pi k d)| / k, so
            // sweeping d from a half to a sliver takes the frame from square to
            // reedy without ever leaving the band-limited world.
            const float d  = lerp (0.50f, 0.08f, p);
            const float kf = (float) k;

            return { std::abs (std::sin (kPi * kf * d)) / kf, kSinePhase };
        }

        Partial spectral (float p, int k)
        {
            // A band-pass window that slides up the harmonic series: a formant
            // sweep frozen into the table rather than performed by a filter.
            const float centre = 1.0f + 5.0f * p;                 // in octaves
            const float d      = (log2Fast ((float) k) - centre) / 0.85f;
            const float band   = std::exp (-0.5f * d * d);
            const float bed    = 0.12f * std::pow ((float) k, -1.30f);

            const float phase  = kSinePhase + 0.08f * (float) k;

            return { std::pow ((float) k, -0.90f) * band + bed,
                     phase - std::floor (phase) };
        }

        Partial organic (float p, int k)
        {
            const float kf   = (float) k;
            const float jit  = 1.0f + 0.60f * p * (organicAmp.at (k) * 2.0f - 1.0f);
            const float amp  = std::pow (kf, -1.35f) * juce::jmax (0.0f, jit);

            // Growing phase disorder is what makes this family read as several
            // slightly different instruments rather than one synthetic one.
            const float phase = kSinePhase + p * organicPhase.at (k);

            return { amp, phase - std::floor (phase) };
        }

        Partial partialFor (int family, float p, int k)
        {
            switch (family)
            {
                case 0:  return softHarmonic (p, k);
                case 1:  return darkDigital   (p, k);
                case 2:  return vocal         (p, k);
                case 3:  return metallic      (p, k);
                case 4:  return asymmetric    (p, k);
                case 5:  return hollow        (p, k);
                case 6:  return spectral      (p, k);
                default: return organic       (p, k);
            }
        }

        /** How much JUCE's inverse transform scales by, measured rather than
            assumed.

            perform() is documented as an inverse transform, not as a normalised
            one, and the factor matters here: every mip level is a different
            transform size, so guessing wrong would make the pyramid change
            level as well as bandwidth, which is audible as a step in loudness
            when a note is transposed up. */
        float inverseScaleFor (juce::dsp::FFT& fft, int n,
                               std::vector<juce::dsp::Complex<float>>& a,
                               std::vector<juce::dsp::Complex<float>>& b)
        {
            std::fill (a.begin(), a.begin() + n, juce::dsp::Complex<float> { 0.0f, 0.0f });

            a[1]              = { 0.5f, 0.0f };
            a[(size_t) n - 1] = { 0.5f, 0.0f };          // a cosine of peak 1

            fft.perform (a.data(), b.data(), true);

            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
                peak = juce::jmax (peak, std::abs (b[(size_t) i].real()));

            return (peak > 1.0e-12f) ? 1.0f / peak : 1.0f;
        }
    }

    // -----------------------------------------------------------------------
    const char* WavetableBank::familyName (int index) noexcept
    {
        static const char* names[kNumFamilies] {
            "SOFT HARMONIC", "DARK DIGITAL", "VOCAL", "METALLIC",
            "ASYMMETRIC", "HOLLOW", "SPECTRAL", "ORGANIC"
        };

        return names[juce::jlimit (0, kNumFamilies - 1, index)];
    }

    const WavetableBank& WavetableBank::shared()
    {
        // Function-local static: built once, on the first caller's thread, and
        // never touched again.  SynthEngine::prepare() is that first caller.
        static const WavetableBank instance;
        return instance;
    }

    // -----------------------------------------------------------------------
    WavetableBank::WavetableBank()
    {
        // Level L holds harmonics up to 1024 >> L.  The table shrinks with it
        // until 64 samples, below which the saving stops being worth the extra
        // interpolation error - a 64-point table still carries 32 harmonics
        // with two points per period at the very top.
        for (int l = 0; l < kNumLevels; ++l)
        {
            levelLength[(size_t) l]    = juce::jmax (64, 2048 >> l);
            levelHarmonics[(size_t) l] = juce::jmax (2, 1024 >> l);
            levelHarmonics[(size_t) l] = juce::jmin (levelHarmonics[(size_t) l],
                                                     levelLength[(size_t) l] / 2 - 1);
            levelOffset[(size_t) l]    = frameStride;
            frameStride               += levelLength[(size_t) l];
        }

        storage.assign ((size_t) (kNumFamilies * kNumFrames * frameStride), 0.0f);

        std::vector<juce::dsp::Complex<float>> spectrum ((size_t) levelLength[0]);
        std::vector<juce::dsp::Complex<float>> scratch  ((size_t) levelLength[0]);

        for (int family = 0; family < kNumFamilies; ++family)
            buildFamily (family, spectrum, scratch);
    }

    void WavetableBank::buildFamily (int family,
                                     std::vector<juce::dsp::Complex<float>>& spectrum,
                                     std::vector<juce::dsp::Complex<float>>& scratch)
    {
        float familyPeak = 0.0f;

        // Levels outermost so the transform and its measured scale factor are
        // built once each, not once per frame.
        for (int level = 0; level < kNumLevels; ++level)
        {
            const int n   = levelLength[(size_t) level];
            const int top = levelHarmonics[(size_t) level];

            int order = 0;
            while ((1 << order) < n)
                ++order;

            juce::dsp::FFT fft (order);
            const float scale = inverseScaleFor (fft, n, spectrum, scratch);

            for (int frameIndex = 0; frameIndex < kNumFrames; ++frameIndex)
            {
                const float p = (kNumFrames > 1) ? (float) frameIndex / (float) (kNumFrames - 1) : 0.0f;

                std::fill (spectrum.begin(), spectrum.begin() + n,
                           juce::dsp::Complex<float> { 0.0f, 0.0f });

                for (int k = 1; k <= top; ++k)
                {
                    const auto partial = partialFor (family, p, k);
                    if (partial.amplitude <= 0.0f)
                        continue;

                    // Real spectrum, so bin k and bin n-k are conjugates and
                    // each carries half the amplitude.
                    const float half = partial.amplitude * 0.5f;
                    const float re   = half * cosineTurns (partial.phaseTurns);
                    const float im   = half * sineTurns   (partial.phaseTurns);

                    spectrum[(size_t) k]         = { re,  im };
                    spectrum[(size_t) (n - k)]   = { re, -im };
                }

                fft.perform (spectrum.data(), scratch.data(), true);

                float* dest = storage.data()
                            + (size_t) ((family * kNumFrames + frameIndex) * frameStride
                                        + levelOffset[(size_t) level]);

                for (int i = 0; i < n; ++i)
                    dest[i] = scratch[(size_t) i].real() * scale;

                if (level == 0)
                    for (int i = 0; i < n; ++i)
                        familyPeak = juce::jmax (familyPeak, std::abs (dest[i]));
            }
        }

        // One normalisation factor for the whole family, taken from the widest
        // frame at full bandwidth.
        //
        // Normalising each frame on its own would flatten the loudness contour
        // the recipe deliberately builds - HOLLOW's narrow pulses are meant to
        // be quieter and thinner than its square, and SPECTRAL's swept band is
        // meant to lose energy as it climbs.  Normalising each mip level on its
        // own would be worse still: band limiting genuinely removes energy, and
        // hiding that would make a rising glissando get louder.
        const float norm = (familyPeak > 1.0e-9f) ? (0.99f / familyPeak) : 1.0f;

        float* base = storage.data() + (size_t) (family * kNumFrames * frameStride);

        for (int i = 0, e = kNumFrames * frameStride; i < e; ++i)
            base[i] *= norm;
    }
}
