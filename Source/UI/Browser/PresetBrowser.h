#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Theme.h"
#include "../Layout.h"
#include "../EditorHost.h"
#include "../Components/Widgets.h"
#include "../Components/Icons.h"
#include "../../Plugin/PluginProcessor.h"
#include "../../Presets/PresetManager.h"

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

        /** True for a compiled-in preset - `PresetInfo::isFactory()`, mirrored.
            A row needs this because DELETE has to refuse a factory preset to
            the user's face rather than by quietly failing, and the browser
            answers that question from its own mirror like every other one. */
        bool              factory = false;

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

        The library comes from `PresetManager`: the compiled-in factory set
        plus whatever user presets are on disk.  The browser reads that array
        and never invents a row, so a name in this list always resolves to
        something that can actually be loaded.
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

        /** Loads the next or previous preset in the library, wrapping.  This
            is what the header bar's preset arrows want; `EditorHost::
            selectRelativePreset` is the one line away from reaching it. */
        bool stepPreset (int delta);

        /** The library, in case something outside wants to count it. */
        int getNumPresets() const noexcept { return library.size(); }

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
        void returnKeyPressed (int row) override;

        // -- geometry -------------------------------------------------------
        juce::Rectangle<int>   drawerRestBounds() const;
        juce::Rectangle<float> listArea (juce::Rectangle<float> drawerLocal) const;
        void layoutDrawer();

        // -- the inline action panel ----------------------------------------
        /** What, if anything, is open between the collection pills and the
            results list.  `remove` rather than `delete`, which is a keyword. */
        enum class PanelMode { none, save, remove };

        /** Shows or hides the panel, sets the widgets that belong to the mode
            visible, and re-lays the drawer out so the list makes room. */
        void setPanel (PanelMode);

        void openSavePanel();
        void openDeletePanel();
        void confirmSave();
        void confirmDelete();

        /** Writes the line the panel shows under its fields.  A warning is
            violet - the one accent this instrument has - and a note is muted. */
        void setPanelMessage (const juce::String&, bool isWarning);

        /** Height of the open panel, and how far the RESULTS block below it is
            pushed down.  Both are zero when nothing is open. */
        float actionPanelHeight() const;
        float contentShift() const;
        juce::Rectangle<float> actionPanelBounds (juce::Rectangle<float> drawerLocal) const;

        void layoutActionPanel (juce::Rectangle<float> panelBounds);
        void paintActionPanel (juce::Graphics&, juce::Rectangle<float> panelBounds) const;

        /** Closes whatever panel is open.  `returnFocus` hands the keyboard
            back to the search box, which is what escape and CANCEL should do -
            and what a row click must NOT do, because the results list has just
            been given the focus that makes its arrow keys work. */
        void closePanel (bool returnFocus);

        /** Lights the save panel's chips from `saveCategory` / `saveMood`, the
            way refreshFilterPills lights the filter rows. */
        void refreshSavePills();

        /** The library row DELETE would act on: whatever is selected in the
            results list, or failing that whatever is loaded.  -1 for neither. */
        int deletionTarget() const;

        /** Index of the row carrying exactly this name, or -1.  Matches
            `PresetManager::indexOfName`, which is what decides which row a
            newly written file turns into. */
        int indexOfPresetNamed (const juce::String&) const;

        /** Index of the USER row whose file is called this, or -1.  Not the
            same question as the one above: two different preset names can
            derive the same file name, and then one file overwrites the other. */
        int indexOfPresetFileNamed (const juce::String&) const;

        /** `base` with a suffix, moved along until no row and no file on disk
            already answers to it. */
        juce::String uniquePresetName (const juce::String& base) const;

        /** The file `PresetManager::saveUserPreset` would write for this name.
            Mirrors the one line in PresetManager that derives it; there is no
            accessor for it, and inventing one would mean editing that file. */
        static juce::String presetFileNameFor (const juce::String& name);

        /** Selects and scrolls to a library row, clearing whatever filter is
            hiding it first.  False if the row does not exist at all. */
        bool revealPreset (int libraryIndex);

        // -- model ----------------------------------------------------------
        void buildFilterPills();
        void rebuildFilter();
        void refreshFilterPills();
        void setCategoryFilter (const juce::String&);
        void setMoodFilter (const juce::String&);

        /** Copies PresetManager's array into `library` and re-applies the
            filter.  The browser owns no preset state of its own. */
        void syncLibrary();

        /** Applies library[index] and leaves the row selected.  Returns false
            if the preset could not be loaded, in which case nothing changed. */
        bool loadPreset (int index);

        /** Scrolls to and selects whatever is currently loaded, if it survived
            the filter. */
        void showCurrentPreset();

        void paintDrawer (juce::Graphics&, juce::Rectangle<float> drawerLocal) const;

        NacarProcessor& processor;
        EditorHost& host;

        /** The library, and the only thing that may change it.  Declared
            before the widgets so it outlives every callback that reaches it. */
        PresetManager presetManager;

        juce::TextEditor searchBox;
        IconButton closeButton { icons::Icon::cross, IconButton::Style::plain };

        juce::Viewport  categoryViewport, moodViewport;
        juce::Component categoryRow, moodRow;
        juce::OwnedArray<PillButton> categoryPills, moodPills;

        PillButton favouritesPill { "FAVOURITES", PillButton::Style::glass };
        PillButton recentPill     { "RECENT",     PillButton::Style::glass };

        // -- the two actions, on the same row and in the same glass ---------
        PillButton savePill   { "SAVE",   PillButton::Style::glass };
        PillButton deletePill { "DELETE", PillButton::Style::glass };

        // -- the inline panel those two open --------------------------------
        //  Not a dialog: JUCE_MODAL_LOOPS_PERMITTED is 0 in this build, and an
        //  async AlertWindow would be a piece of some other instrument.  It is
        //  the drawer's own vocabulary - a glass well, tracked labels, the same
        //  chips as the filter rows - opened in place above the results.
        juce::TextEditor nameBox, tagsBox;

        juce::Viewport  saveCategoryViewport, saveMoodViewport;
        juce::Component saveCategoryRow, saveMoodRow;
        juce::OwnedArray<PillButton> saveCategoryPills, saveMoodPills;

        PillButton confirmPill { "CONFIRM", PillButton::Style::violet };
        PillButton cancelPill  { "CANCEL",  PillButton::Style::glass };

        PanelMode    panel = PanelMode::none;
        juce::String saveCategory, saveMood;

        juce::String panelMessage;
        bool         panelMessageIsWarning = false;

        /** The file name a second CONFIRM press is allowed to overwrite.  One
            press warns, the next one goes through - and any edit to the name
            clears it, so the warning is never spent on a different file. */
        juce::String armedOverwrite;

        /** The row the open DELETE panel is about, -1 when it is about
            nothing.  Captured when the panel opens so that CONFIRM cannot act
            on a row the selection moved to in the meantime. */
        int deleteIndex = -1;

        juce::ListBox resultsList { "presets", nullptr };

        /** The library, mirrored out of PresetManager.  Index i here is index
            i there, which is what makes a row clickable. */
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
