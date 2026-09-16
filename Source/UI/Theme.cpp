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
