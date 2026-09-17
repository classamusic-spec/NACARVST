#pragma once

/**
    THE NACAR PARAMETER LIST.

    Every host-visible parameter in the instrument is declared exactly once,
    here, through the NACAR_PARAMETERS X-macro.  The enum, the APVTS layout, the
    cached audio-thread pointer table and the tooltip text are all generated
    from this single list, so they can never drift apart.

    ------------------------------------------------------------------------
    THE STRING IDS IN THIS FILE ARE PERMANENT.

    Once V1 ships, an ID may never be renamed, reordered out of existence or
    reused for a different meaning - a host has stored it in a session file and
    an automation lane is pointing at it.  Adding new parameters at the end of a
    group is always safe.  Deleting one is not: retire it in place instead.
    ------------------------------------------------------------------------

    Macro arities:

      FLOAT (member, id, name, min, max, default, skew, unit, tooltip)
      CHOICE(member, id, name, "A|B|C", defaultIndex, tooltip)
      BOOL  (member, id, name, default, tooltip)

    `skew` is JUCE's NormalisableRange exponential skew factor: 1.0 is linear,
    values below 1.0 push resolution towards the bottom of the range (times,
    rates, cutoffs), values above 1.0 towards the top.
*/

// clang-format off
#define NACAR_PARAMETERS(FLOAT, CHOICE, BOOL)                                                                                                    \
                                                                                                                                                 \
/* ====================================================================== */                                                                     \
/*  MACROS - the left panel.  These are the performance surface.          */                                                                     \
/* ====================================================================== */                                                                     \
FLOAT (macroMemory,    "macro_memory",    "Memory",        0.0f,  1.0f, 0.22f, 1.0f, "%",  "How much recorded history the sound carries.")        \
CHOICE(memoryGen,      "memory_gen",      "Generation",    "I|II|III|IV", 0,         "Which generation of copy you are hearing.")                 \
FLOAT (macroCharacter, "macro_character", "Character",     0.0f,  1.0f, 0.30f, 1.0f, "%",  "Clean to worn. Saturation, bandwidth, transients.")   \
FLOAT (macroMotion,    "macro_motion",    "Motion",        0.0f,  1.0f, 0.35f, 1.0f, "%",  "Still to alive. Drift, movement, internal life.")     \
FLOAT (macroWorld,     "macro_world",     "World",         0.0f,  1.0f, 0.40f, 1.0f, "%",  "Intimate to expansive. The scale of the room.")       \
FLOAT (macroWeight,    "macro_weight",    "Weight",        0.0f,  1.0f, 0.35f, 1.0f, "%",  "Physical mass in the chosen band.")                   \
CHOICE(weightMode,     "weight_mode",     "Weight Mode",   "SUB|BODY|AIR", 1,        "Which part of the spectrum Weight acts on.")                \
FLOAT (macroAlter,     "macro_alter",     "Alter",         0.0f,  1.0f, 0.0f,  1.0f, "%",  "Morph towards the preset's alternate identity.")      \
FLOAT (randomAmount,   "random_amount",   "Random",        0.0f,  1.0f, 0.25f, 1.0f, "%",  "How far the Random control may wander. Not Mutate.")  \
                                                                                                                                                 \
/* ====================================================================== */                                                                     \
/*  GLOBAL                                                                */                                                                     \
/* ====================================================================== */                                                                     \
FLOAT (masterGain,     "master_gain",     "Output",      -60.0f, 12.0f, 0.0f,  3.2f, "dB", "Instrument output level.")                            \
CHOICE(qualityMode,    "quality_mode",    "Quality",       "ECO|STUDIO|ULTRA", 1,    "Realtime quality. Offline rendering always uses maximum.")  \
CHOICE(sourceMode,     "source_mode",     "Source",        "SYNTH|SAMPLE|GRAIN|RESONATOR|SPECTRAL", 0, "Which engine generates the sound.")       \
                                                                                                                                                 \
/* ====================================================================== */                                                                     \
/*  SYNTH - character and voicing                                         */                                                                     \
/* ====================================================================== */                                                                     \
CHOICE(synthCharacter, "synth_character", "Synth Character", "MIRAGE|HAZE|MASS", 1,  "MIRAGE digital dimension, HAZE atmosphere, MASS weight.")   \
CHOICE(voiceMode,      "voice_mode",      "Voice Mode",    "POLY|MONO|LEGATO", 0,    "Polyphonic, retriggered mono, or legato mono.")             \
FLOAT (polyphony,      "polyphony",       "Polyphony",     1.0f, 32.0f, 16.0f, 1.0f, "",   "Maximum simultaneous voices.")                        \
FLOAT (glideTime,      "glide_time",      "Glide",         0.0f,  2.0f, 0.0f,  0.35f,"s",  "Portamento time between notes.")                      \
CHOICE(glideMode,      "glide_mode",      "Glide Mode",    "TIME|RATE", 0,           "Constant time, or constant semitones per second.")          \
FLOAT (pitchBendRange, "bend_range",      "Bend Range",    0.0f, 24.0f, 2.0f,  1.0f, "st", "Pitch bend range in semitones.")                      \
FLOAT (voiceVariation, "voice_variation", "Variation",     0.0f,  1.0f, 0.30f, 1.0f, "%",  "Deterministic voice-to-voice difference.")            \
FLOAT (driftAmount,    "drift_amount",    "Drift",         0.0f,  1.0f, 0.25f, 1.0f, "%",  "Slow bounded analogue-style wander.")                 \
FLOAT (driftRate,      "drift_rate",      "Drift Rate",    0.01f, 2.0f, 0.12f, 0.3f, "Hz", "How fast the drift wanders.")                         \
                                                                                                                                                 \
