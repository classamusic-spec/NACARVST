#include "BottomBar.h"

namespace nacar::ui
{
    using namespace layout;

    // =======================================================================
    //  Numbers the reference shows but Layout.h does not carry.
    //
    //  Layout.h is frozen, so anything section 9 describes in prose rather than
    //  as a coordinate is defined here, next to the only code that uses it.
    // =======================================================================

    /// Leading glyph inside a source pill, as a fraction of the pill height.
    /// Section 9 shows a small icon, not a badge: 0.40 of 40 px reads as ~16 px.
    static constexpr float sourceIconRatio = 0.40f;

    /// Nav glyph box.  Section 9 asks for an icon "about 17 px" over the label.
    static constexpr float navIconSize = 17.0f;

    /// The OUTPUT caption: 8 px / 0.18 em small caps, per section 9.
    static constexpr float outputLabelSize  = 8.0f;
    static constexpr float outputLabelTrack = 0.18f;

    // -- meter ---------------------------------------------------------------
    //  The meter is the one place mint appears at size, so its construction is
    //  spelled out rather than improvised: a recessed well, a fixed number of
    //  discrete segments, and a decibel - not linear - mapping.

    /// Segment count across the 195 px well.  34 with a 1 px gap gives ~4.6 px
    /// segments, which is the granularity the reference shows.
    static constexpr int   meterSegments = 34;
    static constexpr float meterSegGap   = 1.0f;
    static constexpr float meterRowGap   = 1.0f;   ///< between the two channel rows
    static constexpr float meterInset    = 2.0f;   ///< well wall thickness
    static constexpr float meterCorner   = 3.0f;   ///< a 12 px well takes a small radius

    /// Display range.  -48 dB is the bottom of the scale; below it nothing lights.
    static constexpr float meterFloorDb = -48.0f;

    /// Where the warm run begins.  With 34 segments over 48 dB, -5 dB is the top
    /// four segments, which is the run the reference tints.  (-3 dB would light
    /// only two, so the dB figure is chosen to land on the segment count.)
    static constexpr float meterWarnDb  = -5.0f;

    /// How far the warm run is allowed to travel towards violet.  The palette is
    /// locked to two accents, so the warning is a shift, not a new hue.
    static constexpr float meterWarnMix = 0.55f;

    // -----------------------------------------------------------------------
    //  Source pills and nav items, in reference order.  File-local: other
    //  panels are being written against the same headers in parallel.
    // -----------------------------------------------------------------------
    namespace
    {
        struct SourceSpec { icons::Icon icon; const char* text; };

        constexpr SourceSpec sourceSpecs[5] = {
            { icons::Icon::srcSynth,     "SYNTH"     },
            { icons::Icon::srcSample,    "SAMPLE"    },
            { icons::Icon::srcGrain,     "GRAIN"     },
            { icons::Icon::srcResonator, "RESONATOR" },
            { icons::Icon::srcSpectral,  "SPECTRAL"  }
        };

        struct NavSpec { icons::Icon icon; const char* text; };

        constexpr NavSpec navSpecs[5] = {
            { icons::Icon::navMain, "MAIN" },
            { icons::Icon::navMod,  "MOD"  },
            { icons::Icon::navFx,   "FX"   },
            { icons::Icon::navSeq,  "SEQ"  },
            { icons::Icon::navMix,  "MIX"  }
        };
    }

    /** The slot rect for navigation item i, region-local.

        The reference only pins the active pill, and it happens to sit on MAIN.
        Every other slot is that same rect re-centred on its own nav centre, so
        all five items are identical in size and the pill never jumps.
    */
    static RectF navSlot (int i)
    {
        return RectF (bottom::navActive.getWidth(), bottom::navActive.getHeight())
                   .withCentre ({ bottom::navCentre[i], bottom::navActive.getCentreY() });
    }

