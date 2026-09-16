#include "AtmospherePanel.h"

namespace nacar::ui
{
    using namespace layout;

    // -----------------------------------------------------------------------
    //  Numbers UI spec section 8 states in words but Layout.h (frozen) does
    //  not carry as coordinates.
    // -----------------------------------------------------------------------
    static constexpr float kPanelInset     =  1.0f;   // so the contact shadow reads inside our bounds
    static constexpr float kDividerInset   = 20.0f;   // spec: hairlines span x ~20 to width - 20
    static constexpr float kIconSize       = 22.0f;   // the planet / wave / triangle glyphs
    static constexpr float kToggleWidth    = 30.0f;   // SHADOW's pill switch, Size::large
    static constexpr float kToggleHeight   = 16.0f;
    static constexpr float kRowAbove       = 10.0f;   // a list row's hit area, above its baseline
    static constexpr float kRowBelow       =  6.0f;   // ... and below it
    static constexpr float kListRightInset = 20.0f;
    static constexpr float kDotRadius      =  3.0f;
    static constexpr float kDotGlow        =  9.0f;
    static constexpr float kOffAlpha       =  0.45f;  // a module whose enable is off

    // =======================================================================
    //  Construction
    // =======================================================================
    AtmospherePanel::AtmospherePanel (NacarProcessor& p, EditorHost& hostToUse)
        : processor (p),
          editorTree (p.getStateManager().group (ids::EDITOR))
    {
        // The atmosphere panel asks nothing of the chassis: it owns four
        // self-contained modules and persists its own selection.  EditorHost is
        // part of the region contract, so it is accepted and deliberately not
        // stored rather than kept as a reference nothing reads.
        juce::ignoreUnused (hostToUse);

        describeModules();

        for (auto& m : modules)
            buildModule (m);
    }

    AtmospherePanel::~AtmospherePanel()
    {
        // Each attachment refers to its button, so it has to go first.
        for (auto& m : modules)
            m.powerAttachment.reset();
    }

    void AtmospherePanel::describeModules()
    {
        auto& aura   = modules[0];
        auto& shadow = modules[1];
        auto& breath = modules[2];
        auto& patina = modules[3];

        aura.spec      = atmos::aura;
        aura.title     = "AURA";
        aura.subtitle  = "SPACE & ENVIRONMENT";
        aura.icon      = icons::Icon::planet;
        aura.enable    = PID::auraOn;
        aura.selectionProperty = ids::auraSelectedParam;
        aura.rows = { { "SIZE",     PID::auraSize     },
                      { "DISTANCE", PID::auraDistance },
                      { "FOG",      PID::auraFog      },
                      { "DECAY",    PID::auraDecay    },
                      { "LIGHT",    PID::auraLight    } };

        // SHADOW's leading element is a pill toggle in the reference image, not
        // a glyph - it is the module's on/off.  It and the power ring are bound
        // to the same parameter, so the two always agree.
        shadow.spec     = atmos::shadow;
        shadow.title    = "SHADOW";
        shadow.subtitle = "ATMOSPHERIC DUPLICATE";
        shadow.iconIsToggle = true;
        shadow.enable   = PID::shadowOn;
        shadow.selectionProperty = ids::shadowSelectedParam;
        shadow.rows = { { "LENGTH",   PID::shadowLength   },
                        { "DISTANCE", PID::shadowDistance },
                        { "BLUR",     PID::shadowBlur     },
                        { "PITCH",    PID::shadowPitch    },
                        { "LEVEL",    PID::shadowLevel    } };

        breath.spec     = atmos::breath;
        breath.title    = "BREATH";
        breath.subtitle = "ORGANIC MOVEMENT";
        breath.icon     = icons::Icon::wave3;
        breath.enable   = PID::breathOn;
        breath.selectionProperty = ids::breathSelectedParam;
        breath.rows = { { "AMOUNT", PID::breathAmount },
                        { "SPEED",  PID::breathSpeed  },
                        { "RANDOM", PID::breathRandom },
                        { "SHAPE",  PID::breathShape  } };

        patina.spec     = atmos::patina;
        patina.title    = "PATINA";
        patina.subtitle = "TEXTURE & AGE";
        patina.icon     = icons::Icon::triangle;
        patina.enable   = PID::patinaOn;
        patina.selectionProperty = ids::patinaSelectedParam;
        patina.rows = { { "TONE",  PID::patinaTone  },
                        { "NOISE", PID::patinaNoise },
                        { "WEAR",  PID::patinaWear  },
                        { "DRIFT", PID::patinaDrift } };

        for (const auto& m : modules)
            jassert ((int) m.rows.size() == m.spec.numParams);
    }

