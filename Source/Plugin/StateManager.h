#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

#include "ParameterRegistry.h"

namespace nacar
{
    /**
        Everything NACAR persists that is not a host parameter.

        Host parameters live in the APVTS.  This tree carries the rest: which
        sample is loaded, what the analyser concluded about it, the FX order and
        locks, the mutation seed and recipe history, generation lineage, preset
        identity and the editor's own preferences.

        The schema is versioned.  `upgrade()` migrates older trees forward; it
        never throws away a property it does not recognise, so a session saved
        by a newer build degrades gracefully rather than losing data.
    */
    namespace ids
    {
        #define NACAR_ID(name) inline const juce::Identifier name (#name);

        NACAR_ID (NACAR)             // root
        NACAR_ID (PARAMETERS)        // APVTS subtree
        NACAR_ID (SESSION)           // everything below

        NACAR_ID (schemaVersion)
        NACAR_ID (pluginVersion)

        NACAR_ID (PRESET)
        NACAR_ID (presetName)
        NACAR_ID (presetAuthor)
        NACAR_ID (presetCategory)
        NACAR_ID (presetMood)
        NACAR_ID (presetTags)
        NACAR_ID (presetFavourite)
        NACAR_ID (presetFile)

        NACAR_ID (SAMPLE)
        NACAR_ID (sampleFile)
        NACAR_ID (sampleDisplayName)
        NACAR_ID (sampleRate)
        NACAR_ID (sampleLengthSamples)
        NACAR_ID (sampleChannels)
        NACAR_ID (selectionStart)
        NACAR_ID (selectionEnd)
        NACAR_ID (playhead)
        NACAR_ID (zoom)
        NACAR_ID (scrollPosition)

        NACAR_ID (ANALYSIS)
        NACAR_ID (analysed)
        NACAR_ID (detectedRoot)
        NACAR_ID (rootConfidence)
        NACAR_ID (detectedScale)
        NACAR_ID (scaleConfidence)
        NACAR_ID (detectedTempo)
        NACAR_ID (tempoConfidence)
        NACAR_ID (peakLevel)
        NACAR_ID (loudness)
        NACAR_ID (spectralCentroid)
        NACAR_ID (spectralRolloff)
        NACAR_ID (lowEnergy)
        NACAR_ID (highEnergy)
        NACAR_ID (percussiveRatio)
        NACAR_ID (polyphonicLikelihood)
        NACAR_ID (loopability)
        NACAR_ID (silenceRatio)
        NACAR_ID (TRANSIENTS)
        NACAR_ID (transientPositions)

        NACAR_ID (FXCHAIN)
        NACAR_ID (fxOrder)          // comma-separated slot names, display == DSP order
        NACAR_ID (fxLocks)          // comma-separated locked slot names
        NACAR_ID (selectedFxSlot)

        NACAR_ID (MUTATION)
        NACAR_ID (currentSeed)
        NACAR_ID (seedLocked)
        NACAR_ID (engineVersion)
        NACAR_ID (HISTORY)
        NACAR_ID (RECIPE)
        NACAR_ID (historyIndex)
        NACAR_ID (recipeSeed)
        NACAR_ID (recipeIntent)
        NACAR_ID (recipeHarmony)
        NACAR_ID (recipeDistance)
        NACAR_ID (recipeScore)
        NACAR_ID (recipeFavourite)
        NACAR_ID (recipePayload)

        NACAR_ID (GENERATIONS)
        NACAR_ID (generationIndex)
        NACAR_ID (GENERATION)
        NACAR_ID (generationFile)
        NACAR_ID (generationParent)

        NACAR_ID (EDITOR)
        NACAR_ID (editorScale)
        NACAR_ID (activePage)
        NACAR_ID (browserOpen)
        NACAR_ID (auraSelectedParam)
        NACAR_ID (shadowSelectedParam)
        NACAR_ID (breathSelectedParam)
        NACAR_ID (patinaSelectedParam)

        NACAR_ID (MACROS)
        NACAR_ID (MAPPING)
        NACAR_ID (macroSource)
        NACAR_ID (macroTarget)
        NACAR_ID (macroDepth)
        NACAR_ID (macroCurve)

        NACAR_ID (MODMATRIX)
        NACAR_ID (MODSLOT)
        NACAR_ID (modSource)
        NACAR_ID (modTarget)
        NACAR_ID (modDepth)
        NACAR_ID (modEnabled)

