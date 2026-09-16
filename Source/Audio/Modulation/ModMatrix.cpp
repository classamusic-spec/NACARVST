#include "ModMatrix.h"

#include "../../Plugin/StateManager.h"
#include "../DspCommon.h"

namespace nacar
{
    namespace
    {
        /** The MOD page's source menu, verbatim.  ModPage.cpp writes the
            display name into the tree, so this table and the page's own
            sourceNames() are the two halves of one contract. */
        const char* const kSourceNames[numModSources] = {
            "NONE", "LFO 1", "LFO 2", "ENV 1", "ENV 2", "VELOCITY", "KEY TRACK",
            "MOD WHEEL", "AFTERTOUCH", "BREATH", "PULSE", "ORGANIC RANDOM",
            "MEMORY", "MOTION", "WORLD", "ALTER"
        };
    }

    const char* modSourceName (ModSource s) noexcept
    {
        return kSourceNames[juce::jlimit (0, numModSources - 1, (int) s)];
    }

    ModSource modSourceFromName (juce::StringRef name) noexcept
    {
        for (int i = 0; i < numModSources; ++i)
            if (juce::String (name).equalsIgnoreCase (kSourceNames[i]))
                return (ModSource) i;

        return ModSource::none;
    }

    // -----------------------------------------------------------------------
    ModMatrix::ModMatrix()
    {
        stage[0] = Set {};
        stage[1] = Set {};
        live = Set {};
    }

    void ModMatrix::publish (const Set& s) noexcept
    {
        const int next = 1 - published.load (std::memory_order_relaxed);

        stage[next] = s;

        published.store (next, std::memory_order_release);
        generation.fetch_add (1, std::memory_order_release);
    }

    void ModMatrix::clear()
    {
        publish (Set {});
    }

    void ModMatrix::rebuildFromTree (const juce::ValueTree& matrixTree)
    {
        Set built {};

        const int n = juce::jmin (numSlots, matrixTree.getNumChildren());

        for (int i = 0; i < n; ++i)
        {
            const auto slot = matrixTree.getChild (i);

            if (! slot.hasType (ids::MODSLOT))
                continue;

            auto& r = built[(size_t) i];

            r.source = modSourceFromName (slot.getProperty (ids::modSource).toString());

            // The target is stored as the parameter's permanent string ID, so a
            // routing survives the parameter list growing.  An ID that no
            // longer exists resolves to PID::count and the slot is inert rather
            // than pointing at whatever now sits at that index.
            r.target = ParameterRegistry::fromString (
                           slot.getProperty (ids::modTarget).toString());

            r.depth = juce::jlimit (-1.0f, 1.0f,
                                    (float) (double) slot.getProperty (ids::modDepth));

            r.enabled = (bool) slot.getProperty (ids::modEnabled);
        }

        publish (built);
    }

    // -----------------------------------------------------------------------
    void ModMatrix::refreshLive() noexcept
    {
        const auto g0 = generation.load (std::memory_order_acquire);

        if (g0 == seenGeneration)
            return;

        const int index = published.load (std::memory_order_acquire);
        const Set candidate = stage[(size_t) juce::jlimit (0, 1, index)];

        // If the counter moved while that copy was being taken, the copy may be
        // a mixture of two routing sets.  Discarding it costs one block of
        // staleness; accepting it could enable a routing that was never made.
        if (generation.load (std::memory_order_acquire) != g0)
            return;

        live = candidate;
        seenGeneration = g0;
    }

    void ModMatrix::beginBlock (const SourceValues& v) noexcept
    {
        // Clear only what last block wrote.  numParameters is a few hundred
        // floats and clearing all of them every block would be affordable, but
        // at most eight of them are ever non-zero.
        for (int i = 0; i < numTouched; ++i)
            offsets[(size_t) touched[i]] = 0.0f;

        numTouched = 0;
        sources = v;

        refreshLive();

        for (const auto& r : live)
        {
            if (! r.enabled || r.source == ModSource::none || r.target == PID::count)
                continue;

            const int t = (int) r.target;

            if (t < 0 || t >= numParameters)
                continue;

            if (numTouched < numSlots)
                touched[numTouched++] = t;

            offsets[(size_t) t] += r.depth * sources[(size_t) r.source];
        }
    }

    float ModMatrix::offsetFor (PID p) const noexcept
    {
        const int i = (int) p;

        if (i < 0 || i >= numParameters)
            return 0.0f;

        return juce::jlimit (-1.0f, 1.0f, fx::guard (offsets[(size_t) i]));
    }
}
