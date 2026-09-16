#include "PageSurface.h"

namespace nacar::ui
{
    // =======================================================================
    //  ChoicePill
    // =======================================================================
    ChoicePill::ChoicePill (const ParameterRegistry& p, PID id, PillButton::Style style)
        : PillButton (juce::String(), style), params (p), pid (id)
    {
        setTextSize (8.0f, 0.12f);
        setTrailingIcon (icons::Icon::chevronDown);
        setTooltip (juce::String (ParameterRegistry::definition (pid).name) + "\n"
                    + ParameterRegistry::definition (pid).tooltip);

        onClick = [this] { openMenu(); };
        refresh();
    }

    void ChoicePill::refresh()
    {
        const int c = params.choice (pid);

        if (c == shown)
            return;

        shown = c;

        const auto options = ParameterRegistry::choicesOf (pid);
        setButtonText (juce::isPositiveAndBelow (c, options.size()) ? options[c] : juce::String());
    }

    void ChoicePill::openMenu()
    {
        const auto options = ParameterRegistry::choicesOf (pid);
        const int current = params.choice (pid);

        juce::PopupMenu m;
        m.addSectionHeader (ParameterRegistry::definition (pid).name);

        for (int i = 0; i < options.size(); ++i)
            m.addItem (i + 1, options[i], true, i == current);

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                   .withMinimumWidth ((int) getWidth()),
                         [this] (int result)
                         {
                             if (result > 0)
                             {
                                 params.setFromUI (pid, (float) (result - 1));
                                 refresh();
                             }
                         });
    }

    // =======================================================================
    //  PageSurface
    // =======================================================================
    PageSurface::PageSurface (NacarProcessor& p, EditorHost& h,
                              juce::String pageTitle, juce::String pageSubtitle, Material m)
        : processor (p), host (h), params (p.getParameters()), material (m),
          title (std::move (pageTitle)), subtitle (std::move (pageSubtitle))
    {
    }

    PageSurface::~PageSurface()
    {
        stopTimer();
    }

    void PageSurface::visibilityChanged()
    {
        // Pages are constructed once and shown by the chassis; their clock only
        // runs while they are actually on screen.
        if (isVisible() && tickHz > 0)
            startTimerHz (tickHz);
        else
            stopTimer();
    }

    void PageSurface::timerCallback()
    {
        for (auto* pill : choicePills)
            pill->refresh();

        tick();
    }

    // -----------------------------------------------------------------------
    //  Palette
    // -----------------------------------------------------------------------
    juce::Colour PageSurface::ink() const noexcept
    {
        return material == Material::ceramic ? theme::ink : theme::glassInk;
    }

    juce::Colour PageSurface::inkSoft() const noexcept
    {
        return material == Material::ceramic ? theme::inkMuted : theme::glassInkMuted;
    }

    juce::Colour PageSurface::inkFaint() const noexcept
    {
        return material == Material::ceramic ? theme::inkFaint : theme::glassInkFaint;
    }

    juce::Colour PageSurface::wellFill() const noexcept
    {
        // A well is a cut-out, and a cut-out is always optical glass - on
        // ceramic it reads as the viewport does, on glass as the deepest layer.
        return theme::glassDeep;
    }

    NacarKnob::Style PageSurface::knobStyle() const noexcept
    {
        return material == Material::ceramic ? NacarKnob::Style::ceramic
                                             : NacarKnob::Style::dark;
    }

    // -----------------------------------------------------------------------
    //  Chrome
    // -----------------------------------------------------------------------
    void PageSurface::paintChrome (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);

        if (material == Material::ceramic)
        {
            theme::contactShadow (g, b, layout::radiusPanel);
            theme::ceramicSurface (g, b, layout::radiusPanel);
        }
        else
        {
            theme::glassSurface (g, b, layout::radiusPanel, theme::glassDeep);
        }

        // -- title block ----------------------------------------------------
        // Violet accent bar, the same device the optical viewport uses to mark
        // the head of a panel.
        g.setColour (theme::violet);
        g.fillRect (juce::Rectangle<float> (page::pad, page::pad + 4.0f, 2.0f, 34.0f));

        text (g, title, { page::pad + 18.0f, page::pad + 26.0f },
              page::titleSize, page::titleTrack, ink());

        text (g, subtitle, { page::pad + 19.0f, page::pad + 44.0f },
              page::subSize, page::subTrack, inkFaint());

        theme::hairline (g, { page::pad, page::pad + page::titleBlock - 8.0f },
                            { (float) getWidth() - page::pad, page::pad + page::titleBlock - 8.0f },
                         material == Material::ceramic ? theme::ceramicEdge : theme::glassEdge);
    }

    juce::Rectangle<int> PageSurface::contentArea() const
    {
        return getLocalBounds().reduced ((int) page::pad)
                               .withTrimmedTop ((int) page::titleBlock);
    }

    void PageSurface::section (juce::Graphics& g, juce::Rectangle<float> r,
                               juce::StringRef caption, std::optional<icons::Icon> icon) const
    {
        if (r.isEmpty())
            return;

        if (material == Material::ceramic)
        {
            theme::contactShadow (g, r, layout::radiusCard, 2.0f, 7.0f, 0.16f);
            theme::ceramicSurface (g, r, layout::radiusCard, theme::ceramicLight, theme::ceramicMid);
        }
        else
        {
            theme::glassSurface (g, r, layout::radiusCard, theme::glassMid);
        }

        float textX = r.getX() + page::sectionPad;

        if (icon.has_value())
        {
            const auto iconArea = juce::Rectangle<float> (14.0f, 14.0f)
                                      .withCentre ({ textX + 7.0f, r.getY() + 14.0f });
            icons::draw (g, *icon, iconArea, inkSoft(), 1.4f);
            textX += 21.0f;
        }

        text (g, caption, { textX, r.getY() + 18.0f },
              page::captionSize, page::captionTrack, inkSoft());

        theme::hairline (g, { r.getX() + page::sectionPad, r.getY() + page::captionH },
                            { r.getRight() - page::sectionPad, r.getY() + page::captionH },
                         material == Material::ceramic ? theme::ceramicEdge : theme::glassEdge);
    }

    juce::Rectangle<int> PageSurface::inside (juce::Rectangle<int> sectionBounds)
    {
        return sectionBounds.withTrimmedTop ((int) page::captionH + 4)
                            .reduced ((int) page::sectionPad, 0)
                            .withTrimmedBottom ((int) page::sectionPad);
    }

    void PageSurface::groupCaption (juce::Graphics& g, juce::Rectangle<float> row,
                                    juce::StringRef caption) const
    {
        text (g, caption, { row.getX(), row.getBottom() - 3.0f },
              page::noteSize, page::noteTrack, inkFaint());
    }

    void PageSurface::text (juce::Graphics& g, juce::StringRef s, juce::Point<float> baseline,
                            float sizePx, float trackingEm, juce::Colour colour,
                            juce::Justification just, float width) const
    {
        if (material == Material::ceramic)
            ceramicLabel (g, s, baseline, sizePx, trackingEm, colour, just, width);
        else
            glassLabel (g, s, baseline, sizePx, trackingEm, colour, just, width);
    }

    // -----------------------------------------------------------------------
    //  Declarative controls
    // -----------------------------------------------------------------------
    NacarKnob& PageSurface::addKnob (PID pid, juce::String label, bool bipolar)
    {
        auto* k = knobs.add (new NacarKnob (params, pid, knobStyle()));
        k->setLabel (std::move (label), page::knobLabelSize, page::knobLabelTrack);
        k->setBipolar (bipolar);
        addAndMakeVisible (k);
        return *k;
    }

    SegmentedControl& PageSurface::addSegmented (PID pid, juce::StringArray shortLabels)
    {
        auto options = shortLabels.isEmpty() ? ParameterRegistry::choicesOf (pid) : shortLabels;

        auto* s = new SegmentedControl (options,
                                        material == Material::ceramic
                                            ? SegmentedControl::Style::violetFill
                                            : SegmentedControl::Style::darkFill);
        owned.add (s);

        s->setTextSize (7.5f, 0.10f);
        s->bindTo (params, pid);
        s->setTooltip (juce::String (ParameterRegistry::definition (pid).name) + "\n"
                       + ParameterRegistry::definition (pid).tooltip);

        addAndMakeVisible (s);
        return *s;
    }

    ChoicePill& PageSurface::addChoicePill (PID pid)
    {
        auto* p = new ChoicePill (params, pid,
                                  material == Material::ceramic ? PillButton::Style::ceramic
                                                                : PillButton::Style::glass);
        owned.add (p);
        choicePills.add (p);
        addAndMakeVisible (p);
        return *p;
    }

    PowerButton& PageSurface::addPower (PID pid, PowerButton::Tint tint)
    {
        auto* b = new PowerButton (tint);
        owned.add (b);

        b->setClickingTogglesState (true);
        b->setTooltip (juce::String (ParameterRegistry::definition (pid).name) + "\n"
                       + ParameterRegistry::definition (pid).tooltip);
        addAndMakeVisible (b);

        // The attachment owns the two-way binding, including host automation.
        powerAttachments.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (
            processor.getAPVTS(), ParameterRegistry::idOf (pid), *b));

        return *b;
    }

    ToggleSwitch& PageSurface::addToggle (PID pid)
    {
        auto* t = new ToggleSwitch (ToggleSwitch::Size::small);
        owned.add (t);

        t->bindTo (params, pid);
        t->setTooltip (juce::String (ParameterRegistry::definition (pid).name) + "\n"
                       + ParameterRegistry::definition (pid).tooltip);
        addAndMakeVisible (t);
        return *t;
    }

    PreserveLock& PageSurface::addLock (juce::String caption, PID pid)
    {
        auto* l = new PreserveLock (std::move (caption), params, pid);
        owned.add (l);
        addAndMakeVisible (l);
        return *l;
    }

    // -----------------------------------------------------------------------
    //  Layout
    // -----------------------------------------------------------------------
    void PageSurface::knobGrid (juce::Rectangle<int> area, int columns, float knobRadius,
                                int count)
    {
        const int available = knobs.size() - gridCursor;
        const int remaining = count < 0 ? available : juce::jmin (count, available);

        if (remaining <= 0 || columns <= 0 || area.isEmpty())
            return;

        const int rows  = (remaining + columns - 1) / columns;
        const int box   = juce::roundToInt (knobRadius * 2.0f + page::knobBoxPad);
        const int cellW = area.getWidth() / columns;
        const int cellH = juce::jmax (box, area.getHeight() / juce::jmax (1, rows));

        for (int i = 0; i < remaining; ++i)
        {
            const int row = i / columns;
            const int col = i % columns;

            // A short last row is centred, so a seven-knob grid does not leave
            // a hole in the corner.
            const int inThisRow = juce::jmin (columns, remaining - row * columns);
            const int rowX = area.getX() + (area.getWidth() - inThisRow * cellW) / 2;

            const juce::Rectangle<int> cell (rowX + col * cellW,
                                             area.getY() + row * cellH, cellW, cellH);

            placeKnob (*knobs[gridCursor + i], cell, knobRadius);
        }

        gridCursor += remaining;
    }

    void PageSurface::placeKnob (NacarKnob& k, juce::Rectangle<int> cell, float knobRadius)
    {
        const int box = juce::roundToInt (knobRadius * 2.0f + page::knobBoxPad);

        k.setBounds (juce::Rectangle<int> (box, box).withCentre (cell.getCentre()));

        // Small caps need a shorter throw or they feel glued down.
        k.setDragSensitivity (juce::jmax (170.0f, knobRadius * 9.0f));
    }

    // -----------------------------------------------------------------------
    //  Shared drawing helpers
    // -----------------------------------------------------------------------
    void previewWell (juce::Graphics& g, juce::Rectangle<float> r)
    {
        theme::glassSurface (g, r, 5.0f, theme::glassDeep);
    }

    void violetTrace (juce::Graphics& g, const juce::Path& fill, const juce::Path& line)
    {
        g.setColour (theme::violet.withAlpha (0.12f));
        g.fillPath (fill);

        g.setColour (theme::violet);
        g.strokePath (line, juce::PathStrokeType (1.0f));
    }

    float stableRandom (int index) noexcept
    {
        // A hash, not a generator: the same index always returns the same
        // number, so the random LFO previews are stable across repaints.
        auto h = (juce::uint32) index * 2654435761u + 1013904223u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;

        return (float) (h % 100000u) / 100000.0f;
    }
}
