#include "PresetBrowser.h"

#include <algorithm>
#include <functional>

// ===========================================================================
//  WHAT IS REAL IN THIS FILE
//
//    - the drawer itself: slide animation, scrim, open/close, and the promise
//      that a closed browser is invisible AND transparent to the mouse, so the
//      instrument stays playable underneath it;
//    - the search box, including the escape-closes handler and the fact that it
//      is the only thing in here that ever GRABS keyboard focus - the results
//      list takes it when a row is clicked, which is what makes the arrow keys
//      and return work, but nothing hands it focus behind the user's back;
//    - the CATEGORY and MOOD filter rows, built from the master spec's
//      vocabularies, single-select, click-again-to-clear, horizontally
//      scrolling;
//    - the FAVOURITES and RECENT collection toggles, both backed by state that
//      PresetManager persists to disk;
//    - the filter pipeline: query + category + mood + favourites + recent are
//      applied to `library` and produce `filtered`, and RECENT also orders by
//      last-used;
//    - the results list over the real library: the factory set plus every user
//      preset found on disk.  A click loads; so does return; so does a double
//      click, which also closes the drawer behind it.
//    - SAVE and DELETE, and the inline panel they open between the collection
//      pills and the results list.  It is a panel rather than a dialog because
//      JUCE_MODAL_LOOPS_PERMITTED is 0 in this build and an async AlertWindow
//      would arrive dressed as some other instrument; it is built out of what
//      the drawer already has - a glass well, tracked labels, the same chips as
//      the filter rows - and it pushes the results list down rather than
//      covering it.  CONFIRM writes a real file through PresetManager and the
//      new preset is selected and scrolled to; CONFIRM in the delete panel
//      removes one, and a factory preset is refused in words.
//
//  WHAT THE BROWSER DELIBERATELY DOES NOT OWN
//
//  None of the preset state.  `library` is a mirror of `PresetManager::all()`
//  and is rebuilt from it after anything that could change it, so there is one
//  answer to "is this a favourite" and it is not in this file.  Applying a
//  preset is PresetManager's job too - it goes through the host gesture
//  protocol so the DAW sees the change - and the browser only says which one.
//
//  STILL MISSING
//    - TAGS as a filter row of its own.  PresetEntry carries the tags and the
//      search box already matches against them, but a tag row would need a
//      vocabulary derived from the library and a second selection model, and
//      the drawer has no vertical room for a third chip row at this size.
//    - renaming a user preset in place.  PresetManager has no rename, and SAVE
//      under a new name plus DELETE of the old one is the whole of it today.
//    - the header bar's preset arrows.  `stepPreset` is here and works;
//      `NacarEditor::selectRelativePreset` in PluginEditor.cpp is what has to
//      call it, and that file is outside this change.
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

    // =======================================================================
    //  The inline save / delete panel.
    //
    //  It opens in the gap under the collection chips and pushes the RESULTS
    //  label and the list below it down by `contentShift()`, so nothing in the
    //  drawer is covered and the list is still a list while the panel is open.
    //  Every offset below is measured from the PANEL's own top edge.
    // =======================================================================
    static constexpr float panelTopY      = 310.0f;   ///< under the collections row
    static constexpr float panelPad       = 16.0f;    ///< gutter inside the panel well
    static constexpr float panelToResults = 20.0f;    ///< panel bottom to RESULTS baseline
    static constexpr float actionRowGap   = 18.0f;    ///< filters | actions, on one row

    static constexpr float panelTitleBase  = 24.0f;
    static constexpr float panelTitleSize  = 8.5f;
    static constexpr float panelTitleTrack = 0.22f;
    static constexpr float panelMsgSize    = 8.5f;
    static constexpr float panelMsgTrack   = 0.06f;

    static constexpr float fieldH   = 30.0f;
    static constexpr float fieldGap = 12.0f;
    static constexpr float buttonH  = 26.0f;

    static constexpr float saveFieldLabelBase = 48.0f;
    static constexpr float saveFieldY         = 54.0f;
    static constexpr float saveCatLabelBase   = 106.0f;
    static constexpr float saveCatRowY        = 112.0f;
    static constexpr float saveMoodLabelBase  = 162.0f;
    static constexpr float saveMoodRowY       = 168.0f;
    static constexpr float saveMessageBase    = 210.0f;
    static constexpr float saveButtonsY       = 218.0f;
    static constexpr float savePanelH         = 260.0f;

    static constexpr float removeNameBase    = 50.0f;
    static constexpr float removeNameSize    = 12.5f;
    static constexpr float removeMessageBase = 74.0f;
    static constexpr float removeButtonsY    = 84.0f;
    static constexpr float removePanelH      = 126.0f;

    /// The longest preset name the name field will take.  Well under the 128
    /// characters `juce::File::createLegalFileName` truncates at, so a typed
    /// name can never be silently shortened on its way to a file name.
    static constexpr int maxPresetNameLength = 96;
    static constexpr int maxTagsLength       = 200;

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

    /** Fills a chip row from a vocabulary.  Both the filter rows and the save
        panel's two choices come through here, so the thirteen category words
        and the sixteen mood words are written down once. */
    static void buildPillRow (const juce::StringArray& words,
                              juce::OwnedArray<PillButton>& pills,
                              juce::Component& row,
                              std::function<void (const juce::String&)> onPick)
    {
        for (const auto& word : words)
        {
            auto* pill = makeFilterPill (word);
            pill->onClick = [word, onPick] { onPick (word); };

            pills.add (pill);
            row.addAndMakeVisible (pill);
        }
    }

    /** The vocabulary's own spelling of `word`, or nothing when the vocabulary
        does not have it.  Preset metadata is free text as far as the file
        format is concerned - a preset written by a later build can carry a
        category this one has no chip for - and a chip row cannot light a word
        it does not contain. */
    static juce::String vocabularyWord (const juce::StringArray& words,
                                        const juce::String& word)
    {
        for (const auto& w : words)
            if (w.equalsIgnoreCase (word))
                return w;

        return {};
    }

    /** A preset name cut down to something that fits on one line of a panel
        message.  The messages are drawn with drawTracked, which neither wraps
        nor elides, and a user may call a preset anything. */
    static juce::String shortName (const juce::String& name, int maxChars = 26)
    {
        return name.length() <= maxChars ? name
                                         : name.substring (0, maxChars - 3) + "...";
    }

    static juce::String quoted (const juce::String& s)
    {
        return "\"" + s + "\"";
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
        : processor (p), host (h),
          presetManager (p.getParameters(), p.getStateManager())
    {
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

        // Named `pill` rather than `p`, which is the constructor's own
        // processor parameter: -Wshadow is on for this build.
        for (auto* pill : { &favouritesPill, &recentPill })
        {
            pill->setTextSize (filterPillSize, filterPillTrack);
            pill->setCornerRadius (radiusPill);
            drawer->addAndMakeVisible (pill);
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

        // -- the two actions, at the far end of the same row -----------------
        //  Same chip, same glass, same size as FAVOURITES and RECENT, but they
        //  act on files rather than on the view - hence the gap and the label
        //  of their own.
        savePill  .setIcon (icons::Icon::plus);
        deletePill.setIcon (icons::Icon::minus);

        savePill  .setTooltip ("Save the current sound as a user preset");
        deletePill.setTooltip ("Delete the selected user preset");

        for (auto* pill : { &savePill, &deletePill })
        {
            pill->setTextSize (filterPillSize, filterPillTrack);
            pill->setCornerRadius (radiusPill);
            drawer->addAndMakeVisible (pill);
        }

        savePill  .onClick = [this] { openSavePanel(); };
        deletePill.onClick = [this] { openDeletePanel(); };

        // -- the panel those two open ----------------------------------------
        //  Children of the drawer like everything else, and hidden until a
        //  panel is open: addChildComponent rather than addAndMakeVisible.
        for (auto* box : { &nameBox, &tagsBox })
        {
            box->setMultiLine (false);
            box->setReturnKeyStartsNewLine (false);
            box->setFont (theme::medium (searchFontSize));
            box->setIndents (12, 6);
            box->onReturnKey = [this] { confirmSave(); };
            box->onEscapeKey = [this] { closePanel (true); };

            drawer->addChildComponent (box);
        }

        // Typed input can therefore never reach the 128 characters
        // createLegalFileName truncates at, which is where two long names
        // would start deriving one file.
        nameBox.setInputRestrictions (maxPresetNameLength);
        nameBox.setSelectAllWhenFocused (true);
        nameBox.setTextToShowWhenEmpty ("Preset name", theme::glassInkFaint);

        nameBox.onTextChange = [this]
        {
            // An overwrite warned about is armed for ONE name.  Change the
            // name and the second CONFIRM is a first press again.
            armedOverwrite.clear();

            if (panelMessage.isNotEmpty())
                setPanelMessage ({}, false);
        };

        tagsBox.setInputRestrictions (maxTagsLength);
        tagsBox.setTextToShowWhenEmpty ("comma, separated", theme::glassInkFaint);

        for (auto* v : { &saveCategoryViewport, &saveMoodViewport })
        {
            v->setScrollBarsShown (false, true, false, true);
            v->setScrollBarThickness (scrollBarThickness);
            v->setWantsKeyboardFocus (false);
            drawer->addChildComponent (v);
        }

        saveCategoryViewport.setViewedComponent (&saveCategoryRow, false);
        saveMoodViewport    .setViewedComponent (&saveMoodRow,     false);

        for (auto* pill : { &confirmPill, &cancelPill })
        {
            pill->setTextSize (filterPillSize, filterPillTrack);
            pill->setCornerRadius (radiusPill);
            drawer->addChildComponent (pill);
        }

        confirmPill.onClick = [this]
        {
            if (panel == PanelMode::save)
                confirmSave();
            else if (panel == PanelMode::remove)
                confirmDelete();
        };

        cancelPill.onClick = [this] { closePanel (true); };

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
        syncLibrary();
    }

    PresetBrowser::~PresetBrowser()
    {
        stopTimer();
        resultsList.setModel (nullptr);
    }

    void PresetBrowser::buildFilterPills()
    {
        buildPillRow (categoryVocabulary(), categoryPills, categoryRow,
                      [this] (const juce::String& word) { setCategoryFilter (word); });

        buildPillRow (moodVocabulary(), moodPills, moodRow,
                      [this] (const juce::String& word) { setMoodFilter (word); });

        // The save panel's category and mood are the SAME two vocabularies and
        // the same chips: one word list, read by the thing that filters on it
        // and by the thing that writes it into a file.  A word that could be
        // saved but not filtered for, or the other way round, would be a bug
        // waiting for someone to edit one list and not the other.
        buildPillRow (categoryVocabulary(), saveCategoryPills, saveCategoryRow,
                      [this] (const juce::String& word)
                      {
                          saveCategory = (saveCategory == word ? juce::String() : word);
                          refreshSavePills();
                      });

        buildPillRow (moodVocabulary(), saveMoodPills, saveMoodRow,
                      [this] (const juce::String& word)
                      {
                          saveMood = (saveMood == word ? juce::String() : word);
                          refreshSavePills();
                      });
    }

    // -----------------------------------------------------------------------
    //  Open / close
    // -----------------------------------------------------------------------
    void PresetBrowser::setOpen (bool shouldBeOpen)
    {
        if (open == shouldBeOpen)
            return;

        open = shouldBeOpen;

        // A panel left open is a half-finished sentence: whatever it was about
        // may not even be in the library by the time the drawer comes back.
        closePanel (false);

        if (open)
        {
            setVisible (true);
            setInterceptsMouseClicks (true, true);
            toFront (false);        // false: coming forward must not steal focus

            // Re-read rather than trust the last mirror: a user preset may have
            // been saved, renamed or deleted on disk since the drawer last
            // closed, and the browser is the thing that would show a stale one.
            presetManager.refresh();
            syncLibrary();
            showCurrentPreset();
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
        // The list keeps the bottom of the drawer and gives up the top of its
        // own well to an open panel, so the results are still results while
        // something is being saved or deleted rather than being covered over.
        const float top = listTop + contentShift();

        return { pad, top,
                 drawerLocal.getWidth() - pad * 2.0f,
                 drawerLocal.getHeight() - pad - top };
    }

    float PresetBrowser::actionPanelHeight() const
    {
        switch (panel)
        {
            case PanelMode::save:   return savePanelH;
            case PanelMode::remove: return removePanelH;
            case PanelMode::none:   break;
        }

        return 0.0f;
    }

    float PresetBrowser::contentShift() const
    {
        if (panel == PanelMode::none)
            return 0.0f;

        return panelTopY + actionPanelHeight() + panelToResults - resultsLabelBase;
    }

    juce::Rectangle<float> PresetBrowser::actionPanelBounds (juce::Rectangle<float> drawerLocal) const
    {
        return { pad, panelTopY,
                 drawerLocal.getWidth() - pad * 2.0f,
                 actionPanelHeight() };
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

        // SAVE and DELETE share the row but not its meaning, so they are laid
        // out from the far end inwards, under a label of their own.  The two
        // groups cannot meet: four chips at this text size come to a little
        // over half the drawer's content width.
        float right = b.getRight() - pad;

        for (auto* p : { &deletePill, &savePill })
        {
            p->setSize (1, juce::roundToInt (rowH));

            const float w = p->preferredWidth (pillPad);
            p->setBounds (juce::Rectangle<float> (juce::jmax (x + actionRowGap, right - w),
                                                  collectionsRowY, w, rowH).toNearestInt());
            right -= w + pillGap;
        }

        layoutActionPanel (actionPanelBounds (b));

        resultsList.setBounds (listArea (b).reduced (listWellInset).toNearestInt());
    }

    void PresetBrowser::layoutActionPanel (juce::Rectangle<float> panelBounds)
    {
        const float innerX = panelBounds.getX() + panelPad;
        const float innerW = panelBounds.getWidth() - panelPad * 2.0f;
        const float fieldW = (innerW - fieldGap) * 0.5f;

        nameBox.setBounds (juce::Rectangle<float> (innerX, panelBounds.getY() + saveFieldY,
                                                   fieldW, fieldH).toNearestInt());

        tagsBox.setBounds (juce::Rectangle<float> (innerX + fieldW + fieldGap,
                                                   panelBounds.getY() + saveFieldY,
                                                   fieldW, fieldH).toNearestInt());

        const float viewportH = rowH + (float) scrollBarThickness;

        saveCategoryViewport.setBounds (juce::Rectangle<float> (innerX,
                                                                panelBounds.getY() + saveCatRowY,
                                                                innerW, viewportH).toNearestInt());

        saveMoodViewport.setBounds (juce::Rectangle<float> (innerX,
                                                            panelBounds.getY() + saveMoodRowY,
                                                            innerW, viewportH).toNearestInt());

        layoutPillRow (saveCategoryPills, saveCategoryRow);
        layoutPillRow (saveMoodPills,     saveMoodRow);

        // CANCEL then CONFIRM, right-aligned: the press that commits is the one
        // furthest from the fields, and the one nearest the thumb.
        const float buttonsY = panelBounds.getY()
                                   + (panel == PanelMode::remove ? removeButtonsY : saveButtonsY);

        float right = panelBounds.getRight() - panelPad;

        for (auto* p : { &confirmPill, &cancelPill })
        {
            p->setSize (1, juce::roundToInt (buttonH));

            const float w = p->preferredWidth (pillPad * 1.5f);
            p->setBounds (juce::Rectangle<float> (right - w, buttonsY, w, buttonH).toNearestInt());
            right -= w + pillGap;
        }
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

    void PresetBrowser::refreshSavePills()
    {
        const auto& categories = categoryVocabulary();
        const auto& moods      = moodVocabulary();

        for (int i = 0; i < saveCategoryPills.size() && i < categories.size(); ++i)
            saveCategoryPills[i]->setSelected (saveCategory == categories[i]);

        for (int i = 0; i < saveMoodPills.size() && i < moods.size(); ++i)
            saveMoodPills[i]->setSelected (saveMood == moods[i]);
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
    //  The library
    // -----------------------------------------------------------------------
    void PresetBrowser::syncLibrary()
    {
        library.clearQuick();

        for (const auto& info : presetManager.all())
        {
            PresetEntry e;
            e.name      = info.name;
            e.author    = info.author;
            e.category  = info.category;
            e.mood      = info.mood;
            e.tags      = info.tags;
            e.favourite = info.favourite;
            e.lastUsed  = info.lastUsed;
            e.file      = info.file;
            e.factory   = info.isFactory();

            library.add (e);
        }

        rebuildFilter();
    }

    bool PresetBrowser::loadPreset (int index)
    {
        if (! juce::isPositiveAndBelow (index, library.size()))
            return false;

        // Loading changes the parameter state SAVE would capture, so a save
        // panel cannot be left standing over it: its name field would still say
        // one patch while CONFIRM wrote another.  The focus is deliberately NOT
        // taken back here - the list has just been given it, and that is what
        // makes the arrow keys work.
        closePanel (false);

        if (! presetManager.apply (index))
            return false;

        // apply() stamps lastUsed, which RECENT sorts on, so the mirror has to
        // be refreshed rather than patched in place.
        const auto selectedName = library.getReference (index).name;

        syncLibrary();
        host.chassisChanged();

        for (int row = 0; row < filtered.size(); ++row)
            if (library.getReference (filtered[row]).name == selectedName)
            {
                resultsList.selectRow (row, true, true);
                break;
            }

        return true;
    }

    bool PresetBrowser::stepPreset (int delta)
    {
        closePanel (false);

        if (! presetManager.step (delta))
            return false;

        syncLibrary();
        showCurrentPreset();
        host.chassisChanged();

        return true;
    }

    void PresetBrowser::showCurrentPreset()
    {
        const int current = presetManager.currentIndex();

        if (current < 0)
            return;

        for (int row = 0; row < filtered.size(); ++row)
            if (filtered[row] == current)
            {
                resultsList.selectRow (row, true, true);
                resultsList.scrollToEnsureRowIsOnscreen (row);
                return;
            }
    }

    // -----------------------------------------------------------------------
    //  Saving and deleting
    //
    //  Everything here is message thread: PresetManager does the file IO and
    //  the host gesture protocol, and this file only says which preset, under
    //  what name, and what to do when the answer is no.
    // -----------------------------------------------------------------------
    juce::String PresetBrowser::presetFileNameFor (const juce::String& name)
    {
        // PresetManager owns this derivation; the browser only needs to ask,
        // because it has to know BEFORE it saves which file a name would land
        // on. createLegalFileName DROPS the characters a path cannot carry
        // rather than substituting for them, so "Bass/Lead" and "BassLead" are
        // one file and one would silently replace the other.
        //
        // It answers for the library the browser can see. A .nacarpreset on
        // disk that failed to parse is in no row and collides silently, which
        // is the one case this cannot warn about.
        return PresetManager::fileNameFor (name);
    }

    int PresetBrowser::indexOfPresetNamed (const juce::String& name) const
    {
        // Exactly what PresetManager::indexOfName does, and it has to be: that
        // is the lookup which decides which row a newly written file becomes.
        for (int i = 0; i < library.size(); ++i)
            if (library.getReference (i).name == name)
                return i;

        return -1;
    }

    int PresetBrowser::indexOfPresetFileNamed (const juce::String& fileName) const
    {
        for (int i = 0; i < library.size(); ++i)
        {
            const auto& e = library.getReference (i);

            if (! e.factory && e.file.getFileName() == fileName)
                return i;
        }

        return -1;
    }

    juce::String PresetBrowser::uniquePresetName (const juce::String& base) const
    {
        // Leave room for the suffix, so a long name is not pushed past what the
        // name field will hold and quietly cut somewhere else.
        const auto stem = base.trim().substring (0, maxPresetNameLength - 8).trim();
        const auto root = stem.isEmpty() ? juce::String ("New Preset") : stem;

        for (int n = 1; n < 100; ++n)
        {
            const auto candidate = root + " Copy"
                                        + (n == 1 ? juce::String() : " " + juce::String (n));

            if (indexOfPresetNamed (candidate) < 0
                && indexOfPresetFileNamed (presetFileNameFor (candidate)) < 0)
                return candidate;
        }

        // A hundred copies of one preset: propose the plain one and let the
        // overwrite warning have the last word.
        return root + " Copy";
    }

    int PresetBrowser::deletionTarget() const
    {
        const int row = resultsList.getSelectedRow();

        if (juce::isPositiveAndBelow (row, filtered.size()))
            return filtered[row];

        return presetManager.currentIndex();
    }

    bool PresetBrowser::revealPreset (int libraryIndex)
    {
        if (! juce::isPositiveAndBelow (libraryIndex, library.size()))
            return false;

        if (! filtered.contains (libraryIndex))
        {
            // A preset the user has just made and cannot see has not visibly
            // landed.  Whatever is hiding it - a category, a mood, FAVOURITES,
            // RECENT, the search box - gives way; each of those is one click to
            // put back, and none of them is worth more than the confirmation
            // that the save worked.
            searchBox.setText ({}, false);
            activeCategory.clear();
            activeMood.clear();
            favouritesOnly = false;
            recentOnly     = false;

            refreshFilterPills();
            rebuildFilter();
        }

        for (int row = 0; row < filtered.size(); ++row)
            if (filtered[row] == libraryIndex)
            {
                resultsList.selectRow (row, true, true);
                resultsList.scrollToEnsureRowIsOnscreen (row);
                return true;
            }

        return false;
    }

    void PresetBrowser::setPanelMessage (const juce::String& text, bool isWarning)
    {
        panelMessage = text;
        panelMessageIsWarning = isWarning;

        if (drawer != nullptr)
            drawer->repaint();
    }

    void PresetBrowser::setPanel (PanelMode mode)
    {
        panel = mode;

        panelMessage.clear();
        panelMessageIsWarning = false;
        armedOverwrite.clear();

        const bool saving   = (mode == PanelMode::save);
        const bool removing = (mode == PanelMode::remove);

        if (! removing)
            deleteIndex = -1;

        const bool canDelete = removing
                                   && juce::isPositiveAndBelow (deleteIndex, library.size())
                                   && ! library.getReference (deleteIndex).factory;

        nameBox.setVisible (saving);
        tagsBox.setVisible (saving);
        saveCategoryViewport.setVisible (saving);
        saveMoodViewport    .setVisible (saving);

        // Spec section 01, no dead controls: CONFIRM is only on the panel when
        // there is something for it to confirm, so a refused delete offers
        // CANCEL and nothing that would do nothing.
        confirmPill.setVisible (saving || canDelete);
        cancelPill .setVisible (saving || removing);

        savePill  .setSelected (saving);
        deletePill.setSelected (removing);

        if (drawer != nullptr)
        {
            layoutDrawer();     // the list moves up or down to make the room
            drawer->repaint();
        }
    }

    void PresetBrowser::closePanel (bool returnFocus)
    {
        if (panel != PanelMode::none)
            setPanel (PanelMode::none);

        if (returnFocus && isShowing())
            searchBox.grabKeyboardFocus();
    }

    void PresetBrowser::openSavePanel()
    {
        if (panel == PanelMode::save)       // the chip toggles its own panel
        {
            closePanel (true);
            return;
        }

        // Everything defaults off whatever is loaded, so the common case -
        // "this, but mine" - is SAVE then CONFIRM and nothing in between.
        juce::String base ("New Preset"), category, mood;
        juce::StringArray tags;

        const int loaded = presetManager.currentIndex();

        if (juce::isPositiveAndBelow (loaded, library.size()))
        {
            const auto& e = library.getReference (loaded);

            base     = e.name;
            category = e.category;
            mood     = e.mood;
            tags     = e.tags;
        }

        saveCategory = vocabularyWord (categoryVocabulary(), category);
        saveMood     = vocabularyWord (moodVocabulary(),     mood);

        setPanel (PanelMode::save);

        // The suffixed name is chosen to be free: saveUserPreset writes its
        // file with no regard for one already being there, so an unwary default
        // would replace the preset it was named after.
        nameBox.setText (uniquePresetName (base), false);
        tagsBox.setText (tags.joinIntoString (", "), false);

        refreshSavePills();

        if (isShowing())
        {
            nameBox.grabKeyboardFocus();
            nameBox.selectAll();
        }
    }

    void PresetBrowser::openDeletePanel()
    {
        if (panel == PanelMode::remove)
        {
            closePanel (true);
            return;
        }

        deleteIndex = deletionTarget();
        setPanel (PanelMode::remove);

        if (! juce::isPositiveAndBelow (deleteIndex, library.size()))
            setPanelMessage ("Select a preset in the results list, then press DELETE.", true);
        else if (library.getReference (deleteIndex).factory)
            setPanelMessage ("A factory preset is part of the build. It cannot be deleted.", true);
        else
            setPanelMessage ("Its file is removed from disk. This cannot be undone.", true);
    }

    void PresetBrowser::confirmSave()
    {
        if (panel != PanelMode::save)
            return;

        const auto name = nameBox.getText().trim();

        // saveUserPreset returns -1 for an empty name, silently.  Say so here
        // instead, where the field the user is looking at is.
        if (name.isEmpty())
        {
            setPanelMessage ("Name the preset before saving.", true);
            return;
        }

        const auto fileName = presetFileNameFor (name);

        // A name made entirely of characters a file name cannot carry - "///",
        // "?:*" - legalises to nothing at all, and the file would be written as
        // a bare extension with a leading dot: hidden on this platform, and
        // impossible to tell from any other such name.
        if (fileName == PresetManager::fileExtension() || fileName.startsWithChar ('.'))
        {
            setPanelMessage ("That name leaves nothing a file can be called. Try another.", true);
            return;
        }

        // A name a FACTORY preset already carries is refused outright rather
        // than warned about.  PresetManager would write the file quite happily,
        // but the library puts the factory set first and resolves a name to the
        // first row carrying it - so the save would report the factory preset
        // as its result, select that, and leave the new file in the library as
        // a second row of the same name that nothing here can reach.
        const int sameName = indexOfPresetNamed (name);

        if (sameName >= 0 && library.getReference (sameName).factory)
        {
            setPanelMessage ("A factory preset is already called that. Choose another name.", true);
            return;
        }

        // Anything else landing on the same file is an overwrite, and the write
        // is silent about it.  One press warns, the next goes through, and any
        // edit to the name disarms it again.
        const int clash = indexOfPresetFileNamed (fileName);

        if (clash >= 0 && armedOverwrite != fileName)
        {
            armedOverwrite = fileName;

            const auto& victim = library.getReference (clash);

            setPanelMessage (victim.name == name
                                 ? "A user preset is already called that. CONFIRM again to replace it."
                                 : "This replaces the user preset " + quoted (shortName (victim.name))
                                       + ". CONFIRM again.",
                             true);
            return;
        }

        auto tags = juce::StringArray::fromTokens (tagsBox.getText(), ",", "");
        tags.trim();
        tags.removeEmptyStrings();

        const int index = presetManager.saveUserPreset (name, saveCategory, saveMood, tags);

        if (index < 0)
        {
            setPanelMessage ("The preset could not be written to disk.", true);
            return;
        }

        closePanel (false);

        // saveUserPreset has refreshed the library and written the new preset's
        // identity into the session, so the header bar is already naming it;
        // the mirror and the chassis are what have to catch up.
        syncLibrary();
        host.chassisChanged();
        revealPreset (index);
    }

    void PresetBrowser::confirmDelete()
    {
        if (panel != PanelMode::remove)
            return;

        if (! juce::isPositiveAndBelow (deleteIndex, library.size()))
        {
            setPanelMessage ("There is nothing selected to delete.", true);
            return;
        }

        // deleteUserPreset refuses a factory preset by returning false, which
        // on its own would look like a button that does nothing.  The refusal
        // is made before the call, in words, and CONFIRM is not even on the
        // panel in that case - this is the belt to that pair of braces.
        if (library.getReference (deleteIndex).factory)
        {
            setPanelMessage ("A factory preset is part of the build. It cannot be deleted.", true);
            return;
        }

        const auto name = library.getReference (deleteIndex).name;

        if (! presetManager.deleteUserPreset (deleteIndex))
        {
            setPanelMessage ("Could not delete " + quoted (shortName (name))
                                 + " - its file may be gone or read only.", true);
            return;
        }

        closePanel (false);

        // The delete refreshed PresetManager, so every index after the removed
        // one has moved: the mirror is rebuilt rather than patched.
        syncLibrary();
        showCurrentPreset();
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

        const int index = filtered[row];

        // The heart is a control, not part of the row: hitting it toggles the
        // favourite and must not also load the preset, or marking something to
        // come back to would take you away from what you are playing.
        if ((float) e.x >= (float) rowWidth - rowPad - rowHeartSize * 2.0f)
        {
            const bool wanted = ! library.getReference (index).favourite;

            presetManager.setFavourite (index, wanted);
            library.getReference (index).favourite = wanted;

            if (favouritesOnly)
                rebuildFilter();
            else
                resultsList.repaintRow (row);

            // The ListBox selected the row on the way in, which would leave the
            // violet accent bar pointing at a preset that is not the one
            // playing.  Put the marker back on whatever is actually loaded.
            showCurrentPreset();

            return;
        }

        // Anywhere else on the row loads it, and the drawer stays open: the
        // instrument is playable while browsing, so the point of a single
        // click is to hear the next preset without leaving the list.
        loadPreset (index);
    }

    void PresetBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
    {
        if (! juce::isPositiveAndBelow (row, filtered.size()))
            return;

        const int rowWidth = (e.eventComponent != nullptr ? e.eventComponent->getWidth()
                                                          : resultsList.getWidth());

        if ((float) e.x >= (float) rowWidth - rowPad - rowHeartSize * 2.0f)
            return;                     // the heart already answered the first click

        // The single click has already loaded it.  The second one says "that
        // is the one" and puts the drawer away.
        host.setBrowserOpen (false);
    }

    void PresetBrowser::returnKeyPressed (int row)
    {
        if (! juce::isPositiveAndBelow (row, filtered.size()))
            return;

        if (loadPreset (filtered[row]))
            host.setBrowserOpen (false);
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

        // FAVOURITES and RECENT change what the list shows; SAVE and DELETE
        // change what is on disk.  One row, two labels, because one label over
        // both of them would be a lie about one half.
        glassLabel (g, "USER PRESETS", { b.getRight() - pad, collectionsLabelBase },
                    filterLabelSize, filterLabelTrack, theme::glassInkMuted,
                    juce::Justification::right);

        if (panel != PanelMode::none)
            paintActionPanel (g, actionPanelBounds (b));

        // Everything from here down moves aside for an open panel.
        const float shift = contentShift();

        glassLabel (g, "RESULTS", { pad, resultsLabelBase + shift },
                    filterLabelSize, filterLabelTrack);

        // The count is whatever actually survived the filter.
        glassLabel (g, juce::String (filtered.size())
                           + (filtered.size() == 1 ? " PRESET" : " PRESETS"),
                    { b.getRight() - pad, resultsLabelBase + shift },
                    filterLabelSize, filterLabelTrack, theme::glassInkFaint,
                    juce::Justification::right);

        const auto list = listArea (b);
        theme::glassSurface (g, list, radiusCard, theme::glassDeep);

        if (filtered.isEmpty())
        {
            // Two different empty states, because they mean different things:
            // a filter that matched nothing is the user's own doing and is
            // fixed by clearing it, while an empty library is a broken install.
            const bool libraryIsEmpty = library.isEmpty();

            const juce::String title = libraryIsEmpty ? "No presets installed"
                                                      : "Nothing matches";

            const juce::String subtitle =
                libraryIsEmpty
                    ? juce::String ("The factory library could not be read.")
                    : juce::String ("Clear the search or the filters above to see the other ")
                          + juce::String (library.size()) + " presets.";

            const float centreY = list.getCentreY();

            // drawTracked places a run from the top of its em box, so these two
            // are stacked either side of the well's centre line.
            {
                const auto f = theme::medium (emptyTitleSize);

                g.setColour (theme::glassInkMuted);
                theme::drawTracked (g, title,
                                    { list.getX(), centreY - f.getHeight() - emptyGap * 0.5f,
                                      list.getWidth(), f.getHeight() },
                                    f, emptyTitleTrack, juce::Justification::centred);
            }

            {
                const auto f = theme::medium (emptySubSize);

                g.setColour (theme::glassInkFaint);
                theme::drawTracked (g, subtitle,
                                    { list.getX(), centreY + emptyGap * 0.5f,
                                      list.getWidth(), f.getHeight() },
                                    f, emptySubTrack, juce::Justification::centred);
            }
        }
    }

    void PresetBrowser::paintActionPanel (juce::Graphics& g,
                                          juce::Rectangle<float> panelBounds) const
    {
        // The same well as the results list below it, cut into the same slab:
        // the panel is part of the drawer rather than a card floating on it.
        theme::glassSurface (g, panelBounds, radiusCard, theme::glassDeep);

        const float innerX = panelBounds.getX() + panelPad;
        const float innerW = panelBounds.getWidth() - panelPad * 2.0f;
        const float top    = panelBounds.getY();

        // A warning is violet - the one accent this instrument has, and what
        // "look here" already means everywhere else in it.  A note is muted.
        const auto messageColour = panelMessageIsWarning ? theme::violet
                                                         : theme::glassInkMuted;

        if (panel == PanelMode::save)
        {
            const float fieldW = (innerW - fieldGap) * 0.5f;

            glassLabel (g, "SAVE PRESET", { innerX, top + panelTitleBase },
                        panelTitleSize, panelTitleTrack, theme::glassInk);

            glassLabel (g, "NAME", { innerX, top + saveFieldLabelBase },
                        filterLabelSize, filterLabelTrack);
            glassLabel (g, "TAGS", { innerX + fieldW + fieldGap, top + saveFieldLabelBase },
                        filterLabelSize, filterLabelTrack);
            glassLabel (g, "CATEGORY", { innerX, top + saveCatLabelBase },
                        filterLabelSize, filterLabelTrack);
            glassLabel (g, "MOOD", { innerX, top + saveMoodLabelBase },
                        filterLabelSize, filterLabelTrack);

            glassLabel (g, panelMessage, { innerX, top + saveMessageBase },
                        panelMsgSize, panelMsgTrack, messageColour);

            return;
        }

        glassLabel (g, "DELETE PRESET", { innerX, top + panelTitleBase },
                    panelTitleSize, panelTitleTrack, theme::glassInk);

        // The preset's name, drawn the way the results rows draw theirs - and
        // elided, because a user preset can be called anything up to the length
        // of the name field and the well has one line for it.
        const bool haveTarget = juce::isPositiveAndBelow (deleteIndex, library.size());

        const juce::String targetName = haveTarget ? library.getReference (deleteIndex).name
                                                   : juce::String ("Nothing selected");

        const auto nameFont = theme::medium (removeNameSize);

        g.setFont (nameFont);
        g.setColour (haveTarget ? theme::glassInk : theme::glassInkFaint);
        g.drawText (targetName,
                    juce::Rectangle<float> (innerX, top + removeNameBase - nameFont.getAscent(),
                                            innerW, nameFont.getHeight()),
                    juce::Justification::centredLeft, true);

        glassLabel (g, panelMessage, { innerX, top + removeMessageBase },
                    panelMsgSize, panelMsgTrack, messageColour);
    }
}
