#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Every coordinate in the NACAR interface.

    These are logical units on the 1536 x 1024 canvas of the locked reference
    image, and they mirror DesignReference/NACAR_UI_SPEC.md one for one.  The
    editor applies a single uniform transform; no component computes its own
    scale factor and no component hard-codes a coordinate locally.

    Two conventions hold throughout, without exception:

      * Every coordinate in a region sub-namespace is **region-local**. A panel
        positions its contents against its own origin and never needs to know
        where it sits on the canvas.

      * Every `*Base` / `*Baseline` constant is a **true text baseline**, not the
        top of the em box. `ui::ceramicLabel` and `ui::glassLabel` take a
        baseline and subtract the ascent themselves, so these values pass
        straight through with no conversion at the call site.

    Rule: if a number describes *where something is*, it belongs in this file.

    Note on `const` rather than `constexpr` for the rectangles: juce::Rectangle
    has no constexpr constructor in JUCE 8, so RectF constants cannot be
    constant expressions. juce::Point does, which is why the Pt constants and
    the aggregates built from them stay constexpr.
*/
namespace nacar::layout
{
    using Rect  = juce::Rectangle<int>;
    using RectF = juce::Rectangle<float>;
    using Pt    = juce::Point<float>;

    // -----------------------------------------------------------------------
    //  Canvas
    // -----------------------------------------------------------------------
    inline constexpr int   canvasWidth   = 1536;
    inline constexpr int   canvasHeight  = 1024;
    inline constexpr float aspect        = (float) canvasWidth / (float) canvasHeight;

    inline constexpr float scaleMin      = 0.55f;   // below 75% is unsupported but not fatal
    inline constexpr float scaleMax      = 2.0f;

    inline constexpr float margin        = 8.0f;
    inline constexpr float radiusPanel   = 14.0f;
    inline constexpr float radiusCard    = 10.0f;
    inline constexpr float radiusPill    = 8.0f;

    // -----------------------------------------------------------------------
    //  Regions (UI spec section 2)
    // -----------------------------------------------------------------------
    inline const     RectF header     { 8.0f,    10.0f, 1520.0f,  86.0f };
    inline const     RectF leftPanel  { 8.0f,   102.0f,  317.0f, 818.0f };
    inline const     RectF viewport   { 333.0f, 102.0f,  875.0f, 337.0f };
    inline const     RectF mutatePanel{ 333.0f, 447.0f,  875.0f, 267.0f };
    inline const     RectF fxChain    { 333.0f, 722.0f,  875.0f, 198.0f };
    inline const     RectF rightPanel { 1216.0f,102.0f,  312.0f, 818.0f };
    inline const     RectF bottomBar  { 8.0f,   928.0f, 1520.0f,  66.0f };
    inline const     RectF footer     { 8.0f,   994.0f, 1520.0f,  22.0f };

    // -----------------------------------------------------------------------
    //  Header (UI spec section 3) - coordinates are region-local
    // -----------------------------------------------------------------------
    namespace hdr
    {
        inline constexpr float wordmarkX      = 58.0f;    // 66 - header.x
        inline constexpr float wordmarkBase   = 50.0f;
        inline constexpr float wordmarkSize   = 30.0f;
        inline constexpr float wordmarkTrack  = 0.34f;

        inline constexpr float descriptorX    = 60.0f;
        inline constexpr float descriptorBase  = 68.0f;
        inline constexpr float descriptorSize  = 8.0f;
        inline constexpr float descriptorTrack = 0.42f;

        inline constexpr float dividerX       = 294.0f;
        inline constexpr float dividerTop     = 20.0f;
        inline constexpr float dividerBottom  = 66.0f;

        inline constexpr float taglineX       = 314.0f;
        inline constexpr float taglineBase1   = 41.0f;
        inline constexpr float taglineBase2   = 55.0f;
        inline constexpr float taglineSize    = 8.5f;
        inline constexpr float taglineTrack   = 0.16f;

        inline const     RectF presetBar      { 485.0f, 20.0f, 608.0f, 54.0f };
        inline constexpr float presetBarRadius = 12.0f;

        inline constexpr Pt    sourceGlyph    { 523.0f, 47.0f };
        inline constexpr Pt    meterGlyph     { 573.0f, 47.0f };
        inline constexpr float presetNameX    = 600.0f;
        inline constexpr float presetNameSize = 17.0f;
        inline constexpr Pt    prevArrow      { 830.0f, 47.0f };
        inline constexpr Pt    nextArrow      { 882.0f, 47.0f };
        inline constexpr Pt    favourite      { 950.0f, 47.0f };
        inline constexpr float favouriteRadius = 20.0f;
        inline const     RectF browserPill    { 982.0f, 29.0f, 88.0f, 36.0f };

