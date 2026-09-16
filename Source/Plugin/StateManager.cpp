#include "StateManager.h"

namespace nacar
{
    StateManager::StateManager (juce::AudioProcessorValueTreeState& s)
        : apvts (s), sessionTree (makeDefaultSession())
    {
    }

    juce::ValueTree StateManager::makeDefaultSession()
    {
        juce::ValueTree t (ids::SESSION);
        t.setProperty (ids::schemaVersion, currentSchemaVersion, nullptr);
        // JucePlugin_VersionString only exists in the plugin targets; the test
        // runner compiles this file too, so it is guarded rather than assumed.
       #if defined (JucePlugin_VersionString)
        t.setProperty (ids::pluginVersion, juce::String (JucePlugin_VersionString), nullptr);
       #else
        t.setProperty (ids::pluginVersion, "1.0.0", nullptr);
       #endif

        juce::ValueTree preset (ids::PRESET);
        preset.setProperty (ids::presetName,      "Niebla en la Ciudad", nullptr);
        preset.setProperty (ids::presetAuthor,    "NACAR", nullptr);
        preset.setProperty (ids::presetCategory,  "PADS", nullptr);
        preset.setProperty (ids::presetMood,      "MYSTERIOUS", nullptr);
        preset.setProperty (ids::presetTags,      "", nullptr);
        preset.setProperty (ids::presetFavourite, false, nullptr);
        t.addChild (preset, -1, nullptr);

        juce::ValueTree sample (ids::SAMPLE);
        sample.setProperty (ids::sampleFile,          "", nullptr);
        sample.setProperty (ids::sampleDisplayName,   "", nullptr);
        sample.setProperty (ids::sampleRate,          0.0, nullptr);
        sample.setProperty (ids::sampleLengthSamples, 0, nullptr);
        sample.setProperty (ids::sampleChannels,      0, nullptr);
        sample.setProperty (ids::selectionStart,      0.0, nullptr);
        sample.setProperty (ids::selectionEnd,        0.0, nullptr);
        sample.setProperty (ids::playhead,            0.0, nullptr);
        sample.setProperty (ids::zoom,                0.0, nullptr);
        sample.setProperty (ids::scrollPosition,      0.0, nullptr);
        t.addChild (sample, -1, nullptr);

        juce::ValueTree analysis (ids::ANALYSIS);
        analysis.setProperty (ids::analysed, false, nullptr);
        t.addChild (analysis, -1, nullptr);

        juce::ValueTree fxChain (ids::FXCHAIN);
        // Display order is DSP order.  This is the order in the locked reference.
        fxChain.setProperty (ids::fxOrder, "RETRO,CRUSH,FILTER,REWIND,GRAIN,SPACE", nullptr);
        fxChain.setProperty (ids::fxLocks, "", nullptr);
        fxChain.setProperty (ids::selectedFxSlot, 0, nullptr);
        t.addChild (fxChain, -1, nullptr);

        juce::ValueTree mutation (ids::MUTATION);
        mutation.setProperty (ids::currentSeed,   0, nullptr);
        mutation.setProperty (ids::seedLocked,    false, nullptr);
        mutation.setProperty (ids::engineVersion, 1, nullptr);
        mutation.setProperty (ids::historyIndex,  -1, nullptr);
        mutation.addChild (juce::ValueTree (ids::HISTORY), -1, nullptr);
        t.addChild (mutation, -1, nullptr);

        juce::ValueTree generations (ids::GENERATIONS);
        generations.setProperty (ids::generationIndex, 0, nullptr);
        t.addChild (generations, -1, nullptr);

        juce::ValueTree editor (ids::EDITOR);
        editor.setProperty (ids::editorScale,  1.0, nullptr);
        editor.setProperty (ids::activePage,   0, nullptr);      // MAIN
        editor.setProperty (ids::browserOpen,  false, nullptr);
        editor.setProperty (ids::auraSelectedParam,   0, nullptr);
        editor.setProperty (ids::shadowSelectedParam, 0, nullptr);
        editor.setProperty (ids::breathSelectedParam, 0, nullptr);
        editor.setProperty (ids::patinaSelectedParam, 0, nullptr);
        t.addChild (editor, -1, nullptr);

        t.addChild (juce::ValueTree (ids::MACROS),    -1, nullptr);
        t.addChild (juce::ValueTree (ids::MODMATRIX), -1, nullptr);

        return t;
    }

    juce::ValueTree StateManager::group (const juce::Identifier& type)
    {
        auto child = sessionTree.getChildWithName (type);

        if (! child.isValid())
        {
            child = juce::ValueTree (type);
            sessionTree.addChild (child, -1, nullptr);
        }

        return child;
    }

    void StateManager::resetSession()
    {
        sessionTree.copyPropertiesAndChildrenFrom (makeDefaultSession(), nullptr);
    }

    void StateManager::writeTo (juce::MemoryBlock& destination) const
    {
        juce::ValueTree root (ids::NACAR);
        root.setProperty (ids::schemaVersion, currentSchemaVersion, nullptr);

        root.addChild (apvts.copyState(), -1, nullptr);
        root.addChild (sessionTree.createCopy(), -1, nullptr);

        juce::MemoryOutputStream stream (destination, false);
        root.writeToStream (stream);
    }

    void StateManager::readFrom (const void* data, int sizeInBytes)
    {
        if (data == nullptr || sizeInBytes <= 0)
            return;

        const auto root = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);

        if (! root.isValid())
            return;

        // ------------------------------------------------------------------
        //  Parameters.  APVTS ignores IDs it does not know, so a session from a
        //  newer build loads with the unknown parameters left at their defaults
        //  rather than failing outright.
        // ------------------------------------------------------------------
        if (root.hasType (ids::NACAR))
        {
            if (auto params = root.getChildWithName (apvts.state.getType()); params.isValid())
                apvts.replaceState (params);

            if (auto s = root.getChildWithName (ids::SESSION); s.isValid())
            {
                auto copy = s.createCopy();
                const int version = copy.getProperty (ids::schemaVersion, 0);

                if (version < currentSchemaVersion)
                    upgrade (copy, version);

                sessionTree.copyPropertiesAndChildrenFrom (copy, nullptr);
            }
        }
        else if (root.hasType (apvts.state.getType()))
        {
            // A bare APVTS tree: an early development build, or a host that
            // stored only the parameter state.  Accept it and keep defaults
            // for everything else.
            apvts.replaceState (root);
        }
    }

    void StateManager::upgrade (juce::ValueTree& session, int fromVersion)
    {
        // Migration chain.  Each step moves the tree forward exactly one
        // version and then falls through to the next.  No step ever removes a
        // property it does not understand.
        int v = fromVersion;

        if (v < 1)
        {
            // Pre-versioned development trees: fill in anything missing from
            // the current defaults without disturbing what is already there.
            const auto defaults = makeDefaultSession();

            for (int i = 0; i < defaults.getNumChildren(); ++i)
            {
                const auto d = defaults.getChild (i);

                if (! session.getChildWithName (d.getType()).isValid())
                    session.addChild (d.createCopy(), -1, nullptr);
            }

            v = 1;
        }

        session.setProperty (ids::schemaVersion, currentSchemaVersion, nullptr);
        juce::ignoreUnused (v);
    }
}
