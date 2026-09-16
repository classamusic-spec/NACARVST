#include "SeqPage.h"

/*
    ======================================================================
      SEQ  -  what is real and what is not
    ======================================================================

    REAL.  Four lanes of sixteen steps.  Each step has a value and a gate;
    each lane has a length, an enable and a target parameter chosen from the
    parameter table itself.  All of it is written into the session tree under
    a SEQUENCER child, so it saves with the project, travels with a preset
    and survives a reload.  The editing is real editing: drag to paint
    values, shift-click to toggle gates, and the well shows exactly what is
    stored.

    NOT REAL.  Nothing reads that data.  There is no trigger engine: no
    lane is advanced against the host clock, and no step is ever applied to
    its target parameter.  The PREVIEW playhead on this page is an editing
    aid driven by the interface clock, and the page says so on its face.

    The master specification is deliberate about this (section 129): the
    sequencer is a V1 foundation and must not be allowed to delay core sound
    quality.  So the data model and the editor are finished and the engine is
    not attempted.  When it is written it will live next to the other
    modulation sources in Source/Audio/Modulation/ and will read exactly the
    tree this page writes.
    ======================================================================
*/

namespace nacar::ui
{
    namespace
    {
        // Page-local metrics.  Everything here describes the inside of a lane
        // well, which is a surface Layout.h knows nothing about.
        constexpr float laneHeaderH   = 26.0f;
        constexpr float laneGap       = 8.0f;
        constexpr float stepGap       = 3.0f;
        constexpr float wellCorner    = 6.0f;
        constexpr float gateBarH      = 4.0f;
        constexpr float transportH    = 44.0f;
        constexpr int   targetPillW   = 168;
        constexpr int   lengthPillW   = 62;

        /** The tempo divisions a lane can run at, and what each is worth in
            beats.  Kept together so the two can never disagree. */
        struct Division { const char* name; double beats; };

        const std::array<Division, 9> divisions {{
            { "1/32",  0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 },
            { "1/8T",  1.0 / 3.0 }, { "1/16.", 0.375 }, { "1/8", 0.5 },
            { "1/4T",  2.0 / 3.0 }, { "1/8.", 0.75 },   { "1/4", 1.0 }
        }};

        juce::StringArray divisionNames()
        {
            juce::StringArray names;
            for (const auto& d : divisions)
                names.add (d.name);
            return names;
        }

        const char* defaultLaneName (int index) noexcept
        {
            static const char* names[] = { "LANE 1", "LANE 2", "LANE 3", "LANE 4" };
            return names[juce::jlimit (0, 3, index)];
        }
    }

    // =======================================================================
    //  SeqLaneEditor
    //
    //  One lane's sixteen steps.  Owns no data: it reads and writes the lane's
    //  ValueTree directly, so what is on screen is always what is stored.
    // =======================================================================
    class SeqLaneEditor : public juce::Component
    {
    public:
        SeqLaneEditor (const PageSurface& owner, int laneIndex)
            : page (owner), index (laneIndex)
        {
            setTooltip();
        }

        void setTree (juce::ValueTree t) { tree = std::move (t); repaint(); }
        void setPlayhead (int step) { if (step != playhead) { playhead = step; repaint(); } }
        void setPreviewing (bool p) { if (p != previewing) { previewing = p; repaint(); } }

        int getLength() const
        {
            return juce::jlimit (1, 16, (int) tree.getProperty (seqIds::laneLength, 16));
        }

        float valueAt (int step) const
        {
            const auto packed = tree.getProperty (seqIds::laneValues).toString();
            const auto parts = juce::StringArray::fromTokens (packed, ",", "");

            return juce::isPositiveAndBelow (step, parts.size())
                       ? juce::jlimit (0.0f, 1.0f, parts[step].getFloatValue())
                       : 0.0f;
        }

        bool gateAt (int step) const
        {
            const auto mask = tree.getProperty (seqIds::laneGates).toString();
            return juce::isPositiveAndBelow (step, mask.length()) && mask[step] == '1';
        }

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat();
            previewWell (g, b);

            const int length = getLength();
            const bool enabled = (bool) tree.getProperty (seqIds::laneEnabled, true);

