#include "ModPage.h"

#include <cmath>

namespace nacar::ui
{
    // =======================================================================
    //  Page geometry.
    //
    //  Layout.h is frozen and describes the locked chassis; it knows nothing
    //  about the deep-edit pages, which are new surfaces cut into the centre
    //  column.  The geometry of this page therefore lives here, named, at the
    //  top of the file, exactly as the contract requires.
    //
    //  The page is handed the union of layout::viewport and layout::fxChain -
    //  875 x 818 - so the content area below the title block is 839 x 724.
    // =======================================================================
    static constexpr int rowLfoH    = 166;   // LFO 1 | LFO 2
    static constexpr int rowEnvH    = 176;   // AMP ENV | MOD ENV 1 | MOD ENV 2
    static constexpr int rowModH    = 212;   // BREATH | PULSE
    static constexpr int rowMatrixH = 140;   // MOD MATRIX
    static constexpr int gapPx      = 10;    // 166+176+212+140 + 3 gaps = 724

    static constexpr int ampSectionW    = 283;
    static constexpr int envSectionW    = 268;
    static constexpr int breathSectionW = 270;

    static constexpr int previewW = 120;     // the live LFO shape preview
    static constexpr int previewH = 30;

    // Knob radii.  The MOD page carries thirty-eight knobs in 839 x 724, so the
    // dense groups sit at the small end of the house range; nothing on the page
    // goes below 20, and every knob keeps its label.
    static constexpr float lfoKnobR    = 22.0f;
    static constexpr float envKnobR    = 20.0f;
    static constexpr float breathKnobR = 20.0f;
    static constexpr float pulseKnobR  = 22.0f;
    static constexpr float pulseDestR  = 20.0f;

    static constexpr int matrixSlots   = 8;

    // =======================================================================
    //  Preview drawing
    // =======================================================================

    /** One LFO sample.  `t` is a continuous position in cycles, so the two
        random shapes stay coherent across the whole preview rather than
        restarting every cycle. */
    static float lfoSample (int shape, float t) noexcept
    {
        const float p = t - std::floor (t);

        switch (shape)
        {
            case 0:  return std::sin (p * juce::MathConstants<float>::twoPi);           // SINE
            case 1:  return p < 0.25f ? p * 4.0f                                        // TRIANGLE
                          : (p < 0.75f ? 2.0f - p * 4.0f : p * 4.0f - 4.0f);
            case 2:  return p * 2.0f - 1.0f;                                            // SAW UP
            case 3:  return 1.0f - p * 2.0f;                                            // SAW DOWN
            case 4:  return p < 0.5f ? 1.0f : -1.0f;                                    // SQUARE

            case 5:                                                                     // RANDOM
            {
                const int step = (int) std::floor (t * 4.0f);
                return stableRandom (step) * 2.0f - 1.0f;
            }

            case 6:                                                                     // SMOOTH RANDOM
            {
                const float s = t * 3.0f;
                const int   k = (int) std::floor (s);
                const float f = s - (float) k;
                const float a = stableRandom (k)     * 2.0f - 1.0f;
                const float b = stableRandom (k + 1) * 2.0f - 1.0f;

                return a + (b - a) * (f * f * (3.0f - 2.0f * f));   // smoothstep
            }

            default: break;
        }

        return 0.0f;
    }

    static void drawLfoPreview (juce::Graphics& g, juce::Rectangle<float> area,
                                int shape, float depth, float phase)
    {
        previewWell (g, area);

        const auto r = area.reduced (5.0f, 5.0f);

        g.setColour (theme::glassEdge);
        g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 1.0f);

        // At zero depth the true trace is a flat line, which teaches nothing
        // about the selected shape, so the preview keeps a legible floor.
        const float amp = (r.getHeight() * 0.5f - 1.0f) * juce::jmax (0.18f, depth);

        juce::Path line;
        constexpr int steps = 120;
        constexpr float cycles = 2.0f;

