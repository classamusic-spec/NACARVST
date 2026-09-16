#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

#include "../Plugin/ParameterRegistry.h"
#include "Sources/Synth/SynthEngine.h"

namespace nacar
{
    /** The six reorderable chain slots.  Display order is DSP order. */
    enum class FxSlot { retro = 0, crush, filter, rewind, grain, space, count };

    inline constexpr int numFxSlots = (int) FxSlot::count;

    /** Canonical slot name, as stored in the FXCHAIN state tree. */
    const char* fxSlotName (FxSlot) noexcept;

    /** Parses a slot name back.  Returns FxSlot::count when unknown. */
    FxSlot fxSlotFromName (juce::StringRef) noexcept;

    /**
        The chain order, handed across the thread boundary as a packed integer.

        The audio thread must never read a ValueTree, so the editor resolves the
        order once, packs it into 32 bits (six 3-bit slot indices plus a count)
        and publishes it with a single atomic store.  The audio thread unpacks
        it at the top of each block.
    */
    struct FxOrder
    {
        std::array<FxSlot, (size_t) numFxSlots> slots {};
        int count = 0;

        static FxOrder defaultOrder() noexcept;
        static FxOrder fromCommaSeparated (juce::StringRef) noexcept;

        juce::uint32 pack() const noexcept;
        static FxOrder unpack (juce::uint32) noexcept;
    };

    /**
        THE NACAR SIGNAL CHAIN.

        Owns every engine and routes between them.  The processor holds one of
        these and does nothing but hand it a buffer.

            SOURCE (synth / sample / grain / resonator / spectral)
              -> MEMORY
              -> FX CHAIN, in the user's order
              -> ATMOSPHERE (aura, shadow, patina)
              -> WEIGHT
              -> output

        Realtime contract: after prepare(), nothing below allocates, locks,
        touches the filesystem or logs.
    */
    class NacarEngine
    {
    public:
        NacarEngine();
        ~NacarEngine();

        void prepare (double sampleRate, int maximumBlockSize, int numChannels);
        void reset();

        /** Forces maximum quality regardless of the quality parameter. */
        void setOfflineRendering (bool) noexcept;

        /** Publishes a new chain order.  Message thread; lock-free. */
        void setFxOrder (const FxOrder&) noexcept;

        /** Renders one block.  Audio thread only. */
        void process (juce::AudioBuffer<float>&, juce::MidiBuffer&,
                      const ParameterRegistry&, double hostBpm);

        void allNotesOff();

        int getActiveVoiceCount() const noexcept;
        float getLastPeak() const noexcept;

        /** Direct access for the parts of the UI that visualise the synth. */
        SynthEngine& getSynth() noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NacarEngine)
    };
}
