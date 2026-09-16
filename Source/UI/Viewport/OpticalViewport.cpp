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
            auto button = std::make_unique<IconButton> (toolIcons[i], IconButton::Style::glassSquare);
            button->setColours (theme::glassInkMuted, theme::violet);
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
            b->setColours (theme::glassInk, theme::violet);
            addAndMakeVisible (*b);
        }

        for (auto* b : { &resetButton, &loopButton, &trimButton, &shuffleButton,
                         &zoomOutButton, &zoomInButton })
        {
            b->setColours (theme::glassInkMuted, theme::violet);
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

            const bool active = ((int) i == (int) viewMode);

            toolButtons[i]->setStyle (active ? IconButton::Style::violetSquare
                                             : IconButton::Style::glassSquare);
            toolButtons[i]->setActive (active);
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
        // Glass is a cut-out: recessed fill, inner top shadow, hairline edge,
        // and deliberately no drop shadow.
        theme::glassSurface (g, getLocalBounds().toFloat(), radiusPanel);

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

        theme::glassSurface (g, area, radiusCard, theme::glassDeep);

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

        // Only the decoder may claim SourceKind::sample, and it does that by
        // calling WaveformView::setThumbnailSource() with real audio.  Nothing
        // here ever sets it.
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
