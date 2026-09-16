#include "HeaderBar.h"

namespace nacar::ui
{
    using namespace layout;

    // =======================================================================
    //  Numbers the reference implies but Layout.h does not carry.
    //
    //  Every one of these is a *size* or a *span* for an element whose
    //  *position* is already fixed by layout::hdr.  Per the contract, a number
    //  that is missing from the frozen Layout.h lives here, with its reason.
    // =======================================================================
    namespace
    {
        // icons::pathFor fits the 100 x 100 *authoring box*, not the glyph's own
        // bounds, so the square an icon is drawn in is always wider than the mark
        // that lands in it.  The two header glyphs below are therefore expressed
        // as box sizes, with the mark size they produce noted.

        /// UI spec 3 gives the source mark a centre but no size.  The two
        /// chevrons fill 52 % of the authoring box, so 26 lands a ~13 px mark.
        constexpr float sourceGlyphBox = 26.0f;

        /// The activity meter is deliberately smaller than the source mark: it
        /// is a state indicator, not a label.  The bars fill 64 % of the box,
        /// so 19 lands a ~12 px mark.
        constexpr float meterGlyphBox = 19.0f;

        /// Hit radius for the preset chevrons.  hdr::prevArrow and hdr::nextArrow
        /// are 52 px apart, so 13 leaves a clean 26 px gap between the targets.
        constexpr float arrowRadius = 13.0f;

        /// Hit radius for the settings gear; UI spec 3 gives its centre only.
        constexpr float gearRadius = 13.0f;

        /// Icon-to-hit-area ratios.  IconButton's 0.46 default is tuned for the
        /// viewport's square tool buttons; these three glyphs are sparse in the
        /// authoring box and need more of their disc to read at 100 % scale.
        constexpr float chevronRatio   = 0.66f;
        constexpr float heartRatio     = 0.52f;
        constexpr float gearIconRatio  = 0.62f;

        /// The wave mark fills 84 % of the authoring box horizontally.
        constexpr float waveLogoFootprint = 0.84f;

        /// Width of the wave mark in the reference.  hdr::waveLogo and
        /// hdr::coordX are only 67 px apart and the coordinate block is
        /// right-aligned to coordX, so the mark is trimmed to whatever is left
        /// rather than being allowed to run into the type.
        constexpr float waveLogoWidth = 44.0f;
        constexpr float waveLogoGap   = 6.0f;

        /// Baseline of the preset name.  UI spec 3 puts it at canvas y 64, i.e.
        /// 54 in header-local units.  Layout.h carries hdr::presetNameX and
        /// hdr::presetNameSize but no matching baseline.
        constexpr float presetNameBase = 54.0f;

        /// How far the preset name may run before it reaches the prev chevron
        /// (hdr::prevArrow.x - arrowRadius - a little air - hdr::presetNameX).
        constexpr float presetNameWidth = 200.0f;

        /// UI spec 3 gives the coordinate block a size but not its tracking.
        /// 0.06 em keeps it quiet without letting the two lines set solid.
        constexpr float coordTrack = 0.06f;

        /// Width of the right-aligned coordinate block, measured back from
        /// hdr::coordX.
        constexpr float coordWidth = 96.0f;

        /// BROWSER pill type, from UI spec 3: 9 pt, 0.16 em.
        constexpr float browserTextSize  = 9.0f;
        constexpr float browserTextTrack = 0.16f;

        /// Stroke weight for the header glyphs, expressed for a 24 px icon.
        constexpr float glyphStroke = 1.5f;
    }

    // =======================================================================
    //  HeaderBar::PresetStrip
    // =======================================================================
    HeaderBar::PresetStrip::PresetStrip (juce::ValueTree presetState, EditorHost& h)
        : preset (std::move (presetState)), host (h)
    {
        setCornerRadius (hdr::presetBarRadius);

        prevButton.setColours (theme::glassInkMuted, theme::glassInk);
        prevButton.setIconRatio (chevronRatio);
        prevButton.setTooltip ("Previous preset\nStep back through the library.");
        prevButton.onClick = [this] { host.selectRelativePreset (-1); };
        addAndMakeVisible (prevButton);

        nextButton.setColours (theme::glassInkMuted, theme::glassInk);
        nextButton.setIconRatio (chevronRatio);
        nextButton.setTooltip ("Next preset\nStep forward through the library.");
        nextButton.onClick = [this] { host.selectRelativePreset (1); };
        addAndMakeVisible (nextButton);
    }

