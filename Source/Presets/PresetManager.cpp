#include "PresetManager.h"

#include <algorithm>
#include <cmath>

namespace nacar
{
    namespace
    {
        // -- the preset file's own vocabulary -------------------------------
        const juce::Identifier kPreset          ("NACARPRESET");
        const juce::Identifier kFormatVersion   ("formatVersion");
        const juce::Identifier kParams          ("PARAMETERS");
        const juce::Identifier kParam           ("PARAM");
        const juce::Identifier kParamId         ("id");
        const juce::Identifier kParamValue      ("value");
        const juce::Identifier kBlurb           ("presetBlurb");

        const juce::Identifier kCollections     ("NACARCOLLECTIONS");
        const juce::Identifier kEntry           ("ENTRY");
        const juce::Identifier kEntryName       ("name");

        /** Clamps a value into whatever the parameter can actually hold.  A
            choice parameter's range is not in `minValue`/`maxValue` - the table
            leaves those at zero - so it is taken from the choice list. */
        float clampToRange (PID pid, float v)
        {
            const auto& d = ParameterRegistry::definition (pid);

            switch (d.kind)
            {
                case ParamKind::boolean:
                    return v > 0.5f ? 1.0f : 0.0f;

                case ParamKind::choice:
                {
                    const int n = juce::jmax (1, ParameterRegistry::choicesOf (pid).size());
                    return (float) juce::jlimit (0, n - 1, juce::roundToInt (v));
                }

                case ParamKind::floatValue:
                default:
                    return juce::jlimit (d.minValue, d.maxValue, v);
            }
        }

        juce::String tagsToString (const juce::StringArray& tags)
        {
            return tags.joinIntoString (",");
        }

        juce::StringArray tagsFromString (const juce::String& s)
        {
            auto t = juce::StringArray::fromTokens (s, ",", "");
            t.trim();
            t.removeEmptyStrings();
            return t;
        }
    }

    // =======================================================================
    PresetManager::PresetManager (const ParameterRegistry& r, StateManager& s)
        : registry (r), state (s)
    {
        refresh();
    }

    juce::File PresetManager::userPresetDirectory()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("NACAR")
                       .getChildFile ("Presets");

        if (! dir.isDirectory())
            dir.createDirectory();

