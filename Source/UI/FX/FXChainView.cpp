#include "FXChainView.h"

#include <algorithm>
#include <array>

namespace nacar::ui
{
    using namespace layout;

    // -----------------------------------------------------------------------
    //  Numbers UI spec section 7 describes but Layout.h (frozen) does not carry.
    // -----------------------------------------------------------------------
    static constexpr float kLinkSize      = 12.0f;   // spec: a small chain link on the seam
    static constexpr float kInsertWidth   =  2.0f;   // the violet drop indicator
    static constexpr float kAddSlotIcon   =  0.22f;  // plus, tuned for the 55 x 100 dashed slot
    static constexpr float kLinkRelief    =  0.9f;   // the link's own shadow, along the light
    static constexpr float kHaloSpread    = 10.0f;   // the active card's halo, < the 20 px gap

    // How far past a card's own bounds the things we paint for it reach: the
    // floating shadow's blur and offset, and the active card's halo.  Used as
    // the repaint margin, because a repaint that is one pixel short of the
    // shadow is exactly what makes a dragged card smear.
    static constexpr int   kShadowReach   = 22;

    // -----------------------------------------------------------------------
    //  What the cards throw onto the rack.
    //
    //  A juce::Component cannot paint outside its own bounds, so a card cannot
    //  draw the shadow it casts or the halo it throws: both of those live on
    //  the rack, behind it, and the rack is this component.  The card reports
    //  how far off the rack it is sitting (FXModuleCard::getElevation) and the
    //  numbers below turn that into a shadow.
    //
    //  They mirror theme.cpp's own depth table for `raised` and `floating`,
    //  including the 1.4x that theme::raisedGlass applies to every glass-on-
    //  glass shadow - glass sits genuinely above the panel behind it in a way
    //  ceramic on ceramic does not - so a card shadowed from here is lit
    //  identically to one the vocabulary draws itself.
    // -----------------------------------------------------------------------
    struct CardShadow
    {
        float offset, blur, alpha;
    };

    static CardShadow shadowFor (theme::Elevation e) noexcept
    {
        switch (e)
        {
            case theme::Elevation::floating: return { 6.0f, 20.0f, 0.34f * 1.4f };
            case theme::Elevation::raised:   return { 3.0f, 10.0f, 0.26f * 1.4f };
            case theme::Elevation::resting:  return { 1.5f,  5.0f, 0.20f * 1.4f };
            case theme::Elevation::flush:
            default:                         return { 0.0f,  0.0f, 0.0f };
        }
    }

    // -----------------------------------------------------------------------
    //  Two properties this view persists that StateManager.h (frozen) has no
    //  identifier for.  The session tree keeps properties it does not
    //  recognise - StateManager::upgrade() never discards one - so both survive
    //  a save/load round trip.  They belong in nacar::ids the next time that
    //  file is opened; see the report.
    // -----------------------------------------------------------------------
    static const juce::Identifier fxBypassId    ("fxBypass");     // comma-separated slot names
    static const juce::Identifier fxCollapsedId ("fxCollapsed");  // bool

    // -----------------------------------------------------------------------
    //  The six modules, in canonical order.
    //
    //  This table is identity only - which parameter a slot switches and which
    //  glyph it wears.  It is NOT the chain order: that is read from
    //  ids::fxOrder, because displayed order is DSP order and the user owns it.
    //  GRAIN and CRUSH share the dot-matrix glyph, as in the reference image.
    // -----------------------------------------------------------------------
    static const std::array<FXModuleCard::Slot, (size_t) fx::numCards>& slotTable()
    {
        static const std::array<FXModuleCard::Slot, (size_t) fx::numCards> table
        {{
            { "RETRO",  PID::retroOn,    icons::Icon::cassette    },
            { "CRUSH",  PID::crushOn,    icons::Icon::dotMatrix   },
            { "FILTER", PID::fxFilterOn, icons::Icon::filterCurve },
            { "REWIND", PID::rewindOn,   icons::Icon::rewind      },
            { "GRAIN",  PID::grainFxOn,  icons::Icon::dotMatrix   },
            { "SPACE",  PID::spaceOn,    icons::Icon::concentric  }
        }};

        return table;
    }

