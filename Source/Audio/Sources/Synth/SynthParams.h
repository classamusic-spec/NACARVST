#pragma once

#include "SynthCharacter.h"
#include "SynthCommon.h"
#include "SynthFilter.h"
#include "WavetableBank.h"

namespace nacar::synth
{
    /**
        One block's worth of settings, read from the registry once and handed to
        every voice by const reference.

        Two kinds of field live here.  Discrete choices - waveform, filter
        model, phase mode - are plain values, because they only change when the
        user changes them and a one-sample switch is what the user asked for.
        Continuous quantities that reach the audio path are `Ramp`s, evaluated
        by each voice at its own sample index, so a knob move is a smooth
        trajectory rather than a step at a block boundary.

        Nothing in here is owned by a voice.  The engine builds it, the voices
        read it, and it is rebuilt from scratch every block.
    */
    struct OscSettings
    {
        Waveform  wave       = Waveform::saw;
        PhaseMode phaseMode  = PhaseMode::controlled;
        float     phaseOffset = 0.0f;

        float pitchOffsetSemitones = 0.0f;   ///< octave + semitone + fine, combined

        int   unisonCount = 1;
        float detune = 0.0f;                 ///< 0..1
        float spread = 0.5f;                 ///< 0..1

        int   wtFamily = 0;

        Ramp  level;
        Ramp  pan;
        Ramp  wtPosition;
        Ramp  pulseWidth;
    };

    struct SynthBlockParams
    {
        double sampleRate = 48000.0;
        int    numSamples = 0;
        Quality quality = Quality::studio;

        CharacterProfile character {};
        const WavetableBank* bank = nullptr;

        // -- sources --------------------------------------------------------
        OscSettings oscA, oscB, oscC;

        int   subWave = 0;
        int   subOctave = 0;                  ///< 0 = -1 oct, 1 = -2 oct
        Ramp  subLevel;
        Ramp  subHarmonics;

        int   noiseType = 1;
        Ramp  noiseLevel;
        float noiseAttackOnly = 0.0f;

        Ramp  sync, fm, pm, ringMod;

        // -- shaping --------------------------------------------------------
        Ramp  body, bodyTilt, density, preDrive, postSat;
        int   postSatMode = 1;

        // -- filter ---------------------------------------------------------
        FilterModel filterModel = FilterModel::haze;
        FilterType  filterType  = FilterType::lowpass;
        Ramp  filterCutoffLog2;               ///< log2(Hz), so a sweep is musical
        Ramp  filterResonance, filterDrive;
        float filterKeyTrack = 0.35f;
        float filterEnvAmount = 0.3f;
        float filterVelAmount = 0.35f;

        bool        filter2On = false;
        FilterModel filter2Model = FilterModel::haze;
        FilterType  filter2Type = FilterType::notch;
        Ramp  filter2CutoffLog2, filter2Resonance, filter2Mix;

        // -- envelopes ------------------------------------------------------
        float ampAttack = 0.004f, ampDecay = 0.6f, ampSustain = 0.8f, ampRelease = 0.45f;
        float ampVelocity = 0.55f;
        float env1Attack = 0.002f, env1Decay = 0.35f, env1Sustain = 0.3f, env1Release = 0.4f;
        float env2Attack = 0.9f,   env2Decay = 1.5f,  env2Sustain = 0.6f, env2Release = 2.0f;

        // -- instability ----------------------------------------------------
        float variation = 0.3f;
        float driftAmount = 0.25f;
        float driftRate = 0.12f;

        // -- performance ----------------------------------------------------
        Ramp  pitchBendSemitones;
        Ramp  modWheel;
        Ramp  aftertouch;
        float modWheelDepth = 0.5f;
        float aftertouchDepth = 0.4f;
        float vibratoRate = 5.2f;
        float vibratoDepth = 0.0f;

        float glideTimeSeconds = 0.0f;
        bool  glideConstantRate = false;
    };
}
