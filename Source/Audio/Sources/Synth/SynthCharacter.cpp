#include "SynthCharacter.h"

namespace nacar::synth
{
    // -----------------------------------------------------------------------
    //  The three profiles.
    //
    //  Every number here was chosen against one sentence from the product
    //  specification, and the sentence is quoted next to it.  If a value ever
    //  has to move, the sentence is the thing to test against.
    // -----------------------------------------------------------------------
    static const CharacterProfile mirageProfile = []
    {
        CharacterProfile p;
        p.id                 = Character::mirage;

        // "Dense, clean, high-definition, animated, precise ... favours
        //  wavetable movement, precise tuning, wider unison."
        p.topology           = UnisonTopology::wide;
        p.detuneCentsMax     = 20.0f;
        p.spreadScale        = 1.15f;

        p.driftScale         = 0.45f;   // present, but never sloppy
        p.variationScale     = 0.55f;
        p.tuningPrecision    = 1.0f;    // exact: MIRAGE is the precise one

        // "Must never automatically mean harsh, thin, bright or plastic."
        // The defence against thin is Body leaning on the low mids and the sub
        // staying intact; the defence against harsh is a clean saturation bias
        // and a smooth filter by default.
        p.defaultFilterModel = 1;       // HAZE state variable: smooth, not bitey
        p.preFilterDriveScale = 0.85f;
        p.bodyLowMidGain     = 1.05f;
        p.bodyEvenBias       = -0.10f;  // a touch odd-leaning: definition, not fatness
        p.densityScale       = 1.15f;
        p.subReinforce       = 1.0f;
        p.satBias            = SaturationBias::clean;

        p.wavetableMotion    = 0.35f;   // the animated one
        p.densityBreath      = 0.10f;

        p.attackCurve        = 0.20f;   // crisp, close to linear-in-dB
        p.decayCurve         = 0.0018f;

        p.widthScale         = 1.10f;
        p.lowMonoScale       = 1.0f;
        p.phaseCoherence     = 0.35f;   // high definition needs repeatable attacks
        return p;
    }();

    static const CharacterProfile hazeProfile = []
    {
        CharacterProfile p;
        p.id                 = Character::haze;

        // "Subtle oscillator instability, voice-to-voice variation, soft
        //  asymmetric detune, warm harmonic density, smooth SVF, organic
        //  stereo, slow internal movement, analogue-like envelopes."
        p.topology           = UnisonTopology::haze;
        p.detuneCentsMax     = 15.0f;
        p.spreadScale        = 1.0f;

        p.driftScale         = 1.25f;
        p.variationScale     = 1.30f;
        p.tuningPrecision    = 0.85f;   // deliberately not exact

        p.defaultFilterModel = 1;       // the smooth state variable
        p.preFilterDriveScale = 1.0f;
        p.bodyLowMidGain     = 1.0f;
        p.bodyEvenBias       = 0.12f;   // warm harmonic density
        p.densityScale       = 1.0f;
        p.subReinforce       = 0.9f;
        p.satBias            = SaturationBias::warm;

        p.wavetableMotion    = 0.18f;
        p.densityBreath      = 0.28f;   // the slow internal movement

        p.attackCurve        = 0.42f;   // analogue-like: softer knee both ends
        p.decayCurve         = 0.0060f;

        p.widthScale         = 1.0f;
        p.lowMonoScale       = 1.0f;
        p.phaseCoherence     = 0.0f;    // free-running phases: chords never comb
        return p;
    }();

    static const CharacterProfile massProfile = []
    {
        CharacterProfile p;
        p.id                 = Character::mass;

        // "Strong fundamental, controlled low-mid harmonics, ladder filtering,
        //  sub reinforcement, fast envelopes, pre-filter saturation, low
        //  frequency phase consistency ... physically large without depending
        //  on stereo tricks."
        p.topology           = UnisonTopology::tight;
        p.detuneCentsMax     = 9.0f;
        p.spreadScale        = 0.55f;   // narrow on purpose

        p.driftScale         = 0.65f;
        p.variationScale     = 0.60f;
        p.tuningPrecision    = 0.95f;

        p.defaultFilterModel = 0;       // the ladder
        p.preFilterDriveScale = 1.35f;
        p.bodyLowMidGain     = 1.45f;
        p.bodyEvenBias       = 0.25f;
        p.densityScale       = 1.35f;   // fullness without width is the whole trick
        p.subReinforce       = 1.25f;
        p.satBias            = SaturationBias::thick;

        p.wavetableMotion    = 0.08f;
        p.densityBreath      = 0.12f;

        p.attackCurve        = 0.14f;   // fast and punchy
        p.decayCurve         = 0.0012f;

        p.widthScale         = 0.80f;
        p.lowMonoScale       = 1.30f;   // collapse more of the bottom than the others
        p.phaseCoherence     = 1.0f;    // low-frequency phase consistency, note by note
        return p;
    }();

    CharacterProfile characterProfile (Character c) noexcept
    {
        switch (c)
        {
            case Character::mirage: return mirageProfile;
            case Character::mass:   return massProfile;
            case Character::haze:
            default:                return hazeProfile;
        }
    }

    Character characterFromIndex (int index) noexcept
    {
        switch (index)
        {
            case 0:  return Character::mirage;
            case 2:  return Character::mass;
            default: return Character::haze;
        }
    }

    UnisonTopology topologyFor (const CharacterProfile& p, int unisonCount) noexcept
    {
        if (p.id == Character::mass)
            return (unisonCount >= 5) ? UnisonTopology::dense : UnisonTopology::tight;

        // HAZE opens out into CLOUD at high voice counts: past five sub-voices
        // an asymmetric spread stops reading as detune and starts reading as
        // atmosphere, so it may as well be irregular too.
        if (p.id == Character::haze && unisonCount >= 6)
            return UnisonTopology::cloud;

        return p.topology;
    }
}
