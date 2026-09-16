#include "PresetBrowser.h"

#include <algorithm>

// ===========================================================================
//  WHAT IS REAL IN THIS FILE, AND WHAT IS WAITING
//
//  Real, now:
//    - the drawer itself: slide animation, scrim, open/close, and the promise
//      that a closed browser is invisible AND transparent to the mouse, so the
//      instrument stays playable underneath it;
//    - the search box, including the escape-closes handler and the fact that it
//      is the only thing in here that ever takes keyboard focus;
//    - the CATEGORY and MOOD filter rows, built from the master spec's
//      vocabularies, single-select, click-again-to-clear, horizontally
//      scrolling;
//    - the FAVOURITES and RECENT collection toggles;
//    - the filter pipeline: query + category + mood + favourites + recent are
//      applied to `library` and produce `filtered`, and RECENT also orders by
//      last-used;
//    - the results list: a real juce::ListBox over a real model, with row
//      painting, selection, and a favourite toggle on the row's heart;
//    - the empty state.
//
//  Waiting on Phase 27 (Source/PresetSystem/ does not exist yet):
//    - PresetManager, which is what will fill `library`.  Until it does, the
//      array is empty on purpose and the browser says so.  No placeholder
//      presets are invented here - a name in this list has to resolve to a file.
//    - loading a preset on double-click (see listBoxItemDoubleClicked).
//    - persisting a favourite (see listBoxItemClicked): the flag belongs in the
//      preset file and in StateManager's ids::presetFavourite, and currently
//      lives only in memory.
//    - TAGS as a filter row of its own.  PresetEntry carries the tags and the
//      search box already matches against them, but the tag vocabulary is
//      library-derived, so there is nothing to build chips from yet.
// ===========================================================================

namespace nacar::ui
{
    using namespace layout;

    // =======================================================================
    //  Drawer geometry.
    //
    //  The browser is an overlay, not one of the chassis regions, so Layout.h
    //  carries no namespace for it and - being frozen - cannot grow one.  The
    //  drawer's own rect is *derived* from layout::leftPanel and layout::
    //  viewport in drawerRestBounds(); everything below is internal to the
    //  drawer and is measured from its top-left corner.
    // =======================================================================

    /// How far across the optical viewport the open drawer reaches.  With the
    /// left panel's left edge and the viewport's, 0.35 puts the drawer's right
    /// edge at x 639 - the "x 8..640" the brief asks for, without typing 640.
    static constexpr float drawerViewportFraction = 0.35f;

    static constexpr float pad = 22.0f;            ///< drawer gutter

    static constexpr float headerBase   = 46.0f;
    static constexpr float headerSize   = 11.0f;
    static constexpr float headerTrack  = 0.26f;
    static constexpr float headerRuleY  = 66.0f;
    static constexpr float closeSize    = 22.0f;
    static constexpr float closeCentreY = 40.0f;

    static constexpr float searchY = 84.0f;
    static constexpr float searchH = 34.0f;

    static constexpr float filterLabelSize  = 7.5f;
    static constexpr float filterLabelTrack = 0.20f;
    static constexpr float filterPillSize   = 7.5f;
    static constexpr float filterPillTrack  = 0.14f;

    static constexpr float rowH     = 24.0f;       ///< height of a filter chip
    static constexpr float pillGap  = 6.0f;
    static constexpr float pillPad  = 16.0f;       ///< horizontal padding inside a chip
    static constexpr int   scrollBarThickness = 5;

    static constexpr float categoryLabelBase    = 146.0f;
    static constexpr float categoryRowY         = 156.0f;
    static constexpr float moodLabelBase        = 213.0f;
    static constexpr float moodRowY             = 223.0f;
    static constexpr float collectionsLabelBase = 264.0f;
    static constexpr float collectionsRowY      = 274.0f;
    static constexpr float resultsLabelBase     = 332.0f;
    static constexpr float listTop              = 344.0f;
    static constexpr float listWellInset        = 3.0f;

