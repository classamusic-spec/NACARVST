#include "WaveformView.h"

#include <cmath>
#include <utility>

namespace nacar::ui
{
    // =======================================================================
    //  Numbers that are not in Layout.h
    //
    //  Layout.h owns *where things are*.  Everything below describes how the
    //  field renders or how it responds to a gesture, which Layout.h does not
    //  carry and which the frozen header cannot be extended with.  Anything
    //  here that has a counterpart in the reference image cites it.
    // =======================================================================

    /** Resolution of the source-side min/max reduction for decoded audio.
        4096 buckets is finer than the 840 px field at any zoom below about 5x
        and is cheap to hold (32 KB), so the display rebuild never has to touch
        raw samples again. */
    static constexpr int sourceBuckets = 4096;

    /** Maximum magnification of the visible window.  UI spec section 5 shows a
        zoom slider but does not state its range; 64x puts a 2-minute file at
        roughly two seconds across the field at full zoom. */
    static constexpr float maxZoomFactor = 64.0f;

    static constexpr float zoomWheelStep    = 0.06f;   ///< per wheel notch
    static constexpr float magnifyGain      = 0.60f;   ///< per unit of pinch scale
    static constexpr float wheelPanFraction = 0.90f;   ///< of a window, per notch

    /** Vertical breathing room left around the envelope.  On the main field the
        8 px keeps the crest clear of the selection handles, which the reference
        places 8 px below the field's top edge. */
    static constexpr float envelopeMargin         = 8.0f;
    static constexpr float overviewEnvelopeMargin = 2.0f;

    /** UI spec section 5: the selection handle is a 5 px filled circle at y 194,
        with the waveform field top at y 186. */
    static constexpr float selectionHandleY      = 8.0f;
    static constexpr float selectionHandleRadius = 2.5f;

    /** Pointer tolerances.  A 5 px dot is too small to grab, so the hit area is
        wider than the mark. */
    static constexpr float handleGrabPixels    = 7.0f;
    static constexpr float clickThresholdPixels = 3.0f;
    static constexpr float snapPixels          = 6.0f;

    /** UI spec section 10: shift is the fine gesture, divided by six. */
    static constexpr float fineDragDivisor = 6.0f;

    /** Transient ticks sit on the top edge of the field. */
    static constexpr float markerTickHeight = 6.0f;

    /** Alphas for the envelope.  UI spec section 5 asks for ~70 % on the main
        field and "low contrast" on the overview strip. */
    static constexpr float mainFillAlpha      = 0.70f;
    static constexpr float overviewFillAlpha  = 0.34f;
    static constexpr float selectionFillAlpha = 0.14f;

    /** Type used inside the field.  Layout.h carries no size for these because
        they are states of the field rather than fixed furniture. */
    static constexpr float dropTextSize  = 9.5f;
    static constexpr float dropTextTrack = 0.30f;
    static constexpr float noteTextSize  = 7.5f;
    static constexpr float noteTextTrack = 0.20f;
    static constexpr float badgeTextSize = 7.0f;
    static constexpr float badgeTrack    = 0.22f;
    static constexpr float badgeInset    = 9.0f;

    /** Layout.h has no corner radius below radiusPill (8).  The overview strip
        is only 20 px tall, so 8 would round it into a lozenge. */
    static constexpr float overviewCorner = 4.0f;

    // =======================================================================
    //  Construction
    // =======================================================================
    WaveformView::WaveformView (NacarProcessor& p, bool isOverview)
        : processor (p), overview (isOverview)
    {
        setOpaque (false);
        setInterceptsMouseClicks (true, false);

        if (! overview)
            setWantsKeyboardFocus (false);
    }

    WaveformView::~WaveformView() = default;

