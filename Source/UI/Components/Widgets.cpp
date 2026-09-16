#include "Widgets.h"

#include <algorithm>
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
    //
    //  The allowance is what the parent leaves around the cap, and it is a
    //  contract: MacroPanel and AtmospherePanel size their knob components as
    //  the spec radius plus exactly this, so capRadius() lands the cap on the
    //  radius Layout.h fixes.  It may be divided up differently - and is, below,
    //  to narrow the channel - but its total must not change.
    static constexpr float knobArcSpanDeg    = 140.0f;  ///< +/- from 12 o'clock, so 280 deg total
    static constexpr float knobSeatAllowance = 7.0f;
    static constexpr float knobGrooveGap     = 2.2f;    ///< shelf between the cap and the channel
    static constexpr float knobGrooveWidth   = 4.0f;
    static constexpr float knobGrooveLip     = knobSeatAllowance - knobGrooveGap - knobGrooveWidth;
    static constexpr float knobArcWidth      = 2.4f;    ///< leaves the channel visible either side
    static constexpr float knobArcGlowWidth  = 3.4f;    ///< multiple of the arc width for its halo
    static constexpr int   knobTurnRings     = 9;       ///< bound on the machined-face pass
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
    //  Motion
    // =======================================================================
    namespace detail
    {
        namespace
        {
            constexpr int   motionHz      = 30;     ///< the rate the chassis paints at
            constexpr float motionSettled = 0.004f; ///< below this, stop and land exactly
        }

        MotionTicker::~MotionTicker()
        {
            stopTimer();
        }

        void MotionTicker::add (Motion& m)
        {
            if (std::find (inFlight.begin(), inFlight.end(), &m) == inFlight.end())
                inFlight.push_back (&m);

            if (! isTimerRunning())
                startTimerHz (motionHz);
        }

        void MotionTicker::remove (Motion& m)
        {
            inFlight.erase (std::remove (inFlight.begin(), inFlight.end(), &m), inFlight.end());

            // Nothing is moving, so nothing needs a clock.  This is the whole
            // reason one shared ticker is cheaper than a timer per widget.
            if (inFlight.empty())
                stopTimer();
        }

        void MotionTicker::timerCallback()
        {
            // Stepping repaints, and a repaint can in principle destroy a
            // component, so the sweep runs over a copy and re-checks membership.
            const auto snapshot = inFlight;

            for (auto* m : snapshot)
                if (std::find (inFlight.begin(), inFlight.end(), m) != inFlight.end())
                    if (! m->advance())
                        remove (*m);
        }

        void Motion::setTarget (float t)
        {
            t = juce::jlimit (0.0f, 1.0f, t);

            if (std::abs (t - target) < 1.0e-4f)
                return;

            target = t;

            if (std::abs (target - current) > motionSettled)
                ticker->add (*this);
            else
                current = target;
        }

        void Motion::snapTo (float t)
        {
            current = target = juce::jlimit (0.0f, 1.0f, t);
            ticker->remove (*this);
        }

        bool Motion::advance()
        {
            const float delta = target - current;

            if (std::abs (delta) <= motionSettled)
            {
                current = target;
                owner.repaint();
                return false;
            }

            current += delta * rate;
            owner.repaint();
            return true;
        }
    }

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
                return (int) r.userValue (p);

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

        // -------------------------------------------------------------------
        //  Lighting
        //
        //  Everything directional in this file is built out of these three, so
        //  no widget can quietly light itself from a direction of its own and
        //  break the illusion for the panel it sits in.  The light is
        //  theme::lightX / lightY: upper-left, and it never moves.
        // -------------------------------------------------------------------

        /** A ramp along the light axis of `area`: `nearLight` lands on the
            upper-left, `farFromLight` on the lower-right. */
        juce::ColourGradient acrossLight (juce::Rectangle<float> area,
                                          juce::Colour nearLight, juce::Colour farFromLight,
                                          float reach = 0.8f)
        {
            const float rx = area.getWidth()  * 0.5f * reach;
            const float ry = area.getHeight() * 0.5f * reach;
            const auto  c  = area.getCentre();

            return { nearLight,    c.x + theme::lightX * rx, c.y + theme::lightY * ry,
                     farFromLight, c.x - theme::lightX * rx, c.y - theme::lightY * ry, false };
        }

        /** Paints the annulus of the given mid-radius and width with whatever
            fill is current - a gradient, usually. */
        void fillRing (juce::Graphics& g, juce::Point<float> centre,
                       float midRadius, float width)
        {
            if (midRadius <= 0.0f || width <= 0.0f)
                return;

            g.drawEllipse (squareAt (centre, midRadius), width);
        }

        /** Strokes an arc with whatever fill is current. */
        void strokeArc (juce::Graphics& g, juce::Point<float> centre, float radius,
                        float fromAngle, float toAngle, float thickness)
        {
            if (radius <= 0.0f || std::abs (toAngle - fromAngle) < 1.0e-3f)
                return;

            juce::Path p;
            p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, fromAngle, toAngle, true);

            g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
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
        displayValue = targetValue = params.normalisedUserValue (pid);

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
        return realFrom (params, pid, params.normalisedUserValue (pid));
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
        pressMotion.setTarget (1.0f);
        dragStartValue = params.normalisedUserValue (pid);
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
        pressMotion.setTarget (0.0f);
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
            pressMotion.setTarget (0.0f);
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
        targetValue = params.normalisedUserValue (pid);

        refreshTooltip();

        if (onValueChange)
            onValueChange();
    }

    void ParamControl::mouseEnter (const juce::MouseEvent&)
    {
        isHovered = true;
        hoverMotion.setTarget (1.0f);
        refreshTooltip();
        repaint();
    }

    void ParamControl::mouseExit (const juce::MouseEvent&)
    {
        isHovered = false;
        hoverMotion.setTarget (0.0f);
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
        targetValue = params.normalisedUserValue (pid);

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
                    safe->targetValue = safe->params.normalisedUserValue (safe->pid);
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
        const bool  dark    = style == Style::dark;

        const float hover = hoverMotion.get();
        const float press = pressMotion.get();

        // The seat occupies the whole allowance the parent left around the cap,
        // less the half pixel the outer lip needs to land inside the component.
        const float seatOuter = r + knobGrooveGap + knobGrooveWidth + knobGrooveLip - 0.5f;
        const float seatMid   = (r + seatOuter) * 0.5f;
        const float seatWidth = seatOuter - r;
        const auto  seatRect  = squareAt (centre, seatOuter);
        const auto  cap       = squareAt (centre, r);

        // 1. The seat.  A cap needs somewhere to be raised FROM, and a recess
        //    reads as a recess only when its near wall - the one on the side the
        //    light comes from - is the wall in shade.
        //
        //    theme::capSeat is deliberately NOT called here.  It was, as a body
        //    pass under a correction pass, back when it ramped its shade from
        //    the wrong side; that sign error has since been fixed in Theme.cpp,
        //    and two passes both darkening the upper-left made the seat far too
        //    heavy.  What follows is the fuller of the two - it has the channel
        //    walls and the outer lip as well as the body - so it is the one that
        //    stayed.  theme::capSeat remains in the vocabulary for callers that
        //    want a plain seat and no channel.
        {
            // Light: the channel carries the depth, so the seat around it only
            // has to say which side of the well the light cannot reach.
            // Light: the channel carries the depth, so the seat only says which
            // side of the well the light cannot reach.  These were 0.70 / 0.60
            // and rendered as a thick dark tyre around every cap - the seat was
            // competing with the cap instead of receiving it.
            const auto wall = dark ? juce::Colours::black.withAlpha (0.42f)
                                   : theme::ceramicDeep.withAlpha (0.26f);

            auto grad = acrossLight (seatRect, wall, wall.withAlpha (wall.getFloatAlpha() * 0.12f), 0.95f);
            grad.addColour (0.62, wall.withAlpha (wall.getFloatAlpha() * 0.35f));

            g.setGradientFill (grad);
            fillRing (g, centre, seatMid, seatWidth);
        }

        // The outer lip, where the seat climbs back to the panel: lit on the far
        // side from the light, lost on the near side, which is the inverse of
        // the cap's own rim and is what makes the two read as opposite curves.
        {
            g.setGradientFill (acrossLight (seatRect,
                                            juce::Colours::black.withAlpha (dark ? 0.40f : 0.14f),
                                            juce::Colours::white.withAlpha (dark ? 0.14f : 0.38f),
                                            0.95f));
            fillRing (g, centre, seatOuter - 0.5f, 1.0f);
        }

        // 2. The groove: a machined channel cut into the seat, carrying the
        //    value arc.  Its floor is shaded along the light axis, and its two
        //    walls face opposite ways, so they catch opposite light.
        {
            const float outerLip = grooveR + knobGrooveWidth * 0.5f;
            const float innerLip = grooveR - knobGrooveWidth * 0.5f;

            // The channel floor.  This was ceramicDeep.darker(0.45) and it made
            // the whole seat read as a thick dark tyre: the arc only occupies
            // part of the circumference, so the bare floor is most of what you
            // see, and it was nearly black against a near-white panel.  A
            // machined channel in light ceramic is only a shade or two below
            // the face around it.
            const auto floorNear = dark ? juce::Colours::black
                                        : theme::ceramicDark.darker (0.10f);
            const auto floorFar  = dark ? theme::glassDeep
                                        : theme::ceramicMid;

            g.setGradientFill (acrossLight (seatRect, floorNear, floorFar, 0.9f));
            strokeArc (g, centre, grooveR, -span, span, knobGrooveWidth);

            // Outer wall: faces inward, so it is lit on the lower-right - the
            // same way round as the seat's own lip, which it runs along, so the
            // two must agree or a seam shows where the groove ends.
            g.setGradientFill (acrossLight (seatRect,
                                            juce::Colours::black.withAlpha (dark ? 0.55f : 0.22f),
                                            juce::Colours::white.withAlpha (dark ? 0.16f : 0.55f),
                                            0.95f));
            strokeArc (g, centre, outerLip - 0.5f, -span, span, 1.0f);

            // Inner wall: faces outward, so it is lit on the upper-left.
            g.setGradientFill (acrossLight (seatRect,
                                            juce::Colours::white.withAlpha (dark ? 0.14f : 0.40f),
                                            juce::Colours::black.withAlpha (0.30f),
                                            0.9f));
            strokeArc (g, centre, innerLip + 0.5f, -span, span, 1.0f);
        }

        // 3. The cap's own shadow, thrown down and to the right into the seat.
        //    It lands before the arc, because the arc is the one thing here that
        //    emits light rather than receiving it.
        theme::contactShadow (g, cap.translated (0.8f, 0.0f), r,
                              2.0f - press * 0.8f, 7.0f, 0.26f + press * 0.06f);

        // 4. The value arc, glowing into the groove.  Unipolar sweeps from the
        //    low end, bipolar from 12 o'clock outward in whichever direction the
        //    value went.
        {
            const float from = isBipolar() ? 0.0f : -span;

            if (std::abs (angle - from) > 1.0e-3f)
            {
                const float lit = juce::jmax (press, hover * 0.55f);

                // theme::glow is a point halo; an arc carries its own by being
                // stroked wide and faint underneath itself.  Three passes, no
                // more: this runs at 30 Hz behind every knob on the page.
                g.setColour (theme::violet.withAlpha (0.16f + lit * 0.16f));
                strokeArc (g, centre, grooveR, from, angle, knobArcWidth * knobArcGlowWidth);

                g.setColour (theme::violet.withAlpha (0.42f + lit * 0.20f));
                strokeArc (g, centre, grooveR, from, angle, knobArcWidth * 1.55f);

                g.setColour (isDragging ? theme::violetLight : theme::violet);
                strokeArc (g, centre, grooveR, from, angle, knobArcWidth);

                // The crest: the arc is a lit filament sitting in a channel, so
                // its own upper edge is the brightest thing on the knob.
                g.setColour (theme::violetLight.withAlpha (0.55f + lit * 0.25f));
                strokeArc (g, centre, grooveR - knobArcWidth * 0.28f, from, angle, 0.9f);
            }
        }

        // 5. The cap, turned from aluminium and lit from the upper-left.
        theme::domeCap (g, centre, r, dark, hover);


        // 5c. The turned face.  theme::domeCap draws its machining at five per
        //     cent of white, which at these radii is below what the display can
        //     resolve, and without it an aluminium cap reads as a pearl.  These
        //     are paired: a cut and the burr beside it, which is what a turned
        //     surface actually is.
        {
            juce::Graphics::ScopedSaveState ss (g);

            juce::Path face;
            face.addEllipse (cap.reduced (2.0f));
            g.reduceClipRegion (face);

            // Faint, and they stop well short of the middle: machining you can
            // count the lines of is corduroy, and a cap that rings all the way
            // in has a bullseye where it should have a plain centre.
            const int rings = juce::jlimit (3, knobTurnRings, (int) (r / 9.0f));

            for (int i = 1; i <= rings; ++i)
            {
                const float t  = (float) i / (float) (rings + 1);
                const float rr = r * (0.95f - t * 0.62f);

                g.setColour (juce::Colours::black.withAlpha (0.024f * (1.0f - t * 0.5f)));
                g.drawEllipse (squareAt (centre, rr), 0.8f);

                g.setColour (juce::Colours::white.withAlpha (0.032f * (1.0f - t * 0.5f)));
                g.drawEllipse (squareAt (centre, rr + 1.0f), 0.8f);
            }
        }

        // 6. One indicator line, the only thing on the cap that moves.  It is
        //    incised rather than printed: the far side of an engraved channel
        //    catches the light, so a bright line trails the dark one.
        {
            const auto in  = pointOnDial (centre, r * knobIndicatorIn,  angle);
            const auto out = pointOnDial (centre, r * knobIndicatorOut, angle);
            const juce::Point<float> away { -theme::lightX * 0.9f, -theme::lightY * 0.9f };

            strokeLine (g, in.translated (away.x, away.y), out.translated (away.x, away.y),
                        knobIndicatorW * 0.7f,
                        dark ? theme::violet.withAlpha (0.30f)
                             : juce::Colours::white.withAlpha (0.55f));

            // A violet indicator on a near-black cap is a lamp, not a mark.
            if (dark)
                strokeLine (g, in, out, knobIndicatorW * 2.6f, theme::violet.withAlpha (0.22f));

            strokeLine (g, in, out, knobIndicatorW, dark ? theme::violet : theme::ink);
        }

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
        /** Paints the pill body and returns the colour its text and icons take.

            `press` and `hover` are 0..1 and animated: the dimensional routines
            in Theme.h sink the object, tighten its shadow and move its specular
            to the bottom edge as `press` rises, which is what a real key does
            and what makes the difference between seeing a press and feeling it. */
        juce::Colour paintPillBackground (juce::Graphics& g, juce::Rectangle<float> b,
                                          PillButton::Style style, float selected,
                                          float hover, float press, float corner)
        {
            using S = PillButton::Style;
            using E = theme::Elevation;

            switch (style)
            {
                case S::ceramic:
                {
                    // Selected reads as pressed IN rather than as tinted: the
                    // face is brighter and it sits lower in its own shadow.
                    const float sink = juce::jmax (press, selected * 0.62f);

                    const auto top = theme::ceramicLight.brighter (selected * 0.30f);
                    const auto bot = theme::ceramicMid  .brighter (selected * 0.45f);

                    theme::raisedCeramic (g, b, corner, E::resting, sink, hover, top, bot);
                    return theme::ink;
                }

                case S::glass:
                {
                    theme::raisedGlass (g, b, corner, E::resting, press, hover,
                                        theme::glassRaised);

                    if (selected <= 0.01f)
                        return theme::glassInk;

                    // Selected glass is lit from inside its own edge rather than
                    // outlined - an outline is a line, a rim is a thickness.
                    theme::innerGlow (g, b, corner, theme::violet, 0.45f * selected, 5.0f);

                    g.setColour (theme::violet.withAlpha (0.35f + 0.55f * selected));
                    g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);

                    return theme::glassInk.interpolatedWith (theme::violet, selected);
                }

                case S::violet:
                {
                    theme::accentSurface (g, b, corner, theme::violet, press, hover);
                    return theme::ink;
                }

                case S::violetOutline:
                {
                    // No body: this one is a rim with air behind it.  What it
                    // gains is a thickness - a violet edge with the chassis
                    // shading below it, and a glow that comes up on hover.
                    const float lit = juce::jmax (hover, press);

                    if (lit > 0.01f)
                        theme::innerGlow (g, b, corner, theme::violet,
                                          0.28f + 0.30f * press, 6.0f);

                    if (press > 0.01f)
                    {
                        g.setColour (theme::violet.withAlpha (0.16f * press));
                        g.fillRoundedRectangle (b, corner);
                    }

                    g.setColour (juce::Colours::black.withAlpha (0.18f * (1.0f - press)));
                    g.drawRoundedRectangle (b.reduced (0.5f).translated (0.0f, 1.0f), corner, 1.0f);

                    g.setColour (selected > 0.5f ? theme::violetLight : theme::violet);
                    g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);

                    return theme::violet;
                }

                case S::dashed:
                default:
                {
                    // The FX "add slot": a hole in the rack that nothing has
                    // been put into yet.  It draws no fill - the rack behind it
                    // already cuts the well this sits over, and filling here
                    // would cover the floor you are meant to see.  What it adds
                    // is the lip of the hole, and a light inside it on hover.
                    if (hover > 0.01f)
                        theme::innerGlow (g, b, corner, theme::violet, 0.30f * hover, 6.0f);

                    juce::Path outline;
                    outline.addRoundedRectangle (b.reduced (0.5f), corner);

                    const float dashes[] = { 4.0f, 3.0f };
                    juce::Path dashed;
                    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);

                    g.setColour (theme::glassEdge.brighter (0.30f * hover)
                                                 .withAlpha (0.8f + 0.2f * hover));
                    g.fillPath (dashed);

                    return theme::glassInkMuted.brighter (0.25f * hover);
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
        selectAnim.setTarget (selected ? 1.0f : 0.0f);
        repaint();
    }

    void PillButton::buttonStateChanged()
    {
        // juce::Button already tracks over / down for us; all this does is turn
        // the two booleans into something that can travel.
        hoverAnim.setTarget (isOver() ? 1.0f : 0.0f);
        pressAnim.setTarget (isDown() ? 1.0f : 0.0f);
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
        const auto b = getLocalBounds().toFloat().reduced (1.0f);

        // The booleans JUCE hands paintButton are the truth; the motions are the
        // same truth part-way there.  Nudging the targets from here keeps them
        // honest if a state change ever arrives without buttonStateChanged.
        hoverAnim.setTarget (highlighted ? 1.0f : 0.0f);
        pressAnim.setTarget (down ? 1.0f : 0.0f);

        const float press = pressAnim.get();
        const float sel   = selectAnim.get();

        auto colour = paintPillBackground (g, b, style, sel, hoverAnim.get(), press, corner);

        if (! isEnabled())
            colour = colour.withAlpha (0.45f);

        // Content travels with the face it is printed on.
        const float travel = press * 0.8f + sel * 0.4f;

        const auto  font     = theme::label (textSize);
        const float iconSize = b.getHeight() * iconRatio;
        const float textW    = theme::trackedWidth (getButtonText(), font, tracking);

        const bool  hasText = getButtonText().isNotEmpty();
        const float gap     = hasText ? pillIconGap : 0.0f;

        float content = textW;
        if (leadingIcon.has_value())  content += iconSize + gap;
        if (trailingIcon.has_value()) content += iconSize + gap;

        float x = b.getCentreX() - content * 0.5f;

        const float cy = b.getCentreY() + travel;

        if (leadingIcon.has_value())
        {
            icons::draw (g, *leadingIcon, squareAt ({ x + iconSize * 0.5f, cy },
                                                    iconSize * 0.5f), colour);
            x += iconSize + gap;
        }

        g.setColour (colour);
        theme::drawTracked (g, getButtonText(),
                            { x, b.getY() + travel, textW, b.getHeight() },
                            font, tracking, juce::Justification::centredLeft);
        x += textW;

        if (trailingIcon.has_value())
        {
            x += gap;
            icons::draw (g, *trailingIcon, squareAt ({ x + iconSize * 0.5f, cy },
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
        activeAnim.setTarget (active ? 1.0f : 0.0f);
        repaint();
    }

    void IconButton::buttonStateChanged()
    {
        hoverAnim.setTarget (isOver() ? 1.0f : 0.0f);
        pressAnim.setTarget (isDown() ? 1.0f : 0.0f);
    }

    void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        hoverAnim.setTarget (highlighted ? 1.0f : 0.0f);
        pressAnim.setTarget (down ? 1.0f : 0.0f);

        const auto  b    = getLocalBounds().toFloat().reduced (1.0f);
        const float d    = juce::jmin (b.getWidth(), b.getHeight());
        const auto  disc = squareAt (b.getCentre(), d * 0.5f);

        const float hover = hoverAnim.get();
        const float press = pressAnim.get();
        const float lit   = activeAnim.get();

        auto colour = normalColour.interpolatedWith (activeColour, lit);

        // Round or square, the surface is the same object lit from the same
        // place; only its corner radius differs.
        switch (style)
        {
            case Style::plain:
                break;

            case Style::glassRound:
                theme::raisedGlass (g, disc, d * 0.5f, theme::Elevation::resting,
                                    press, hover, theme::glassRaised);
                break;

            case Style::ceramicRound:
                theme::raisedCeramic (g, disc, d * 0.5f, theme::Elevation::resting,
                                      press, hover);
                break;

            case Style::glassSquare:
                theme::raisedGlass (g, b, iconSquareCorner, theme::Elevation::resting,
                                    press, hover, theme::glassRaised);
                break;

            case Style::violetSquare:
                // The active toolbar button in the reference viewport.  It is
                // not a violet chip sitting on the glass - it is the glass with
                // a light behind it, so it is cut IN and lit from inside.
                theme::recessedWell (g, b, iconSquareCorner,
                                     theme::glassDeep.interpolatedWith (theme::violet, 0.20f),
                                     0.9f + press * 0.4f);

                theme::innerGlow (g, b, iconSquareCorner, theme::violet,
                                  0.55f + hover * 0.20f, 6.0f);

                g.setColour (theme::violet.withAlpha (0.55f + hover * 0.25f));
                g.drawRoundedRectangle (b.reduced (0.5f), iconSquareCorner, 1.0f);

                colour = activeColour;
                break;
        }

        if (! isEnabled())
            colour = colour.withAlpha (0.4f);
        else if (style == Style::plain)
            colour = colour.brighter (0.25f * hover * (1.0f - lit));

        // The glyph is printed on the face, so it travels with it.
        icons::draw (g, icon, squareAt (b.getCentre().translated (0.0f, press * 0.8f),
                                        d * ratio * 0.5f), colour);
    }

    // =======================================================================
    //  PowerButton
    // =======================================================================
    PowerButton::PowerButton (Tint t)
        : juce::Button (juce::String()),
          tint (t),
          // Mint is the FX rack, which is glass; violet is the atmosphere
          // modules, which are ceramic.  See the note in Widgets.h.
          seat (t == Tint::mint ? Seat::glass : Seat::ceramic)
    {
        // A power button is a latch, so callers only ever read getToggleState().
        setClickingTogglesState (true);
    }

    void PowerButton::buttonStateChanged()
    {
        hoverAnim.setTarget (isOver() ? 1.0f : 0.0f);
        pressAnim.setTarget (isDown() ? 1.0f : 0.0f);
        onAnim   .setTarget (getToggleState() ? 1.0f : 0.0f);
    }

    void PowerButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        hoverAnim.setTarget (highlighted ? 1.0f : 0.0f);
        pressAnim.setTarget (down ? 1.0f : 0.0f);
        onAnim   .setTarget (getToggleState() ? 1.0f : 0.0f);

        const auto  b      = getLocalBounds().toFloat();
        const auto  centre = b.getCentre();
        const float r      = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 1.0f;
        const auto  accent = tint == Tint::mint ? theme::mint : theme::violet;
        const auto  disc   = squareAt (centre, r);

        const float hover = hoverAnim.get();
        const float press = pressAnim.get();
        const float on    = onAnim.get();

        // The halo the lamp throws onto whatever it is mounted in.  This reads
        // first and from across the window: it is the "alive" indicator on
        // every FX card and every atmosphere module.  Smaller on ceramic,
        // where the surface is light and a wide halo turns into a smudge.
        if (on > 0.01f && seat == Seat::ceramic)
            theme::outerGlow (g, disc, r, accent,
                              (0.20f + hover * 0.10f) * on * (1.0f - press * 0.25f), 5.0f);

        if (on > 0.01f && seat == Seat::glass)
        {
            // Clipped to a circle, so this half of the halo can never square
            // off against the component edge however tight the box is - which
            // is what it used to do, landing on the FX cards as a mint tile.
            // The parent draws the rest; see the note on haloAlpha in Widgets.h.
            juce::Graphics::ScopedSaveState ss (g);

            juce::Path round;
            round.addEllipse (getLocalBounds().toFloat());
            g.reduceClipRegion (round);

            theme::outerGlow (g, disc, r, accent,
                              (haloAlpha + hover * 0.18f) * on * (1.0f - press * 0.25f),
                              haloSpread);
        }

        if (seat == Seat::ceramic)
        {
            // UI spec section 8: "ceramic circles r 13 with a violet power
            // glyph".  A raised disc rather than a lens - a lamp set into
            // ceramic does not look like a lamp set into glass, and the module
            // headers are ceramic.  The light comes from the GLYPH here, not
            // from behind the face.
            theme::contactShadow (g, disc, r, 2.0f - press * 0.9f, 6.0f,
                                  0.22f - press * 0.06f);

            theme::domeCap (g, centre.translated (0.0f, press * 0.7f), r, false, hover);

            if (on > 0.01f)
                theme::glow (g, centre, r * 0.72f, accent, 0.30f * on);

            g.setColour (theme::ceramicEdge.interpolatedWith (accent, on * 0.85f)
                             .withAlpha (0.60f + 0.35f * on));
            g.drawEllipse (disc.reduced (1.0f), 1.0f + 0.4f * on);

            icons::draw (g, icons::Icon::power,
                         squareAt (centre.translated (0.0f, press * 0.7f), r * 0.5f),
                         theme::inkFaint.interpolatedWith (accent.darker (0.25f), on)
                             .brighter (0.18f * hover),
                         1.6f);
            return;
        }

        // A dark lens set into the card, not a button printed on it: dead dark
        // glass when it is off, and the same glass with something behind it
        // when it is on.
        theme::recessedWell (g, disc, r,
                             theme::glassDeep.interpolatedWith (accent, 0.12f * on),
                             1.0f + press * 0.35f);

        if (on > 0.01f)
        {
            theme::innerGlow (g, disc, r, accent, 0.55f * on, 5.0f);
            theme::glow (g, centre, r * 0.85f, accent, 0.26f * on);   // the lamp itself
        }

        // The ring: the bezel the lens is held by, which is what actually
        // carries the colour at this size.
        g.setColour (theme::glassEdge.interpolatedWith (accent, on)
                         .withAlpha (0.55f + 0.40f * on + 0.10f * hover));
        g.drawEllipse (disc.reduced (1.0f), 1.3f + 0.4f * on);

        icons::draw (g, icons::Icon::power,
                     squareAt (centre.translated (0.0f, press * 0.6f), r * 0.5f),
                     theme::glassInkFaint.interpolatedWith (accent, on)
                         .brighter (0.20f * hover),
                     1.6f);
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

        const int   count  = juce::jmax (1, options.size());
        const auto  b      = getLocalBounds().toFloat().reduced (0.5f);
        const float corner = juce::jmin (layout::radiusPill, b.getHeight() * segmentCornerRatio);
        const float inner  = juce::jmax (0.0f, corner - segmentInset);

        // Where the raised segment is, as opposed to which one is selected: it
        // travels, and the travel is most of what tells the eye that the thing
        // sliding is an object and the thing it slides in is a channel.
        const float lastIndex = (float) juce::jmax (1, count - 1);
        selectPos.setTarget ((float) selectedIndex / lastIndex);

        const float pos = selectPos.get() * lastIndex;

        // 1. The track, cut into the chassis.  A raised segment only reads as
        //    raised against something that reads as below the surface.
        theme::recessedWell (g, b, corner,
                             style == Style::recessed ? theme::ceramicMid.darker (0.06f)
                                                      : theme::ceramicDark,
                             style == Style::recessed ? 0.75f : 1.15f);

        // 2. The selected segment, a solid object sitting in that well.
        {
            const float w   = b.getWidth() / (float) count;
            const auto  seg = juce::Rectangle<float> (b.getX() + w * pos, b.getY(), w, b.getHeight())
                                  .reduced (segmentInset, segmentInset);

            // Clipped to the track, because a thing inside a channel cannot
            // throw its shadow or its glow over the channel's own walls.
            juce::Graphics::ScopedSaveState ss (g);

            juce::Path well;
            well.addRoundedRectangle (b.reduced (0.5f), corner);
            g.reduceClipRegion (well);

            switch (style)
            {
                case Style::violetFill:
                    theme::accentSurface (g, seg, inner, theme::violet, 0.0f, 0.0f);
                    break;

                case Style::darkFill:
                    theme::raisedGlass (g, seg, inner, theme::Elevation::resting,
                                        0.0f, 0.0f, theme::glassMid);
                    break;

                case Style::recessed:
                default:
                    theme::raisedCeramic (g, seg, inner, theme::Elevation::resting, 0.0f, 0.0f);
                    break;
            }
        }

        // 3. The labels.  A label belongs to the chip while the chip is under
        //    it, so its colour crosses over as the chip travels rather than
        //    snapping when the index changes.
        const auto font = theme::label (textSize);

        for (int i = 0; i < options.size(); ++i)
        {
            const auto  seg  = segmentBounds (i).reduced (segmentInset, segmentInset);
            const float onIt = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs ((float) i - pos));

            const auto off = (i == hoverIndex ? theme::ink : theme::inkMuted);
            const auto onC = style == Style::darkFill ? theme::glassInk
                                                      : theme::ink;   // dark on violet reads better

            g.setColour (off.interpolatedWith (onC, onIt));
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

        // Restored state is not a gesture, so it must not be seen to travel.
        animated.snapTo (state ? 1.0f : 0.0f);

        setTooltip (tooltipFor (p));
        repaint();
    }

    void ToggleSwitch::setToggleState (bool shouldBeOn, juce::NotificationType notification)
    {
        if (shouldBeOn == state)
            return;

        state = shouldBeOn;

        // The thumb travels.  It is driven by the shared motion ticker rather
        // than by a Timer of this switch's own: there are eight of these in the
        // preserve row alone, and at most one of them is ever moving.
        animated.setTarget (state ? 1.0f : 0.0f);
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
            const bool live = boundIndex (*boundParams, boundPid, state ? 1 : 0) > 0;

            if (live != state)
            {
                state = live;
                animated.setTarget (state ? 1.0f : 0.0f);
            }
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
        const float on    = animated.get();
        const float hover = hoverAnim.get();

        // The track is a channel cut into the chassis, so the thumb has a floor
        // to sit on rather than a colour to sit next to.
        // ceramicDark is only a shade off the panel it is cut into, and at
        // 26 x 14 that is not enough contrast for a slot to read as a slot, so
        // the off state goes down to the bottom of the ceramic ramp.
        theme::recessedWell (g, track, r,
                             theme::ceramicDeep.darker (0.34f)
                                 .interpolatedWith (theme::violetDeep, on)
                                 .brighter (hover * 0.08f),
                             1.35f);

        if (on > 0.01f)
            theme::innerGlow (g, track, r, theme::violetLight, 0.45f * on, 4.0f);

        const juce::Point<float> knobCentre { juce::jmap (on, track.getX() + r,
                                                          track.getRight() - r),
                                              track.getCentreY() };

        const auto thumb = squareAt (knobCentre, knobR);

        // The thumb is a raised ceramic object with its own contact shadow, and
        // theme::raisedCeramic draws exactly that - a circle is a rounded
        // rectangle whose corner is its own half-height.
        theme::raisedCeramic (g, thumb, knobR, theme::Elevation::resting, 0.0f, hover);

        // Lit from underneath when the switch is on, which is what makes the
        // violet read as coming from the channel rather than from the thumb.
        if (on > 0.01f)
        {
            g.setColour (theme::violetDeep.withAlpha (0.55f * on));
            g.drawEllipse (thumb.reduced (0.5f), 1.0f);
        }
    }

    void ToggleSwitch::mouseDown (const juce::MouseEvent&)
    {
        setToggleState (! state);
    }

    void ToggleSwitch::mouseEnter (const juce::MouseEvent&)
    {
        hoverAnim.setTarget (1.0f);
        repaint();
    }

    void ToggleSwitch::mouseExit (const juce::MouseEvent&)
    {
        hoverAnim.setTarget (0.0f);
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
                // An emitting object: a halo on the chassis, a rim of its own
                // colour, and a core brighter than either.
                const auto lamp = squareAt (dot, layout::macro::genDotRActive);

                theme::glow (g, dot, genGlowRadius, theme::violet, 0.45f);
                theme::outerGlow (g, lamp, layout::macro::genDotRActive, theme::violet,
                                  0.40f, 5.0f);

                g.setColour (theme::violet);
                g.fillEllipse (lamp);

                g.setGradientFill (acrossLight (lamp, theme::violetLight, theme::violetDeep, 0.9f));
                g.fillEllipse (lamp.reduced (0.6f));

                g.setColour (theme::violetLight.withAlpha (0.9f));
                g.fillEllipse (squareAt (dot.translated (theme::lightX * layout::macro::genDotRActive * 0.35f,
                                                         theme::lightY * layout::macro::genDotRActive * 0.35f),
                                         layout::macro::genDotRActive * 0.34f));
            }
            else
            {
                // A blind hole in the ceramic: dark floor, its near wall in
                // shade, a catch of light on the far one.
                const auto well = squareAt (dot, layout::macro::genDotR);
                const float lift = (i == hover ? 0.15f : 0.0f);

                theme::recessedWell (g, well, layout::macro::genDotR,
                                     theme::ceramicDeep.brighter (lift), 1.25f);
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

        const float x     = juce::jmap (value, x0, x1);
        const float hover = hoverAnim.get();
        const float press = pressAnim.get();

        // The track is a channel, not a rule: 3 px of well is the least that can
        // carry an inner shadow and still read as a hairline at this size.
        //
        // It is cut into glass - this slider only exists in the viewport - so it
        // is darker than its ground rather than lighter, and it is the cap that
        // carries the light.
        const juce::Rectangle<float> track (x0 - 1.0f, y - 1.5f,
                                            (x1 - x0) + 2.0f, 3.0f);

        theme::recessedWell (g, track, 1.5f, theme::glassDeep, 1.0f);

        g.setColour (theme::glassEdge.withAlpha (0.8f));
        g.drawRoundedRectangle (track.reduced (0.25f), 1.5f, 0.6f);

        // The travelled part of the channel is lit from within.
        if (x > x0 + 0.5f)
        {
            const juce::Rectangle<float> lit (track.getX(), track.getY(),
                                              x - track.getX(), track.getHeight());

            g.setColour (theme::violet.withAlpha (0.28f + hover * 0.14f));
            g.fillRoundedRectangle (lit.expanded (0.0f, 1.2f), 2.2f);

            g.setColour (theme::violet);
            g.fillRoundedRectangle (lit, 1.5f);
        }

        const juce::Point<float> cap { x, y };
        const float capR = hairlineCapR - press * 0.4f;

        theme::contactShadow (g, squareAt (cap, capR).translated (0.6f, 0.0f), capR,
                              1.6f - press * 0.6f, 5.0f, 0.26f);

        theme::domeCap (g, cap, capR, false, hover);
    }

    void HairlineSlider::mouseDown (const juce::MouseEvent& e)
    {
        pressAnim.setTarget (1.0f);
        setFromMouse (e);
    }

    void HairlineSlider::mouseDrag (const juce::MouseEvent& e)
    {
        setFromMouse (e);
    }

    void HairlineSlider::mouseUp (const juce::MouseEvent&)
    {
        pressAnim.setTarget (0.0f);
    }

    void HairlineSlider::mouseEnter (const juce::MouseEvent&)
    {
        hoverAnim.setTarget (1.0f);
    }

    void HairlineSlider::mouseExit (const juce::MouseEvent&)
    {
        hoverAnim.setTarget (0.0f);
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

        // A panel is the largest raised object in the chassis, and it is lit by
        // the same light as the smallest: the specular runs along its top edge,
        // the bevel along its bottom, and the sheen comes from the upper-left.
        //
        // Elevation::resting rather than ::raised, because spec section 1 fixes
        // the chassis bevel at 60 % white over 22 % black and that is what
        // resting draws.  A brighter edge on a panel this size stops reading as
        // a bevel and starts reading as a drawn line.
        theme::raisedCeramic (g, b, corner, theme::Elevation::resting);
    }

    GlassPanel::GlassPanel (float cornerRadius)
        : corner (cornerRadius)
    {
    }

    void GlassPanel::paint (juce::Graphics& g)
    {
        // Glass is a cut-out, so it never carries a drop shadow (spec section 1)
        // - it carries the opposite, an inner shadow under its top edge and a
        // catch of light along its bottom one.
        const auto b = getLocalBounds().toFloat().reduced (0.5f);

        theme::recessedWell (g, b, corner, fill, 1.2f);

        // Four one-pixel bands do not reach far enough for a cut-out this deep,
        // so the top of the well is carried further in by a gradient.  It lands
        // over the bands, which are black already, and leaves the catch of light
        // along the bottom untouched.
        {
            const float depth = juce::jmin (16.0f, b.getHeight() * 0.35f);

            juce::Graphics::ScopedSaveState ss (g);

            juce::Path clip;
            clip.addRoundedRectangle (b, corner);
            g.reduceClipRegion (clip);

            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f),
                                                     b.getCentreX(), b.getY(),
                                                     juce::Colours::transparentBlack,
                                                     b.getCentreX(), b.getY() + depth, false));
            g.fillRect (b.withHeight (depth));
        }

        g.setColour (theme::glassEdge);
        g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
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
