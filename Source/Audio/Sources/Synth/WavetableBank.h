#pragma once

#include <juce_dsp/juce_dsp.h>

#include "SynthCommon.h"

#include <array>
#include <vector>

namespace nacar::synth
{
    /**
        THE FACTORY WAVETABLES.

        Eight families, built procedurally at startup from a per-family spectral
        recipe.  NACAR ships no wavetable assets: the tables are a pure function
        of the code in WavetableBank.cpp, so they cannot go missing, cannot be
        version-skewed against a preset, and cost nothing to install.

        Layout.  Each family holds kNumFrames frames.  Each frame holds a mip
        pyramid of kNumLevels progressively band-limited copies of itself, and
        level L contains no harmonic above (1024 >> L).  An oscillator picks the
        level whose highest harmonic still fits below Nyquist for the note it is
        playing, so a table never aliases however high it is transposed.

        The bank is immutable and shared.  It is built exactly once, by the
        first call to shared(), which SynthEngine::prepare() makes on the
        message thread.  Every voice then reads from the same memory - building
        a table per voice would be thirty-two times the work and thirty-two
        times the cache pressure for identical data.
    */
    class WavetableBank
    {
    public:
        static constexpr int kNumFamilies = 8;
        static constexpr int kNumFrames   = 12;
        static constexpr int kNumLevels   = 10;

        /** The one instance.  Built on first use; safe to call from prepare(),
            never called from the audio thread. */
        static const WavetableBank& shared();

        /** Start of one band-limited frame.  Length is lengthOf (level) and the
            table is a power of two, so wrap with maskOf (level). */
        const float* frame (int family, int frameIndex, int level) const noexcept
        {
            const int f = juce::jlimit (0, kNumFamilies - 1, family);
            const int n = juce::jlimit (0, kNumFrames   - 1, frameIndex);
            const int l = juce::jlimit (0, kNumLevels   - 1, level);

            return storage.data() + (size_t) ((f * kNumFrames + n) * frameStride + levelOffset[(size_t) l]);
        }

        int lengthOf (int level) const noexcept { return levelLength[(size_t) juce::jlimit (0, kNumLevels - 1, level)]; }
        int maskOf   (int level) const noexcept { return lengthOf (level) - 1; }

        /** Fractional mip level for a phase increment of `inc` cycles/sample.

            Level L tops out at harmonic 1024 >> L, and the highest harmonic
            that fits below Nyquist is 0.5 / inc, so the first safe level is
            log2 (2048 * inc).  Returned unfloored so the caller can cross-fade
            across the boundary rather than click through it. */
        static float levelForIncrement (float inc) noexcept
        {
            return juce::jlimit (0.0f, (float) (kNumLevels - 1),
                                 log2Fast (juce::jmax (1.0e-9f, 2048.0f * std::abs (inc))));
        }

        /** Names, in the order of the osc_x_wt_table choice list. */
        static const char* familyName (int) noexcept;

    private:
        WavetableBank();

        void buildFamily (int family, std::vector<juce::dsp::Complex<float>>& spectrum,
                          std::vector<juce::dsp::Complex<float>>& scratch);

        std::vector<float> storage;
        std::array<int, kNumLevels> levelLength {};
        std::array<int, kNumLevels> levelOffset {};
        std::array<int, kNumLevels> levelHarmonics {};
        int frameStride = 0;

        JUCE_DECLARE_NON_COPYABLE (WavetableBank)
    };
}