        inline constexpr Pt    gear           { 1237.0f, 43.0f };
        inline constexpr Pt    waveLogo       { 1335.0f, 43.0f };
        inline constexpr float coordX         { 1402.0f };
        inline constexpr float coordBase1     { 40.0f };
        inline constexpr float coordBase2     { 54.0f };
        inline constexpr float coordSize      { 8.5f };
    }

    // -----------------------------------------------------------------------
    //  Left macro panel (UI spec section 4) - region-local
    // -----------------------------------------------------------------------
    namespace macro
    {
        struct KnobSpec { Pt centre; float radius; float labelBaseline; };

        //                                 centre (local)        r      label
        inline constexpr KnobSpec memory   { { 135.0f, 106.0f }, 82.0f, 210.0f };
        inline constexpr KnobSpec character{ {  86.0f, 313.0f }, 48.0f, 374.0f };
        inline constexpr KnobSpec motion   { { 228.0f, 313.0f }, 48.0f, 374.0f };
        inline constexpr KnobSpec world    { {  86.0f, 498.0f }, 48.0f, 567.0f };
        inline constexpr KnobSpec weight   { { 228.0f, 498.0f }, 48.0f, 563.0f };
        inline constexpr KnobSpec alter    { { 105.0f, 718.0f }, 70.0f, 797.0f };
        inline constexpr KnobSpec random   { { 251.0f, 713.0f }, 22.0f, 754.0f };

        // Generation selector
        inline constexpr float genDotX     = 245.0f;
        inline constexpr float genLabelX   = 264.0f;
        inline constexpr float genY[4]     = { 66.0f, 100.0f, 134.0f, 167.0f };
        inline constexpr float genDotR     = 5.0f;
        inline constexpr float genDotRActive = 6.0f;

        // Scale legends
        inline constexpr float legendSize  = 7.5f;
        inline constexpr float legendTrack = 0.12f;
        inline constexpr float legendCharacterBase = 398.0f;
        inline constexpr float legendMotionBase    = 398.0f;
        inline constexpr float legendWorldBase     = 592.0f;

        // WEIGHT mode selector
        inline const     RectF weightModes { 182.0f, 574.0f, 96.0f, 22.0f };

        inline constexpr float knobLabelSize  = 9.5f;
        inline constexpr float knobLabelTrack = 0.18f;
    }

    // -----------------------------------------------------------------------
    //  Optical viewport (UI spec section 5) - region-local
    // -----------------------------------------------------------------------
    namespace vp
    {
        inline constexpr float inset          = 19.0f;

        inline const     RectF accentBar      { 24.0f, 34.0f, 2.0f, 34.0f };
        inline constexpr float titleX         = 60.0f;
        inline constexpr float titleBase      = 48.0f;
        inline constexpr float titleSize      = 13.0f;
        inline constexpr float metaBase       = 67.0f;
        inline constexpr float metaSize       = 9.5f;
        inline constexpr Pt    pencil         { 271.0f, 61.0f };

        inline const     RectF snapPill       { 613.0f, 35.0f, 57.0f, 29.0f };
        inline constexpr float toolY          = 35.0f;
        inline constexpr float toolSize       = 29.0f;
        inline constexpr float toolX[4]       = { 685.0f, 724.0f, 767.0f, 811.0f };

        inline const     RectF waveField      { 19.0f, 84.0f, 840.0f, 160.0f };
        inline const     RectF overviewStrip   { 19.0f, 246.0f, 840.0f, 20.0f };

        inline constexpr float transportY     = 299.0f;   // centre line, local
        inline constexpr Pt    playButton     { 53.0f,  299.0f };
        inline constexpr Pt    stopButton     { 111.0f, 299.0f };
        inline constexpr float transportR     = 22.0f;
        inline constexpr Pt    resetButton    { 172.0f, 299.0f };
        inline constexpr float timeX          = 216.0f;
        inline constexpr float timeSize       = 11.0f;
        inline constexpr Pt    loopButton     { 384.0f, 299.0f };
        inline constexpr Pt    trimButton     { 436.0f, 299.0f };
        inline constexpr Pt    shuffleButton  { 514.0f, 299.0f };
        inline constexpr Pt    zoomOutButton  { 564.0f, 299.0f };
        inline constexpr float zoomLabelX     = 627.0f;
        inline constexpr float zoomTrackL     = 670.0f;
        inline constexpr float zoomTrackR     = 797.0f;
        inline constexpr Pt    zoomInButton   { 825.0f, 299.0f };
        inline constexpr float iconButtonR    = 15.0f;
    }

