#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <atomic>

#include "../../Plugin/ParameterRegistry.h"

namespace nacar
{
    /**
        The modulation sources named by specification section 54, in exactly the
        order the MOD page's source menu lists them.

        The enum's order is load bearing: `modSourceFromName` maps the display
        strings the page writes into the session tree onto these values, and the
        page writes the *name*, not an index, so the two lists must agree by
        name.  Adding a source means adding it to both.
    */
    enum class ModSource : int
    {
        none = 0,
        lfo1, lfo2,
        env1, env2,             ///< per-voice: not available here, see README
        velocity, keyTrack,     ///< per-voice: not available here, see README
        modWheel, aftertouch,
        breath, pulse, organicRandom,
        memory, motion, world, alter,
        count
    };

    inline constexpr int numModSources = (int) ModSource::count;

    /** Parses the display name the MOD page stores.  Message thread: it builds
        juce::Strings.  Returns ModSource::none for anything unrecognised. */
    ModSource modSourceFromName (juce::StringRef) noexcept;

    /** Canonical display name, matching the MOD page's menu exactly. */
    const char* modSourceName (ModSource) noexcept;

    // =======================================================================
    //  MOD MATRIX  -  specification sections 54 and 127
    //
    //  Eight routings, each a source, a target parameter, a bipolar depth and
    //  an enable.  The MOD page persists them in the session tree under
    //  ids::MODMATRIX / ids::MODSLOT; the audio thread must never read a
    //  ValueTree, so the message thread resolves the tree into a fixed array of
    //  plain values and publishes it.
    //
    //  THE HANDOFF.  Two staging buffers and an atomic index, plus a generation
    //  counter that the audio thread validates across its own copy:
    //
    //    writer (message thread)      reader (audio thread, once per block)
    //    ----------------------       ------------------------------------
    //    fill stage[1 - published]    g0 = generation (acquire)
    //    published = that index       if g0 == seen: nothing changed, done
    //    generation += 1              copy stage[published] into `live`
    //                                 g1 = generation (acquire)
    //                                 if g1 == g0: accept, seen = g0
    //                                 else:        discard, keep last block's
    //
    //  The index alone would be *almost* right: two publications inside one
    //  block - which a drag on a depth bar produces easily - can land the second
    //  one in the buffer the audio thread is reading.  The generation counter
    //  closes that, because a torn copy is exactly a copy across which the
    //  counter moved.  The failure mode is one block of staleness during a
    //  drag, never a torn read, and the reader never blocks, spins or retries.
    //
    //  THE UNITS.  `offsetFor` returns a *normalised* offset: the number a
    //  consumer adds to `ParameterRegistry::normalised (pid)` before mapping
    //  back through the parameter's own range.  Depth is bipolar -1..1 and the
    //  global sources are -1..1 (Pulse and the macros are 0..1), so one routing
    //  at full depth can move a parameter across its whole range.  The sum over
    //  routings is clamped to -1..1, because more than a full range of offset
    //  means nothing.
    // =======================================================================
    class ModMatrix
    {
    public:
        static constexpr int numSlots = 8;

        struct Routing
        {
            ModSource source = ModSource::none;
            PID       target = PID::count;      ///< PID::count means unresolved
            float     depth = 0.0f;             ///< -1..1
            bool      enabled = false;
        };

        using Set = std::array<Routing, (size_t) numSlots>;
        using SourceValues = std::array<float, (size_t) numModSources>;

        ModMatrix();

        /** Re-reads the MODMATRIX tree and publishes the result.  Message
            thread only: it compares juce::Strings and resolves parameter IDs. */
        void rebuildFromTree (const juce::ValueTree& matrixTree);

        /** Clears every routing and publishes that.  Message thread. */
        void clear();

        /** Latches this block's source values and picks up any newly published
            routing set.  Audio thread, once per block, before any query. */
        void beginBlock (const SourceValues&) noexcept;

        /** Total modulation offset for one parameter this block, in normalised
            units, -1..1.  Audio thread.  O(1). */
        float offsetFor (PID) const noexcept;

        /** The routing set the audio thread is currently using.  For tests. */
        const Set& liveRoutings() const noexcept { return live; }

    private:
        void publish (const Set&) noexcept;
        void refreshLive() noexcept;

        // -- published across the thread boundary ---------------------------
        Set stage[2];
        std::atomic<int> published { 0 };
        std::atomic<juce::uint32> generation { 0 };

        // -- audio thread only ----------------------------------------------
        Set live {};
        juce::uint32 seenGeneration = 0;

        SourceValues sources {};

        std::array<float, (size_t) numParameters> offsets {};
        int touched[numSlots] {};
        int numTouched = 0;
    };
}
