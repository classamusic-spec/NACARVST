#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>

#include "../Theme.h"
#include "../Layout.h"
#include "../EditorHost.h"
#include "../Components/Widgets.h"
#include "../Components/Icons.h"
#include "../../Plugin/PluginProcessor.h"

namespace nacar::ui
{
    /**
        Metrics shared by the four deep-edit pages.

        These describe where things sit, so by the letter of the contract they
        belong in Layout.h - but Layout.h is frozen and knows nothing about the
        pages, which are new surfaces cut into the centre column.  They live
        here instead, in one place, rather than being re-typed in four files.
        Per-page numbers stay at the top of each page's own .cpp.
    */
    namespace page
    {
        inline constexpr float pad            = 18.0f;   ///< page border inset
        inline constexpr float titleBlock     = 58.0f;   ///< height of the title block
        inline constexpr float titleSize      = 15.0f;
        inline constexpr float titleTrack     = 0.14f;
        inline constexpr float subSize        = 7.5f;
        inline constexpr float subTrack       = 0.20f;

        inline constexpr float captionH       = 24.0f;   ///< caption strip inside a section
        inline constexpr float captionSize    = 7.5f;
        inline constexpr float captionTrack   = 0.18f;
        inline constexpr float noteSize       = 7.0f;    ///< explanatory small print
        inline constexpr float noteTrack      = 0.16f;

        inline constexpr float knobLabelSize  = 8.0f;
        inline constexpr float knobLabelTrack = 0.16f;
        inline constexpr float knobBoxPad     = 20.0f;   ///< slack round the cap for its label

        inline constexpr float sectionPad     = 12.0f;   ///< inner padding of a section
        inline constexpr float rowH           = 24.0f;   ///< segmented / pill row
        inline constexpr float gap            = 10.0f;   ///< between sections
    }

    // =======================================================================
    //  ChoicePill
    //
    //  A choice parameter with more options than a segmented control can carry
    //  (LFO division has fourteen, grain pitch mode seven).  A juce::ComboBox
    //  would be drawn by LookAndFeel_V4 and would read as a settings dialog,
    //  and NacarLookAndFeel only restyles the popup - so this is a house pill
    //  that opens the house PopupMenu.  Documented choice, see report.
    // =======================================================================
    class ChoicePill : public PillButton
    {
    public:
        ChoicePill (const ParameterRegistry&, PID, PillButton::Style = PillButton::Style::ceramic);

        /** Pulls the label back from the parameter.  Cheap; called on the page tick. */
        void refresh();

        PID getParameterID() const noexcept { return pid; }

    private:
        void openMenu();