    void AtmospherePanel::buildModule (Module& m)
    {
        const auto& enableDef = ParameterRegistry::definition (m.enable);
        const auto enableTip = juce::String (enableDef.name) + "  -  " + juce::String (enableDef.tooltip);

        // -- power ring ------------------------------------------------------
        // Violet here, not mint: the atmosphere rings are violet in the
        // reference, where the FX chain's are mint.
        m.power = std::make_unique<PowerButton> (PowerButton::Tint::violet);
        m.power->setClickingTogglesState (true);
        m.power->setTooltip (enableTip);
        m.power->onStateChange = [this, &m] { powerChanged (m); };
        addAndMakeVisible (*m.power);

        if (auto* apvts = processor.getParameters().state())
            m.powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                                    (*apvts, ParameterRegistry::idOf (m.enable), *m.power);

        m.powered = m.power->getToggleState();

        // -- SHADOW's header toggle ------------------------------------------
        if (m.iconIsToggle)
        {
            m.toggle = std::make_unique<ToggleSwitch> (ToggleSwitch::Size::large);
            m.toggle->bindTo (processor.getParameters(), m.enable);
            m.toggle->setTooltip (enableTip);
            addAndMakeVisible (*m.toggle);
        }

        // -- one knob per parameter, stacked ---------------------------------
        for (const auto& row : m.rows)
        {
            auto knob = std::make_unique<NacarKnob> (processor.getParameters(), row.pid);

            // A parameter whose range goes negative is bipolar, so its arc
            // sweeps outward from 12 o'clock.  That resolves to exactly the
            // three the spec names: AURA LIGHT, SHADOW PITCH, PATINA TONE.
            if (ParameterRegistry::definition (row.pid).minValue < 0.0f)
                knob->setBipolar (true);

            addChildComponent (*knob);
            m.knobs.push_back (std::move (knob));
        }

        m.selected = juce::jlimit (0, juce::jmax (0, (int) m.rows.size() - 1),
                                   (int) editorTree.getProperty (m.selectionProperty, 0));

        if (! m.knobs.empty())
            m.knobs[(size_t) m.selected]->setVisible (true);

        const float alpha = moduleAlpha (m);

        for (auto& knob : m.knobs)
            knob->setAlpha (alpha);
    }

    // =======================================================================
    //  State
    // =======================================================================
    void AtmospherePanel::setSelectedRow (Module& m, int row)
    {
        row = juce::jlimit (0, juce::jmax (0, (int) m.rows.size() - 1), row);

        if (row == m.selected || m.knobs.empty())
            return;

        m.knobs[(size_t) m.selected]->setVisible (false);
        m.selected = row;
        m.knobs[(size_t) m.selected]->setVisible (true);

        // The dot is real state: which parameter the module's knob is on
        // survives closing the editor.
        editorTree.setProperty (m.selectionProperty, row, nullptr);

        repaint();
    }

    void AtmospherePanel::powerChanged (Module& m)
    {
        const bool now = m.power->getToggleState();

        if (now == m.powered)
            return;

        m.powered = now;

        const float alpha = moduleAlpha (m);

        for (auto& knob : m.knobs)
            knob->setAlpha (alpha);

        repaint();
    }

    float AtmospherePanel::moduleAlpha (const Module& m) const
    {
        return m.powered ? 1.0f : kOffAlpha;
    }

    // =======================================================================
    //  Geometry
    // =======================================================================
    juce::Rectangle<float> AtmospherePanel::rowBounds (const Module& m, int row) const
    {
        const float baseline = m.spec.listTop + (float) row * m.spec.listSpacing;
        const float left     = atmos::dotX - kDotRadius - 3.0f;

        return { left, baseline - kRowAbove,
                 (float) getWidth() - kListRightInset - left, kRowAbove + kRowBelow };
    }

    juce::Rectangle<float> AtmospherePanel::knobBoundsFor (const Module& m) const
    {
        return centredSquare (m.spec.knob, m.spec.knobRadius);
    }

    void AtmospherePanel::resized()
    {
        for (auto& m : modules)
        {
            if (m.power != nullptr)
                m.power->setBounds (centredSquare (m.spec.power, atmos::powerR).toNearestInt());

            if (m.toggle != nullptr)
                m.toggle->setBounds (juce::Rectangle<float> (kToggleWidth, kToggleHeight)
                                         .withCentre (m.spec.icon).toNearestInt());

            const auto knobArea = knobBoundsFor (m).toNearestInt();

            for (auto& knob : m.knobs)
                knob->setBounds (knobArea);
        }
    }