        // -- the step sequencer, specification section 129 -------------------
        //
        //  SESSION holds one SEQUENCER child, which holds one SEQLANE per lane.
        //  These lived in Source/UI/Pages/SeqPage.h while the page was the only
        //  thing that could see them; the trigger engine is in Source/Audio,
        //  which must not include Source/UI, so they moved here to the rest of
        //  the session schema rather than being declared twice.
        //
        //  A lane packs its sixteen step values into one comma-separated string
        //  and its sixteen gates into a sixteen-character mask.  Packing a short
        //  list into a single property is already the house pattern - see
        //  transientPositions - and it keeps a lane small enough to live inside
        //  a preset.
        NACAR_ID (SEQUENCER)
        NACAR_ID (SEQLANE)
        NACAR_ID (seqDivision)      // index into seq::divisions
        NACAR_ID (laneName)
        NACAR_ID (laneTarget)       // permanent parameter string ID
        NACAR_ID (laneEnabled)
        NACAR_ID (laneLength)       // 1..16 steps per cycle
        NACAR_ID (laneValues)       // "0.500,0.250,..." x16
        NACAR_ID (laneGates)        // "1011..." x16

        #undef NACAR_ID
    }

    /**
        What a stored `seqDivision` index MEANS.

        The SEQ page draws the name and the sequencer engine runs at the beats,
        so the two halves of one contract are kept in one table where they
        cannot disagree - and in a header both Source/UI and Source/Audio may
        include, which is the whole reason it is not in the page.
    */
    namespace seq
    {
        struct Division
        {
            const char* name;
            double      beats;
        };

        inline constexpr std::array<Division, 9> divisions {{
            { "1/32",  0.125 },     { "1/16T", 1.0 / 6.0 }, { "1/16",  0.25 },
            { "1/8T",  1.0 / 3.0 }, { "1/16.", 0.375 },     { "1/8",   0.5 },
            { "1/4T",  2.0 / 3.0 }, { "1/8.",  0.75 },      { "1/4",   1.0 }
        }};

        inline constexpr int numDivisions = (int) divisions.size();
        inline constexpr int defaultDivision = 2;       ///< 1/16

        /** Four lanes of sixteen steps.  The page draws this many and the
            engine advances this many; neither may decide it alone. */
        inline constexpr int numLanes = 4;
        inline constexpr int numSteps = 16;

        /** Beats per step for a stored index, clamped rather than trusted: the
            index comes out of a session file and may be from any build. */
        inline double beatsForDivision (int index) noexcept
        {
            return divisions[(size_t) juce::jlimit (0, numDivisions - 1, index)].beats;
        }
    }

    class StateManager
    {
    public:
        /** Bump this whenever the shape of the SESSION tree changes. */
        static constexpr int currentSchemaVersion = 1;

        /** The factory preset a fresh session opens on.

            It is named here rather than typed into makeDefaultSession(),
            because the processor has to apply the same one at construction -
            a default session that NAMES a patch it has not loaded is a header
            bar telling the user something untrue. */
        static constexpr const char* defaultPresetName = "Niebla en la Ciudad";

        explicit StateManager (juce::AudioProcessorValueTreeState&);

        /** The mutable non-parameter tree.  Message thread only. */
        juce::ValueTree& session() noexcept { return sessionTree; }
        const juce::ValueTree& session() const noexcept { return sessionTree; }

        /** Convenience: fetch (creating if needed) a named child of SESSION. */
        juce::ValueTree group (const juce::Identifier&);

        /** Serialises parameters + session into one binary blob for the host. */
        void writeTo (juce::MemoryBlock&) const;

        /** Restores from a host blob.  Tolerates older and newer schemas. */
        void readFrom (const void* data, int sizeInBytes);

        /** Resets the session tree to its defaults without touching parameters. */
        void resetSession();

        /** Migrates a SESSION tree written by an older build. */
        static void upgrade (juce::ValueTree& session, int fromVersion);

        /** Builds a fresh, fully-populated default SESSION tree. */
        static juce::ValueTree makeDefaultSession();

        juce::UndoManager& undoManager() noexcept { return undo; }

    private:
        juce::AudioProcessorValueTreeState& apvts;
        juce::ValueTree sessionTree;
        juce::UndoManager undo;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StateManager)
    };
}