            for (int s = 0; s < 16; ++s)
            {
                const auto cell = stepBounds (s);

                if (cell.isEmpty())
                    continue;

                const bool inCycle = s < length;
                const bool gate = gateAt (s);
                const float value = valueAt (s);

                // The step's own ground: a step outside the lane's length is
                // drawn but dimmed, so shortening a lane hides nothing.
                g.setColour (theme::glassRaised.withAlpha (inCycle ? 0.85f : 0.35f));
                g.fillRoundedRectangle (cell, wellCorner * 0.5f);

                if (gate)
                {
                    const float h = juce::jmax (2.0f, (cell.getHeight() - gateBarH - 4.0f) * value);
                    const auto bar = juce::Rectangle<float> (cell.getX() + 1.0f,
                                                             cell.getBottom() - gateBarH - 3.0f - h,
                                                             cell.getWidth() - 2.0f, h);

                    const float alpha = (enabled && inCycle) ? 1.0f : 0.4f;

                    g.setColour (theme::violet.withAlpha (0.85f * alpha));
                    g.fillRoundedRectangle (bar, 2.0f);

                    g.setColour (theme::violetLight.withAlpha (alpha));
                    g.fillRect (bar.getX(), bar.getY(), bar.getWidth(), 1.0f);
                }

                // The gate itself, as a bar along the bottom of the cell: a
                // step can be gated off and keep its value, which is what makes
                // a pattern editable rather than destructive.
                g.setColour (gate ? theme::mint.withAlpha (inCycle ? 0.8f : 0.3f)
                                  : theme::glassEdge);
                g.fillRoundedRectangle (cell.getX() + 1.0f, cell.getBottom() - gateBarH - 1.0f,
                                        cell.getWidth() - 2.0f, gateBarH, 1.5f);

                // Beat markers every fourth step.
                if (s % 4 == 0)
                {
                    g.setColour (theme::glassInkFaint.withAlpha (0.5f));
                    g.fillRect (cell.getX(), b.getY() + 2.0f, 1.0f, 4.0f);
                }
            }

            if (previewing && juce::isPositiveAndBelow (playhead, 16))
            {
                const auto cell = stepBounds (playhead);
                g.setColour (theme::violetLight.withAlpha (0.55f));
                g.drawRoundedRectangle (cell.reduced (0.5f), wellCorner * 0.5f, 1.2f);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int step = stepAt (e.position);

            if (step < 0)
                return;

            if (e.mods.isShiftDown() || e.mods.isRightButtonDown())
            {
                paintingGates = true;
                gateTarget = ! gateAt (step);
                setGate (step, gateTarget);
            }
            else
            {
                paintingGates = false;
                setValue (step, valueFromY (e.position.y));

                // Painting a value on a silent step turns it on: nobody drags a
                // bar up in order to leave it muted.
                if (! gateAt (step))
                    setGate (step, true);
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const int step = stepAt (e.position);

            if (step < 0)
                return;

            if (paintingGates)
                setGate (step, gateTarget);
            else
                setValue (step, valueFromY (e.position.y));
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const int step = stepAt (e.position);

            if (step >= 0)
            {
                setValue (step, 0.5f);
                setGate (step, false);
            }
        }

    private:
        void setTooltip()
        {
            juce::ignoreUnused (index);
        }

        juce::Rectangle<float> stepBounds (int step) const
        {
            const auto b = getLocalBounds().toFloat().reduced (4.0f, 3.0f);
            const float w = (b.getWidth() - stepGap * 15.0f) / 16.0f;

            if (w <= 1.0f)
                return {};

            return { b.getX() + (float) step * (w + stepGap), b.getY(), w, b.getHeight() };
        }

        int stepAt (juce::Point<float> p) const
        {
            const auto b = getLocalBounds().toFloat().reduced (4.0f, 3.0f);
            const float pitch = (b.getWidth() - stepGap * 15.0f) / 16.0f + stepGap;

            if (pitch <= 0.0f)
                return -1;

            return juce::jlimit (0, 15, (int) ((p.x - b.getX()) / pitch));
        }

        float valueFromY (float y) const
        {
            const auto b = getLocalBounds().toFloat().reduced (4.0f, 3.0f);
            const float usable = juce::jmax (1.0f, b.getHeight() - gateBarH - 6.0f);

            return juce::jlimit (0.0f, 1.0f, 1.0f - (y - b.getY()) / usable);
        }

        void setValue (int step, float value)
        {
            if (! tree.isValid())
                return;

            auto parts = juce::StringArray::fromTokens (
                tree.getProperty (seqIds::laneValues).toString(), ",", "");

            while (parts.size() < 16)
                parts.add ("0.5");

            parts.set (step, juce::String (juce::jlimit (0.0f, 1.0f, value), 3));
            tree.setProperty (seqIds::laneValues, parts.joinIntoString (","), nullptr);
            repaint();
        }

        void setGate (int step, bool on)
        {
            if (! tree.isValid())
                return;

            auto mask = tree.getProperty (seqIds::laneGates).toString();

            while (mask.length() < 16)
                mask += "0";

            mask = mask.substring (0, step) + (on ? "1" : "0") + mask.substring (step + 1);
            tree.setProperty (seqIds::laneGates, mask, nullptr);
            repaint();
        }

        const PageSurface& page;
        int index = 0;
        juce::ValueTree tree;

        int  playhead = 0;
        bool previewing = false;
        bool paintingGates = false;
        bool gateTarget = true;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeqLaneEditor)
    };

