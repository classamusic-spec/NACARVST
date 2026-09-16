#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <utility>
#include <vector>

#include "../Theme.h"
#include "../Layout.h"
#include "../../Plugin/PluginProcessor.h"
#include "SynthVisualizer.h"

namespace nacar::ui
{
    /**
        The waveform field - the centrepiece of the optical viewport.

        Draws an audio overview in NACAR's violet language: a filled envelope
        mirrored about the vertical centre, a brighter crest along the top and
        bottom edges, a faint centre rule, a translucent selection with circular
        handles, and a near-white playhead.  UI spec section 5.

        RENDERING MODEL
        ---------------
        Two envelopes are kept:

          sourceEnvelope   a fixed-resolution min/max reduction of whatever the
                           source is.  Built once, when the source changes.
          displayEnvelope  exactly one min/max pair per pixel column of this
                           component, windowed out of sourceEnvelope.  Rebuilt
                           on resize, zoom and scroll - never per frame, and
                           never from raw samples.

        paint() only ever walks displayEnvelope and turns it into a juce::Path.

        SOURCES
        -------
        Three things can fill sourceEnvelope:

          setThumbnailSource()   real decoded audio.  Complete and working, but
                                 nothing calls it in V1 Phase 2 because there is
                                 no sample engine yet to decode with.
          setSynthSource()       a SynthVisualizer, polled through
                                 refreshFromSynth().  This is what the viewport
                                 shows while no sample is loaded, and it is
                                 labelled on screen as the synth's live output.
          nothing                the quiet "DROP AUDIO" invitation.

        A dropped file writes its path into the SAMPLE tree and stops there - see
        filesDropped().  No waveform is invented for a file that has not been
        read.
    */
    class WaveformView : public juce::Component,
                         public juce::FileDragAndDropTarget
    {
    public:
        /** What the field is currently showing, and therefore what it is
            allowed to claim on screen. */
        enum class SourceKind
        {
            none,           ///< nothing at all: "DROP AUDIO"
            synthMonitor,   ///< the live synth output level, labelled as such
            pendingSample,  ///< a file path is known, its audio is not
            sample          ///< real decoded audio handed to setThumbnailSource
        };

        WaveformView (NacarProcessor&, bool isOverview = false);
        ~WaveformView() override;

        // -- source ---------------------------------------------------------

        /** Reduces real decoded audio into the source envelope. */
        void setThumbnailSource (const float* const* channels, int numChannels, int numSamples);

        /** Drops the envelope and returns to the empty state. */
        void clearSource();

        /** Attaches the live synth monitor.  Not owned. */
        void setSynthSource (const SynthVisualizer*);

        /** Pulls one frame of the synth monitor into the source envelope. */
        void refreshFromSynth();

        void setSourceKind (SourceKind);
        SourceKind getSourceKind() const noexcept { return kind; }

        // -- view -----------------------------------------------------------

        /** 0 = whole source, 1 = maximum magnification. */
        void setZoom (float normalised, juce::NotificationType = juce::dontSendNotification);
        float getZoom() const noexcept { return zoomNormalised; }

        /** Left edge of the visible window, normalised over the whole source. */
        void setScroll (double normalisedStart);
        double getScroll() const noexcept { return windowStart; }
        double getVisibleSpan() const noexcept { return windowSpan; }

        void setPlayhead (double normalisedPosition);
        double getPlayhead() const noexcept { return playhead; }

        void setSelection (double start, double end,
                           juce::NotificationType = juce::dontSendNotification);
        double getSelectionStart() const noexcept { return selectionStart; }
        double getSelectionEnd() const noexcept { return selectionEnd; }
        bool hasSelection() const noexcept { return selectionEnd > selectionStart; }

        void setShowMarkers (bool);
        bool getShowMarkers() const noexcept { return showMarkers; }

        void setSnapToTransients (bool);
        bool getSnapToTransients() const noexcept { return snapToTransients; }

        /** Normalised transient positions, from the ANALYSIS tree. */
        void setTransients (const juce::Array<double>&);

        /** Overview mode only: the slice of the source the main field shows. */
        void setVisibleWindow (double start, double end);

        /** The badge drawn in the top-left corner of the field. */
        void setSourceLabel (juce::String);

        // -- callbacks ------------------------------------------------------
        std::function<void (double)>          onPlayheadMoved;
        std::function<void (double, double)>  onSelectionChanged;
        std::function<void (float)>           onZoomChanged;
        std::function<void (double)>          onScrolled;
        std::function<void (juce::File)>      onFileDropped;

        // -- juce::Component ------------------------------------------------
        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

        // -- juce::FileDragAndDropTarget ------------------------------------
        //
        //  NOTE: the JUCE 8 entry point is isInterestedInFileDrag(), not
        //  isInterestedInFileDragAndDrop().  Same contract, different spelling.
        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int x, int y) override;
        void fileDragMove (const juce::StringArray&, int x, int y) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray&, int x, int y) override;

        /** The extensions the field accepts on a drop. */
        static bool isSupportedAudioFile (const juce::String& path);

    private:
        enum class DragMode { none, newSelection, moveSelection, dragStart, dragEnd };

        void rebuildDisplayEnvelope();
        void rebuildSourceFromSynth();

        float  xForPosition (double normalised) const;
        double positionForX (float x) const;
        double snapped (double normalised) const;
        void   clampWindow();
        void   applyZoomAroundPosition (float newZoomNormalised, double anchorPosition,
                                        float anchorX);

        juce::Rectangle<float> fieldArea() const;
        juce::Path buildEnvelopePath (juce::Rectangle<float> area, juce::Path& crestTop,
                                      juce::Path& crestBottom) const;

        void paintEmptyInvitation (juce::Graphics&, juce::Rectangle<float> area,
                                   bool monitoringSynth) const;
        void paintPendingSample (juce::Graphics&, juce::Rectangle<float> area) const;
        void paintSelection (juce::Graphics&, juce::Rectangle<float> area) const;
        void paintMarkers (juce::Graphics&, juce::Rectangle<float> area) const;
        void paintPlayhead (juce::Graphics&, juce::Rectangle<float> area) const;
        void paintVisibleWindow (juce::Graphics&, juce::Rectangle<float> area) const;
        void paintDropOverlay (juce::Graphics&, juce::Rectangle<float> area) const;
        void paintSourceBadge (juce::Graphics&, juce::Rectangle<float> area) const;

        NacarProcessor& processor;
        const bool overview;

        const SynthVisualizer* synthSource = nullptr;

        std::vector<std::pair<float, float>> sourceEnvelope;
        std::vector<std::pair<float, float>> displayEnvelope;

        SourceKind kind = SourceKind::none;
        juce::String sourceLabel;

        float  zoomNormalised = 0.0f;
        double windowStart    = 0.0;
        double windowSpan     = 1.0;

        double playhead       = 0.0;
        double selectionStart = 0.0;
        double selectionEnd   = 0.0;

        bool showMarkers      = false;
        bool snapToTransients = false;
        juce::Array<double> transients;

        double visibleWindowStart = 0.0;
        double visibleWindowEnd   = 1.0;

        // -- interaction ----------------------------------------------------
        DragMode dragMode = DragMode::none;
        double   dragAnchor = 0.0;          ///< fixed edge, or grab offset
        double   dragOriginStart = 0.0;
        double   dragOriginEnd = 0.0;
        float    dragOriginX = 0.0f;
        bool     dragHasMoved = false;
        int      hoveredHandle = -1;        ///< 0 = start, 1 = end, -1 = none

        bool dragOver = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
    };
}
