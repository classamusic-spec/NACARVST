#pragma once

#include "PageSurface.h"

namespace nacar::ui
{
    /**
        FX - deep editing for the six chain modules (master spec section 128).

        RETRO, CRUSH, FILTER, REWIND, GRAIN and SPACE as a 3 x 2 grid of glass
        cards, in the locked display order, which is also the DSP order.  Each
        card carries the module's icon, its mint power ring and its complete
        parameter set from ParameterList.h - nothing is left off the page and
        nothing on the page writes nowhere.
    */
    class FXPage : public PageSurface
    {
    public:
        FXPage (NacarProcessor&, EditorHost&);
        ~FXPage() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        static constexpr int numModules = 6;

        struct Module
        {
            juce::String name;
            icons::Icon  icon;
            PID          enable;

            juce::Rectangle<int> bounds, footnote;
            PowerButton* power = nullptr;
        };

        Module modules[numModules];

        SegmentedControl* filterMode     = nullptr;
        SegmentedControl* rewindMode     = nullptr;
        SegmentedControl* rewindDivision = nullptr;
        ToggleSwitch*     rewindSync     = nullptr;
        ChoicePill*       grainPitchMode = nullptr;
        SegmentedControl* grainWindow    = nullptr;
        ToggleSwitch*     grainFreeze    = nullptr;
        SegmentedControl* spaceCharacter = nullptr;

        // Switches on glass carry their own caption, drawn by the page: the
        // PreserveLock widget writes its caption in ceramic ink.
        juce::Rectangle<int> rewindSyncCaption, grainFreezeCaption, grainPitchCaption;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FXPage)
    };
}