    static constexpr float searchFontSize = 12.5f;

    // -- the empty state -----------------------------------------------------
    static constexpr float emptyTitleSize  = 13.0f;
    static constexpr float emptyTitleTrack = 0.04f;
    static constexpr float emptySubSize    = 9.5f;
    static constexpr float emptySubTrack   = 0.02f;
    static constexpr float emptyGap        = 8.0f;   ///< between the two lines

    // -- one results row -----------------------------------------------------
    static constexpr float listRowH     = 46.0f;
    static constexpr float rowPad       = 16.0f;
    static constexpr float rowAccentW   = 2.0f;      ///< selected-row violet bar
    static constexpr float rowNameSize  = 12.5f;
    static constexpr float rowNameBase  = 21.0f;
    static constexpr float rowMetaSize  = 7.5f;
    static constexpr float rowMetaTrack = 0.16f;
    static constexpr float rowMetaBase  = 35.0f;
    static constexpr float rowHeartSize = 15.0f;

    // -- overlay -------------------------------------------------------------
    static constexpr float scrimAlpha       = 0.55f;   ///< the brief's ~55 % black
    static constexpr float drawerEdgeShadow = 18.0f;   ///< the opening's shadow, cast rightwards
    static constexpr int   animationHz      = 60;
    static constexpr float animationSeconds = 0.22f;

    // =======================================================================
    //  Vocabularies (master build spec).  Held as function-local statics so the
    //  words exist once and the header stays free of them.
    // =======================================================================
    static const juce::StringArray& categoryVocabulary()
    {
        static const juce::StringArray words =
            juce::StringArray::fromTokens ("KEYS PADS PLUCKS BELLS LEADS BASS SUB VOCAL-LIKE "
                                           "TEXTURE ATMOSPHERE DRUMS PERCUSSION SEQUENCES",
                                           " ", "");
        return words;
    }

    static const juce::StringArray& moodVocabulary()
    {
        static const juce::StringArray words =
            juce::StringArray::fromTokens ("DARK INTIMATE BROKEN NOSTALGIC AIRY AGGRESSIVE "
                                           "ROMANTIC COLD WARM CINEMATIC DIRTY DREAMY HAUNTED "
                                           "LUSH MINIMAL MYSTERIOUS",
                                           " ", "");
        return words;
    }

    // =======================================================================
    //  PresetEntry
    // =======================================================================
    bool PresetEntry::matches (const juce::String& lowercaseQuery) const
    {
        if (lowercaseQuery.isEmpty())
            return true;

        if (name    .containsIgnoreCase (lowercaseQuery)
            || author  .containsIgnoreCase (lowercaseQuery)
            || category.containsIgnoreCase (lowercaseQuery)
            || mood    .containsIgnoreCase (lowercaseQuery))
            return true;

        for (const auto& tag : tags)
            if (tag.containsIgnoreCase (lowercaseQuery))
                return true;

        return false;
    }

    // =======================================================================
    //  Helpers
    // =======================================================================
    static PillButton* makeFilterPill (const juce::String& text)
    {
        auto* p = new PillButton (text, PillButton::Style::glass);

        p->setTextSize (filterPillSize, filterPillTrack);
        p->setCornerRadius (radiusPill);

        return p;
    }

    /** Lays a chip row out left to right and sizes its host to fit, which is
        what gives the viewport something to scroll. */
    static void layoutPillRow (juce::OwnedArray<PillButton>& pills, juce::Component& row)
    {
        float x = 0.0f;

        for (auto* p : pills)
        {
            // Height first: preferredWidth() scales a pill's icons by its height,
            // and answers from a guessed proportion while it still has none.
            p->setSize (1, juce::roundToInt (rowH));

            const float w = p->preferredWidth (pillPad);
            p->setBounds (juce::Rectangle<float> (x, 0.0f, w, rowH).toNearestInt());
            x += w + pillGap;
        }

        row.setSize (juce::roundToInt (juce::jmax (0.0f, x - pillGap)),
                     juce::roundToInt (rowH));
    }