    // =======================================================================
    //  SeqPage
    // =======================================================================
    SeqPage::SeqPage (NacarProcessor& p, EditorHost& h)
        : PageSurface (p, h, "SEQUENCER", "STEP MODULATION FOUNDATION", Material::ceramic)
    {
        fetchTree();

        for (int i = 0; i < numLanes; ++i)
        {
            auto& lane = lanes[i];

            auto* editor = new SeqLaneEditor (*this, i);
            extras.add (editor);
            addAndMakeVisible (editor);
            lane.editor = editor;

            auto* target = new PillButton ("NO TARGET", PillButton::Style::ceramic);
            target->setTextSize (7.5f, 0.10f);
            target->setTooltip ("Which parameter this lane will drive.\n"
                                "Stored now; read by the trigger engine when it lands.");
            target->onClick = [this, i] { chooseTarget (i); };
            extras.add (target);
            addAndMakeVisible (target);
            lane.target = target;

            auto* length = new PillButton ("16", PillButton::Style::ceramic);
            length->setTextSize (7.5f, 0.10f);
            length->setTooltip ("Steps per cycle.");
            length->onClick = [this, i] { chooseLength (i); };
            extras.add (length);
            addAndMakeVisible (length);
            lane.length = length;

            auto* enable = new ToggleSwitch (ToggleSwitch::Size::small);
            enable->setTooltip ("Enable this lane.");
            enable->onToggle = [this, i] (bool on)
            {
                if (lanes[i].tree.isValid())
                    lanes[i].tree.setProperty (seqIds::laneEnabled, on, nullptr);

                if (lanes[i].editor != nullptr)
                    lanes[i].editor->repaint();
            };
            extras.add (enable);
            addAndMakeVisible (enable);
            lane.enable = enable;
        }

        previewButton = new PillButton ("PREVIEW", PillButton::Style::ceramic);
        previewButton->setTextSize (8.0f, 0.14f);
        previewButton->setTooltip ("Runs the playhead so a pattern can be read.\n"
                                   "An editing aid. It does not modulate anything.");
        previewButton->onClick = [this]
        {
            previewRunning = ! previewRunning;
            previewButton->setSelected (previewRunning);
            previewStartMs = juce::Time::getMillisecondCounterHiRes();

            for (auto& lane : lanes)
                if (lane.editor != nullptr)
                    lane.editor->setPreviewing (previewRunning);
        };
        extras.add (previewButton);
        addAndMakeVisible (previewButton);

        divisionButton = new PillButton ("1/16", PillButton::Style::ceramic);
        divisionButton->setTextSize (8.0f, 0.14f);
        divisionButton->setTooltip ("Step length against the host tempo.");
        divisionButton->onClick = [this] { chooseDivision(); };
        extras.add (divisionButton);
        addAndMakeVisible (divisionButton);

        refreshLanes (true);
        setTickHz (24);     // fast enough for the preview playhead to read smoothly
    }

    SeqPage::~SeqPage() = default;

