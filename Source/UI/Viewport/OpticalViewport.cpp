#include "OpticalViewport.h"

#include <cmath>

namespace nacar::ui
{
    using namespace layout;

    // =======================================================================
    //  Numbers that are not in Layout.h
    //
    //  Layout.h is frozen and owns every position.  What follows is either a
    //  gap between two things Layout.h does place, or a property of a control
    //  rather than of a coordinate.  Each one says why it exists.
    // =======================================================================

    /** Breathing room between the header text and the control to its right.
        Layout.h fixes the text origin (vp::titleX) and the control centres
        (vp::pencil, vp::snapPill) but not the gap between them. */
    static constexpr float headerTextGutter = 10.0f;

    /** UI spec section 5 puts the time readout on baseline 406 and the ZOOM
        label on 405, absolute.  layout::vp carries only the transport centre
        line (vp::transportY == 299 local == 401 absolute), so the offset from
        it lives here.  One value for both: they differ by a single pixel. */
    static constexpr float transportBaselineOffset = 5.0f;

    /** Gap between the time readout and the loop button beyond it. */
    static constexpr float transportTextGutter = 12.0f;

    /** The ZOOM caption.  Section 5 gives its position but not its type size;
        this matches the other tracked captions in the reference. */
    static constexpr float zoomLabelSize  = 8.0f;
    static constexpr float zoomLabelTrack = 0.18f;

    /** The zoom slider's track is positioned by Layout.h; its hit height is not.
        Matching the other transport controls keeps the row's target sizes even. */
    static constexpr float zoomSliderHeight = 2.0f * vp::iconButtonR;

    /** The accent bar is 2 px wide; a 1 px radius stops it reading as a hard
        rectangle at 200 % scale. */
    static constexpr float accentBarCorner = 1.0f;

    /** Padding around the inline rename field, so its frame clears the text. */
    static constexpr float renameEditorPad = 4.0f;

    /** One press of zoom-in / zoom-out, in slider units. */
    static constexpr float zoomButtonStep = 0.10f;

    /** Width of the selection SHUFFLE creates when there is nothing to reuse. */
    static constexpr double defaultShuffleWidth = 0.25;

    /** Display-transport and control-sync rate.  Matches the editor's own. */
    static constexpr int viewportTimerHz = 30;

    /** Type for the honest note shown in list view. */
    static constexpr float listTitleSize  = 9.5f;
    static constexpr float listTitleTrack = 0.30f;
    static constexpr float listNoteSize   = 7.5f;
    static constexpr float listNoteTrack  = 0.20f;

    // -----------------------------------------------------------------------
    //  How deep the well is.
    //
    //  UI spec section 12: the glass regions are cut-outs, and the viewport is
    //  deeper at its centre than at its edges.  theme::recessedWell draws its
    //  inner shadow as four inset strokes, which is the right depth for a pill
    //  or a switch track and reads as a hairline on a panel 337 px tall - so
    //  the panel adds a gradient band of its own under the top edge, and the
    //  well routine supplies the bevel and the catch of light below it.
    // -----------------------------------------------------------------------

    /** Height of the inner shadow under the viewport's top edge. */
    static constexpr float wellShadowDepth = 26.0f;

    /** Alpha of that shadow where it meets the top edge. */
    static constexpr float wellShadowAlpha = 0.58f;

    /** Passed to theme::recessedWell.  Above 1 the bevel and the catch of light
        both strengthen, which is what "noticeably deeper" means here. */
    static constexpr float wellDepth = 1.45f;

    /** Corner radius of the four view-mode buttons.  Widgets.h draws its icon
        squares at 8; the same value keeps them in the family. */
    static constexpr float toolButtonCorner = 8.0f;

    /** Corner radius of the square stop button.  It is 44 px across where the
        view-mode buttons are 29, so holding the same radius would read as a
        sharper corner; 12 is the same corner-to-size ratio (29/8) at 44, which
        is what makes the two read as the same shape at two sizes. */
    static constexpr float transportSquareCorner = 12.0f;

    /** How far inside the active view-mode button its violet glow reaches. */
    static constexpr float toolGlowSpread = 4.0f;

    // =======================================================================
    //  ViewportButton
    // =======================================================================
    ViewportButton::ViewportButton (icons::Icon i, Body b, theme::Elevation e)
        : IconButton (i, IconButton::Style::plain), glyph (i), body (b), elevation (e)
    {
    }

    void ViewportButton::setGlyphColours (juce::Colour rest, juce::Colour lit)
    {
        restColour = rest;
        litColour  = lit;
        repaint();
    }

    void ViewportButton::setGlyphRatio (float r) noexcept
    {
        glyphRatio = r;
        repaint();
    }

