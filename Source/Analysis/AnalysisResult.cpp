/*
    AnalysisResult <-> the session tree.

    Every identifier used here is already reserved in StateManager.h.  Nothing
    new is declared: if a measurement had nowhere to live, it would not be in
    AnalysisResult.h either.
*/

#include "AnalysisResult.h"

#include "../Plugin/StateManager.h"

#include <cmath>
#include <string>

namespace nacar
{
    namespace
    {
        /** The transient list is packed into one comma-separated property, the
            same way the sequencer packs its step values - a child node per
            onset would put thousands of ValueTree nodes inside every preset.

            The cap exists for the same reason.  A five-minute percussion loop
            can produce several thousand onsets; 1024 of them is already a 7 kB
            string, and a preset that carries one is a preset that loads slowly.
            The in-memory AnalysisResult is NOT capped - only what is persisted
            is - so a consumer that has just run the analysis sees everything.
            A round-trip through the tree does not. */
        constexpr int kMaxPersistedTransients = 1024;

        juce::String packTransients (const std::vector<int>& positions, int limit)
        {
            const auto count = (size_t) juce::jmin ((int) positions.size(), limit);

            std::string out;
            out.reserve (count * 7);

            for (size_t i = 0; i < count; ++i)
            {
                if (i != 0)
                    out += ',';

                out += std::to_string (positions[i]);
            }

            return juce::String (out);
        }

        /** Same list, normalised to 0..1 against the sample's own length.

            This exists for one reason and it is worth stating plainly.
            AnalysisResult.h defines `transients` as positions IN SAMPLES, and
            that is what the TRANSIENTS child carries and what readFrom() reads
            back.  But OpticalViewport::readTransients(), which shipped in phase
            18, reads ids::transientPositions straight off the ANALYSIS node and
            clamps every value into 0..1 - it expects normalised positions, and
            WaveformView::setTransients() says so in its own documentation.

            Writing sample positions there would put every marker on the right
            edge of the waveform.  So both forms are written: the canonical one
            on the TRANSIENTS child, and the normalised one on ANALYSIS for the
            viewport.  It costs one property and no new identifier.

            This is a contract mismatch between two files that are both frozen
            to me, and somebody should collapse it into one representation.
            Until then, the rule is: the TRANSIENTS child is the truth. */
        juce::String packNormalisedTransients (const std::vector<int>& positions,
                                               int lengthSamples, int limit)
        {
            if (lengthSamples <= 0)
                return {};

            const auto count = (size_t) juce::jmin ((int) positions.size(), limit);

            juce::String out;
            out.preallocateBytes (count * 8);

            for (size_t i = 0; i < count; ++i)
            {
                if (i != 0)
                    out << ',';

                const double n = juce::jlimit (0.0, 1.0,
                                               (double) positions[i] / (double) lengthSamples);
                out << juce::String (n, 6);
            }

            return out;
        }

        /** The sample length, found by looking sideways at the SAMPLE branch.

            writeTo() is handed only the ANALYSIS branch, and the normalised
            mirror above needs a length that AnalysisResult does not carry. When
            the branch is detached - as it is in the unit tests - there is no
            SAMPLE sibling and no mirror is written, which is the honest
            outcome: a position cannot be normalised against a length nobody
            knows. */
        int lengthFromSiblingSampleBranch (const juce::ValueTree& analysisBranch)
        {
            const auto session = analysisBranch.getParent();

            if (! session.isValid())
                return 0;

            const auto sample = session.getChildWithName (ids::SAMPLE);

            if (! sample.isValid())
                return 0;

            return (int) sample.getProperty (ids::sampleLengthSamples, 0);
        }

        float readFloat (const juce::ValueTree& t, const juce::Identifier& id, float fallback)
        {
            const auto v = t.getProperty (id);

            if (v.isVoid())
                return fallback;

            const auto f = (float) v;
            return std::isfinite (f) ? f : fallback;
        }

        double readDouble (const juce::ValueTree& t, const juce::Identifier& id, double fallback)
        {
            const auto v = t.getProperty (id);

            if (v.isVoid())
                return fallback;

            const auto d = (double) v;
            return std::isfinite (d) ? d : fallback;
        }
    }

