#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Theme.h"
#include "../Layout.h"
#include "../EditorHost.h"
#include "../Components/Widgets.h"
#include "../Components/Icons.h"
#include "../../Plugin/PluginProcessor.h"

namespace nacar::ui
{
    /**
        One row of the browser's library model.

        This is the shape the browser filters and draws.  It is deliberately the
        whole of what a row needs and nothing more; when PresetSystem lands,
        PresetManager fills an array of these and the browser does not change.

        The field names line up with the preset identity already reserved in
        StateManager (ids::presetName, presetAuthor, presetCategory, presetMood,
        presetTags, presetFavourite, presetFile).
    */
    struct PresetEntry
    {
        juce::String      name;
        juce::String      author;
        juce::String      category;      ///< one of the thirteen category words
        juce::String      mood;          ///< one of the sixteen mood words
        juce::StringArray tags;
        bool              favourite = false;
        juce::int64       lastUsed  = 0; ///< ms since epoch; 0 means never loaded
        juce::File        file;

        /** True when the (already lower-cased, already trimmed) query appears in
            the name, author, category, mood or any tag. */
        bool matches (const juce::String& lowercaseQuery) const;
    };

    /**
        The preset browser - a dark optical-glass drawer over the whole canvas.

        Master build spec section 143: SEARCH, CATEGORY, MOOD, TAGS, FAVORITES,
        RECENT, and the instrument stays playable while browsing.

        The drawer slides in from the left over a dimming scrim.  When closed the
        component is invisible *and* transparent to the mouse, so it can never
        stand between the player and the instrument.  Nothing in here grabs
        keyboard focus except the search box.

        There is no factory library yet (Phase 27), so the list is genuinely
        empty and says so.  See the comment block at the top of the .cpp for
        exactly what is wired and what is waiting.
    */
    class PresetBrowser : public juce::Component,
                          private juce::ListBoxModel,
                          private juce::Timer
    {
    public:
        PresetBrowser (NacarProcessor&, EditorHost&);
        ~PresetBrowser() override;

        /** Opens or closes the drawer, with the slide animation. */
        void setOpen (bool);

        bool isOpen() const noexcept { return open; }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        class Drawer;

        // -- animation ------------------------------------------------------
        void timerCallback() override;
        void positionDrawer();

        // -- juce::ListBoxModel ---------------------------------------------
        int  getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height,
                               bool rowIsSelected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

        // -- geometry -------------------------------------------------------
        juce::Rectangle<int>   drawerRestBounds() const;
        juce::Rectangle<float> listArea (juce::Rectangle<float> drawerLocal) const;
        void layoutDrawer();

        // -- model ----------------------------------------------------------
        void buildFilterPills();
        void rebuildFilter();
        void refreshFilterPills();
        void setCategoryFilter (const juce::String&);
        void setMoodFilter (const juce::String&);

        void paintDrawer (juce::Graphics&, juce::Rectangle<float> drawerLocal) const;

        NacarProcessor& processor;
        EditorHost& host;

        juce::TextEditor searchBox;
        IconButton closeButton { icons::Icon::cross, IconButton::Style::plain };

        juce::Viewport  categoryViewport, moodViewport;
        juce::Component categoryRow, moodRow;
        juce::OwnedArray<PillButton> categoryPills, moodPills;

        PillButton favouritesPill { "FAVOURITES", PillButton::Style::glass };
        PillButton recentPill     { "RECENT",     PillButton::Style::glass };

        juce::ListBox resultsList { "presets", nullptr };

        /** The library.  Empty until PresetSystem/PresetManager exists - the
            browser reads it, never invents it. */
        juce::Array<PresetEntry> library;

        /** Indices into `library` that survive the current filter. */
        juce::Array<int> filtered;

        juce::String activeCategory, activeMood;
        bool favouritesOnly = false;
        bool recentOnly     = false;

        bool  open  = false;
        float phase = 0.0f;   ///< linear 0..1 animation position
        float slide = 0.0f;   ///< eased phase: 0 fully hidden, 1 fully out

        // Declared last so it is destroyed first, detaching the widgets above
        // while they are all still alive.
        std::unique_ptr<Drawer> drawer;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
    };
}
