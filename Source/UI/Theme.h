#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    NACAR visual language.

    Every colour, font and shading routine used anywhere in the interface lives
    here. Components never construct a juce::Colour from a literal - if a shade
    is missing from this file, it gets added here first.

    The palette is transcribed from DesignReference/NACAR_V1_LOCKED_ART_DIRECTION.png
    and documented in DesignReference/NACAR_UI_SPEC.md section 1.
*/
namespace nacar::theme
{
    // -----------------------------------------------------------------------
    //  Ceramic / machined aluminium - the chassis
    // -----------------------------------------------------------------------
    inline const juce::Colour ceramicLight  { 0xffe6e2de };
    inline const juce::Colour ceramicMid    { 0xffd8d3cf };
    inline const juce::Colour ceramicDark   { 0xffc8c3bf };
    inline const juce::Colour ceramicEdge   { 0xffb4aea9 };
    inline const juce::Colour ceramicDeep   { 0xffa39c96 };

    // -----------------------------------------------------------------------
    //  Optical glass - viewport, FX chain, nav, preset bar
    // -----------------------------------------------------------------------
    inline const juce::Colour glassDeep     { 0xff0c0c0e };
    inline const juce::Colour glassMid      { 0xff111114 };
    inline const juce::Colour glassRaised   { 0xff17171a };
    inline const juce::Colour glassEdge     { 0xff2a2a30 };

    // -----------------------------------------------------------------------
    //  Accents.  Violet = selected / active.  Mint = powered / alive.
    //  Nothing else in the interface uses these hues.
    // -----------------------------------------------------------------------
    inline const juce::Colour violet        { 0xffb8a0ff };
    inline const juce::Colour violetLight   { 0xffc9b7ff };
    inline const juce::Colour violetDeep    { 0xffa987ff };

    /** Type ON an accent face, and nothing else.
        
        The three violets above are all light - they are made for glowing on a
        dark ground, which is what violet does everywhere else in this
        instrument. On an accentSurface face, which brightens its fill by 0.30,
        they converge with it: measured on the render, violetDeep on the MUTATE
        button gives a contrast ratio of 1.6:1, and plain violet 1.3:1. That is
        not a legibility opinion, it is a number, and a label nobody can read is
        not a style choice.
        
        This is the fourth violet, dark enough to be read and still
        unambiguously the same hue: 4.4:1 on the MUTATE face. It was added
        rather than approximated because a widget author measured the problem,
        reported it, and correctly refused to invent a shade in a frozen file. */
    inline const juce::Colour violetInk     { 0xff5b3fa8 };

    inline const juce::Colour mint          { 0xff9dffe4 };
    inline const juce::Colour mintDeep      { 0xff78f5d5 };

    // -----------------------------------------------------------------------
    //  Type
    // -----------------------------------------------------------------------
    inline const juce::Colour ink           { 0xff2b2825 };
    inline const juce::Colour inkMuted      { 0xff6e6862 };
    inline const juce::Colour inkFaint      { 0xff9a938c };

    inline const juce::Colour glassInk      { 0xfff2f0ee };
    inline const juce::Colour glassInkMuted { 0xff8a8a93 };
    inline const juce::Colour glassInkFaint { 0xff5a5a63 };

    // -----------------------------------------------------------------------
    //  Fonts
    //
    //  NACAR uses one grotesque at several weights and a lot of letter-spacing.
    //  JUCE has no tracking on juce::Font, so tracked runs are drawn through
    //  drawTracked() below, which lays out glyph by glyph.
    // -----------------------------------------------------------------------
    juce::Font display  (float height);          ///< wordmark / large headings
    juce::Font medium   (float height);          ///< values, preset names
    juce::Font label    (float height);          ///< small caps labels
    juce::Font mono     (float height);          ///< times, numeric readouts

    /** Draws text with explicit letter-spacing, expressed in ems. */
    void drawTracked (juce::Graphics&, juce::StringRef text, juce::Rectangle<float> area,
                      const juce::Font&, float trackingEm,
                      juce::Justification = juce::Justification::centredLeft);