    static juce::String defaultOrderString()
    {
        juce::StringArray names;

        for (const auto& s : slotTable())
            names.add (s.name);

        return names.joinIntoString (",");
    }

    int FXChainView::canonicalIndex (juce::StringRef slotName)
    {
        const auto& table = slotTable();

        for (int i = 0; i < (int) table.size(); ++i)
            if (juce::String (table[(size_t) i].name) == juce::String (slotName))
                return i;

        return -1;
    }

    // =======================================================================
    //  Construction
    // =======================================================================
    FXChainView::FXChainView (NacarProcessor& p, EditorHost& h)
        : processor (p),
          host (h),
          fxTree (p.getStateManager().group (ids::FXCHAIN)),
          addButton (icons::Icon::plus, IconButton::Style::glassSquare),
          collapseButton (icons::Icon::collapse, IconButton::Style::plain),
          addSlotButton (juce::String(), PillButton::Style::dashed)
    {
        // A session written by an older build may not carry an order at all.
        if (! fxTree.hasProperty (ids::fxOrder))
            fxTree.setProperty (ids::fxOrder, defaultOrderString(), nullptr);

        collapsed = (bool) fxTree.getProperty (fxCollapsedId, false);

        addButton.setColours (theme::glassInkMuted, theme::violet);
        addButton.setTooltip ("Add a module back into the chain");
        addButton.onClick = [this] { showAddMenu (&addButton); };
        addAndMakeVisible (addButton);

        collapseButton.setColours (theme::glassInkMuted, theme::violet);
        collapseButton.setTooltip ("Collapse the chain");
        collapseButton.setIcon (collapsed ? icons::Icon::expand : icons::Icon::collapse);
        collapseButton.setActive (collapsed);
        collapseButton.onClick = [this] { setCollapsed (! collapsed); };
        addAndMakeVisible (collapseButton);

        addSlotButton.setIcon (icons::Icon::plus, kAddSlotIcon);
        addSlotButton.setCornerRadius (radiusCard);
        addSlotButton.setTooltip ("Add a module back into the chain");
        addSlotButton.onClick = [this] { showAddMenu (&addSlotButton); };
        addAndMakeVisible (addSlotButton);
        addSlotButton.setVisible (! collapsed);

        rebuildCards();

        fxTree.addListener (this);
    }

    FXChainView::~FXChainView()
    {
        fxTree.removeListener (this);
    }

    // =======================================================================
    //  Chain state
    //
    //  ids::fxOrder is the single source of truth for what is in the chain and
    //  in which order.  ids::selectedFxSlot holds the *canonical* index of the
    //  selected module (RETRO = 0 ... SPACE = 5) rather than its position in
    //  the row, so selection follows a module when the chain is reordered and
    //  survives a removal.
    // =======================================================================
    juce::StringArray FXChainView::readOrder() const
    {
        juce::StringArray out;

        const auto raw = fxTree.getProperty (ids::fxOrder).toString();

        for (const auto& token : juce::StringArray::fromTokens (raw, ",", ""))
        {
            const auto name = token.trim().toUpperCase();

            if (name.isNotEmpty() && canonicalIndex (name) >= 0 && ! out.contains (name))
                out.add (name);
        }

        return out;
    }

    juce::StringArray FXChainView::readList (const juce::Identifier& property) const
    {
        juce::StringArray out;

        for (const auto& token : juce::StringArray::fromTokens (fxTree.getProperty (property).toString(),
                                                                ",", ""))
        {
            const auto name = token.trim().toUpperCase();

            if (name.isNotEmpty() && ! out.contains (name))
                out.add (name);
        }

        return out;
    }