/* ---- Oscillator A ---------------------------------------------------- */                                                                     \
CHOICE(oscAWave,       "osc_a_wave",      "A Wave",        "SINE|TRIANGLE|SAW|PULSE|WAVETABLE", 2, "Oscillator A waveform.")                      \
FLOAT (oscALevel,      "osc_a_level",     "A Level",       0.0f,  1.0f, 0.80f, 1.0f, "%",  "Oscillator A level into the voice mixer.")            \
FLOAT (oscAOctave,     "osc_a_octave",    "A Octave",     -4.0f,  4.0f, 0.0f,  1.0f, "oct","Oscillator A octave.")                                \
FLOAT (oscASemi,       "osc_a_semi",      "A Semitone",  -12.0f, 12.0f, 0.0f,  1.0f, "st", "Oscillator A semitone offset.")                       \
FLOAT (oscAFine,       "osc_a_fine",      "A Fine",      -50.0f, 50.0f, 0.0f,  1.0f, "ct", "Oscillator A fine tuning.")                           \
FLOAT (oscAPan,        "osc_a_pan",       "A Pan",        -1.0f,  1.0f, 0.0f,  1.0f, "",   "Oscillator A stereo position.")                       \
CHOICE(oscAPhase,      "osc_a_phase",     "A Phase",       "FREE|RESET|RANDOM|CONTROLLED", 3, "Start phase behaviour per note.")                  \
FLOAT (oscAPhaseOffset,"osc_a_phase_off", "A Phase Offset",0.0f,  1.0f, 0.0f,  1.0f, "",   "Fixed start phase when phase mode is RESET.")         \
FLOAT (oscAWtPos,      "osc_a_wt_pos",    "A Position",    0.0f,  1.0f, 0.0f,  1.0f, "%",  "Wavetable frame position.")                           \
CHOICE(oscAWtTable,    "osc_a_wt_table",  "A Table",       "SOFT HARMONIC|DARK DIGITAL|VOCAL|METALLIC|ASYMMETRIC|HOLLOW|SPECTRAL|ORGANIC", 0, "Wavetable family.") \
FLOAT (oscAPulseWidth, "osc_a_pw",        "A Pulse Width", 0.02f, 0.98f, 0.5f, 1.0f, "%",  "Pulse width for the PULSE waveform.")                 \
FLOAT (oscAUnison,     "osc_a_unison",    "A Unison",      1.0f,  8.0f, 1.0f,  1.0f, "",   "Unison sub-voices on oscillator A.")                  \
FLOAT (oscADetune,     "osc_a_detune",    "A Detune",      0.0f,  1.0f, 0.22f, 1.0f, "%",  "Unison detune spread.")                               \
FLOAT (oscASpread,     "osc_a_spread",    "A Spread",      0.0f,  1.0f, 0.50f, 1.0f, "%",  "Unison stereo spread.")                               \
                                                                                                                                                 \
/* ---- Oscillator B ---------------------------------------------------- */                                                                     \
CHOICE(oscBWave,       "osc_b_wave",      "B Wave",        "SINE|TRIANGLE|SAW|PULSE|WAVETABLE", 2, "Oscillator B waveform.")                      \
FLOAT (oscBLevel,      "osc_b_level",     "B Level",       0.0f,  1.0f, 0.55f, 1.0f, "%",  "Oscillator B level into the voice mixer.")            \
FLOAT (oscBOctave,     "osc_b_octave",    "B Octave",     -4.0f,  4.0f, 0.0f,  1.0f, "oct","Oscillator B octave.")                                \
FLOAT (oscBSemi,       "osc_b_semi",      "B Semitone",  -12.0f, 12.0f, 0.0f,  1.0f, "st", "Oscillator B semitone offset.")                       \
FLOAT (oscBFine,       "osc_b_fine",      "B Fine",      -50.0f, 50.0f, 7.0f,  1.0f, "ct", "Oscillator B fine tuning. Small values make Reese.")  \
FLOAT (oscBPan,        "osc_b_pan",       "B Pan",        -1.0f,  1.0f, 0.0f,  1.0f, "",   "Oscillator B stereo position.")                       \
CHOICE(oscBPhase,      "osc_b_phase",     "B Phase",       "FREE|RESET|RANDOM|CONTROLLED", 3, "Start phase behaviour per note.")                  \
FLOAT (oscBPhaseOffset,"osc_b_phase_off", "B Phase Offset",0.0f,  1.0f, 0.0f,  1.0f, "",   "Fixed start phase when phase mode is RESET.")         \
FLOAT (oscBWtPos,      "osc_b_wt_pos",    "B Position",    0.0f,  1.0f, 0.0f,  1.0f, "%",  "Wavetable frame position.")                           \
CHOICE(oscBWtTable,    "osc_b_wt_table",  "B Table",       "SOFT HARMONIC|DARK DIGITAL|VOCAL|METALLIC|ASYMMETRIC|HOLLOW|SPECTRAL|ORGANIC", 1, "Wavetable family.") \
FLOAT (oscBPulseWidth, "osc_b_pw",        "B Pulse Width", 0.02f, 0.98f, 0.5f, 1.0f, "%",  "Pulse width for the PULSE waveform.")                 \
FLOAT (oscBUnison,     "osc_b_unison",    "B Unison",      1.0f,  8.0f, 1.0f,  1.0f, "",   "Unison sub-voices on oscillator B.")                  \
FLOAT (oscBDetune,     "osc_b_detune",    "B Detune",      0.0f,  1.0f, 0.22f, 1.0f, "%",  "Unison detune spread.")                               \
FLOAT (oscBSpread,     "osc_b_spread",    "B Spread",      0.0f,  1.0f, 0.50f, 1.0f, "%",  "Unison stereo spread.")                               \
                                                                                                                                                 \
/* ---- Auxiliary oscillator -------------------------------------------- */                                                                     \
CHOICE(oscCWave,       "osc_c_wave",      "C Wave",        "SINE|TRIANGLE|SAW|PULSE|WAVETABLE", 0, "Auxiliary oscillator waveform.")              \
FLOAT (oscCLevel,      "osc_c_level",     "C Level",       0.0f,  1.0f, 0.0f,  1.0f, "%",  "Auxiliary oscillator level.")                         \
FLOAT (oscCOctave,     "osc_c_octave",    "C Octave",     -4.0f,  4.0f, 1.0f,  1.0f, "oct","Auxiliary oscillator octave.")                        \
FLOAT (oscCSemi,       "osc_c_semi",      "C Semitone",  -12.0f, 12.0f, 0.0f,  1.0f, "st", "Auxiliary oscillator semitone offset.")               \
FLOAT (oscCFine,       "osc_c_fine",      "C Fine",      -50.0f, 50.0f, 0.0f,  1.0f, "ct", "Auxiliary oscillator fine tuning.")                   \
FLOAT (oscCPan,        "osc_c_pan",       "C Pan",        -1.0f,  1.0f, 0.0f,  1.0f, "",   "Auxiliary oscillator stereo position.")               \
FLOAT (oscCWtPos,      "osc_c_wt_pos",    "C Position",    0.0f,  1.0f, 0.0f,  1.0f, "%",  "Wavetable frame position.")                           \
CHOICE(oscCWtTable,    "osc_c_wt_table",  "C Table",       "SOFT HARMONIC|DARK DIGITAL|VOCAL|METALLIC|ASYMMETRIC|HOLLOW|SPECTRAL|ORGANIC", 6, "Wavetable family.") \
                                                                                                                                                 \
/* ---- Oscillator interaction ------------------------------------------ */                                                                     \
FLOAT (oscSync,        "osc_sync",        "Sync",          0.0f,  1.0f, 0.0f,  1.0f, "%",  "Hard-syncs B to A. Bandlimited.")                     \
FLOAT (oscFmAmount,    "osc_fm",          "FM",            0.0f,  1.0f, 0.0f,  1.0f, "%",  "B frequency-modulates A at audio rate.")              \
FLOAT (oscPmAmount,    "osc_pm",          "PM",            0.0f,  1.0f, 0.0f,  1.0f, "%",  "B phase-modulates A at audio rate.")                  \
FLOAT (oscRingMod,     "osc_ring",        "Ring Mod",      0.0f,  1.0f, 0.0f,  1.0f, "%",  "Ring modulation between A and B.")                    \
                                                                                                                                                 \
