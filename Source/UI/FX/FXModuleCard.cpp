#include "FXModuleCard.h"

namespace nacar::ui
{
    using namespace layout;

    // -----------------------------------------------------------------------
    //  Card-internal geometry.
    //
    //  Layout.h is frozen and gives the card's outer rect (112 x 100 at
    //  fx::cardY) plus the name's type size, but UI spec section 7 describes
    //  the card's contents only as an ordered list - "8 px in", "centred",
    //  "in the middle", "bottom-right".  Those four phrases are resolved here,
    //  in card-local coordinates, rather than in a component that has no
    //  business inventing chassis coordinates.
    // -----------------------------------------------------------------------
    static constexpr float kCornerInset    =  8.0f;   // spec: "8 px in"
    static constexpr float kCornerGlyph    =  9.0f;   // the - and x glyphs
    static constexpr float kCornerHit      = 18.0f;   // their (larger) hit target
    static constexpr float kNameBaseline   = 27.0f;
    static constexpr float kLockGlyph      =  8.0f;
    static constexpr float kLockGap        =  4.0f;
    static constexpr float kModuleGlyph    = 34.0f;   // spec: "a large module glyph"
    static constexpr float kModuleGlyphY   = 56.0f;
    static constexpr float kPowerRadius    = 11.0f;
    static constexpr float kPowerInset     = 21.0f;   // centre, in from bottom-right
    static constexpr float kDragThreshold  =  4.0f;   // px before a click becomes a drag

    // How far the module glyph stands off the card face.  It is a distance in
    // pixels along theme::lightX / lightY, never a direction of its own: the
    // glyph's shadow falls exactly where every other shadow in the instrument
    // falls, which is the only reason a stroke drawn twice reads as relief
    // rather than as a printing error.
    static constexpr float kGlyphRelief    =  1.5f;


    // -----------------------------------------------------------------------
    FXModuleCard::FXModuleCard (NacarProcessor& p, Slot s)
        : processor (p), slot (s)
    {
        setInterceptsMouseClicks (true, true);

        const auto& def = ParameterRegistry::definition (slot.enable);

        power.setClickingTogglesState (true);
        // The same two-line shape ui::tooltipFor() gives every bound widget.
        power.setTooltip (juce::String (def.name) + "\n" + juce::String (def.tooltip));
        power.onStateChange = [this] { powerStateChanged(); };
        addAndMakeVisible (power);

        // The power ring is the module's enable parameter, nothing else.  The
        // attachment sends an initial update, so `powered` is correct from the
        // first paint.
        if (auto* apvts = processor.getParameters().state())
            powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                                  (*apvts, ParameterRegistry::idOf (slot.enable), power);

        powered = power.getToggleState();
    }

    FXModuleCard::~FXModuleCard()
    {
        // The attachment refers to the button, so it must die first.
        powerAttachment.reset();
    }

    // -----------------------------------------------------------------------
    //  State
    // -----------------------------------------------------------------------
    void FXModuleCard::setSelected (bool shouldBeSelected)
    {
        if (selected == shouldBeSelected)
            return;

        selected = shouldBeSelected;
        repaint();
    }

    void FXModuleCard::setLocked (bool shouldBeLocked)
    {
        if (locked == shouldBeLocked)
            return;

        locked = shouldBeLocked;
        repaint();
    }

    void FXModuleCard::setBypassed (bool shouldBeBypassed)
    {
        if (bypassed == shouldBeBypassed)
            return;

        bypassed = shouldBeBypassed;
        repaint();
    }

    void FXModuleCard::setDragging (bool shouldBeDragging)
    {
        if (dragging == shouldBeDragging)
            return;

        dragging = shouldBeDragging;
        repaint();
    }

    theme::Elevation FXModuleCard::getElevation() const noexcept
    {
        // A card at rest is a module seated in the rack; hovered it comes up
        // towards the pointer; dragged it is off the rack altogether.
        if (dragging)
            return theme::Elevation::floating;

        return cardHovered ? theme::Elevation::floating : theme::Elevation::raised;
    }

    void FXModuleCard::setCardHovered (bool shouldBeHovered)
    {
        if (cardHovered == shouldBeHovered)
            return;

        cardHovered = shouldBeHovered;
        repaint();

        // The rack draws our shadow and our halo, so it has to redraw too.
        if (onHoverChanged != nullptr)
            onHoverChanged();
    }

    void FXModuleCard::powerStateChanged()
    {
        const bool now = power.getToggleState();

        if (now == powered)
            return;

        powered = now;
        repaint();
    }

    // -----------------------------------------------------------------------
    //  Geometry
    // -----------------------------------------------------------------------
    juce::Rectangle<float> FXModuleCard::bypassGlyphArea() const
    {
        return { kCornerInset, kCornerInset, kCornerGlyph, kCornerGlyph };
    }