    // -----------------------------------------------------------------------
    void SeqPage::fetchTree()
    {
        auto& session = processor.getStateManager();
        seqTree = session.group (seqIds::SEQUENCER);

        if (! seqTree.hasProperty (seqIds::seqDivision))
            seqTree.setProperty (seqIds::seqDivision, 2, nullptr);   // 1/16

        while (seqTree.getNumChildren() < numLanes)
        {
            juce::ValueTree lane (seqIds::SEQLANE);
            const int index = seqTree.getNumChildren();

            lane.setProperty (seqIds::laneName, defaultLaneName (index), nullptr);
            lane.setProperty (seqIds::laneTarget, "", nullptr);
            lane.setProperty (seqIds::laneEnabled, index == 0, nullptr);
            lane.setProperty (seqIds::laneLength, 16, nullptr);

            juce::StringArray values;
            juce::String gates;

            for (int s = 0; s < 16; ++s)
            {
                values.add ("0.500");
                gates += "0";
            }

            lane.setProperty (seqIds::laneValues, values.joinIntoString (","), nullptr);
            lane.setProperty (seqIds::laneGates, gates, nullptr);

            seqTree.addChild (lane, -1, nullptr);
        }

        for (int i = 0; i < numLanes; ++i)
            lanes[i].tree = seqTree.getChild (i);
    }

    void SeqPage::refreshLanes (bool force)
    {
        for (int i = 0; i < numLanes; ++i)
        {
            auto& lane = lanes[i];

            if (! lane.tree.isValid())
                continue;

            if (lane.editor != nullptr && force)
                lane.editor->setTree (lane.tree);

            if (lane.target != nullptr)
            {
                const auto id = lane.tree.getProperty (seqIds::laneTarget).toString();
                const auto name = parameterDisplayName (id);

                if (lane.target->getButtonText() != name)
                    lane.target->setButtonText (name);

                lane.target->setSelected (id.isNotEmpty());
            }

            if (lane.length != nullptr)
            {
                const juce::String n ((int) lane.tree.getProperty (seqIds::laneLength, 16));

                if (lane.length->getButtonText() != n)
                    lane.length->setButtonText (n);
            }

            if (lane.enable != nullptr)
                lane.enable->setToggleState ((bool) lane.tree.getProperty (seqIds::laneEnabled, false),
                                             juce::dontSendNotification);
        }

        if (divisionButton != nullptr)
        {
            const int d = juce::jlimit (0, (int) divisions.size() - 1,
                                        (int) seqTree.getProperty (seqIds::seqDivision, 2));

            if (divisionButton->getButtonText() != divisions[(size_t) d].name)
                divisionButton->setButtonText (divisions[(size_t) d].name);
        }
    }