/*  Mod envelope 2 into pitch and into the PM index.                                                                                             */\
/*                                                                                                                                               */\
/*  These exist because envelope 1 could only reach the filter, which put two                                                                    */\
/*  ordinary sounds out of reach: a kick or 808 whose pitch falls on the attack,                                                                 */\
/*  and a tine electric piano whose FM index decays while the note rings. Both                                                                   */\
/*  were being faked with a resonant filter sweep, which is a different thing.                                                                   */\
/*                                                                                                                                               */\
/*  Envelope 2 and not envelope 1 on purpose: the tine needs its index to decay                                                                  */\
/*  fast while the filter opens slowly, so the two destinations need separate                                                                    */\
/*  timing. Both default to zero, so no existing patch changes.                                                                                  */\
FLOAT (pitchEnvAmount, "pitch_env_amt",   "Pitch Env",   -48.0f, 48.0f, 0.0f,  1.0f, "st", "Mod envelope 2 into oscillator pitch.")               \
FLOAT (pmEnvAmount,    "pm_env_amt",      "PM Env",       -1.0f,  1.0f, 0.0f,  1.0f, "",   "Mod envelope 2 into the PM index.")                   \
                                                                                                                                                 \
/* ---- Sub and noise --------------------------------------------------- */                                                                     \
CHOICE(subWave,        "sub_wave",        "Sub Wave",      "SINE|TRIANGLE|SOFT SQUARE", 0, "Sub oscillator waveform.")                            \
FLOAT (subLevel,       "sub_level",       "Sub Level",     0.0f,  1.0f, 0.35f, 1.0f, "%",  "Sub oscillator level. Always mono-compatible.")       \
CHOICE(subOctave,      "sub_octave",      "Sub Octave",    "-1|-2", 0,               "How far below the note the sub sits.")                      \
FLOAT (subHarmonics,   "sub_harmonics",   "Sub Harmonics", 0.0f,  1.0f, 0.15f, 1.0f, "%",  "Adds audible harmonics so the sub survives small speakers.") \
CHOICE(noiseType,      "noise_type",      "Noise",         "WHITE|PINK|DARK|AIR|DUST|DIGITAL|TEXTURE", 1, "Noise / exciter colour.")              \
FLOAT (noiseLevel,     "noise_level",     "Noise Level",   0.0f,  1.0f, 0.0f,  1.0f, "%",  "Noise level into the voice mixer.")                   \
FLOAT (noiseAttackOnly,"noise_attack",    "Noise Attack",  0.0f,  1.0f, 0.0f,  1.0f, "%",  "Confines noise to the attack, for exciters.")         \
                                                                                                                                                 \
/* ---- Body / density / drive ------------------------------------------ */                                                                     \
FLOAT (bodyAmount,     "body_amount",     "Body",          0.0f,  1.0f, 0.30f, 1.0f, "%",  "Physical size. Low settings read as bigger, not dirtier.") \
FLOAT (bodyTilt,       "body_tilt",       "Body Tilt",    -1.0f,  1.0f, 0.0f,  1.0f, "",   "Even against odd harmonic emphasis in the Body stage.") \
FLOAT (densityAmount,  "density_amount",  "Density",       0.0f,  1.0f, 0.25f, 1.0f, "%",  "Perceived fullness without extra stereo width.")      \
FLOAT (preFilterDrive, "pre_drive",       "Drive",         0.0f,  1.0f, 0.15f, 1.0f, "%",  "Gain-compensated drive before the filter.")           \
FLOAT (postSaturation, "post_sat",        "Saturation",    0.0f,  1.0f, 0.12f, 1.0f, "%",  "Restrained nonlinear finishing after the filter.")    \
CHOICE(postSatMode,    "post_sat_mode",   "Sat Mode",      "SOFT|WARM|DENSE|EDGE", 1, "Saturation character.")                                    \
                                                                                                                                                 \
/* ---- Primary filter -------------------------------------------------- */                                                                     \
CHOICE(filterModel,    "filter_model",    "Filter Model",  "MASS|HAZE|COMB|FORMANT", 1, "Ladder-style, state variable, comb or formant.")         \
CHOICE(filterType,     "filter_type",     "Filter Type",   "LP|HP|BP|NOTCH", 0,      "Response of the primary filter.")                           \
FLOAT (filterCutoff,   "filter_cutoff",   "Cutoff",       20.0f, 20000.0f, 8000.0f, 0.25f, "Hz", "Primary filter cutoff.")                        \
FLOAT (filterResonance,"filter_res",      "Resonance",     0.0f,  1.0f, 0.15f, 1.0f, "%",  "Primary filter resonance.")                           \
FLOAT (filterDrive,    "filter_drive",    "Filter Drive",  0.0f,  1.0f, 0.20f, 1.0f, "%",  "Nonlinearity inside the filter.")                     \
FLOAT (filterKeyTrack, "filter_keytrack", "Key Track",     0.0f,  2.0f, 0.35f, 1.0f, "x",  "How far cutoff follows the note. 1.0 tracks exactly.") \
FLOAT (filterEnvAmount,"filter_env_amt",  "Filter Env",   -1.0f,  1.0f, 0.30f, 1.0f, "",   "Mod envelope 1 into cutoff.")                         \
FLOAT (filterVelAmount,"filter_vel_amt",  "Filter Vel",    0.0f,  1.0f, 0.35f, 1.0f, "%",  "Velocity into cutoff.")                               \
                                                                                                                                                 \
/* ---- Creative filter ------------------------------------------------- */                                                                     \
BOOL  (filter2On,      "filter2_on",      "Filter 2",      false,                    "Second, creative filter stage.")                            \
CHOICE(filter2Type,    "filter2_type",    "Filter 2 Type", "LP|HP|BP|NOTCH|COMB|FORMANT", 4, "Response of the creative filter.")                  \
FLOAT (filter2Cutoff,  "filter2_cutoff",  "Filter 2 Cutoff", 20.0f, 20000.0f, 2200.0f, 0.25f, "Hz", "Creative filter cutoff.")                    \
FLOAT (filter2Res,     "filter2_res",     "Filter 2 Res",  0.0f,  1.0f, 0.30f, 1.0f, "%",  "Creative filter resonance.")                          \
FLOAT (filter2Mix,     "filter2_mix",     "Filter 2 Mix",  0.0f,  1.0f, 0.50f, 1.0f, "%",  "Creative filter blend.")                              \
                                                                                                                                                 \