    void HeaderBar::PresetStrip::paint (juce::Graphics& g)
    {
        // The glass cut-out itself: flat deep fill, inner top shadow, hairline.
        GlassPanel::paint (g);

        // The hdr:: constants are header-local.  The strip's origin is taken
        // off once, here, so nothing inside the strip invents a coordinate.
        const auto origin = hdr::presetBar.getPosition();

        icons::draw (g, icons::Icon::sourceBrackets,
                     centredSquare (hdr::sourceGlyph - origin, sourceGlyphBox * 0.5f),
                     theme::glassInkMuted, glyphStroke);

        // Violet, because the meter reads the selected preset's activity.
        icons::draw (g, icons::Icon::meterBars,
                     centredSquare (hdr::meterGlyph - origin, meterGlyphBox * 0.5f),
                     theme::violet, glyphStroke);

        // The preset name is a name, not a tracked micro-label, so it is set
        // flush in theme::medium rather than through ui::glassLabel.
        const auto font = theme::medium (hdr::presetNameSize);

        g.setFont (font);
        g.setColour (theme::glassInk);
        g.drawText (preset.getProperty (ids::presetName).toString(),
                    juce::Rectangle<float> (hdr::presetNameX - origin.x,
                                            presetNameBase - origin.y - font.getAscent(),
                                            presetNameWidth, font.getHeight()),
                    juce::Justification::centredLeft, false);
    }

    void HeaderBar::PresetStrip::resized()
    {
        const auto origin = hdr::presetBar.getPosition();

        prevButton.setBounds (centredSquare (hdr::prevArrow - origin, arrowRadius).toNearestInt());
        nextButton.setBounds (centredSquare (hdr::nextArrow - origin, arrowRadius).toNearestInt());
    }

    // =======================================================================
    //  HeaderBar
    // =======================================================================
    HeaderBar::HeaderBar (NacarProcessor& p, EditorHost& h)
        : processor (p), host (h),
          presetTree (processor.getStateManager().group (ids::PRESET)),
          presetStrip (presetTree, h)
    {
        addAndMakeVisible (presetStrip);

        // The favourite disc, the BROWSER pill and the gear are raised ceramic
        // and are added after the strip, so where hdr::favourite and
        // hdr::browserPill overlap hdr::presetBar they read as sitting on the
        // glass rather than being cut into it.
        favouriteButton.setColours (theme::inkMuted, theme::violet);
        favouriteButton.setIconRatio (heartRatio);
        favouriteButton.onClick = [this] { toggleFavourite(); };
        addAndMakeVisible (favouriteButton);

        browserButton.setTextSize (browserTextSize, browserTextTrack);
        browserButton.setTooltip ("Browser\nOpen the preset library.");
        browserButton.onClick = [this]
        {
            host.setBrowserOpen (! host.isBrowserOpen());
            refreshBrowserPill();
        };
        addAndMakeVisible (browserButton);

        gearButton.setColours (theme::inkMuted, theme::ink);
        gearButton.setIconRatio (gearIconRatio);
        gearButton.setTooltip ("Settings\nInterface scale, audio quality, about.");
        gearButton.onClick = [this] { host.showSettingsMenu (*this); };
        addAndMakeVisible (gearButton);

        presetTree.addListener (this);

        refreshFavourite();
        refreshBrowserPill();
    }

    HeaderBar::~HeaderBar()
    {
        presetTree.removeListener (this);
    }