    void FXChainView::writeOrder (const juce::StringArray& order)
    {
        {
            const juce::ScopedValueSetter<bool> svs (writingOurselves, true);
            fxTree.setProperty (ids::fxOrder, order.joinIntoString (","), nullptr);
        }

        rebuildCards();

        // Order is chassis-wide state: the deep-edit FX page shows the same
        // chain, so let the editor know rather than repainting only ourselves.
        host.chassisChanged();
    }

    void FXChainView::writeList (const juce::Identifier& property, const juce::StringArray& list)
    {
        {
            const juce::ScopedValueSetter<bool> svs (writingOurselves, true);
            fxTree.setProperty (property, list.joinIntoString (","), nullptr);
        }

        syncCardStates();
    }

    void FXChainView::addModule (int canonical)
    {
        if (! juce::isPositiveAndBelow (canonical, fx::numCards))
            return;

        auto order = readOrder();
        const juce::String name (slotTable()[(size_t) canonical].name);

        if (order.contains (name))
            return;

        order.add (name);
        writeOrder (order);
    }

    void FXChainView::removeModule (const juce::String& name)
    {
        auto order = readOrder();
        const int at = order.indexOf (name);

        if (at < 0)
            return;

        order.remove (at);

        // Never leave the selection pointing at a module that is not in the
        // chain: hand it to whichever module took the removed one's place.
        if ((int) fxTree.getProperty (ids::selectedFxSlot, 0) == canonicalIndex (name)
             && ! order.isEmpty())
        {
            const auto neighbour = order[juce::jlimit (0, order.size() - 1, at)];

            const juce::ScopedValueSetter<bool> svs (writingOurselves, true);
            fxTree.setProperty (ids::selectedFxSlot, canonicalIndex (neighbour), nullptr);
        }

        // A removed module keeps neither its lock nor its bypass.
        auto locks = readList (ids::fxLocks);
        auto bypasses = readList (fxBypassId);

        if (locks.contains (name) || bypasses.contains (name))
        {
            const juce::ScopedValueSetter<bool> svs (writingOurselves, true);

            locks.removeString (name);
            bypasses.removeString (name);

            fxTree.setProperty (ids::fxLocks, locks.joinIntoString (","), nullptr);
            fxTree.setProperty (fxBypassId, bypasses.joinIntoString (","), nullptr);
        }

        writeOrder (order);
    }

    void FXChainView::selectModule (const juce::String& name)
    {
        const int canonical = canonicalIndex (name);

        if (canonical < 0 || (int) fxTree.getProperty (ids::selectedFxSlot, 0) == canonical)
            return;

        {
            const juce::ScopedValueSetter<bool> svs (writingOurselves, true);
            fxTree.setProperty (ids::selectedFxSlot, canonical, nullptr);
        }

        syncCardStates();
    }

    void FXChainView::setModuleLocked (const juce::String& name, bool shouldBeLocked)
    {
        auto locks = readList (ids::fxLocks);

        if (shouldBeLocked == locks.contains (name))
            return;

        if (shouldBeLocked)
            locks.add (name);
        else
            locks.removeString (name);

        writeList (ids::fxLocks, locks);
    }

    void FXChainView::setModuleBypassed (const juce::String& name, bool shouldBeBypassed)
    {
        auto bypasses = readList (fxBypassId);

        if (shouldBeBypassed == bypasses.contains (name))
            return;

        if (shouldBeBypassed)
            bypasses.add (name);
        else
            bypasses.removeString (name);

        writeList (fxBypassId, bypasses);
    }

    void FXChainView::setCollapsed (bool shouldBeCollapsed)
    {
        collapsed = shouldBeCollapsed;

        {
            const juce::ScopedValueSetter<bool> svs (writingOurselves, true);
            fxTree.setProperty (fxCollapsedId, collapsed, nullptr);
        }

        collapseButton.setIcon (collapsed ? icons::Icon::expand : icons::Icon::collapse);
        collapseButton.setActive (collapsed);
        collapseButton.setTooltip (collapsed ? "Expand the chain" : "Collapse the chain");
        addSlotButton.setVisible (! collapsed);

        for (auto& card : cards)
            if (card != nullptr)
                card->setVisible (! collapsed);

        repaint();
    }

