#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Every glyph in the NACAR interface.

    Icons are vector paths, not images: they stay crisp at 200 % and on HiDPI,
    and they recolour with state.  Each path is authored inside a 100 x 100 box
    centred on (50, 50) and is scaled to fit wherever it is drawn.

    Stroked icons are returned unclosed and are meant to be drawn with
    drawStroked(); filled icons are closed shapes for drawFilled().  draw()
    picks the right one.
*/
namespace nacar::icons
{
    enum class Icon
    {
        // -- header ---------------------------------------------------------
        gear, waveLogo, heart, heartFilled, chevronLeft, chevronRight,
        chevronUp, chevronDown, sourceBrackets, meterBars,

        // -- viewport toolbar ------------------------------------------------
        pencil, waveformMode, listMode, markers, expand,

        // -- transport -------------------------------------------------------
        play, pause, stop, returnToZero, loop, trim, shuffle,
        zoomOut, zoomIn,

        // -- mutate ----------------------------------------------------------
        sparkle, cube, print, makeInstrument, arrowRight,

        // -- FX modules ------------------------------------------------------
        cassette, dotMatrix, filterCurve, rewind, concentric,
        link, power, plus, minus, cross, collapse,

        // -- atmosphere ------------------------------------------------------
        planet, wave3, triangle,

        // -- source modes ----------------------------------------------------
        srcSynth, srcSample, srcGrain, srcResonator, srcSpectral,

        // -- bottom navigation -----------------------------------------------
        navMain, navMod, navFx, navSeq, navMix,

        // -- misc ------------------------------------------------------------
        search, folder, star, dice, lock, unlock, dragHandle,

        count
    };

    /** True when this icon is a solid shape rather than a stroked outline. */
    bool isFilled (Icon) noexcept;

    /** The raw path, authored in a 100 x 100 box. */
    const juce::Path& path (Icon);

    /** Path scaled and centred to fit `area`, preserving aspect. */
    juce::Path pathFor (Icon, juce::Rectangle<float> area);

    /** Draws the icon in `area`, choosing fill or stroke automatically.
        `thickness` is expressed for a 24 px icon and scales with the area. */
    void draw (juce::Graphics&, Icon, juce::Rectangle<float> area,
               juce::Colour, float thickness = 1.5f);
}
