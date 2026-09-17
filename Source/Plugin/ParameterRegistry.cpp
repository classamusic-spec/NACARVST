#include "ParameterRegistry.h"

#include <cmath>
#include <string>
#include <unordered_map>

namespace nacar
{
    // -----------------------------------------------------------------------
    //  Static table, generated from NACAR_PARAMETERS
    // -----------------------------------------------------------------------
    static const std::array<ParamDef, numParameters>& buildTable()
    {
        static const std::array<ParamDef, numParameters> table =
        {{
            #define NACAR_DEF_FLOAT(member, pid, nm, mn, mx, dflt, skw, unt, tip) \
                ParamDef { PID::member, pid, nm, ParamKind::floatValue, mn, mx, dflt, skw, unt, tip, nullptr, 0 },
            #define NACAR_DEF_CHOICE(member, pid, nm, choices, dflt, tip) \
                ParamDef { PID::member, pid, nm, ParamKind::choice, 0.0f, 0.0f, (float) dflt, 1.0f, "", tip, choices, dflt },
            #define NACAR_DEF_BOOL(member, pid, nm, dflt, tip) \
                ParamDef { PID::member, pid, nm, ParamKind::boolean, 0.0f, 1.0f, dflt ? 1.0f : 0.0f, 1.0f, "", tip, nullptr, 0 },

            NACAR_PARAMETERS (NACAR_DEF_FLOAT, NACAR_DEF_CHOICE, NACAR_DEF_BOOL)

            #undef NACAR_DEF_FLOAT
            #undef NACAR_DEF_CHOICE
            #undef NACAR_DEF_BOOL
        }};

        return table;
    }

    const std::array<ParamDef, numParameters>& ParameterRegistry::allDefinitions() noexcept
    {
        return buildTable();
    }

    const ParamDef& ParameterRegistry::definition (PID p) noexcept
    {
        jassert (p != PID::count);
        return buildTable()[(size_t) p];
    }

    const char* ParameterRegistry::idOf (PID p) noexcept
    {
        return definition (p).id;
    }

    PID ParameterRegistry::fromString (juce::StringRef s) noexcept
    {
        // Built once; the table is immutable so a flat map is enough.
        static const std::unordered_map<std::string, PID> lookup = []
        {
            std::unordered_map<std::string, PID> m;
            for (const auto& d : buildTable())
                m.emplace (d.id, d.pid);
            return m;
        }();

        const auto it = lookup.find (juce::String (s).toStdString());
        return it == lookup.end() ? PID::count : it->second;
    }

    // -----------------------------------------------------------------------
    //  Layout
    // -----------------------------------------------------------------------
    static juce::StringArray splitChoices (const char* pipeSeparated)
    {
        return juce::StringArray::fromTokens (juce::String (pipeSeparated), "|", "");
    }

    /**  Formats a value the way a producer expects to read it, not the way a
         float prints.  Times switch between ms and s, frequencies between Hz
         and kHz, normalised ranges read as percentages.                     */
    static juce::String formatReal (const ParamDef& d, float v)
    {
        const juce::String unit (d.unit);

        if (unit == "%")
            return juce::String (juce::roundToInt (v * 100.0f)) + " %";

        if (unit == "Hz")
            return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                                : juce::String (v, v < 100.0f ? 1 : 0) + " Hz";

        if (unit == "s")
            return v < 1.0f ? juce::String (v * 1000.0f, v < 0.01f ? 2 : 1) + " ms"
                            : juce::String (v, 2) + " s";

        if (unit == "ms")  return juce::String (v, 1) + " ms";
        if (unit == "dB")  return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
        if (unit == "st")  return (v > 0.0f ? "+" : "") + juce::String (v, 2) + " st";
        if (unit == "ct")  return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " ct";
        if (unit == "oct") return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " oct";
        if (unit == "x")   return juce::String (v, 2) + " x";
        if (unit == "/s")  return juce::String (v, 1) + " /s";
        if (unit == "bit") return juce::String (juce::roundToInt (v)) + " bit";

        // Unitless: bipolar controls read with a sign, counts read as integers.
        if (d.minValue < 0.0f)
            return (v > 0.0f ? "+" : "") + juce::String (v, 2);

        if (d.maxValue > 1.5f && std::abs (v - std::round (v)) < 1.0e-4f)
            return juce::String (juce::roundToInt (v));

        return juce::String (v, 2);
    }

    juce::AudioProcessorValueTreeState::ParameterLayout ParameterRegistry::createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        for (const auto& d : buildTable())
        {
            const juce::ParameterID pid { d.id, 1 };

            switch (d.kind)
            {
                case ParamKind::floatValue:
                {
                    // Integer-like controls (voice counts, octaves, bit depth)
                    // snap; everything else is continuous.
                    const juce::String unit (d.unit);
                    const bool integral = (unit.isEmpty() || unit == "oct" || unit == "bit")
                                          && d.maxValue > 1.5f
                                          && d.maxValue <= 128.0f
                                          && std::abs (d.minValue - std::round (d.minValue)) < 1.0e-6f
                                          && std::abs (d.maxValue - std::round (d.maxValue)) < 1.0e-6f;

                    juce::NormalisableRange<float> range (d.minValue, d.maxValue,
                                                          integral ? 1.0f : 0.0f,
                                                          d.skew);

                    const auto def = d;
                    layout.add (std::make_unique<juce::AudioParameterFloat> (
                        pid, d.name, range, d.defaultValue,
                        juce::AudioParameterFloatAttributes()
                            .withLabel (d.unit)
                            .withStringFromValueFunction ([def] (float v, int)
                                                          { return formatReal (def, v); })));
                    break;
                }

                case ParamKind::choice:
                    layout.add (std::make_unique<juce::AudioParameterChoice> (
                        pid, d.name, splitChoices (d.choicesPipeSeparated), d.defaultChoice));
                    break;

                case ParamKind::boolean:
                    layout.add (std::make_unique<juce::AudioParameterBool> (
                        pid, d.name, d.defaultValue > 0.5f));
                    break;
            }
        }