    // -----------------------------------------------------------------------
    //  Mutate panel (UI spec section 6) - region-local
    // -----------------------------------------------------------------------
    namespace mut
    {
        inline constexpr Pt    sparkle        { 35.0f, 34.0f };
        inline constexpr float titleX         = 60.0f;
        inline constexpr float titleBase      = 45.0f;
        inline constexpr float titleSize      = 25.0f;
        inline constexpr float titleTrack     = 0.06f;
        inline constexpr float subtitleX      = 61.0f;
        inline constexpr float subtitleBase   = 61.0f;
        inline constexpr float subtitleSize   = 7.5f;
        inline constexpr float subtitleTrack  = 0.20f;

        inline constexpr float harmonyLabelX  = 367.0f;
        inline constexpr float selectorBase   = 39.0f;
        inline const     RectF harmonySeg     { 432.0f, 19.0f, 175.0f, 31.0f };
        inline constexpr float distanceLabelX = 637.0f;
        inline const     RectF distanceSeg    { 697.0f, 19.0f, 160.0f, 31.0f };
        inline constexpr float selectorLabelSize  = 8.5f;
        inline constexpr float selectorLabelTrack = 0.16f;

        inline constexpr float intentY        = 83.0f;
        inline constexpr float intentH        = 32.0f;
        inline constexpr float intentX0       = 24.0f;
        inline constexpr float intentGap      = 8.0f;
        inline constexpr float intentPadding  = 34.0f;
        inline constexpr float intentSize     = 8.5f;
        inline constexpr float intentTrack    = 0.10f;

        inline constexpr float actionY        = 143.0f;
        inline constexpr float actionH        = 57.0f;
        inline const     RectF mutateButton   {  22.0f, 143.0f, 183.0f, 57.0f };
        inline const     RectF againButton    { 208.0f, 143.0f, 186.0f, 57.0f };
        inline const     RectF printButton    { 397.0f, 143.0f, 160.0f, 57.0f };
        inline constexpr float actionDivider  = 592.0f;
        inline const     RectF makeInstrument { 622.0f, 143.0f, 235.0f, 57.0f };

        inline constexpr float preserveBase   = 245.0f;
        inline constexpr float preserveLabelX = 24.0f;
        inline constexpr float preserveX0     = 125.0f;
        inline constexpr float preserveSize   = 8.0f;
        inline constexpr float preserveTrack  = 0.10f;
        inline constexpr float switchW        = 26.0f;
        inline constexpr float switchH        = 14.0f;
        inline constexpr float switchGap      = 10.0f;
        inline constexpr float preserveGap    = 22.0f;
    }

    // -----------------------------------------------------------------------
    //  FX chain (UI spec section 7) - region-local
    // -----------------------------------------------------------------------
    namespace fx
    {
        inline constexpr float titleX       = 16.0f;
        inline constexpr float titleBase    = 25.0f;
        inline constexpr float titleSize    = 8.5f;
        inline constexpr float titleTrack   = 0.18f;
        inline const     RectF addButton    {  95.0f, 11.0f, 22.0f, 22.0f };
        inline const     RectF collapse     { 839.0f, 11.0f, 24.0f, 22.0f };

        inline constexpr float cardY        = 55.0f;
        inline constexpr float cardH        = 100.0f;
        inline constexpr float cardW        = 112.0f;
        inline constexpr float cardPitch    = 132.0f;
        inline constexpr float cardX0       = 17.0f;
        inline constexpr float linkY        = 104.0f;
        inline const     RectF addSlot      { 807.0f, 55.0f, 55.0f, 100.0f };

        inline constexpr float nameSize     = 8.5f;
        inline constexpr float nameTrack    = 0.14f;
        inline constexpr int   numCards     = 6;
    }

    // -----------------------------------------------------------------------
    //  Right atmosphere panel (UI spec section 8) - region-local
    // -----------------------------------------------------------------------
    namespace atmos
    {
        struct ModuleSpec
        {
            Pt    icon;
            float titleBaseline;
            float subtitleBaseline;
            Pt    knob;
            float knobRadius;
            Pt    power;
            float listTop;          ///< baseline of the first parameter row
            float listSpacing;
            int   numParams;
        };

