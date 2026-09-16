#include "Icons.h"

#include <array>
#include <cmath>
#include <initializer_list>

namespace nacar::icons
{
    // -----------------------------------------------------------------------
    //  Authoring conventions
    //
    //  Every glyph is drawn inside a 100 x 100 box with its visual mass on
    //  (50, 50).  It is the *box*, not the glyph's own bounds, that gets mapped
    //  onto the destination rectangle - which is the only way a play triangle
    //  and a gear can be given different weights and still read as the same
    //  size sitting next to each other in a toolbar.  Glyphs that carry a lot
    //  of ink (play, stop, the sequencer blocks) are therefore authored
    //  noticeably smaller than the open outlines (gear, cassette, planet).
    //
    //  Stroked glyphs borrow their corner radii from PathStrokeType's rounded
    //  joints, so their polygons are left mathematically sharp here.  Filled
    //  glyphs get no such help and carry their own fillets - addRoundedPolygon.
    // -----------------------------------------------------------------------

    namespace
    {
        using Pt = juce::Point<float>;

        constexpr float pi    = juce::MathConstants<float>::pi;
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        /** The authoring box.  Fixed, and deliberately not the glyph's bounds. */
        const juce::Rectangle<float> authoringBox { 0.0f, 0.0f, 100.0f, 100.0f };

        void addLine (juce::Path& p, float x1, float y1, float x2, float y2)
        {
            p.startNewSubPath (x1, y1);
            p.lineTo (x2, y2);
        }

        void addDot (juce::Path& p, float cx, float cy, float r)
        {
            p.addEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        }

        /** Open polyline - the workhorse for stroked glyphs. */
        void addPolyline (juce::Path& p, std::initializer_list<Pt> pts)
        {
            const auto* v = pts.begin();
            const auto n  = (int) pts.size();

            if (n < 2)
                return;

            p.startNewSubPath (v[0]);

            for (int i = 1; i < n; ++i)
                p.lineTo (v[i]);
        }

        /** Closed polygon with every corner filleted by a quadratic.

            Filled glyphs cannot borrow the stroke's rounded joints, so a play
            triangle or a star would come out needle-sharp and alias badly at
            16 px.  The fillet is clamped to half a side so that short edges -
            the 12 px eave on the home icon, say - cannot turn inside out.
        */
        void addRoundedPolygon (juce::Path& p, const Pt* v, int n, float radius)
        {
            if (n < 3)
                return;

            auto towards = [radius] (Pt from, Pt to)
            {
                const auto d   = to - from;
                const auto len = d.getDistanceFromOrigin();

                if (len <= 1.0e-4f)
                    return from;

                return from + d * (juce::jmin (radius, len * 0.5f) / len);
            };

            p.startNewSubPath (towards (v[0], v[1]));

            for (int i = 1; i <= n; ++i)
            {
                const auto cur = v[i % n];

                p.lineTo (towards (cur, v[i - 1]));
                p.quadraticTo (cur, towards (cur, v[(i + 1) % n]));
            }

            p.closeSubPath();
        }

        void addRoundedPolygon (juce::Path& p, std::initializer_list<Pt> pts, float radius)
        {
            addRoundedPolygon (p, pts.begin(), (int) pts.size(), radius);
        }

        /** A V-shaped arrow head whose point sits on `tip`, opening backwards
            along `direction`.

            Stroked glyphs get an open V rather than a solid triangle: at 24 px a
            filled head next to a 1.5 px stroke reads as two different weights.
        */
        void addArrowHead (juce::Path& p, Pt tip, Pt direction, float length, float spread)
        {
            const auto d = direction / juce::jmax (1.0e-4f, direction.getDistanceFromOrigin());
            const Pt   n { -d.y, d.x };
            const auto base = tip - d * length;

            p.startNewSubPath (base + n * spread);
            p.lineTo (tip);
            p.lineTo (base - n * spread);
        }

        /** A sine approximated with one cubic per half cycle.

            Two control points at 4/3 of the amplitude put the apex within a
            quarter of a percent of a real sine; a quadratic misses it by 25 %
            and the wave ends up looking like a row of tents.
        */
        void addWavyLine (juce::Path& p, float x0, float x1, float y,
                          float amplitude, int halfCycles)
        {
            const float step = (x1 - x0) / (float) halfCycles;
            const float ctrl = amplitude * 4.0f / 3.0f;

            p.startNewSubPath (x0, y);

            for (int i = 0; i < halfCycles; ++i)
            {
                const float sx  = x0 + step * (float) i;
                const float dir = (i % 2 == 0) ? -1.0f : 1.0f;   // first hump goes up

                p.cubicTo (sx + step * 0.36f, y + ctrl * dir,
                           sx + step * 0.64f, y + ctrl * dir,
                           sx + step,         y);
            }
        }

