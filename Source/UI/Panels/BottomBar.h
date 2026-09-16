#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Theme.h"
#include "../Layout.h"
#include "../EditorHost.h"
#include "../Components/Widgets.h"
#include "../Components/Icons.h"
#include "../../Plugin/PluginProcessor.h"

namespace nacar::ui
{
    /**
        The bottom bar - UI spec section 9.

        Three groups sitting straight on the chassis, with no surface of their
        own between them:

          left    five source pills (SYNTH .. SPECTRAL).  The selected one is
                  the only dark-glass chip in the whole interface.
          centre  the five-page navigation, a glass bar carrying a raised
                  ceramic pill under the active page.
          right   the OUTPUT caption, the stereo segmented mint meter, and the
                  master gain knob.

        The editor repaints this region at 30 Hz for the meter, so paint() is
        also where the bar notices that the host moved SOURCE or the page under
        it.  All coordinates are region-local and come from layout::bottom.
    */
    class BottomBar : public juce::Component
    {
    public:
        BottomBar (NacarProcessor&, EditorHost&);
        ~BottomBar() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        /** One navigation slot: icon, tracked caption, hover and active state.

            A child component per item rather than painted regions on the bar,
            because hover feedback is required and mouseEnter / mouseExit on a
            real component is both cheaper and less error-prone than tracking
            the pointer against five rectangles in the parent.
        */
        class NavItem;

        void syncFromHost();
        void applySourceStyles (int selectedSource);
        void applyPageStyles (int activePage);
        void paintOutputMeter (juce::Graphics&, juce::Rectangle<float> area) const;

        NacarProcessor& processor;
        EditorHost& host;

        juce::OwnedArray<PillButton> sourcePills;

        GlassPanel navPanel { layout::radiusPanel };
        juce::OwnedArray<NavItem> navItems;

        NacarKnob masterKnob;

        // What the bar is currently showing, so the 30 Hz paint only touches a
        // child when the value behind it actually moved.
        int shownSource = -1;
        int shownPage   = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BottomBar)
    };
}