    // =======================================================================
    //  Source
    // =======================================================================
    void WaveformView::setThumbnailSource (const float* const* channels,
                                           int numChannels, int numSamples)
    {
        if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
        {
            clearSource();
            return;
        }

        const int buckets = juce::jlimit (1, sourceBuckets, numSamples);
        sourceEnvelope.assign ((size_t) buckets, { 0.0f, 0.0f });

        const double samplesPerBucket = (double) numSamples / (double) buckets;

        for (int b = 0; b < buckets; ++b)
        {
            const int i0 = juce::jlimit (0, numSamples - 1,
                                         (int) std::floor ((double) b * samplesPerBucket));
            const int i1 = juce::jlimit (i0 + 1, numSamples,
                                         (int) std::ceil ((double) (b + 1) * samplesPerBucket));

            float mn = 0.0f, mx = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* data = channels[ch];

                if (data == nullptr)
                    continue;

                for (int i = i0; i < i1; ++i)
                {
                    mn = juce::jmin (mn, data[i]);
                    mx = juce::jmax (mx, data[i]);
                }
            }

            sourceEnvelope[(size_t) b] = { mn, mx };
        }

        kind = SourceKind::sample;
        rebuildDisplayEnvelope();
        repaint();
    }

    void WaveformView::clearSource()
    {
        sourceEnvelope.clear();
        displayEnvelope.clear();
        kind = SourceKind::none;
        repaint();
    }

    void WaveformView::setSynthSource (const SynthVisualizer* v)
    {
        synthSource = v;

        if (v != nullptr && kind == SourceKind::none)
            setSourceKind (SourceKind::synthMonitor);
    }

    void WaveformView::setSourceKind (SourceKind k)
    {
        if (kind == k)
            return;

        kind = k;

        if (kind != SourceKind::sample)
            sourceEnvelope.clear();

        if (kind == SourceKind::synthMonitor)
            rebuildSourceFromSynth();

        rebuildDisplayEnvelope();
        repaint();
    }

    void WaveformView::rebuildSourceFromSynth()
    {
        if (synthSource == nullptr)
            return;

        synthSource->fillEnvelope (sourceEnvelope, SynthVisualizer::historySize);
    }

    void WaveformView::refreshFromSynth()
    {
        if (synthSource == nullptr || kind != SourceKind::synthMonitor)
            return;

        rebuildSourceFromSynth();

        // The monitor always shows the newest slice of the ring, so zooming in
        // shortens the window from the left and the trace keeps scrolling.
        if (! overview)
        {
            windowStart = 1.0 - windowSpan;
            clampWindow();
        }

        rebuildDisplayEnvelope();
        repaint();
    }

    // =======================================================================
    //  Envelope
    // =======================================================================
    void WaveformView::rebuildDisplayEnvelope()
    {
        const int columns = juce::jmax (1, getWidth());
        const int n = (int) sourceEnvelope.size();

        if (n == 0)
        {
            displayEnvelope.clear();
            return;
        }

        if ((int) displayEnvelope.size() != columns)
            displayEnvelope.resize ((size_t) columns);

        const double s0   = overview ? 0.0 : windowStart;
        const double span = juce::jmax (1.0e-9, overview ? 1.0 : windowSpan);

        const double bucketsPerColumn = span * (double) n / (double) columns;

        for (int c = 0; c < columns; ++c)
        {
            const double a = (s0 + span * ((double) c       / (double) columns)) * (double) n;
            const double b = (s0 + span * ((double) (c + 1) / (double) columns)) * (double) n;

            float mn = 0.0f, mx = 0.0f;

            if (bucketsPerColumn >= 1.0)
            {
                const int i0 = juce::jlimit (0, n - 1, (int) std::floor (a));
                const int i1 = juce::jlimit (i0 + 1, n, (int) std::ceil (b));

                for (int i = i0; i < i1; ++i)
                {
                    mn = juce::jmin (mn, sourceEnvelope[(size_t) i].first);
                    mx = juce::jmax (mx, sourceEnvelope[(size_t) i].second);
                }
            }
            else
            {
                // Zoomed past the source resolution: interpolate between the two
                // neighbouring buckets rather than draw a staircase.
                const double centre = (a + b) * 0.5 - 0.5;
                const int    ia = juce::jlimit (0, n - 1, (int) std::floor (centre));
                const int    ib = juce::jlimit (0, n - 1, ia + 1);
                const float  t  = (float) juce::jlimit (0.0, 1.0, centre - std::floor (centre));

                mn = sourceEnvelope[(size_t) ia].first
                        + (sourceEnvelope[(size_t) ib].first - sourceEnvelope[(size_t) ia].first) * t;
                mx = sourceEnvelope[(size_t) ia].second
                        + (sourceEnvelope[(size_t) ib].second - sourceEnvelope[(size_t) ia].second) * t;
            }

            displayEnvelope[(size_t) c] = { mn, mx };
        }
    }

    juce::Path WaveformView::buildEnvelopePath (juce::Rectangle<float> area,
                                                juce::Path& crestTop,
                                                juce::Path& crestBottom) const
    {
        juce::Path body;

        const int n = (int) displayEnvelope.size();

        if (n < 2 || area.getWidth() <= 0.0f)
            return body;

        const float margin = overview ? overviewEnvelopeMargin : envelopeMargin;
        const float cy     = area.getCentreY();
        const float half   = juce::jmax (1.0f, area.getHeight() * 0.5f - margin);
        const float dx     = area.getWidth() / (float) (n - 1);

        body.preallocateSpace (n * 8);
        crestTop.preallocateSpace (n * 4);
        crestBottom.preallocateSpace (n * 4);

        // Top edge, left to right.
        for (int i = 0; i < n; ++i)
        {
            const float x = area.getX() + (float) i * dx;
            const float y = cy - juce::jlimit (-1.0f, 1.0f, displayEnvelope[(size_t) i].second) * half;

            if (i == 0)
            {
                body.startNewSubPath (x, y);
                crestTop.startNewSubPath (x, y);
            }
            else
            {
                body.lineTo (x, y);
                crestTop.lineTo (x, y);
            }
        }

        // Bottom edge, right to left: the same shape mirrored about the centre.
        for (int i = n - 1; i >= 0; --i)
        {
            const float x = area.getX() + (float) i * dx;
            const float y = cy - juce::jlimit (-1.0f, 1.0f, displayEnvelope[(size_t) i].first) * half;

            body.lineTo (x, y);

            if (i == n - 1)
                crestBottom.startNewSubPath (x, y);
            else
                crestBottom.lineTo (x, y);
        }

        body.closeSubPath();
        return body;
    }

    // =======================================================================
    //  View state
    // =======================================================================
    void WaveformView::setZoom (float normalised, juce::NotificationType notification)
    {
        normalised = juce::jlimit (0.0f, 1.0f, normalised);

        if (std::abs (normalised - zoomNormalised) < 1.0e-6f)
            return;

        if (overview)
        {
            zoomNormalised = normalised;
            return;
        }

        const double centre = windowStart + windowSpan * 0.5;
        const double anchor = (playhead >= windowStart && playhead <= windowStart + windowSpan)
                                ? playhead : centre;

        applyZoomAroundPosition (normalised, anchor, xForPosition (anchor));

        if (notification != juce::dontSendNotification && onZoomChanged != nullptr)
            onZoomChanged (zoomNormalised);
    }

    void WaveformView::applyZoomAroundPosition (float newZoom, double anchorPosition, float anchorX)
    {
        zoomNormalised = juce::jlimit (0.0f, 1.0f, newZoom);
        windowSpan = 1.0 / std::pow ((double) maxZoomFactor, (double) zoomNormalised);

        const auto area = fieldArea();
        const double fraction = area.getWidth() > 0.0f
                                  ? (double) ((anchorX - area.getX()) / area.getWidth())
                                  : 0.5;

        windowStart = anchorPosition - fraction * windowSpan;

        clampWindow();
        rebuildDisplayEnvelope();
        repaint();
    }

    void WaveformView::setScroll (double normalisedStart)
    {
        if (overview)
            return;

        const double before = windowStart;
        windowStart = normalisedStart;
        clampWindow();

        if (std::abs (before - windowStart) > 1.0e-9)
        {
            rebuildDisplayEnvelope();
            repaint();
        }
    }

    void WaveformView::clampWindow()
    {
        windowSpan  = juce::jlimit (1.0 / (double) maxZoomFactor, 1.0, windowSpan);
        windowStart = juce::jlimit (0.0, juce::jmax (0.0, 1.0 - windowSpan), windowStart);
    }

    void WaveformView::setPlayhead (double normalisedPosition)
    {
        normalisedPosition = juce::jlimit (0.0, 1.0, normalisedPosition);

        if (std::abs (normalisedPosition - playhead) < 1.0e-9)
            return;

        playhead = normalisedPosition;
        repaint();
    }

    void WaveformView::setSelection (double start, double end,
                                     juce::NotificationType notification)
    {
        start = juce::jlimit (0.0, 1.0, start);
        end   = juce::jlimit (0.0, 1.0, end);

        if (end < start)
            std::swap (start, end);

        const bool changed = std::abs (start - selectionStart) > 1.0e-9
                          || std::abs (end   - selectionEnd)   > 1.0e-9;

        selectionStart = start;
        selectionEnd   = end;

        if (changed)
            repaint();

        if (changed && notification != juce::dontSendNotification && onSelectionChanged != nullptr)
            onSelectionChanged (selectionStart, selectionEnd);
    }

    void WaveformView::setShowMarkers (bool shouldShow)
    {
        if (showMarkers == shouldShow)
            return;

        showMarkers = shouldShow;
        repaint();
    }

    void WaveformView::setSnapToTransients (bool shouldSnap)
    {
        snapToTransients = shouldSnap;
    }

    void WaveformView::setTransients (const juce::Array<double>& positions)
    {
        transients = positions;

        if (showMarkers)
            repaint();
    }

    void WaveformView::setVisibleWindow (double start, double end)
    {
        visibleWindowStart = juce::jlimit (0.0, 1.0, start);
        visibleWindowEnd   = juce::jlimit (0.0, 1.0, end);

        if (overview)
            repaint();
    }

    void WaveformView::setSourceLabel (juce::String text)
    {
        if (sourceLabel == text)
            return;

        sourceLabel = std::move (text);
        repaint();
    }

    // =======================================================================
    //  Coordinates
    // =======================================================================
    juce::Rectangle<float> WaveformView::fieldArea() const
    {
        return getLocalBounds().toFloat();
    }

    float WaveformView::xForPosition (double normalised) const
    {
        const auto area = fieldArea();
        const double s0   = overview ? 0.0 : windowStart;
        const double span = juce::jmax (1.0e-9, overview ? 1.0 : windowSpan);

        return area.getX() + (float) ((normalised - s0) / span) * area.getWidth();
    }

    double WaveformView::positionForX (float x) const
    {
        const auto area = fieldArea();

        if (area.getWidth() <= 0.0f)
            return 0.0;

        const double s0   = overview ? 0.0 : windowStart;
        const double span = overview ? 1.0 : windowSpan;

        return s0 + (double) ((x - area.getX()) / area.getWidth()) * span;
    }

    double WaveformView::snapped (double normalised) const
    {
        if (! snapToTransients || transients.isEmpty())
            return normalised;

        const auto area = fieldArea();

        if (area.getWidth() <= 0.0f)
            return normalised;

        const double span      = overview ? 1.0 : windowSpan;
        const double tolerance = (double) (snapPixels / area.getWidth()) * span;

        double best = normalised;
        double bestDistance = tolerance;

        for (auto t : transients)
        {
            const double d = std::abs (t - normalised);

            if (d <= bestDistance)
            {
                bestDistance = d;
                best = t;
            }
        }

        return best;
    }

    void WaveformView::resized()
    {
        rebuildDisplayEnvelope();
    }

    // =======================================================================
    //  Painting
    // =======================================================================
    void WaveformView::paint (juce::Graphics& g)
    {
        const auto area   = fieldArea();
        const float corner = overview ? overviewCorner : layout::radiusCard;

        // The well.  Glass is a cut-out, so this is fill plus inner shadow plus
        // hairline - never a drop shadow.
        theme::glassSurface (g, area, corner, overview ? theme::glassMid : theme::glassDeep);

        juce::Graphics::ScopedSaveState saved (g);
        {
            juce::Path clip;
            clip.addRoundedRectangle (area, corner);
            g.reduceClipRegion (clip);
        }

        // Faint centre rule.
        g.setColour (theme::glassEdge);
        g.fillRect (area.getX() + 1.0f, area.getCentreY() - 0.5f, area.getWidth() - 2.0f, 1.0f);

        // The invitation is a watermark for an empty field.  While the synth
        // monitor is actually showing something it is not an empty field, so
        // the invitation gives way to the waveform rather than sitting across
        // it - it comes back the moment the instrument falls silent.
        const bool monitorIdle = (kind == SourceKind::synthMonitor)
                                 && (synthSource == nullptr || ! synthSource->hasSignal());

        if (kind == SourceKind::none || monitorIdle)
            paintEmptyInvitation (g, area);

        if (kind == SourceKind::pendingSample)
        {
            paintPendingSample (g, area);
        }
        else if (kind != SourceKind::none && displayEnvelope.size() > 1)
        {
            juce::Path crestTop, crestBottom;
            const auto body = buildEnvelopePath (area, crestTop, crestBottom);

            g.setColour (theme::violet.withAlpha (overview ? overviewFillAlpha : mainFillAlpha));
            g.fillPath (body);

            g.setColour (theme::violetLight.withAlpha (overview ? 0.45f : 0.95f));
            g.strokePath (crestTop,    juce::PathStrokeType (1.0f));
            g.strokePath (crestBottom, juce::PathStrokeType (1.0f));
        }

        if (hasSelection())
            paintSelection (g, area);

        if (showMarkers && ! transients.isEmpty())
            paintMarkers (g, area);

        paintPlayhead (g, area);

        if (overview)
            paintVisibleWindow (g, area);

        if (! overview && sourceLabel.isNotEmpty())
            paintSourceBadge (g, area);

        if (dragOver)
            paintDropOverlay (g, area);
    }

    void WaveformView::paintEmptyInvitation (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        if (overview || area.getHeight() < dropTextSize * 3.0f)
            return;

        const auto f = theme::label (dropTextSize);

        g.setColour (theme::glassInkFaint);
        theme::drawTracked (g, "DROP AUDIO",
                            { area.getX(), area.getCentreY() - f.getHeight() * 0.5f,
                              area.getWidth(), f.getHeight() },
                            f, dropTextTrack, juce::Justification::horizontallyCentred);
    }

    void WaveformView::paintPendingSample (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        if (overview)
            return;

        // A file path is known and nothing has read the file.  Saying so is the
        // only honest thing to draw here - see filesDropped().
        const auto big   = theme::label (dropTextSize);
        const auto small = theme::label (noteTextSize);

        g.setColour (theme::glassInkMuted);
        theme::drawTracked (g, "AWAITING DECODE",
                            { area.getX(), area.getCentreY() - big.getHeight(),
                              area.getWidth(), big.getHeight() },
                            big, dropTextTrack, juce::Justification::horizontallyCentred);

        g.setColour (theme::glassInkFaint);
        theme::drawTracked (g, "NO SAMPLE ENGINE IN THIS BUILD",
                            { area.getX(), area.getCentreY() + small.getHeight() * 0.5f,
                              area.getWidth(), small.getHeight() },
                            small, noteTextTrack, juce::Justification::horizontallyCentred);
    }

    void WaveformView::paintSelection (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const float x0 = xForPosition (selectionStart);
        const float x1 = xForPosition (selectionEnd);

        if (x1 < area.getX() || x0 > area.getRight())
            return;

        g.setColour (theme::violet.withAlpha (selectionFillAlpha));
        g.fillRect (juce::Rectangle<float> (x0, area.getY(), juce::jmax (1.0f, x1 - x0),
                                            area.getHeight()));

        g.setColour (theme::violet);
        g.fillRect (x0 - 0.5f, area.getY(), 1.0f, area.getHeight());
        g.fillRect (x1 - 0.5f, area.getY(), 1.0f, area.getHeight());

        if (overview)
            return;

        // Circular grab handles at the top of each edge.
        const float hy = area.getY() + selectionHandleY;

        for (int edge = 0; edge < 2; ++edge)
        {
            const float hx = (edge == 0 ? x0 : x1);
            const float r  = selectionHandleRadius + (hoveredHandle == edge ? 1.0f : 0.0f);

            g.setColour (hoveredHandle == edge ? theme::violetLight : theme::violet);
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f)
                               .withCentre ({ hx, hy }));
        }
    }

    void WaveformView::paintMarkers (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const float h = juce::jmin (markerTickHeight, area.getHeight() * 0.25f);

        g.setColour (theme::violet.withAlpha (0.75f));

        for (auto t : transients)
        {
            const float x = xForPosition (t);

            if (x < area.getX() - 1.0f || x > area.getRight() + 1.0f)
                continue;

            g.fillRect (x - 0.5f, area.getY() + 1.0f, 1.0f, h);
        }
    }

    void WaveformView::paintPlayhead (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const float x = xForPosition (playhead);

        if (x < area.getX() - 1.0f || x > area.getRight() + 1.0f)
            return;

        g.setColour (theme::glassInk.withAlpha (overview ? 0.55f : 1.0f));
        g.fillRect (x - 0.5f, area.getY(), 1.0f, area.getHeight());
    }

    void WaveformView::paintVisibleWindow (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        if (visibleWindowEnd - visibleWindowStart >= 0.999)
            return;

        const float x0 = xForPosition (visibleWindowStart);
        const float x1 = xForPosition (visibleWindowEnd);

        const juce::Rectangle<float> box (x0, area.getY() + 0.5f,
                                          juce::jmax (2.0f, x1 - x0), area.getHeight() - 1.0f);

        g.setColour (theme::glassInk.withAlpha (0.07f));
        g.fillRect (box);

        g.setColour (theme::glassInkMuted.withAlpha (0.55f));
        g.drawRect (box, 1.0f);
    }

    void WaveformView::paintSourceBadge (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const auto f = theme::label (badgeTextSize);

        g.setColour (theme::glassInkMuted);
        theme::drawTracked (g, sourceLabel,
                            { area.getX() + badgeInset, area.getY() + badgeInset,
                              area.getWidth() - badgeInset * 2.0f, f.getHeight() },
                            f, badgeTrack, juce::Justification::centredLeft);
    }

    void WaveformView::paintDropOverlay (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const float corner = overview ? overviewCorner : layout::radiusCard;

        g.setColour (theme::violet.withAlpha (0.12f));
        g.fillRoundedRectangle (area, corner);

        g.setColour (theme::violetDeep);
        g.drawRoundedRectangle (area.reduced (1.0f), corner, 2.0f);

        if (overview || area.getHeight() < dropTextSize * 3.0f)
            return;

        const auto f = theme::label (dropTextSize);

        g.setColour (theme::violetLight);
        theme::drawTracked (g, "RELEASE TO REMEMBER",
                            { area.getX(), area.getCentreY() - f.getHeight() * 0.5f,
                              area.getWidth(), f.getHeight() },
                            f, dropTextTrack, juce::Justification::horizontallyCentred);
    }

    // =======================================================================
    //  Interaction - UI spec sections 5 and 10
    // =======================================================================
    void WaveformView::mouseDown (const juce::MouseEvent& e)
    {
        if (overview)
        {
            mouseDrag (e);      // clicking the strip scrubs the main field's window
            return;
        }

        dragHasMoved    = false;
        dragOriginX     = e.position.x;
        dragOriginStart = selectionStart;
        dragOriginEnd   = selectionEnd;

        const double p = positionForX (e.position.x);

        if (hasSelection() && std::abs (e.position.x - xForPosition (selectionStart)) <= handleGrabPixels)
        {
            dragMode   = DragMode::dragStart;
            dragAnchor = selectionEnd;          // the edge that stays put
        }
        else if (hasSelection() && std::abs (e.position.x - xForPosition (selectionEnd)) <= handleGrabPixels)
        {
            dragMode   = DragMode::dragEnd;
            dragAnchor = selectionStart;
        }
        else if (hasSelection()
                 && e.position.x > xForPosition (selectionStart)
                 && e.position.x < xForPosition (selectionEnd))
        {
            dragMode   = DragMode::moveSelection;
            dragAnchor = p;                     // where inside the block we grabbed
        }
        else
        {
            dragMode   = DragMode::newSelection;
            dragAnchor = snapped (p);           // the fixed edge of the new block
        }
    }

    void WaveformView::mouseDrag (const juce::MouseEvent& e)
    {
        if (overview)
        {
            // Drag the visible window around by its centre.
            const double span = juce::jmax (1.0e-6, visibleWindowEnd - visibleWindowStart);
            const double start = juce::jlimit (0.0, juce::jmax (0.0, 1.0 - span),
                                               positionForX (e.position.x) - span * 0.5);

            visibleWindowStart = start;
            visibleWindowEnd   = start + span;
            repaint();

            if (onScrolled != nullptr)
                onScrolled (start);

            return;
        }

        if (dragMode == DragMode::none)
            return;

        if (e.getDistanceFromDragStart() > (int) clickThresholdPixels)
            dragHasMoved = true;

        if (! dragHasMoved)
            return;

        // Shift is the fine gesture: the pointer travels six times as far as the
        // edge does.  UI spec section 10.
        const float x = e.mods.isShiftDown()
                          ? dragOriginX + (e.position.x - dragOriginX) / fineDragDivisor
                          : e.position.x;

        const double p = snapped (juce::jlimit (0.0, 1.0, positionForX (x)));

        switch (dragMode)
        {
            case DragMode::newSelection:
            case DragMode::dragStart:
            case DragMode::dragEnd:
                setSelection (juce::jmin (dragAnchor, p), juce::jmax (dragAnchor, p),
                              juce::sendNotification);
                break;

            case DragMode::moveSelection:
            {
                const double width = dragOriginEnd - dragOriginStart;
                const double start = juce::jlimit (0.0, juce::jmax (0.0, 1.0 - width),
                                                   dragOriginStart + (p - dragAnchor));
                setSelection (start, start + width, juce::sendNotification);
                break;
            }

            case DragMode::none:
            default:
                break;
        }
    }

    void WaveformView::mouseUp (const juce::MouseEvent& e)
    {
        if (overview)
        {
            dragMode = DragMode::none;
            return;
        }

        if (! dragHasMoved && dragMode != DragMode::none)
        {
            // A click, not a drag: move the playhead.  UI spec section 10.
            const double p = snapped (juce::jlimit (0.0, 1.0, positionForX (e.position.x)));

            setPlayhead (p);

            if (onPlayheadMoved != nullptr)
                onPlayheadMoved (p);
        }

        dragMode     = DragMode::none;
        dragHasMoved = false;
    }

    void WaveformView::mouseMove (const juce::MouseEvent& e)
    {
        if (overview)
            return;

        int handle = -1;

        if (hasSelection())
        {
            if (std::abs (e.position.x - xForPosition (selectionStart)) <= handleGrabPixels)
                handle = 0;
            else if (std::abs (e.position.x - xForPosition (selectionEnd)) <= handleGrabPixels)
                handle = 1;
        }

        if (handle != hoveredHandle)
        {
            hoveredHandle = handle;
            setMouseCursor (handle >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                                        : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void WaveformView::mouseExit (const juce::MouseEvent&)
    {
        if (hoveredHandle != -1)
        {
            hoveredHandle = -1;
            setMouseCursor (juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void WaveformView::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (overview)
            return;

        setSelection (0.0, 1.0, juce::sendNotification);
    }

    void WaveformView::mouseWheelMove (const juce::MouseEvent& e,
                                       const juce::MouseWheelDetails& wheel)
    {
        if (overview)
            return;

        // Horizontal wheel, or shift + wheel, pans instead of zooming.
        if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
        {
            const float delta = std::abs (wheel.deltaX) > 0.0f ? wheel.deltaX : wheel.deltaY;
            setScroll (windowStart - (double) delta * windowSpan * (double) wheelPanFraction);

            if (onScrolled != nullptr)
                onScrolled (windowStart);

            return;
        }

        const double anchor = positionForX (e.position.x);

        applyZoomAroundPosition (zoomNormalised + wheel.deltaY * zoomWheelStep,
                                 anchor, e.position.x);

        if (onZoomChanged != nullptr)
            onZoomChanged (zoomNormalised);

        if (onScrolled != nullptr)
            onScrolled (windowStart);
    }

    void WaveformView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
    {
        if (overview)
            return;

        const double anchor = positionForX (e.position.x);

        applyZoomAroundPosition (zoomNormalised + (scaleFactor - 1.0f) * magnifyGain,
                                 anchor, e.position.x);

        if (onZoomChanged != nullptr)
            onZoomChanged (zoomNormalised);

        if (onScrolled != nullptr)
            onScrolled (windowStart);
    }

    // =======================================================================
    //  Drag and drop
    // =======================================================================
    bool WaveformView::isSupportedAudioFile (const juce::String& path)
    {
        const auto extension = juce::File (path).getFileExtension().toLowerCase();

        return extension == ".wav"  || extension == ".aiff" || extension == ".aif"
            || extension == ".mp3"  || extension == ".flac";
    }

    bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
    {
        for (const auto& f : files)
            if (isSupportedAudioFile (f))
                return true;

        return false;
    }

    void WaveformView::fileDragEnter (const juce::StringArray&, int, int)
    {
        dragOver = true;
        repaint();
    }

    void WaveformView::fileDragMove (const juce::StringArray&, int, int)
    {
    }

    void WaveformView::fileDragExit (const juce::StringArray&)
    {
        dragOver = false;
        repaint();
    }

    void WaveformView::filesDropped (const juce::StringArray& files, int, int)
    {
        dragOver = false;
        repaint();

        juce::File dropped;

        for (const auto& f : files)
        {
            if (isSupportedAudioFile (f))
            {
                dropped = juce::File (f);
                break;
            }
        }

        if (! dropped.existsAsFile())
            return;

        // ------------------------------------------------------------------
        //  PHASE 2 BEHAVIOUR - READ THIS BEFORE CHANGING IT
        //
        //  The dropped file's path and name are written into the SAMPLE tree
        //  and nothing else happens.  The file is deliberately NOT opened:
        //
        //    * decoding on the message thread would stall the editor, and on a
        //      large AIFF it would stall it for seconds;
        //    * V1 Phase 2 has no sample engine, so decoded audio would have
        //      nowhere to go even if we had it;
        //    * inventing an envelope for audio nobody has read would be a lie
        //      drawn in violet, which is worse than an empty field.
        //
        //  Asynchronous decoding, waveform and overview generation, and the
        //  analysis pass (root, scale, tempo, transients, loudness) land in
        //  Phases 18-19.  They will live in Source/Audio/Sources/SampleEngine
        //  and Source/Analysis/, and they will fill ids::sampleRate,
        //  ids::sampleLengthSamples, ids::sampleChannels and the ANALYSIS
        //  subtree, then hand the decoded peaks to setThumbnailSource() above -
        //  which is already complete and working.  Nothing in this file needs
        //  to change when they arrive.
        // ------------------------------------------------------------------
        auto sample = processor.getStateManager().group (ids::SAMPLE);

        sample.setProperty (ids::sampleFile,        dropped.getFullPathName(), nullptr);
        sample.setProperty (ids::sampleDisplayName, dropped.getFileName(),     nullptr);

        // Everything the decoder and the analyser own is cleared rather than
        // guessed at, so nothing downstream can mistake a stale value for fact.
        sample.setProperty (ids::sampleRate,          0.0, nullptr);
        sample.setProperty (ids::sampleLengthSamples, 0,   nullptr);
        sample.setProperty (ids::sampleChannels,      0,   nullptr);
        sample.setProperty (ids::playhead,            0.0, nullptr);
        sample.setProperty (ids::selectionStart,      0.0, nullptr);
        sample.setProperty (ids::selectionEnd,        0.0, nullptr);

        if (onFileDropped != nullptr)
            onFileDropped (dropped);
    }
}