        /** Mirrored waveform drawn as capsule bars.  `amps` are fractions of
            `halfHeight`; bars are spread evenly across [x0, x1]. */
        void addWaveformBars (juce::Path& p, std::initializer_list<float> amps,
                              float x0, float x1, float centreY,
                              float halfHeight, float barWidth)
        {
            const auto* a = amps.begin();
            const auto  n = (int) amps.size();

            if (n < 1)
                return;

            const float first = x0 + barWidth * 0.5f;
            const float last  = x1 - barWidth * 0.5f;

            for (int i = 0; i < n; ++i)
            {
                const float t  = n == 1 ? 0.5f : (float) i / (float) (n - 1);
                const float cx = first + (last - first) * t;
                const float h  = juce::jmax (barWidth, halfHeight * a[i] * 2.0f);

                p.addRoundedRectangle (cx - barWidth * 0.5f, centreY - h * 0.5f,
                                       barWidth, h, barWidth * 0.5f);
            }
        }

        // -------------------------------------------------------------------
        //  Shapes that appear in more than one glyph
        // -------------------------------------------------------------------

        void addHeart (juce::Path& p)
        {
            p.startNewSubPath (50.0f, 80.0f);
            p.cubicTo (32.0f, 72.0f, 12.0f, 54.0f, 12.0f, 38.0f);
            p.cubicTo (12.0f, 20.0f, 38.0f, 16.0f, 50.0f, 34.0f);
            p.cubicTo (62.0f, 16.0f, 88.0f, 20.0f, 88.0f, 38.0f);
            p.cubicTo (88.0f, 54.0f, 68.0f, 72.0f, 50.0f, 80.0f);
            p.closeSubPath();
        }

        /** Lens plus handle, shared by search and the two zoom glyphs so that
            they stay a family rather than three separate magnifiers. */
        void addMagnifier (juce::Path& p)
        {
            addDot  (p, 42.0f, 42.0f, 27.0f);
            addLine (p, 61.0f, 61.0f, 85.0f, 85.0f);
        }

        void addPadlockBody (juce::Path& p)
        {
            p.addRoundedRectangle (22.0f, 40.0f, 56.0f, 40.0f, 8.0f);

            // The keyhole is small enough that the stroke fills it in solid,
            // which is what it should look like at every size we draw at.
            addDot (p, 50.0f, 60.0f, 4.5f);
        }