/* ---- Envelopes ------------------------------------------------------- */                                                                     \
FLOAT (ampAttack,      "amp_attack",      "Amp Attack",    0.0005f, 12.0f, 0.004f, 0.16f, "s", "Amplitude envelope attack.")                      \
FLOAT (ampDecay,       "amp_decay",       "Amp Decay",     0.002f, 20.0f, 0.60f,  0.28f, "s", "Amplitude envelope decay.")                        \
FLOAT (ampSustain,     "amp_sustain",     "Amp Sustain",   0.0f,  1.0f, 0.80f,  1.0f,  "%", "Amplitude envelope sustain level.")                  \
FLOAT (ampRelease,     "amp_release",     "Amp Release",   0.002f, 20.0f, 0.45f,  0.28f, "s", "Amplitude envelope release.")                      \
FLOAT (ampVelocity,    "amp_velocity",    "Amp Velocity",  0.0f,  1.0f, 0.55f,  1.0f,  "%", "How much velocity controls level.")                  \
FLOAT (env1Attack,     "env1_attack",     "Env 1 Attack",  0.0005f, 12.0f, 0.002f, 0.16f, "s", "Mod envelope 1 attack.")                          \
FLOAT (env1Decay,      "env1_decay",      "Env 1 Decay",   0.002f, 20.0f, 0.35f,  0.28f, "s", "Mod envelope 1 decay.")                            \
FLOAT (env1Sustain,    "env1_sustain",    "Env 1 Sustain", 0.0f,  1.0f, 0.30f,  1.0f,  "%", "Mod envelope 1 sustain.")                            \
FLOAT (env1Release,    "env1_release",    "Env 1 Release", 0.002f, 20.0f, 0.40f,  0.28f, "s", "Mod envelope 1 release.")                          \
FLOAT (env2Attack,     "env2_attack",     "Env 2 Attack",  0.0005f, 12.0f, 0.90f,  0.28f, "s", "Mod envelope 2 attack.")                          \
FLOAT (env2Decay,      "env2_decay",      "Env 2 Decay",   0.002f, 20.0f, 1.50f,  0.28f, "s", "Mod envelope 2 decay.")                            \
FLOAT (env2Sustain,    "env2_sustain",    "Env 2 Sustain", 0.0f,  1.0f, 0.60f,  1.0f,  "%", "Mod envelope 2 sustain.")                            \
FLOAT (env2Release,    "env2_release",    "Env 2 Release", 0.002f, 20.0f, 2.00f,  0.28f, "s", "Mod envelope 2 release.")                          \
                                                                                                                                                 \
/* ---- Stereo ---------------------------------------------------------- */                                                                     \
FLOAT (synthWidth,     "synth_width",     "Width",         0.0f,  2.0f, 1.0f,  1.0f, "x",  "Overall synth stereo width.")                         \
FLOAT (lowMonoFreq,    "low_mono_freq",  "Low Mono",      40.0f, 400.0f, 130.0f, 0.5f, "Hz", "Below this frequency the image is collapsed to centre.") \
FLOAT (highWidth,      "high_width",      "High Width",    0.0f,  2.0f, 1.15f, 1.0f, "x",  "Extra width above the midrange only.")                \
                                                                                                                                                 \
/* ====================================================================== */                                                                     \
/*  MODULATION                                                            */                                                                     \
/* ====================================================================== */                                                                     \
CHOICE(lfo1Shape,      "lfo1_shape",      "LFO 1 Shape",   "SINE|TRIANGLE|SAW UP|SAW DOWN|SQUARE|RANDOM|SMOOTH RANDOM", 0, "LFO 1 waveform.")     \
FLOAT (lfo1Rate,       "lfo1_rate",       "LFO 1 Rate",    0.01f, 40.0f, 1.2f,  0.28f, "Hz", "Free-running LFO 1 rate.")                          \
BOOL  (lfo1Sync,       "lfo1_sync",       "LFO 1 Sync",    false,                    "Lock LFO 1 to host tempo.")                                 \
CHOICE(lfo1Division,   "lfo1_div",        "LFO 1 Division","1/32|1/16T|1/16|1/8T|1/16.|1/8|1/4T|1/8.|1/4|1/2T|1/4.|1/2|1/1|2/1", 8, "Tempo division for LFO 1.") \
FLOAT (lfo1Depth,      "lfo1_depth",      "LFO 1 Depth",   0.0f,  1.0f, 0.0f,  1.0f, "%",  "LFO 1 output depth.")                                 \
FLOAT (lfo1Phase,      "lfo1_phase",      "LFO 1 Phase",   0.0f,  1.0f, 0.0f,  1.0f, "",   "LFO 1 start phase.")                                  \
CHOICE(lfo2Shape,      "lfo2_shape",      "LFO 2 Shape",   "SINE|TRIANGLE|SAW UP|SAW DOWN|SQUARE|RANDOM|SMOOTH RANDOM", 6, "LFO 2 waveform.")     \
FLOAT (lfo2Rate,       "lfo2_rate",       "LFO 2 Rate",    0.01f, 40.0f, 0.25f, 0.28f, "Hz", "Free-running LFO 2 rate.")                          \
BOOL  (lfo2Sync,       "lfo2_sync",       "LFO 2 Sync",    false,                    "Lock LFO 2 to host tempo.")                                 \
CHOICE(lfo2Division,   "lfo2_div",        "LFO 2 Division","1/32|1/16T|1/16|1/8T|1/16.|1/8|1/4T|1/8.|1/4|1/2T|1/4.|1/2|1/1|2/1", 12, "Tempo division for LFO 2.") \
FLOAT (lfo2Depth,      "lfo2_depth",      "LFO 2 Depth",   0.0f,  1.0f, 0.0f,  1.0f, "%",  "LFO 2 output depth.")                                 \
FLOAT (lfo2Phase,      "lfo2_phase",      "LFO 2 Phase",   0.0f,  1.0f, 0.0f,  1.0f, "",   "LFO 2 start phase.")                                  \
                                                                                                                                                 \
BOOL  (breathOn,       "breath_on",       "Breath",        true,                     "Organic non-repeating movement.")                           \
FLOAT (breathAmount,   "breath_amount",   "Breath Amount", 0.0f,  1.0f, 0.35f, 1.0f, "%",  "How far Breath moves things.")                        \
FLOAT (breathSpeed,    "breath_speed",    "Breath Speed",  0.01f,  4.0f, 0.22f, 0.3f, "Hz", "How quickly Breath wanders.")                        \
FLOAT (breathRandom,   "breath_random",   "Breath Random", 0.0f,  1.0f, 0.45f, 1.0f, "%",  "Irregularity of the wander.")                         \
FLOAT (breathShape,    "breath_shape",    "Breath Shape",  0.0f,  1.0f, 0.50f, 1.0f, "",   "Smooth drifting through to stepped and glided.")      \
                                                                                                                                                 \
