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

        IconButton pencilButton { icons::pencil, IconButton::Style::plain };
        PillButton snapPill     { "SNAP", PillButton::Style::glass };

        std::array<std::unique_ptr<IconButton>, 4> toolButtons;

        IconButton playButton    { icons::play,         IconButton::Style::glassRound };
        IconButton stopButton    { icons::stop,         IconButton::Style::glassRound };
        IconButton resetButton   { icons::returnToZero, IconButton::Style::plain };
        IconButton loopButton    { icons::loop,         IconButton::Style::plain };
        IconButton trimButton    { icons::trim,         IconButton::Style::plain };
        IconButton shuffleButton { icons::shuffle,      IconButton::Style::plain };
        IconButton zoomOutButton { icons::zoomOut,      IconButton::Style::plain };
        IconButton zoomInButton  { icons::zoomIn,       IconButton::Style::plain };

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