    // =======================================================================
    //  Cards
    // =======================================================================
    std::unique_ptr<FXModuleCard> FXChainView::makeCard (const FXModuleCard::Slot& s)
    {
        auto card = std::make_unique<FXModuleCard> (processor, s);
        auto* raw = card.get();
        const juce::String name (s.name);

        card->onSelect = [this, name] { selectModule (name); };

        card->onRemove = [this, name]
        {
            // Removing destroys this card and we are inside its own mouseUp,
            // so the edit is deferred by one message.
            juce::Component::SafePointer<FXChainView> safe (this);

            juce::MessageManager::callAsync ([safe, name]
                                             {
                                                 if (safe != nullptr)
                                                     safe->removeModule (name);
                                             });
        };

        card->onLockRequested   = [this, name] (bool l) { setModuleLocked (name, l); };
        card->onBypassRequested = [this, name] (bool b) { setModuleBypassed (name, b); };

        // We paint this card's shadow and its halo, so its hover has to reach us.
        card->onHoverChanged = [this, raw] { repaint (raw->getBounds().expanded (kShadowReach)); };

        card->onDragStart = [this, raw] (const juce::MouseEvent& e) { beginCardDrag (*raw, e); };
        card->onDragMove  = [this]      (const juce::MouseEvent& e) { dragCard (e); };
        card->onDragEnd   = [this]      (const juce::MouseEvent&)   { endCardDrag(); };

        addAndMakeVisible (*card);
        card->setVisible (! collapsed);

        return card;
    }

    void FXChainView::rebuildCards()
    {
        const auto order = readOrder();

        std::vector<std::unique_ptr<FXModuleCard>> rebuilt;
        rebuilt.reserve ((size_t) order.size());

        for (const auto& name : order)
        {
            // Reuse the card we already have for this slot.  Reordering the
            // chain must not destroy the component the mouse is dragging.
            const auto existing = std::find_if (cards.begin(), cards.end(),
                                                [&name] (const std::unique_ptr<FXModuleCard>& c)
                                                {
                                                    return c != nullptr && c->getSlotName() == name;
                                                });

            if (existing != cards.end())
            {
                rebuilt.push_back (std::move (*existing));
                continue;
            }

            const int canonical = canonicalIndex (name);

            if (juce::isPositiveAndBelow (canonical, fx::numCards))
                rebuilt.push_back (makeCard (slotTable()[(size_t) canonical]));
        }

        cards.clear();            // whatever was not reused has left the chain
        cards = std::move (rebuilt);

        syncCardStates();
        layOutCards();
        repaint();
    }

    void FXChainView::syncCardStates()
    {
        const auto locks    = readList (ids::fxLocks);
        const auto bypasses = readList (fxBypassId);
        const int  selected = (int) fxTree.getProperty (ids::selectedFxSlot, 0);

        for (auto& card : cards)
        {
            if (card == nullptr)
                continue;

            const auto name = card->getSlotName();

            card->setSelected (canonicalIndex (name) == selected);
            card->setLocked   (locks.contains (name));
            card->setBypassed (bypasses.contains (name));
        }
    }

    void FXChainView::layOutCards()
    {
        for (int i = 0; i < (int) cards.size(); ++i)
            if (cards[(size_t) i] != nullptr && i != dragIndex)
                cards[(size_t) i]->setBounds (fxCard (i).toNearestInt());
    }

