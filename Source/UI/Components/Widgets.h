#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Theme.h"
#include "../Layout.h"
#include "Icons.h"
#include "../../Plugin/ParameterRegistry.h"

namespace nacar::ui
{
    class ParamAttachment;

    // =======================================================================
    //  ParamControl - the behaviour every parameter-bound widget shares
    //
    //  Drag / shift-fine / double-click-reset / ctrl-type / right-click menu /
    //  wheel, plus the host gesture protocol and the tooltip text.  Widgets
    //  inherit this and only implement paint().
    // =======================================================================
    class ParamControl : public juce::Component,
                         public juce::SettableTooltipClient,
                         private juce::Timer
    {
    public:
        ParamControl (const ParameterRegistry&, PID);
        ~ParamControl() override;

        PID getParameterID() const noexcept { return pid; }

        /** 0..1 position of the control. */
        float getNormalised() const noexcept { return displayValue; }

        /** Real-unit value. */
        float getRealValue() const noexcept;

        /** Bipolar controls draw their arc outward from 12 o'clock. */
        void setBipolar (bool shouldBeBipolar) noexcept { bipolar = shouldBeBipolar; }
        bool isBipolar() const noexcept { return bipolar; }

        /** Extra sensitivity multiplier for small controls. */
        void setDragSensitivity (float pixelsForFullRange) noexcept;

        std::function<void()> onValueChange;

        // -- juce::Component --------------------------------------------------
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    protected:
        const ParameterRegistry& params;
        PID pid;

        bool  isHovered = false;
        bool  isDragging = false;
        float displayValue = 0.0f;      ///< smoothed 0..1, what paint() should use

        /** Refreshes the tooltip from the current value. */
        void refreshTooltip();

    private:
        void timerCallback() override;
        void showContextMenu();
        void showValueEntry();
        void setNormalisedFromDrag (float);

        std::unique_ptr<ParamAttachment> attachment;

        float dragStartValue = 0.0f;
        float pixelsForFullRange = 260.0f;
        float targetValue = 0.0f;
        bool  bipolar = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamControl)
    };

    // =======================================================================
    //  NacarKnob
    //
    //  The machined ceramic knob from the reference: contact shadow, seat
    //  groove carrying the violet value arc, machined cap, single indicator
    //  line.  RANDOM uses the dark variant.
    // =======================================================================
    class NacarKnob : public ParamControl
    {
    public:
        enum class Style { ceramic, dark };

        NacarKnob (const ParameterRegistry&, PID, Style = Style::ceramic);

        void setLabel (juce::String text, float sizePx, float trackingEm);
        void setScaleLegend (juce::String left, juce::String right);
        void setStyle (Style s) { style = s; repaint(); }

        /** Radius of the cap, in this component's coordinates. */
        float capRadius() const noexcept;

        void paint (juce::Graphics&) override;

    private:
        Style style;
        juce::String label, legendLeft, legendRight;
        float labelSize = layout::macro::knobLabelSize;
        float labelTrack = layout::macro::knobLabelTrack;
    };

    // =======================================================================
    //  Buttons
    // =======================================================================

    /** A ceramic or glass pill.  The workhorse of the interface. */
    class PillButton : public juce::Button
    {
    public:
        enum class Style { ceramic, glass, violet, violetOutline, dashed };

        PillButton (juce::String text, Style = Style::ceramic);

        void setIcon (icons::Icon, float sizeRatio = 0.5f);
        void setTrailingIcon (icons::Icon);
        void setStyle (Style s) { style = s; repaint(); }
        void setTextSize (float px, float trackingEm);
        void setCornerRadius (float r) { corner = r; repaint(); }
        void setSelected (bool shouldBeSelected);
        bool isSelected() const noexcept { return selected; }

        /** Width this pill wants, given its label, icons and padding. */
        float preferredWidth (float horizontalPadding) const;

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        Style style;
        bool selected = false;
        float textSize = 8.5f;
        float tracking = 0.14f;
        float corner = layout::radiusPill;
        std::optional<icons::Icon> leadingIcon, trailingIcon;
        float iconRatio = 0.5f;
    };

    /** A bare icon in a circular or square hit area. */
    class IconButton : public juce::Button
    {
    public:
        enum class Style { plain, glassRound, ceramicRound, glassSquare, violetSquare };

        IconButton (icons::Icon, Style = Style::plain);

        void setIcon (icons::Icon i) { icon = i; repaint(); }
        void setStyle (Style s) { style = s; repaint(); }
        void setColours (juce::Colour normal, juce::Colour active);
        void setActive (bool shouldBeActive);
        bool isActive() const noexcept { return active; }
        void setIconRatio (float r) { ratio = r; repaint(); }

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        icons::Icon icon;
        Style style;
        bool active = false;
        float ratio = 0.46f;
        juce::Colour normalColour { theme::inkMuted };
        juce::Colour activeColour { theme::violet };
    };

    /** The mint power ring on every FX card and atmosphere module. */
    class PowerButton : public juce::Button
    {
    public:
        enum class Tint { mint, violet };

        explicit PowerButton (Tint = Tint::mint);

        void setTint (Tint t) { tint = t; repaint(); }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        Tint tint;
    };

    // =======================================================================
    //  Selectors
    // =======================================================================

    /** SAFE | COLOR | FREE, NEAR | FAR | UNKNOWN, SUB | BODY | AIR. */
    class SegmentedControl : public juce::Component,
                             public juce::SettableTooltipClient
    {
    public:
        enum class Style { violetFill, darkFill, recessed };

        SegmentedControl (juce::StringArray options, Style = Style::violetFill);

        void setSelectedIndex (int, juce::NotificationType = juce::sendNotification);
        int getSelectedIndex() const noexcept { return selectedIndex; }

        void setTextSize (float px, float trackingEm);
        void bindTo (const ParameterRegistry&, PID);

        std::function<void (int)> onSelect;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        int indexAt (juce::Point<float>) const;
        juce::Rectangle<float> segmentBounds (int) const;

        juce::StringArray options;
        Style style;
        int selectedIndex = 0;
        int hoverIndex = -1;
        float textSize = 8.5f;
        float tracking = 0.14f;

        const ParameterRegistry* boundParams = nullptr;
        PID boundPid = PID::count;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentedControl)
    };

    /** The preserve-lock switch, and the SHADOW module's header toggle. */
    class ToggleSwitch : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        enum class Size { small, large };

        explicit ToggleSwitch (Size = Size::small);

        void setToggleState (bool, juce::NotificationType = juce::sendNotification);
        bool getToggleState() const noexcept { return state; }
        void bindTo (const ParameterRegistry&, PID);

        std::function<void (bool)> onToggle;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        Size size;
        bool state = false;
        bool hovered = false;
        float animated = 0.0f;

        const ParameterRegistry* boundParams = nullptr;
        PID boundPid = PID::count;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleSwitch)
    };

    /** A labelled preserve lock: switch plus its caption, as one hit target. */
    class PreserveLock : public juce::Component
    {
    public:
        PreserveLock (juce::String caption, const ParameterRegistry&, PID);

        float preferredWidth() const;
        void resized() override;
        void paint (juce::Graphics&) override;

        ToggleSwitch& getSwitch() noexcept { return toggle; }

    private:
        juce::String caption;
        ToggleSwitch toggle;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreserveLock)
    };

    /** The four Roman numerals down the side of the MEMORY knob. */
    class GenerationSelector : public juce::Component,
                               public juce::SettableTooltipClient
    {
    public:
        GenerationSelector (const ParameterRegistry&, PID);

        void setSelected (int index, juce::NotificationType = juce::sendNotification);
        int getSelected() const noexcept { return selected; }

        std::function<void (int)> onSelect;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> rowBounds (int) const;

        const ParameterRegistry& params;
        PID pid;
        int selected = 0;
        int hover = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenerationSelector)
    };

    /** The zoom slider in the transport row: a hairline track and a small cap. */
    class HairlineSlider : public juce::Component
    {
    public:
        HairlineSlider();

        void setValue (float normalised, juce::NotificationType = juce::sendNotification);
        float getValue() const noexcept { return value; }

        std::function<void (float)> onValueChange;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

    private:
        void setFromMouse (const juce::MouseEvent&);
        float value = 0.5f;
    };

    // =======================================================================
    //  Panels
    // =======================================================================

    /** A raised ceramic region of the chassis. */
    class CeramicPanel : public juce::Component
    {
    public:
        explicit CeramicPanel (float cornerRadius = layout::radiusPanel);
        void paint (juce::Graphics&) override;

        void setCornerRadius (float r) { corner = r; repaint(); }

    private:
        float corner;
    };

    /** A recessed optical-glass region of the chassis. */
    class GlassPanel : public juce::Component
    {
    public:
        explicit GlassPanel (float cornerRadius = layout::radiusPanel);
        void paint (juce::Graphics&) override;

        void setCornerRadius (float r) { corner = r; repaint(); }
        void setFill (juce::Colour c) { fill = c; repaint(); }

    private:
        float corner;
        juce::Colour fill { theme::glassDeep };
    };

    // =======================================================================
    //  Helpers
    // =======================================================================

    /** Draws a small-caps tracked label on ceramic. */
    void ceramicLabel (juce::Graphics&, juce::StringRef, juce::Point<float> baselineOrigin,
                       float sizePx, float trackingEm, juce::Colour = theme::inkMuted,
                       juce::Justification = juce::Justification::centredLeft,
                       float width = 0.0f);

    /** Draws a small-caps tracked label on glass. */
    void glassLabel (juce::Graphics&, juce::StringRef, juce::Point<float> baselineOrigin,
                     float sizePx, float trackingEm, juce::Colour = theme::glassInkMuted,
                     juce::Justification = juce::Justification::centredLeft,
                     float width = 0.0f);

    /** The scale legend under CHARACTER / MOTION / WORLD: two ends and a rule. */
    void scaleLegend (juce::Graphics&, juce::StringRef left, juce::StringRef right,
                      juce::Rectangle<float> area);
}