BOOL  (pulseOn,        "pulse_on",        "Pulse",         false,                    "Rhythmic movement system.")                                 \
CHOICE(pulseSource,    "pulse_source",    "Pulse Source",  "CLOCK|SIDECHAIN|MIDI", 0, "What triggers the pulse.")                                 \
CHOICE(pulseDivision,  "pulse_div",       "Pulse Division","1/16|1/8T|1/16.|1/8|1/4T|1/8.|1/4|1/2|1/1", 6, "Pulse rate against host tempo.")       \
FLOAT (pulseDepth,     "pulse_depth",     "Pulse Depth",   0.0f,  1.0f, 0.45f, 1.0f, "%",  "Overall pulse intensity.")                            \
FLOAT (pulseAttack,    "pulse_attack",    "Pulse Attack",  0.0005f, 0.2f, 0.002f, 0.3f, "s", "How fast the duck happens.")                        \
FLOAT (pulseRelease,   "pulse_release",   "Pulse Release", 0.01f,  2.0f, 0.22f, 0.3f, "s",  "How fast the sound returns.")                        \
FLOAT (pulseSmooth,    "pulse_smooth",    "Pulse Smooth",  0.0f,  1.0f, 0.40f, 1.0f, "%",  "Curve softness of the pulse envelope.")               \
FLOAT (pulseToVolume,  "pulse_volume",    "Pulse Volume",  0.0f,  1.0f, 0.80f, 1.0f, "%",  "Pulse into level.")                                   \
FLOAT (pulseToFilter,  "pulse_filter",    "Pulse Filter",  0.0f,  1.0f, 0.35f, 1.0f, "%",  "Pulse into brightness. A kick makes it darker.")      \
FLOAT (pulseToSpace,   "pulse_space",     "Pulse Space",   0.0f,  1.0f, 0.30f, 1.0f, "%",  "Pulse into wetness. A kick makes it drier.")          \
FLOAT (pulseToWidth,   "pulse_width",     "Pulse Width",   0.0f,  1.0f, 0.25f, 1.0f, "%",  "Pulse into stereo. A kick makes it narrower.")        \
FLOAT (pulseToMemory,  "pulse_memory",    "Pulse Memory",  0.0f,  1.0f, 0.0f,  1.0f, "%",  "Pulse into Memory artefacts.")                        \
                                                                                                                                                 \
/* ====================================================================== */                                                                     \
/*  FX CHAIN - displayed order is DSP order                               */                                                                     \
/* ====================================================================== */                                                                     \
BOOL  (retroOn,        "retro_on",        "Retro",         true,                     "Playback-medium colouration.")                              \
FLOAT (retroEra,       "retro_era",       "Era",           0.0f,  1.0f, 0.40f, 1.0f, "%",  "Which decade of machine you are playing back through.") \
FLOAT (retroAge,       "retro_age",       "Age",           0.0f,  1.0f, 0.30f, 1.0f, "%",  "How worn the medium is.")                             \
FLOAT (retroDrift,     "retro_drift",     "Retro Drift",   0.0f,  1.0f, 0.25f, 1.0f, "%",  "Wow and flutter.")                                    \
FLOAT (retroWear,      "retro_wear",      "Wear",          0.0f,  1.0f, 0.20f, 1.0f, "%",  "Dropouts and channel instability.")                   \
FLOAT (retroTone,      "retro_tone",      "Retro Tone",   -1.0f,  1.0f, -0.15f,1.0f, "",   "Spectral tilt of the medium.")                        \
FLOAT (retroNoise,     "retro_noise",     "Retro Noise",   0.0f,  1.0f, 0.12f, 1.0f, "%",  "Noise floor of the medium.")                          \
FLOAT (retroMix,       "retro_mix",       "Retro Mix",     0.0f,  1.0f, 1.0f,  1.0f, "%",  "Dry / wet.")                                          \
                                                                                                                                                 \
BOOL  (crushOn,        "crush_on",        "Crush",         false,                    "Bit and rate reduction.")                                   \
FLOAT (crushAmount,    "crush_amount",    "Crush",         0.0f,  1.0f, 0.25f, 1.0f, "%",  "Master crush intensity.")                             \
FLOAT (crushBits,      "crush_bits",      "Bits",          1.0f, 24.0f, 12.0f, 1.0f, "bit","Effective bit depth.")                                \
FLOAT (crushRate,      "crush_rate",      "Rate",         500.0f, 48000.0f, 22050.0f, 0.3f, "Hz", "Effective sample rate.")                       \
FLOAT (crushJitter,    "crush_jitter",    "Jitter",        0.0f,  1.0f, 0.0f,  1.0f, "%",  "Instability in the sample clock.")                    \
FLOAT (crushDrive,     "crush_drive",     "Crush Drive",   0.0f,  1.0f, 0.20f, 1.0f, "%",  "Drive into the quantiser.")                           \
FLOAT (crushTone,      "crush_tone",      "Crush Tone",   -1.0f,  1.0f, 0.0f,  1.0f, "",   "Tilt after crushing.")                                \
FLOAT (crushMix,       "crush_mix",       "Crush Mix",     0.0f,  1.0f, 0.60f, 1.0f, "%",  "Dry / wet.")                                          \
                                                                                                                                                 \
BOOL  (fxFilterOn,     "fxfilter_on",     "Filter FX",     true,                     "Chain filter.")                                             \
CHOICE(fxFilterMode,   "fxfilter_mode",   "Filter Mode",   "LP|HP|BP|NOTCH|COMB|FORMANT", 0, "Chain filter response.")                            \
FLOAT (fxFilterCutoff, "fxfilter_cutoff", "FX Cutoff",    20.0f, 20000.0f, 12000.0f, 0.25f, "Hz", "Chain filter cutoff.")                         \
FLOAT (fxFilterRes,    "fxfilter_res",    "FX Resonance",  0.0f,  1.0f, 0.12f, 1.0f, "%",  "Chain filter resonance.")                             \
FLOAT (fxFilterDrive,  "fxfilter_drive",  "FX Filter Drive",0.0f, 1.0f, 0.10f, 1.0f, "%",  "Drive inside the chain filter.")                      \
FLOAT (fxFilterMorph,  "fxfilter_morph",  "Morph",         0.0f,  1.0f, 0.0f,  1.0f, "%",  "Morph between adjacent responses.")                   \
FLOAT (fxFilterMotion, "fxfilter_motion", "FX Motion",     0.0f,  1.0f, 0.0f,  1.0f, "%",  "Self-modulation of the cutoff.")                      \
FLOAT (fxFilterMix,    "fxfilter_mix",    "FX Filter Mix", 0.0f,  1.0f, 1.0f,  1.0f, "%",  "Dry / wet.")                                          \
                                                                                                                                                 \
BOOL  (rewindOn,       "rewind_on",       "Rewind",        false,                    "Plays back recent history.")                                \
CHOICE(rewindMode,     "rewind_mode",     "Rewind Mode",   "REVERSE|STOP|DIVE|RETURN", 0, "What the transport does when triggered.")              \
CHOICE(rewindDivision, "rewind_div",      "Rewind Length", "1/16|1/8|1/4|1/2|1 BAR|2 BAR", 2, "How far back it reaches.")                         \
BOOL  (rewindSync,     "rewind_sync",     "Rewind Sync",   true,                     "Lock the rewind window to host tempo.")                     \
FLOAT (rewindLength,   "rewind_length",   "Rewind Free",   0.01f,  4.0f, 0.5f,  0.3f, "s",  "Free-running rewind window.")                        \
FLOAT (rewindCurve,    "rewind_curve",    "Curve",         0.0f,  1.0f, 0.50f, 1.0f, "",   "Shape of the speed ramp.")                            \
FLOAT (rewindSpeed,    "rewind_speed",    "Speed",         0.1f,  4.0f, 1.0f,  1.0f, "x",  "Playback rate during the gesture.")                   \
FLOAT (rewindTail,     "rewind_tail",     "Tail",          0.0f,  1.0f, 0.30f, 1.0f, "%",  "How much of the gesture survives afterwards.")        \
FLOAT (rewindMix,      "rewind_mix",      "Rewind Mix",    0.0f,  1.0f, 0.70f, 1.0f, "%",  "Dry / wet.")                                          \
                                                                                                                                                 \
