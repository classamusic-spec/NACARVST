#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

#include "../Components/Widgets.h"
#include "../../Plugin/ParameterRegistry.h"

namespace nacar::ui
{
    /**
        The mutate panel's intent row.

        Ten ceramic pills - MEMORY CLOUD BROKEN REVERSE DISTANT RHYTHMIC DARK
        GHOST PLAYABLE CINEMATIC - mapped one-to-one onto the choices of
        PID::mutationIntent.  The labels are taken straight from the parameter
        table rather than retyped here, so the pill at index i is always the
        choice at index i and the two can never drift apart.

        The row is always one line.  UI spec section 6 lays the pills out at
        x 357 with an 8 px gap and 34 px of padding each; when that overruns the
        panel the padding is scaled down for every pill by the same amount
        rather than wrapping or clipping a label.  See resized().

        Selection is written through ParameterRegistry::setFromUI, so it is a
        single undoable host edit and reaches the audio thread the same way any
        other parameter change does.
    */
    class IntentSelector : public juce::Component,
                           private juce::Timer
    {
    public:
        /** Choices of PID::mutationIntent.  Asserted against the table in the ctor. */
        static constexpr int numIntents = 10;

        explicit IntentSelector (const ParameterRegistry&);
        ~IntentSelector() override;

        /** Selects an intent.  With sendNotification this also writes the
            parameter and fires onSelect; with dontSendNotification it only
            moves the highlight, which is what the parameter poll uses. */
        void setSelectedIndex (int newIndex,
                               juce::NotificationType = juce::sendNotification);

        int getSelectedIndex() const noexcept { return selectedIndex; }

        /** Width the row would like at Layout.h's full pill padding.  Wider than
            the mutate panel in practice - resized() is what resolves that. */
        float preferredWidth() const;

        std::function<void (int)> onSelect;

        void resized() override;

    private:
        void timerCallback() override;

        const ParameterRegistry& params;
        std::array<std::unique_ptr<PillButton>, (size_t) numIntents> pills;
        int selectedIndex = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntentSelector)
    };
}