    // =======================================================================
    //  PresetBrowser::Drawer
    //
    //  The panel itself.  It owns nothing: every widget is a member of the
    //  browser and merely parented here, so the whole drawer can be slid around
    //  by moving one component.
    // =======================================================================
    class PresetBrowser::Drawer : public juce::Component
    {
    public:
        explicit Drawer (PresetBrowser& o) : owner (o)
        {
            setWantsKeyboardFocus (false);
        }

        void paint (juce::Graphics& g) override
        {
            owner.paintDrawer (g, getLocalBounds().toFloat());
        }

    private:
        PresetBrowser& owner;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Drawer)
    };

    // =======================================================================
    //  PresetBrowser
    // =======================================================================
    PresetBrowser::PresetBrowser (NacarProcessor& p, EditorHost& h)
        : processor (p), host (h)
    {
        juce::ignoreUnused (processor);

        // Closed is the default, and closed means *gone*: invisible, and
        // transparent to the mouse so it can never stand between the player and
        // the instrument.
        setVisible (false);
        setInterceptsMouseClicks (false, false);
        setWantsKeyboardFocus (false);

        drawer = std::make_unique<Drawer> (*this);
        addAndMakeVisible (*drawer);

        // -- header ---------------------------------------------------------
        closeButton.setColours (theme::glassInkMuted, theme::glassInk);
        closeButton.setTooltip ("Close the browser");
        closeButton.onClick = [this] { host.setBrowserOpen (false); };
        drawer->addAndMakeVisible (closeButton);

        // -- search ---------------------------------------------------------
        //  Colours come from NacarLookAndFeel, which already dresses TextEditor
        //  in glass; nothing is overridden here.
        searchBox.setMultiLine (false);
        searchBox.setReturnKeyStartsNewLine (false);
        searchBox.setFont (theme::medium (searchFontSize));
        searchBox.setIndents (12, 6);
        searchBox.setTextToShowWhenEmpty ("Search presets", theme::glassInkFaint);
        searchBox.onTextChange = [this] { rebuildFilter(); };
        searchBox.onEscapeKey  = [this] { host.setBrowserOpen (false); };
        drawer->addAndMakeVisible (searchBox);

        // -- filter rows ----------------------------------------------------
        for (auto* v : { &categoryViewport, &moodViewport })
        {
            v->setScrollBarsShown (false, true, false, true);
            v->setScrollBarThickness (scrollBarThickness);
            v->setWantsKeyboardFocus (false);
            drawer->addAndMakeVisible (v);
        }

        categoryViewport.setViewedComponent (&categoryRow, false);
        moodViewport    .setViewedComponent (&moodRow,     false);

        buildFilterPills();

        favouritesPill.setIcon (icons::Icon::heart);

        for (auto* p : { &favouritesPill, &recentPill })
        {
            p->setTextSize (filterPillSize, filterPillTrack);
            p->setCornerRadius (radiusPill);
            drawer->addAndMakeVisible (p);
        }

        favouritesPill.onClick = [this]
        {
            favouritesOnly = ! favouritesOnly;
            refreshFilterPills();
            rebuildFilter();
        };

        recentPill.onClick = [this]
        {
            recentOnly = ! recentOnly;
            refreshFilterPills();
            rebuildFilter();
        };

        // -- results --------------------------------------------------------
        resultsList.setModel (this);
        resultsList.setRowHeight (juce::roundToInt (listRowH));
        resultsList.setOutlineThickness (0);
        resultsList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        resultsList.setColour (juce::ListBox::outlineColourId,    juce::Colours::transparentBlack);

        if (auto* vp = resultsList.getViewport())
            vp->setScrollBarThickness (scrollBarThickness);

        drawer->addAndMakeVisible (resultsList);

        refreshFilterPills();
        rebuildFilter();
    }

    PresetBrowser::~PresetBrowser()
    {
        stopTimer();
        resultsList.setModel (nullptr);
    }

    void PresetBrowser::buildFilterPills()
    {
        for (const auto& word : categoryVocabulary())
        {
            auto* pill = makeFilterPill (word);
            pill->onClick = [this, word] { setCategoryFilter (word); };

            categoryPills.add (pill);
            categoryRow.addAndMakeVisible (pill);
        }

        for (const auto& word : moodVocabulary())
        {
            auto* pill = makeFilterPill (word);
            pill->onClick = [this, word] { setMoodFilter (word); };

            moodPills.add (pill);
            moodRow.addAndMakeVisible (pill);
        }
    }

    // -----------------------------------------------------------------------
    //  Open / close
    // -----------------------------------------------------------------------
    void PresetBrowser::setOpen (bool shouldBeOpen)
    {
        if (open == shouldBeOpen)
            return;

        open = shouldBeOpen;

        if (open)
        {
            setVisible (true);
            setInterceptsMouseClicks (true, true);
            toFront (false);        // false: coming forward must not steal focus
            rebuildFilter();
        }

        startTimerHz (animationHz);
    }

    void PresetBrowser::timerCallback()
    {
        const int ticks = juce::jmax (1, juce::roundToInt (animationSeconds * (float) animationHz));
        const float step = 1.0f / (float) ticks;

        phase = juce::jlimit (0.0f, 1.0f, phase + (open ? step : -step));

        // Smoothstep, so the drawer neither starts nor stops with a visible edge.
        slide = phase * phase * (3.0f - 2.0f * phase);

        positionDrawer();
        repaint();

        if ((open && phase >= 1.0f) || (! open && phase <= 0.0f))
        {
            stopTimer();

            if (open)
            {
                // The one and only place the browser takes keyboard focus, and
                // escape hands it straight back by closing the drawer.
                if (isShowing())
                    searchBox.grabKeyboardFocus();
            }
            else
            {
                setVisible (false);
                setInterceptsMouseClicks (false, false);
            }
        }
    }

    // -----------------------------------------------------------------------
    //  Geometry
    // -----------------------------------------------------------------------
    juce::Rectangle<int> PresetBrowser::drawerRestBounds() const
    {
        // The drawer covers the left macro panel whole and the first third of
        // the optical viewport, top and bottom flush with the panel it replaces -
        // as if it had been pulled straight out of the chassis.  Derived from the
        // regions, never typed in.
        const float left    = layout::leftPanel.getX();
        const float topY    = layout::leftPanel.getY();
        const float bottomY = layout::leftPanel.getBottom();
        const float right   = layout::viewport.getX()
                              + layout::viewport.getWidth() * drawerViewportFraction;

        return juce::Rectangle<float> (left, topY, right - left, bottomY - topY).toNearestInt();
    }

    juce::Rectangle<float> PresetBrowser::listArea (juce::Rectangle<float> drawerLocal) const
    {
        return { pad, listTop,
                 drawerLocal.getWidth() - pad * 2.0f,
                 drawerLocal.getHeight() - pad - listTop };
    }

    void PresetBrowser::positionDrawer()
    {
        if (drawer == nullptr)
            return;

        const auto rest = drawerRestBounds();

        // Hidden is the drawer's own width plus the chassis margin, so at rest
        // not a pixel of it is on the canvas.
        const int hidden = rest.getWidth() + juce::roundToInt (margin);

        drawer->setBounds (rest.withX (rest.getX()
                                       - juce::roundToInt ((1.0f - slide) * (float) hidden)));
    }

    void PresetBrowser::resized()
    {
        if (drawer == nullptr)
            return;

        const auto rest = drawerRestBounds();
        drawer->setSize (rest.getWidth(), rest.getHeight());

        layoutDrawer();
        positionDrawer();
    }

    void PresetBrowser::layoutDrawer()
    {
        const auto b = drawer->getLocalBounds().toFloat();
        const float contentW = b.getWidth() - pad * 2.0f;

        closeButton.setBounds (juce::Rectangle<float> (closeSize, closeSize)
                                   .withCentre ({ b.getRight() - pad - closeSize * 0.5f,
                                                  closeCentreY })
                                   .toNearestInt());

        searchBox.setBounds (juce::Rectangle<float> (pad, searchY, contentW, searchH).toNearestInt());

        const float viewportH = rowH + (float) scrollBarThickness;

        categoryViewport.setBounds (juce::Rectangle<float> (pad, categoryRowY,
                                                            contentW, viewportH).toNearestInt());
        moodViewport    .setBounds (juce::Rectangle<float> (pad, moodRowY,
                                                            contentW, viewportH).toNearestInt());

        layoutPillRow (categoryPills, categoryRow);
        layoutPillRow (moodPills,     moodRow);

        float x = pad;

        for (auto* p : { &favouritesPill, &recentPill })
        {
            p->setSize (1, juce::roundToInt (rowH));

            const float w = p->preferredWidth (pillPad);
            p->setBounds (juce::Rectangle<float> (x, collectionsRowY, w, rowH).toNearestInt());
            x += w + pillGap;
        }

        resultsList.setBounds (listArea (b).reduced (listWellInset).toNearestInt());
    }

    // -----------------------------------------------------------------------
    //  Filtering
    // -----------------------------------------------------------------------
    void PresetBrowser::setCategoryFilter (const juce::String& word)
    {
        // Single select; clicking the lit chip clears it, so there is no "ALL"
        // chip to keep in step with everything else.
        activeCategory = (activeCategory == word ? juce::String() : word);

        refreshFilterPills();
        rebuildFilter();
    }

    void PresetBrowser::setMoodFilter (const juce::String& word)
    {
        activeMood = (activeMood == word ? juce::String() : word);

        refreshFilterPills();
        rebuildFilter();
    }

    void PresetBrowser::refreshFilterPills()
    {
        // Every chip in the drawer is a glass chip; selecting one gives it the
        // violet rim and violet ink that mean "selected" everywhere in NACAR.
        // Raised ceramic would be a bright slab on a dark drawer, and there are
        // thirty-one of these.
        auto apply = [] (PillButton& p, bool on) { p.setSelected (on); };

        const auto& categories = categoryVocabulary();
        const auto& moods      = moodVocabulary();

        for (int i = 0; i < categoryPills.size() && i < categories.size(); ++i)
            apply (*categoryPills[i], activeCategory == categories[i]);

        for (int i = 0; i < moodPills.size() && i < moods.size(); ++i)
            apply (*moodPills[i], activeMood == moods[i]);

        apply (favouritesPill, favouritesOnly);
        apply (recentPill,     recentOnly);
    }

    void PresetBrowser::rebuildFilter()
    {
        const auto query = searchBox.getText().trim().toLowerCase();

        filtered.clearQuick();

        for (int i = 0; i < library.size(); ++i)
        {
            const auto& e = library.getReference (i);

            if (activeCategory.isNotEmpty() && ! e.category.equalsIgnoreCase (activeCategory))
                continue;

            if (activeMood.isNotEmpty() && ! e.mood.equalsIgnoreCase (activeMood))
                continue;

            if (favouritesOnly && ! e.favourite)
                continue;

            if (recentOnly && e.lastUsed == 0)
                continue;

            if (! e.matches (query))
                continue;

            filtered.add (i);
        }

        // RECENT is an ordering as much as a filter.
        if (recentOnly && filtered.size() > 1)
            std::stable_sort (filtered.begin(), filtered.end(),
                              [this] (int a, int b)
                              {
                                  return library.getReference (a).lastUsed
                                       > library.getReference (b).lastUsed;
                              });

        // An empty result is drawn by the drawer, not by the list, so the list
        // gets out of the way rather than painting a blank well over it.
        resultsList.setVisible (! filtered.isEmpty());
        resultsList.deselectAllRows();
        resultsList.updateContent();

        if (drawer != nullptr)
            drawer->repaint();
    }

    // -----------------------------------------------------------------------
    //  juce::ListBoxModel
    // -----------------------------------------------------------------------
    int PresetBrowser::getNumRows()
    {
        return filtered.size();
    }

    void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                                          bool rowIsSelected)
    {
        if (! juce::isPositiveAndBelow (row, filtered.size()))
            return;

        const auto& e = library.getReference (filtered[row]);
        const juce::Rectangle<float> b (0.0f, 0.0f, (float) width, (float) height);

        if (rowIsSelected)
        {
            g.setColour (theme::violet.withAlpha (0.14f));
            g.fillRoundedRectangle (b.reduced (2.0f), radiusPill);

            g.setColour (theme::violet);
            g.fillRoundedRectangle (b.reduced (3.0f, 7.0f).withWidth (rowAccentW), 1.0f);
        }

        // The preset name is the one untracked run in the drawer, so it is drawn
        // directly rather than through glassLabel - placed off its baseline the
        // same way the label helpers do it.
        const auto nameFont = theme::medium (rowNameSize);

        g.setFont (nameFont);
        g.setColour (theme::glassInk);
        g.drawText (e.name,
                    juce::Rectangle<float> (rowPad, rowNameBase - nameFont.getAscent(),
                                            b.getWidth() - rowPad * 2.0f - rowHeartSize,
                                            nameFont.getHeight()),
                    juce::Justification::centredLeft, true);

        juce::String meta (e.category);

        if (e.mood.isNotEmpty())
            meta += (meta.isEmpty() ? juce::String() : juce::String::fromUTF8 ("  \xc2\xb7  ")) + e.mood;

        glassLabel (g, meta, { rowPad, rowMetaBase }, rowMetaSize, rowMetaTrack,
                    theme::glassInkMuted);

        icons::draw (g, e.favourite ? icons::Icon::heartFilled : icons::Icon::heart,
                     juce::Rectangle<float> (rowHeartSize, rowHeartSize)
                         .withCentre ({ b.getRight() - rowPad - rowHeartSize * 0.5f,
                                        b.getCentreY() }),
                     e.favourite ? theme::violet : theme::glassInkFaint, 1.3f);

        theme::hairline (g, { rowPad, b.getBottom() - 0.5f },
                            { b.getRight() - rowPad, b.getBottom() - 0.5f }, theme::glassEdge);
    }

    void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
    {
        if (! juce::isPositiveAndBelow (row, filtered.size()))
            return;

        // The event arrives in the row component's own coordinates.
        const int rowWidth = (e.eventComponent != nullptr ? e.eventComponent->getWidth()
                                                          : resultsList.getWidth());

        if ((float) e.x >= (float) rowWidth - rowPad - rowHeartSize * 2.0f)
        {
            auto& entry = library.getReference (filtered[row]);
            entry.favourite = ! entry.favourite;

            // MISSING: persistence.  The flag belongs in the preset file and in
            // StateManager's ids::presetFavourite; PresetManager writes both.
            // Until Phase 27 it lives only in this array.
            if (favouritesOnly)
                rebuildFilter();
            else
                resultsList.repaintRow (row);
        }
    }

    void PresetBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
    {
        juce::ignoreUnused (row);

        // MISSING: loading.  This would hand library[filtered[row]].file to
        // PresetManager::load() and then close the drawer.  PresetManager does
        // not exist yet (Phase 27), so this is deliberately inert rather than
        // pretending a preset was loaded.
    }

    // -----------------------------------------------------------------------
    //  Paint
    // -----------------------------------------------------------------------
    void PresetBrowser::paint (juce::Graphics& g)
    {
        if (slide <= 0.0f)
            return;

        // The scrim dims the chassis and takes the click that closes the drawer.
        // That is all it does: it is not a modal layer and it never touches the
        // keyboard, so the instrument stays playable behind it.
        g.setColour (juce::Colours::black.withAlpha (scrimAlpha * slide));
        g.fillRect (getLocalBounds());

        if (drawer != nullptr)
        {
            // The shadow of the opening the drawer came out of, falling on the
            // chassis to its right.  Glass itself never carries a drop shadow.
            const auto d = drawer->getBounds().toFloat();
            const juce::Rectangle<float> edge (d.getRight(), d.getY(),
                                               drawerEdgeShadow, d.getHeight());

            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.38f * slide),
                                                     edge.getX(), edge.getCentreY(),
                                                     juce::Colours::transparentBlack,
                                                     edge.getRight(), edge.getCentreY(), false));
            g.fillRect (edge);
        }
    }

    void PresetBrowser::mouseDown (const juce::MouseEvent& e)
    {
        // Only scrim clicks get this far: the drawer is a child and keeps its own.
        if (drawer == nullptr || ! drawer->getBounds().contains (e.getPosition()))
            host.setBrowserOpen (false);
    }

    void PresetBrowser::paintDrawer (juce::Graphics& g, juce::Rectangle<float> b) const
    {
        // The drawer is a slab of the same optical glass as the viewport, one
        // step lighter than the wells cut into it so the depth still reads.
        theme::glassSurface (g, b, radiusPanel, theme::glassMid);

        glassLabel (g, "BROWSER", { pad, headerBase }, headerSize, headerTrack, theme::glassInk);

        theme::hairline (g, { pad, headerRuleY }, { b.getRight() - pad, headerRuleY },
                         theme::glassEdge);

        glassLabel (g, "CATEGORY",    { pad, categoryLabelBase },    filterLabelSize, filterLabelTrack);
        glassLabel (g, "MOOD",        { pad, moodLabelBase },        filterLabelSize, filterLabelTrack);
        glassLabel (g, "COLLECTIONS", { pad, collectionsLabelBase }, filterLabelSize, filterLabelTrack);
        glassLabel (g, "RESULTS",     { pad, resultsLabelBase },     filterLabelSize, filterLabelTrack);

        // The count is whatever actually survived the filter - right now, zero.
        glassLabel (g, juce::String (filtered.size())
                           + (filtered.size() == 1 ? " PRESET" : " PRESETS"),
                    { b.getRight() - pad, resultsLabelBase },
                    filterLabelSize, filterLabelTrack, theme::glassInkFaint,
                    juce::Justification::right);

        const auto list = listArea (b);
        theme::glassSurface (g, list, radiusCard, theme::glassDeep);

        if (filtered.isEmpty())
        {
            // There is no factory library yet.  Say so, rather than dressing the
            // panel with names that do not resolve to a file on disk.
            const float centreY = list.getCentreY();

            // drawTracked places a run from the top of its em box, so these two
            // are stacked either side of the well's centre line.
            {
                const auto f = theme::medium (emptyTitleSize);

                g.setColour (theme::glassInkMuted);
                theme::drawTracked (g, "No presets installed",
                                    { list.getX(), centreY - f.getHeight() - emptyGap * 0.5f,
                                      list.getWidth(), f.getHeight() },
                                    f, emptyTitleTrack, juce::Justification::centred);
            }

            {
                const auto f = theme::medium (emptySubSize);

                g.setColour (theme::glassInkFaint);
                theme::drawTracked (g, "The factory library lands with the sound design phase.",
                                    { list.getX(), centreY + emptyGap * 0.5f,
                                      list.getWidth(), f.getHeight() },
                                    f, emptySubTrack, juce::Justification::centred);
            }
        }
    }
}