    /** Width a tracked run will occupy. */
    float trackedWidth (juce::StringRef text, const juce::Font&, float trackingEm);

    // -----------------------------------------------------------------------
    //  Shading vocabulary  (UI spec section 1)
    // -----------------------------------------------------------------------

    /** Soft contact shadow underneath a raised ceramic element. */
    void contactShadow (juce::Graphics&, juce::Rectangle<float> bounds, float corner,
                        float offsetY = 2.0f, float blur = 8.0f, float alpha = 0.18f);

    /** Raised ceramic surface: vertical gradient, specular top edge, bevel bottom. */
    void ceramicSurface (juce::Graphics&, juce::Rectangle<float> bounds, float corner,
                         juce::Colour top = ceramicLight, juce::Colour bottom = ceramicMid);

    /** Recessed optical glass cut-out: flat deep fill, inner top shadow, hairline. */
    void glassSurface (juce::Graphics&, juce::Rectangle<float> bounds, float corner,
                       juce::Colour fill = glassDeep);

    /** 1 px hairline rule in the chassis. */
    void hairline (juce::Graphics&, juce::Point<float> a, juce::Point<float> b,
                   juce::Colour = ceramicEdge);

    /** Accent glow - a soft radial halo used behind active indicators. */
    void glow (juce::Graphics&, juce::Point<float> centre, float radius,
               juce::Colour, float alpha = 0.55f);

    /** Machined concentric sheen used on knob caps.

        SUPERSEDED by domeCap() below, which lights the cap as a solid object
        rather than as a disc with a gradient on it.  Kept because removing it
        would change every call site in one commit; new code calls domeCap. */
    void machinedCap (juce::Graphics&, juce::Point<float> centre, float radius,
                      bool darkCap = false);

    // -----------------------------------------------------------------------
    //  DIMENSIONAL VOCABULARY
    //
    //  The routines above draw surfaces.  These draw OBJECTS: things with a
    //  thickness, sitting on the chassis, lit from one direction, casting a
    //  shadow onto what is behind them.  That difference is the whole of what
    //  separates the reference image from a flat redraw of it.
    //
    //  ONE LIGHT, AND IT NEVER MOVES.  Upper-left, about sixty degrees of
    //  elevation.  Every specular, every bevel, every contact shadow in the
    //  instrument derives from `lightX`/`lightY` below, so an element cannot be lit
    //  from a direction of its own and break the illusion for everything
    //  around it.  This is the single most important rule in this file: a
    //  panel of controls reads as machined metal only while they all agree
    //  about where the light is.
    // -----------------------------------------------------------------------

    /** The light direction, as a unit vector in screen space (y grows down).
        Upper-left: negative x, negative y. */
    inline constexpr float lightX = -0.55f;
    inline constexpr float lightY = -0.83f;

    /**
        How far off the chassis an element sits.  Elevation drives the shadow,
        the strength of the specular and the depth of the bevel together,
        because in life those three are one fact seen three ways - and a
        control whose shadow says "floating" while its bevel says "flush" is
        exactly what makes an interface look drawn rather than built.
    */
    enum class Elevation
    {
        flush,      ///< engraved into the chassis: no shadow, bevel only
        resting,    ///< a pill, a small button - just off the surface
        raised,     ///< a primary action, a knob cap
        floating    ///< a card or a drawer over everything else
    };

    /**
        A raised ceramic object.  Contact shadow, domed body, specular top edge,
        bevelled bottom edge, and the inset highlight just under the top edge
        that reads as the material's own thickness.

        `press` and `hover` are 0..1 and are meant to be animated.  Pressing
        does not merely darken: the object sinks, its shadow tightens and its
        specular moves to the bottom edge, which is what a real key does.
    */
    void raisedCeramic (juce::Graphics&, juce::Rectangle<float> bounds, float corner,
                        Elevation = Elevation::resting,
                        float press = 0.0f, float hover = 0.0f,
                        juce::Colour top = ceramicLight,
                        juce::Colour bottom = ceramicMid);