    // =======================================================================
    //  Reordering
    //
    //  Deliberately not a DragAndDropContainer: one card moves, the drop index
    //  comes from the pointer against fx::cardPitch, and mouseUp rewrites
    //  ids::fxOrder.  Nothing else in the chassis needs to know.
    // =======================================================================
    void FXChainView::beginCardDrag (FXModuleCard& card, const juce::MouseEvent& e)
    {
        dragIndex = -1;

        for (int i = 0; i < (int) cards.size(); ++i)
            if (cards[(size_t) i].get() == &card)
                dragIndex = i;

        if (dragIndex < 0)
            return;

        dragStartBounds = card.getBounds();
        dragStartMouseX = e.getEventRelativeTo (this).position.x;
        dropIndex       = dragIndex;

        card.setDragging (true);
        card.toFront (false);

        // The hole it leaves and the deeper shadow it now casts are ours.
        repaint (cardRowArea());
    }

    void FXChainView::dragCard (const juce::MouseEvent& e)
    {
        if (! juce::isPositiveAndBelow (dragIndex, (int) cards.size()))
            return;

        auto& card = *cards[(size_t) dragIndex];

        const float dx = e.getEventRelativeTo (this).position.x - dragStartMouseX;
        const int   x  = juce::jlimit (0, juce::jmax (0, getWidth() - (int) fx::cardW),
                                       juce::roundToInt ((float) dragStartBounds.getX() + dx));

        const auto before = card.getBounds();
        card.setBounds (dragStartBounds.withX (x));

        // Every move, not only when the landing index changes: the lifted
        // card's shadow is painted by us, outside the card's own bounds, so the
        // region JUCE repaints for the move itself stops short of it and the
        // shadow smears across the rack.  Scoped to where the card actually
        // was and is, because this runs at pointer rate.
        repaint (before.getUnion (card.getBounds()).expanded (kShadowReach));

        const int landing = dropIndexFor ((float) x + fx::cardW * 0.5f);

        if (landing != dropIndex)
        {
            dropIndex = landing;
            repaint (cardRowArea());
        }
    }

    void FXChainView::endCardDrag()
    {
        if (! juce::isPositiveAndBelow (dragIndex, (int) cards.size()))
            return;

        const int from = dragIndex;
        const int to   = dropIndex;

        cards[(size_t) from]->setDragging (false);

        dragIndex = -1;
        dropIndex = -1;

        auto order = readOrder();

        if (to >= 0 && to != from
             && juce::isPositiveAndBelow (from, order.size())
             && juce::isPositiveAndBelow (to, order.size()))
        {
            order.move (from, to);
            writeOrder (order);     // rebuildCards() permutes the cards we already have
        }
        else
        {
            layOutCards();          // snap the dragged card back into the row
        }

        repaint();
    }

    int FXChainView::dropIndexFor (float cardCentreX) const
    {
        const int n = (int) cards.size();

        if (n <= 1)
            return 0;

        const float rel = (cardCentreX - fx::cardX0 - fx::cardW * 0.5f) / fx::cardPitch;

        return juce::jlimit (0, n - 1, juce::roundToInt (rel));
    }

    float FXChainView::insertionLineX() const
    {
        const float gap  = fx::cardPitch - fx::cardW;
        const float base = fx::cardX0 + (float) dropIndex * fx::cardPitch;

        // Moving left, the card lands before the card at dropIndex; moving
        // right, everything between shuffles down and it lands after it.
        return dropIndex < dragIndex ? base - gap * 0.5f
                                     : base + fx::cardW + gap * 0.5f;
    }

