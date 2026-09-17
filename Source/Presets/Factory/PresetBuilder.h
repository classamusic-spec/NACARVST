#pragma once

#include "../FactoryPresets.h"

// ===========================================================================
//  THE SHARED VOCABULARY EVERY FACTORY PRESET FILE IS WRITTEN IN.
//
//  Split out of FactoryPresets.cpp when the library outgrew one file.  What
//  lives here is the part that must be identical in all of them: the named
//  choice indices, the modulation source strings the matrix actually parses,
//  and the little builder that makes a preset read as a list of decisions
//  rather than a list of floats.
//
//  A choice index is written as `ch::mass`, never as `2.0f`, because "2.0f"
//  in a preset is not a reviewable statement and `ch::mass` is.  If you find
//  yourself typing a bare number into a CHOICE parameter, add the name here
//  first.
// ===========================================================================

namespace nacar::presets::detail
{
    // -- choice indices, named -------------------------------------------
    //  Written out rather than inferred, because "2.0f" in a preset is not
    //  a reviewable statement and "ch::mass" is.
    namespace ch
    {
        // synth_character
        constexpr float mirage = 0.0f, haze = 1.0f, mass = 2.0f;
        // voice_mode
        constexpr float poly = 0.0f, mono = 1.0f, legato = 2.0f;
        // osc_?_wave
        constexpr float sine = 0.0f, tri = 1.0f, saw = 2.0f, square = 3.0f, wt = 4.0f;
        // osc_?_phase
        constexpr float phaseFree = 0.0f, phaseReset = 1.0f, phaseRandom = 2.0f,
                        phaseControlled = 3.0f;
        // osc_?_wt_table
        constexpr float tSoft = 0.0f, tDark = 1.0f, tVocal = 2.0f, tMetal = 3.0f,
                        tAsym = 4.0f, tHollow = 5.0f, tSpectral = 6.0f, tOrganic = 7.0f;
        // sub_wave
        constexpr float subSine = 0.0f, subTri = 1.0f, subSquare = 2.0f;
        // sub_octave
        constexpr float subMinus1 = 0.0f, subMinus2 = 1.0f;
        // noise_type
        constexpr float nWhite = 0.0f, nPink = 1.0f, nDark = 2.0f, nAir = 3.0f,
                        nDust = 4.0f, nDigital = 5.0f, nTexture = 6.0f;
        // post_sat_mode
        constexpr float satSoft = 0.0f, satWarm = 1.0f, satDense = 2.0f, satEdge = 3.0f;
        // filter_model
        constexpr float fMass = 0.0f, fHaze = 1.0f, fComb = 2.0f, fFormant = 3.0f;
        // filter_type
        constexpr float lp = 0.0f, hp = 1.0f, bp = 2.0f, notch = 3.0f;
        // filter2_type / fxfilter_mode
        constexpr float f2Lp = 0.0f, f2Hp = 1.0f, f2Bp = 2.0f, f2Notch = 3.0f,
                        f2Comb = 4.0f, f2Formant = 5.0f;
        // lfo?_shape
        constexpr float lfoSine = 0.0f, lfoTri = 1.0f, lfoSawUp = 2.0f, lfoSawDown = 3.0f,
                        lfoSquare = 4.0f, lfoRandom = 5.0f, lfoSmooth = 6.0f;
        // lfo?_div, all fourteen.  A division only does anything when that
        // LFO's `lfo?_sync` BOOL is on - the rate knob is what runs otherwise.
        //  "1/32|1/16T|1/16|1/8T|1/16.|1/8|1/4T|1/8.|1/4|1/2T|1/4.|1/2|1/1|2/1"
        constexpr float d32 = 0.0f, d16t = 1.0f, d16 = 2.0f, d8t = 3.0f, d16dot = 4.0f,
                        d8 = 5.0f, d4t = 6.0f, d8dot = 7.0f, d4 = 8.0f, d2t = 9.0f,
                        d4dot = 10.0f, d2 = 11.0f, d1 = 12.0f, d2bar = 13.0f;
        // pulse_source "CLOCK|SIDECHAIN|MIDI".  Pulse needs `pulse_on` set.
        constexpr float pClock = 0.0f, pSidechain = 1.0f, pMidi = 2.0f;
        // pulse_div, all nine  "1/16|1/8T|1/16.|1/8|1/4T|1/8.|1/4|1/2|1/1"
        constexpr float p16 = 0.0f, p8t = 1.0f, p16dot = 2.0f, p8 = 3.0f, p4t = 4.0f,
                        p8dot = 5.0f, p4 = 6.0f, p2 = 7.0f, p1 = 8.0f;
        // rewind_mode / rewind_div, all six  "1/16|1/8|1/4|1/2|1 BAR|2 BAR"
        constexpr float rReverse = 0.0f, rStop = 1.0f, rDive = 2.0f, rReturn = 3.0f;
        constexpr float rw16 = 0.0f, rw8 = 1.0f, rw4 = 2.0f, rw2 = 3.0f, rw1bar = 4.0f,
                        rw2bar = 5.0f;
        // grain_pitch_mode / grain_window
        constexpr float gRoot = 0.0f, gOctave = 1.0f, gFifth = 2.0f, gThird = 3.0f,
                        gScale = 4.0f, gChromatic = 5.0f, gFree = 6.0f;
        constexpr float wHann = 0.0f, wTukey = 1.0f, wGauss = 2.0f, wExpo = 3.0f,
                        wPercussive = 4.0f;
        // space_char
        constexpr float sRoom = 0.0f, sChamber = 1.0f, sDark = 2.0f, sDistant = 3.0f,
                        sInfinite = 4.0f;
        // patina_profile
        constexpr float pSoft = 0.0f, pVintage = 1.0f, pChrome = 2.0f, pHaze = 3.0f,
                        pSmoke = 4.0f, pCustom = 5.0f;
        // memory_gen
        constexpr float genI = 0.0f, genII = 1.0f, genIII = 2.0f, genIV = 3.0f;
        // weight_mode
        constexpr float wSub = 0.0f, wBody = 1.0f, wAir = 2.0f;