    /** The same object in optical glass: a dark control on a dark ground, which
        needs its edges lit rather than its face, or it disappears. */
    void raisedGlass (juce::Graphics&, juce::Rectangle<float> bounds, float corner,
                      Elevation = Elevation::resting,
                      float press = 0.0f, float hover = 0.0f,
                      juce::Colour fill = glassRaised);

    /** An accent-filled primary action.  The fill is lit from within as well as
        from above, which is what separates "selected" from "painted violet". */
    void accentSurface (juce::Graphics&, juce::Rectangle<float> bounds, float corner,
                        juce::Colour accent, float press = 0.0f, float hover = 0.0f);

    /** A well cut into the surface: inner shadow under the top edge, a catch of
        light along the bottom.  The exact inverse of raisedCeramic, and what a
        groove, a track or a pressed state should look like. */
    void recessedWell (juce::Graphics&, juce::Rectangle<float> bounds, float corner,
                       juce::Colour fill, float depth = 1.0f);

    /** A soft glow just inside an edge.  What makes an active card read as lit
        from within rather than outlined. */
    void innerGlow (juce::Graphics&, juce::Rectangle<float> bounds, float corner,
                    juce::Colour, float alpha, float spread);

    /** A glow outside an edge - the halo an active element throws onto the
        surface behind it. */
    void outerGlow (juce::Graphics&, juce::Rectangle<float> bounds, float corner,
                    juce::Colour, float alpha, float spread);

    /**
        A knob cap as a solid object.

        The difference from machinedCap is that the light source is a POINT off
        the upper-left rather than a linear gradient: the bright spot sits
        inside the disc, the far edge falls into shadow, and the rim catches
        light on one side and loses it on the other.  That is what a turned
        aluminium cap does, and it is why a knob drawn this way looks like it
        could be gripped.
    */
    void domeCap (juce::Graphics&, juce::Point<float> centre, float radius,
                  bool darkCap = false, float hover = 0.0f);

    /** The machined recess a knob cap sits in: a dark seat ring with its own
        inner shadow, so the cap has somewhere to be raised FROM. */
    void capSeat (juce::Graphics&, juce::Point<float> centre, float capRadius,
                  float seatWidth = 3.0f);

    // -----------------------------------------------------------------------
    //  CHASSIS VOCABULARY
    //
    //  The routines above are for controls.  These are for the four large
    //  regions the controls sit in - the plates, the cut-outs in them, and the
    //  engraved rules that divide them.  They are composed from raisedCeramic
    //  and recessedWell rather than replacing them, and they exist separately
    //  because a plate is not a big button: raisedCeramic's contact shadow is
    //  entirely clipped away at panel size, and a well two hundred pixels tall
    //  strokes its inner shadow down both sides, where the sides are all you
    //  see - which turns a seated module into a box, and the reference has no
    //  boxes.
    //
    //  These were written by a chassis author while this file was frozen to
    //  them, so they lived in HeaderBar.h and the other three panels included
    //  the header bar's header to reach them.  That was flagged rather than
    //  left quiet, and this is where they belong.
    // -----------------------------------------------------------------------
    namespace chassis
    {
        /** How far a plate's painted body sits inside its component bounds.

            A component clips its own paint to its bounds, so a panel can never
            cast a shadow onto the chassis behind it; one pixel of margin is
            what lets the near edge of that contact read at all.  The value is
            shared so the three ceramic plates agree with each other. */
        inline constexpr float plateInset = 1.0f;

