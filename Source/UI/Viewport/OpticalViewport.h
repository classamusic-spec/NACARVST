#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

#include "../Theme.h"
#include "../Layout.h"
#include "../EditorHost.h"
#include "../Components/Widgets.h"
#include "../../Plugin/PluginProcessor.h"
#include "WaveformView.h"
#include "SynthVisualizer.h"

namespace nacar::ui
{
    /**
        A control inside the optical viewport, drawn as an object.

        IconButton's round and square styles lay their body down with
        theme::glassSurface, which is the CUT-OUT routine: an inner shadow under
        the top edge and no drop shadow.  That is right for a hole and wrong for
        a control that is meant to sit proud of the glass, and it is why the
        transport row currently reads as a set of glyphs printed on the panel.

        This subclass keeps the whole of IconButton's behaviour - hit area,
        tooltip, enabled and active flags, the juce::Button contract - and
        replaces only the body, with theme::raisedGlass, so the viewport's
        controls are lit from the same upper-left source as the rest of the
        instrument.  Widgets.h belongs to another part of the build and is not
        edited from here; subclassing is how the viewport states its own case
        without reaching into it.

        IconButton::setColours writes members this class cannot see, so the
        glyph colours are kept again here.
    */
    class ViewportButton : public IconButton
    {
    public:
        enum class Body
        {
            disc,    ///< play: a raised glass disc
            square,  ///< stop, and the four view-mode buttons
            glyph    ///< a bare glyph at rest, which grows a body under the pointer
        };

        ViewportButton (icons::Icon, Body, theme::Elevation = theme::Elevation::resting);

        void setGlyphColours (juce::Colour rest, juce::Colour lit);
        void setGlyphRatio (float) noexcept;

        /** Corner radius for Body::square.  Left unset, the button takes the
            radius the view-mode buttons use; stop is larger than those and sets
            its own, so the two read as the same corner at different sizes. */
        void setCornerRadius (float) noexcept;

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    protected:
        void buttonStateChanged() override;

    private:
        icons::Icon glyph;
        Body body;
        theme::Elevation elevation;

        float glyphRatio = 0.46f;
        float corner = 0.0f;              ///< 0 = the default for this body
        juce::Colour restColour { theme::glassInkMuted };
        juce::Colour litColour  { theme::violet };

        /** Hover and press as quantities rather than as booleans, on the same
            shared ticker every other control in the instrument uses - so the
            transport row travels rather than switching, like its neighbours. */
        detail::Motion hoverAnim { *this, 0.30f };
        detail::Motion pressAnim { *this, 0.55f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViewportButton)
    };

    /**
        The optical viewport - UI spec section 5.

        A recessed optical-glass panel cut into the chassis at layout::viewport,
        carrying the source header, the waveform field, the overview strip and
        the transport row.  Everything inside it is positioned from
        layout::vp, which is already region-local.

        Structure:

            header      accent bar, title, meta line, rename pencil,
                        SNAP pill, four view-mode buttons
            field       WaveformView at vp::waveField
            overview    a second WaveformView, constructed with isOverview = true,
                        at vp::overviewStrip.  Two instances of the same class
                        rather than a painted sub-section: it keeps one envelope
                        pipeline, one set of gestures and one paint routine, and
                        it lets the strip be scrubbed to pan the main field.
            transport   play / stop / reset / loop / trim / shuffle, the time
                        readout, and the ZOOM label with its hairline slider

        With no sample loaded the field shows the instrument's own output level
        over time, via SynthVisualizer, badged on screen as the synth's live
        output.  It is never presented as a loaded sample.
    */
    class OpticalViewport : public juce::Component,
                            private juce::ValueTree::Listener,
                            private juce::Timer
    {
    public:
        OpticalViewport (NacarProcessor&, EditorHost&);
        ~OpticalViewport() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** The four view-mode buttons, in the order the reference shows them. */
        enum class ViewMode { waveform = 0, list, markers, expand };

        void setViewMode (ViewMode);
        ViewMode getViewMode() const noexcept { return viewMode; }

        WaveformView& getWaveformView() noexcept { return waveField; }

    private:
        // -- state ----------------------------------------------------------
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
        void valueTreeRedirected (juce::ValueTree&) override;

        void timerCallback() override;

        void refreshFromState();
        void refreshHeaderText();
        void syncFromParameters();
        void syncOverviewWindow();
        void applyViewMode();

        juce::ValueTree sampleTree() const;
        void writeSampleProperty (const juce::Identifier&, const juce::var&);

        bool   hasSample() const;
        double sampleLengthSeconds() const;
        juce::Array<double> readTransients() const;

        // -- transport ------------------------------------------------------
        void setPlaying (bool);
        void advanceTransport();
        void movePlayhead (double normalised);

        // -- rename ---------------------------------------------------------
        void startRename();
        void commitRename (bool keepChanges);

        // -- painting -------------------------------------------------------
        void paintHeader (juce::Graphics&);
        void paintTransportRow (juce::Graphics&);
        void paintListPlaceholder (juce::Graphics&);

        float titleWidth() const;
        float metaWidth() const;
        juce::Rectangle<float> titleRect() const;

        // -- wiring ---------------------------------------------------------
        void buildButtons();
        void wireCallbacks();

        NacarProcessor& processor;
        EditorHost& host;

        SynthVisualizer visualizer;

        WaveformView waveField    { processor, false };
        WaveformView overviewView { processor, true  };

        IconButton pencilButton { icons::Icon::pencil, IconButton::Style::plain };
        PillButton snapPill     { "SNAP", PillButton::Style::glass };

        std::array<std::unique_ptr<ViewportButton>, 4> toolButtons;

        // Play is the most-looked-at control in the panel and is the one thing
        // in the row raised a full step off the glass.  Stop is a rounded
        // square beside it - the reference pairs a circle with a square here,
        // and the square rhymes with the four view-mode buttons above.  The
        // rest are glyphs until the pointer is on them.
        ViewportButton playButton    { icons::Icon::play,   ViewportButton::Body::disc,
                                       theme::Elevation::raised };
        ViewportButton stopButton    { icons::Icon::stop,   ViewportButton::Body::square,
                                       theme::Elevation::resting };
        ViewportButton resetButton   { icons::Icon::returnToZero, ViewportButton::Body::glyph };
        ViewportButton loopButton    { icons::Icon::loop,    ViewportButton::Body::glyph };
        ViewportButton trimButton    { icons::Icon::trim,    ViewportButton::Body::glyph };
        ViewportButton shuffleButton { icons::Icon::shuffle, ViewportButton::Body::glyph };
        ViewportButton zoomOutButton { icons::Icon::zoomOut, ViewportButton::Body::glyph };
        ViewportButton zoomInButton  { icons::Icon::zoomIn,  ViewportButton::Body::glyph };

        HairlineSlider zoomSlider;
        juce::TextEditor renameEditor;

        juce::String titleText, metaText;

        ViewMode viewMode = ViewMode::waveform;

        bool   playing = false;
        double lastTransportTime = 0.0;

        bool writingState = false;

        int lastSourceMode = -1;
        int lastCharacter  = -1;

        juce::Random shuffleRandom;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpticalViewport)
    };
}
