#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "FXModuleCard.h"

#include "../Theme.h"
#include "../Layout.h"
#include "../EditorHost.h"
#include "../Components/Icons.h"
#include "../Components/Widgets.h"
#include "../../Plugin/PluginProcessor.h"

namespace nacar::ui
{
    /**
        The FX chain (UI spec section 7).

        A recessed glass panel holding the six FX modules as drag-reorderable
        cards, a `+` that re-adds a removed module, a collapse control and a
        dashed add-slot at the end of the row.

        Displayed order *is* DSP order (master spec 84), so this view is the
        only writer of `ids::fxOrder`: the card row is not a picture of the
        chain, it is the chain.  Selection, locks and per-slot enables are
        persisted the same way.  The DSP that reads that order lands in phases
        12-17 under Source/Audio/FX/.
    */
    class FXChainView : public juce::Component,
                        private juce::ValueTree::Listener
    {
    public:
        FXChainView (NacarProcessor&, EditorHost&);
        ~FXChainView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        // -- chain state ----------------------------------------------------
        juce::StringArray readOrder() const;
        juce::StringArray readList (const juce::Identifier&) const;
        void writeOrder (const juce::StringArray&);
        void writeList (const juce::Identifier&, const juce::StringArray&);

        static int canonicalIndex (juce::StringRef slotName);

        void addModule (int canonicalIndex);
        void removeModule (const juce::String& slotName);
        void selectModule (const juce::String& slotName);
        void setModuleLocked (const juce::String& slotName, bool);
        void setModuleBypassed (const juce::String& slotName, bool);
        void setCollapsed (bool);

        // -- cards ----------------------------------------------------------
        /** The shadows and halos the cards cast onto the rack.  They belong to
            the cards but cannot be painted by them: a component cannot paint
            outside its own bounds, and all of this falls outside theirs. */
        void paintCardSeats (juce::Graphics&);

        /** The strip the card row occupies, plus the reach of what it throws. */
        juce::Rectangle<int> cardRowArea() const;

        std::unique_ptr<FXModuleCard> makeCard (const FXModuleCard::Slot&);
        void rebuildCards();
        void syncCardStates();
        void layOutCards();

        // -- reordering -----------------------------------------------------
        void beginCardDrag (FXModuleCard&, const juce::MouseEvent&);
        void dragCard (const juce::MouseEvent&);
        void endCardDrag();
        int  dropIndexFor (float cardCentreX) const;
        float insertionLineX() const;

        void showAddMenu (juce::Component* anchor);

        // -- juce::ValueTree::Listener ---------------------------------------
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeParentChanged (juce::ValueTree&) override;
        void reacquireTree();

        NacarProcessor& processor;
        EditorHost& host;
        juce::ValueTree fxTree;

        IconButton addButton;
        IconButton collapseButton;
        PillButton addSlotButton;

        std::vector<std::unique_ptr<FXModuleCard>> cards;

        int   dragIndex = -1;          ///< index in `cards` of the card being dragged
        int   dropIndex = -1;          ///< where it would land
        juce::Rectangle<int> dragStartBounds;
        float dragStartMouseX = 0.0f;

        bool collapsed = false;
        bool writingOurselves = false; ///< suppresses the listener for our own edits

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FXChainView)
    };
}