    void AnalysisResult::writeTo (juce::ValueTree& branch, juce::UndoManager* undo) const
    {
        if (! branch.isValid())
            return;

        branch.setProperty (ids::analysed,        analysed,                 undo);

        branch.setProperty (ids::detectedRoot,    root,                     undo);
        branch.setProperty (ids::rootConfidence,  (double) rootConfidence,  undo);
        branch.setProperty (ids::detectedScale,   scale,                    undo);
        branch.setProperty (ids::scaleConfidence, (double) scaleConfidence, undo);

        branch.setProperty (ids::detectedTempo,   tempo,                    undo);
        branch.setProperty (ids::tempoConfidence, (double) tempoConfidence, undo);

        branch.setProperty (ids::peakLevel,        (double) peakLevel,        undo);
        branch.setProperty (ids::loudness,         (double) loudness,         undo);
        branch.setProperty (ids::spectralCentroid, (double) spectralCentroid, undo);
        branch.setProperty (ids::spectralRolloff,  (double) spectralRolloff,  undo);
        branch.setProperty (ids::lowEnergy,        (double) lowEnergy,        undo);
        branch.setProperty (ids::highEnergy,       (double) highEnergy,       undo);

        branch.setProperty (ids::percussiveRatio,      (double) percussiveRatio,      undo);
        branch.setProperty (ids::polyphonicLikelihood, (double) polyphonicLikelihood, undo);
        branch.setProperty (ids::loopability,          (double) loopability,          undo);
        branch.setProperty (ids::silenceRatio,         (double) silenceRatio,         undo);

        auto transientBranch = branch.getOrCreateChildWithName (ids::TRANSIENTS, undo);
        transientBranch.setProperty (ids::transientPositions,
                                     packTransients (transients, kMaxPersistedTransients),
                                     undo);

        const auto normalised = packNormalisedTransients (transients,
                                                          lengthFromSiblingSampleBranch (branch),
                                                          kMaxPersistedTransients);

        if (normalised.isNotEmpty())
            branch.setProperty (ids::transientPositions, normalised, undo);
        else
            branch.removeProperty (ids::transientPositions, undo);
    }

    AnalysisResult AnalysisResult::readFrom (const juce::ValueTree& branch)
    {
        AnalysisResult r;

        if (! branch.isValid())
            return r;

        r.analysed = (bool) branch.getProperty (ids::analysed, false);

        r.root            = (int)   branch.getProperty (ids::detectedRoot, -1);
        r.rootConfidence  = readFloat (branch, ids::rootConfidence, 0.0f);
        r.scale           = (int)   branch.getProperty (ids::detectedScale, -1);
        r.scaleConfidence = readFloat (branch, ids::scaleConfidence, 0.0f);

        r.tempo           = readDouble (branch, ids::detectedTempo, 0.0);
        r.tempoConfidence = readFloat  (branch, ids::tempoConfidence, 0.0f);

        r.peakLevel        = readFloat (branch, ids::peakLevel,        0.0f);
        r.loudness         = readFloat (branch, ids::loudness,         -144.0f);
        r.spectralCentroid = readFloat (branch, ids::spectralCentroid, 0.0f);
        r.spectralRolloff  = readFloat (branch, ids::spectralRolloff,  0.0f);
        r.lowEnergy        = readFloat (branch, ids::lowEnergy,        0.0f);
        r.highEnergy       = readFloat (branch, ids::highEnergy,       0.0f);

        r.percussiveRatio      = readFloat (branch, ids::percussiveRatio,      0.0f);
        r.polyphonicLikelihood = readFloat (branch, ids::polyphonicLikelihood, 0.0f);
        r.loopability          = readFloat (branch, ids::loopability,          0.0f);
        r.silenceRatio         = readFloat (branch, ids::silenceRatio,         0.0f);

        // Only the TRANSIENTS child is read.  The property of the same name on
        // ANALYSIS is the normalised mirror the phase-18 viewport wants, and
        // reading it back as samples would silently divide every position by
        // the length of the file.  See packNormalisedTransients above.
        const auto transientBranch = branch.getChildWithName (ids::TRANSIENTS);

        if (transientBranch.isValid())
        {
            const auto csv = transientBranch.getProperty (ids::transientPositions, "").toString();

            if (csv.isNotEmpty())
            {
                const auto tokens = juce::StringArray::fromTokens (csv, ",", "");
                r.transients.reserve ((size_t) tokens.size());

                for (const auto& token : tokens)
                {
                    const auto trimmed = token.trim();

                    if (trimmed.isNotEmpty())
                        r.transients.push_back (juce::jmax (0, trimmed.getIntValue()));
                }
            }
        }

        return r;
    }
}