        // -------------------------------------------------------------------
        //  The families that had no names, which meant presets were avoiding
        //  the parameters rather than writing bare numbers into them - which
        //  is the right instinct and the wrong outcome.
        // -------------------------------------------------------------------

        // glide_mode "TIME|RATE"
        constexpr float glideTime = 0.0f, glideRate = 1.0f;
        // osc_c_wave, the same five as A and B; osc_c_wt_table, the same eight
        // as `tSoft`..`tOrganic` above - reuse those rather than duplicating.

        // filter2_type and fxfilter_mode share `f2Lp`..`f2Formant` above.

        // quality_mode "ECO|STUDIO|ULTRA".  A preset should normally leave this
        // alone: it is the user's CPU budget, not the patch's character.
        constexpr float qEco = 0.0f, qStudio = 1.0f, qUltra = 2.0f;

        // res_material "STRING|PLATE|TUBE|BODY|METAL|GLASS"
        constexpr float mString = 0.0f, mPlate = 1.0f, mTube = 2.0f, mBody = 3.0f,
                        mMetal = 4.0f, mGlass = 5.0f;

        // grain_src_window "HANN|TUKEY|GAUSS|EXPO|PERCUSSIVE" - the granular
        // SOURCE's window, which is a different parameter from the GRAIN
        // effect's `wHann`..`wPercussive` above and happens to share its words.
        constexpr float gwHann = 0.0f, gwTukey = 1.0f, gwGauss = 2.0f, gwExpo = 3.0f,
                        gwPercussive = 4.0f;

