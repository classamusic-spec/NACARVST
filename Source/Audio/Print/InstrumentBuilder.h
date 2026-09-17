#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "../../Plugin/ParameterRegistry.h"
#include "../../Plugin/StateManager.h"
#include "../../Analysis/AnalysisResult.h"
#include "../Sources/Sample/SampleBuffer.h"

namespace nacar
{
    /**
        MAKE INSTRUMENT - the step the instrument is named for.

        NACAR's premise ends "and turn the result into another instrument".
        This is that turn: whatever is in the sample slot - a print of a patch,
        or a mutation of one - stops being a thing you are auditioning and
        becomes the thing the keyboard plays.  From there it can be printed
        again, mutated again, and committed again, which is the loop the whole
        product is built around.

        WHY IT IS HERE AND NOT IN THE PROCESSOR.  The processor cannot be
        linked into the test runner: it defines the plugin's entry points and
        reaches for the editor.  Logic that lives there is logic nothing can
        test, and this logic decides a root note, writes a file and rewrites the
        session - all of which are worth testing.  Compare SourcePipeline, which
        exists for the same reason.

        MESSAGE THREAD.  It writes parameters through the host gesture protocol
        and edits the session tree; neither is safe anywhere else.
    */
    class InstrumentBuilder
    {
    public:
        struct Result
        {
            bool ok = false;
            juce::String failure;        ///< shown to the user, in their words

            juce::File   file;           ///< where the instrument was written
            juce::String name;           ///< what the session now calls it
            int   generationIndex = -1;  ///< its place in the lineage
            float rootNote = 60.0f;      ///< the note it plays at its own pitch
            bool  rootFromAnalysis = false;   ///< or the print's own note
        };

        /** Appends a GENERATION child naming this file and the generation it
            came from, and returns its index.

            Every generation records its parent, so a chain of prints and
            instruments can be walked back to the patch it started as. The
            first one's parent is -1, which is what a root is. */
        static int recordGeneration (StateManager&, const juce::File&,
                                     const juce::String& displayName);

        /** Commits `source` as the instrument's playable source.

            `sourceName` is what the session currently calls itself, and is used
            both to name the file and to derive the new name. */
        static Result build (const SampleBuffer& source,
                             const AnalysisResult& analysis,
                             const ParameterRegistry&,
                             StateManager&,
                             const juce::String& sourceName);

    private:
        InstrumentBuilder() = delete;
    };
}
