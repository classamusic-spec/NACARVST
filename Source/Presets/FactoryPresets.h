#pragma once

#include <juce_core/juce_core.h>
#include <vector>

#include "../Plugin/ParameterRegistry.h"

namespace nacar::presets
{
    /**
        THE NACAR FACTORY LIBRARY.

        A factory preset is data, not a file.  It is compiled in, so it cannot
        go missing, cannot be version-skewed against the engine that plays it,
        and needs no installer.  What it stores is exactly what a user preset
        stores - parameter values, the FX chain order, and the modulation
        matrix - so `PresetManager` applies both through one code path.

        ------------------------------------------------------------------
        WHAT A PRESET MAY REACH.

        Everything below is reachable from the instrument as it stands today:
        the synth core, Memory's four generations, the six FX slots and their
        order, the three atmosphere engines, Weight, the two LFOs, Breath,
        Pulse and the eight-slot modulation matrix.

        Nothing here depends on a loaded sample, an analysis result or a
        mutation, because none of those exist yet.  `source_mode` is read by no
        engine, so every preset stays on SYNTH and says so by omission.  The
        four per-voice modulation sources (ENV 1, ENV 2, VELOCITY, KEY TRACK)
        read zero in the global matrix, so no routing here names one.
        ------------------------------------------------------------------

        Parameters are held as PIDs rather than as strings because that is a
        compile-time check: a preset cannot name a control the build does not
        have.  Persistence is the other way round - `PresetManager` writes the
        permanent string ID into a user preset file, so a saved preset survives
        the parameter list growing.
    */

    /** One parameter setting, in the parameter's own real units. */
    struct ParamValue
    {
        PID   pid;
        float value;
    };

    /** One modulation routing, exactly as the MOD page would have made it.

        `source` is the display name the matrix parses ("LFO 1", "BREATH",
        "PULSE", "ORGANIC RANDOM", "MEMORY", "MOTION", "WORLD", "ALTER",
        "MOD WHEEL", "AFTERTOUCH").  `depth` is the bipolar -1..1 normalised
        depth the matrix already documents. */
    struct ModRouting
    {
        const char* source;
        PID         target;
        float       depth;
    };

    /**
        How the verification renderer should audition this preset.

        This is a render hint and nothing more.  It says which note the preset
        was voiced around so `NacarBench --presets` measures each one on
        material it was designed for rather than on one note for all of them -
        a kick measured at C4 and a pad measured at C1 would both read as
        broken when neither is.
    */
    struct Audition
    {
        int    midiNote   = 48;
        int    chordNotes = 1;
        float  velocity   = 0.85f;
        double seconds    = 4.0;
    };

    struct FactoryPreset
    {
        juce::String name;
        juce::String category;     ///< one of the browser's thirteen words
        juce::String mood;         ///< one of the browser's sixteen words
        juce::StringArray tags;    ///< two to four, lower case
        juce::String blurb;        ///< the one sentence that is its identity

        juce::String fxOrder;      ///< comma-separated slot names; empty means the default
        Audition     audition;

        std::vector<ParamValue> values;
        std::vector<ModRouting> mods;
    };

    /** The whole factory set, built once and shared. */
    const std::vector<FactoryPreset>& factoryLibrary();

    /** The author string every factory preset carries. */
    juce::String factoryAuthor();
}
