#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

#include "../Theme.h"
#include "../Layout.h"
#include "../EditorHost.h"
#include "../Components/Icons.h"
#include "../Components/Widgets.h"
#include "../../Plugin/PluginProcessor.h"

namespace nacar::ui
{
    /**
        The right-hand atmosphere panel (UI spec section 8).

        A raised ceramic panel divided by three hairlines into AURA, SHADOW,
        BREATH and PATINA.  Every module has the same anatomy: an icon and a
        name, a subtitle, a violet power ring, one large knob and a list of the
        parameters that knob can drive.  The violet dot in the list marks which
        parameter the knob is on; clicking a row moves it.

        A NacarKnob takes its PID at construction and Widgets.h is frozen, so a
        knob cannot be re-bound.  Each module therefore builds one knob per
        parameter, all sharing the same bounds, and shows only the selected one.
        Eighteen knobs across the panel is cheap, and it keeps the binding
        immutable, which is what the widget contract wants.
    */
    class AtmospherePanel : public juce::Component,
                            private juce::ValueTree::Listener
    {
    public:
        AtmospherePanel (NacarProcessor&, EditorHost&);
        ~AtmospherePanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        struct Row
        {
            const char* name = nullptr;
            PID         pid  = PID::count;
        };

        struct Module
        {
            Module() = default;

            layout::atmos::ModuleSpec spec {};
            const char* title    = nullptr;
            const char* subtitle = nullptr;

            icons::Icon icon = icons::Icon::planet;
            bool        iconIsToggle = false;   ///< SHADOW wears a pill switch, not a glyph

            PID enable = PID::count;
            juce::Identifier selectionProperty;
            std::vector<Row> rows;

            std::vector<std::unique_ptr<NacarKnob>> knobs;   ///< one per row, stacked
            std::unique_ptr<PowerButton>  power;
            std::unique_ptr<ToggleSwitch> toggle;
            std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;

            int  selected = 0;
            int  hoverRow = -1;
            bool powered  = false;
        };

        static constexpr int numModules = 4;

        void describeModules();
        void buildModule (Module&);

        void setSelectedRow (Module&, int row);
        void powerChanged (Module&);

        float moduleAlpha (const Module&) const;
        juce::Rectangle<float> rowBounds (const Module&, int row) const;
        juce::Rectangle<float> knobBoundsFor (const Module&) const;

        /** The full-width band of the plate a module occupies, from the seam
            above it to the seam below.  Used only for shading. */
        juce::Rectangle<float> moduleBand (int index) const;

        void paintModule (juce::Graphics&, const Module&) const;

        // -- juce::ValueTree::Listener ---------------------------------------
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeParentChanged (juce::ValueTree&) override;
        void reacquireTree();

        NacarProcessor& processor;
        juce::ValueTree editorTree;

        std::array<Module, (size_t) numModules> modules;

        bool writingOurselves = false;   ///< suppresses the listener for our own edits

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AtmospherePanel)
    };
}
