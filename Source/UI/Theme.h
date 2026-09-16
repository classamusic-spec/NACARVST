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