    // =======================================================================
    //  Paint
    // =======================================================================
    void AtmospherePanel::paint (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat().reduced (kPanelInset);

        theme::contactShadow (g, b, radiusPanel);
        theme::ceramicSurface (g, b, radiusPanel);

        // -- module dividers -------------------------------------------------
        for (const float y : atmos::divider)
        {
            const float left  = kDividerInset;
            const float right = (float) getWidth() - kDividerInset;

            theme::hairline (g, { left, y }, { right, y }, theme::ceramicEdge);

            // The seam is machined, so the surface below it catches the light.
            theme::hairline (g, { left, y + 1.0f }, { right, y + 1.0f }, theme::ceramicLight);
        }

        for (const auto& m : modules)
            paintModule (g, m);
    }

    void AtmospherePanel::paintModule (juce::Graphics& g, const Module& m) const
    {
        const auto& s = m.spec;
        const float alpha = moduleAlpha (m);

        // -- icon -------------------------------------------------------------
        // SHADOW's leading element is the ToggleSwitch child, so there is no
        // glyph to draw for it.
        if (! m.iconIsToggle)
            icons::draw (g, m.icon, centredSquare (s.icon, kIconSize * 0.5f), theme::ink, 1.6f);

        // -- name and subtitle ------------------------------------------------
        // These stay at full strength when the module is off: the panel should
        // always read as four modules, with only the controls going quiet.
        ceramicLabel (g, m.title, { atmos::titleX, s.titleBaseline },
                      atmos::titleSize, atmos::titleTrack, theme::ink);

        ceramicLabel (g, m.subtitle, { atmos::titleX, s.subtitleBaseline },
                      atmos::subSize, atmos::subTrack, theme::inkFaint);

        // -- the rule down the parameter list ---------------------------------
        const int rows = (int) m.rows.size();

        if (rows == 0)
            return;

        const float firstBaseline = s.listTop;
        const float lastBaseline  = s.listTop + (float) (rows - 1) * s.listSpacing;

        theme::hairline (g, { atmos::ruleX, firstBaseline - kRowAbove },
                         { atmos::ruleX, lastBaseline + kRowBelow },
                         theme::ceramicEdge.withMultipliedAlpha (alpha));

        // -- the rows ----------------------------------------------------------
        for (int i = 0; i < rows; ++i)
        {
            const float baseline = s.listTop + (float) i * s.listSpacing;
            const bool  isSelected = (i == m.selected);

            const auto ink = (isSelected || i == m.hoverRow) ? theme::ink : theme::inkMuted;

            ceramicLabel (g, m.rows[(size_t) i].name, { atmos::listX, baseline },
                          atmos::listSize, atmos::listTrack, ink.withMultipliedAlpha (alpha));

            if (! isSelected)
                continue;

            // The dot is the whole point of the list: it says which parameter
            // the big knob is currently driving.
            const juce::Point<float> dot { atmos::dotX, baseline - atmos::listSize * 0.35f };

            theme::glow (g, dot, kDotGlow, theme::violet, 0.40f * alpha);

            g.setColour (theme::violet.withMultipliedAlpha (alpha));
            g.fillEllipse (centredSquare (dot, kDotRadius));
        }
    }

    // =======================================================================
    //  Mouse - the parameter list
    // =======================================================================
    void AtmospherePanel::mouseDown (const juce::MouseEvent& e)
    {
        for (auto& m : modules)
            for (int i = 0; i < (int) m.rows.size(); ++i)
                if (rowBounds (m, i).contains (e.position))
                {
                    setSelectedRow (m, i);
                    return;
                }
    }

    void AtmospherePanel::mouseMove (const juce::MouseEvent& e)
    {
        bool changed = false;
        bool overRow = false;

        for (auto& m : modules)
        {
            int hover = -1;

            for (int i = 0; i < (int) m.rows.size(); ++i)
                if (rowBounds (m, i).contains (e.position))
                    hover = i;

            overRow = overRow || (hover >= 0);

            if (hover != m.hoverRow)
            {
                m.hoverRow = hover;
                changed = true;
            }
        }

        setMouseCursor (overRow ? juce::MouseCursor::PointingHandCursor
                                : juce::MouseCursor::NormalCursor);

        if (changed)
            repaint();
    }

    void AtmospherePanel::mouseExit (const juce::MouseEvent&)
    {
        bool changed = false;

        for (auto& m : modules)
            if (m.hoverRow != -1)
            {
                m.hoverRow = -1;
                changed = true;
            }

        if (changed)
            repaint();
    }
}