        // -------------------------------------------------------------------
        //  The glyphs
        //
        //  No default: label.  A new enumerator must produce a -Wswitch warning
        //  rather than silently drawing nothing.
        // -------------------------------------------------------------------
        juce::Path build (Icon icon)
        {
            juce::Path p;

            switch (icon)
            {
                // -- header -------------------------------------------------

                case Icon::gear:
                {
                    // Eight teeth: outer arc, flank, root arc, flank.  With
                    // startAsNewSubPath = false addCentredArc draws the flank
                    // for us as the lineTo into the next arc's start point.
                    constexpr int   teeth = 8;
                    constexpr float outer = 40.0f;
                    constexpr float root  = 29.0f;

                    const float pitch = twoPi / (float) teeth;
                    const float half  = pitch * 0.29f;   // tooth spans ~58 % of the pitch
                    const float flank = pitch * 0.13f;

                    for (int i = 0; i < teeth; ++i)
                    {
                        const float a = pitch * (float) i;

                        p.addCentredArc (50.0f, 50.0f, outer, outer, 0.0f,
                                         a - half, a + half, i == 0);
                        p.addCentredArc (50.0f, 50.0f, root, root, 0.0f,
                                         a + half + flank, a + pitch - half - flank, false);
                    }

                    p.closeSubPath();
                    addDot (p, 50.0f, 50.0f, 12.5f);
                    break;
                }

                case Icon::waveLogo:
                {
                    // Three half-circles on a shared baseline.  The outer two
                    // overlap the centre one so the silhouette reads as a
                    // single wave rather than as three separate humps.
                    constexpr float base = 66.0f;

                    p.addCentredArc (30.0f, base, 22.0f, 22.0f, 0.0f, -pi * 0.5f, pi * 0.5f, true);
                    p.addCentredArc (50.0f, base, 32.0f, 32.0f, 0.0f, -pi * 0.5f, pi * 0.5f, true);
                    p.addCentredArc (70.0f, base, 22.0f, 22.0f, 0.0f, -pi * 0.5f, pi * 0.5f, true);
                    break;
                }

                case Icon::heart:
                case Icon::heartFilled:
                    addHeart (p);
                    break;

                case Icon::chevronLeft:
                    addPolyline (p, { { 62.0f, 22.0f }, { 36.0f, 50.0f }, { 62.0f, 78.0f } });
                    break;

                case Icon::chevronRight:
                    addPolyline (p, { { 38.0f, 22.0f }, { 64.0f, 50.0f }, { 38.0f, 78.0f } });
                    break;

                case Icon::chevronUp:
                    addPolyline (p, { { 22.0f, 62.0f }, { 50.0f, 36.0f }, { 78.0f, 62.0f } });
                    break;

                case Icon::chevronDown:
                    addPolyline (p, { { 22.0f, 38.0f }, { 50.0f, 64.0f }, { 78.0f, 38.0f } });
                    break;

                case Icon::sourceBrackets:
                    // The preset bar's source mark: two small chevrons back to back.
                    addPolyline (p, { { 42.0f, 30.0f }, { 24.0f, 50.0f }, { 42.0f, 70.0f } });
                    addPolyline (p, { { 58.0f, 30.0f }, { 76.0f, 50.0f }, { 58.0f, 70.0f } });
                    break;

                case Icon::meterBars:
                {
                    // Three bars on a common baseline, uneven like a live meter.
                    constexpr float baseY = 77.0f;
                    const float tops[] = { 43.0f, 23.0f, 35.0f };
                    const float lefts[] = { 18.0f, 43.5f, 69.0f };

                    for (int i = 0; i < 3; ++i)
                        p.addRoundedRectangle (lefts[i], tops[i], 13.0f, baseY - tops[i], 4.5f);

                    break;
                }

                // -- viewport toolbar ---------------------------------------

                case Icon::pencil:
                    // Body drawn as the outline of a pencil lying at 45 degrees,
                    // with the collar line where the timber meets the lead.
                    addPolyline (p, { { 20.0f, 80.0f }, { 38.0f, 76.0f }, { 82.0f, 32.0f },
                                      { 68.0f, 18.0f }, { 24.0f, 62.0f } });
                    p.closeSubPath();
                    addLine (p, 38.0f, 76.0f, 24.0f, 62.0f);
                    break;

                case Icon::waveformMode:
                    // Five fat capsules: the chunkiest of the three waveform
                    // glyphs, because it is the one that gets a violet fill.
                    addWaveformBars (p, { 0.37f, 0.74f, 1.0f, 0.63f, 0.45f },
                                     10.0f, 90.0f, 50.0f, 38.0f, 11.0f);
                    break;

                case Icon::listMode:
                    for (int i = 0; i < 3; ++i)
                    {
                        const float y = 26.0f + 24.0f * (float) i;

                        addDot  (p, 19.0f, y, 3.5f);   // stroke fills this in solid
                        addLine (p, 34.0f, y, 84.0f, y);
                    }
                    break;

                case Icon::markers:
                    // Two transient markers of different heights, each flagged.
                    addLine (p, 22.0f, 14.0f, 22.0f, 86.0f);
                    addPolyline (p, { { 22.0f, 14.0f }, { 50.0f, 23.0f }, { 22.0f, 32.0f } });
                    p.closeSubPath();

                    addLine (p, 56.0f, 30.0f, 56.0f, 86.0f);
                    addPolyline (p, { { 56.0f, 30.0f }, { 78.0f, 37.0f }, { 56.0f, 44.0f } });
                    p.closeSubPath();
                    break;

                case Icon::expand:
                    addPolyline (p, { { 34.0f, 14.0f }, { 14.0f, 14.0f }, { 14.0f, 34.0f } });
                    addPolyline (p, { { 66.0f, 14.0f }, { 86.0f, 14.0f }, { 86.0f, 34.0f } });
                    addPolyline (p, { { 34.0f, 86.0f }, { 14.0f, 86.0f }, { 14.0f, 66.0f } });
                    addPolyline (p, { { 66.0f, 86.0f }, { 86.0f, 86.0f }, { 86.0f, 66.0f } });
                    break;

                // -- transport ----------------------------------------------

                case Icon::play:
                    // Shifted right of geometric centre: a triangle's mass sits
                    // a third of the way from its base, not in the middle.
                    addRoundedPolygon (p, { { 32.0f, 18.0f }, { 84.0f, 50.0f }, { 32.0f, 82.0f } }, 8.0f);
                    break;

                case Icon::pause:
                    p.addRoundedRectangle (27.0f, 20.0f, 16.0f, 60.0f, 5.0f);
                    p.addRoundedRectangle (57.0f, 20.0f, 16.0f, 60.0f, 5.0f);
                    break;

                case Icon::stop:
                    // Smaller than the play triangle: a square of equal span
                    // would carry nearly half as much ink again.
                    p.addRoundedRectangle (28.0f, 28.0f, 44.0f, 44.0f, 9.0f);
                    break;

                case Icon::returnToZero:
                    // Start bar on the left, and an anticlockwise arrow curling
                    // back down towards it.
                    addLine (p, 16.0f, 24.0f, 16.0f, 76.0f);
                    p.addCentredArc (58.0f, 50.0f, 25.0f, 25.0f, 0.0f, pi * 1.25f, -pi * 0.25f, true);
                    addArrowHead (p, { 40.32f, 32.32f }, { -0.707f, 0.707f }, 14.0f, 9.0f);
                    break;

                case Icon::loop:
                    // Stadium with a head at each end of the long sides, so the
                    // direction of travel is legible without an obvious break.
                    p.addRoundedRectangle (18.0f, 30.0f, 64.0f, 40.0f, 20.0f);
                    addPolyline (p, { { 49.0f, 21.0f }, { 61.0f, 30.0f }, { 49.0f, 39.0f } });
                    addPolyline (p, { { 51.0f, 61.0f }, { 39.0f, 70.0f }, { 51.0f, 79.0f } });
                    break;

                case Icon::trim:
                    // Crop marks: two overlapping L shapes.
                    addPolyline (p, { { 30.0f, 12.0f }, { 30.0f, 70.0f }, { 88.0f, 70.0f } });
                    addPolyline (p, { { 12.0f, 30.0f }, { 70.0f, 30.0f }, { 70.0f, 88.0f } });
                    break;

                case Icon::shuffle:
                    p.startNewSubPath (14.0f, 30.0f);
                    p.lineTo (30.0f, 30.0f);
                    p.cubicTo (48.0f, 30.0f, 52.0f, 70.0f, 70.0f, 70.0f);
                    p.lineTo (85.0f, 70.0f);

                    p.startNewSubPath (14.0f, 70.0f);
                    p.lineTo (30.0f, 70.0f);
                    p.cubicTo (48.0f, 70.0f, 52.0f, 30.0f, 70.0f, 30.0f);
                    p.lineTo (85.0f, 30.0f);

                    addArrowHead (p, { 85.0f, 70.0f }, { 1.0f, 0.0f }, 11.0f, 8.0f);
                    addArrowHead (p, { 85.0f, 30.0f }, { 1.0f, 0.0f }, 11.0f, 8.0f);
                    break;

                case Icon::zoomOut:
                    addMagnifier (p);
                    addLine (p, 30.0f, 42.0f, 54.0f, 42.0f);
                    break;

                case Icon::zoomIn:
                    addMagnifier (p);
                    addLine (p, 30.0f, 42.0f, 54.0f, 42.0f);
                    addLine (p, 42.0f, 30.0f, 42.0f, 54.0f);
                    break;

                // -- mutate -------------------------------------------------

                case Icon::sparkle:
                    // Four-pointed star with quadratically concave sides: the
                    // controls sit close to the centre so the waist pulls in.
                    p.startNewSubPath (50.0f, 8.0f);
                    p.quadraticTo (57.0f, 43.0f, 92.0f, 50.0f);
                    p.quadraticTo (57.0f, 57.0f, 50.0f, 92.0f);
                    p.quadraticTo (43.0f, 57.0f, 8.0f, 50.0f);
                    p.quadraticTo (43.0f, 43.0f, 50.0f, 8.0f);
                    p.closeSubPath();
                    break;

                case Icon::cube:
                    // Isometric cube: hexagonal silhouette plus the three edges
                    // that meet at the near corner.
                    addPolyline (p, { { 50.0f, 10.0f }, { 85.0f, 30.0f }, { 85.0f, 70.0f },
                                      { 50.0f, 90.0f }, { 15.0f, 70.0f }, { 15.0f, 30.0f } });
                    p.closeSubPath();

                    addLine (p, 50.0f, 50.0f, 15.0f, 30.0f);
                    addLine (p, 50.0f, 50.0f, 85.0f, 30.0f);
                    addLine (p, 50.0f, 50.0f, 50.0f, 90.0f);
                    break;

                case Icon::print:
                    // Page with a folded corner; the fold is drawn as its own
                    // two edges so it reads as paper rather than as a notch.
                    addPolyline (p, { { 22.0f, 12.0f }, { 62.0f, 12.0f }, { 78.0f, 28.0f },
                                      { 78.0f, 88.0f }, { 22.0f, 88.0f } });
                    p.closeSubPath();

                    addPolyline (p, { { 62.0f, 12.0f }, { 62.0f, 28.0f }, { 78.0f, 28.0f } });
                    addLine (p, 34.0f, 52.0f, 66.0f, 52.0f);
                    addLine (p, 34.0f, 68.0f, 66.0f, 68.0f);
                    break;

                case Icon::makeInstrument:
                    // The button draws its own trailing chevron, so this is the
                    // waveform only.
                    addWaveformBars (p, { 0.34f, 0.70f, 0.95f, 0.55f, 0.88f, 0.62f, 0.38f },
                                     10.0f, 90.0f, 50.0f, 38.0f, 8.0f);
                    break;

                case Icon::arrowRight:
                    addLine (p, 17.0f, 50.0f, 83.0f, 50.0f);
                    addArrowHead (p, { 83.0f, 50.0f }, { 1.0f, 0.0f }, 18.0f, 17.0f);
                    break;

                // -- FX modules ---------------------------------------------

                case Icon::cassette:
                    p.addRoundedRectangle (8.0f, 22.0f, 84.0f, 56.0f, 10.0f);
                    p.addRoundedRectangle (20.0f, 36.0f, 60.0f, 28.0f, 6.0f);
                    addDot (p, 36.0f, 50.0f, 7.5f);
                    addDot (p, 64.0f, 50.0f, 7.5f);
                    break;

                case Icon::dotMatrix:
                    for (int row = 0; row < 4; ++row)
                        for (int col = 0; col < 4; ++col)
                            addDot (p, 20.0f + 20.0f * (float) col,
                                       20.0f + 20.0f * (float) row, 6.0f);
                    break;

                case Icon::filterCurve:
                    // Low-pass response: passband, resonant peak, roll-off.
                    p.startNewSubPath (10.0f, 49.0f);
                    p.lineTo (42.0f, 49.0f);
                    p.cubicTo (52.0f, 49.0f, 54.0f, 21.0f, 62.0f, 21.0f);
                    p.cubicTo (70.0f, 21.0f, 72.0f, 45.0f, 78.0f, 59.0f);
                    p.cubicTo (82.0f, 69.0f, 86.0f, 76.0f, 90.0f, 80.0f);
                    break;

                case Icon::rewind:
                    addRoundedPolygon (p, { { 11.0f, 50.0f }, { 47.0f, 22.0f }, { 47.0f, 78.0f } }, 5.0f);
                    addRoundedPolygon (p, { { 49.0f, 50.0f }, { 85.0f, 22.0f }, { 85.0f, 78.0f } }, 5.0f);
                    break;

                case Icon::concentric:
                    addDot (p, 50.0f, 50.0f, 42.0f);
                    addDot (p, 50.0f, 50.0f, 27.0f);
                    addDot (p, 50.0f, 50.0f, 6.0f);   // stroke fills this in solid
                    break;

                case Icon::link:
                {
                    // Two capsules on a 45 degree axis, offset along it so they
                    // interlock.  Built off-origin and rotated, because a
                    // rotated rounded rectangle is the only honest way to get
                    // the ends right.
                    juce::Path segment;
                    segment.addRoundedRectangle (-26.0f, -12.0f, 52.0f, 24.0f, 12.0f);

                    for (auto centre : { Pt { 38.0f, 62.0f }, Pt { 62.0f, 38.0f } })
                    {
                        juce::Path link (segment);
                        link.applyTransform (juce::AffineTransform::rotation (-pi * 0.25f)
                                                 .translated (centre.x, centre.y));
                        p.addPath (link);
                    }

                    break;
                }

                case Icon::power:
                    // IEC 5009: broken ring, stem through the gap.
                    p.addCentredArc (50.0f, 54.0f, 30.0f, 30.0f, 0.0f, pi * 0.21f, pi * 1.79f, true);
                    addLine (p, 50.0f, 14.0f, 50.0f, 50.0f);
                    break;

                case Icon::plus:
                    addLine (p, 18.0f, 50.0f, 82.0f, 50.0f);
                    addLine (p, 50.0f, 18.0f, 50.0f, 82.0f);
                    break;

                case Icon::minus:
                    addLine (p, 18.0f, 50.0f, 82.0f, 50.0f);
                    break;

                case Icon::cross:
                    addLine (p, 22.0f, 22.0f, 78.0f, 78.0f);
                    addLine (p, 78.0f, 22.0f, 22.0f, 78.0f);
                    break;

                case Icon::collapse:
                    addLine (p, 26.0f, 24.0f, 74.0f, 24.0f);
                    addPolyline (p, { { 24.0f, 50.0f }, { 50.0f, 74.0f }, { 76.0f, 50.0f } });
                    break;

                // -- atmosphere ---------------------------------------------

                case Icon::planet:
                {
                    addDot (p, 50.0f, 50.0f, 26.0f);

                    juce::Path ring;
                    ring.addEllipse (-42.0f, -12.0f, 84.0f, 24.0f);
                    ring.applyTransform (juce::AffineTransform::rotation (-0.32f)
                                             .translated (50.0f, 50.0f));
                    p.addPath (ring);
                    break;
                }

                case Icon::wave3:
                    // Amplitude decays downwards, so the stack is nudged down a
                    // couple of units to put the ink back on centre.
                    addWavyLine (p, 10.0f, 90.0f, 28.0f, 10.0f, 4);
                    addWavyLine (p, 10.0f, 90.0f, 52.0f, 7.0f, 4);
                    addWavyLine (p, 10.0f, 90.0f, 76.0f, 4.5f, 4);
                    break;

                case Icon::triangle:
                    // PATINA's mark. Outlined, per the locked reference - see the
                    // note in isFilled().
                    addPolyline (p, { { 50.0f, 16.0f }, { 88.0f, 82.0f }, { 12.0f, 82.0f } });
                    p.closeSubPath();
                    break;

                // -- source modes -------------------------------------------

                case Icon::srcSynth:
                    addWavyLine (p, 8.0f, 92.0f, 50.0f, 26.0f, 2);
                    break;

                case Icon::srcSample:
                    // Denser and thinner than waveformMode: this is sampled
                    // material, not a display mode.
                    addWaveformBars (p, { 0.32f, 0.68f, 0.95f, 0.52f, 1.0f,
                                          0.74f, 0.86f, 0.46f, 0.30f },
                                     9.0f, 91.0f, 50.0f, 38.0f, 6.0f);
                    break;

                case Icon::srcGrain:
                    for (int i = 0; i < 12; ++i)
                    {
                        const float a = twoPi * (float) i / 12.0f;

                        addDot (p, 50.0f + 34.0f * std::sin (a),
                                   50.0f - 34.0f * std::cos (a), 6.0f);
                    }
                    break;

                case Icon::srcResonator:
                    // Tuning fork: two prongs, a curved yoke, a stem.
                    p.startNewSubPath (28.0f, 12.0f);
                    p.lineTo (28.0f, 44.0f);
                    p.cubicTo (28.0f, 66.0f, 72.0f, 66.0f, 72.0f, 44.0f);
                    p.lineTo (72.0f, 12.0f);

                    addLine (p, 50.0f, 60.0f, 50.0f, 88.0f);
                    break;

                case Icon::srcSpectral:
                    // Spectrum triangle with three bands, each inset so it sits
                    // inside the outline rather than butting against it.
                    addPolyline (p, { { 50.0f, 14.0f }, { 86.0f, 82.0f }, { 14.0f, 82.0f } });
                    p.closeSubPath();

                    addLine (p, 35.0f, 46.0f, 65.0f, 46.0f);
                    addLine (p, 28.0f, 60.0f, 72.0f, 60.0f);
                    addLine (p, 21.0f, 74.0f, 79.0f, 74.0f);
                    break;

                // -- bottom navigation --------------------------------------

                case Icon::navMain:
                    // House silhouette with eaves; drawn solid so that the
                    // active nav item can simply take the violet fill.
                    addRoundedPolygon (p, { { 50.0f, 10.0f }, { 90.0f, 46.0f }, { 78.0f, 46.0f },
                                            { 78.0f, 86.0f }, { 22.0f, 86.0f }, { 22.0f, 46.0f },
                                            { 10.0f, 46.0f } }, 5.0f);
                    break;

                case Icon::navMod:
                    addPolyline (p, { { 8.0f, 72.0f }, { 29.0f, 28.0f }, { 50.0f, 72.0f },
                                      { 71.0f, 28.0f }, { 92.0f, 72.0f } });
                    break;

                case Icon::navFx:
                    // Crossed patch leads with a cap on each of the four ends.
                    addLine (p, 24.0f, 24.0f, 76.0f, 76.0f);
                    addLine (p, 76.0f, 24.0f, 24.0f, 76.0f);

                    addLine (p, 18.3f, 29.7f, 29.7f, 18.3f);
                    addLine (p, 70.3f, 18.3f, 81.7f, 29.7f);
                    addLine (p, 18.3f, 70.3f, 29.7f, 81.7f);
                    addLine (p, 70.3f, 81.7f, 81.7f, 70.3f);
                    break;

                case Icon::navSeq:
                {
                    const float tops[] = { 62.0f, 48.0f, 34.0f, 20.0f };

                    for (int i = 0; i < 4; ++i)
                        p.addRoundedRectangle (10.0f + 21.0f * (float) i, tops[i],
                                               17.0f, 80.0f - tops[i], 4.0f);

                    break;
                }

                case Icon::navMix:
                {
                    const float tracks[] = { 22.0f, 50.0f, 78.0f };
                    const float caps[]   = { 34.0f, 60.0f, 46.0f };

                    for (int i = 0; i < 3; ++i)
                    {
                        addLine (p, tracks[i], 14.0f, tracks[i], 86.0f);
                        p.addRoundedRectangle (tracks[i] - 10.0f, caps[i] - 4.5f,
                                               20.0f, 9.0f, 4.5f);
                    }

                    break;
                }

                // -- misc ---------------------------------------------------

                case Icon::search:
                    addMagnifier (p);
                    break;

                case Icon::folder:
                    addPolyline (p, { { 10.0f, 80.0f }, { 10.0f, 22.0f }, { 38.0f, 22.0f },
                                      { 46.0f, 33.0f }, { 90.0f, 33.0f }, { 90.0f, 80.0f } });
                    p.closeSubPath();
                    break;

                case Icon::star:
                {
                    // Sat 3 units low: an apex-up star reads as sitting high if
                    // its bounding box is centred.
                    std::array<Pt, 10> v;

                    for (int i = 0; i < 10; ++i)
                    {
                        const float a = -pi * 0.5f + pi * 0.2f * (float) i;
                        const float r = (i % 2 == 0) ? 42.0f : 18.0f;

                        v[(size_t) i] = { 50.0f + r * std::cos (a), 53.0f + r * std::sin (a) };
                    }

                    addRoundedPolygon (p, v.data(), (int) v.size(), 4.0f);
                    break;
                }

                case Icon::dice:
                    p.addRoundedRectangle (14.0f, 14.0f, 72.0f, 72.0f, 16.0f);
                    addDot (p, 32.0f, 32.0f, 5.0f);
                    addDot (p, 68.0f, 32.0f, 5.0f);
                    addDot (p, 50.0f, 50.0f, 5.0f);
                    addDot (p, 32.0f, 68.0f, 5.0f);
                    addDot (p, 68.0f, 68.0f, 5.0f);
                    break;

                case Icon::lock:
                    addPadlockBody (p);
                    p.addCentredArc (50.0f, 42.0f, 17.0f, 20.0f, 0.0f, -pi * 0.5f, pi * 0.5f, true);
                    break;

                case Icon::unlock:
                    // Same body, shackle swung open and its free end left in
                    // the air so the difference survives at 16 px.
                    addPadlockBody (p);
                    p.addCentredArc (42.0f, 42.0f, 17.0f, 20.0f, 0.0f, -pi * 0.5f, pi * 0.32f, true);
                    break;

                case Icon::dragHandle:
                    for (int col = 0; col < 2; ++col)
                        for (int row = 0; row < 3; ++row)
                            addDot (p, 38.0f + 24.0f * (float) col,
                                       26.0f + 24.0f * (float) row, 7.0f);
                    break;

                case Icon::count:
                    break;   // not a glyph
            }

            return p;
        }