        return dir;
    }

    // -----------------------------------------------------------------------
    //  Payloads
    // -----------------------------------------------------------------------
    PresetManager::Payload PresetManager::payloadOf (const presets::FactoryPreset& p)
    {
        Payload out;
        out.values = p.values;
        out.fxOrder = p.fxOrder;

        for (const auto& m : p.mods)
            out.mods.push_back ({ juce::String (m.source),
                                  juce::String (ParameterRegistry::idOf (m.target)),
                                  (double) m.depth,
                                  true });

        return out;
    }

    juce::String PresetManager::fxOrderOf (const Payload& p)
    {
        return p.fxOrder.isNotEmpty() ? p.fxOrder
                                      : juce::String ("RETRO,CRUSH,FILTER,REWIND,GRAIN,SPACE");
    }

    juce::ValueTree PresetManager::makeModMatrixTree (const Payload& p)
    {
        juce::ValueTree matrix (ids::MODMATRIX);

        // The matrix reads at most eight children and resolves each target
        // through the registry, so a slot naming a control this build does not
        // have goes inert rather than pointing at whatever now sits there.
        const int n = juce::jmin (8, (int) p.mods.size());

        for (int i = 0; i < n; ++i)
        {
            const auto& r = p.mods[(size_t) i];

            juce::ValueTree slot (ids::MODSLOT);
            slot.setProperty (ids::modSource,  r.source, nullptr);
            slot.setProperty (ids::modTarget,  r.target, nullptr);
            slot.setProperty (ids::modDepth,   juce::jlimit (-1.0, 1.0, r.depth), nullptr);
            slot.setProperty (ids::modEnabled, r.enabled, nullptr);

            matrix.addChild (slot, -1, nullptr);
        }

        // The MOD page expects eight slots to exist so it has eight rows to
        // draw; the empty ones are inert because their source is NONE.
        for (int i = n; i < 8; ++i)
        {
            juce::ValueTree slot (ids::MODSLOT);
            slot.setProperty (ids::modSource,  "NONE", nullptr);
            slot.setProperty (ids::modTarget,  "",     nullptr);
            slot.setProperty (ids::modDepth,   0.0,    nullptr);
            slot.setProperty (ids::modEnabled, false,  nullptr);

            matrix.addChild (slot, -1, nullptr);
        }

        return matrix;
    }

    // -----------------------------------------------------------------------
    //  Applying
    // -----------------------------------------------------------------------
    void PresetManager::applyParameters (const ParameterRegistry& registry, const Payload& p)
    {
        // Start from the defaults so a preset is a complete statement rather
        // than a diff against whatever was loaded before it.
        std::array<float, (size_t) numParameters> target {};

        for (int i = 0; i < numParameters; ++i)
            target[(size_t) i] = ParameterRegistry::defaultRealValue ((PID) i);

        for (const auto& v : p.values)
        {
            const int index = (int) v.pid;

            if (juce::isPositiveAndBelow (index, numParameters))
                target[(size_t) index] = clampToRange (v.pid, v.value);
        }

        for (int i = 0; i < numParameters; ++i)
        {
            const auto pid = (PID) i;
            auto* rp = registry.parameter (pid);

            if (rp == nullptr)
                continue;

            // Compare in normalised space: that is the number the host stores,
            // so it is the one that decides whether the host would see a
            // change at all.  -Wfloat-equal is on for this build, and rightly,
            // so this is a tolerance rather than an equality test.
            const float wanted = rp->convertTo0to1 (target[(size_t) i]);
            const float held   = rp->convertTo0to1 (registry.userValue (pid));

            if (std::abs (wanted - held) > 1.0e-6f)
                registry.setFromUI (pid, target[(size_t) i]);
        }
    }

    bool PresetManager::apply (int index)
    {
        if (! juce::isPositiveAndBelow (index, library.size()))
            return false;

        auto& info = library.getReference (index);

        Payload payload;

        if (info.isFactory())
        {
            const auto& factory = presets::factoryLibrary();

            if (! juce::isPositiveAndBelow (info.factoryIndex, (int) factory.size()))
                return false;

            payload = payloadOf (factory[(size_t) info.factoryIndex]);
        }
        else
        {
            PresetInfo onDisk;

            if (! info.file.existsAsFile() || ! readPresetFile (info.file, onDisk, payload))
                return false;
        }

        // 1. the parameters, through the gesture protocol.
        applyParameters (registry, payload);

        // 2 and 3. the chain order and the modulation matrix.
        writePayloadToSession (state, payload);

        // 4. identity, so the header bar and the next session save agree with
        //    what is actually loaded.
        info.lastUsed = juce::Time::currentTimeMillis();
        current = index;

        writeIdentityToSession (info);
        saveCollections();

        return true;
    }

    bool PresetManager::step (int delta)
    {
        if (library.isEmpty() || delta == 0)
            return false;

        const int n = library.size();
        const int from = current < 0 ? (delta > 0 ? -1 : 0) : current;

        int next = (from + delta) % n;

        if (next < 0)
            next += n;

        return apply (next);
    }

    void PresetManager::writePayloadToSession (StateManager& state, const Payload& payload)
    {
        // The chain order.  The processor listens to this tree and republishes
        // the packed order to the audio thread itself.
        auto chain = state.group (ids::FXCHAIN);
        chain.setProperty (ids::fxOrder, fxOrderOf (payload), nullptr);
        chain.setProperty ("fxBypass", "", nullptr);

        // The modulation matrix, written INTO the existing slot nodes rather
        // than over them.  The MOD page caches one ValueTree handle per row and
        // only re-fetches when the MODMATRIX node itself is replaced, so
        // swapping the children out would leave its eight rows holding trees
        // that are no longer in the session - the page would show the old
        // routings and editing one would reach nothing.  Setting the properties
        // in place also fires the property-changed callback the processor uses
        // to republish the matrix.
        auto matrix = state.group (ids::MODMATRIX);
        const auto replacement = makeModMatrixTree (payload);

        while (matrix.getNumChildren() > replacement.getNumChildren())
            matrix.removeChild (matrix.getNumChildren() - 1, nullptr);

        while (matrix.getNumChildren() < replacement.getNumChildren())
            matrix.addChild (juce::ValueTree (ids::MODSLOT), -1, nullptr);

        for (int i = 0; i < replacement.getNumChildren(); ++i)
        {
            auto dst = matrix.getChild (i);
            const auto src = replacement.getChild (i);

            dst.setProperty (ids::modSource,  src.getProperty (ids::modSource),  nullptr);
            dst.setProperty (ids::modTarget,  src.getProperty (ids::modTarget),  nullptr);
            dst.setProperty (ids::modDepth,   src.getProperty (ids::modDepth),   nullptr);
            dst.setProperty (ids::modEnabled, src.getProperty (ids::modEnabled), nullptr);
        }
    }

    bool PresetManager::applyFactoryPresetByName (const ParameterRegistry& registry,
                                                  StateManager& state,
                                                  const juce::String& name)
    {
        const auto& factory = presets::factoryLibrary();

        const auto match = std::find_if (factory.begin(), factory.end(),
                                         [&name] (const presets::FactoryPreset& p)
                                         { return p.name == name; });

        if (match == factory.end())
            return false;

        const auto payload = payloadOf (*match);

        applyParameters (registry, payload);
        writePayloadToSession (state, payload);

        auto preset = state.group (ids::PRESET);

        preset.setProperty (ids::presetName,      match->name, nullptr);
        preset.setProperty (ids::presetAuthor,    presets::factoryAuthor(), nullptr);
        preset.setProperty (ids::presetCategory,  match->category, nullptr);
        preset.setProperty (ids::presetMood,      match->mood, nullptr);
        preset.setProperty (ids::presetTags,      match->tags.joinIntoString (","), nullptr);
        preset.setProperty (ids::presetFavourite, false, nullptr);

        return true;
    }

    void PresetManager::writeIdentityToSession (const PresetInfo& info)
    {
        auto preset = state.group (ids::PRESET);

        preset.setProperty (ids::presetName,      info.name, nullptr);
        preset.setProperty (ids::presetAuthor,    info.author, nullptr);
        preset.setProperty (ids::presetCategory,  info.category, nullptr);
        preset.setProperty (ids::presetMood,      info.mood, nullptr);
        preset.setProperty (ids::presetTags,      tagsToString (info.tags), nullptr);
        preset.setProperty (ids::presetFavourite, info.favourite, nullptr);
        preset.setProperty (ids::presetFile,      info.file.getFullPathName(), nullptr);
    }

    // -----------------------------------------------------------------------
    //  Files
    // -----------------------------------------------------------------------
    bool PresetManager::readPresetFile (const juce::File& file, PresetInfo& info, Payload& payload)
    {
        if (! file.existsAsFile())
            return false;

        auto xml = juce::parseXML (file);

        if (xml == nullptr)
            return false;

        const auto tree = juce::ValueTree::fromXml (*xml);

        if (! tree.isValid() || ! tree.hasType (kPreset))
            return false;

        // Read but not branched on: see `presetFormatVersion`.  A file with a
        // version this build has never heard of is still read, because every
        // property in it is addressed by name.
        const int fileVersion = (int) tree.getProperty (kFormatVersion, presetFormatVersion);
        juce::ignoreUnused (fileVersion);

        info = {};
        info.name     = tree.getProperty (ids::presetName).toString();
        info.author   = tree.getProperty (ids::presetAuthor).toString();
        info.category = tree.getProperty (ids::presetCategory).toString();
        info.mood     = tree.getProperty (ids::presetMood).toString();
        // Nothing in the interface writes a blurb for a user preset yet - the
        // save path has no field for one - but a hand-edited file may carry
        // one, and the browser will show it if it does.
        info.blurb    = tree.getProperty (kBlurb).toString();
        info.tags     = tagsFromString (tree.getProperty (ids::presetTags).toString());
        info.file     = file;

        if (info.name.isEmpty())
            info.name = file.getFileNameWithoutExtension();

        payload = {};

        if (const auto params = tree.getChildWithName (kParams); params.isValid())
        {
            for (int i = 0; i < params.getNumChildren(); ++i)
            {
                const auto p = params.getChild (i);

                if (! p.hasType (kParam))
                    continue;

                // Resolved by permanent string ID, so the file survives the
                // parameter list growing and an ID this build does not know is
                // dropped instead of landing on the wrong control.
                const auto pid = ParameterRegistry::fromString (
                                     p.getProperty (kParamId).toString());

                if (pid == PID::count)
                    continue;

                payload.values.push_back ({ pid, (float) (double) p.getProperty (kParamValue) });
            }
        }

        if (const auto chain = tree.getChildWithName (ids::FXCHAIN); chain.isValid())
            payload.fxOrder = chain.getProperty (ids::fxOrder).toString();

        if (const auto matrix = tree.getChildWithName (ids::MODMATRIX); matrix.isValid())
        {
            for (int i = 0; i < matrix.getNumChildren(); ++i)
            {
                const auto slot = matrix.getChild (i);

                if (! slot.hasType (ids::MODSLOT))
                    continue;

                Routing r;
                r.source  = slot.getProperty (ids::modSource).toString();
                r.target  = slot.getProperty (ids::modTarget).toString();
                r.depth   = (double) slot.getProperty (ids::modDepth);
                r.enabled = (bool) slot.getProperty (ids::modEnabled);

                payload.mods.push_back (r);
            }
        }

        return true;
    }

    int PresetManager::saveUserPreset (const juce::String& name,
                                       const juce::String& category,
                                       const juce::String& mood,
                                       const juce::StringArray& tags)
    {
        const auto trimmed = name.trim();

        if (trimmed.isEmpty())
            return -1;

        juce::ValueTree tree (kPreset);
        tree.setProperty (kFormatVersion, presetFormatVersion, nullptr);
        tree.setProperty (ids::presetName,     trimmed, nullptr);
        tree.setProperty (ids::presetAuthor,   "User", nullptr);
        tree.setProperty (ids::presetCategory, category, nullptr);
        tree.setProperty (ids::presetMood,     mood, nullptr);
        tree.setProperty (ids::presetTags,     tagsToString (tags), nullptr);

        // Every parameter, by permanent string ID, at the value the user set -
        // never the modulated value, or a preset saved while an LFO was running
        // would recall wherever the LFO happened to be.
        juce::ValueTree params (kParams);

        for (int i = 0; i < numParameters; ++i)
        {
            const auto pid = (PID) i;

            juce::ValueTree p (kParam);
            p.setProperty (kParamId,    juce::String (ParameterRegistry::idOf (pid)), nullptr);
            p.setProperty (kParamValue, (double) registry.userValue (pid), nullptr);

            params.addChild (p, -1, nullptr);
        }

        tree.addChild (params, -1, nullptr);

        if (const auto chain = state.session().getChildWithName (ids::FXCHAIN); chain.isValid())
        {
            juce::ValueTree saved (ids::FXCHAIN);
            saved.setProperty (ids::fxOrder, chain.getProperty (ids::fxOrder).toString(), nullptr);
            tree.addChild (saved, -1, nullptr);
        }

        if (const auto matrix = state.session().getChildWithName (ids::MODMATRIX); matrix.isValid())
            tree.addChild (matrix.createCopy(), -1, nullptr);

        const auto file = userPresetDirectory()
                              .getChildFile (juce::File::createLegalFileName (trimmed)
                                             + fileExtension());

        if (auto xml = tree.createXml())
            if (! xml->writeTo (file, {}))
                return -1;

        refresh();

        const int index = indexOfName (trimmed);

        if (index >= 0)
        {
            current = index;
            writeIdentityToSession (library.getReference (index));
        }

        return index;
    }

    bool PresetManager::deleteUserPreset (int index)
    {
        if (! juce::isPositiveAndBelow (index, library.size()))
            return false;

        const auto& info = library.getReference (index);

        if (info.isFactory() || ! info.file.existsAsFile())
            return false;

        if (! info.file.deleteFile())
            return false;

        refresh();
        return true;
    }

    // -----------------------------------------------------------------------
    //  The library
    // -----------------------------------------------------------------------
    void PresetManager::refresh()
    {
        const auto previousName = juce::isPositiveAndBelow (current, library.size())
                                      ? library.getReference (current).name
                                      : juce::String();

        library.clearQuick();
        loadCollections();

        const auto& factory = presets::factoryLibrary();

        for (int i = 0; i < (int) factory.size(); ++i)
        {
            const auto& f = factory[(size_t) i];

            PresetInfo info;
            info.name         = f.name;
            info.author       = presets::factoryAuthor();
            info.category     = f.category;
            info.mood         = f.mood;
            info.tags         = f.tags;
            info.blurb        = f.blurb;
            info.factoryIndex = i;

            library.add (info);
        }

        // User presets after the factory set, alphabetically among themselves,
        // so the factory order - which is by category - is not shuffled by what
        // happens to be on disk.
        juce::Array<juce::File> files;
        userPresetDirectory().findChildFiles (files, juce::File::findFiles, false,
                                              "*" + fileExtension());

        std::sort (files.begin(), files.end(),
                   [] (const juce::File& a, const juce::File& b)
                   {
                       return a.getFileName().compareIgnoreCase (b.getFileName()) < 0;
                   });

        for (const auto& file : files)
        {
            PresetInfo info;
            Payload ignored;

            if (readPresetFile (file, info, ignored))
                library.add (info);
        }

        // Favourites and last-used are stored centrally rather than in the
        // files, because a factory preset has no file to carry them.
        for (auto& info : library)
        {
            const auto entry = collections.getChildWithProperty (kEntryName, info.name);

            if (entry.isValid())
            {
                info.favourite = (bool) entry.getProperty (ids::presetFavourite);
                info.lastUsed  = (juce::int64) (double) entry.getProperty ("lastUsed");
            }
        }

        current = previousName.isNotEmpty() ? indexOfName (previousName) : -1;

        // Nothing has been applied yet, but the session already names a preset:
        // either a host restored one, or this is a fresh session and
        // StateManager's defaults named it.  Adopting that name as the current
        // index is bookkeeping only - no parameter is touched - and it is what
        // makes the first press of "next preset" step from where the header bar
        // says we are rather than from the top of the list.
        if (current < 0)
        {
            const auto named = state.session().getChildWithName (ids::PRESET)
                                   .getProperty (ids::presetName).toString();

            if (named.isNotEmpty())
                current = indexOfName (named);
        }
    }

    int PresetManager::indexOfName (const juce::String& name) const
    {
        for (int i = 0; i < library.size(); ++i)
            if (library.getReference (i).name == name)
                return i;

        return -1;
    }

    void PresetManager::setFavourite (int index, bool shouldBeFavourite)
    {
        if (! juce::isPositiveAndBelow (index, library.size()))
            return;

        library.getReference (index).favourite = shouldBeFavourite;

        if (index == current)
            state.group (ids::PRESET).setProperty (ids::presetFavourite,
                                                   shouldBeFavourite, nullptr);

        saveCollections();
    }

    void PresetManager::loadCollections()
    {
        collections = juce::ValueTree (kCollections);

        const auto file = userPresetDirectory().getChildFile ("Library.settings");

        if (! file.existsAsFile())
            return;

        if (auto xml = juce::parseXML (file))
            if (auto tree = juce::ValueTree::fromXml (*xml); tree.isValid() && tree.hasType (kCollections))
                collections = tree;
    }

    void PresetManager::saveCollections() const
    {
        juce::ValueTree tree (kCollections);

        for (const auto& info : library)
        {
            if (! info.favourite && info.lastUsed == 0)
                continue;

            juce::ValueTree entry (kEntry);
            entry.setProperty (kEntryName, info.name, nullptr);
            entry.setProperty (ids::presetFavourite, info.favourite, nullptr);
            entry.setProperty ("lastUsed", (double) info.lastUsed, nullptr);

            tree.addChild (entry, -1, nullptr);
        }

        if (auto xml = tree.createXml())
            xml->writeTo (userPresetDirectory().getChildFile ("Library.settings"), {});
    }
}
