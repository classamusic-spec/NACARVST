#include "Theme.h"

namespace nacar::theme
{
    // -----------------------------------------------------------------------
    //  Fonts
    //
    //  NACAR ships no bundled typeface in V1, so the display face is resolved
    //  from a preference list of grotesques that exist on macOS and Windows,
    //  falling back to the platform sans.  Every size in Layout.h assumes a
    //  grotesque with roughly Helvetica's metrics.
    // -----------------------------------------------------------------------
    static juce::String resolveFamily()
    {
        static const juce::String family = []
        {
            const juce::StringArray preferred {
                "Helvetica Neue", "Inter", "Avenir Next", "Segoe UI Variable Display",
                "Segoe UI", "Roboto", "DejaVu Sans", "Liberation Sans", "Arial"
            };

            const auto available = juce::Font::findAllTypefaceNames();
            for (const auto& name : preferred)
                if (available.contains (name))
                    return name;

            return juce::Font::getDefaultSansSerifFontName();
        }();

        return family;
    }

    static juce::Font make (float height, bool bold)
    {
        return juce::Font (juce::FontOptions (resolveFamily(), height,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font display (float height) { return make (height, false); }
    juce::Font medium  (float height) { return make (height, false); }
    juce::Font label   (float height) { return make (height, true);  }

    juce::Font mono (float height)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              height, juce::Font::plain));
    }

    // -----------------------------------------------------------------------
    //  Tracked text
    // -----------------------------------------------------------------------
    float trackedWidth (juce::StringRef text, const juce::Font& f, float trackingEm)
    {
        const juce::String s (text);
        if (s.isEmpty())
            return 0.0f;

        const float extra = trackingEm * f.getHeight();
        float w = 0.0f;

        for (auto c : s)
            w += f.getStringWidthFloat (juce::String::charToString (c)) + extra;

        return juce::jmax (0.0f, w - extra);   // no trailing track after the last glyph
    }

    void drawTracked (juce::Graphics& g, juce::StringRef text, juce::Rectangle<float> area,
                      const juce::Font& f, float trackingEm, juce::Justification just)
    {
        const juce::String s (text);
        if (s.isEmpty())
            return;

        g.setFont (f);

        const float extra = trackingEm * f.getHeight();
        const float total = trackedWidth (s, f, trackingEm);

        float x = area.getX();
        if (just.testFlags (juce::Justification::horizontallyCentred))
            x = area.getCentreX() - total * 0.5f;
        else if (just.testFlags (juce::Justification::right))
            x = area.getRight() - total;

        // Vertical placement: JUCE draws single lines from the top of the em box,
        // so centre on the ascent rather than on the full line height.
        float y = area.getY();
        if (just.testFlags (juce::Justification::verticallyCentred))
            y = area.getCentreY() - f.getHeight() * 0.5f;
        else if (just.testFlags (juce::Justification::bottom))
            y = area.getBottom() - f.getHeight();

        for (auto c : s)
        {
            const juce::String glyph = juce::String::charToString (c);
            const float w = f.getStringWidthFloat (glyph);

            g.drawText (glyph, juce::Rectangle<float> (x, y, w + 1.0f, f.getHeight()),
                        juce::Justification::centredLeft, false);

            x += w + extra;
        }
    }

    // -----------------------------------------------------------------------
    //  Shading
    // -----------------------------------------------------------------------
    void contactShadow (juce::Graphics& g, juce::Rectangle<float> bounds, float corner,
                        float offsetY, float blur, float alpha)
    {
        // Layered translucent rounded rects: cheap, stable at every scale factor,
        // and does not need an offscreen image the way DropShadow does.
        const int steps = juce::jlimit (3, 10, (int) blur);

        for (int i = steps; i >= 1; --i)
        {
            const float t     = (float) i / (float) steps;
            const float grow  = blur * t;
            const float a     = alpha * (1.0f - t) * 0.55f;

            g.setColour (juce::Colours::black.withAlpha (a));
            g.fillRoundedRectangle (bounds.expanded (grow * 0.5f)
                                          .translated (0.0f, offsetY * t),
                                    corner + grow * 0.5f);
        }
    }