        /** Every glyph, built once on first use.  Paths are immutable after
            construction, so the table can be shared by every component. */
        const std::array<juce::Path, (size_t) Icon::count>& table()
        {
            static const std::array<juce::Path, (size_t) Icon::count> paths = []
            {
                std::array<juce::Path, (size_t) Icon::count> a;

                for (size_t i = 0; i < a.size(); ++i)
                    a[i] = build ((Icon) i);

                return a;
            }();

            return paths;
        }
    }

    // -----------------------------------------------------------------------
    //  Fill or stroke
    //
    //  Listed exhaustively rather than defaulted, so that adding an enumerator
    //  is a compile warning instead of an icon that quietly comes out hollow.
    // -----------------------------------------------------------------------
    bool isFilled (Icon icon) noexcept
    {
        switch (icon)
        {
            case Icon::heartFilled:
            case Icon::meterBars:
            case Icon::waveformMode:
            case Icon::play:
            case Icon::pause:
            case Icon::stop:
            case Icon::rewind:
            case Icon::sparkle:
            case Icon::makeInstrument:
            case Icon::dotMatrix:
            case Icon::srcSample:
            case Icon::srcGrain:
            case Icon::navMain:
            case Icon::navSeq:
            case Icon::star:
            case Icon::dragHandle:
                return true;

            // triangle is in the atmosphere panel and the locked reference
            // shows it outlined, so it strokes even though a solid triangle
            // would be the more usual choice.
            case Icon::gear:
            case Icon::waveLogo:
            case Icon::heart:
            case Icon::chevronLeft:
            case Icon::chevronRight:
            case Icon::chevronUp:
            case Icon::chevronDown:
            case Icon::sourceBrackets:
            case Icon::pencil:
            case Icon::listMode:
            case Icon::markers:
            case Icon::expand:
            case Icon::returnToZero:
            case Icon::loop:
            case Icon::trim:
            case Icon::shuffle:
            case Icon::zoomOut:
            case Icon::zoomIn:
            case Icon::cube:
            case Icon::print:
            case Icon::arrowRight:
            case Icon::cassette:
            case Icon::filterCurve:
            case Icon::concentric:
            case Icon::link:
            case Icon::power:
            case Icon::plus:
            case Icon::minus:
            case Icon::cross:
            case Icon::collapse:
            case Icon::planet:
            case Icon::wave3:
            case Icon::triangle:
            case Icon::srcSynth:
            case Icon::srcResonator:
            case Icon::srcSpectral:
            case Icon::navMod:
            case Icon::navFx:
            case Icon::navMix:
            case Icon::search:
            case Icon::folder:
            case Icon::dice:
            case Icon::lock:
            case Icon::unlock:
            case Icon::count:
                return false;
        }

        return false;
    }