    juce::Rectangle<float> FXModuleCard::removeGlyphArea() const
    {
        return { (float) getWidth() - kCornerInset - kCornerGlyph, kCornerInset,
                 kCornerGlyph, kCornerGlyph };
    }

    juce::Rectangle<float> FXModuleCard::moduleGlyphArea() const
    {
        return centredSquare ({ (float) getWidth() * 0.5f, kModuleGlyphY }, kModuleGlyph * 0.5f);
    }

    FXModuleCard::Hit FXModuleCard::hitAt (juce::Point<float> p) const
    {
        const auto grow = (kCornerHit - kCornerGlyph) * 0.5f;

        if (bypassGlyphArea().expanded (grow).contains (p))
            return Hit::bypass;

        if (removeGlyphArea().expanded (grow).contains (p))
            return Hit::remove;

        return Hit::body;
    }

    void FXModuleCard::resized()
    {
        const auto centre = juce::Point<float> ((float) getWidth()  - kPowerInset,
                                                (float) getHeight() - kPowerInset);

        power.setBounds (centredSquare (centre, kPowerRadius).toNearestInt());
    }

    // -----------------------------------------------------------------------
    //  Paint
    // -----------------------------------------------------------------------
    void FXModuleCard::paint (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();

        // Hover lifts the card towards the pointer rather than tinting it: the
        // fill brightens a little, the top edge catches more light, and - the
        // part that actually sells it - the shadow FXChainView draws behind us
        // spreads, because we told it we are further off the rack.
        const float hover = (cardHovered && ! dragging) ? 1.0f : 0.0f;

        // -- body ----------------------------------------------------------
        // A module seated in the rack: a lit top edge, a shaded bottom edge and
        // a face barely lighter than the well behind it, because a dark object
        // on a dark ground can only be read by its edges.
        //
        // The elevation passed here is deliberately `flush`.  raisedGlass()
        // derives exactly one thing from elevation - the contact shadow - and
        // that shadow falls OUTSIDE these bounds, where a child component
        // cannot paint, so drawing it here would only paint layers that the
        // body fill immediately covers.  FXChainView draws it for us, behind
        // the card, at getElevation(); the two cannot drift apart because both
        // read that one function.
        theme::raisedGlass (g, b, radiusCard, theme::Elevation::flush, 0.0f, hover,
                            dragging ? theme::glassEdge : theme::glassRaised);

        // -- the active module is lit from within ---------------------------
        if (selected)
        {
            // Small numbers on purpose.  Wound up, innerGlow stops being light
            // coming through the card's edge and becomes a violet tube drawn
            // round it, which is the outlined look this is meant to replace.
            theme::innerGlow (g, b, radiusCard, theme::violet, 0.26f + hover * 0.08f, 5.0f);

            g.setColour (theme::violet);
            g.drawRoundedRectangle (b.reduced (0.5f), radiusCard, 1.0f);
        }
        else if (dragging)
        {
            g.setColour (theme::violetDeep);
            g.drawRoundedRectangle (b.reduced (0.5f), radiusCard, 1.0f);
        }

        // Bypassed modules are still in the chain but are not doing anything,
        // so the whole card reads back.
        const float content = bypassed ? 0.38f : 1.0f;

        // -- bypass, top-left ----------------------------------------------
        {
            const auto colour = bypassed ? theme::violet
                                         : (hovered == Hit::bypass ? theme::glassInk
                                                                   : theme::glassInkMuted);
            // No relief on this one or the x: at nine pixels the shadow lands
            // inside the stroke and reads as a blur, not as a raised glyph.
            icons::draw (g, icons::Icon::minus, bypassGlyphArea(), colour, 1.5f);
        }

        // -- name, centred, with the lock beside it when locked -------------
        {
            const auto name = getSlotName();
            const auto font = theme::label (fx::nameSize);
            const float nameWidth = theme::trackedWidth (name, font, fx::nameTrack);
            const float total = nameWidth + (locked ? kLockGlyph + kLockGap : 0.0f);

            float x = (b.getWidth() - total) * 0.5f;

            if (locked)
            {
                // A locked module is one AGAIN will not touch (master spec 116),
                // so the lock is stated in the selection colour.
                icons::draw (g, icons::Icon::lock,
                             { x, kNameBaseline - kLockGlyph, kLockGlyph, kLockGlyph },
                             theme::violet.withMultipliedAlpha (content), 1.3f);
                x += kLockGlyph + kLockGap;
            }

            const auto ink = (powered ? theme::glassInk : theme::glassInkMuted)
                                 .withMultipliedAlpha (content);

            glassLabel (g, name, { x, kNameBaseline }, fx::nameSize, fx::nameTrack, ink);
        }

        // -- remove, top-right ---------------------------------------------
        icons::draw (g, icons::Icon::cross, removeGlyphArea(),
                     hovered == Hit::remove ? theme::glassInk : theme::glassInkFaint, 1.5f);

        // -- module glyph ---------------------------------------------------
        {
            const auto area = moduleGlyphArea();

            if (selected)
                theme::glow (g, area.getCentre(), kModuleGlyph * 0.92f,
                             theme::violet, 0.30f * content);

            // Violet when selected, mint-neutral glass ink otherwise, and
            // dimmed when the module's power is off.
            auto colour = selected ? theme::violet : theme::glassInk;
            colour = colour.withMultipliedAlpha ((powered ? 1.0f : 0.45f) * content);

            // The glyph stands off the card face: its own shadow falls away
            // from the light, so it sits ON the card rather than in it.
            icons::draw (g, slot.glyph,
                         area.translated (-theme::lightX * kGlyphRelief,
                                          -theme::lightY * kGlyphRelief),
                         theme::glassDeep.darker (0.8f).withAlpha (0.8f * content), 1.6f);

            icons::draw (g, slot.glyph, area, colour, 1.6f);
        }

        // -- the light the power ring throws onto the card ------------------
        // The ring paints itself: PowerButton's glass seat is a recessed lens
        // with its own mint halo when lit and dead dark glass when not, and
        // nothing here draws a second lamp.  But that halo is eight pixels of
        // spread inside a twenty-two pixel component, so it is cut off square
        // by the ring's own bounds and reads as a mint tile on the card.  The
        // part that lands on the CARD is the card's to draw, and it is drawn
        // with the ring's box excluded and to the ring's own numbers, so the
        // two halves meet at the seam instead of adding up across it.
        const auto ring = power.getBounds();

        if (powered && ! ring.isEmpty())
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.excludeClipRegion (ring);

            const auto r = ring.toFloat();
            theme::outerGlow (g, r, r.getWidth() * 0.5f, theme::mint,
                              PowerButton::haloAlpha, PowerButton::haloSpread);
        }
    }

    // -----------------------------------------------------------------------
    //  Mouse
    // -----------------------------------------------------------------------
    void FXModuleCard::mouseDown (const juce::MouseEvent& e)
    {
        dragStarted = false;

        if (e.mods.isPopupMenu())
        {
            pressedOn = Hit::none;
            showContextMenu();
            return;
        }

        pressedOn = hitAt (e.position);
    }

    void FXModuleCard::mouseDrag (const juce::MouseEvent& e)
    {
        if (pressedOn != Hit::body)
            return;

        if (! dragStarted)
        {
            if ((float) e.getDistanceFromDragStart() < kDragThreshold)
                return;

            dragStarted = true;

            if (onDragStart != nullptr)
                onDragStart (e);
        }

        if (onDragMove != nullptr)
            onDragMove (e);
    }

    void FXModuleCard::mouseUp (const juce::MouseEvent& e)
    {
        if (dragStarted)
        {
            dragStarted = false;
            pressedOn = Hit::none;

            if (onDragEnd != nullptr)
                onDragEnd (e);

            return;
        }

        const auto released = hitAt (e.position);
        const auto pressed  = pressedOn;
        pressedOn = Hit::none;

        if (released != pressed)
            return;

        switch (released)
        {
            case Hit::bypass:
                // FXChainView owns the write; it calls setBypassed() back.
                if (onBypassRequested != nullptr)
                    onBypassRequested (! bypassed);
                break;

            case Hit::remove:
                // This can delete the card, so nothing may touch `this` after it.
                if (onRemove != nullptr)
                    onRemove();
                return;

            case Hit::body:
                if (onSelect != nullptr)
                    onSelect();
                break;

            case Hit::none:
            default:
                break;
        }
    }

    void FXModuleCard::mouseEnter (const juce::MouseEvent&)
    {
        setCardHovered (true);
    }

    void FXModuleCard::mouseMove (const juce::MouseEvent& e)
    {
        setCardHovered (true);

        const auto h = hitAt (e.position);
        const auto shown = (h == Hit::body ? Hit::none : h);

        if (shown == hovered)
            return;

        hovered = shown;
        repaint();
    }

    void FXModuleCard::mouseExit (const juce::MouseEvent&)
    {
        // Moving onto the power ring is an exit as far as we are concerned -
        // the ring is a child component - but the card has not stopped being
        // under the pointer, and letting it drop would make it flinch away
        // just as the user reaches for it.
        setCardHovered (isMouseOver (true));

        if (hovered == Hit::none)
            return;

        hovered = Hit::none;
        repaint();
    }

    void FXModuleCard::showContextMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader (getSlotName());
        m.addItem (1, "Lock",   ! locked, locked);
        m.addItem (2, "Unlock",   locked, ! locked);

        juce::Component::SafePointer<FXModuleCard> safe (this);

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe] (int result)
                         {
                             if (safe == nullptr || result == 0)
                                 return;

                             if (safe->onLockRequested != nullptr)
                                 safe->onLockRequested (result == 1);
                         });
    }
}
