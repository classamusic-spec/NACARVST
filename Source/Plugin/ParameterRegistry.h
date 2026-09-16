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
    // -----------------------------------------------------------------------
    class ParameterRegistry
    {
    public:
        ParameterRegistry() = default;

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

        // -- audio-thread accessors, all lock-free --------------------------

        /** Current value in real units (Hz, seconds, dB, 0..1, choice index). */
        forcedinline float raw (PID p) const noexcept
        {
            jassert (values[(size_t) p] != nullptr);
            return values[(size_t) p]->load (std::memory_order_relaxed);
        }

        forcedinline bool flag (PID p) const noexcept   { return raw (p) > 0.5f; }
        forcedinline int  choice (PID p) const noexcept { return (int) raw (p); }

        /** Value mapped to 0..1 across the parameter's range. */
        float normalised (PID) const noexcept;

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

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterRegistry)
    };
}