    void ViewportButton::setCornerRadius (float r) noexcept
    {
        corner = r;
        repaint();
    }

    void ViewportButton::buttonStateChanged()
    {
        IconButton::buttonStateChanged();

        hoverAnim.setTarget (isOver() ? 1.0f : 0.0f);
        pressAnim.setTarget (isDown() ? 1.0f : 0.0f);
    }

    void ViewportButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const auto  b = getLocalBounds().toFloat().reduced (1.0f);
        const float d = juce::jmin (b.getWidth(), b.getHeight());

        const float press = pressAnim.get();
        const float hover = juce::jmax (hoverAnim.get(), highlighted ? 1.0f : 0.0f);

        juce::ignoreUnused (down);

        auto colour = isActive() ? litColour : restColour;

        switch (body)
        {
            case Body::disc:
            {
                const auto disc = juce::Rectangle<float> (d, d).withCentre (b.getCentre());
                theme::raisedGlass (g, disc, d * 0.5f, elevation, press, hover);
                break;
            }

            case Body::square:
            {
                const float c = corner > 0.0f ? corner : toolButtonCorner;

                theme::raisedGlass (g, b, c, elevation, press, hover);

                // The active one is lit rather than filled: violet inside the
                // edge and a little of it thrown onto the glass behind.  Spec
                // section 12 - violet is one of the two things that emit.
                if (isActive())
                {
                    theme::outerGlow (g, b, c, theme::violet,
                                      0.22f + hover * 0.10f, toolGlowSpread);
                    theme::innerGlow (g, b, c, theme::violet, 0.60f, toolGlowSpread);
                }
                break;
            }

            case Body::glyph:
            default:
                // The reference draws these as bare glyphs on the glass, so
                // that is what they are at rest.  They still become objects
                // under the pointer, which is what makes the row feel touchable
                // without making it louder than the image.
                //
                //  The body arrives whole rather than fading in: fading it
                //  would need a transparency layer, which is an offscreen
                //  image, and the one thing this viewport may not do is
                //  allocate one per frame.  The travel that is felt is the
                //  sink under the press, and theme::raisedGlass animates that
                //  from `press` for nothing.
                if (juce::jmax (hover, press) > 0.5f)
                    theme::raisedGlass (g, juce::Rectangle<float> (d, d).withCentre (b.getCentre()),
                                        d * 0.5f, theme::Elevation::resting, press, hover);
                break;
        }

        if (! isEnabled())
            colour = colour.withAlpha (0.4f);
        else if (hover > 0.5f && ! isActive())
            colour = colour.brighter (0.25f);

        const float glyphR = d * glyphRatio * 0.5f;
        const auto  centre = b.getCentre().translated (0.0f, press * 0.8f);