        const ParameterRegistry& params;
        PID pid;
        int shown = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoicePill)
    };

    // =======================================================================
    //  PageSurface
    //
    //  The shared vocabulary of MOD / FX / SEQ / MIX: one chrome, one title
    //  block, one section material, one way of declaring a knob.  The pages
    //  themselves then read as parameter layouts rather than as construction.
    // =======================================================================
    class PageSurface : public juce::Component,
                        private juce::Timer
    {
    public:
        /**  MOD and SEQ are about control, so they are machined ceramic like
             the macro panel and the mutate panel.  FX and MIX are about signal,
             so they are optical glass like the viewport and the FX chain.  The
             knob style follows the ground: ceramic caps on ceramic, dark caps
             on glass.  */
        enum class Material { ceramic, glass };

        PageSurface (NacarProcessor&, EditorHost&,
                     juce::String title, juce::String subtitle, Material);
        ~PageSurface() override;

        // -- chrome ---------------------------------------------------------

        /** Page ground plus the title block.  Call first from paint(). */
        void paintChrome (juce::Graphics&);

        /** Everything below the title block. */
        juce::Rectangle<int> contentArea() const;

        /** A labelled sub-panel in the page's material. */
        void section (juce::Graphics&, juce::Rectangle<float>, juce::StringRef caption,
                      std::optional<icons::Icon> icon = {}) const;

        /** The usable interior of a section: below its caption, inset. */
        static juce::Rectangle<int> inside (juce::Rectangle<int> sectionBounds);

        /** A secondary caption inside a section, for a sub-group of controls. */
        void groupCaption (juce::Graphics&, juce::Rectangle<float> row, juce::StringRef) const;

        /** Tracked small-caps type in the page's material. */
        void text (juce::Graphics&, juce::StringRef, juce::Point<float> baseline,
                   float sizePx, float trackingEm, juce::Colour,
                   juce::Justification = juce::Justification::centredLeft,
                   float width = 0.0f) const;

        // -- declarative controls -------------------------------------------

        NacarKnob&        addKnob (PID, juce::String label, bool bipolar = false);
        SegmentedControl& addSegmented (PID, juce::StringArray shortLabels = {});
        ChoicePill&       addChoicePill (PID);
        PowerButton&      addPower (PID, PowerButton::Tint = PowerButton::Tint::mint);
        ToggleSwitch&     addToggle (PID);

        /** A switch with its own caption.  Ceramic pages only: PreserveLock
            draws its caption in ceramic ink, which would vanish on glass. */
        PreserveLock&     addLock (juce::String caption, PID);

        // -- layout ---------------------------------------------------------

        /** Rewinds the knob cursor.  Call at the top of resized(). */
        void beginLayout() noexcept { gridCursor = 0; }

        /** Lays out the knobs added since the last grid call.  `count` limits
            the run to the next `count` knobs; -1 takes every knob still
            unplaced, which is what a page with a single grid wants. */
        void knobGrid (juce::Rectangle<int> area, int columns, float knobRadius,
                       int count = -1);

        /** Places one knob by hand, without touching the cursor. */
        static void placeKnob (NacarKnob&, juce::Rectangle<int> cell, float knobRadius);

        /** The box a cap of this radius needs.  Taller than wide: the cap hangs
            from the top and the label sits underneath it, inside the bounds. */
        static int knobBoxWidth (float knobRadius) noexcept;
        static int knobBoxHeight (float knobRadius) noexcept;

        /** The groove NacarKnob leaves around its cap, which the box must
            contain.  Mirrors knobSeatAllowance in Widgets.cpp. */
        static constexpr float knobSeatAllowance = 7.0f;

        /** Advances the cursor past knobs placed by hand. */
        void skipKnobs (int count) noexcept { gridCursor += count; }

        // -- parameter target menus -----------------------------------------
        //  The MOD matrix and the SEQ lanes both need to point at "some
        //  parameter", so the menu is built once, here, from the parameter
        //  table itself - it can never drift from the parameters that exist.

        /** Builds a grouped menu of every float parameter.  Item 1 is
            NO TARGET; every other item id is (int) pid + 2. */
        static void buildParameterMenu (juce::PopupMenu&, const juce::String& currentParameterId);

        /** Turns a menu result into a stored target: "" for no target,
            otherwise the parameter's permanent string ID. */
        static juce::String parameterMenuResult (int menuItemId);

        /** Display name for a stored target ID. */
        static juce::String parameterDisplayName (const juce::String& parameterId);

        // -- palette --------------------------------------------------------
        juce::Colour ink()      const noexcept;   ///< primary type
        juce::Colour inkSoft()  const noexcept;   ///< secondary type
        juce::Colour inkFaint() const noexcept;   ///< units, small print
        juce::Colour wellFill() const noexcept;   ///< recessed sub-surface fill
        NacarKnob::Style knobStyle() const noexcept;

    protected:
        /** Called on the page clock while the page is visible. */
        virtual void tick() {}
        void setTickHz (int hz) noexcept { tickHz = hz; }

        NacarProcessor&          processor;
        EditorHost&              host;
        const ParameterRegistry& params;
        const Material           material;

        juce::OwnedArray<NacarKnob> knobs;

    private:
        void timerCallback() override;
        void visibilityChanged() override;

        juce::String title, subtitle;
        int gridCursor = 0;
        int tickHz = 8;

        juce::OwnedArray<juce::Component> owned;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachments;
        juce::Array<ChoicePill*> choicePills;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageSurface)
    };

    // -----------------------------------------------------------------------
    //  Small shared drawing helpers the pages use for their previews.
    // -----------------------------------------------------------------------

    /** A recessed glass well - the ground for a preview, grid or meter. */
    void previewWell (juce::Graphics&, juce::Rectangle<float>);

    /** Fills under a path at 12 % violet and strokes the top at 1 px violet. */
    void violetTrace (juce::Graphics&, const juce::Path& fill, const juce::Path& line);

    /** Deterministic 0..1 pseudo-random, seeded from a constant so previews
        never flicker between repaints. */
    float stableRandom (int index) noexcept;
}
