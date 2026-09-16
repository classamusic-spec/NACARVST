#pragma once

#include "SynthCommon.h"
#include "../../../Plugin/ParameterRegistry.h"

namespace nacar::synth
{
    /** How a unison group distributes detune, pan and start phase. */
    enum class UnisonTopology
    {
        tight,      ///< small symmetric detune, narrow pan - plucks and bass
        dense,      ///< very close tuning, big body, almost no audible chorus
        wide,       ///< larger spread, modern stereo
        haze,       ///< soft asymmetric detune, organic phase distribution
        cloud       ///< irregular, wide, atmospheric
    };

    /** Which saturation curve the voice saturator leans towards before the
        user's Sat Mode is applied on top. */
    enum class SaturationBias { clean, warm, thick };

    /**
        A character is not a synthesizer.

        MIRAGE, HAZE and MASS share every oscillator, filter, envelope and voice
        in this engine.  What differs is the set of coefficients below, which
        the voice consults while it renders.  Anything that is NOT in this
        struct is identical across the three - that is the whole point, and it
        is why a patch keeps its identity when you switch character.
    */
    struct CharacterProfile
    {
        Character id = Character::haze;

        // -- unison -----------------------------------------------------
        UnisonTopology topology         = UnisonTopology::haze;
        float          detuneCentsMax   = 14.0f;   ///< cents at Detune = 100%
        float          spreadScale      = 1.0f;    ///< multiplies the Spread parameter

        // -- instability ------------------------------------------------
        float driftScale                = 1.0f;    ///< multiplies Drift
        float variationScale            = 1.0f;    ///< multiplies Variation
        float tuningPrecision           = 1.0f;    ///< 1 = exact, lower = looser

        // -- tone -------------------------------------------------------
        int   defaultFilterModel        = 1;       ///< index into MASS|HAZE|COMB|FORMANT
        float preFilterDriveScale       = 1.0f;
        float bodyLowMidGain            = 1.0f;    ///< how hard Body leans on the low mids
        float bodyEvenBias              = 0.0f;    ///< added to Body Tilt
        float densityScale              = 1.0f;
        float subReinforce              = 1.0f;    ///< multiplies Sub Level
        SaturationBias satBias          = SaturationBias::warm;

        // -- movement ---------------------------------------------------
        float wavetableMotion           = 0.0f;    ///< env 2 -> wavetable position
        float densityBreath             = 0.0f;    ///< env 2 -> density amount

        // -- envelopes --------------------------------------------------
        float attackCurve               = 0.30f;   ///< larger = straighter attack
        float decayCurve                = 0.0025f; ///< larger = straighter decay/release

        // -- stereo -----------------------------------------------------
        float widthScale                = 1.0f;    ///< multiplies Width before the stereo stage
        float lowMonoScale              = 1.0f;    ///< multiplies the low-mono crossover
        float phaseCoherence            = 0.0f;    ///< 1 = force note-synchronous phases
    };

    /** The three profiles, as constants.  No allocation, no lookup table. */
    CharacterProfile characterProfile (Character) noexcept;

    /** Maps the parameter index to the enum, defensively. */
    Character characterFromIndex (int) noexcept;

    /** The unison topology a character uses for a given voice count.

        MASS is the only one that changes its mind: two or three sub-voices want
        TIGHT (a firm, barely-moving pluck or bass), but once you ask for five
        or more the only way to stay physically large instead of becoming a
        chorus is to pull the tuning in and go DENSE. */
    UnisonTopology topologyFor (const CharacterProfile&, int unisonCount) noexcept;
}