    const juce::Path& path (Icon icon)
    {
        static const juce::Path empty;

        const auto index = (size_t) icon;

        if (index >= (size_t) Icon::count)
            return empty;

        return table()[index];
    }

    juce::Path pathFor (Icon icon, juce::Rectangle<float> area)
    {
        auto p = path (icon);

        if (p.isEmpty() || area.isEmpty())
            return p;

        // Fit the authoring box, not the glyph's own bounds: scaling by the
        // bounds would blow every sparse glyph up to fill the area and throw
        // away the relative weights the glyphs were drawn with.
        p.applyTransform (juce::RectanglePlacement (juce::RectanglePlacement::centred)
                              .getTransformToFit (authoringBox, area));

        return p;
    }

    void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area,
               juce::Colour colour, float thickness)
    {
        const auto p = pathFor (icon, area);

        if (p.isEmpty())
            return;

        g.setColour (colour);

        if (isFilled (icon))
        {
            g.fillPath (p);
            return;
        }

        // thickness is quoted for a 24 px icon.  The smaller edge is what
        // pathFor scaled by, so it is what the stroke has to follow; below
        // 0.7 px a hairline stops resolving at 75 % UI scale, so it is
        // clamped rather than allowed to disappear.
        const float scale  = juce::jmin (area.getWidth(), area.getHeight()) / 24.0f;
        const float stroke = juce::jmax (0.7f, thickness * scale);

        g.strokePath (p, juce::PathStrokeType (stroke,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}