        for (int i = 0; i <= steps; ++i)
        {
            const float u = (float) i / (float) steps;
            const float v = lfoSample (shape, phase + u * cycles);
            const float x = r.getX() + u * r.getWidth();
            const float y = r.getCentreY() - v * amp;

            if (i == 0)
                line.startNewSubPath (x, y);
            else
                line.lineTo (x, y);
        }

        g.setColour (theme::violet);
        g.strokePath (line, juce::PathStrokeType (1.0f));
    }

    /** A real ADSR curve from the four parameter values.  Segment widths are
        square-root compressed so a 4 ms attack is still visible beside a 20 s
        release; the sustain segment always gets a fixed share of the width. */
    static void drawEnvelopePreview (juce::Graphics& g, juce::Rectangle<float> area,
                                     float attackS, float decayS, float sustain, float releaseS)
    {
        previewWell (g, area);

        const auto r = area.reduced (7.0f, 8.0f);

        const float wa = std::sqrt (juce::jmax (0.0f, attackS));
        const float wd = std::sqrt (juce::jmax (0.0f, decayS));
        const float wr = std::sqrt (juce::jmax (0.0f, releaseS));
        const float sum = juce::jmax (1.0e-4f, wa + wd + wr);

        constexpr float holdShare = 0.20f;
        const float usable = r.getWidth() * (1.0f - holdShare);

        const float x0 = r.getX();
        const float xa = x0 + usable * (wa / sum);
        const float xd = xa + usable * (wd / sum);
        const float xs = xd + r.getWidth() * holdShare;
        const float xr = r.getRight();

        const float yTop = r.getY();
        const float yBot = r.getBottom();
        const float ySus = yBot - (yBot - yTop) * juce::jlimit (0.0f, 1.0f, sustain);

        juce::Path line;
        line.startNewSubPath (x0, yBot);
        line.quadraticTo (x0 + (xa - x0) * 0.55f, yTop + (yBot - yTop) * 0.12f, xa, yTop);
        line.quadraticTo (xa + (xd - xa) * 0.35f, ySus, xd, ySus);
        line.lineTo (xs, ySus);
        line.quadraticTo (xs + (xr - xs) * 0.35f, yBot, xr, yBot);

        juce::Path fill (line);
        fill.lineTo (x0, yBot);
        fill.closeSubPath();

        violetTrace (g, fill, line);

        // Sustain level, so the S knob has something to read against.
        g.setColour (theme::violet.withAlpha (0.22f));
        g.drawLine (xd, ySus, xs, ySus, 1.0f);

        g.setColour (theme::glassEdge);
        g.drawLine (r.getX(), yBot, r.getRight(), yBot, 1.0f);
    }

    /** Breath is deliberately not an LFO (master spec section 57): it is a
        bounded random walk that never repeats.  This preview is an
        illustration of the walk the current settings describe - it is computed
        from the parameters, not read back from the running modulator, which
        does not exist until the modulation engine lands. */
    static void drawBreathPreview (juce::Graphics& g, juce::Rectangle<float> area,
                                   float amount, float speed, float randomness, float shape)
    {
        previewWell (g, area);

        const auto r = area.reduced (6.0f, 6.0f);

        g.setColour (theme::glassEdge);
        g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 1.0f);

        const int anchors = juce::jlimit (4, 24, 4 + (int) (speed * 5.0f));

        std::array<float, 32> walk {};
        float v = 0.0f;

        for (int i = 0; i < anchors + 2; ++i)
        {
            const float step = (stableRandom (i + 101) * 2.0f - 1.0f)
                                * (0.18f + 0.55f * randomness);
            v = juce::jlimit (-1.0f, 1.0f, v * 0.86f + step);   // bounded: it always comes home
            walk[(size_t) i] = v;
        }

        const float amp = (r.getHeight() * 0.5f - 1.0f) * juce::jmax (0.15f, amount);

        juce::Path line;
        constexpr int steps = 160;

        for (int i = 0; i <= steps; ++i)
        {
            const float u = (float) i / (float) steps;
            const float s = u * (float) anchors;
            const int   k = juce::jmin (anchors, (int) std::floor (s));
            const float f = s - (float) k;

            // shape 0 = smooth drift, shape 1 = stepped and glided.
            const float smooth = f * f * (3.0f - 2.0f * f);
            const float glide  = juce::jlimit (0.0f, 1.0f, f * 6.0f);
            const float mix    = smooth + (glide - smooth) * juce::jlimit (0.0f, 1.0f, shape);

            const float a = walk[(size_t) k];
            const float b = walk[(size_t) juce::jmin (anchors + 1, k + 1)];

            const float x = r.getX() + u * r.getWidth();
            const float y = r.getCentreY() - (a + (b - a) * mix) * amp;

            if (i == 0)
                line.startNewSubPath (x, y);
            else
                line.lineTo (x, y);
        }

        g.setColour (theme::violet);
        g.strokePath (line, juce::PathStrokeType (1.0f));
    }

    // =======================================================================
    //  ModMatrix
    //
    //  Eight routing slots.  There are NO host parameters behind this table -
    //  check ParameterList.h, there is no mod_matrix group - so every slot is
    //  persisted in the session tree under ids::MODMATRIX / ids::MODSLOT with
    //  ids::modSource, ids::modTarget, ids::modDepth and ids::modEnabled.
    //
    //  The target is stored as the parameter's permanent string ID, which is
    //  guaranteed never to be renamed, rather than as an index into a list
    //  that will grow.  The source is stored as its display name.
    //
    //  NOTHING CONSUMES THESE ROUTINGS YET.  The modulation engine reads them
    //  in Phase 10; until then the matrix edits and persists state and that is
    //  all it claims to do.
    // =======================================================================
    class ModMatrix : public juce::Component
    {
    public:
        ModMatrix (NacarProcessor& p, PageSurface& s)
            : processor (p), surface (s)
        {
            fetchTree();

            for (int i = 0; i < matrixSlots; ++i)
            {
                auto& slot = slots[(size_t) i];

                slot.source = new PillButton (juce::String(), PillButton::Style::ceramic);
                slot.target = new PillButton (juce::String(), PillButton::Style::ceramic);

                for (auto* b : { slot.source, slot.target })
                {
                    owned.add (b);
                    b->setTextSize (7.5f, 0.10f);
                    b->setCornerRadius (6.0f);
                    addAndMakeVisible (b);
                }

                slot.source->onClick = [this, i] { chooseSource (i); };
                slot.target->onClick = [this, i] { chooseTarget (i); };
                slot.target->setTrailingIcon (icons::Icon::chevronDown);

                slot.enable = new ToggleSwitch (ToggleSwitch::Size::small);
                owned.add (slot.enable);
                addAndMakeVisible (slot.enable);

                slot.enable->onToggle = [this, i] (bool on)
                {
                    if (slots[(size_t) i].tree.isValid())
                        slots[(size_t) i].tree.setProperty (ids::modEnabled, on, nullptr);
                };
            }

            refreshFromState (true);
        }

        /** Re-reads the tree.  Called on the page clock because loading a host
            session replaces the SESSION children wholesale, which invalidates
            the handles cached here. */
        void refreshFromState (bool force = false)
        {
            const auto current = processor.getStateManager().group (ids::MODMATRIX);

            if (force || current != matrixTree)
                fetchTree();

            bool changed = false;

            for (int i = 0; i < matrixSlots; ++i)
            {
                auto& slot = slots[(size_t) i];

                if (! slot.tree.isValid())
                    continue;

                const auto source = slot.tree.getProperty (ids::modSource).toString();
                const auto target = PageSurface::parameterDisplayName (
                                        slot.tree.getProperty (ids::modTarget).toString());
                const bool on     = (bool) slot.tree.getProperty (ids::modEnabled);

                if (force || slot.source->getButtonText() != source)
                {
                    slot.source->setButtonText (source);
                    changed = true;
                }

                if (force || slot.target->getButtonText() != target)
                {
                    slot.target->setButtonText (target);
                    changed = true;
                }

                if (slot.enable->getToggleState() != on)
                    slot.enable->setToggleState (on, juce::dontSendNotification);

                const float depth = (float) (double) slot.tree.getProperty (ids::modDepth);

                if (std::abs (depth - slot.shownDepth) > 1.0e-4f)
                {
                    slot.shownDepth = depth;
                    changed = true;
                }
            }

            if (changed)
                repaint();
        }

        void resized() override
        {
            auto area = getLocalBounds();
            const int columnW = (area.getWidth() - columnGap) / 2;

            auto left  = area.removeFromLeft (columnW);
            area.removeFromLeft (columnGap);
            auto right = area;

            const int rowH = left.getHeight() / (matrixSlots / 2);

            for (int i = 0; i < matrixSlots; ++i)
            {
                auto& column = (i < matrixSlots / 2) ? left : right;
                auto row = column.removeFromTop (rowH);

                auto& slot = slots[(size_t) i];
                slot.row = row;

                auto r = row.reduced (0, (rowH - 20) / 2);

                slot.source->setBounds (r.removeFromLeft (sourcePillW));
                r.removeFromLeft (6);
                slot.target->setBounds (r.removeFromLeft (targetPillW));
                r.removeFromLeft (8);

                slot.depthBar = r.removeFromLeft (depthBarW).reduced (0, 4);
                r.removeFromLeft (8);

                slot.enable->setBounds (r.getX(), r.getCentreY() - 7, 26, 14);
            }
        }

        void paint (juce::Graphics& g) override
        {
            for (int i = 0; i < matrixSlots; ++i)
            {
                const auto& slot = slots[(size_t) i];
                const auto bar = slot.depthBar.toFloat();

                if (bar.isEmpty())
                    continue;

                const bool live = slot.enable != nullptr && slot.enable->getToggleState();

                theme::glassSurface (g, bar, 3.0f, theme::glassDeep);

                // Bipolar: the bar grows outward from the centre detent.
                const float centre = bar.getCentreX();
                const float depth  = juce::jlimit (-1.0f, 1.0f, slot.shownDepth);
                const float end    = centre + depth * (bar.getWidth() * 0.5f - 2.0f);

                g.setColour (theme::violet.withAlpha (live ? 0.95f : 0.35f));
                g.fillRect (juce::Rectangle<float> (juce::jmin (centre, end), bar.getY() + 2.0f,
                                                    std::abs (end - centre), bar.getHeight() - 4.0f));

                g.setColour (theme::glassEdge);
                g.drawLine (centre, bar.getY() + 1.0f, centre, bar.getBottom() - 1.0f, 1.0f);

                // The readout sits on the page's own material, so it goes
                // through the surface rather than assuming glass or ceramic.
                surface.text (g, juce::String (juce::roundToInt (depth * 100.0f)),
                              { bar.getRight(), bar.getY() - 3.0f },
                              6.5f, 0.10f, surface.inkFaint(),
                              juce::Justification::right, 40.0f);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = slotAt (e.getPosition());

            if (dragging >= 0)
                dragStart = slots[(size_t) dragging].shownDepth;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging < 0)
                return;

            auto& slot = slots[(size_t) dragging];
            const float span = juce::jmax (1.0f, (float) slot.depthBar.getWidth() * 0.5f);
            const float fine = e.mods.isShiftDown() ? (1.0f / 6.0f) : 1.0f;

            setDepth (dragging, dragStart + (float) e.getDistanceFromDragStartX() / span * fine);
        }

        void mouseUp (const juce::MouseEvent&) override { dragging = -1; }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const int slot = slotAt (e.getPosition());

            if (slot >= 0)
                setDepth (slot, 0.0f);
        }

    private:
        struct Slot
        {
            juce::ValueTree tree;
            PillButton*   source = nullptr;
            PillButton*   target = nullptr;
            ToggleSwitch* enable = nullptr;
            juce::Rectangle<int> row, depthBar;
            float shownDepth = 0.0f;
        };

        static constexpr int columnGap   = 12;
        static constexpr int sourcePillW = 92;
        static constexpr int targetPillW = 150;
        static constexpr int depthBarW   = 108;

        static const juce::StringArray& sourceNames()
        {
            // The modulation sources named by the master spec.
            static const juce::StringArray names {
                "NONE", "LFO 1", "LFO 2", "ENV 1", "ENV 2", "VELOCITY", "KEY TRACK",
                "MOD WHEEL", "AFTERTOUCH", "BREATH", "PULSE", "ORGANIC RANDOM",
                "MEMORY", "MOTION", "WORLD", "ALTER"
            };

            return names;
        }

        void fetchTree()
        {
            matrixTree = processor.getStateManager().group (ids::MODMATRIX);

            while (matrixTree.getNumChildren() < matrixSlots)
            {
                juce::ValueTree slot (ids::MODSLOT);
                slot.setProperty (ids::modSource,  "NONE", nullptr);
                slot.setProperty (ids::modTarget,  "",     nullptr);
                slot.setProperty (ids::modDepth,   0.0,    nullptr);
                slot.setProperty (ids::modEnabled, false,  nullptr);
                matrixTree.addChild (slot, -1, nullptr);
            }

            for (int i = 0; i < matrixSlots; ++i)
                slots[(size_t) i].tree = matrixTree.getChild (i);
        }

        int slotAt (juce::Point<int> p) const
        {
            for (int i = 0; i < matrixSlots; ++i)
                if (slots[(size_t) i].depthBar.expanded (0, 6).contains (p))
                    return i;

            return -1;
        }

        void setDepth (int index, float value)
        {
            auto& slot = slots[(size_t) index];

            if (! slot.tree.isValid())
                return;

            slot.shownDepth = juce::jlimit (-1.0f, 1.0f, value);
            slot.tree.setProperty (ids::modDepth, (double) slot.shownDepth, nullptr);
            repaint();
        }

        void chooseSource (int index)
        {
            auto& slot = slots[(size_t) index];

            if (! slot.tree.isValid())
                return;

            const auto current = slot.tree.getProperty (ids::modSource).toString();
            const auto& names = sourceNames();

            juce::PopupMenu m;
            m.addSectionHeader ("SOURCE");

            for (int i = 0; i < names.size(); ++i)
                m.addItem (i + 1, names[i], true, names[i] == current);

            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slot.source),
                             [this, index] (int result)
                             {
                                 if (result <= 0)
                                     return;

                                 auto& s = slots[(size_t) index];

                                 if (s.tree.isValid())
                                     s.tree.setProperty (ids::modSource, sourceNames()[result - 1], nullptr);

                                 refreshFromState();
                             });
        }

        void chooseTarget (int index)
        {
            auto& slot = slots[(size_t) index];

            if (! slot.tree.isValid())
                return;

            // The target list is the parameter table itself - see
            // PageSurface::buildParameterMenu.  The SEQ lanes use the same one.
            juce::PopupMenu menu;
            PageSurface::buildParameterMenu (menu, slot.tree.getProperty (ids::modTarget).toString());

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slot.target),
                                [this, index] (int result)
                                {
                                    if (result <= 0)
                                        return;

                                    auto& s = slots[(size_t) index];

                                    if (! s.tree.isValid())
                                        return;

                                    s.tree.setProperty (ids::modTarget,
                                                        PageSurface::parameterMenuResult (result), nullptr);
                                    refreshFromState();
                                });
        }

        NacarProcessor& processor;
        PageSurface& surface;
        juce::ValueTree matrixTree;
        std::array<Slot, (size_t) matrixSlots> slots;
        juce::OwnedArray<juce::Component> owned;

        int   dragging  = -1;
        float dragStart = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModMatrix)
    };

    // =======================================================================
    //  ModPage
    // =======================================================================
    ModPage::ModPage (NacarProcessor& p, EditorHost& h)
        : PageSurface (p, h, "MODULATION",
                       juce::String::fromUTF8 ("LFOS \xc2\xb7 ENVELOPES \xc2\xb7 BREATH \xc2\xb7 PULSE \xc2\xb7 MATRIX"),
                       Material::ceramic)
    {
        setTickHz (12);

        lfo[0] = { PID::lfo1Shape, PID::lfo1Sync, PID::lfo1Division,
                   PID::lfo1Rate,  PID::lfo1Depth, PID::lfo1Phase, "LFO 1" };
        lfo[1] = { PID::lfo2Shape, PID::lfo2Sync, PID::lfo2Division,
                   PID::lfo2Rate,  PID::lfo2Depth, PID::lfo2Phase, "LFO 2" };

        // Seven shapes will not fit a segmented control at full width with
        // their parameter names, so the segments carry short forms; the order
        // is the parameter's own order, which is what bindTo() maps.
        const juce::StringArray shapeLabels { "SIN", "TRI", "SAW+", "SAW-", "SQR", "RND", "S-RND" };

        for (auto& l : lfo)
        {
            l.shapeControl = &addSegmented (l.shape, shapeLabels);
            l.syncLock     = &addLock ("SYNC", l.sync);

            // Fourteen tempo divisions is far too many for a segmented control,
            // and a juce::ComboBox would be drawn by LookAndFeel_V4 and read as
            // a settings dialog.  A house pill opening the house PopupMenu it
            // is - see ChoicePill in PageSurface.h.
            l.divisionPill = &addChoicePill (l.division);

            l.rateKnob  = &addKnob (l.rate,  "RATE");
            l.depthKnob = &addKnob (l.depth, "DEPTH");
            l.phaseKnob = &addKnob (l.phase, "PHASE");
        }

        env[0] = { PID::ampAttack,  PID::ampDecay,  PID::ampSustain,  PID::ampRelease,  "AMP ENV" };
        env[1] = { PID::env1Attack, PID::env1Decay, PID::env1Sustain, PID::env1Release, "MOD ENV 1" };
        env[2] = { PID::env2Attack, PID::env2Decay, PID::env2Sustain, PID::env2Release, "MOD ENV 2" };

        addKnob (env[0].attack,  "ATTACK");
        addKnob (env[0].decay,   "DECAY");
        addKnob (env[0].sustain, "SUSTAIN");
        addKnob (env[0].release, "RELEASE");
        velocityKnob = &addKnob (PID::ampVelocity, "VELOCITY");

        for (int i = 1; i < 3; ++i)
        {
            addKnob (env[(size_t) i].attack,  "ATTACK");
            addKnob (env[(size_t) i].decay,   "DECAY");
            addKnob (env[(size_t) i].sustain, "SUSTAIN");
            addKnob (env[(size_t) i].release, "RELEASE");
        }

        // BREATH.  Not an LFO: a bounded random walk (master spec section 57).
        breathLock = &addLock ("BREATH", PID::breathOn);
        addKnob (PID::breathAmount, "AMOUNT");
        addKnob (PID::breathSpeed,  "SPEED");
        addKnob (PID::breathRandom, "RANDOM");
        addKnob (PID::breathShape,  "SHAPE");

        // PULSE.
        pulsePower       = &addPower (PID::pulseOn);
        pulseSourceSeg   = &addSegmented (PID::pulseSource);
        pulseDivisionSeg = &addSegmented (PID::pulseDivision);

        addKnob (PID::pulseDepth,   "DEPTH");
        addKnob (PID::pulseAttack,  "ATTACK");
        addKnob (PID::pulseRelease, "RELEASE");
        addKnob (PID::pulseSmooth,  "SMOOTH");

        addKnob (PID::pulseToVolume, "VOLUME");
        addKnob (PID::pulseToFilter, "FILTER");
        addKnob (PID::pulseToSpace,  "SPACE");
        addKnob (PID::pulseToWidth,  "WIDTH");
        addKnob (PID::pulseToMemory, "MEMORY");

        matrix = std::make_unique<ModMatrix> (processor, *this);
        addAndMakeVisible (*matrix);

        for (int i = 0; i < 2; ++i)
            applySyncVisibility (i);
    }

    ModPage::~ModPage() = default;

    void ModPage::applySyncVisibility (int index)
    {
        auto& l = lfo[(size_t) index];
        const bool synced = params.flag (l.sync);

        // One slot, one rate control: the free-running knob when the LFO runs
        // free, the tempo division when it is locked.  Never both, never a
        // control that writes nowhere.
        l.rateKnob->setVisible (! synced);
        l.divisionPill->setVisible (synced);
        l.syncState = synced ? 1 : 0;
    }

    void ModPage::tick()
    {
        for (int i = 0; i < 2; ++i)
        {
            auto& l = lfo[(size_t) i];

            if ((params.flag (l.sync) ? 1 : 0) != l.syncState)
                applySyncVisibility (i);

            const float hash = (float) params.choice (l.shape) * 7.31f
                             + params.raw (l.depth) * 3.17f
                             + params.raw (l.phase) * 1.13f;

            if (std::abs (hash - l.previewHash) > 1.0e-5f)
            {
                l.previewHash = hash;
                repaint (l.preview);
            }
        }

        for (auto& e : env)
        {
            const float hash = params.raw (e.attack) * 1.7f + params.raw (e.decay) * 2.3f
                             + params.raw (e.sustain) * 3.1f + params.raw (e.release) * 4.7f;

            if (std::abs (hash - e.previewHash) > 1.0e-5f)
            {
                e.previewHash = hash;
                repaint (e.preview);
            }
        }

        {
            const float hash = params.raw (PID::breathAmount) * 1.7f
                             + params.raw (PID::breathSpeed)  * 2.9f
                             + params.raw (PID::breathRandom) * 3.7f
                             + params.raw (PID::breathShape)  * 5.3f;

            if (std::abs (hash - breathHash) > 1.0e-5f)
            {
                breathHash = hash;
                repaint (breathPreview);
            }
        }

        if (matrix != nullptr)
            matrix->refreshFromState();
    }

    // -----------------------------------------------------------------------
    void ModPage::resized()
    {
        beginLayout();

        auto content = contentArea();

        auto rowA = content.removeFromTop (rowLfoH);
        content.removeFromTop (gapPx);
        auto rowB = content.removeFromTop (rowEnvH);
        content.removeFromTop (gapPx);
        auto rowC = content.removeFromTop (rowModH);
        content.removeFromTop (gapPx);
        matrixBounds = content.removeFromTop (rowMatrixH);

        // -- LFOs -----------------------------------------------------------
        lfo[0].bounds = rowA.removeFromLeft ((rowA.getWidth() - gapPx) / 2);
        rowA.removeFromLeft (gapPx);
        lfo[1].bounds = rowA;

        for (auto& l : lfo)
        {
            auto in = inside (l.bounds);

            l.shapeControl->setBounds (in.removeFromTop ((int) page::rowH));
            in.removeFromTop (4);

            auto middle = in.removeFromTop (previewH);
            l.preview = middle.removeFromRight (previewW);

            l.syncLock->setBounds (middle.getX(), middle.getCentreY() - 9,
                                   (int) l.syncLock->preferredWidth(), 18);

            in.removeFromTop (4);

            auto knobRow = in.removeFromTop (64);
            const int cell = knobRow.getWidth() / 3;

            l.rateCell = knobRow.removeFromLeft (cell);
            placeKnob (*l.rateKnob, l.rateCell, lfoKnobR);
            l.divisionPill->setBounds (juce::Rectangle<int> (78, 22).withCentre (l.rateCell.getCentre()));

            placeKnob (*l.depthKnob, knobRow.removeFromLeft (cell), lfoKnobR);
            placeKnob (*l.phaseKnob, knobRow, lfoKnobR);

            skipKnobs (3);
        }

        // -- Envelopes ------------------------------------------------------
        env[0].bounds = rowB.removeFromLeft (ampSectionW);
        rowB.removeFromLeft (gapPx);
        env[1].bounds = rowB.removeFromLeft (envSectionW);
        rowB.removeFromLeft (gapPx);
        env[2].bounds = rowB;

        {
            auto in = inside (env[0].bounds);

            auto previewRow = in.removeFromTop (62);
            auto velocityCell = previewRow.removeFromRight (64);
            env[0].preview = previewRow.withTrimmedRight (6);

            in.removeFromTop (6);
            knobGrid (in.removeFromTop (62), 4, envKnobR, 4);

            placeKnob (*velocityKnob, velocityCell, envKnobR);
            skipKnobs (1);
        }

        for (int i = 1; i < 3; ++i)
        {
            auto in = inside (env[(size_t) i].bounds);

            env[(size_t) i].preview = in.removeFromTop (62);
            in.removeFromTop (6);
            knobGrid (in.removeFromTop (62), 4, envKnobR, 4);
        }

        // -- Breath / Pulse -------------------------------------------------
        breathBounds = rowC.removeFromLeft (breathSectionW);
        rowC.removeFromLeft (gapPx);
        pulseBounds = rowC;

        {
            auto in = inside (breathBounds);

            auto lockRow = in.removeFromTop (20);
            breathLock->setBounds (lockRow.getX(), lockRow.getY(),
                                   (int) breathLock->preferredWidth(), 20);

            in.removeFromTop (6);
            knobGrid (in.removeFromTop (60), 4, breathKnobR, 4);

            in.removeFromTop (8);
            breathPreview = in.removeFromTop (70);
        }

        {
            auto in = inside (pulseBounds);

            pulsePower->setBounds (pulseBounds.getRight() - 34, pulseBounds.getY() + 4, 22, 22);

            auto segRow = in.removeFromTop (22);
            pulseSourceSeg->setBounds (segRow.removeFromLeft (132));
            segRow.removeFromLeft (10);
            pulseDivisionSeg->setBounds (segRow.removeFromLeft (324));

            in.removeFromTop (6);
            knobGrid (in.removeFromTop (62), 4, pulseKnobR, 4);

            in.removeFromTop (4);
            pulseDestCaption = in.removeFromTop (12);
            pulseDestRule    = in.removeFromTop (1);
            in.removeFromTop (3);

            knobGrid (in.removeFromTop (58), 5, pulseDestR, 5);
        }

        // -- Matrix ---------------------------------------------------------
        matrix->setBounds (inside (matrixBounds));
    }

    // -----------------------------------------------------------------------
    void ModPage::paint (juce::Graphics& g)
    {
        paintChrome (g);

        for (auto& l : lfo)
        {
            section (g, l.bounds.toFloat(), l.caption, icons::Icon::wave3);
            drawLfoPreview (g, l.preview.toFloat(), params.choice (l.shape),
                            params.raw (l.depth), params.raw (l.phase));
        }

        for (auto& e : env)
        {
            section (g, e.bounds.toFloat(), e.caption, icons::Icon::triangle);
            drawEnvelopePreview (g, e.preview.toFloat(),
                                 params.raw (e.attack), params.raw (e.decay),
                                 params.raw (e.sustain), params.raw (e.release));
        }

        // Breath says what it is in its own caption: it is not an LFO.
        section (g, breathBounds.toFloat(), "BREATH   ORGANIC, NON-REPEATING", icons::Icon::wave3);
        drawBreathPreview (g, breathPreview.toFloat(),
                           params.raw (PID::breathAmount), params.raw (PID::breathSpeed),
                           params.raw (PID::breathRandom), params.raw (PID::breathShape));

        section (g, pulseBounds.toFloat(), "PULSE", icons::Icon::meterBars);

        // The destination group teaches the behaviour (master spec section 83):
        // the point of Pulse is not that it ducks, it is what it ducks.
        text (g, "A KICK MAKES IT QUIETER, DARKER, NARROWER, DRIER",
              { (float) pulseDestCaption.getRight(), (float) pulseDestCaption.getBottom() },
              page::noteSize, 0.10f, inkFaint(),
              juce::Justification::right, (float) pulseDestCaption.getWidth());

        theme::hairline (g, { (float) pulseDestRule.getX(), (float) pulseDestRule.getY() },
                            { (float) pulseDestRule.getRight(), (float) pulseDestRule.getY() },
                         theme::ceramicEdge);

        section (g, matrixBounds.toFloat(),
                 "MOD MATRIX   EIGHT SLOTS   SAVED WITH THE PRESET", icons::Icon::link);
    }
}