    // =======================================================================
    //  Add menu
    // =======================================================================
    void FXChainView::showAddMenu (juce::Component* anchor)
    {
        const auto order = readOrder();

        juce::PopupMenu m;
        int missing = 0;

        for (int i = 0; i < fx::numCards; ++i)
        {
            const juce::String name (slotTable()[(size_t) i].name);

            if (! order.contains (name))
            {
                m.addItem (i + 1, name);
                ++missing;
            }
        }

        // All six ship in the chain, so the menu is empty until one is removed.
        if (missing == 0)
            m.addItem (900, "All modules in chain", false, false);

        juce::Component::SafePointer<FXChainView> safe (this);

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor),
                         [safe] (int result)
                         {
                             if (safe != nullptr && result >= 1 && result <= fx::numCards)
                                 safe->addModule (result - 1);
                         });
    }

    // =======================================================================
    //  juce::ValueTree::Listener
    // =======================================================================
    void FXChainView::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
    {
        // Our own edits have already been applied to the cards.
        if (writingOurselves || tree != fxTree)
            return;

        if (property == ids::fxOrder)
        {
            if (dragIndex >= 0)
                return;             // a drag in flight owns the row until it ends

            rebuildCards();
        }
        else if (property == ids::fxLocks || property == ids::selectedFxSlot
                  || property == fxBypassId)
        {
            syncCardStates();
        }
        else if (property == fxCollapsedId)
        {
            setCollapsed ((bool) fxTree.getProperty (fxCollapsedId, false));
        }
    }

    void FXChainView::valueTreeParentChanged (juce::ValueTree& tree)
    {
        if (tree != fxTree || fxTree.getParent().isValid())
            return;

        // Restoring host state copies a whole new SESSION over the old one:
        // every child is detached first and the restored ones are added after,
        // so the FXCHAIN we hold is briefly an orphan and re-reading it now
        // would find nothing.  Pick the new one up once the restore is done.
        juce::Component::SafePointer<FXChainView> safe (this);

        juce::MessageManager::callAsync ([safe]
                                         {
                                             if (safe != nullptr)
                                                 safe->reacquireTree();
                                         });
    }

    void FXChainView::reacquireTree()
    {
        fxTree.removeListener (this);
        fxTree = processor.getStateManager().group (ids::FXCHAIN);
        fxTree.addListener (this);

        if (! fxTree.hasProperty (ids::fxOrder))
            fxTree.setProperty (ids::fxOrder, defaultOrderString(), nullptr);

        setCollapsed ((bool) fxTree.getProperty (fxCollapsedId, false));
        rebuildCards();
    }

    // =======================================================================
    //  Paint / layout
    // =======================================================================
    void FXChainView::paint (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();

        // -- the rack -------------------------------------------------------
        // Not a surface with things drawn on it: a well cut into the chassis,
        // with the cards seated inside it.  Almost all of the depth in this
        // region is that one contrast - the well's inner shadow under its top
        // edge against the cards' own lit top edges a few pixels below.
        // No glassEdge hairline round it any more: that rim is what lights the
        // edge of something raised, and this is the opposite of raised.  The
        // well's own dark top edge and lit bottom edge draw the boundary, and
        // the ceramic chassis outside it supplies all the contrast needed.
        theme::recessedWell (g, b, radiusPanel, theme::glassDeep, 1.0f);

        glassLabel (g, "FX CHAIN", { fx::titleX, fx::titleBase },
                    fx::titleSize, fx::titleTrack, theme::glassInkMuted);

        const int n = (int) cards.size();

        if (collapsed)
        {
            // Collapsed, the chain still reads as a chain - just as a line of
            // type on the seam line instead of six cards.
            for (int i = 0; i < n; ++i)
            {
                const auto slotRect = fxCard (i);
                const auto& card = *cards[(size_t) i];

                // ui::glassLabel treats the origin as the centre when the
                // justification is centred, so the card's centre goes in.
                glassLabel (g, card.getSlotName(),
                            { slotRect.getCentreX(), fx::linkY + fx::nameSize * 0.5f },
                            fx::nameSize, fx::nameTrack,
                            card.getSelected() ? theme::violet : theme::glassInkMuted,
                            juce::Justification::centred);
            }
        }
        else
        {
            // The empty slot is a hole in the rack, not a card that has been
            // switched off: the dashed pill sits over a well, so you can see
            // the floor of the rack through it.
            theme::recessedWell (g, fx::addSlot, radiusCard, theme::glassDeep, 0.85f);

            paintCardSeats (g);
        }

        // -- the chain link on every seam ----------------------------------
        {
            const float gap = fx::cardPitch - fx::cardW;

            for (int i = 0; i + 1 < n; ++i)
            {
                const float seam = fx::cardX0 + (float) i * fx::cardPitch + fx::cardW + gap * 0.5f;
                const auto  area = centredSquare ({ seam, fx::linkY }, kLinkSize * 0.5f);

                // Lying on the floor of the rack, so it has a shadow under it -
                // faint, but enough that the seam is a place rather than a gap.
                icons::draw (g, icons::Icon::link,
                             area.translated (-theme::lightX * kLinkRelief,
                                              -theme::lightY * kLinkRelief),
                             theme::glassDeep.darker (0.8f).withAlpha (0.85f), 1.3f);

                icons::draw (g, icons::Icon::link, area, theme::glassInkFaint, 1.3f);
            }
        }

        // -- drop indicator -------------------------------------------------
        if (dragIndex >= 0 && dropIndex >= 0 && dropIndex != dragIndex)
        {
            const auto bar = juce::Rectangle<float> (insertionLineX() - kInsertWidth * 0.5f,
                                                     fx::cardY - 4.0f,
                                                     kInsertWidth, fx::cardH + 8.0f);

            theme::outerGlow (g, bar, kInsertWidth * 0.5f, theme::violet, 0.55f, 6.0f);

            g.setColour (theme::violetLight);
            g.fillRoundedRectangle (bar, kInsertWidth * 0.5f);
        }
    }

    /** The strip the card row and everything it throws occupies. */
    juce::Rectangle<int> FXChainView::cardRowArea() const
    {
        return { 0, (int) fx::cardY - kShadowReach,
                 getWidth(), (int) fx::cardH + kShadowReach * 2 };
    }

    // -----------------------------------------------------------------------
    //  Everything the cards throw onto the rack: the hole a lifted card leaves
    //  behind it, the contact shadow each one casts, and the halo the active
    //  one throws.  None of it can be painted by the cards themselves, because
    //  all of it falls outside their bounds.
    // -----------------------------------------------------------------------
    void FXChainView::paintCardSeats (juce::Graphics& g)
    {
        const int n = (int) cards.size();

        // 1. The slot a dragged card came out of.  There is genuinely nothing
        //    there while the drag is in flight, so it reads as bare rack floor.
        if (juce::isPositiveAndBelow (dragIndex, n))
            theme::recessedWell (g, dragStartBounds.toFloat(), radiusCard,
                                 theme::glassDeep, 1.4f);

        // 2. The seated cards, lowest first: a card that is being dragged is
        //    above the rest and its shadow has to fall across theirs.
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int i = 0; i < n; ++i)
            {
                const auto* card = cards[(size_t) i].get();

                if (card == nullptr || ! card->isVisible())
                    continue;

                if ((i == dragIndex) != (pass == 1))
                    continue;

                const auto r = card->getBounds().toFloat();
                const auto d = shadowFor (card->getElevation());

                if (d.alpha > 0.0f)
                    theme::contactShadow (g, r, radiusCard, d.offset, d.blur, d.alpha);

                // The active module is lit from within - FXModuleCard draws the
                // inner glow - and this is the light that gets out: a halo on
                // the rack around it, kept inside the 20 px gap so it never
                // reaches the module either side.
                if (card->getSelected())
                    theme::outerGlow (g, r, radiusCard, theme::violet,
                                      card->getCardHovered() ? 0.30f : 0.22f, kHaloSpread);
            }
        }
    }

    void FXChainView::resized()
    {
        addButton     .setBounds (fx::addButton.toNearestInt());
        collapseButton.setBounds (fx::collapse .toNearestInt());
        addSlotButton .setBounds (fx::addSlot  .toNearestInt());

        layOutCards();
    }
}