    // =======================================================================
    //  BottomBar::NavItem
    // =======================================================================
    class BottomBar::NavItem : public juce::Component
    {
    public:
        NavItem (icons::Icon i, juce::String t)
            : icon (i), text (std::move (t))
        {
            setWantsKeyboardFocus (false);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void setActive (bool shouldBeActive)
        {
            if (active == shouldBeActive)
                return;

            active = shouldBeActive;
            repaint();
        }

        std::function<void()> onClick;

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat();

            // Every slot shares the active pill's top edge, so the icon and the
            // label baseline are just the region-local constants moved into the
            // item's own coordinates.
            const float iconCentreY  = bottom::navIconY    - bottom::navActive.getY();
            const float labelBaseline = bottom::navLabelBase - bottom::navActive.getY();

            if (active)
            {
                // A raised ceramic pill lifted out of the glass bar.
                theme::contactShadow (g, b.reduced (1.0f), radiusCard, 2.0f, 7.0f, 0.34f);
                theme::ceramicSurface (g, b.reduced (1.0f), radiusCard);
            }
            else if (hovered)
            {
                // "glass raised" is the material language's hover fill.
                g.setColour (theme::glassRaised);
                g.fillRoundedRectangle (b.reduced (1.0f), radiusCard);
            }

            const auto tint = active  ? theme::ink
                            : hovered ? theme::glassInk
                                      : theme::glassInkMuted;

            icons::draw (g, icon,
                         juce::Rectangle<float> (navIconSize, navIconSize)
                             .withCentre ({ b.getCentreX(), iconCentreY }),
                         tint, 1.5f);

            // The caption is centred on the slot; ceramic when the item is
            // raised, glass otherwise, because that is the surface it sits on.
            const juce::Point<float> caption { b.getCentreX(), labelBaseline };

            if (active)
                ceramicLabel (g, text, caption, bottom::navSize, bottom::navTrack,
                              tint, juce::Justification::centred);
            else
                glassLabel (g, text, caption, bottom::navSize, bottom::navTrack,
                            tint, juce::Justification::centred);
        }