        // -------------------------------------------------------------------
        //  root_note and scale_type.
        //
        //  These do NOT transpose anything and do NOT put the synth in a key.
        //  They are the harmony context the MUTATION engine reads: AUTO means
        //  "use what the analyser detected", and anything else forces it.  A
        //  preset that sets them is making a statement about how a mutation of
        //  it should behave, which is a legitimate thing for a preset to say -
        //  but it is not how you make a patch sound minor.  Set the intervals
        //  in the oscillators for that.
        // -------------------------------------------------------------------
        // root_note "AUTO|C|C#|D|D#|E|F|F#|G|G#|A|A#|B"
        constexpr float keyAuto = 0.0f, keyC = 1.0f, keyCs = 2.0f, keyD = 3.0f,
                        keyDs = 4.0f, keyE = 5.0f, keyF = 6.0f, keyFs = 7.0f,
                        keyG = 8.0f, keyGs = 9.0f, keyA = 10.0f, keyAs = 11.0f,
                        keyB = 12.0f;
        // scale_type "AUTO|MAJOR|MINOR|DORIAN|PHRYGIAN|LYDIAN|MIXOLYDIAN|
        //             HARMONIC MINOR|MELODIC MINOR|CHROMATIC"
        constexpr float scAuto = 0.0f, scMajor = 1.0f, scMinor = 2.0f, scDorian = 3.0f,
                        scPhrygian = 4.0f, scLydian = 5.0f, scMixolydian = 6.0f,
                        scHarmonicMinor = 7.0f, scMelodicMinor = 8.0f, scChromatic = 9.0f;
        // harmony_mode "SAFE|COLOR|FREE"
        constexpr float hSafe = 0.0f, hColour = 1.0f, hFree = 2.0f;
        // distance_mode "NEAR|FAR|UNKNOWN"
        constexpr float distNear = 0.0f, distFar = 1.0f, distUnknown = 2.0f;
        // mutation_intent "MEMORY|CLOUD|BROKEN|REVERSE|DISTANT|RHYTHMIC|DARK|
        //                  GHOST|PLAYABLE|CINEMATIC"
        constexpr float miMemory = 0.0f, miCloud = 1.0f, miBroken = 2.0f, miReverse = 3.0f,
                        miDistant = 4.0f, miRhythmic = 5.0f, miDark = 6.0f, miGhost = 7.0f,
                        miPlayable = 8.0f, miCinematic = 9.0f;

        // -------------------------------------------------------------------
        //  source_mode "SYNTH|SAMPLE|GRAIN|RESONATOR|SPECTRAL".
        //
        //  Named for completeness, and a factory preset should leave it on
        //  SYNTH.  A preset carries parameters, not audio: the other four
        //  modes need a loaded sample, so a preset that ships on one of them
        //  is a preset that is silent until the user finds a file - which is
        //  a dead control by a slower route.
        // -------------------------------------------------------------------
        //  Named `mode*` and not `src*` so they do not read as the modulation
        //  source constants below, which are a different thing entirely.
        constexpr float modeSynth = 0.0f, modeSample = 1.0f, modeGrain = 2.0f,
                        modeResonator = 3.0f, modeSpectral = 4.0f;
    }

    // -- modulation source names, verbatim from the matrix ----------------
    constexpr const char* srcLfo1    = "LFO 1";
    constexpr const char* srcLfo2    = "LFO 2";
    constexpr const char* srcBreath  = "BREATH";
    constexpr const char* srcPulse   = "PULSE";
    constexpr const char* srcOrganic = "ORGANIC RANDOM";
    constexpr const char* srcMemory  = "MEMORY";
    constexpr const char* srcMotion  = "MOTION";
    constexpr const char* srcWorld   = "WORLD";
    constexpr const char* srcWheel   = "MOD WHEEL";
    constexpr const char* srcTouch   = "AFTERTOUCH";

    /** A tiny builder, so a preset reads as a list of decisions. */
    struct Build
    {
        FactoryPreset p;

        Build (const char* name, const char* category, const char* mood,
               const char* tags, const char* blurb)
        {
            p.name     = name;
            p.category = category;
            p.mood     = mood;
            p.blurb    = blurb;
            p.tags     = juce::StringArray::fromTokens (juce::String (tags), ",", "");
            p.tags.trim();
            p.tags.removeEmptyStrings();
        }

        Build& at (int note, int chord, float vel, double secs)
        {
            p.audition = { note, chord, vel, secs };
            return *this;
        }

        Build& chain (const char* order) { p.fxOrder = order; return *this; }

        Build& v (PID pid, float value)
        {
            p.values.push_back ({ pid, value });
            return *this;
        }

        Build& route (const char* source, PID target, float depth)
        {
            p.mods.push_back ({ source, target, depth });
            return *this;
        }
    };

    /** Takes an lvalue reference on purpose: the chained setters return
        `Build&`, so `add (out, Build (...).v (...).v (...))` hands this a
        reference to the temporary, which lives until the full expression
        ends.  Binding an rvalue reference would not compile against that
        chain and a by-value parameter would copy the whole preset. */
    inline void add (std::vector<FactoryPreset>& out, Build& b)
    {
        out.push_back (std::move (b.p));
    }
}
