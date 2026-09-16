#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "../Theme.h"
#include "../Layout.h"
#include "../EditorHost.h"
#include "../Components/Icons.h"
#include "../Components/Widgets.h"
#include "../../Plugin/PluginProcessor.h"

namespace nacar::ui
{
    // =======================================================================
    //  chassis - the shading the four region panels share
    //
    //  The header, the macro panel, the atmosphere panel and the bottom bar are
    //  one machined object seen in four places.  They therefore cannot each
    //  invent their own edge: a plate whose rim is lit differently from the
    //  plate beside it stops being the same piece of metal, which is the whole
    //  of what UI spec section 12 is about.  These four routines are written
    //  once and used by all four panels.
    //
    //  They belong in Theme.h, beside the raisedCeramic() and recessedWell()
    //  they are built out of.  Theme.h is frozen for V1, so they live in the
    //  header of the topmost region instead and the other three include it.
    //
    //  Nothing here introduces a colour.  Every value is either a theme::
    //  shade or a white / black lighting operation of exactly the kind
    //  Theme.cpp already performs for every specular and bevel it draws.
    // =======================================================================

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
