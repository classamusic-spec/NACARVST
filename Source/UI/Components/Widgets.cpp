#include "Widgets.h"

#include <cmath>
#include <optional>

namespace nacar::ui
{
    // -----------------------------------------------------------------------
    //  Widget-internal metrics.
    //
    //  Layout.h owns every coordinate in the chassis and is frozen.  The
    //  numbers below are not coordinates - they are the internal proportions of
    //  the widgets themselves, which nothing outside this file can position -
    //  so they live next to the painting code that uses them.
    // -----------------------------------------------------------------------

    // Knob (UI spec sections 4 and 10)
    static constexpr float knobArcSpanDeg    = 140.0f;  ///< +/- from 12 o'clock, so 280 deg total
    static constexpr float knobGrooveGap     = 1.5f;    ///< clearance between the cap and the seat
    static constexpr float knobGrooveWidth   = 5.0f;
    static constexpr float knobSeatAllowance = knobGrooveGap + knobGrooveWidth + 0.5f;
    static constexpr float knobArcWidth      = 3.4f;
    static constexpr float knobIndicatorIn   = 0.45f;   ///< fraction of the cap radius
    static constexpr float knobIndicatorOut  = 0.88f;
    static constexpr float knobIndicatorW    = 2.0f;
    static constexpr float knobLabelGap      = 16.0f;   ///< cap edge to label baseline
    static constexpr float knobLegendGap     = 8.0f;    ///< label baseline to legend strip
    static constexpr float knobLegendHeight  = 11.0f;

    // Buttons
    static constexpr float pillIconGap       = 8.0f;
    static constexpr float iconSquareCorner  = 8.0f;

    // Segmented control
    static constexpr float segmentCornerRatio = 0.28f;  ///< 22 px -> 6, 31 px -> 8, as in the spec
    static constexpr float segmentInset       = 2.0f;

    // Switch
    static constexpr float switchKnobInset    = 2.0f;

    // Hairline slider
    static constexpr float hairlineCapR       = 5.0f;

    // Generation selector (UI spec section 4: 11 px violet halo)
    static constexpr float genGlowRadius      = 11.0f;
    static constexpr int   numGenerations     = 4;

    // Inline value entry
    static constexpr int   valueEntryWidth    = 84;
    static constexpr int   valueEntryHeight   = 22;

    // =======================================================================
    //  Shared helpers
    // =======================================================================
    namespace
    {
        /** Real-unit value for a 0..1 position, honouring the parameter's skew,
            interval and choice count. */
        float realFrom (const ParameterRegistry& r, PID p, float normalised)
        {
            if (auto* rp = r.parameter (p))
                return rp->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, normalised));

