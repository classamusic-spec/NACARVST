#include "InstrumentBuilder.h"
#include "PrintEngine.h"

namespace nacar
{
    int InstrumentBuilder::recordGeneration (StateManager& state, const juce::File& file,
                                             const juce::String& displayName)
    {
        auto generations = state.group (ids::GENERATIONS);

        juce::ValueTree entry (ids::GENERATION);
        entry.setProperty (ids::generationFile, file.getFullPathName(), nullptr);

        // Read BEFORE the child is added, so a generation's parent is the one
        // that was current when it was made rather than itself.
        const int parent = generations.getNumChildren() > 0
                         ? (int) generations.getProperty (ids::generationIndex, -1)
                         : -1;

        entry.setProperty (ids::generationParent, parent, nullptr);
        entry.setProperty (ids::sampleDisplayName, displayName, nullptr);

        generations.addChild (entry, -1, nullptr);

        const int index = generations.getNumChildren() - 1;
        generations.setProperty (ids::generationIndex, index, nullptr);

        return index;
    }

    InstrumentBuilder::Result InstrumentBuilder::build (const SampleBuffer& source,
                                                        const AnalysisResult& analysis,
                                                        const ParameterRegistry& registry,
                                                        StateManager& state,
                                                        const juce::String& sourceName)
    {
        Result result;

        if (source.isEmpty())
        {
            result.failure = "NOTHING TO MAKE AN INSTRUMENT FROM";
            return result;
        }

        // Committed to disk, because from here on this is a sample the session
        // LOADS rather than a buffer the session happens to be holding: the
        // audio is not in the host's saved blob, only the path is.
        result.file = source.file;

        if (! result.file.existsAsFile())
            result.file = PrintEngine::writeToDisk (source, sourceName.isNotEmpty() ? sourceName
                                                                                    : "INSTRUMENT");

        if (! result.file.existsAsFile())
        {
            result.failure = "COULD NOT WRITE THE INSTRUMENT";
            return result;
        }

        result.generationIndex = recordGeneration (state, result.file, "INSTRUMENT");

        // -- the root note ---------------------------------------------------
        //
        //  What decides it is the analysis, and only when the analysis is
        //  confident enough to be acted on. A key detected at 0.3 confidence is
        //  a guess, and tuning an instrument to a guess is worse than tuning it
        //  to the note it was demonstrably rendered at: see the note on
        //  confidence in AnalysisResult.h.
        result.rootNote = 60.0f;

        if (analysis.analysed && analysis.keyIsUsable())
        {
            // The analyser reports a pitch class and not an octave, so it is
            // placed in the octave the instrument was printed in - the only
            // octave there is any evidence for.
            result.rootNote = (float) (48 + (analysis.root % 12));
            result.rootFromAnalysis = true;
        }

        registry.setFromUI (PID::sourceMode,     1.0f);      // SAMPLE
        registry.setFromUI (PID::sampleRootNote, result.rootNote);
        registry.setFromUI (PID::sampleKeyTrack, 1.0f);
        registry.setFromUI (PID::sampleStart,    0.0f);
        registry.setFromUI (PID::sampleEnd,      1.0f);
        registry.setFromUI (PID::sampleTune,     0.0f);
        registry.setFromUI (PID::sampleReverse,  0.0f);
        registry.setFromUI (PID::sampleLoop,     0.0f);

        auto sample = state.group (ids::SAMPLE);

        sample.setProperty (ids::sampleFile,          result.file.getFullPathName(), nullptr);
        sample.setProperty (ids::sampleDisplayName,   result.file.getFileNameWithoutExtension(), nullptr);
        sample.setProperty (ids::sampleRate,          source.sourceRate, nullptr);
        sample.setProperty (ids::sampleLengthSamples, source.lengthSamples(), nullptr);
        sample.setProperty (ids::sampleChannels,      source.numChannels(), nullptr);
        sample.setProperty (ids::selectionStart,      0.0, nullptr);
        sample.setProperty (ids::selectionEnd,        0.0, nullptr);

        // The name says which generation you are on, so the loop this
        // instrument is built around - print, mutate, make, print again - is
        // visible in the header rather than only in the tree. The stem is taken
        // before any previous " GEN n", or the names would nest.
        const auto stem = sourceName.upToFirstOccurrenceOf (" GEN ", false, false).trim();

        result.name = (stem.isNotEmpty() ? stem : juce::String ("NACAR"))
                      + " GEN " + juce::String (result.generationIndex + 1);

        state.group (ids::PRESET).setProperty (ids::presetName, result.name, nullptr);

        result.ok = true;
        return result;
    }
}