    void HeaderBar::paint (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();

        theme::contactShadow (g, b, radiusPanel);
        theme::ceramicSurface (g, b, radiusPanel);

        // -- identity -------------------------------------------------------
        // fromUTF8 so the acute A survives whatever the compiler thinks the
        // source encoding is.
        ceramicLabel (g, juce::String::fromUTF8 ("N\xc3\x81""CAR"),
                      { hdr::wordmarkX, hdr::wordmarkBase },
                      hdr::wordmarkSize, hdr::wordmarkTrack, theme::ink);

        ceramicLabel (g, "MEMORY INSTRUMENT",
                      { hdr::descriptorX, hdr::descriptorBase },
                      hdr::descriptorSize, hdr::descriptorTrack, theme::inkMuted);

        theme::hairline (g, { hdr::dividerX, hdr::dividerTop },
                            { hdr::dividerX, hdr::dividerBottom }, theme::ceramicEdge);

        ceramicLabel (g, "SOUNDS", { hdr::taglineX, hdr::taglineBase1 },
                      hdr::taglineSize, hdr::taglineTrack, theme::inkMuted);

        ceramicLabel (g, "WITH A PAST.", { hdr::taglineX, hdr::taglineBase2 },
                      hdr::taglineSize, hdr::taglineTrack, theme::inkMuted);

        // -- coordinates, right-aligned to hdr::coordX ----------------------
        const auto north = juce::String::fromUTF8 ("25.7617\xc2\xb0 N");
        const auto west  = juce::String::fromUTF8 ("80.1918\xc2\xb0 W");

        ceramicLabel (g, north, { hdr::coordX, hdr::coordBase1 }, hdr::coordSize, coordTrack,
                      theme::inkFaint, juce::Justification::right, coordWidth);

        ceramicLabel (g, west, { hdr::coordX, hdr::coordBase2 }, hdr::coordSize, coordTrack,
                      theme::inkFaint, juce::Justification::right, coordWidth);

        // -- wave logo: a mark, not a control, so it is painted --------------
        // It takes the reference width, or whatever the coordinate block leaves
        // it, whichever is smaller - type never gets crowded by decoration.
        {
            const float coordLeft = hdr::coordX - theme::trackedWidth (north,
                                                                       theme::label (hdr::coordSize),
                                                                       coordTrack);
            const float markWidth = juce::jmin (waveLogoWidth,
                                                2.0f * (coordLeft - waveLogoGap - hdr::waveLogo.x));

            icons::draw (g, icons::Icon::waveLogo,
                         centredSquare (hdr::waveLogo, markWidth / waveLogoFootprint * 0.5f),
                         theme::ink, glyphStroke);
        }
    }

    void HeaderBar::resized()
    {
        presetStrip.setBounds (hdr::presetBar.toNearestInt());

        favouriteButton.setBounds (centredSquare (hdr::favourite,
                                                  hdr::favouriteRadius).toNearestInt());
        browserButton.setBounds (hdr::browserPill.toNearestInt());
        gearButton.setBounds (centredSquare (hdr::gear, gearRadius).toNearestInt());
    }

    // -----------------------------------------------------------------------
    //  Preset state
    // -----------------------------------------------------------------------
    void HeaderBar::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
    {
        if (tree != presetTree)
            return;

        if (property == ids::presetFavourite)
            refreshFavourite();

        // The name lives on the strip, the favourite on the bar; a preset
        // change can move either, so both are refreshed.
        presetStrip.repaint();
        repaint();
    }

    void HeaderBar::toggleFavourite()
    {
        const bool isFavourite = (bool) presetTree.getProperty (ids::presetFavourite, false);

        // No undo manager: marking a favourite is preset metadata, not an edit
        // to the instrument, and it should not sit in the mutation undo stack.
        presetTree.setProperty (ids::presetFavourite, ! isFavourite, nullptr);
    }

    void HeaderBar::refreshFavourite()
    {
        const bool isFavourite = (bool) presetTree.getProperty (ids::presetFavourite, false);

        favouriteButton.setIcon (isFavourite ? icons::Icon::heartFilled : icons::Icon::heart);
        favouriteButton.setActive (isFavourite);
        favouriteButton.setTooltip (isFavourite
                                        ? "Favourite\nRemove this preset from your favourites."
                                        : "Favourite\nKeep this preset in your favourites.");
    }

    void HeaderBar::refreshBrowserPill()
    {
        browserButton.setSelected (host.isBrowserOpen());
    }
}