        void mouseEnter (const juce::MouseEvent&) override { hovered = true;  repaint(); }
        void mouseExit  (const juce::MouseEvent&) override { hovered = false; repaint(); }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()) && onClick != nullptr)
                onClick();
        }

    private:
        icons::Icon icon;
        juce::String text;
        bool active = false;
        bool hovered = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NavItem)
    };

    // =======================================================================
    //  BottomBar
    // =======================================================================
    BottomBar::BottomBar (NacarProcessor& p, EditorHost& h)
        : processor (p), host (h),
          masterKnob (p.getParameters(), PID::masterGain)
    {
        // The strip between the groups is bare chassis - clicks on it belong to
        // whatever is underneath, not to this component.
        setInterceptsMouseClicks (false, true);

        const auto& sourceDef = ParameterRegistry::definition (PID::sourceMode);

        for (int i = 0; i < 5; ++i)
        {
            auto* pill = new PillButton (sourceSpecs[i].text, PillButton::Style::ceramic);

            pill->setIcon (sourceSpecs[i].icon, sourceIconRatio);
            pill->setTextSize (bottom::sourceSize, bottom::sourceTrack);
            pill->setCornerRadius (radiusCard);
            pill->setTooltip (juce::String (sourceSpecs[i].text) + "\n" + sourceDef.tooltip);
            pill->onClick = [this, i] { host.setSource ((Source) i); };

            sourcePills.add (pill);
            addAndMakeVisible (pill);
        }

        addAndMakeVisible (navPanel);

        for (int i = 0; i < 5; ++i)
        {
            auto* item = new NavItem (navSpecs[i].icon, navSpecs[i].text);
            item->onClick = [this, i] { host.setPage ((Page) i); };

            navItems.add (item);
            addAndMakeVisible (item);          // added after navPanel, so on top of it
        }

        // No label: the OUTPUT caption beside it is the knob's caption.
        addAndMakeVisible (masterKnob);

        syncFromHost();
    }

    BottomBar::~BottomBar() = default;

    void BottomBar::resized()
    {
        for (int i = 0; i < sourcePills.size(); ++i)
            sourcePills[i]->setBounds (RectF (bottom::sourceX[i], bottom::sourceY,
                                              bottom::sourceW[i], bottom::sourceH).toNearestInt());

        navPanel.setBounds (bottom::navBar.toNearestInt());

        for (int i = 0; i < navItems.size(); ++i)
            navItems[i]->setBounds (navSlot (i).toNearestInt());

        masterKnob.setBounds (centredSquare (bottom::masterKnob, bottom::masterRadius).toNearestInt());
    }

    // -----------------------------------------------------------------------
    //  State
    // -----------------------------------------------------------------------
    void BottomBar::syncFromHost()
    {
        const int source = (int) host.getSource();
        const int page   = (int) host.getPage();

        if (source != shownSource)
        {
            shownSource = source;
            applySourceStyles (source);
        }

        if (page != shownPage)
        {
            shownPage = page;
            applyPageStyles (page);
        }
    }

    void BottomBar::applySourceStyles (int selectedSource)
    {
        for (int i = 0; i < sourcePills.size(); ++i)
        {
            const bool on = (i == selectedSource);

            // The active source is the only dark-glass chip on the chassis -
            // everything else here is raised ceramic.
            sourcePills[i]->setStyle (on ? PillButton::Style::glass
                                         : PillButton::Style::ceramic);
            sourcePills[i]->setSelected (on);
        }
    }

    void BottomBar::applyPageStyles (int activePage)
    {
        for (int i = 0; i < navItems.size(); ++i)
            navItems[i]->setActive (i == activePage);
    }

    // -----------------------------------------------------------------------
    //  Paint
    // -----------------------------------------------------------------------
    void BottomBar::paint (juce::Graphics& g)
    {
        // The editor already repaints this region at 30 Hz for the meter, which
        // makes it the cheapest place to notice that the host moved SOURCE or
        // that the page changed.  Both setters only touch a child when the value
        // behind it actually moved, so this cannot turn into a repaint loop.
        syncFromHost();

        // The bar carries no panel of its own: the source pills and the nav bar
        // bring their own surfaces and the strip between them is bare chassis.

        ceramicLabel (g, "OUTPUT", { bottom::outputLabelX, bottom::outputBase },
                      outputLabelSize, outputLabelTrack, theme::inkMuted);

        paintOutputMeter (g, bottom::meter);
    }

    void BottomBar::paintOutputMeter (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        // A cut-out in the chassis, not a raised element: deep fill, inner top
        // shadow, hairline.
        theme::glassSurface (g, area, meterCorner);

        const auto field = area.reduced (meterInset);

        if (field.isEmpty())
            return;

        const float rowH = (field.getHeight() - meterRowGap) * 0.5f;
        const float segW = (field.getWidth() - meterSegGap * (float) (meterSegments - 1))
                           / (float) meterSegments;

        if (rowH <= 0.0f || segW <= 0.0f)
            return;

        const float span = -meterFloorDb;                       // 48 dB of scale

        // First segment of the warm run, from the dB threshold.
        const int warnFrom = juce::jlimit (1, meterSegments - 1,
                                           juce::roundToInt ((meterWarnDb - meterFloorDb) / span
                                                             * (float) meterSegments));

        for (int ch = 0; ch < 2; ++ch)
        {
            // Read the processor every paint: this is a real meter, not a mock.
            const float level = processor.getMeterLevel (ch);
            const float db    = juce::Decibels::gainToDecibels (level, meterFloorDb);
            const float t     = juce::jlimit (0.0f, 1.0f, (db - meterFloorDb) / span);

            const int lit = juce::roundToInt (t * (float) meterSegments);
            const float y = field.getY() + (float) ch * (rowH + meterRowGap);

            for (int s = 0; s < meterSegments; ++s)
            {
                const juce::Rectangle<float> seg (field.getX() + (float) s * (segW + meterSegGap),
                                                  y, segW, rowH);

                if (s < lit)
                {
                    const float run = (float) s / (float) (meterSegments - 1);
                    auto c = theme::mintDeep.interpolatedWith (theme::mint, run);

                    if (s >= warnFrom)
                    {
                        const float warmth = (float) (s - warnFrom + 1)
                                             / (float) juce::jmax (1, meterSegments - warnFrom);
                        c = c.interpolatedWith (theme::violetDeep, warmth * meterWarnMix);
                    }

                    g.setColour (c);
                }
                else
                {
                    // Unlit segments stay legible so the scale reads when idle.
                    g.setColour (theme::glassEdge.withAlpha (0.55f));
                }

                g.fillRect (seg);
            }

            // A very faint bloom over the lit run: mint is the powered colour,
            // and a flat fill alone reads as printed rather than lit.
            if (lit > 0)
            {
                const float runW = (float) lit * (segW + meterSegGap) - meterSegGap;

                g.setColour (theme::mint.withAlpha (0.10f));
                g.fillRoundedRectangle (juce::Rectangle<float> (field.getX(), y - 0.5f,
                                                               runW, rowH + 1.0f), 1.5f);
            }
        }
    }
}
