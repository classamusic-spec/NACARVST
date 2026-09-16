#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Theme.h"
#include "../Layout.h"
#include "../EditorHost.h"
#include "../Components/Icons.h"
#include "../Components/Widgets.h"
#include "../../Plugin/PluginProcessor.h"

namespace nacar::ui
{
    /**
        The header: a raised ceramic bar spanning the whole chassis.

        It carries the identity of the instrument on the left (wordmark,
        descriptor, tagline), the preset transport in the middle (a recessed
        glass strip with the source mark, the activity meter, the preset name
        and its two chevrons), and the chassis controls on the right (the
        favourite disc, the BROWSER pill, the settings gear, the wave logo and
        the coordinates of the place the instrument is named after).

        Everything static is painted here; everything you can touch is a child.
        The preset name is read from the PRESET group of the session tree, and
        the bar listens to that tree so a preset change repaints it.

        UI spec section 3.
    */
    class HeaderBar : public juce::Component,
                      private juce::ValueTree::Listener
    {
    public:
        HeaderBar (NacarProcessor&, EditorHost&);
        ~HeaderBar() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        // -------------------------------------------------------------------
        /** The recessed glass strip at layout::hdr::presetBar.

            It owns the two chevrons and paints the source mark, the meter mark
            and the preset name.  It exists as its own component because glass
            is an opaque cut-out: anything the HeaderBar painted underneath it
            would simply be covered.
        */
        class PresetStrip : public GlassPanel
        {
        public:
            PresetStrip (juce::ValueTree presetState, EditorHost&);

            void paint (juce::Graphics&) override;
            void resized() override;

        private:
            juce::ValueTree preset;
            EditorHost& host;

            IconButton prevButton { icons::Icon::chevronLeft,  IconButton::Style::plain };
            IconButton nextButton { icons::Icon::chevronRight, IconButton::Style::plain };

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetStrip)
        };

        // -- juce::ValueTree::Listener ---------------------------------------
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

        void toggleFavourite();
        void refreshFavourite();
        void refreshBrowserPill();

        NacarProcessor& processor;
        EditorHost& host;

        juce::ValueTree presetTree;

        PresetStrip presetStrip;
        IconButton  favouriteButton { icons::Icon::heart, IconButton::Style::ceramicRound };
        PillButton  browserButton   { "BROWSER", PillButton::Style::ceramic };
        IconButton  gearButton      { icons::Icon::gear, IconButton::Style::plain };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
    };
}