        inline constexpr float iconX      =  49.0f;   // 1265 - 1216
        inline constexpr float titleX     =  74.0f;
        inline constexpr float listX      = 224.0f;
        inline constexpr float ruleX      = 213.0f;
        inline constexpr float dotX       = 198.0f;
        inline constexpr float powerR     =  13.0f;

        inline constexpr float titleSize  = 13.0f;
        inline constexpr float titleTrack = 0.13f;
        inline constexpr float subSize    = 7.0f;
        inline constexpr float subTrack   = 0.16f;
        inline constexpr float listSize   = 8.0f;
        inline constexpr float listTrack  = 0.12f;

        inline constexpr float divider[3] = { 230.0f, 459.0f, 653.0f };

        inline constexpr ModuleSpec aura   { { 49.0f,  44.0f },  42.0f,  56.0f,
                                             { 106.0f, 150.0f }, 70.0f, { 270.0f,  44.0f },
                                              98.0f, 26.0f, 5 };
        inline constexpr ModuleSpec shadow { { 49.0f, 271.0f }, 269.0f, 283.0f,
                                             { 106.0f, 377.0f }, 70.0f, { 270.0f, 271.0f },
                                             329.0f, 26.0f, 5 };
        inline constexpr ModuleSpec breath { { 49.0f, 495.0f }, 493.0f, 507.0f,
                                             { 106.0f, 577.0f }, 62.0f, { 270.0f, 495.0f },
                                             551.0f, 26.0f, 4 };
        inline constexpr ModuleSpec patina { { 49.0f, 686.0f }, 684.0f, 698.0f,
                                             { 106.0f, 759.0f }, 62.0f, { 270.0f, 686.0f },
                                             736.0f, 22.0f, 4 };
    }

    // -----------------------------------------------------------------------
    //  Bottom bar (UI spec section 9) - region-local
    // -----------------------------------------------------------------------
    namespace bottom
    {
        inline constexpr float sourceY     = 12.0f;
        inline constexpr float sourceH     = 40.0f;
        inline constexpr float sourceX[5]  = {  27.0f, 127.0f, 234.0f, 338.0f, 457.0f };
        inline constexpr float sourceW[5]  = {  89.0f,  96.0f,  95.0f, 109.0f, 101.0f };
        inline constexpr float sourceSize  = 8.5f;
        inline constexpr float sourceTrack = 0.14f;

        inline const     RectF navBar      { 582.0f, 2.0f, 490.0f, 60.0f };
        inline const     RectF navActive   { 597.0f, 8.0f,  90.0f, 48.0f };
        inline constexpr float navCentre[5]= { 642.0f, 739.0f, 832.0f, 921.0f, 1012.0f };
        inline constexpr float navIconY    = 22.0f;
        inline constexpr float navLabelBase= 46.0f;
        inline constexpr float navSize     = 7.5f;
        inline constexpr float navTrack    = 0.14f;

        inline constexpr float outputLabelX = 1127.0f;
        inline constexpr float outputBase   = 35.0f;
        inline const     RectF meter        { 1197.0f, 24.0f, 195.0f, 12.0f };
        inline constexpr Pt    masterKnob   { 1464.0f, 29.0f };
        inline constexpr float masterRadius = 27.0f;
    }

    // -----------------------------------------------------------------------
    //  Footer
    // -----------------------------------------------------------------------
    namespace foot
    {
        inline constexpr float leftX   = 27.0f;
        inline constexpr float rightX  = 1492.0f;
        inline constexpr float baseline = 8.0f;
        inline constexpr float size    = 7.0f;
        inline constexpr float track   = 0.24f;
    }

    // -----------------------------------------------------------------------
    //  Helpers
    // -----------------------------------------------------------------------
    inline RectF knobBounds (const macro::KnobSpec& k)
    {
        return RectF (k.radius * 2.0f, k.radius * 2.0f).withCentre (k.centre);
    }

    inline RectF centredSquare (Pt centre, float halfSize)
    {
        return RectF (halfSize * 2.0f, halfSize * 2.0f).withCentre (centre);
    }

    /** Rect for FX card index i (0..numCards-1), region-local. */
    inline RectF fxCard (int i)
    {
        return RectF (fx::cardX0 + (float) i * fx::cardPitch, fx::cardY, fx::cardW, fx::cardH);
    }
}