            return ParameterRegistry::defaultRealValue (p);
        }

        float normalisedFrom (const ParameterRegistry& r, PID p, float realValue)
        {
            if (auto* rp = r.parameter (p))
                return rp->convertTo0to1 (realValue);

            return 0.0f;
        }

        /** Live choice / flag index, safe before the registry has been attached. */
        int boundIndex (const ParameterRegistry& r, PID p, int fallback)
        {
            if (r.parameter (p) != nullptr)
                return (int) r.raw (p);

            return fallback;
        }

        float clampToRange (const ParameterRegistry& r, PID p, float realValue)
        {
            if (auto* rp = r.parameter (p))
            {
                const auto& range = rp->getNormalisableRange();
                return range.snapToLegalValue (juce::jlimit (range.start, range.end, realValue));
            }

            return realValue;
        }

        /** Hover lifts a surface, pressing darkens it.  Applied to the fill only:
            nothing in this interface moves or resizes under the pointer. */
        juce::Colour hoverLift (juce::Colour c, bool highlighted, bool down)
        {
            if (down)        return c.darker (0.06f);
            if (highlighted) return c.brighter (0.04f);

            return c;
        }

        /** Point on a dial, angle measured clockwise from 12 o'clock. */
        juce::Point<float> pointOnDial (juce::Point<float> centre, float radius, float angle)
        {
            return { centre.x + std::sin (angle) * radius,
                     centre.y - std::cos (angle) * radius };
        }

        juce::Rectangle<float> squareAt (juce::Point<float> centre, float halfSize)
        {
            return juce::Rectangle<float> (halfSize * 2.0f, halfSize * 2.0f).withCentre (centre);
        }

        void strokeLine (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b,
                         float thickness, juce::Colour c)
        {
            // Graphics::drawLine has butt ends; the knob indicator and the arc
            // both want round ones, so they go through a stroked path.
            juce::Path p;
            p.startNewSubPath (a);
            p.lineTo (b);

            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }

        juce::String tooltipFor (PID pid)
        {
            const auto& d = ParameterRegistry::definition (pid);
            return juce::String (d.name) + "\n" + juce::String (d.tooltip);
        }
    }

    // =======================================================================
    //  ParamAttachment
    //
    //  Deliberately empty.  Value tracking is done by polling
    //  ParameterRegistry::normalised() from ParamControl's timer: that read is
    //  lock-free, it costs one relaxed atomic load per control per frame, and
    //  it cannot re-enter the way an APVTS::Listener does when a control writes
    //  the very parameter it is listening to.  The type exists because
    //  Widgets.h declares a unique_ptr to it, and unique_ptr needs a complete
    //  type where ParamControl's destructor is compiled - here.
    // =======================================================================
    class ParamAttachment
    {
    };

    // =======================================================================
    //  Inline value entry
    //
    //  ParamControl has no member for a TextEditor and Widgets.h is frozen, so
    //  the editor lives here.  Only one can be open at a time in any case: it
    //  holds the keyboard focus until it is dismissed.
    // =======================================================================
    namespace
    {
        std::unique_ptr<juce::TextEditor> valueEntry;
        juce::Component::SafePointer<juce::Component> valueEntryOwner;

        void closeValueEntry (bool deferred)
        {
            if (valueEntry == nullptr)
                return;

            valueEntryOwner = nullptr;

            if (deferred)
            {
                // Called from one of the editor's own callbacks, so the stack is
                // still inside it: let the message loop do the deleting.
                juce::MessageManager::callAsync ([] { valueEntry.reset(); });
                return;
            }

            valueEntry.reset();
        }

        void closeValueEntryOwnedBy (juce::Component& owner)
        {
            if (valueEntryOwner == &owner)
                closeValueEntry (false);
        }

        /** Reads typed text back into real units.  Choices accept their own name
            or an index, booleans accept ON / OFF, and the unit the formatter
            appended is simply ignored. */
        std::optional<float> parseTypedValue (PID pid, const juce::String& text)
        {
            const auto& d = ParameterRegistry::definition (pid);
            const auto trimmed = text.trim();

            if (trimmed.isEmpty())
                return {};

            if (d.kind == ParamKind::boolean)
            {
                if (trimmed.equalsIgnoreCase ("on")  || trimmed.equalsIgnoreCase ("true"))  return 1.0f;
                if (trimmed.equalsIgnoreCase ("off") || trimmed.equalsIgnoreCase ("false")) return 0.0f;
            }

            if (d.kind == ParamKind::choice)
            {
                const auto choices = ParameterRegistry::choicesOf (pid);

                for (int i = 0; i < choices.size(); ++i)
                    if (choices[i].equalsIgnoreCase (trimmed))
                        return (float) i;
            }

            const auto numeric = trimmed.retainCharacters ("0123456789+-.,");

            if (numeric.isEmpty())
                return {};

            float v = numeric.replaceCharacter (',', '.').getFloatValue();

            // The formatter shows 0..1 ranges as percentages and short times as
            // milliseconds, so accept what it printed.
            const juce::String unit (d.unit);

            if (unit == "%")
                v *= 0.01f;
            else if (unit == "s" && v > d.maxValue && v * 0.001f <= d.maxValue)
                v *= 0.001f;

            return v;
        }
    }

    // =======================================================================
    //  ParamControl
    // =======================================================================
    ParamControl::ParamControl (const ParameterRegistry& registry, PID parameterID)
        : params (registry), pid (parameterID)
    {
        displayValue = targetValue = params.normalised (pid);

        refreshTooltip();
        startTimerHz (30);
    }

    ParamControl::~ParamControl()
    {
        stopTimer();
        closeValueEntryOwnedBy (*this);
    }

    float ParamControl::getRealValue() const noexcept
    {
        // Round-tripped through the parameter's own range, which is exact.
        return realFrom (params, pid, params.normalised (pid));
    }

    void ParamControl::setDragSensitivity (float pixels) noexcept
    {
        pixelsForFullRange = juce::jmax (1.0f, pixels);
    }

    void ParamControl::refreshTooltip()
    {
        const auto& d = ParameterRegistry::definition (pid);

        setTooltip (juce::String (d.name) + "\n"
                    + params.formatValue (pid) + "\n"
                    + juce::String (d.tooltip));
    }

    void ParamControl::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
        {
            showContextMenu();
            return;
        }

        // isCommandDown() is Cmd on macOS and Ctrl elsewhere, which also keeps
        // this off macOS's ctrl-click - that is already the context menu.
        if (e.mods.isCommandDown())
        {
            showValueEntry();
            return;
        }

        isDragging = true;
        dragStartValue = params.normalised (pid);
        displayValue = targetValue = dragStartValue;

        params.beginGesture (pid);
    }

    void ParamControl::mouseDrag (const juce::MouseEvent& e)
    {
        if (! isDragging)
            return;

        // Vertical only: 1 px is 1 / pixelsForFullRange of the range, and shift
        // divides that by six.  Shift is applied to the whole travel rather than
        // to the travel since it was pressed, so pressing it mid-drag rescales
        // from the drag origin - the same simplification JUCE's own sliders make.
        float delta = (float) -e.getDistanceFromDragStartY() / pixelsForFullRange;

        if (e.mods.isShiftDown())
            delta /= 6.0f;

        setNormalisedFromDrag (dragStartValue + delta);
    }

    void ParamControl::mouseUp (const juce::MouseEvent&)
    {
        if (! isDragging)
            return;

        isDragging = false;
        params.endGesture (pid);
        repaint();
    }

    void ParamControl::mouseDoubleClick (const juce::MouseEvent&)
    {
        // The second mouse-down already opened a gesture; close it before the
        // one-shot reset so the host never sees one gesture nested in another.
        if (isDragging)
        {
            isDragging = false;
            params.endGesture (pid);
        }

        const float def = ParameterRegistry::defaultRealValue (pid);

        params.setFromUI (pid, def);
        targetValue = normalisedFrom (params, pid, def);

        refreshTooltip();

        if (onValueChange)
            onValueChange();
    }

    void ParamControl::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
    {
        auto* rp = params.parameter (pid);

        if (rp == nullptr)
            return;

        const float dir = (std::abs (wheel.deltaY) > 0.0f ? wheel.deltaY : wheel.deltaX)
                          * (wheel.isReversed ? -1.0f : 1.0f);

        if (std::abs (dir) < 1.0e-6f)
            return;

        // One step: the parameter's own interval when it has one (choices,
        // voice counts), otherwise a fiftieth of the range.
        const auto& range = rp->getNormalisableRange();
        const float step  = range.interval > 0.0f ? range.interval
                                                  : (range.end - range.start) / 50.0f;

        const float next = getRealValue() + (dir > 0.0f ? step : -step);

        params.setFromUI (pid, clampToRange (params, pid, next));
        targetValue = params.normalised (pid);

        refreshTooltip();

        if (onValueChange)
            onValueChange();
    }

    void ParamControl::mouseEnter (const juce::MouseEvent&)
    {
        isHovered = true;
        refreshTooltip();
        repaint();
    }

    void ParamControl::mouseExit (const juce::MouseEvent&)
    {
        isHovered = false;
        repaint();
    }

    void ParamControl::setNormalisedFromDrag (float n)
    {
        n = juce::jlimit (0.0f, 1.0f, n);

        // Under the finger the control is not smoothed: it tracks exactly.
        targetValue = displayValue = n;

        params.setDuringGesture (pid, realFrom (params, pid, n));
        refreshTooltip();

        if (onValueChange)
            onValueChange();

        repaint();
    }

    void ParamControl::timerCallback()
    {
        targetValue = params.normalised (pid);

        if (isDragging)
            return;

        const float delta = targetValue - displayValue;
        const float step  = delta * 0.35f;

        if (std::abs (step) > 0.0005f)
        {
            displayValue += step;
            repaint();
        }
        else if (std::abs (delta) > 0.0f)
        {
            // Settle exactly, once, so the arc never stops a hair short of the
            // value the host is reporting.
            displayValue = targetValue;
            refreshTooltip();
            repaint();
        }
    }

    void ParamControl::showContextMenu()
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());

        menu.addItem (1, "Reset to Default");
        menu.addItem (2, "Type Value...");
        menu.addSeparator();
        menu.addItem (3, "MIDI Learn (not in V1)", false, false);

        const juce::Component::SafePointer<ParamControl> safe (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe] (int result)
        {
            if (safe == nullptr)
                return;

            if (result == 1)
            {
                const float def = ParameterRegistry::defaultRealValue (safe->pid);

                safe->params.setFromUI (safe->pid, def);
                safe->targetValue = normalisedFrom (safe->params, safe->pid, def);
                safe->refreshTooltip();

                if (safe->onValueChange)
                    safe->onValueChange();
            }
            else if (result == 2)
            {
                safe->showValueEntry();
            }
        });
    }

    void ParamControl::showValueEntry()
    {
        closeValueEntry (false);

        valueEntry = std::make_unique<juce::TextEditor>();
        auto& ed = *valueEntry;

        ed.setMultiLine (false);
        ed.setReturnKeyStartsNewLine (false);
        ed.setJustification (juce::Justification::centred);
        ed.setFont (theme::medium (12.0f));
        ed.setText (params.formatValue (pid), false);

        // The entry is a child of the control when there is room for it and of
        // the control's parent when there is not - RANDOM is 44 px across, and a
        // clipped text field is worse than one that overhangs its knob.
        juce::Component* host = this;

        if (getWidth() < valueEntryWidth + 4 || getHeight() < valueEntryHeight + 4)
            if (auto* p = getParentComponent())
                host = p;

        ed.setBounds (juce::Rectangle<int> (valueEntryWidth, valueEntryHeight)
                          .withCentre (host->getLocalPoint (this, getLocalBounds().getCentre())));

        valueEntryOwner = this;      // the control being edited, not the host of the field
        host->addAndMakeVisible (ed);

        ed.grabKeyboardFocus();
        ed.selectAll();

        const juce::Component::SafePointer<ParamControl> safe (this);

        ed.onReturnKey = [safe]
        {
            if (safe != nullptr && valueEntry != nullptr)
            {
                if (const auto typed = parseTypedValue (safe->pid, valueEntry->getText()))
                {
                    safe->params.setFromUI (safe->pid, clampToRange (safe->params, safe->pid, *typed));
                    safe->targetValue = safe->params.normalised (safe->pid);
                    safe->refreshTooltip();

                    if (safe->onValueChange)
                        safe->onValueChange();
                }
            }

            closeValueEntry (true);
        };

        ed.onEscapeKey = [] { closeValueEntry (true); };
        ed.onFocusLost = [] { closeValueEntry (true); };
    }

    // =======================================================================
    //  NacarKnob
    // =======================================================================
    NacarKnob::NacarKnob (const ParameterRegistry& registry, PID parameterID, Style s)
        : ParamControl (registry, parameterID), style (s)
    {
    }

    void NacarKnob::setLabel (juce::String text, float sizePx, float trackingEm)
    {
        label = std::move (text);
        labelSize = sizePx;
        labelTrack = trackingEm;
        repaint();
    }

    void NacarKnob::setScaleLegend (juce::String left, juce::String right)
    {
        legendLeft = std::move (left);
        legendRight = std::move (right);
        repaint();
    }

    float NacarKnob::capRadius() const noexcept
    {
        const float half = juce::jmin ((float) getWidth(), (float) getHeight()) * 0.5f;
        return juce::jmax (4.0f, half - knobSeatAllowance);
    }

    /** The cap hangs from the top of the component so that a generously sized
        knob keeps its label room underneath.  For a square component this is
        exactly the centre, so nothing moves when there is no label. */
    static juce::Point<float> capCentreOf (const juce::Component& c, float capR)
    {
        return { (float) c.getWidth() * 0.5f, capR + knobSeatAllowance };
    }

    void NacarKnob::paint (juce::Graphics& g)
    {
        const float r       = capRadius();
        const auto  centre  = capCentreOf (*this, r);
        const float span    = juce::degreesToRadians (knobArcSpanDeg);
        const float value   = juce::jlimit (0.0f, 1.0f, displayValue);
        const float angle   = -span + 2.0f * span * value;
        const float grooveR = r + knobGrooveGap + knobGrooveWidth * 0.5f;

        // 1. The seat groove: a machined recess just outside the cap, which is
        //    where the value arc lives.
        {
            juce::Path groove;
            groove.addCentredArc (centre.x, centre.y, grooveR, grooveR, 0.0f, -span, span, true);

            g.setColour (theme::ceramicDark);
            g.strokePath (groove, juce::PathStrokeType (knobGrooveWidth, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));

            juce::Path lip;
            lip.addCentredArc (centre.x, centre.y, grooveR + knobGrooveWidth * 0.5f,
                               grooveR + knobGrooveWidth * 0.5f, 0.0f, -span, span, true);

            g.setColour (theme::ceramicDeep.withAlpha (0.5f));
            g.strokePath (lip, juce::PathStrokeType (1.0f));
        }

        // 2. The value arc.  Unipolar sweeps from the low end, bipolar from
        //    12 o'clock outward in whichever direction the value went.
        {
            const float from = isBipolar() ? 0.0f : -span;

            if (std::abs (angle - from) > 1.0e-3f)
            {
                juce::Path arc;
                arc.addCentredArc (centre.x, centre.y, grooveR, grooveR, 0.0f, from, angle, true);

                const float glowAlpha = isDragging ? 0.55f : (isHovered ? 0.42f : 0.28f);

                // theme::glow is a point halo; an arc carries its own by being
                // stroked wide and faint underneath itself.
                g.setColour (theme::violet.withAlpha (glowAlpha));
                g.strokePath (arc, juce::PathStrokeType (knobArcWidth * 2.6f,
                                                         juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));

                g.setColour (isDragging ? theme::violetLight : theme::violet);
                g.strokePath (arc, juce::PathStrokeType (knobArcWidth,
                                                         juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
            }
        }

        // 3 and 4. The cap sits on the chassis, so it casts a contact shadow.
        const auto cap = squareAt (centre, r);

        theme::contactShadow (g, cap, r);
        theme::machinedCap (g, centre, r, style == Style::dark);

        // 5. One indicator line, the only thing on the cap that moves.
        strokeLine (g, pointOnDial (centre, r * knobIndicatorIn,  angle),
                       pointOnDial (centre, r * knobIndicatorOut, angle),
                    knobIndicatorW,
                    style == Style::dark ? theme::violet : theme::ink);

        // 7. Label, 8. scale legend.  Both hang below the cap, so the parent
        //    sizes the component tall enough to contain them.
        const float labelBaseline = centre.y + r + knobLabelGap;

        // A dark cap only ever sits on glass, and ceramic ink on glass is
        // invisible - which is how a page of dark knobs ends up unlabelled.
        if (label.isNotEmpty())
        {
            const juce::Point<float> at { (float) getWidth() * 0.5f, labelBaseline };

            if (style == Style::dark)
                glassLabel (g, label, at, labelSize, labelTrack,
                            theme::glassInkMuted, juce::Justification::centred);
            else
                ceramicLabel (g, label, at, labelSize, labelTrack,
                              theme::ink, juce::Justification::centred);
        }

        if (legendLeft.isNotEmpty() || legendRight.isNotEmpty())
            scaleLegend (g, legendLeft, legendRight,
                         { centre.x - r, labelBaseline + knobLegendGap, r * 2.0f, knobLegendHeight });
    }

    // =======================================================================
    //  PillButton
    // =======================================================================
    namespace
    {
        /** Paints the pill body and returns the colour its text and icons take. */
        juce::Colour paintPillBackground (juce::Graphics& g, juce::Rectangle<float> b,
                                          PillButton::Style style, bool selected,
                                          bool highlighted, bool down, float corner)
        {
            using S = PillButton::Style;

            switch (style)
            {
                case S::ceramic:
                {
                    theme::contactShadow (g, b, corner, down ? 1.0f : 2.0f, down ? 5.0f : 8.0f);

                    const auto top = selected ? theme::ceramicDark : theme::ceramicLight;
                    const auto bot = selected ? theme::ceramicDeep : theme::ceramicMid;

                    theme::ceramicSurface (g, b, corner, hoverLift (top, highlighted, down),
                                                         hoverLift (bot, highlighted, down));
                    return theme::ink;
                }

                case S::glass:
                {
                    theme::glassSurface (g, b, corner, hoverLift (theme::glassRaised, highlighted, down));

                    if (! selected)
                        return theme::glassInk;

                    g.setColour (theme::violet);
                    g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
                    return theme::violet;
                }

                case S::violet:
                {
                    theme::contactShadow (g, b, corner, down ? 1.0f : 2.0f, down ? 5.0f : 8.0f);

                    const auto top = theme::ceramicLight.interpolatedWith (theme::violetLight, 0.18f);
                    const auto bot = theme::ceramicMid  .interpolatedWith (theme::violetLight, 0.18f);

                    theme::ceramicSurface (g, b, corner, hoverLift (top, highlighted, down),
                                                         hoverLift (bot, highlighted, down));

                    g.setColour (theme::violetDeep);
                    g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
                    return theme::ink;
                }

                case S::violetOutline:
                {
                    if (highlighted || down)
                    {
                        g.setColour (theme::violet.withAlpha (down ? 0.18f : 0.10f));
                        g.fillRoundedRectangle (b, corner);
                    }

                    g.setColour (selected ? theme::violetLight : theme::violet);
                    g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
                    return theme::violet;
                }

                case S::dashed:
                default:
                {
                    // The FX "add slot": an outline with nothing in it yet.
                    juce::Path outline;
                    outline.addRoundedRectangle (b.reduced (0.5f), corner);

                    const float dashes[] = { 4.0f, 3.0f };
                    juce::Path dashed;
                    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);

                    g.setColour (theme::glassEdge.withAlpha (highlighted ? 1.0f : 0.8f));
                    g.fillPath (dashed);
                    return theme::glassInkMuted;
                }
            }
        }
    }

    PillButton::PillButton (juce::String text, Style s)
        : juce::Button (text), style (s)
    {
    }

    void PillButton::setIcon (icons::Icon i, float sizeRatio)
    {
        leadingIcon = i;
        iconRatio = sizeRatio;
        repaint();
    }

    void PillButton::setTrailingIcon (icons::Icon i)
    {
        trailingIcon = i;
        repaint();
    }

    void PillButton::setTextSize (float px, float trackingEm)
    {
        textSize = px;
        tracking = trackingEm;
        repaint();
    }

    void PillButton::setSelected (bool shouldBeSelected)
    {
        if (selected == shouldBeSelected)
            return;

        selected = shouldBeSelected;
        repaint();
    }

    float PillButton::preferredWidth (float horizontalPadding) const
    {
        // Before the first resized() there is no height to scale the icons by,
        // so fall back to a pill proportioned like the reference.
        const float h = getHeight() > 0 ? (float) getHeight() : textSize * 3.5f;
        const float iconSize = h * iconRatio;

        float w = theme::trackedWidth (getButtonText(), theme::label (textSize), tracking)
                  + horizontalPadding * 2.0f;

        // The gap only exists between an icon and something after it, so a pill
        // with no label reserves none - otherwise its glyph sits off centre.
        const bool hasText = getButtonText().isNotEmpty();

        if (leadingIcon.has_value())  w += iconSize + (hasText ? pillIconGap : 0.0f);
        if (trailingIcon.has_value()) w += iconSize + (hasText ? pillIconGap : 0.0f);

        return w;
    }

    void PillButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);

        if (down)
            b = b.translated (0.0f, 0.5f);   // pressed settles into its own shadow

        auto colour = paintPillBackground (g, b, style, selected, highlighted, down, corner);

        if (! isEnabled())
            colour = colour.withAlpha (0.45f);

        const auto  font     = theme::label (textSize);
        const float iconSize = b.getHeight() * iconRatio;
        const float textW    = theme::trackedWidth (getButtonText(), font, tracking);

        const bool  hasText = getButtonText().isNotEmpty();
        const float gap     = hasText ? pillIconGap : 0.0f;

        float content = textW;
        if (leadingIcon.has_value())  content += iconSize + gap;
        if (trailingIcon.has_value()) content += iconSize + gap;

        float x = b.getCentreX() - content * 0.5f;

        if (leadingIcon.has_value())
        {
            icons::draw (g, *leadingIcon, squareAt ({ x + iconSize * 0.5f, b.getCentreY() },
                                                    iconSize * 0.5f), colour);
            x += iconSize + gap;
        }

        g.setColour (colour);
        theme::drawTracked (g, getButtonText(), { x, b.getY(), textW, b.getHeight() },
                            font, tracking, juce::Justification::centredLeft);
        x += textW;

        if (trailingIcon.has_value())
        {
            x += gap;
            icons::draw (g, *trailingIcon, squareAt ({ x + iconSize * 0.5f, b.getCentreY() },
                                                     iconSize * 0.5f), colour);
        }
    }

    // =======================================================================
    //  IconButton
    // =======================================================================
    IconButton::IconButton (icons::Icon i, Style s)
        : juce::Button (juce::String()), icon (i), style (s)
    {
    }

    void IconButton::setColours (juce::Colour normal, juce::Colour activeCol)
    {
        normalColour = normal;
        activeColour = activeCol;
        repaint();
    }

    void IconButton::setActive (bool shouldBeActive)
    {
        if (active == shouldBeActive)
            return;

        active = shouldBeActive;
        repaint();
    }

    void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const auto  b = getLocalBounds().toFloat().reduced (1.0f);
        const float d = juce::jmin (b.getWidth(), b.getHeight());
        const auto  disc = squareAt (b.getCentre(), d * 0.5f);

        auto colour = active ? activeColour : normalColour;

        switch (style)
        {
            case Style::plain:
                break;

            case Style::glassRound:
                theme::glassSurface (g, disc, d * 0.5f,
                                     hoverLift (theme::glassRaised, highlighted, down));
                break;

            case Style::ceramicRound:
                theme::contactShadow (g, disc, d * 0.5f, down ? 1.0f : 2.0f, 7.0f);
                theme::ceramicSurface (g, disc, d * 0.5f,
                                       hoverLift (theme::ceramicLight, highlighted, down),
                                       hoverLift (theme::ceramicMid,   highlighted, down));
                break;

            case Style::glassSquare:
                theme::glassSurface (g, b, iconSquareCorner,
                                     hoverLift (theme::glassRaised, highlighted, down));
                break;

            case Style::violetSquare:
                // The active toolbar button in the reference viewport: a violet
                // wash rather than a solid fill, so the glyph stays legible.
                g.setColour (theme::violet.withAlpha (down ? 0.30f : (highlighted ? 0.26f : 0.22f)));
                g.fillRoundedRectangle (b, iconSquareCorner);
                g.setColour (theme::violet.withAlpha (0.75f));
                g.drawRoundedRectangle (b.reduced (0.5f), iconSquareCorner, 1.0f);
                colour = activeColour;
                break;
        }

        if (! isEnabled())
            colour = colour.withAlpha (0.4f);
        else if (highlighted && ! active && style == Style::plain)
            colour = colour.brighter (0.25f);

        icons::draw (g, icon, squareAt (b.getCentre(), d * ratio * 0.5f), colour);
    }

    // =======================================================================
    //  PowerButton
    // =======================================================================
    PowerButton::PowerButton (Tint t)
        : juce::Button (juce::String()), tint (t)
    {
        // A power button is a latch, so callers only ever read getToggleState().
        setClickingTogglesState (true);
    }

    void PowerButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const auto  b      = getLocalBounds().toFloat();
        const auto  centre = b.getCentre();
        const float r      = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 1.0f;
        const bool  on     = getToggleState();
        const auto  accent = tint == Tint::mint ? theme::mint : theme::violet;
        const auto  disc   = squareAt (centre, r);

        // The halo reads first - this is the "alive" indicator on every FX card.
        if (on)
            theme::glow (g, centre, r * 1.9f, accent, highlighted ? 0.42f : 0.32f);

        theme::contactShadow (g, disc, r, down ? 1.0f : 2.0f, 6.0f, 0.16f);

        g.setGradientFill (juce::ColourGradient (hoverLift (theme::ceramicLight, highlighted, down),
                                                 centre.x, disc.getY(),
                                                 hoverLift (theme::ceramicMid, highlighted, down),
                                                 centre.x, disc.getBottom(), false));
        g.fillEllipse (disc);

        g.setColour (theme::ceramicEdge);
        g.drawEllipse (disc.reduced (0.5f), 1.0f);

        if (on)
            theme::glow (g, centre, r * 0.95f, accent, 0.30f);   // the lamp under the glyph

        icons::draw (g, icons::Icon::power, squareAt (centre, r * 0.5f),
                     on ? accent : theme::inkFaint, 1.6f);
    }

    // =======================================================================
    //  SegmentedControl
    // =======================================================================
    SegmentedControl::SegmentedControl (juce::StringArray opts, Style s)
        : options (std::move (opts)), style (s)
    {
    }

    void SegmentedControl::setTextSize (float px, float trackingEm)
    {
        textSize = px;
        tracking = trackingEm;
        repaint();
    }

    void SegmentedControl::bindTo (const ParameterRegistry& registry, PID p)
    {
        boundParams = &registry;
        boundPid = p;

        selectedIndex = juce::jlimit (0, juce::jmax (0, options.size() - 1),
                                      boundIndex (registry, p, selectedIndex));

        setTooltip (tooltipFor (p));
        repaint();
    }

    void SegmentedControl::setSelectedIndex (int index, juce::NotificationType notification)
    {
        index = juce::jlimit (0, juce::jmax (0, options.size() - 1), index);

        if (index == selectedIndex)
            return;

        selectedIndex = index;
        repaint();

        if (notification == juce::dontSendNotification)
            return;

        if (boundParams != nullptr && boundPid != PID::count)
            boundParams->setFromUI (boundPid, (float) index);

        if (onSelect)
            onSelect (index);
    }

    juce::Rectangle<float> SegmentedControl::segmentBounds (int index) const
    {
        const auto  b = getLocalBounds().toFloat();
        const float w = b.getWidth() / (float) juce::jmax (1, options.size());

        return { b.getX() + w * (float) index, b.getY(), w, b.getHeight() };
    }

    int SegmentedControl::indexAt (juce::Point<float> p) const
    {
        const auto b = getLocalBounds().toFloat();

        if (options.isEmpty() || ! b.contains (p))
            return -1;

        return juce::jlimit (0, options.size() - 1,
                             (int) ((p.x - b.getX()) / (b.getWidth() / (float) options.size())));
    }

    void SegmentedControl::paint (juce::Graphics& g)
    {
        // Widgets.h gives this control no timer, so a bound selector re-reads the
        // host value every time it repaints.  Automation therefore lands on the
        // next chassis repaint rather than on its own clock.
        if (boundParams != nullptr && boundPid != PID::count)
            selectedIndex = juce::jlimit (0, juce::jmax (0, options.size() - 1),
                                          boundIndex (*boundParams, boundPid, selectedIndex));

        const auto  b      = getLocalBounds().toFloat().reduced (0.5f);
        const float corner = juce::jmin (layout::radiusPill, b.getHeight() * segmentCornerRatio);
        const float inner  = juce::jmax (0.0f, corner - segmentInset);

        if (style == Style::recessed)
        {
            // Spec section 4: WEIGHT's unselected segments sit flush with the
            // panel, so the track is no more than a hairline.
            g.setColour (theme::ceramicEdge.withAlpha (0.6f));
            g.drawRoundedRectangle (b, corner, 1.0f);
        }
        else
        {
            g.setColour (theme::ceramicDark);
            g.fillRoundedRectangle (b, corner);

            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addRoundedRectangle (b, corner);
            g.reduceClipRegion (clip);

            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.16f),
                                                     b.getCentreX(), b.getY(),
                                                     juce::Colours::transparentBlack,
                                                     b.getCentreX(), b.getY() + b.getHeight() * 0.6f,
                                                     false));
            g.fillRect (b);
        }

        const auto font = theme::label (textSize);

        for (int i = 0; i < options.size(); ++i)
        {
            const auto seg = segmentBounds (i).reduced (segmentInset, segmentInset);
            auto textColour = (i == hoverIndex ? theme::ink : theme::inkMuted);

            if (i == selectedIndex)
            {
                switch (style)
                {
                    case Style::violetFill:
                        g.setColour (theme::violet.withAlpha (0.85f));
                        g.fillRoundedRectangle (seg, inner);
                        textColour = theme::ink;     // dark on violet reads better than white
                        break;

                    case Style::darkFill:
                        g.setColour (theme::glassMid);
                        g.fillRoundedRectangle (seg, inner);
                        textColour = theme::glassInk;
                        break;

                    case Style::recessed:
                    default:
                        g.setColour (theme::ceramicDark);
                        g.fillRoundedRectangle (seg, inner);
                        g.setColour (theme::ceramicEdge.withAlpha (0.5f));
                        g.drawRoundedRectangle (seg.reduced (0.5f), inner, 1.0f);
                        textColour = theme::ink;
                        break;
                }
            }

            g.setColour (textColour);
            theme::drawTracked (g, options[i], seg, font, tracking, juce::Justification::centred);
        }
    }

    void SegmentedControl::mouseDown (const juce::MouseEvent& e)
    {
        const int i = indexAt (e.position);

        if (i >= 0)
            setSelectedIndex (i);
    }

    void SegmentedControl::mouseMove (const juce::MouseEvent& e)
    {
        const int i = indexAt (e.position);

        if (i == hoverIndex)
            return;

        hoverIndex = i;
        repaint();
    }

    void SegmentedControl::mouseExit (const juce::MouseEvent&)
    {
        if (hoverIndex < 0)
            return;

        hoverIndex = -1;
        repaint();
    }

    // =======================================================================
    //  ToggleSwitch
    // =======================================================================
    ToggleSwitch::ToggleSwitch (Size s)
        : size (s)
    {
    }

    void ToggleSwitch::bindTo (const ParameterRegistry& registry, PID p)
    {
        boundParams = &registry;
        boundPid = p;

        state = boundIndex (registry, p, state ? 1 : 0) > 0;
        animated = state ? 1.0f : 0.0f;

        setTooltip (tooltipFor (p));
        repaint();
    }

    void ToggleSwitch::setToggleState (bool shouldBeOn, juce::NotificationType notification)
    {
        if (shouldBeOn == state)
            return;

        state = shouldBeOn;

        // The switch is intentionally instantaneous: Widgets.h is frozen and
        // gives ToggleSwitch no Timer base, and animating from paint() would tie
        // the travel to whatever else happens to be repainting.  `animated`
        // therefore follows the state directly.
        animated = state ? 1.0f : 0.0f;
        repaint();

        if (notification == juce::dontSendNotification)
            return;

        if (boundParams != nullptr && boundPid != PID::count)
            boundParams->setFromUI (boundPid, state ? 1.0f : 0.0f);

        if (onToggle)
            onToggle (state);
    }

    void ToggleSwitch::paint (juce::Graphics& g)
    {
        if (boundParams != nullptr && boundPid != PID::count)
        {
            // Same polling-on-repaint arrangement as SegmentedControl.
            state = boundIndex (*boundParams, boundPid, state ? 1 : 0) > 0;
            animated = state ? 1.0f : 0.0f;
        }

        // Size::large is the SHADOW module's header toggle, which is drawn at
        // whatever size the module gives it; Size::small is the fixed 26 x 14
        // preserve-lock track from Layout.h.
        const auto track = size == Size::large
                             ? getLocalBounds().toFloat().reduced (1.0f)
                             : juce::Rectangle<float> (layout::mut::switchW, layout::mut::switchH)
                                   .withCentre (getLocalBounds().toFloat().getCentre());

        const float r     = track.getHeight() * 0.5f;
        const float knobR = juce::jmax (1.0f, r - switchKnobInset);

        g.setColour (state ? theme::violet
                           : (hovered ? theme::ceramicDark.brighter (0.05f) : theme::ceramicDark));
        g.fillRoundedRectangle (track, r);

        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.drawRoundedRectangle (track.reduced (0.5f), r, 1.0f);

        const juce::Point<float> knobCentre { juce::jmap (animated, track.getX() + r,
                                                          track.getRight() - r),
                                              track.getCentreY() };

        if (state)
            theme::glow (g, knobCentre, knobR * 2.2f, theme::violet, 0.35f);

        theme::contactShadow (g, squareAt (knobCentre, knobR), knobR, 1.0f, 4.0f, 0.22f);

        g.setGradientFill (juce::ColourGradient (state ? theme::glassInk    : theme::ceramicLight,
                                                 knobCentre.x, knobCentre.y - knobR,
                                                 state ? theme::ceramicLight : theme::ceramicMid,
                                                 knobCentre.x, knobCentre.y + knobR, false));
        g.fillEllipse (squareAt (knobCentre, knobR));

        g.setColour (state ? theme::violetDeep : theme::ceramicEdge);
        g.drawEllipse (squareAt (knobCentre, knobR).reduced (0.5f), 1.0f);
    }

    void ToggleSwitch::mouseDown (const juce::MouseEvent&)
    {
        setToggleState (! state);
    }

    void ToggleSwitch::mouseEnter (const juce::MouseEvent&)
    {
        hovered = true;
        repaint();
    }

    void ToggleSwitch::mouseExit (const juce::MouseEvent&)
    {
        hovered = false;
        repaint();
    }

    // =======================================================================
    //  PreserveLock
    // =======================================================================
    PreserveLock::PreserveLock (juce::String captionText, const ParameterRegistry& registry, PID p)
        : caption (std::move (captionText))
    {
        addAndMakeVisible (toggle);
        toggle.bindTo (registry, p);

        // Switch plus caption is one hit target.  PreserveLock cannot override
        // mouseDown - Widgets.h is frozen and does not declare it - so the
        // switch listens to the row instead.  Nesting is off, so a click on the
        // switch itself still only toggles once.
        addMouseListener (&toggle, false);
    }

    float PreserveLock::preferredWidth() const
    {
        const auto font = theme::label (layout::mut::preserveSize);

        return layout::mut::switchW + layout::mut::switchGap
               + theme::trackedWidth (caption, font, layout::mut::preserveTrack);
    }

    void PreserveLock::resized()
    {
        toggle.setBounds (juce::Rectangle<float> (0.0f,
                                                  (float) getHeight() * 0.5f - layout::mut::switchH * 0.5f,
                                                  layout::mut::switchW, layout::mut::switchH)
                              .toNearestInt());
    }

    void PreserveLock::paint (juce::Graphics& g)
    {
        const auto  font = theme::label (layout::mut::preserveSize);
        const float baseline = (float) getHeight() * 0.5f + font.getAscent() - font.getHeight() * 0.5f;

        ceramicLabel (g, caption,
                      { layout::mut::switchW + layout::mut::switchGap, baseline },
                      layout::mut::preserveSize, layout::mut::preserveTrack,
                      toggle.getToggleState() ? theme::ink : theme::inkMuted);
    }

    // =======================================================================
    //  GenerationSelector
    // =======================================================================
    GenerationSelector::GenerationSelector (const ParameterRegistry& registry, PID parameterID)
        : params (registry), pid (parameterID)
    {
        selected = juce::jlimit (0, numGenerations - 1, boundIndex (registry, parameterID, 0));
        setTooltip (tooltipFor (parameterID));
    }

    void GenerationSelector::setSelected (int index, juce::NotificationType notification)
    {
        index = juce::jlimit (0, numGenerations - 1, index);

        if (index == selected)
            return;

        selected = index;
        repaint();

        if (notification == juce::dontSendNotification)
            return;

        params.setFromUI (pid, (float) index);

        if (onSelect)
            onSelect (index);
    }

    juce::Rectangle<float> GenerationSelector::rowBounds (int index) const
    {
        // The absolute Y of each row lives in the parent (layout::macro::genY);
        // this component is positioned by the macro panel, so internally the
        // rows are simply the four equal bands of its own height.
        const auto  b = getLocalBounds().toFloat();
        const float h = b.getHeight() / (float) numGenerations;

        return { b.getX(), b.getY() + h * (float) index, b.getWidth(), h };
    }

    void GenerationSelector::paint (juce::Graphics& g)
    {
        // Polled on repaint; see SegmentedControl::paint.
        selected = juce::jlimit (0, numGenerations - 1, boundIndex (params, pid, selected));

        auto numerals = ParameterRegistry::choicesOf (pid);

        if (numerals.size() < numGenerations)
            numerals = juce::StringArray { "I", "II", "III", "IV" };

        const auto  font = theme::label (layout::macro::knobLabelSize);
        const float dotX = layout::macro::genDotRActive + 1.0f;
        const float textX = dotX + (layout::macro::genLabelX - layout::macro::genDotX);

        for (int i = 0; i < numGenerations; ++i)
        {
            const auto row = rowBounds (i);
            const juce::Point<float> dot { row.getX() + dotX, row.getCentreY() };
            const bool isActive = (i == selected);

            if (isActive)
            {
                theme::glow (g, dot, genGlowRadius, theme::violet, 0.45f);
                g.setColour (theme::violet);
                g.fillEllipse (squareAt (dot, layout::macro::genDotRActive));
            }
            else
            {
                g.setColour (i == hover ? theme::ceramicDeep.brighter (0.15f) : theme::ceramicDeep);
                g.fillEllipse (squareAt (dot, layout::macro::genDotR));
            }

            const float baseline = row.getCentreY() + font.getAscent() - font.getHeight() * 0.5f;

            ceramicLabel (g, numerals[i], { row.getX() + textX, baseline },
                          layout::macro::knobLabelSize, layout::macro::knobLabelTrack,
                          isActive ? theme::ink : (i == hover ? theme::ink : theme::inkMuted));
        }
    }

    void GenerationSelector::mouseDown (const juce::MouseEvent& e)
    {
        for (int i = 0; i < numGenerations; ++i)
            if (rowBounds (i).contains (e.position))
                setSelected (i);
    }

    void GenerationSelector::mouseMove (const juce::MouseEvent& e)
    {
        int found = -1;

        for (int i = 0; i < numGenerations; ++i)
            if (rowBounds (i).contains (e.position))
                found = i;

        if (found == hover)
            return;

        hover = found;
        repaint();
    }

    void GenerationSelector::mouseExit (const juce::MouseEvent&)
    {
        if (hover < 0)
            return;

        hover = -1;
        repaint();
    }

    // =======================================================================
    //  HairlineSlider
    // =======================================================================
    HairlineSlider::HairlineSlider()
    {
    }

    void HairlineSlider::setValue (float normalised, juce::NotificationType notification)
    {
        normalised = juce::jlimit (0.0f, 1.0f, normalised);

        if (std::abs (normalised - value) < 1.0e-6f)
            return;

        value = normalised;
        repaint();

        if (notification != juce::dontSendNotification && onValueChange)
            onValueChange (value);
    }

    void HairlineSlider::paint (juce::Graphics& g)
    {
        const auto  b  = getLocalBounds().toFloat();
        const float y  = b.getCentreY();
        const float x0 = b.getX() + hairlineCapR;
        const float x1 = b.getRight() - hairlineCapR;

        if (x1 <= x0)
            return;

        const float x = juce::jmap (value, x0, x1);

        theme::hairline (g, { x0, y }, { x1, y });

        g.setColour (theme::violet);
        g.drawLine (x0, y, x, y, 1.0f);

        const juce::Point<float> cap { x, y };

        theme::contactShadow (g, squareAt (cap, hairlineCapR), hairlineCapR, 1.5f, 5.0f, 0.22f);
        theme::machinedCap (g, cap, hairlineCapR);
    }

    void HairlineSlider::mouseDown (const juce::MouseEvent& e)
    {
        setFromMouse (e);
    }

    void HairlineSlider::mouseDrag (const juce::MouseEvent& e)
    {
        setFromMouse (e);
    }

    void HairlineSlider::setFromMouse (const juce::MouseEvent& e)
    {
        const float x0 = hairlineCapR;
        const float x1 = (float) getWidth() - hairlineCapR;

        if (x1 > x0)
            setValue ((e.position.x - x0) / (x1 - x0));
    }

    // =======================================================================
    //  Panels
    // =======================================================================
    CeramicPanel::CeramicPanel (float cornerRadius)
        : corner (cornerRadius)
    {
    }

    void CeramicPanel::paint (juce::Graphics& g)
    {
        // Half a pixel in, so the 1 px specular and bevel edges land inside the
        // component instead of straddling its boundary.
        const auto b = getLocalBounds().toFloat().reduced (0.5f);

        theme::contactShadow (g, b, corner);
        theme::ceramicSurface (g, b, corner);
    }

    GlassPanel::GlassPanel (float cornerRadius)
        : corner (cornerRadius)
    {
    }

    void GlassPanel::paint (juce::Graphics& g)
    {
        // Glass is a cut-out, so it never carries a drop shadow (spec section 1).
        theme::glassSurface (g, getLocalBounds().toFloat().reduced (0.5f), corner, fill);
    }

    // =======================================================================
    //  Helpers
    // =======================================================================
    namespace
    {
        /** theme::drawTracked positions by rectangle; these labels position by
            baseline, so the rectangle is rebuilt around the baseline here. */
        void trackedLabel (juce::Graphics& g, juce::StringRef text,
                           juce::Point<float> baselineOrigin, const juce::Font& font,
                           float trackingEm, juce::Colour colour,
                           juce::Justification just, float width)
        {
            if (text.isEmpty())
                return;

            const float w = width > 0.0f ? width : theme::trackedWidth (text, font, trackingEm);

            float x = baselineOrigin.x;

            if (just.testFlags (juce::Justification::right))
                x -= w;
            else if (just.testFlags (juce::Justification::horizontallyCentred))
                x -= w * 0.5f;

            const juce::Rectangle<float> area (x, baselineOrigin.y - font.getAscent(),
                                               w, font.getHeight());

            g.setColour (colour);
            theme::drawTracked (g, text, area, font, trackingEm,
                                juce::Justification (just.getOnlyHorizontalFlags()
                                                     | juce::Justification::verticallyCentred));
        }
    }

    void ceramicLabel (juce::Graphics& g, juce::StringRef text, juce::Point<float> baselineOrigin,
                       float sizePx, float trackingEm, juce::Colour colour,
                       juce::Justification just, float width)
    {
        // The text is drawn as given: the reference sets these runs in capitals
        // already, and silently upper-casing would surprise the caller.
        trackedLabel (g, text, baselineOrigin, theme::label (sizePx), trackingEm,
                      colour, just, width);
    }

    void glassLabel (juce::Graphics& g, juce::StringRef text, juce::Point<float> baselineOrigin,
                     float sizePx, float trackingEm, juce::Colour colour,
                     juce::Justification just, float width)
    {
        trackedLabel (g, text, baselineOrigin, theme::label (sizePx), trackingEm,
                      colour, just, width);
    }

    void scaleLegend (juce::Graphics& g, juce::StringRef left, juce::StringRef right,
                      juce::Rectangle<float> area)
    {
        const auto  font = theme::label (layout::macro::legendSize);
        const float baseline = area.getCentreY() + font.getAscent() - font.getHeight() * 0.5f;
        const float track = layout::macro::legendTrack;

        const float leftW  = theme::trackedWidth (left,  font, track);
        const float rightW = theme::trackedWidth (right, font, track);

        ceramicLabel (g, left,  { area.getX(), baseline },
                      layout::macro::legendSize, track, theme::inkFaint);

        ceramicLabel (g, right, { area.getRight(), baseline },
                      layout::macro::legendSize, track, theme::inkFaint,
                      juce::Justification::right);

        // The rule fills what is left between the two ends.
        const float ruleL = area.getX() + leftW + 6.0f;
        const float ruleR = area.getRight() - rightW - 6.0f;

        if (ruleR > ruleL + 2.0f)
            theme::hairline (g, { ruleL, area.getCentreY() }, { ruleR, area.getCentreY() });
    }
}
