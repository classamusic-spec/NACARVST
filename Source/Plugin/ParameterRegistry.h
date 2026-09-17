#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

#include "ParameterList.h"

namespace nacar
{
    // -----------------------------------------------------------------------
    //  PID - a stable compile-time handle for every parameter.
    //
    //  Generated from NACAR_PARAMETERS so it can never fall out of step with
    //  the string IDs.  Audio-thread code addresses parameters by PID; only
    //  the registry ever touches a string ID.
    // -----------------------------------------------------------------------
    enum class PID : int
    {
        #define NACAR_PID_FLOAT(member, ...)  member,
        #define NACAR_PID_CHOICE(member, ...) member,
        #define NACAR_PID_BOOL(member, ...)   member,
        NACAR_PARAMETERS (NACAR_PID_FLOAT, NACAR_PID_CHOICE, NACAR_PID_BOOL)
        #undef NACAR_PID_FLOAT
        #undef NACAR_PID_CHOICE
        #undef NACAR_PID_BOOL

        count
    };

    inline constexpr int numParameters = (int) PID::count;

    enum class ParamKind { floatValue, choice, boolean };

    /** Static description of one parameter. */
    struct ParamDef
    {
        PID              pid;
        const char*      id;          ///< permanent string ID. Never change this.
        const char*      name;
        ParamKind        kind;

        float            minValue;
        float            maxValue;
        float            defaultValue;
        float            skew;

        const char*      unit;
        const char*      tooltip;
        const char*      choicesPipeSeparated;   ///< nullptr unless kind == choice
        int              defaultChoice;
    };

    // -----------------------------------------------------------------------
    //  ParameterRegistry
    //
    //  Owns the static parameter table, builds the APVTS layout, and caches one
    //  raw atomic pointer per parameter so the audio thread never performs a
    //  string lookup or takes a lock.
    //
    //  THE MODULATION OVERLAY.  A parameter has two values and both are real:
    //  the one the user set, and the one actually in force once the modulation
    //  matrix has had its say.  `raw()` returns the second, because that is
    //  what an engine means when it asks for a cutoff; `userValue()` returns
    //  the first, because that is what a knob should draw and what a preset
    //  should store.
    //
    //  The overlay is written once per block by NacarEngine from the resolved
    //  matrix, and only for the at-most-eight parameters a routing actually
    //  targets.  Every other entry holds `noModulation`, which costs one
    //  relaxed load and one compare on the read path.  Nothing else writes it:
    //  the APVTS is never touched, so a modulated parameter does not drift, is
    //  not saved modulated, and does not fight the user's own edits.
    // -----------------------------------------------------------------------
    class ParameterRegistry
    {
    public:
        ParameterRegistry();

        /** Builds the layout handed to the AudioProcessorValueTreeState. */
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

        /** Static table lookup. Valid before the APVTS exists. */
        static const ParamDef& definition (PID) noexcept;
        static const std::array<ParamDef, numParameters>& allDefinitions() noexcept;
        static const char* idOf (PID) noexcept;

        /** Looks a PID up by string ID. Returns PID::count when unknown. */
        static PID fromString (juce::StringRef) noexcept;

        /** Caches the atomic pointers. Call once, from the constructor. */
        void attach (juce::AudioProcessorValueTreeState&);

        /** Attaches to a caller-owned table of values instead of to an APVTS,
            for rendering the instrument offline.

            A print has to be of ONE state. If the renderer read the live
            registry, a knob moved while it worked would land halfway through
            the audio and the file would be of a patch that never existed. So
            the caller snapshots the session into its own storage, attaches to
            that, and renders something that cannot move under it.

            ONLY THE READ PATH IS VALID on a registry attached this way:
            raw(), userValue(), flag() and choice(), which is everything an
            engine asks for. There is no APVTS behind it, so `parameter()`
            returns null and the gesture and setFromUI methods have nothing to
            write to - they are for a user turning a knob, and nobody is
            turning a knob inside an offline render. `storage` must outlive
            this registry. */
        void attachSnapshot (std::array<std::atomic<float>, numParameters>& storage) noexcept;

