#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Theme.h"
#include "../Layout.h"
#include "../EditorHost.h"
#include "../Components/Widgets.h"
#include "../../Plugin/PluginProcessor.h"

namespace nacar::ui
{
    /**
        The left macro panel: the instrument's performance surface.

        Seven knobs in the hierarchy the reference locks down -
        MEMORY -> CHARACTER / MOTION -> WORLD / WEIGHT -> ALTER - plus the four
        Roman-numeral generation dots beside MEMORY, the SUB | BODY | AIR band
        selector under WEIGHT, and the small dark RANDOM cap.

        RANDOM is a depth control, not a trigger, and it is deliberately
        subordinate: it is not MUTATE.  See the note in the .cpp.

        The panel itself paints only the ceramic and the three scale legends;
        every knob paints its own cap, arc and name.

        UI spec section 4.
    */
    class MacroPanel : public juce::Component
    {
    public:
        MacroPanel (NacarProcessor&, EditorHost&);
        ~MacroPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void configureKnob (NacarKnob&, const juce::String& name);

        NacarProcessor& processor;
        EditorHost& host;

        NacarKnob memoryKnob;
        NacarKnob characterKnob;
        NacarKnob motionKnob;
        NacarKnob worldKnob;
        NacarKnob weightKnob;
        NacarKnob alterKnob;
        NacarKnob randomKnob;

        GenerationSelector generation;
        SegmentedControl   weightModeSelector;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacroPanel)
    };
}