/*  Whether the gesture re-arms itself, or fires once and waits.                                                                                 */\
/*                                                                                                                                               */\
/*  Section 88 describes Rewind as a momentary control: triggered, runs for a                                                                    */\
/*  window, ends. With no trigger parameter to read, `rewind_on` had to be                                                                       */\
/*  treated as a repeat enable instead - the grid or the free-run timer kept                                                                     */\
/*  re-firing it for as long as it was on, which is a useful effect and is not                                                                   */\
/*  the one the specification asks for.                                                                                                          */\
/*                                                                                                                                               */\
/*  Default TRUE, so every preset written before this existed behaves exactly                                                                    */\
/*  as it did. Turned off, `rewind_on` is a one-shot: its rising edge fires the                                                                  */\
/*  gesture once and nothing re-arms it. Pulse still triggers it, because a                                                                      */\
/*  one-shot is a gesture that does not REPEAT, not one that cannot be played.                                                                   */\
BOOL  (rewindRepeat,   "rewind_repeat",   "Rewind Repeat", true,                     "Re-fire on the grid, or fire once and wait.")         \
                                                                                                                                                 \
BOOL  (grainFxOn,      "grainfx_on",      "Grain",         false,                    "Granular reconstruction of the chain signal.")              \
FLOAT (grainScatter,   "grain_scatter",   "Scatter",       0.0f,  1.0f, 0.35f, 1.0f, "%",  "Master granular intensity.")                          \
FLOAT (grainSize,      "grain_size",      "Grain Size",    5.0f, 500.0f, 80.0f, 0.35f,"ms", "Length of each grain.")                              \
FLOAT (grainDensity,   "grain_density",   "Density",       0.5f, 80.0f, 14.0f, 0.35f, "/s", "Grains per second.")                                 \
FLOAT (grainPosition,  "grain_position",  "Position",      0.0f,  1.0f, 0.30f, 1.0f, "%",  "Where in the history buffer grains are taken from.")  \
CHOICE(grainPitchMode, "grain_pitch_mode","Grain Pitch",   "ROOT|OCTAVE|FIFTH|THIRD|SCALE|CHROMATIC|FREE", 0, "How grain pitches are chosen.")    \
FLOAT (grainPitch,     "grain_pitch",     "Grain Tune",  -24.0f, 24.0f, 0.0f,  1.0f, "st", "Grain transposition when pitch mode is FREE.")        \
FLOAT (grainSpread,    "grain_spread",    "Grain Spread",  0.0f,  1.0f, 0.55f, 1.0f, "%",  "Stereo distribution of grains.")                      \
FLOAT (grainJitter,    "grain_jitter",    "Grain Jitter",  0.0f,  1.0f, 0.30f, 1.0f, "%",  "Timing and position irregularity.")                   \
FLOAT (grainDirection, "grain_direction", "Direction",     0.0f,  1.0f, 0.15f, 1.0f, "%",  "Proportion of grains that play backwards.")           \
CHOICE(grainWindow,    "grain_window",    "Window",        "HANN|TUKEY|GAUSS|EXPO|PERCUSSIVE", 0, "Grain envelope shape.")                        \
FLOAT (grainFeedback,  "grain_feedback",  "Grain Feedback",0.0f,  0.95f, 0.0f, 1.0f, "%",  "Feeds grains back into the buffer.")                  \
BOOL  (grainFreeze,    "grain_freeze",    "Freeze",        false,                    "Stops the read position advancing.")                        \
FLOAT (grainMix,       "grain_mix",       "Grain Mix",     0.0f,  1.0f, 0.50f, 1.0f, "%",  "Dry / wet.")                                          \
                                                                                                                                                 \
BOOL  (spaceOn,        "space_on",        "Space",         true,                     "Atmospheric reverb.")                                       \
CHOICE(spaceCharacter, "space_char",      "Space Character","ROOM|CHAMBER|DARK|DISTANT|INFINITE", 1, "Reverb character.")                         \
FLOAT (spaceDistance,  "space_distance",  "Distance",      0.0f,  1.0f, 0.35f, 1.0f, "%",  "How far away the source sits. Real perceptual depth.") \
FLOAT (spaceSize,      "space_size",      "Space Size",    0.0f,  1.0f, 0.55f, 1.0f, "%",  "Size of the environment.")                            \
FLOAT (spaceDecay,     "space_decay",     "Decay",         0.1f, 30.0f, 3.2f,  0.3f,  "s",  "Reverb decay time.")                                 \
FLOAT (spaceFog,       "space_fog",       "Fog",           0.0f,  1.0f, 0.40f, 1.0f, "%",  "Diffusion and blur of the tail.")                     \
FLOAT (spaceLight,     "space_light",     "Light",        -1.0f,  1.0f, -0.20f,1.0f, "",   "Brightness of the tail.")                             \
FLOAT (spacePreDelay,  "space_predelay",  "Pre-Delay",     0.0f, 250.0f, 22.0f, 0.4f, "ms", "Gap before the reverb starts.")                      \
FLOAT (spaceMix,       "space_mix",       "Space Mix",     0.0f,  1.0f, 0.28f, 1.0f, "%",  "Dry / wet.")                                          \
                                                                                                                                                 \
/* ====================================================================== */                                                                     \
/*  ATMOSPHERE - the right-hand panel                                     */                                                                     \
/* ====================================================================== */                                                                     \
BOOL  (auraOn,         "aura_on",         "Aura",          true,                     "Space and environment.")                                    \
FLOAT (auraSize,       "aura_size",       "Aura Size",     0.0f,  1.0f, 0.50f, 1.0f, "%",  "Scale of the atmosphere.")                            \
FLOAT (auraDistance,   "aura_distance",   "Aura Distance", 0.0f,  1.0f, 0.35f, 1.0f, "%",  "Perceptual depth. Not just wet level.")               \
FLOAT (auraFog,        "aura_fog",        "Aura Fog",      0.0f,  1.0f, 0.40f, 1.0f, "%",  "How much the atmosphere obscures.")                   \
FLOAT (auraDecay,      "aura_decay",      "Aura Decay",    0.0f,  1.0f, 0.45f, 1.0f, "%",  "How long the atmosphere holds.")                      \
FLOAT (auraLight,      "aura_light",      "Aura Light",   -1.0f,  1.0f, 0.0f,  1.0f, "",   "Brightness of the atmosphere.")                       \
                                                                                                                                                 \
