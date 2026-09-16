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

        // -- body ----------------------------------------------------------
        // A card is a cut-out in the panel's glass, so it carries the same
        // vocabulary the panel does - flat fill, inner top shadow, glassEdge
        // hairline - with a raised fill and the card radius.  Glass never gets
        // a drop shadow, so a dragged card is lifted by brightening its fill
        // and rimming it instead.
        theme::glassSurface (g, b, radiusCard, dragging ? theme::glassEdge : theme::glassRaised);

        if (selected || dragging)
        {
            g.setColour (selected ? theme::violet : theme::violetDeep);
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

            icons::draw (g, slot.glyph, area, colour, 1.6f);
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

    void FXModuleCard::mouseMove (const juce::MouseEvent& e)
    {
        const auto h = hitAt (e.position);
        const auto shown = (h == Hit::body ? Hit::none : h);

        if (shown == hovered)
            return;

        hovered = shown;
        repaint();
    }

    void FXModuleCard::mouseExit (const juce::MouseEvent&)
    {
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