        /** Strokes one rounded-rectangle outline clipped to a horizontal band.

            juce::Path::addRoundedRectangle's per-corner flags still close the
            path, so stroking "just the top edge" of a rounded shape draws a
            square-cornered outline all the way round.  Clipping is the only way
            to light one edge of a rounded rectangle and not all four. */
        inline void strokeBand (juce::Graphics& g, juce::Rectangle<float> bounds, float corner,
                                float fromT, float toT, juce::Colour colour,
                                float thickness = 1.0f, float inset = 0.5f)
        {
            if (colour.getFloatAlpha() <= 0.004f || bounds.getHeight() <= 1.0f)
                return;

            juce::Path outline;
            outline.addRoundedRectangle (bounds.reduced (inset),
                                         juce::jmax (0.5f, corner - inset));

            const auto band = bounds.withTop    (bounds.getY() + bounds.getHeight() * fromT)
                                    .withBottom (bounds.getY() + bounds.getHeight() * toT);

            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (band.getSmallestIntegerContainer());
            g.setColour (colour);
            g.strokePath (outline, juce::PathStrokeType (thickness));
        }

        /**
            A region panel as a machined plate rather than a filled rectangle.

            This is raisedCeramic's anatomy - contact, body, sheen,
            specular top edge, bevelled bottom, the inset highlight that is the
            material's own thickness - composed rather than called, for two
            reasons.

            The first is that raisedCeramic spends ten full-size rounded-rect
            fills on a contact shadow, and a component clips its own paint to
            its bounds, so on a panel every one of those ten is thrown away.
            The bottom bar repaints at 30 Hz for the meter; it cannot afford to
            throw away ten fills of its own width thirty times a second.

            The second is that a plate is not a button.  It is large enough to
            curve away from the light well before it reaches its own rim, and
            without that fall-off the two shaded sides stay as bright as the lit
            ones and the whole thing reads as printed however carefully its edge
            is drawn.
        */
        inline void plate (juce::Graphics& g, juce::Rectangle<float> bounds, float corner)
        {
            if (bounds.isEmpty())
                return;

            // 1. The edge against the window.  The body is about to cover the
            //    inner half of this stroke, which leaves it exactly filling the
            //    margin plateInset holds back - a half-pixel of contact, which
            //    is all a component that cannot paint outside itself can have.
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.drawRoundedRectangle (bounds.expanded (0.5f), corner + 0.5f, 1.0f);

            // 2. The body: vertical gradient, upper-left sheen, specular top
            //    edge, bevelled bottom edge.
            ceramicSurface (g, bounds, corner);

            // 3. The material's own thickness - the highlight one pixel inside
            //    the top edge that every raised object in the instrument
            //    carries, and the detail that most separates a moulded plate
            //    from a rectangle with a border.
            strokeBand (g, bounds.reduced (1.0f), juce::jmax (0.5f, corner - 1.0f),
                        0.0f, 0.35f, juce::Colours::white.withAlpha (0.42f));

            // 4. And the far edge deepened: ceramicSurface bevels the bottom
            //    for an object a few pixels thick, which is not enough here.
            strokeBand (g, bounds, corner, 0.55f, 1.0f,
                        juce::Colours::black.withAlpha (0.14f));

            // 5. The fall-off.
            juce::Graphics::ScopedSaveState ss (g);

            juce::Path body;
            body.addRoundedRectangle (bounds, corner);
            g.reduceClipRegion (body);

            // The light is upper-left, so the bottom and the right are the two
            // sides that lose it.
            const float fall = juce::jmin (22.0f, bounds.getHeight() * 0.12f);

            if (fall > 1.0f)
            {
                g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack,
                                                         bounds.getCentreX(), bounds.getBottom() - fall,
                                                         juce::Colours::black.withAlpha (0.055f),
                                                         bounds.getCentreX(), bounds.getBottom(), false));
                g.fillRect (bounds.withTop (bounds.getBottom() - fall));
            }

            const float side = juce::jmin (22.0f, bounds.getWidth() * 0.08f);