        /** True when this registry is reading a snapshot rather than an APVTS,
            so a caller can assert it is not about to write through it. */
        bool isSnapshot() const noexcept { return apvts == nullptr && values[0] != nullptr; }

        // -- audio-thread accessors, all lock-free --------------------------

        /** The value in force, in real units (Hz, seconds, dB, 0..1, choice
            index): what the user set, plus whatever the modulation matrix is
            adding to it this block.  This is what every engine should read. */
        forcedinline float raw (PID p) const noexcept
        {
            jassert (values[(size_t) p] != nullptr);

            const float m = modOverride[(size_t) p].load (std::memory_order_relaxed);

            // Ordered, not an equality test: -Wfloat-equal is on for this build
            // and it is right to be.  Every NACAR range sits far above -1e29,
            // so the comparison separates the sentinel from real values without
            // relying on an exact bit pattern surviving the round trip.
            return m > noModulationFloor ? m
                                         : values[(size_t) p]->load (std::memory_order_relaxed);
        }

        /** What the user set, with no modulation applied.  The UI wants this:
            a knob that jitters because an LFO is running is not showing the
            user their own setting. */
        forcedinline float userValue (PID p) const noexcept
        {
            jassert (values[(size_t) p] != nullptr);
            return values[(size_t) p]->load (std::memory_order_relaxed);
        }

        forcedinline bool flag (PID p) const noexcept   { return raw (p) > 0.5f; }
        forcedinline int  choice (PID p) const noexcept { return (int) raw (p); }

        /** Value mapped to 0..1 across the parameter's range.  Follows raw(),
            so it includes modulation. */
        float normalised (PID) const noexcept;

        /** The user's value mapped to 0..1, with no modulation. */
        float normalisedUserValue (PID) const noexcept;

        // -- the modulation overlay -----------------------------------------
        //
        //  Audio thread, once per block, from NacarEngine.  Nothing else may
        //  write these: a second writer would make `raw()` non-deterministic
        //  within a block, which is the one property every engine relies on.

        /** The sentinel that means "this parameter is not modulated", and the
            floor that recognises it.  A real parameter can never reach either:
            the widest range in the instrument is a few thousand. */
        static constexpr float noModulation      = -1.0e30f;
        static constexpr float noModulationFloor = -1.0e29f;

        /** Offsets a parameter by a normalised amount, clamped into its own
            range, and publishes the result for this block.  `normalisedOffset`
            of zero still installs an override, so a routing at zero depth
            behaves identically to one at any other depth. */
        void setModulation (PID, float normalisedOffset) const noexcept;

        /** Removes one override, so raw() falls back to the user's value. */
        void clearModulation (PID) const noexcept;

        /** Removes every override.  Call on reset and whenever the matrix
            stops targeting a parameter it used to target - a stale override
            is a parameter frozen at a modulated value. */
        void clearAllModulation() const noexcept;

        // -- message-thread accessors ---------------------------------------

        juce::RangedAudioParameter* parameter (PID) const noexcept;
        juce::AudioProcessorValueTreeState* state() const noexcept { return apvts; }

        /** Human-readable value including unit, for tooltips and value entry. */
        juce::String formatValue (PID) const;

        /** Sets a parameter from the UI as one discrete, undoable edit. */
        void setFromUI (PID, float newRealValue) const;

        /** Drag protocol: begin, then any number of sets, then end. */
        void beginGesture (PID) const;
        void setDuringGesture (PID, float newRealValue) const;
        void endGesture (PID) const;

        static juce::StringArray choicesOf (PID);
        static float defaultRealValue (PID) noexcept;

    private:
        juce::AudioProcessorValueTreeState* apvts = nullptr;
        std::array<std::atomic<float>*, numParameters> values {};
        std::array<juce::RangedAudioParameter*, numParameters> params {};

        /** Mutable because it is a per-block audio-thread cache, not user
            state: an engine holding a `const ParameterRegistry&` is promising
            not to change what the user set, and this does not. */
        mutable std::array<std::atomic<float>, numParameters> modOverride;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterRegistry)
    };
}