        return layout;
    }

    // -----------------------------------------------------------------------
    //  Attachment
    // -----------------------------------------------------------------------
    ParameterRegistry::ParameterRegistry()
    {
        // std::atomic<float> is not zero-initialised by its default
        // constructor, and an indeterminate override would make raw() return
        // garbage for every parameter before the first block.
        clearAllModulation();
    }

    void ParameterRegistry::attach (juce::AudioProcessorValueTreeState& s)
    {
        apvts = &s;

        for (const auto& d : buildTable())
        {
            const auto i = (size_t) d.pid;
            values[i] = s.getRawParameterValue (d.id);
            params[i] = s.getParameter (d.id);

            // A null here means the layout and the table disagree, which can
            // only happen if someone edited one without the other.
            jassert (values[i] != nullptr && params[i] != nullptr);
        }
    }

    void ParameterRegistry::attachSnapshot (std::array<std::atomic<float>, numParameters>& storage) noexcept
    {
        // Deliberately leaves `apvts` and `params` null: see the header. The
        // UI-writing methods all guard on a null RangedAudioParameter already,
        // so they no-op rather than crash if one is ever reached from here -
        // but reaching one means somebody is editing a snapshot, which is a
        // mistake this cannot make safe.
        apvts = nullptr;

        for (int i = 0; i < numParameters; ++i)
        {
            values[(size_t) i] = &storage[(size_t) i];
            params[(size_t) i] = nullptr;
        }
    }

    juce::RangedAudioParameter* ParameterRegistry::parameter (PID p) const noexcept
    {
        return params[(size_t) p];
    }

    float ParameterRegistry::normalised (PID p) const noexcept
    {
        if (auto* rp = params[(size_t) p])
            return rp->convertTo0to1 (raw (p));

        return 0.0f;
    }

    float ParameterRegistry::normalisedUserValue (PID p) const noexcept
    {
        if (auto* rp = params[(size_t) p])
            return rp->convertTo0to1 (userValue (p));

        return 0.0f;
    }

    // -----------------------------------------------------------------------
    //  The modulation overlay
    // -----------------------------------------------------------------------
    void ParameterRegistry::setModulation (PID p, float normalisedOffset) const noexcept
    {
        auto* rp = params[(size_t) p];

        if (rp == nullptr || values[(size_t) p] == nullptr)
            return;

        // Clamped in NORMALISED space, before the range's own skew is undone.
        // Clamping the real value instead would let a skewed parameter - every
        // frequency in the instrument - travel a different distance upwards
        // than downwards for the same depth, which reads as a matrix that is
        // stronger in one direction.
        const float base = rp->convertTo0to1 (values[(size_t) p]->load (std::memory_order_relaxed));
        const float sum  = juce::jlimit (0.0f, 1.0f, base + normalisedOffset);

        modOverride[(size_t) p].store (rp->convertFrom0to1 (sum), std::memory_order_relaxed);
    }

    void ParameterRegistry::clearModulation (PID p) const noexcept
    {
        modOverride[(size_t) p].store (noModulation, std::memory_order_relaxed);
    }

    void ParameterRegistry::clearAllModulation() const noexcept
    {
        for (auto& m : modOverride)
            m.store (noModulation, std::memory_order_relaxed);
    }

    juce::String ParameterRegistry::formatValue (PID p) const
    {
        const auto& d = definition (p);

        // userValue, not raw: this feeds tooltips and typed value entry, and a
        // field that moves while an LFO is running cannot be typed into.
        const float v = userValue (p);

        if (d.kind == ParamKind::boolean)
            return v > 0.5f ? "ON" : "OFF";

        if (d.kind == ParamKind::choice)
        {
            const auto c = splitChoices (d.choicesPipeSeparated);
            return juce::isPositiveAndBelow ((int) v, c.size()) ? c[(int) v] : juce::String();
        }

        return formatReal (d, v);
    }

    void ParameterRegistry::setFromUI (PID p, float newRealValue) const
    {
        // One-shot edit: wrapped in its own gesture so the host records a
        // single discrete change.  Drags use begin/set/end instead.
        if (auto* rp = params[(size_t) p])
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->convertTo0to1 (newRealValue));
            rp->endChangeGesture();
        }
    }

    void ParameterRegistry::beginGesture (PID p) const
    {
        if (auto* rp = params[(size_t) p])
            rp->beginChangeGesture();
    }

    void ParameterRegistry::setDuringGesture (PID p, float newRealValue) const
    {
        if (auto* rp = params[(size_t) p])
            rp->setValueNotifyingHost (rp->convertTo0to1 (newRealValue));
    }

    void ParameterRegistry::endGesture (PID p) const
    {
        if (auto* rp = params[(size_t) p])
            rp->endChangeGesture();
    }

    juce::StringArray ParameterRegistry::choicesOf (PID p)
    {
        const auto& d = definition (p);
        return d.choicesPipeSeparated != nullptr ? splitChoices (d.choicesPipeSeparated)
                                                 : juce::StringArray();
    }

    float ParameterRegistry::defaultRealValue (PID p) noexcept
    {
        return definition (p).defaultValue;
    }
}
