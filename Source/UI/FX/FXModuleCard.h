#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "../Theme.h"
#include "../Layout.h"
#include "../Components/Icons.h"
#include "../Components/Widgets.h"
#include "../../Plugin/PluginProcessor.h"

namespace nacar::ui
{
    /**
        One module in the FX chain (UI spec section 7).

        A glass card carrying, top to bottom: a bypass `-` glyph at the top
        left, the module name, a remove `x` glyph at the top right, the module's
        own large glyph in the middle and a mint power ring at the bottom right.

        The card owns only what belongs to a single module: the power ring's
        binding to the module's enable parameter, and the hit testing for its
        four controls.  Everything that is a property of the *chain* - order,
        selection, locks - lives in FXChainView, which drives the card through
        setSelected() / setLocked() / setBypassed() and listens to the callbacks
        below.  That keeps one writer for the FXCHAIN state tree.
    */
    class FXModuleCard : public juce::Component
    {
    public:
        /** Static description of a chain slot: its persisted name, the
            parameter its power ring switches, and its glyph. */
        struct Slot
        {
            const char* name;
            PID         enable;
            icons::Icon glyph;
        };

        FXModuleCard (NacarProcessor&, Slot);
        ~FXModuleCard() override;

        const Slot&  getSlot() const noexcept     { return slot; }
        juce::String getSlotName() const          { return juce::String (slot.name); }

        void setSelected (bool);
        bool getSelected() const noexcept         { return selected; }

        void setLocked (bool);
        bool getLocked() const noexcept           { return locked; }

        void setBypassed (bool);
        bool getBypassed() const noexcept         { return bypassed; }

        /** Raises the card visually while the chain is reordering it. */
        void setDragging (bool);

        /** True while the pointer is over the card's body.

            FXChainView needs this because the things a raised card throws onto
            the rack - its contact shadow, its halo - are painted by the rack,
            and they have to grow when the card lifts towards the pointer. */
        bool getCardHovered() const noexcept      { return cardHovered; }

        /** How far off the rack this card is sitting right now.

            Asked by FXChainView so that the shadow it draws behind the card and
            the light the card draws on itself cannot disagree. */
        theme::Elevation getElevation() const noexcept;

        // -- callbacks, all owned by FXChainView -----------------------------
        std::function<void()>     onSelect;          ///< the card was clicked
        std::function<void()>     onRemove;          ///< the `x` was clicked
        std::function<void (bool)> onBypassRequested; ///< the `-` was clicked
        std::function<void (bool)> onLockRequested;   ///< from the right-click menu

        std::function<void()>     onHoverChanged;    ///< the rack must redraw what we throw

        std::function<void (const juce::MouseEvent&)> onDragStart;
        std::function<void (const juce::MouseEvent&)> onDragMove;
        std::function<void (const juce::MouseEvent&)> onDragEnd;

        // -- juce::Component --------------------------------------------------
        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        enum class Hit { none, bypass, remove, body };

        Hit hitAt (juce::Point<float>) const;
        juce::Rectangle<float> bypassGlyphArea() const;
        juce::Rectangle<float> removeGlyphArea() const;
        juce::Rectangle<float> moduleGlyphArea() const;

        void showContextMenu();
        void powerStateChanged();
        void setCardHovered (bool);

        NacarProcessor& processor;
        Slot slot;

        PowerButton power { PowerButton::Tint::mint };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;

        bool selected = false;
        bool locked   = false;
        bool bypassed = false;
        bool dragging = false;
        bool powered  = false;

        Hit  hovered      = Hit::none;
        bool cardHovered  = false;
        Hit  pressedOn    = Hit::none;
        bool dragStarted  = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FXModuleCard)
    };
}