            if (side > 1.0f)
            {
                g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack,
                                                         bounds.getRight() - side, bounds.getCentreY(),
                                                         juce::Colours::black.withAlpha (0.038f),
                                                         bounds.getRight(), bounds.getCentreY(), false));
                g.fillRect (bounds.withLeft (bounds.getRight() - side));
            }
        }

        /**
            The lip of a cut in the chassis.

            The glass inside a cut-out is opaque and is painted by whatever owns
            it.  What makes the result read as a recess rather than as a dark
            rectangle laid on ceramic happens entirely *outside* those bounds:
            the near wall of the cut, at the top, is the one wall the light
            cannot reach, and the far wall, at the bottom, is the one that
            catches it.  Spec section 12: "a dark rectangle drawn on ceramic is
            not a recess; an inner shadow under its top edge is."
        */
        inline void cutOut (juce::Graphics& g, juce::Rectangle<float> bounds, float corner,
                            float alpha = 1.0f)
        {
            if (bounds.isEmpty() || alpha <= 0.01f)
                return;

            // Ambient occlusion: the ceramic immediately around a cut turns
            // down into it and never gets the whole of the light back.  This is
            // doing most of the work - a dark rim drawn against near-black
            // glass is invisible by definition, so what actually says "hole"
            // is the four pixels of ceramic *outside* it going down towards
            // ceramicEdge.  Outermost ring first.
            {
                static constexpr float ring[4][2] = { { 4.2f, 0.020f },
                                                      { 3.0f, 0.042f },
                                                      { 1.8f, 0.070f },
                                                      { 0.6f, 0.105f } };

                for (const auto& r : ring)
                {
                    g.setColour (juce::Colours::black.withAlpha (r[1] * alpha));
                    g.drawRoundedRectangle (bounds.expanded (r[0]), corner + r[0], 1.5f);
                }
            }

            const auto rim = bounds.expanded (1.0f);

            strokeBand (g, rim, corner + 1.0f, 0.0f, 0.55f,
                        ceramicDeep.withAlpha (0.80f * alpha), 1.4f, 0.0f);

            strokeBand (g, rim, corner + 1.0f, 0.50f, 1.0f,
                        juce::Colours::white.withAlpha (0.62f * alpha), 1.2f, 0.0f);
        }

        /**
            An engraved rule: a cut line, and the lip below it that catches the
            light.  One hairline on its own is a drawn line; two, a pixel apart
            and opposite in value, is a groove in a lit surface.

            Horizontal grooves take their lit lip underneath; vertical ones take
            it on the right, because in both cases that is the wall turned
            towards an upper-left source.
        */
        inline void engravedLine (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b,
                                  float alpha = 1.0f)
        {
            if (alpha <= 0.01f)
                return;

            const bool vertical = std::abs (b.x - a.x) < std::abs (b.y - a.y);

            const juce::Point<float> lip = vertical ? juce::Point<float> (1.0f, 0.0f)
                                                    : juce::Point<float> (0.0f, 1.0f);

            // Snapped to the half-pixel grid the line actually lands on.  Two
            // hairlines a pixel apart only read as a groove while both of them
            // are crisp: let either fall between rows and the pair turns into
            // one soft grey smear, which is worse than the single line it
            // replaces.
            const auto snap = [vertical] (juce::Point<float> p)
            {
                return vertical ? juce::Point<float> (std::floor (p.x) + 0.5f, p.y)
                                : juce::Point<float> (p.x, std::floor (p.y) + 0.5f);
            };

            const auto from = snap (a);
            const auto to   = snap (b);

            hairline (g, from, to, ceramicDeep.withAlpha (0.55f * alpha));
            hairline (g, from + lip, to + lip,
                             juce::Colours::white.withAlpha (0.50f * alpha));
        }

    }

    // -----------------------------------------------------------------------
    //  LookAndFeel
    // -----------------------------------------------------------------------
    class NacarLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        NacarLookAndFeel();

        juce::Font getPopupMenuFont() override;
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
        void drawTooltip (juce::Graphics&, const juce::String&, int width, int height) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>,
                                               juce::Rectangle<int>) override;
    };
}