    void ceramicSurface (juce::Graphics& g, juce::Rectangle<float> bounds, float corner,
                         juce::Colour top, juce::Colour bottom)
    {
        if (bounds.isEmpty())
            return;

        g.setGradientFill (juce::ColourGradient (top,    bounds.getCentreX(), bounds.getY(),
                                                 bottom, bounds.getCentreX(), bounds.getBottom(),
                                                 false));
        g.fillRoundedRectangle (bounds, corner);

        // Faint sheen from the upper-left, as if lit by a single soft source.
        {
            juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.20f),
                                        bounds.getX() + bounds.getWidth() * 0.18f,
                                        bounds.getY() + bounds.getHeight() * 0.10f,
                                        juce::Colours::transparentWhite,
                                        bounds.getRight(), bounds.getBottom(), true);
            g.setGradientFill (sheen);
            g.fillRoundedRectangle (bounds, corner);
        }

        // Specular top edge and bevelled bottom edge.
        //
        // Both are strokes of the same rounded rectangle, each clipped to the
        // half it belongs to.  Path::addRoundedRectangle's per-corner flags
        // only choose which corners are curved - the path is still closed - so
        // stroking one of those directly would draw a square-cornered outline
        // all the way round, which is invisible on ceramic and glaringly
        // obvious on glass.
        {
            juce::Path outline;
            outline.addRoundedRectangle (bounds.reduced (0.5f), corner);

            const juce::PathStrokeType stroke (1.0f);

            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (bounds.withHeight (bounds.getHeight() * 0.55f)
                                          .getSmallestIntegerContainer());
                g.setColour (juce::Colours::white.withAlpha (0.60f));
                g.strokePath (outline, stroke);
            }

            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (bounds.withTop (bounds.getCentreY())
                                          .getSmallestIntegerContainer());
                g.setColour (juce::Colours::black.withAlpha (0.22f));
                g.strokePath (outline, stroke);
            }
        }
    }

    void glassSurface (juce::Graphics& g, juce::Rectangle<float> bounds, float corner,
                       juce::Colour fill)
    {
        if (bounds.isEmpty())
            return;

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, corner);

        // Inner shadow across the top: sells the cut-out.
        {
            const float depth = juce::jmin (16.0f, bounds.getHeight() * 0.35f);
            juce::Graphics::ScopedSaveState ss (g);

            juce::Path clip;
            clip.addRoundedRectangle (bounds, corner);
            g.reduceClipRegion (clip);

            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f),
                                                     bounds.getCentreX(), bounds.getY(),
                                                     juce::Colours::transparentBlack,
                                                     bounds.getCentreX(), bounds.getY() + depth,
                                                     false));
            g.fillRect (bounds.withHeight (depth));
        }

        g.setColour (glassEdge);
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
    }

    void hairline (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour c)
    {
        g.setColour (c);
        g.drawLine (a.x, a.y, b.x, b.y, 1.0f);
    }

    void glow (juce::Graphics& g, juce::Point<float> centre, float radius,
               juce::Colour c, float alpha)
    {
        juce::ColourGradient grad (c.withAlpha (alpha), centre.x, centre.y,
                                   c.withAlpha (0.0f), centre.x + radius, centre.y, true);
        grad.addColour (0.45, c.withAlpha (alpha * 0.45f));

        g.setGradientFill (grad);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    }

    void machinedCap (juce::Graphics& g, juce::Point<float> centre, float radius, bool darkCap)
    {
        const auto body = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

        const auto top    = darkCap ? juce::Colour (0xff2a2a30) : ceramicLight;
        const auto bottom = darkCap ? juce::Colour (0xff141418) : ceramicDark;

        g.setGradientFill (juce::ColourGradient (top,    centre.x, body.getY(),
                                                 bottom, centre.x, body.getBottom(), false));
        g.fillEllipse (body);

        // Concentric machining rings.
        const int rings = juce::jlimit (3, 9, (int) (radius / 7.0f));
        for (int i = 1; i <= rings; ++i)
        {
            const float t = (float) i / (float) (rings + 1);
            const float rr = radius * (1.0f - t * 0.82f);

            g.setColour ((darkCap ? juce::Colours::white : juce::Colours::white)
                             .withAlpha (0.045f * (1.0f - t)));
            g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (centre), 1.0f);
        }

        // Specular arc, upper-left.
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius - 1.5f, radius - 1.5f, 0.0f,
                               juce::MathConstants<float>::pi * 1.15f,
                               juce::MathConstants<float>::pi * 1.85f, true);
            g.setColour (juce::Colours::white.withAlpha (darkCap ? 0.16f : 0.70f));
            g.strokePath (arc, juce::PathStrokeType (1.4f));
        }

        // Rim.
        g.setColour (darkCap ? juce::Colour (0xff000000).withAlpha (0.8f) : ceramicEdge);
        g.drawEllipse (body.reduced (0.5f), 1.0f);
    }

    // =======================================================================
    //  DIMENSIONAL VOCABULARY
    //
    //  Everything below lights from lightX / lightY and nowhere else.
    // =======================================================================
    namespace
    {
        struct Depth
        {
            float shadowOffset;   ///< how far the contact shadow falls
            float shadowBlur;
            float shadowAlpha;
            float specular;       ///< alpha of the lit edge
            float bevel;          ///< alpha of the shaded edge
            float innerLift;      ///< alpha of the inset highlight under the top edge
        };

        Depth depthFor (Elevation e) noexcept
        {
            switch (e)
            {
                case Elevation::flush:    return { 0.0f, 0.0f,  0.00f, 0.34f, 0.26f, 0.00f };
                case Elevation::resting:  return { 1.5f, 5.0f,  0.20f, 0.62f, 0.24f, 0.34f };
                case Elevation::raised:   return { 3.0f, 10.0f, 0.26f, 0.78f, 0.30f, 0.46f };
                case Elevation::floating: return { 6.0f, 20.0f, 0.34f, 0.70f, 0.34f, 0.40f };
            }

            return { 1.5f, 5.0f, 0.20f, 0.62f, 0.24f, 0.34f };
        }

        /** Strokes one rounded-rectangle outline, clipped to a horizontal band.

            JUCE's per-corner addRoundedRectangle still closes the path, so
            stroking "just the top" draws a square-cornered box all the way
            round.  Clipping is the only way to light one edge of a rounded
            shape without lighting all four. */
        void strokeBand (juce::Graphics& g, juce::Rectangle<float> bounds, float corner,
                         float fromT, float toT, juce::Colour c, float thickness,
                         float inset = 0.5f)
        {
            if (c.getFloatAlpha() <= 0.002f || bounds.getHeight() <= 1.0f)
                return;

            juce::Path outline;
            outline.addRoundedRectangle (bounds.reduced (inset),
                                         juce::jmax (0.5f, corner - inset));

            const auto band = bounds.withTop (bounds.getY() + bounds.getHeight() * fromT)
                                    .withBottom (bounds.getY() + bounds.getHeight() * toT);

            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (band.getSmallestIntegerContainer());
            g.setColour (c);
            g.strokePath (outline, juce::PathStrokeType (thickness));
        }

        /** The point the light comes from, for a shape of this size. */
        juce::Point<float> lightSourceFor (juce::Rectangle<float> b, float reach = 0.9f) noexcept
        {
            return { b.getCentreX() + lightX * b.getWidth()  * reach,
                     b.getCentreY() + lightY * b.getHeight() * reach };
        }
    }

    void raisedCeramic (juce::Graphics& g, juce::Rectangle<float> bounds, float corner,
                        Elevation elevation, float press, float hover,
                        juce::Colour top, juce::Colour bottom)
    {
        if (bounds.isEmpty())
            return;

        press = juce::jlimit (0.0f, 1.0f, press);
        hover = juce::jlimit (0.0f, 1.0f, hover);

        const auto d = depthFor (elevation);

        // Pressing sinks the object.  The travel is small - a pixel and a half
        // at most - because a control that moves further than its own bevel
        // stops looking like a key and starts looking like a bug.
        const float travel = d.shadowOffset * 0.5f * press;
        const auto  body   = bounds.translated (0.0f, travel);

        // 1. Contact shadow.  It tightens as the object sinks, because the
        //    gap casting it is smaller - this is most of what sells a press.
        if (d.shadowAlpha > 0.0f)
            contactShadow (g, body, corner,
                           d.shadowOffset * (1.0f - press * 0.7f),
                           d.shadowBlur   * (1.0f - press * 0.5f),
                           d.shadowAlpha  * (1.0f - press * 0.45f));

        // 2. The body.  Two gradients: a vertical one for the material's own
        //    shading, and a radial one from the light for the sheen.  Hover
        //    lifts the whole thing rather than tinting it, so a hovered control
        //    reads as "closer" instead of "a different colour".
        {
            const float lift = hover * 0.045f - press * 0.05f;

            const auto t = top   .brighter (lift);
            const auto b = bottom.brighter (lift * 0.5f);

            g.setGradientFill (juce::ColourGradient (press > 0.5f ? b : t,
                                                     body.getCentreX(), body.getY(),
                                                     press > 0.5f ? t : b,
                                                     body.getCentreX(), body.getBottom(), false));
            g.fillRoundedRectangle (body, corner);
        }

        {
            const auto from = lightSourceFor (body, 0.55f);

            juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.26f * (1.0f - press * 0.6f)),
                                        from.x, from.y,
                                        juce::Colours::transparentWhite,
                                        body.getRight(), body.getBottom(), true);
            g.setGradientFill (sheen);
            g.fillRoundedRectangle (body, corner);
        }

        // 3. The edges.  Lit along the top, shaded along the bottom - and the
        //    two swap when the object is pressed, which is what a key does when
        //    its face tilts away from the light.
        const float spec  = d.specular * (1.0f - press) + d.bevel * press;
        const float shade = d.bevel    * (1.0f - press) + d.specular * press * 0.5f;

        strokeBand (g, body, corner, 0.0f, 0.55f,
                    (press > 0.5f ? juce::Colours::black.withAlpha (shade)
                                  : juce::Colours::white.withAlpha (spec)), 1.0f);

        strokeBand (g, body, corner, 0.45f, 1.0f,
                    (press > 0.5f ? juce::Colours::white.withAlpha (spec * 0.7f)
                                  : juce::Colours::black.withAlpha (shade)), 1.0f);

        // 4. The inset highlight one pixel inside the top edge.  This is the
        //    material's own thickness, and it is the single detail that most
        //    distinguishes a moulded object from a rectangle with a border.
        if (d.innerLift > 0.0f && press < 0.5f && bounds.getHeight() > 6.0f)
            strokeBand (g, body.reduced (1.0f), juce::jmax (0.5f, corner - 1.0f),
                        0.0f, 0.4f,
                        juce::Colours::white.withAlpha (d.innerLift * (1.0f - press)),
                        1.0f, 0.5f);

        // 5. The outer hairline, so the object has an edge against the chassis
        //    even where its own shading happens to match it.
        g.setColour (ceramicEdge.withAlpha (0.55f));
        g.drawRoundedRectangle (body.reduced (0.5f), corner, 1.0f);
    }

    void raisedGlass (juce::Graphics& g, juce::Rectangle<float> bounds, float corner,
                      Elevation elevation, float press, float hover, juce::Colour fill)
    {
        if (bounds.isEmpty())
            return;

        press = juce::jlimit (0.0f, 1.0f, press);
        hover = juce::jlimit (0.0f, 1.0f, hover);

        const auto d = depthFor (elevation);

        const float travel = d.shadowOffset * 0.4f * press;
        const auto  body   = bounds.translated (0.0f, travel);

        // Glass on glass casts a shadow the way glass on ceramic does not:
        // the drawer and the cards genuinely sit above the panel behind them.
        if (d.shadowAlpha > 0.0f)
            contactShadow (g, body, corner,
                           d.shadowOffset * (1.0f - press * 0.7f),
                           d.shadowBlur   * (1.0f - press * 0.5f),
                           d.shadowAlpha  * 1.4f * (1.0f - press * 0.45f));

        // A dark control on a dark ground cannot be read by its face, so the
        // face is nearly flat and all the information is in the edges.
        {
            const auto t = fill.brighter (0.10f + hover * 0.10f - press * 0.06f);
            const auto b = fill.darker   (0.22f - hover * 0.05f);

            g.setGradientFill (juce::ColourGradient (press > 0.5f ? b : t,
                                                     body.getCentreX(), body.getY(),
                                                     press > 0.5f ? t : b,
                                                     body.getCentreX(), body.getBottom(), false));
            g.fillRoundedRectangle (body, corner);
        }

        strokeBand (g, body, corner, 0.0f, 0.5f,
                    juce::Colours::white.withAlpha ((0.16f + hover * 0.10f) * (1.0f - press * 0.7f)),
                    1.0f);

        strokeBand (g, body, corner, 0.5f, 1.0f,
                    juce::Colours::black.withAlpha (0.45f), 1.0f);

        g.setColour (glassEdge.withAlpha (0.9f + hover * 0.1f));
        g.drawRoundedRectangle (body.reduced (0.5f), corner, 1.0f);
    }

    void accentSurface (juce::Graphics& g, juce::Rectangle<float> bounds, float corner,
                        juce::Colour accent, float press, float hover)
    {
        if (bounds.isEmpty())
            return;

        press = juce::jlimit (0.0f, 1.0f, press);
        hover = juce::jlimit (0.0f, 1.0f, hover);

        const auto body = bounds.translated (0.0f, 1.2f * press);

        // The halo the accent throws onto the chassis.  Small, and it grows on
        // hover: this is the only element in the interface that emits light,
        // so it has to be the only one that behaves like it.
        outerGlow (g, body, corner, accent, (0.28f + hover * 0.22f) * (1.0f - press * 0.4f), 7.0f);

        contactShadow (g, body, corner, 2.0f * (1.0f - press * 0.6f), 8.0f,
                       0.20f * (1.0f - press * 0.4f));

        {
            const auto t = accent.brighter (0.30f + hover * 0.10f - press * 0.10f);
            const auto b = accent.brighter (0.08f);

            g.setGradientFill (juce::ColourGradient (press > 0.5f ? b : t,
                                                     body.getCentreX(), body.getY(),
                                                     press > 0.5f ? t : b,
                                                     body.getCentreX(), body.getBottom(), false));
            g.fillRoundedRectangle (body, corner);
        }

        // Lit from within, not merely filled: a soft core brighter than the
        // edges, placed at the light rather than at the centre.
        {
            const auto from = lightSourceFor (body, 0.35f);

            juce::ColourGradient core (accent.brighter (0.55f).withAlpha (0.55f * (1.0f - press * 0.5f)),
                                       from.x, from.y,
                                       juce::Colours::transparentWhite,
                                       body.getRight(), body.getBottom(), true);
            g.setGradientFill (core);
            g.fillRoundedRectangle (body, corner);
        }

        strokeBand (g, body, corner, 0.0f, 0.55f,
                    juce::Colours::white.withAlpha ((0.62f + hover * 0.14f) * (1.0f - press * 0.7f)),
                    1.0f);

        strokeBand (g, body, corner, 0.45f, 1.0f,
                    accent.darker (0.45f).withAlpha (0.55f), 1.0f);

        g.setColour (accent.darker (0.25f).withAlpha (0.75f));
        g.drawRoundedRectangle (body.reduced (0.5f), corner, 1.0f);
    }

    void recessedWell (juce::Graphics& g, juce::Rectangle<float> bounds, float corner,
                       juce::Colour fill, float depth)
    {
        if (bounds.isEmpty())
            return;

        depth = juce::jlimit (0.0f, 2.0f, depth);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, corner);

        // The inner shadow: successive inset outlines, strongest at the top,
        // because the light comes from above and a well's near wall is the one
        // in shadow.
        const int steps = 4;

        for (int i = 0; i < steps; ++i)
        {
            const float inset = 0.5f + (float) i;
            const float a     = 0.30f * depth * (1.0f - (float) i / (float) steps);

            strokeBand (g, bounds, corner, 0.0f, 0.6f,
                        juce::Colours::black.withAlpha (a), 1.0f, inset);
        }

        // The catch of light on the far wall, which is what tells the eye the
        // shape is below the surface rather than a dark patch painted on it.
        strokeBand (g, bounds, corner, 0.6f, 1.0f,
                    juce::Colours::white.withAlpha (0.14f * depth), 1.0f);
    }

    void innerGlow (juce::Graphics& g, juce::Rectangle<float> bounds, float corner,
                    juce::Colour c, float alpha, float spread)
    {
        if (bounds.isEmpty() || alpha <= 0.002f)
            return;

        const int steps = juce::jlimit (2, 8, (int) spread);

        for (int i = 0; i < steps; ++i)
        {
            const float t     = (float) i / (float) steps;
            const float inset = 0.5f + spread * t;

            if (bounds.getWidth() <= inset * 2.0f || bounds.getHeight() <= inset * 2.0f)
                break;

            g.setColour (c.withAlpha (alpha * (1.0f - t) * 0.6f));
            g.drawRoundedRectangle (bounds.reduced (inset),
                                    juce::jmax (0.5f, corner - inset), 1.4f);
        }
    }

    void outerGlow (juce::Graphics& g, juce::Rectangle<float> bounds, float corner,
                    juce::Colour c, float alpha, float spread)
    {
        if (bounds.isEmpty() || alpha <= 0.002f)
            return;

        const int steps = juce::jlimit (2, 10, (int) spread);

        for (int i = steps; i >= 1; --i)
        {
            const float t    = (float) i / (float) steps;
            const float grow = spread * t;

            g.setColour (c.withAlpha (alpha * (1.0f - t) * 0.45f));
            g.fillRoundedRectangle (bounds.expanded (grow), corner + grow);
        }
    }

    void capSeat (juce::Graphics& g, juce::Point<float> centre, float capRadius,
                  float seatWidth)
    {
        const float outer = capRadius + seatWidth;
        const auto  ring  = juce::Rectangle<float> (outer * 2.0f, outer * 2.0f).withCentre (centre);

        // The seat is a well, so it is darkest where the light cannot reach -
        // the upper-left, the side nearest the source.  That inversion is what
        // makes a recess read as a recess rather than as a smaller disc.
        juce::ColourGradient seat (ceramicDeep.withAlpha (0.55f),
                                   centre.x - lightX * outer * 0.8f,
                                   centre.y - lightY * outer * 0.8f,
                                   ceramicLight.withAlpha (0.0f),
                                   centre.x + lightX * outer * 0.9f,
                                   centre.y + lightY * outer * 0.9f, false);

        g.setGradientFill (seat);
        g.fillEllipse (ring);

        g.setColour (ceramicEdge.withAlpha (0.45f));
        g.drawEllipse (ring.reduced (0.5f), 1.0f);
    }

    void domeCap (juce::Graphics& g, juce::Point<float> centre, float radius,
                  bool darkCap, float hover)
    {
        if (radius <= 1.0f)
            return;

        hover = juce::jlimit (0.0f, 1.0f, hover);

        const auto body = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

        const auto base   = darkCap ? juce::Colour (0xff202026) : ceramicLight;
        const auto shadow = darkCap ? juce::Colour (0xff0e0e12) : ceramicDark.darker (0.10f);

        // 1. The body, lit from a POINT rather than by a linear ramp.  The
        //    bright spot sits inside the disc at about a third of the radius
        //    towards the light; everything falls away from there.  That is the
        //    whole difference between a disc and a dome.
        {
            const juce::Point<float> hot { centre.x + lightX * radius * 0.42f,
                                           centre.y + lightY * radius * 0.42f };

            juce::ColourGradient dome (base.brighter (0.22f + hover * 0.06f), hot.x, hot.y,
                                       shadow, centre.x - lightX * radius * 1.05f,
                                               centre.y - lightY * radius * 1.05f, true);

            // A mid stop, so the falloff is not linear: real curvature turns
            // away from the light fastest near the terminator.
            dome.addColour (0.55, base.darker (darkCap ? 0.10f : 0.03f));

            g.setGradientFill (dome);
            g.fillEllipse (body);
        }

        // 2. Machined rings.  Concentric, faint, and denser towards the rim -
        //    a turned surface is cut from the outside in.
        {
            const int rings = juce::jlimit (3, 10, (int) (radius / 6.0f));

            for (int i = 1; i <= rings; ++i)
            {
                const float t  = (float) i / (float) (rings + 1);
                const float rr = radius * (1.0f - t * 0.86f);

                g.setColour ((darkCap ? juce::Colours::white : juce::Colours::white)
                                 .withAlpha (0.05f * (1.0f - t * 0.7f)));
                g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (centre),
                               0.8f);
            }
        }

        // 3. The rim.  Lit on the side facing the light, lost on the far side,
        //    and drawn as two arcs rather than one circle so that the two ends
        //    genuinely differ.
        {
            const float rimR  = radius - 0.9f;
            const float toward = std::atan2 (-lightX, lightY);   // JUCE arcs: 0 is up, cw

            juce::Path lit;
            lit.addCentredArc (centre.x, centre.y, rimR, rimR, 0.0f,
                               toward - 1.35f, toward + 1.35f, true);

            g.setColour (juce::Colours::white.withAlpha (darkCap ? 0.28f : 0.85f));
            g.strokePath (lit, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

            juce::Path dark;
            dark.addCentredArc (centre.x, centre.y, rimR, rimR, 0.0f,
                                toward + 1.55f, toward + 4.73f, true);

            g.setColour (juce::Colours::black.withAlpha (darkCap ? 0.55f : 0.26f));
            g.strokePath (dark, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }

        // 4. A tight specular, the size of the light itself.  Small and bright
        //    beats large and soft: it is what makes the surface read as
        //    polished rather than matte.
        {
            const float sr = radius * 0.30f;
            const juce::Point<float> at { centre.x + lightX * radius * 0.52f,
                                          centre.y + lightY * radius * 0.52f };

            juce::ColourGradient spec (juce::Colours::white.withAlpha (darkCap ? 0.16f : 0.42f),
                                       at.x, at.y,
                                       juce::Colours::transparentWhite,
                                       at.x + sr, at.y + sr, true);
            g.setGradientFill (spec);
            g.fillEllipse (juce::Rectangle<float> (sr * 2.0f, sr * 2.0f).withCentre (at));
        }

        // 5. The outermost edge against whatever is behind it.
        g.setColour (darkCap ? juce::Colours::black.withAlpha (0.85f)
                             : ceramicEdge.withAlpha (0.85f));
        g.drawEllipse (body.reduced (0.5f), 1.0f);
    }

    // -----------------------------------------------------------------------
    //  LookAndFeel
    // -----------------------------------------------------------------------
    NacarLookAndFeel::NacarLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId,          glassMid);
        setColour (juce::PopupMenu::textColourId,                glassInk);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, violet.withAlpha (0.22f));
        setColour (juce::PopupMenu::highlightedTextColourId,     glassInk);
        setColour (juce::PopupMenu::headerTextColourId,          glassInkMuted);

        setColour (juce::TooltipWindow::backgroundColourId,      glassDeep);
        setColour (juce::TooltipWindow::textColourId,            glassInk);
        setColour (juce::TooltipWindow::outlineColourId,         glassEdge);

        setColour (juce::TextEditor::backgroundColourId,         glassDeep);
        setColour (juce::TextEditor::textColourId,               glassInk);
        setColour (juce::TextEditor::highlightColourId,          violet.withAlpha (0.35f));
        setColour (juce::TextEditor::outlineColourId,            glassEdge);
        setColour (juce::TextEditor::focusedOutlineColourId,     violet);
        setColour (juce::CaretComponent::caretColourId,          violet);

        setColour (juce::ScrollBar::thumbColourId,               ceramicEdge);
    }

    juce::Font NacarLookAndFeel::getPopupMenuFont()
    {
        return medium (14.0f);
    }

    void NacarLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        const juce::Rectangle<float> b (0.0f, 0.0f, (float) width, (float) height);
        glassSurface (g, b.reduced (1.0f), 8.0f, glassMid);
    }

    juce::Rectangle<int> NacarLookAndFeel::getTooltipBounds (const juce::String& tip,
                                                            juce::Point<int> pos,
                                                            juce::Rectangle<int> parentArea)
    {
        const auto lines = juce::StringArray::fromLines (tip);
        const auto f = medium (12.0f);

        int w = 0;
        for (const auto& line : lines)
            w = juce::jmax (w, (int) std::ceil (f.getStringWidthFloat (line)));

        w = juce::jlimit (90, 320, w + 18);
        const int h = (int) (lines.size() * (f.getHeight() + 3.0f)) + 14;

        return juce::Rectangle<int> (pos.x > parentArea.getCentreX() ? pos.x - (w + 12) : pos.x + 14,
                                     pos.y > parentArea.getCentreY() ? pos.y - (h + 6)  : pos.y + 18,
                                     w, h).constrainedWithin (parentArea);
    }

    void NacarLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text,
                                        int width, int height)
    {
        const juce::Rectangle<float> b (0.0f, 0.0f, (float) width, (float) height);

        contactShadow (g, b.reduced (1.0f), 7.0f, 2.0f, 7.0f, 0.35f);
        glassSurface (g, b.reduced (1.0f), 7.0f, glassDeep);

        const auto lines = juce::StringArray::fromLines (text);
        const auto f = medium (12.0f);
        g.setFont (f);

        float y = 7.0f;
        for (int i = 0; i < lines.size(); ++i)
        {
            g.setColour (i == 0 ? glassInk : glassInkMuted);
            g.drawText (lines[i], juce::Rectangle<float> (9.0f, y, b.getWidth() - 18.0f, f.getHeight()),
                        juce::Justification::centredLeft, true);
            y += f.getHeight() + 3.0f;
        }
    }
}
