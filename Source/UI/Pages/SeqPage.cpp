#include "SeqPage.h"

/*
    ======================================================================
      SEQ  -  what this page does
    ======================================================================

    Four lanes of sixteen steps.  Each step has a value and a gate; each
    lane has a length, an enable and a target parameter chosen from the
    parameter table itself.  All of it is written into the session tree
    under a SEQUENCER child, so it saves with the project, travels with a
    preset and survives a reload.  The editing is real editing: drag to
    paint values, shift-click to toggle gates, and the well shows exactly
    what is stored.

    AND IT IS PLAYED.  Source/Audio/Modulation/SequencerEngine advances
    exactly this tree against the host transport and applies each lane's
    current step to its target through the modulation overlay; the processor
    republishes the branch whenever anything here writes to it.  A step
    value is absolute - 0..1 across the target parameter's whole range, the
    height of the bar the well draws - and a gate of 0 holds the previous
    value rather than zeroing it.  The engine's header is where those
    decisions are argued.

    The PREVIEW playhead is now a readout of the engine's own position, not
    a clock of its own: it asks the processor which step each lane is on.
    The lanes advance whether or not it is switched on.
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

        /** The tempo divisions a lane can run at.  The table itself lives in
            StateManager.h beside the identifiers, because the sequencer engine
            has to run at the beats this page prints the name of and neither
            half may own the pairing alone. */
        juce::StringArray divisionNames()
        {
            juce::StringArray names;
            for (const auto& d : seq::divisions)
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
            return juce::jlimit (1, 16, (int) tree.getProperty (ids::laneLength, 16));
        }

        float valueAt (int step) const
        {
            const auto packed = tree.getProperty (ids::laneValues).toString();
            const auto parts = juce::StringArray::fromTokens (packed, ",", "");

            return juce::isPositiveAndBelow (step, parts.size())
                       ? juce::jlimit (0.0f, 1.0f, parts[step].getFloatValue())
                       : 0.0f;
        }

        bool gateAt (int step) const
        {
            const auto mask = tree.getProperty (ids::laneGates).toString();
            return juce::isPositiveAndBelow (step, mask.length()) && mask[step] == '1';
        }

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat();
            previewWell (g, b);

            const int length = getLength();
            const bool enabled = (bool) tree.getProperty (ids::laneEnabled, true);

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
                tree.getProperty (ids::laneValues).toString(), ",", "");

            while (parts.size() < 16)
                parts.add ("0.5");

            parts.set (step, juce::String (juce::jlimit (0.0f, 1.0f, value), 3));
            tree.setProperty (ids::laneValues, parts.joinIntoString (","), nullptr);
            repaint();
        }

        void setGate (int step, bool on)
        {
            if (! tree.isValid())
                return;

            auto mask = tree.getProperty (ids::laneGates).toString();

            while (mask.length() < 16)
                mask += "0";

            mask = mask.substring (0, step) + (on ? "1" : "0") + mask.substring (step + 1);
            tree.setProperty (ids::laneGates, mask, nullptr);
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
            target->setTooltip ("Which parameter this lane drives.\n"
                                "A step is absolute: 0..1 across this parameter's range.");
            target->onClick = [this, i] { chooseTarget (i); };
            extras.add (target);
            addAndMakeVisible (target);
            lane.target = target;

            auto* length = new PillButton ("16", PillButton::Style::ceramic);
            length->setTextSize (7.5f, 0.10f);
            length->setTooltip ("Steps per cycle. Lanes of different lengths drift\n"
                                "against each other and realign at their common multiple.");
            length->onClick = [this, i] { chooseLength (i); };
            extras.add (length);
            addAndMakeVisible (length);
            lane.length = length;

            auto* enable = new ToggleSwitch (ToggleSwitch::Size::small);
            enable->setTooltip ("Enable this lane. Off, it writes nothing at all\n"
                                "and its target goes back to the knob.");
            enable->onToggle = [this, i] (bool on)
            {
                if (lanes[i].tree.isValid())
                    lanes[i].tree.setProperty (ids::laneEnabled, on, nullptr);

                if (lanes[i].editor != nullptr)
                    lanes[i].editor->repaint();
            };
            extras.add (enable);
            addAndMakeVisible (enable);
            lane.enable = enable;
        }

        previewButton = new PillButton ("PREVIEW", PillButton::Style::ceramic);
        previewButton->setTextSize (8.0f, 0.14f);
        previewButton->setTooltip ("Shows the playhead: where the sequencer actually is.\n"
                                   "The lanes run whether this is on or off.");
        previewButton->onClick = [this]
        {
            previewRunning = ! previewRunning;
            previewButton->setSelected (previewRunning);

            for (auto& lane : lanes)
                if (lane.editor != nullptr)
                    lane.editor->setPreviewing (previewRunning);
        };
        extras.add (previewButton);
        addAndMakeVisible (previewButton);

        divisionButton = new PillButton ("1/16", PillButton::Style::ceramic);
        divisionButton->setTextSize (8.0f, 0.14f);
        divisionButton->setTooltip ("Step length against the host tempo.\n"
                                    "Shared by all four lanes.");
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
        seqTree = session.group (ids::SEQUENCER);

        if (! seqTree.hasProperty (ids::seqDivision))
            seqTree.setProperty (ids::seqDivision, 2, nullptr);   // 1/16

        while (seqTree.getNumChildren() < numLanes)
        {
            juce::ValueTree lane (ids::SEQLANE);
            const int index = seqTree.getNumChildren();

            lane.setProperty (ids::laneName, defaultLaneName (index), nullptr);
            lane.setProperty (ids::laneTarget, "", nullptr);
            lane.setProperty (ids::laneEnabled, index == 0, nullptr);
            lane.setProperty (ids::laneLength, 16, nullptr);

            juce::StringArray values;
            juce::String gates;

            for (int s = 0; s < 16; ++s)
            {
                values.add ("0.500");
                gates += "0";
            }

            lane.setProperty (ids::laneValues, values.joinIntoString (","), nullptr);
            lane.setProperty (ids::laneGates, gates, nullptr);

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
                const auto id = lane.tree.getProperty (ids::laneTarget).toString();
                const auto name = parameterDisplayName (id);

                if (lane.target->getButtonText() != name)
                    lane.target->setButtonText (name);

                lane.target->setSelected (id.isNotEmpty());
            }

            if (lane.length != nullptr)
            {
                const juce::String n ((int) lane.tree.getProperty (ids::laneLength, 16));

                if (lane.length->getButtonText() != n)
                    lane.length->setButtonText (n);
            }

            if (lane.enable != nullptr)
                lane.enable->setToggleState ((bool) lane.tree.getProperty (ids::laneEnabled, false),
                                             juce::dontSendNotification);
        }

        if (divisionButton != nullptr)
        {
            const int d = juce::jlimit (0, seq::numDivisions - 1,
                                        (int) seqTree.getProperty (ids::seqDivision, 2));

            if (divisionButton->getButtonText() != seq::divisions[(size_t) d].name)
                divisionButton->setButtonText (seq::divisions[(size_t) d].name);
        }
    }

    void SeqPage::chooseTarget (int laneIndex)
    {
        if (! juce::isPositiveAndBelow (laneIndex, numLanes) || ! lanes[laneIndex].tree.isValid())
            return;

        const auto current = lanes[laneIndex].tree.getProperty (ids::laneTarget).toString();

        juce::PopupMenu menu;
        buildParameterMenu (menu, current);

        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (lanes[laneIndex].target),
                            [this, laneIndex] (int result)
                            {
                                if (result == 0)
                                    return;

                                lanes[laneIndex].tree.setProperty (
                                    ids::laneTarget, parameterMenuResult (result), nullptr);
                                refreshLanes();
                            });
    }

    void SeqPage::chooseLength (int laneIndex)
    {
        if (! juce::isPositiveAndBelow (laneIndex, numLanes) || ! lanes[laneIndex].tree.isValid())
            return;

        const int current = (int) lanes[laneIndex].tree.getProperty (ids::laneLength, 16);

        juce::PopupMenu menu;
        for (int n = 1; n <= 16; ++n)
            menu.addItem (n, juce::String (n) + (n == 1 ? " step" : " steps"), true, n == current);

        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (lanes[laneIndex].length),
                            [this, laneIndex] (int result)
                            {
                                if (result <= 0)
                                    return;

                                lanes[laneIndex].tree.setProperty (ids::laneLength, result, nullptr);
                                refreshLanes();

                                if (lanes[laneIndex].editor != nullptr)
                                    lanes[laneIndex].editor->repaint();
                            });
    }

    void SeqPage::chooseDivision()
    {
        const int current = juce::jlimit (0, seq::numDivisions - 1,
                                          (int) seqTree.getProperty (ids::seqDivision, 2));

        juce::PopupMenu menu;
        const auto names = divisionNames();

        for (int i = 0; i < names.size(); ++i)
            menu.addItem (i + 1, names[i], true, i == current);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (divisionButton),
                            [this] (int result)
                            {
                                if (result <= 0)
                                    return;

                                seqTree.setProperty (ids::seqDivision, result - 1, nullptr);
                                refreshLanes();
                            });
    }

    // -----------------------------------------------------------------------
    void SeqPage::tick()
    {
        refreshLanes();

        if (! previewRunning)
            return;

        // The engine's own position, not a clock of this page's.  A playhead
        // that ran on the interface timer would agree with the audio only by
        // coincidence, and would disagree the moment the host looped - which is
        // exactly when a user is looking at it.  One relaxed load per lane.
        for (int i = 0; i < numLanes; ++i)
            if (lanes[i].editor != nullptr)
                lanes[i].editor->setPlayhead (processor.getSequencerStep (i));
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
                                  ? lane.tree.getProperty (ids::laneName).toString()
                                  : juce::String (defaultLaneName (i));

            text (g, name, { (float) lane.header.getX() + 34.0f,
                             (float) lane.header.getCentreY() + 3.0f },
                  8.0f, 0.16f, ink());
        }

        // The line that used to say there was no trigger engine.  There is one
        // now, so it says what the sequencer actually does instead: a page that
        // lies in the user's favour is no better than one that lies against
        // them.  A step is an absolute position, not an offset from the knob,
        // and that is the one thing about this page that is not obvious.
        const auto note = transportBounds.toFloat();
        text (g, "LANES DRIVE THEIR TARGETS  \xc2\xb7  A STEP IS ABSOLUTE, 0-100% OF RANGE",
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
