#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

#include "FactoryPresets.h"
#include "../Plugin/ParameterRegistry.h"
#include "../Plugin/StateManager.h"

namespace nacar
{
    /**
        THE PRESET SYSTEM.

        One library, two sources.  The factory set is compiled in
        (`FactoryPresets.cpp`); user presets are files on disk.  Both end up as
        the same `PresetInfo` and both are applied through the same code path,
        so a user preset is not a second-class citizen and a factory preset is
        not a special case.

        ------------------------------------------------------------------
        WHAT "APPLYING" MEANS.

        A preset is not a diff.  Applying one sets *every* host parameter: the
        ones the preset names to what it says, and the rest to their own
        defaults.  Anything else leaks the last patch into the new one - a
        Rewind left armed, a filter left resonating - and makes what you hear
        depend on what you were listening to before.

        Every parameter that actually changes goes through
        `ParameterRegistry::setFromUI`, which is `beginChangeGesture` /
        `setValueNotifyingHost` / `endChangeGesture`.  So the host sees a
        discrete edit per parameter, automation lanes follow, and undo works.
        Parameters already at the target value are left alone rather than
        re-sent, so loading a preset twice is not two hundred and fifty-one
        spurious automation touches.

        Threading: everything here is message-thread only.  It touches the
        APVTS and the session ValueTree and does file IO.  Nothing it does
        blocks the audio thread: parameter writes are atomic stores, and the
        chain order and modulation matrix are published to the engine by
        `NacarProcessor`'s existing ValueTree listener, which already resolves
        both off the tree and hands them over as plain data.
        ------------------------------------------------------------------

        PERSISTENCE AND THE FORWARD-MIGRATION RULE.

        A user preset file stores each parameter by its **permanent string ID**,
        never by index.  Loading one resolves every ID through
        `ParameterRegistry::fromString`: an ID the build does not know is
        dropped, and a parameter the file does not mention takes its default.
        So a preset saved today still loads after the parameter list grows, and
        a preset saved by a newer build loads here with the new controls at
        their defaults rather than failing.
    */

    /** One row of the library, whatever it came from. */
    struct PresetInfo
    {
        juce::String      name;
        juce::String      author;
        juce::String      category;
        juce::String      mood;
        juce::StringArray tags;
        juce::String      blurb;

        bool              favourite = false;
        juce::int64       lastUsed  = 0;      ///< ms since epoch, 0 == never

        juce::File        file;               ///< empty for a factory preset
        int               factoryIndex = -1;  ///< -1 for a user preset

        bool isFactory() const noexcept { return factoryIndex >= 0; }
    };

    class PresetManager
    {
    public:
        /** One modulation routing, in the shape the session tree stores. */
        struct Routing
        {
            juce::String source;   ///< matrix display name, e.g. "LFO 1"
            juce::String target;   ///< the parameter's permanent string ID
            double       depth = 0.0;
            bool         enabled = true;
        };

        /** Everything a preset actually sets, after any file has been parsed.
            Factory and user presets both become one of these. */
        struct Payload
        {
            std::vector<presets::ParamValue> values;
            std::vector<Routing>             mods;
            juce::String                     fxOrder;   ///< empty == the default order
        };

        PresetManager (const ParameterRegistry&, StateManager&);

        // -- the library ----------------------------------------------------

        /** Rebuilds from the factory set plus every user preset on disk, and
            re-reads the stored favourites and last-used times. */
        void refresh();

        int size() const noexcept                       { return library.size(); }
        const juce::Array<PresetInfo>& all() const noexcept { return library; }
        const PresetInfo& operator[] (int i) const      { return library.getReference (i); }

        /** Index of the preset with this name, or -1. */
        int indexOfName (const juce::String&) const;

        /** Which preset was last applied, or -1 if none was. */
        int currentIndex() const noexcept               { return current; }

        // -- using a preset -------------------------------------------------

        /** Applies library[index] and records it as current and recently used.
            Message thread.  False if the index is out of range or a user
            preset's file has gone. */
        bool apply (int index);

        /** Steps the selection by delta through the library, wrapping. */
        bool step (int delta);

        /** Persists the favourite flag for this preset. */
        void setFavourite (int index, bool);

        // -- saving ---------------------------------------------------------

        /** Captures the current parameter state, the FX order and the mod
            matrix into a new user preset file, then refreshes.  Returns the
            new preset's index, or -1. */
        int saveUserPreset (const juce::String& name,
                            const juce::String& category,
                            const juce::String& mood,
                            const juce::StringArray& tags);

        /** Removes a user preset's file.  Refuses factory presets. */
        bool deleteUserPreset (int index);

        // -- the pieces, exposed for the offline verification tool ----------

        /** The factory preset's parameters, routings and chain order, with
            nothing resolved against the current state. */
        static Payload payloadOf (const presets::FactoryPreset&);

        /** Reads a user preset file.  False if it is not one, or is corrupt. */
        static bool readPresetFile (const juce::File&, PresetInfo&, Payload&);

        /** Sets every parameter: the payload's own values where it has them,
            each parameter's default everywhere else, through the host gesture
            protocol.  Message thread. */
        static void applyParameters (const ParameterRegistry&, const Payload&);

        /** Writes a payload's chain order and modulation matrix into a session
            tree, in place.  The parameters are `applyParameters`' job; this is
            everything else a preset carries.

            In place matters: the MOD page caches one ValueTree handle per row
            and only re-fetches when the MODMATRIX node itself is replaced, so
            swapping the children out would leave its eight rows holding trees
            no longer in the session.  Message thread. */
        static void writePayloadToSession (StateManager&, const Payload&);

        /** Applies the factory preset with this name, parameters and chain and
            matrix and identity, straight into a registry and a session.

            This exists for one caller: the processor, at construction, so that
            a fresh instance opens on the patch its header names instead of on
            the parameter list's raw defaults with a preset's name written over
            them.  It needs no PresetManager instance because at that point in
            construction there is no library, no disk and no editor yet.

            Returns false when no factory preset carries that name, which is a
            mistake in the build rather than a condition at runtime.  Message
            thread. */
        static bool applyFactoryPresetByName (const ParameterRegistry&, StateManager&,
                                              const juce::String& name);

        /** Builds the MODMATRIX subtree a payload describes, in exactly the
            shape `ModMatrix::rebuildFromTree` expects. */
        static juce::ValueTree makeModMatrixTree (const Payload&);

        /** The chain order string a payload asks for, defaulted if it has
            none. */
        static juce::String fxOrderOf (const Payload&);

        /** Where user presets live, created on demand. */
        static juce::File userPresetDirectory();

        static juce::String fileExtension()  { return ".nacarpreset"; }

        /** Stamped into every file this build writes.

            The reader does not branch on it and does not need to, because the
            format's forward compatibility comes from its shape rather than
            from a migration step: parameters are addressed by permanent string
            ID, an ID this build does not know is dropped, and a parameter the
            file does not mention takes its default.  A file from a newer build
            therefore loads with the controls this build lacks left alone, and
            a file from an older one loads with the controls it predates at
            their defaults.  The number is here so that a change which is NOT
            covered by that - a different tree shape, a different meaning for an
            existing property - has something to branch on. */
        static constexpr int presetFormatVersion = 1;

    private:
        void loadCollections();
        void saveCollections() const;
        void writeIdentityToSession (const PresetInfo&);

        const ParameterRegistry& registry;
        StateManager& state;

        juce::Array<PresetInfo> library;
        int current = -1;

        /** name -> { favourite, lastUsed }, so the flags survive a restart for
            factory presets too, which have no file to keep them in. */
        juce::ValueTree collections;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
    };
}