BOOL  (shadowOn,       "shadow_on",       "Shadow",        false,                    "An atmospheric duplicate behind the original.")             \
FLOAT (shadowLength,   "shadow_length",   "Shadow Length", 0.0f,  1.0f, 0.40f, 1.0f, "%",  "How far behind the shadow trails.")                   \
FLOAT (shadowDistance, "shadow_distance", "Shadow Distance",0.0f, 1.0f, 0.55f, 1.0f, "%",  "How far away the duplicate sits.")                    \
FLOAT (shadowBlur,     "shadow_blur",     "Shadow Blur",   0.0f,  1.0f, 0.50f, 1.0f, "%",  "How smeared the duplicate is.")                       \
FLOAT (shadowPitch,    "shadow_pitch",  "Shadow Pitch",  -24.0f, 24.0f, 0.0f,  1.0f, "st", "Transposition of the duplicate.")                     \
FLOAT (shadowLevel,    "shadow_level",    "Shadow Level",  0.0f,  1.0f, 0.35f, 1.0f, "%",  "Level of the duplicate.")                             \
                                                                                                                                                 \
BOOL  (patinaOn,       "patina_on",       "Patina",        true,                     "Surface texture and age.")                                  \
CHOICE(patinaProfile,  "patina_profile",  "Patina Profile","SOFT|VINTAGE|CHROME|HAZE|SMOKE|CUSTOM", 1, "Which surface the sound has aged on.")    \
FLOAT (patinaTone,     "patina_tone",     "Patina Tone",  -1.0f,  1.0f, -0.10f,1.0f, "",   "Spectral tilt of the surface.")                       \
FLOAT (patinaNoise,    "patina_noise",    "Patina Noise",  0.0f,  1.0f, 0.15f, 1.0f, "%",  "Texture noise bed.")                                  \
FLOAT (patinaWear,     "patina_wear",     "Patina Wear",   0.0f,  1.0f, 0.25f, 1.0f, "%",  "How eroded the surface is.")                          \
FLOAT (patinaDrift,    "patina_drift",    "Patina Drift",  0.0f,  1.0f, 0.20f, 1.0f, "%",  "Slow instability of the surface.")                    \
                                                                                                                                                 \
/* ====================================================================== */                                                                     \
/*  MEMORY ENGINE (the macro drives it; these shape it)                   */                                                                     \
/* ====================================================================== */                                                                     \
FLOAT (memoryBandwidth,"memory_bandwidth","Memory Bandwidth", 0.0f, 1.0f, 0.50f, 1.0f, "%", "How much top and bottom the medium lost.")           \
FLOAT (memoryWobble,   "memory_wobble",   "Memory Wobble", 0.0f,  1.0f, 0.35f, 1.0f, "%",  "Pitch instability of the copy.")                      \
FLOAT (memoryDiffusion,"memory_diffusion","Memory Diffusion",0.0f,1.0f, 0.30f, 1.0f, "%",  "Phase smearing between copies.")                      \
FLOAT (memoryAsymmetry,"memory_asymmetry","Memory Asymmetry",0.0f,1.0f, 0.25f, 1.0f, "%",  "How differently the two channels aged.")              \
                                                                                                                                                 \
/* ====================================================================== */                                                                     \
/*  WEIGHT                                                                */                                                                     \
/* ====================================================================== */                                                                     \
FLOAT (weightHarmonics,"weight_harmonics","Weight Harmonics",0.0f,1.0f, 0.30f, 1.0f, "%",  "Harmonic reinforcement so the weight survives small speakers.") \
FLOAT (weightCompress, "weight_compress", "Weight Compress",0.0f, 1.0f, 0.25f, 1.0f, "%",  "Dynamic control inside the Weight stage.")            \
                                                                                                                                                 \
/* ====================================================================== */                                                                     \
/*  SAMPLE ENGINE                                                         */                                                                     \
/* ====================================================================== */                                                                     \
FLOAT (sampleStart,    "sample_start",    "Sample Start",  0.0f,  1.0f, 0.0f,  1.0f, "%",  "Playback start point.")                               \
FLOAT (sampleEnd,      "sample_end",      "Sample End",    0.0f,  1.0f, 1.0f,  1.0f, "%",  "Playback end point.")                                 \
BOOL  (sampleLoop,     "sample_loop",     "Loop",          false,                    "Loop between the loop points.")                             \
FLOAT (sampleLoopStart,"sample_loop_start","Loop Start",   0.0f,  1.0f, 0.0f,  1.0f, "%",  "Loop start point.")                                   \
FLOAT (sampleLoopEnd,  "sample_loop_end", "Loop End",      0.0f,  1.0f, 1.0f,  1.0f, "%",  "Loop end point.")                                     \
FLOAT (sampleCrossfade,"sample_xfade",    "Crossfade",     0.0f,  1.0f, 0.08f, 1.0f, "%",  "Loop crossfade length.")                              \
BOOL  (sampleReverse,  "sample_reverse",  "Reverse",       false,                    "Play the sample backwards.")                                \
FLOAT (sampleGain,     "sample_gain",     "Sample Gain", -24.0f, 24.0f, 0.0f,  1.0f, "dB", "Sample playback level.")                              \
FLOAT (sampleTune,     "sample_tune",     "Sample Tune", -24.0f, 24.0f, 0.0f,  1.0f, "st", "Sample transposition.")                               \
FLOAT (sampleKeyTrack, "sample_keytrack", "Sample Key Track", 0.0f, 1.0f, 1.0f, 1.0f, "%", "How far the sample follows the keyboard.")            \
FLOAT (sampleRootNote, "sample_root",     "Sample Root",   0.0f, 127.0f, 60.0f, 1.0f, "",  "MIDI note at which the sample plays untransposed.")   \
                                                                                                                                                 \
/* ====================================================================== */                                                                     \
/*  GRAIN SOURCE ENGINE                                                   */                                                                     \
/* ====================================================================== */                                                                     \
FLOAT (grainSrcPosition, "gsrc_position", "Source Position", 0.0f, 1.0f, 0.20f, 1.0f, "%", "Read position in the source.")                        \
FLOAT (grainSrcSize,     "gsrc_size",     "Source Grain Size", 5.0f, 500.0f, 110.0f, 0.35f, "ms", "Grain length.")                                \
FLOAT (grainSrcDensity,  "gsrc_density",  "Source Density", 0.5f, 80.0f, 22.0f, 0.35f, "/s", "Grains per second.")                                \
FLOAT (grainSrcPitch,    "gsrc_pitch",    "Source Grain Pitch", -24.0f, 24.0f, 0.0f, 1.0f, "st", "Grain transposition.")                          \
FLOAT (grainSrcSpread,   "gsrc_spread",   "Source Spread",  0.0f, 1.0f, 0.60f, 1.0f, "%",  "Stereo distribution.")                                \
FLOAT (grainSrcJitter,   "gsrc_jitter",   "Source Jitter",  0.0f, 1.0f, 0.30f, 1.0f, "%",  "Timing irregularity.")                                \
FLOAT (grainSrcDirection,"gsrc_direction","Source Direction",0.0f, 1.0f, 0.20f, 1.0f, "%", "Proportion playing backwards.")                       \
CHOICE(grainSrcWindow,   "gsrc_window",   "Source Window",  "HANN|TUKEY|GAUSS|EXPO|PERCUSSIVE", 2, "Grain envelope.")                             \
BOOL  (grainSrcFreeze,   "gsrc_freeze",   "Source Freeze",  false,                   "Hold the read position.")                                   \
                                                                                                                                                 \