    void SeqPage::chooseTarget (int laneIndex)
    {
        if (! juce::isPositiveAndBelow (laneIndex, numLanes) || ! lanes[laneIndex].tree.isValid())
            return;

        const auto current = lanes[laneIndex].tree.getProperty (seqIds::laneTarget).toString();

        juce::PopupMenu menu;
        buildParameterMenu (menu, current);

        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (lanes[laneIndex].target),
                            [this, laneIndex] (int result)
                            {
                                if (result == 0)
                                    return;

                                lanes[laneIndex].tree.setProperty (
                                    seqIds::laneTarget, parameterMenuResult (result), nullptr);
                                refreshLanes();
                            });
    }

    void SeqPage::chooseLength (int laneIndex)
    {
        if (! juce::isPositiveAndBelow (laneIndex, numLanes) || ! lanes[laneIndex].tree.isValid())
            return;

        const int current = (int) lanes[laneIndex].tree.getProperty (seqIds::laneLength, 16);

        juce::PopupMenu menu;
        for (int n = 1; n <= 16; ++n)
            menu.addItem (n, juce::String (n) + (n == 1 ? " step" : " steps"), true, n == current);

        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (lanes[laneIndex].length),
                            [this, laneIndex] (int result)
                            {
                                if (result <= 0)
                                    return;

                                lanes[laneIndex].tree.setProperty (seqIds::laneLength, result, nullptr);
                                refreshLanes();

                                if (lanes[laneIndex].editor != nullptr)
                                    lanes[laneIndex].editor->repaint();
                            });
    }

    void SeqPage::chooseDivision()
    {
        const int current = juce::jlimit (0, (int) divisions.size() - 1,
                                          (int) seqTree.getProperty (seqIds::seqDivision, 2));

        juce::PopupMenu menu;
        const auto names = divisionNames();

        for (int i = 0; i < names.size(); ++i)
            menu.addItem (i + 1, names[i], true, i == current);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (divisionButton),
                            [this] (int result)
                            {
                                if (result <= 0)
                                    return;

                                seqTree.setProperty (seqIds::seqDivision, result - 1, nullptr);
                                refreshLanes();
                            });
    }

    double SeqPage::stepSeconds() const
    {
        const int d = juce::jlimit (0, (int) divisions.size() - 1,
                                    (int) seqTree.getProperty (seqIds::seqDivision, 2));

        const double bpm = juce::jlimit (20.0, 300.0, processor.getHostBpm());

        return divisions[(size_t) d].beats * 60.0 / bpm;
    }

    // -----------------------------------------------------------------------
    void SeqPage::tick()
    {
        refreshLanes();

        if (! previewRunning)
            return;

        const double elapsed = (juce::Time::getMillisecondCounterHiRes() - previewStartMs) * 0.001;
        const double step = juce::jmax (0.01, stepSeconds());

        for (int i = 0; i < numLanes; ++i)
        {
            auto& lane = lanes[i];

            if (lane.editor == nullptr)
                continue;

            const int length = lane.editor->getLength();
            lane.editor->setPlayhead ((int) std::fmod (elapsed / step, (double) length));
        }

        playhead = lanes[0].editor != nullptr ? 0 : 0;
    }

    // -----------------------------------------------------------------------
    void SeqPage::paint (juce::Graphics& g)
    {
        paintChrome (g);

        section (g, lanesBounds.toFloat(), "LANES", icons::Icon::navSeq);
        section (g, transportBounds.toFloat(), "PREVIEW", icons::Icon::play);

        for (int i = 0; i < numLanes; ++i)
        {
            const auto& lane = lanes[i];

            if (lane.header.isEmpty())
                continue;

            const auto name = lane.tree.isValid()
                                  ? lane.tree.getProperty (seqIds::laneName).toString()
                                  : juce::String (defaultLaneName (i));

            text (g, name, { (float) lane.header.getX() + 34.0f,
                             (float) lane.header.getCentreY() + 3.0f },
                  8.0f, 0.16f, ink());
        }

        // The honesty line.  It is on the page, not only in the source, because
        // a step editor that looks finished and does nothing is exactly the
        // kind of thing the specification forbids shipping unannounced.
        const auto note = transportBounds.toFloat();
        text (g, "EDITS AND SAVES STEP DATA  \xc2\xb7  NO TRIGGER ENGINE IN THIS BUILD",
              { note.getRight() - page::sectionPad, note.getBottom() - page::sectionPad },
              page::noteSize, 0.14f, inkFaint(),
              juce::Justification::right, note.getWidth() - page::sectionPad * 2.0f);
    }

    void SeqPage::resized()
    {
        beginLayout();

        auto area = contentArea();

        transportBounds = area.removeFromBottom ((int) transportH);
        area.removeFromBottom ((int) page::gap);
        lanesBounds = area;

        auto inner = inside (lanesBounds);
        const int laneH = juce::jmax (40,
            (inner.getHeight() - (int) laneGap * (numLanes - 1)) / numLanes);

        for (int i = 0; i < numLanes; ++i)
        {
            auto row = inner.removeFromTop (laneH);

            if (i < numLanes - 1)
                inner.removeFromTop ((int) laneGap);

            auto& lane = lanes[i];

            lane.header = row.removeFromTop ((int) laneHeaderH);
            lane.well = row;

            auto header = lane.header;

            if (lane.enable != nullptr)
                lane.enable->setBounds (header.removeFromLeft (30).withSizeKeepingCentre (26, 14));

            header.removeFromLeft (4);
            header.removeFromLeft (110);                 // room for the painted lane name

            if (lane.length != nullptr)
                lane.length->setBounds (header.removeFromRight (lengthPillW)
                                              .withSizeKeepingCentre (lengthPillW, 22));

            header.removeFromRight (6);

            if (lane.target != nullptr)
                lane.target->setBounds (header.removeFromRight (targetPillW)
                                              .withSizeKeepingCentre (targetPillW, 22));

            if (lane.editor != nullptr)
                lane.editor->setBounds (lane.well.reduced (0, 2));
        }

        auto transport = inside (transportBounds);

        if (previewButton != nullptr)
            previewButton->setBounds (transport.removeFromLeft (96).withSizeKeepingCentre (96, 26));

        transport.removeFromLeft (8);

        if (divisionButton != nullptr)
            divisionButton->setBounds (transport.removeFromLeft (72).withSizeKeepingCentre (72, 26));
    }
}
