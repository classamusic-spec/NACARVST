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

    /// The halo the lit run throws into the channel and onto the chassis around
    /// it.  Mint is one of the two things in the instrument that emits rather
    /// than reflects (spec section 12), so the meter has to behave like a
    /// source: a flat fill of a bright colour is a printed bar, not a light.
    static constexpr float meterGlowSpread = 5.0f;
    static constexpr float meterGlowAlpha  = 0.20f;   ///< over the whole lit run
    static constexpr float meterTipSpread  = 4.0f;
    static constexpr float meterTipAlpha   = 0.34f;   ///< concentrated at the level
    static constexpr int   meterTipSegs    = 3;       ///< how much of the run is "the tip"

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
                // A ceramic pill lifted out of the slab it sits in.  The slab
                // is a cut-out, so this is the one element in the bottom bar
                // that is genuinely above the chassis rather than below it, and
                // it gets the full raised treatment: contact shadow onto the
                // glass, specular top edge, bevelled bottom, and the inset
                // highlight a pixel inside the top that reads as thickness.
                theme::raisedCeramic (g, b.reduced (1.0f), radiusCard,
                                      theme::Elevation::raised);
            }
            else if (hovered)
            {
                // Still inside the cut, so it is lit at its edges rather than
                // in its face: a dark chip on a dark ground carries no
                // information in its fill.
                theme::raisedGlass (g, b.reduced (1.0f), radiusCard,
                                    theme::Elevation::flush, 0.0f, 1.0f);
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

        // The bar is a ceramic plate, the same piece of chassis as the header
        // at the other end of the window - the source pills are raised out of
        // it, and the nav slab and the output channel are cut into it.  (It
        // used to paint nothing at all and let the bare canvas show through,
        // which left the one band of the interface where every element floated
        // on nothing.)
        const auto b = getLocalBounds().toFloat().reduced (theme::chassis::plateInset);

        theme::chassis::plate (g, b, radiusPanel);

        // The two cuts.  Their lips are on the ceramic, outside the bounds of
        // whatever fills them, so they are drawn here rather than by the slab
        // and the meter themselves.
        //
        // Clipped to the face of the plate because the nav slab is 60 px tall
        // in a 66 px bar: its lip would otherwise land on the plate's own top
        // rim and put a dark notch through the specular for a third of the
        // width of the window.  The slab keeps its side and bottom walls, and
        // the near wall it loses is the one the reference has no room for
        // either.
        {
            juce::Graphics::ScopedSaveState ss (g);

            juce::Path face;
            face.addRoundedRectangle (b.reduced (1.5f), juce::jmax (0.5f, radiusPanel - 1.5f));
            g.reduceClipRegion (face);

            theme::chassis::cutOut (g, bottom::navBar, radiusPanel);
            theme::chassis::cutOut (g, bottom::meter, meterCorner);
        }

        ceramicLabel (g, "OUTPUT", { bottom::outputLabelX, bottom::outputBase },
                      outputLabelSize, outputLabelTrack, theme::inkMuted);

        paintOutputMeter (g, bottom::meter);
    }

    void BottomBar::paintOutputMeter (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        // The channel.  theme::recessedWell rather than glassSurface: on a fill
        // this dark the inner shadow under the top edge does almost nothing,
        // and the catch of light along the bottom wall - which is what says
        // "below the surface" rather than "painted on it" - is the half that
        // reads.  The lip above it is drawn by paint(), on the ceramic.
        theme::recessedWell (g, area, meterCorner, theme::glassDeep, 1.0f);

        g.setColour (theme::glassEdge.withAlpha (0.85f));
        g.drawRoundedRectangle (area.reduced (0.5f), meterCorner, 1.0f);

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

            // The light, before the segments that make it.  Drawn underneath so
            // the segmentation stays crisp and only the halo spills: a mint run
            // with nothing around it is a printed bar, and this is supposed to
            // be a lamp inside a channel.
            if (lit > 0)
            {
                const float runW = (float) lit * (segW + meterSegGap) - meterSegGap;
                const juce::Rectangle<float> run (field.getX(), y, runW, rowH);

                theme::outerGlow (g, run, 1.5f, theme::mint,
                                  meterGlowAlpha * (0.5f + t * 0.5f), meterGlowSpread);

                // Brighter where the level actually is.
                const float tipW = juce::jmin (runW,
                                               (float) meterTipSegs * (segW + meterSegGap));

                theme::outerGlow (g, run.withLeft (run.getRight() - tipW), 1.5f,
                                  theme::mint, meterTipAlpha, meterTipSpread);
            }

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

            // A very faint bloom over the lit run, on top of the segments this
            // time: light spreading across the gaps between them.
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