        icons::draw (g, glyph, juce::Rectangle<float> (glyphR * 2.0f, glyphR * 2.0f)
                                   .withCentre (centre), colour);
    }

    // -----------------------------------------------------------------------
    static juce::String formatTime (double seconds, bool withTenths)
    {
        if (! std::isfinite (seconds) || seconds < 0.0)
            seconds = 0.0;

        const int whole   = (int) seconds;
        const int minutes = whole / 60;
        const int secs    = whole % 60;

        juce::String out;
        out << minutes << ":" << juce::String (secs).paddedLeft ('0', 2);

        if (withTenths)
            out << "." << juce::String (juce::jlimit (0, 9, (int) ((seconds - (double) whole) * 10.0)));

        return out;
    }

    static juce::String middleDot()
    {
        return juce::String::fromUTF8 (" \xc2\xb7 ");
    }

    // =======================================================================
    //  Construction
    // =======================================================================
    OpticalViewport::OpticalViewport (NacarProcessor& p, EditorHost& h)
        : processor (p), host (h), visualizer (p)
    {
        setOpaque (false);

        addAndMakeVisible (waveField);
        addAndMakeVisible (overviewView);

        buildButtons();
        wireCallbacks();

        // The field shows the instrument's live output until something is
        // loaded, so the optical heart of the interface is never a dead
        // rectangle during development.  WaveformView badges it as the synth's
        // output - it is never dressed up as a sample.
        waveField.setSynthSource (&visualizer);
        overviewView.setSynthSource (&visualizer);
        // INSTRUMENT, not SYNTH: the processor taps the scope after the whole
        // chain and the master gain, so what this draws includes Memory, the
        // FX slots and the atmosphere modules, not the voice core alone.
        waveField.setSourceLabel (juce::String ("INSTRUMENT OUTPUT") + middleDot() + "LIVE LEVEL");

        visualizer.start();

        renameEditor.setMultiLine (false, false);
        renameEditor.setReturnKeyStartsNewLine (false);
        renameEditor.setFont (theme::medium (vp::titleSize));
        renameEditor.setJustification (juce::Justification::centredLeft);
        renameEditor.setSelectAllWhenFocused (true);
        renameEditor.onReturnKey = [this] { commitRename (true); };
        renameEditor.onEscapeKey = [this] { commitRename (false); };
        renameEditor.onFocusLost = [this] { commitRename (true); };
        addChildComponent (renameEditor);

        processor.getStateManager().session().addListener (this);

        refreshFromState();
        applyViewMode();

        startTimerHz (viewportTimerHz);
    }

    OpticalViewport::~OpticalViewport()
    {
        stopTimer();

        visualizer.onFrame = nullptr;
        visualizer.stop();

        processor.getStateManager().session().removeListener (this);
    }

    void OpticalViewport::buildButtons()
    {
        const std::array<icons::Icon, 4> toolIcons {
            icons::Icon::waveformMode, icons::Icon::listMode, icons::Icon::markers, icons::Icon::expand
        };

        const std::array<const char*, 4> toolTips {
            "Waveform view", "Slice list", "Transient markers", "Expanded waveform"
        };

        for (size_t i = 0; i < toolIcons.size(); ++i)
        {
            auto button = std::make_unique<ViewportButton> (toolIcons[i],
                                                           ViewportButton::Body::square);
            button->setGlyphColours (theme::glassInkMuted, theme::violet);
            button->setTooltip (toolTips[i]);

            const int index = (int) i;
            button->onClick = [this, index] { setViewMode ((ViewMode) index); };

            addAndMakeVisible (*button);
            toolButtons[i] = std::move (button);
        }

        pencilButton.setColours (theme::glassInkMuted, theme::violet);
        pencilButton.setTooltip ("Rename this source");
        addAndMakeVisible (pencilButton);

        snapPill.setTooltip ("Snap selections to transient markers");
        addAndMakeVisible (snapPill);

        for (auto* b : { &playButton, &stopButton })
        {
            b->setGlyphColours (theme::glassInk, theme::violet);
            addAndMakeVisible (*b);
        }

        stopButton.setCornerRadius (transportSquareCorner);

        for (auto* b : { &resetButton, &loopButton, &trimButton, &shuffleButton,
                         &zoomOutButton, &zoomInButton })
        {
            b->setGlyphColours (theme::glassInkMuted, theme::violet);
            addAndMakeVisible (*b);
        }

        playButton   .setTooltip ("Play");
        stopButton   .setTooltip ("Stop and return to the start of the selection");
        resetButton  .setTooltip ("Return to zero");
        loopButton   .setTooltip ("Loop between the loop points");
        trimButton   .setTooltip ("Trim playback to the selection");
        shuffleButton.setTooltip ("Move the selection somewhere else");
        zoomOutButton.setTooltip ("Zoom out");
        zoomInButton .setTooltip ("Zoom in");

        addAndMakeVisible (zoomSlider);
    }

    void OpticalViewport::wireCallbacks()
    {
        // -- the field ------------------------------------------------------
        waveField.onPlayheadMoved = [this] (double position)
        {
            overviewView.setPlayhead (position);
            writeSampleProperty (ids::playhead, position);
        };

        waveField.onSelectionChanged = [this] (double start, double end)
        {
            overviewView.setSelection (start, end);
            trimButton.setEnabled (end > start);

            writeSampleProperty (ids::selectionStart, start);
            writeSampleProperty (ids::selectionEnd,   end);
        };

        waveField.onZoomChanged = [this] (float zoom)
        {
            zoomSlider.setValue (zoom, juce::dontSendNotification);
            writeSampleProperty (ids::zoom, (double) zoom);
            syncOverviewWindow();
        };

        waveField.onScrolled = [this] (double start)
        {
            writeSampleProperty (ids::scrollPosition, start);
            syncOverviewWindow();
        };

        waveField.onFileDropped = [this] (juce::File)
        {
            // Dropping audio is a statement about which engine the player wants.
            host.setSource (Source::sample);
            refreshFromState();
        };

        // Scrubbing the overview strip pans the main field.
        overviewView.onScrolled = [this] (double start)
        {
            waveField.setScroll (start);
            writeSampleProperty (ids::scrollPosition, waveField.getScroll());
            syncOverviewWindow();
        };

        overviewView.onFileDropped = [this] (juce::File)
        {
            host.setSource (Source::sample);
            refreshFromState();
        };

        // -- header ---------------------------------------------------------
        pencilButton.onClick = [this] { startRename(); };

        snapPill.onClick = [this]
        {
            const bool snap = ! snapPill.isSelected();
            snapPill.setSelected (snap);
            waveField.setSnapToTransients (snap);
            overviewView.setSnapToTransients (snap);
        };

        // -- transport ------------------------------------------------------
        playButton.onClick = [this] { setPlaying (! playing); };

        stopButton.onClick = [this]
        {
            setPlaying (false);
            movePlayhead (waveField.hasSelection() ? waveField.getSelectionStart() : 0.0);
        };

        resetButton.onClick = [this] { movePlayhead (0.0); };

        loopButton.onClick = [this]
        {
            // Genuinely functional: PID::sampleLoop is a real host parameter.
            const auto& params = processor.getParameters();
            const bool nowLooping = ! params.flag (PID::sampleLoop);

            params.setFromUI (PID::sampleLoop, nowLooping ? 1.0f : 0.0f);
            loopButton.setActive (nowLooping);
        };

        trimButton.onClick = [this]
        {
            if (! waveField.hasSelection())
                return;

            // Also genuinely functional: these are real parameters, and the
            // sample engine will read them when it arrives.
            const auto& params = processor.getParameters();
            params.setFromUI (PID::sampleStart, (float) waveField.getSelectionStart());
            params.setFromUI (PID::sampleEnd,   (float) waveField.getSelectionEnd());
        };

        shuffleButton.onClick = [this]
        {
            const double width = waveField.hasSelection()
                                   ? waveField.getSelectionEnd() - waveField.getSelectionStart()
                                   : defaultShuffleWidth;

            const double start = shuffleRandom.nextDouble() * juce::jmax (0.0, 1.0 - width);
            waveField.setSelection (start, start + width, juce::sendNotification);
        };

        zoomOutButton.onClick = [this]
        {
            zoomSlider.setValue (zoomSlider.getValue() - zoomButtonStep, juce::sendNotification);
        };

        zoomInButton.onClick = [this]
        {
            zoomSlider.setValue (zoomSlider.getValue() + zoomButtonStep, juce::sendNotification);
        };

        zoomSlider.onValueChange = [this] (float value)
        {
            waveField.setZoom (value);
            writeSampleProperty (ids::zoom, (double) value);
            syncOverviewWindow();
        };

        // -- the synth monitor ----------------------------------------------
        visualizer.onFrame = [this]
        {
            waveField.refreshFromSynth();
            overviewView.refreshFromSynth();
        };
    }

    // =======================================================================
    //  Layout
    // =======================================================================
    void OpticalViewport::resized()
    {
        pencilButton.setBounds (centredSquare (vp::pencil, vp::iconButtonR).toNearestInt());
        snapPill.setBounds (vp::snapPill.toNearestInt());

        for (size_t i = 0; i < toolButtons.size(); ++i)
            if (toolButtons[i] != nullptr)
                toolButtons[i]->setBounds (RectF (vp::toolX[i], vp::toolY,
                                                  vp::toolSize, vp::toolSize).toNearestInt());

        applyViewMode();

        playButton .setBounds (centredSquare (vp::playButton,  vp::transportR).toNearestInt());
        stopButton .setBounds (centredSquare (vp::stopButton,  vp::transportR).toNearestInt());
        resetButton.setBounds (centredSquare (vp::resetButton, vp::iconButtonR).toNearestInt());

        loopButton   .setBounds (centredSquare (vp::loopButton,    vp::iconButtonR).toNearestInt());
        trimButton   .setBounds (centredSquare (vp::trimButton,    vp::iconButtonR).toNearestInt());
        shuffleButton.setBounds (centredSquare (vp::shuffleButton, vp::iconButtonR).toNearestInt());
        zoomOutButton.setBounds (centredSquare (vp::zoomOutButton, vp::iconButtonR).toNearestInt());
        zoomInButton .setBounds (centredSquare (vp::zoomInButton,  vp::iconButtonR).toNearestInt());

        zoomSlider.setBounds (RectF (vp::zoomTrackL,
                                     vp::transportY - zoomSliderHeight * 0.5f,
                                     vp::zoomTrackR - vp::zoomTrackL,
                                     zoomSliderHeight).toNearestInt());

        renameEditor.setBounds (titleRect().expanded (renameEditorPad).toNearestInt());
    }

    float OpticalViewport::titleWidth() const
    {
        // The title stops short of the rename pencil.
        return juce::jmax (1.0f, vp::pencil.x - vp::iconButtonR - headerTextGutter - vp::titleX);
    }

    float OpticalViewport::metaWidth() const
    {
        // The meta line runs on further, stopping short of the SNAP pill.
        return juce::jmax (1.0f, vp::snapPill.getX() - headerTextGutter - vp::titleX);
    }

    juce::Rectangle<float> OpticalViewport::titleRect() const
    {
        // Layout.h states every *Base constant as a true text baseline, so the
        // em box is rebuilt around it rather than started at it.
        const auto font = theme::medium (vp::titleSize);
        return { vp::titleX, vp::titleBase - font.getAscent(), titleWidth(), font.getHeight() };
    }

    void OpticalViewport::setViewMode (ViewMode mode)
    {
        if (viewMode == mode)
            return;

        viewMode = mode;
        applyViewMode();
    }

    void OpticalViewport::applyViewMode()
    {
        const bool expanded = (viewMode == ViewMode::expand);
        const bool listView = (viewMode == ViewMode::list);

        // Expanded takes the overview strip's room as well.
        const auto field = expanded ? vp::waveField.getUnion (vp::overviewStrip)
                                    : vp::waveField;

        waveField.setBounds (field.toNearestInt());
        waveField.setVisible (! listView);

        overviewView.setBounds (vp::overviewStrip.toNearestInt());
        overviewView.setVisible (! expanded && ! listView);

        waveField   .setShowMarkers (viewMode == ViewMode::markers);
        overviewView.setShowMarkers (viewMode == ViewMode::markers);

        for (size_t i = 0; i < toolButtons.size(); ++i)
        {
            if (toolButtons[i] == nullptr)
                continue;

            // ViewportButton draws the active state as light rather than as a
            // different style, so the flag is the whole of it.
            toolButtons[i]->setActive ((int) i == (int) viewMode);
        }

        syncOverviewWindow();
        repaint();
    }

    void OpticalViewport::syncOverviewWindow()
    {
        const double start = waveField.getScroll();
        overviewView.setVisibleWindow (start, start + waveField.getVisibleSpan());
    }

    // =======================================================================
    //  Painting
    // =======================================================================
    void OpticalViewport::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();

        // ------------------------------------------------------------------
        //  The well.
        //
        //  Glass is a hole in the chassis, never an object on it, so there is
        //  no drop shadow anywhere in here.  What makes a hole read as a hole
        //  is the wall the light cannot reach - the near one, under the top
        //  edge - and a catch of light on the far one.  UI spec section 12.
        // ------------------------------------------------------------------

        // 1. The body, deeper at the centre than at the edges.  The three glass
        //    shades are used for exactly what section 1 names them for: deep at
        //    the centre of the viewport, mid through the middle distance,
        //    raised where the cut meets the chassis.
        {
            const float reach = juce::jmax (1.0f, std::hypot (bounds.getWidth(),
                                                              bounds.getHeight()) * 0.5f);

            juce::ColourGradient body (theme::glassDeep, bounds.getCentreX(), bounds.getCentreY(),
                                       theme::glassRaised, bounds.getCentreX() + reach,
                                       bounds.getCentreY(), true);
            body.addColour (0.62, theme::glassMid);

            g.setGradientFill (body);
            g.fillRoundedRectangle (bounds, radiusPanel);
        }

        // 2. The inner shadow.  theme::recessedWell's own is four inset strokes
        //    - the right depth for a switch track, a hairline on a panel this
        //    tall - so the depth of the cut is a gradient band, and the well
        //    routine below supplies the bevel and the catch of light.
        {
            juce::Graphics::ScopedSaveState saved (g);

            juce::Path clip;
            clip.addRoundedRectangle (bounds, radiusPanel);
            g.reduceClipRegion (clip);

            g.setGradientFill (juce::ColourGradient (
                juce::Colours::black.withAlpha (wellShadowAlpha),
                bounds.getCentreX(), bounds.getY(),
                juce::Colours::transparentBlack,
                bounds.getCentreX(), bounds.getY() + wellShadowDepth, false));

            g.fillRect (bounds.withHeight (wellShadowDepth));
        }

        // 3. The walls.  A transparent body, because the gradient above is the
        //    body: recessedWell then strokes its bevel and its catch of light
        //    onto that instead of flattening it back to one colour.
        theme::recessedWell (g, bounds, radiusPanel, juce::Colours::transparentBlack, wellDepth);

        // 4. The hairline where the cut meets the chassis - spec section 1.
        g.setColour (theme::glassEdge);
        g.drawRoundedRectangle (bounds.reduced (0.5f), radiusPanel, 1.0f);

        paintHeader (g);

        if (viewMode == ViewMode::list)
            paintListPlaceholder (g);

        paintTransportRow (g);
    }

    void OpticalViewport::paintHeader (juce::Graphics& g)
    {
        g.setColour (theme::violet);
        g.fillRoundedRectangle (vp::accentBar, accentBarCorner);

        if (! renameEditor.isVisible())
        {
            const auto font = theme::medium (vp::titleSize);

            g.setColour (theme::glassInk);
            g.setFont (font);
            g.drawText (titleText, titleRect(), juce::Justification::centredLeft, true);
        }

        // vp::metaBase is a true baseline, so the box is rebuilt around it.
        const auto metaFont = theme::medium (vp::metaSize);

        g.setColour (theme::glassInkMuted);
        g.setFont (metaFont);
        g.drawText (metaText,
                    RectF (vp::titleX, vp::metaBase - metaFont.getAscent(),
                           metaWidth(), metaFont.getHeight()),
                    juce::Justification::centredLeft, true);
    }

    void OpticalViewport::paintTransportRow (juce::Graphics& g)
    {
        const float baseline = vp::transportY + transportBaselineOffset;

        // -- time readout ---------------------------------------------------
        {
            const auto font = theme::mono (vp::timeSize);
            const float width = juce::jmax (1.0f, vp::loopButton.x - vp::iconButtonR
                                                    - transportTextGutter - vp::timeX);

            const double lengthSeconds = sampleLengthSeconds();
            const juce::String readout =
                formatTime (waveField.getPlayhead() * lengthSeconds, true)
                    + " / " + formatTime (lengthSeconds, false);

            g.setColour (theme::glassInk);
            g.setFont (font);
            g.drawText (readout,
                        RectF (vp::timeX, baseline - font.getAscent(), width, font.getHeight()),
                        juce::Justification::centredLeft, false);
        }

        // -- ZOOM caption ---------------------------------------------------
        glassLabel (g, "ZOOM", { vp::zoomLabelX, baseline },
                    zoomLabelSize, zoomLabelTrack, theme::glassInkMuted);
    }

    void OpticalViewport::paintListPlaceholder (juce::Graphics& g)
    {
        // The slice list needs the analysis pass, which is not in this build.
        // Drawing an empty list, or a list of invented slices, would both be
        // worse than saying so.  The list view lands with Source/Analysis/ in
        // Phase 19.
        const auto area = vp::waveField;

        // The same cut the waveform field occupies, so switching view mode
        // changes what is in the well rather than how deep it is.
        theme::recessedWell (g, area, radiusCard, theme::glassDeep, wellDepth);

        const auto title = theme::label (listTitleSize);
        const auto note  = theme::label (listNoteSize);

        g.setColour (theme::glassInkMuted);
        theme::drawTracked (g, "SLICE LIST",
                            RectF (area.getX(), area.getCentreY() - title.getHeight(),
                                   area.getWidth(), title.getHeight()),
                            title, listTitleTrack, juce::Justification::horizontallyCentred);

        g.setColour (theme::glassInkFaint);
        theme::drawTracked (g, "NEEDS THE ANALYSIS PASS",
                            RectF (area.getX(), area.getCentreY() + note.getHeight() * 0.5f,
                                   area.getWidth(), note.getHeight()),
                            note, listNoteTrack, juce::Justification::horizontallyCentred);
    }

    // =======================================================================
    //  State
    // =======================================================================
    juce::ValueTree OpticalViewport::sampleTree() const
    {
        return processor.getStateManager().group (ids::SAMPLE);
    }

    void OpticalViewport::writeSampleProperty (const juce::Identifier& id, const juce::var& value)
    {
        const juce::ScopedValueSetter<bool> guard (writingState, true);

        auto tree = sampleTree();
        tree.setProperty (id, value, nullptr);
    }

    bool OpticalViewport::hasSample() const
    {
        return sampleTree().getProperty (ids::sampleDisplayName, "").toString().isNotEmpty();
    }

    double OpticalViewport::sampleLengthSeconds() const
    {
        const auto tree = sampleTree();

        const double rate   = (double) tree.getProperty (ids::sampleRate, 0.0);
        const double length = (double) tree.getProperty (ids::sampleLengthSamples, 0.0);

        return rate > 0.0 ? length / rate : 0.0;
    }

    juce::Array<double> OpticalViewport::readTransients() const
    {
        juce::Array<double> positions;

        const auto analysis = processor.getStateManager().session().getChildWithName (ids::ANALYSIS);

        if (! analysis.isValid())
            return positions;

        // Tolerate both shapes: the list as a property of ANALYSIS, or of a
        // TRANSIENTS child.  The analysis pass has not shipped yet and either
        // is a reasonable thing for it to write.
        juce::String csv = analysis.getProperty (ids::transientPositions, "").toString();

        if (csv.isEmpty())
        {
            const auto child = analysis.getChildWithName (ids::TRANSIENTS);

            if (child.isValid())
                csv = child.getProperty (ids::transientPositions, "").toString();
        }

        if (csv.isEmpty())
            return positions;

        for (const auto& token : juce::StringArray::fromTokens (csv, ",", ""))
        {
            const auto trimmed = token.trim();

            if (trimmed.isNotEmpty())
                positions.add (juce::jlimit (0.0, 1.0, trimmed.getDoubleValue()));
        }

        return positions;
    }

    void OpticalViewport::refreshHeaderText()
    {
        const auto tree = sampleTree();
        const auto displayName = tree.getProperty (ids::sampleDisplayName, "").toString();

        const auto& params = processor.getParameters();

        if (displayName.isNotEmpty())
        {
            titleText = "User Sample";

            const double rate   = (double) tree.getProperty (ids::sampleRate, 0.0);
            const double length = (double) tree.getProperty (ids::sampleLengthSamples, 0.0);

            juce::StringArray parts;
            parts.add (displayName);

            if (rate > 0.0)
            {
                parts.add (juce::String (rate / 1000.0, 1) + " kHz");

                if (length > 0.0)
                    parts.add (formatTime (length / rate, false));
            }

            // Nothing has read the file yet, so its rate and length are unknown
            // rather than assumed.  See WaveformView::filesDropped().
            if (rate <= 0.0 || length <= 0.0)
                parts.add ("not yet decoded");

            metaText = parts.joinIntoString (middleDot());
        }
        else
        {
            const int source = juce::jlimit (0, (int) Source::spectral, params.choice (PID::sourceMode));

            if (source == (int) Source::synth)
            {
                const auto characters = ParameterRegistry::choicesOf (PID::synthCharacter);
                const int index = juce::jlimit (0, juce::jmax (0, characters.size() - 1),
                                                params.choice (PID::synthCharacter));

                titleText = characters.isEmpty() ? juce::String ("SYNTH") : characters[index];
            }
            else
            {
                const auto sources = ParameterRegistry::choicesOf (PID::sourceMode);
                titleText = sources.isEmpty() ? juce::String ("SOURCE") : sources[source];
            }

            metaText = "no source loaded";
        }
    }

    void OpticalViewport::refreshFromState()
    {
        const auto tree = sampleTree();

        refreshHeaderText();

        const auto file   = tree.getProperty (ids::sampleFile, "").toString();
        const double rate = (double) tree.getProperty (ids::sampleRate, 0.0);
        const double len  = (double) tree.getProperty (ids::sampleLengthSamples, 0.0);

        const bool decoded = (rate > 0.0 && len > 0.0);

        // The decoded audio, handed over once per sample.
        //
        // Only real audio may claim SourceKind::sample, and this is where it
        // arrives: from the slot the audio thread plays out of, through the
        // overview the decoder already built. A file whose decode failed or has
        // not finished never gets here, so the field can never draw a waveform
        // for audio the instrument does not hold.
        {
            auto loaded = processor.getSampleSlot().acquire();
            const auto* raw = loaded.get();

            if (raw != shownSample)
            {
                shownSample = raw;

                if (raw != nullptr && ! raw->isEmpty())
                {
                    waveField.setPeakSource (raw->peaks);
                    overviewView.setPeakSource (raw->peaks);

                    waveField.setSourceLabel (raw->displayName.isNotEmpty()
                                                ? raw->displayName
                                                : juce::String ("SAMPLE"));
                }
                else
                {
                    waveField.clearSource();
                    overviewView.clearSource();
                }
            }
        }

        if (waveField.getSourceKind() != WaveformView::SourceKind::sample)
        {
            const auto kind = (file.isNotEmpty() && ! decoded)
                                ? WaveformView::SourceKind::pendingSample
                                : WaveformView::SourceKind::synthMonitor;

            waveField.setSourceKind (kind);
            overviewView.setSourceKind (kind);

            waveField.setSourceLabel (kind == WaveformView::SourceKind::synthMonitor
                                        ? juce::String ("INSTRUMENT OUTPUT") + middleDot() + "LIVE LEVEL"
                                        : juce::String());
        }

        waveField   .setPlayhead ((double) tree.getProperty (ids::playhead, 0.0));
        overviewView.setPlayhead ((double) tree.getProperty (ids::playhead, 0.0));

        const double selStart = (double) tree.getProperty (ids::selectionStart, 0.0);
        const double selEnd   = (double) tree.getProperty (ids::selectionEnd,   0.0);

        waveField   .setSelection (selStart, selEnd);
        overviewView.setSelection (selStart, selEnd);
        trimButton.setEnabled (selEnd > selStart);

        const float zoom = (float) (double) tree.getProperty (ids::zoom, 0.0);
        zoomSlider.setValue (zoom, juce::dontSendNotification);
        waveField.setZoom (zoom);
        waveField.setScroll ((double) tree.getProperty (ids::scrollPosition, 0.0));

        const auto transients = readTransients();
        waveField   .setTransients (transients);
        overviewView.setTransients (transients);

        pencilButton.setEnabled (hasSample());

        syncOverviewWindow();
        repaint();
    }

    void OpticalViewport::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
    {
        if (writingState)
            return;

        if (tree.hasType (ids::SAMPLE) || tree.hasType (ids::ANALYSIS) || tree.hasType (ids::TRANSIENTS))
            refreshFromState();
    }

    void OpticalViewport::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
    {
        if (child.hasType (ids::SAMPLE) || child.hasType (ids::ANALYSIS) || child.hasType (ids::TRANSIENTS))
            refreshFromState();
    }

    void OpticalViewport::valueTreeRedirected (juce::ValueTree&)
    {
        refreshFromState();
    }

    void OpticalViewport::syncFromParameters()
    {
        const auto& params = processor.getParameters();

        const bool looping = params.flag (PID::sampleLoop);

        if (looping != loopButton.isActive())
            loopButton.setActive (looping);

        const int source    = params.choice (PID::sourceMode);
        const int character = params.choice (PID::synthCharacter);

        if (source != lastSourceMode || character != lastCharacter)
        {
            lastSourceMode = source;
            lastCharacter  = character;

            refreshHeaderText();
            repaint();
        }
    }

    // =======================================================================
    //  Transport
    //
    //  This is a DISPLAY transport.  It moves the playhead and the readout, and
    //  nothing else: V1 Phase 2 has no sample engine, so there is no audio for
    //  it to drive.  With no sample loaded there is also no length to travel
    //  across, so pressing play swaps the glyph and the playhead stays where it
    //  is - the header says "no source loaded" and the field says so too.
    //  The real transport arrives with Source/Audio/Sources/SampleEngine in
    //  Phase 18 and will drive setPlayhead() from the engine's read position
    //  instead of from this timer.
    // =======================================================================
    void OpticalViewport::setPlaying (bool shouldPlay)
    {
        if (playing == shouldPlay)
            return;

        playing = shouldPlay;

        playButton.setIcon (playing ? icons::Icon::pause : icons::Icon::play);
        playButton.setActive (playing);

        lastTransportTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    }

    void OpticalViewport::advanceTransport()
    {
        const double lengthSeconds = sampleLengthSeconds();
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const double elapsed = juce::jlimit (0.0, 0.5, now - lastTransportTime);

        lastTransportTime = now;

        if (lengthSeconds <= 0.0)
            return;

        const bool looping = processor.getParameters().flag (PID::sampleLoop);

        const double from = waveField.hasSelection() ? waveField.getSelectionStart() : 0.0;
        const double to   = waveField.hasSelection() ? waveField.getSelectionEnd()   : 1.0;

        double position = waveField.getPlayhead() + elapsed / lengthSeconds;

        if (position >= to)
        {
            if (looping)
                position = from + std::fmod (position - from, juce::jmax (1.0e-6, to - from));
            else
            {
                position = to;
                setPlaying (false);
            }
        }

        // Not written into the tree on every frame: the playhead is persisted
        // when the player puts it somewhere, not thirty times a second.
        waveField   .setPlayhead (position);
        overviewView.setPlayhead (position);
    }

    void OpticalViewport::movePlayhead (double normalised)
    {
        normalised = juce::jlimit (0.0, 1.0, normalised);

        waveField   .setPlayhead (normalised);
        overviewView.setPlayhead (normalised);

        writeSampleProperty (ids::playhead, normalised);
    }

    void OpticalViewport::timerCallback()
    {
        syncFromParameters();

        if (playing)
            advanceTransport();
    }

    // =======================================================================
    //  Rename
    // =======================================================================
    void OpticalViewport::startRename()
    {
        if (! hasSample())
            return;

        renameEditor.setText (sampleTree().getProperty (ids::sampleDisplayName, "").toString(), false);
        renameEditor.setBounds (titleRect().expanded (renameEditorPad).toNearestInt());
        renameEditor.setVisible (true);
        renameEditor.grabKeyboardFocus();
        renameEditor.selectAll();

        repaint();
    }

    void OpticalViewport::commitRename (bool keepChanges)
    {
        if (! renameEditor.isVisible())
            return;

        const auto text = renameEditor.getText().trim();

        renameEditor.setVisible (false);

        if (keepChanges && text.isNotEmpty())
            writeSampleProperty (ids::sampleDisplayName, text);

        refreshHeaderText();
        repaint();
    }
}
