#pragma once

#include <juce_core/juce_core.h>

#include <array>

#include "../Analysis/AnalysisResult.h"

namespace nacar::harmony
{
    /** The scale vocabulary, in the order the scaleType parameter lists them
        after AUTO. Adding one means adding it in both places. */
    enum class Scale
    {
        major = 0, minor, dorian, phrygian, lydian, mixolydian,
        harmonicMinor, melodicMinor, chromatic, count
    };

    /** The twelve semitone offsets of a scale as a bitmask, bit 0 = root. */
    juce::uint16 maskOf (Scale) noexcept;

    const char* nameOf (Scale) noexcept;

    /**
        HOW FAR A TRANSFORMATION MAY STRAY - master spec's HARMONY control.

        This is the difference between an instrument that mutates musically and
        one that mutates randomly, and it is the whole reason phase 20 exists
        separately from phase 21.
    */
    enum class Mode
    {
        safe,   ///< stay in the detected key. Every pitch snaps to the scale.
        colour, ///< the scale, plus borrowed tones that stay consonant with it
        free    ///< no constraint. Chromatic, and the user asked for it.
    };

    /**
        What the harmony engine knows while a mutation is being generated.

        Built once from an AnalysisResult and the user's controls, then consulted
        for every pitch decision. It is deliberately cheap to copy and contains
        no state that changes during a mutation.
    */
    struct Context
    {
        int   root  = 0;          ///< 0..11
        Scale scale = Scale::minor;
        Mode  mode  = Mode::safe;

        /** False when the analysis could not find a key. A SAFE mutation on an
            unknown key must NOT invent one and snap everything to C minor -
            it falls back to leaving pitch alone, which is the honest reading
            of "stay in the detected key" when there is no detected key. */
        bool keyKnown = false;

        /** Builds one from an analysis result and the two user controls.
            `forcedScale` is the scaleType parameter: negative means AUTO, in
            which case the analysis decides. */
        static Context from (const AnalysisResult&, Mode, int forcedScale);

        /** The nearest pitch this context permits, in semitones from the root's
            octave. Under Mode::free, or when the key is unknown under
            Mode::safe, this returns `semitone` unchanged. */
        int snap (int semitone) const noexcept;

        /** The same in cents, for a continuous transposition: returns how far
            to move, which is zero when no constraint applies. */
        float snapCents (float cents) const noexcept;

        /** True when a semitone is in the permitted set. */
        bool permits (int semitone) const noexcept;
    };

    /**
        Key detection from a chroma vector.

        Separated from the analyser so that it can be tested against synthetic
        chroma - a correlation this small is easy to get subtly wrong, and a
        test that feeds it a known profile is worth more than one that feeds it
        a real file and eyeballs the answer.

        `chroma` is twelve energies, C first, in any consistent unit.
        Returns root -1 and confidence 0 when nothing correlates well enough.
    */
    struct Detection
    {
        int   root = -1;
        Scale scale = Scale::minor;
        float rootConfidence = 0.0f;
        float scaleConfidence = 0.0f;
    };

    Detection detectKey (const std::array<float, 12>& chroma) noexcept;
}
