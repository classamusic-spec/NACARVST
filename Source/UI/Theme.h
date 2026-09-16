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

    /** Machined concentric sheen used on knob caps. */
    void machinedCap (juce::Graphics&, juce::Point<float> centre, float radius,
                      bool darkCap = false);

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