/* ====================================================================== */                                                                     \
/*  RESONATOR ENGINE                                                      */                                                                     \
/* ====================================================================== */                                                                     \
CHOICE(resMaterial,    "res_material",    "Material",      "STRING|PLATE|TUBE|BODY|METAL|GLASS", 0, "What is resonating.")                        \
FLOAT (resExcitation,  "res_excitation",  "Excitation",    0.0f,  1.0f, 0.50f, 1.0f, "%",  "How hard the resonator is struck.")                   \
FLOAT (resDecay,       "res_decay",       "Res Decay",     0.0f,  1.0f, 0.60f, 1.0f, "%",  "How long it rings.")                                  \
FLOAT (resDamping,     "res_damping",     "Damping",       0.0f,  1.0f, 0.35f, 1.0f, "%",  "How quickly the highs die away.")                     \
FLOAT (resSize,        "res_size",        "Res Size",      0.0f,  1.0f, 0.50f, 1.0f, "%",  "Physical size of the resonator.")                     \
FLOAT (resBrightness,  "res_brightness",  "Brightness",    0.0f,  1.0f, 0.50f, 1.0f, "%",  "Spectral brightness.")                                \
FLOAT (resInharmonic,  "res_inharmonic",  "Inharmonicity", 0.0f,  1.0f, 0.10f, 1.0f, "%",  "How far the partials depart from the harmonic series.") \
FLOAT (resMix,         "res_mix",         "Res Mix",       0.0f,  1.0f, 1.0f,  1.0f, "%",  "Dry / wet.")                                          \
                                                                                                                                                 \
/* ====================================================================== */                                                                     \
/*  SPECTRAL ENGINE                                                       */                                                                     \
/* ====================================================================== */                                                                     \
BOOL  (specFreeze,     "spec_freeze",     "Spectral Freeze", false,                  "Hold the current spectrum.")                                \
FLOAT (specBlur,       "spec_blur",       "Blur",          0.0f,  1.0f, 0.25f, 1.0f, "%",  "Smears the spectrum over time.")                      \
FLOAT (specSmear,      "spec_smear",      "Smear",         0.0f,  1.0f, 0.20f, 1.0f, "%",  "Smears the spectrum across frequency.")               \
FLOAT (specShift,      "spec_shift",    "Shift",         -24.0f, 24.0f, 0.0f,  1.0f, "st", "Frequency shift of the whole spectrum.")              \
FLOAT (specHarmonic,   "spec_harmonic",   "Harmonic",      0.0f,  1.0f, 0.35f, 1.0f, "%",  "Emphasises harmonic partials over noise.")            \
FLOAT (specRedistribute,"spec_redistribute","Redistribute",0.0f,  1.0f, 0.0f,  1.0f, "%",  "Moves energy between partials.")                      \
FLOAT (specMix,        "spec_mix",        "Spectral Mix",  0.0f,  1.0f, 1.0f,  1.0f, "%",  "Dry / wet.")                                          \
                                                                                                                                                 \
/* ====================================================================== */                                                                     \
/*  MUTATION - the intelligence layer                                     */                                                                     \
/* ====================================================================== */                                                                     \
CHOICE(harmonyMode,    "harmony_mode",    "Harmony",       "SAFE|COLOR|FREE", 0,     "How far Mutate may stray harmonically.")                    \
CHOICE(distanceMode,   "distance_mode",   "Mutation Distance", "NEAR|FAR|UNKNOWN", 0, "How far Mutate may stray timbrally. Independent of Harmony.") \
CHOICE(mutationIntent, "mutation_intent", "Intent",        "MEMORY|CLOUD|BROKEN|REVERSE|DISTANT|RHYTHMIC|DARK|GHOST|PLAYABLE|CINEMATIC", 0, "What kind of transformation to aim for.") \
BOOL  (preservePitch,  "preserve_pitch",  "Preserve Pitch", false,                   "Mutation may not transpose the source.")                    \
BOOL  (preserveKey,    "preserve_key",    "Preserve Key",   false,                   "Mutation must stay in the detected key.")                   \
BOOL  (preserveRhythm, "preserve_rhythm", "Preserve Rhythm",false,                   "Mutation may not alter the timing grid.")                   \
BOOL  (preserveTransients,"preserve_transients","Preserve Transients", false,        "Mutation must leave the attacks intact.")                   \
BOOL  (preserveStereo, "preserve_stereo", "Preserve Stereo",false,                   "Mutation may not change the stereo image.")                 \
BOOL  (preserveLength, "preserve_length", "Preserve Length",false,                   "Mutation may not change the duration.")                     \
BOOL  (preserveLowEnd, "preserve_lowend", "Preserve Low End", false,                 "Mutation may not disturb the low frequencies.")             \
BOOL  (preserveAll,    "preserve_all",    "Preserve All",   false,                   "Master lock. Holds everything at once.")                    \
CHOICE(rootNote,       "root_note",       "Root",          "AUTO|C|C#|D|D#|E|F|F#|G|G#|A|A#|B", 0, "Detected or forced tonal centre.")            \
CHOICE(scaleType,      "scale_type",      "Scale",         "AUTO|MAJOR|MINOR|DORIAN|PHRYGIAN|LYDIAN|MIXOLYDIAN|HARMONIC MINOR|MELODIC MINOR|CHROMATIC", 0, "Detected or forced scale.") \
                                                                                                                                                 \
/* ====================================================================== */                                                                     \
/*  PERFORMANCE                                                           */                                                                     \
/* ====================================================================== */                                                                     \
FLOAT (modWheelDepth,  "modwheel_depth",  "Mod Wheel",     0.0f,  1.0f, 0.50f, 1.0f, "%",  "How much the mod wheel does in this preset.")         \
FLOAT (aftertouchDepth,"aftertouch_depth","Aftertouch",    0.0f,  1.0f, 0.40f, 1.0f, "%",  "How much aftertouch does in this preset.")            \
FLOAT (vibratoRate,    "vibrato_rate",    "Vibrato Rate",  0.1f, 12.0f, 5.2f,  0.4f, "Hz", "Performance vibrato rate.")                           \
FLOAT (vibratoDepth,   "vibrato_depth",   "Vibrato Depth", 0.0f,  1.0f, 0.0f,  1.0f, "%",  "Performance vibrato depth at full mod wheel.")
// clang-format on
