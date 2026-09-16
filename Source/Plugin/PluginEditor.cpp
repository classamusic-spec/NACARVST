#include "PluginEditor.h"

#include "../UI/Components/Widgets.h"
#include "../UI/Panels/HeaderBar.h"
#include "../UI/Panels/MacroPanel.h"
#include "../UI/Panels/AtmospherePanel.h"
#include "../UI/Panels/BottomBar.h"
#include "../UI/Viewport/OpticalViewport.h"
#include "../UI/Mutation/MutationPanel.h"
#include "../UI/FX/FXChainView.h"
#include "../UI/Browser/PresetBrowser.h"
#include "../UI/Pages/ModPage.h"
#include "../UI/Pages/FXPage.h"
#include "../UI/Pages/SeqPage.h"
#include "../UI/Pages/MixPage.h"

namespace nacar
{
    using namespace layout;

    // =======================================================================
    //  NacarCanvas
    // =======================================================================
    NacarCanvas::NacarCanvas (NacarProcessor& p, ui::EditorHost& h)
        : processor (p), host (h)
    {
        setOpaque (true);
        setSize (canvasWidth, canvasHeight);

        headerBar       = std::make_unique<ui::HeaderBar>       (processor, host);
        macroPanel      = std::make_unique<ui::MacroPanel>      (processor, host);
        opticalViewport = std::make_unique<ui::OpticalViewport> (processor, host);
        mutationPanel   = std::make_unique<ui::MutationPanel>   (processor, host);
        fxChainView     = std::make_unique<ui::FXChainView>     (processor, host);
        atmospherePanel = std::make_unique<ui::AtmospherePanel> (processor, host);
        bottomBar       = std::make_unique<ui::BottomBar>       (processor, host);

        modPage = std::make_unique<ui::ModPage> (processor, host);
        fxPage  = std::make_unique<ui::FXPage>  (processor, host);
        seqPage = std::make_unique<ui::SeqPage> (processor, host);
        mixPage = std::make_unique<ui::MixPage> (processor, host);

        presetBrowser = std::make_unique<ui::PresetBrowser> (processor, host);

        for (auto* c : std::initializer_list<juce::Component*> {
                 headerBar.get(), macroPanel.get(), opticalViewport.get(),
                 mutationPanel.get(), fxChainView.get(), atmospherePanel.get(),
                 bottomBar.get(), modPage.get(), fxPage.get(), seqPage.get(),
                 mixPage.get(), presetBrowser.get() })
        {
            addChildComponent (c);
        }

        // MAIN is the landing page.
        headerBar->setVisible (true);
        macroPanel->setVisible (true);
        atmospherePanel->setVisible (true);
        bottomBar->setVisible (true);

        refreshPage (ui::Page::main);
    }

    NacarCanvas::~NacarCanvas() = default;

    void NacarCanvas::paint (juce::Graphics& g)
    {
        // The chassis itself: a single machined surface everything is cut into.
        g.fillAll (theme::ceramicMid);

        const juce::Rectangle<float> all (0.0f, 0.0f, (float) canvasWidth, (float) canvasHeight);

        g.setGradientFill (juce::ColourGradient (theme::ceramicLight, all.getWidth() * 0.30f, 0.0f,
                                                 theme::ceramicDark, all.getWidth() * 0.85f,
                                                 all.getHeight(), false));
        g.fillRect (all);

        // Very faint vertical brushing, so large flat areas are not dead.
        g.setColour (juce::Colours::white.withAlpha (0.016f));
        for (float x = 0.0f; x < all.getWidth(); x += 3.0f)
            g.fillRect (x, 0.0f, 1.0f, all.getHeight());

        // -- footer type ----------------------------------------------------
        {
            const juce::Rectangle<float> f = layout::footer;

            ui::ceramicLabel (g, "A PAST LIVES IN EVERY SOUND",
                              { f.getX() + foot::leftX, f.getY() + foot::baseline },
                              foot::size, foot::track, theme::inkFaint);

            ui::ceramicLabel (g, juce::String::fromUTF8 ("N\xc3\x81""CAR      V1.0.0      MMXXV"),
                              { foot::rightX, f.getY() + foot::baseline },
                              foot::size, foot::track, theme::inkFaint,
                              juce::Justification::right, 320.0f);
        }
    }

    void NacarCanvas::resized()
    {
        // Region accessors on this class share names with the layout rects, so
        // the layout namespace is spelled out here on purpose.
        headerBar      ->setBounds (layout::header     .toNearestInt());
        macroPanel     ->setBounds (layout::leftPanel  .toNearestInt());
        opticalViewport->setBounds (layout::viewport   .toNearestInt());
        mutationPanel  ->setBounds (layout::mutatePanel.toNearestInt());
        fxChainView    ->setBounds (layout::fxChain    .toNearestInt());
        atmospherePanel->setBounds (layout::rightPanel .toNearestInt());
        bottomBar      ->setBounds (layout::bottomBar  .toNearestInt());

        // The deep-edit pages occupy the whole centre column.
        const auto centre = layout::viewport.getUnion (layout::fxChain).toNearestInt();
        modPage->setBounds (centre);
        fxPage ->setBounds (centre);
        seqPage->setBounds (centre);
        mixPage->setBounds (centre);

        presetBrowser->setBounds (getLocalBounds());
    }

    void NacarCanvas::refreshPage (ui::Page page)
    {
        const bool isMain = (page == ui::Page::main);

        opticalViewport->setVisible (isMain);
        mutationPanel  ->setVisible (isMain);
        fxChainView    ->setVisible (isMain);

        modPage->setVisible (page == ui::Page::mod);
        fxPage ->setVisible (page == ui::Page::fx);
        seqPage->setVisible (page == ui::Page::seq);
        mixPage->setVisible (page == ui::Page::mix);
    }

    // =======================================================================
    //  NacarEditor
    // =======================================================================
    NacarEditor::NacarEditor (NacarProcessor& p)
        : juce::AudioProcessorEditor (&p), processor (p)
    {
        setLookAndFeel (&lookAndFeel);

        canvas = std::make_unique<NacarCanvas> (processor, *this);
        addAndMakeVisible (*canvas);

        // Restore the scale and page the user left the editor in.
        auto ed = editorState();
        uiScale = juce::jlimit (scaleMin, scaleMax, (float) (double) ed.getProperty (ids::editorScale, 1.0));
        page    = (ui::Page) juce::jlimit (0, 4, (int) ed.getProperty (ids::activePage, 0));

        canvas->refreshPage (page);

        setResizable (true, true);

        if (auto* c = getConstrainer())
        {
            c->setFixedAspectRatio ((double) aspect);
            c->setSizeLimits ((int) (canvasWidth * scaleMin), (int) (canvasHeight * scaleMin),
                              (int) (canvasWidth * scaleMax), (int) (canvasHeight * scaleMax));
        }

        setSize ((int) std::round (canvasWidth * uiScale),
                 (int) std::round (canvasHeight * uiScale));

        startTimerHz (30);
    }

    NacarEditor::~NacarEditor()
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    juce::ValueTree NacarEditor::editorState()
    {
        return processor.getStateManager().group (ids::EDITOR);
    }

    void NacarEditor::paint (juce::Graphics& g)
    {
        // Only ever visible in the sliver left by rounding the aspect ratio.
        g.fillAll (theme::ceramicDark);
    }

    void NacarEditor::resized()
    {
        applyTransform();
    }

    void NacarEditor::applyTransform()
    {
        if (canvas == nullptr)
            return;

        // One uniform transform for the whole interface.  Fitting to the
        // smaller axis keeps the panel square-on at any window shape the host
        // forces on us.
        const float sx = (float) getWidth()  / (float) canvasWidth;
        const float sy = (float) getHeight() / (float) canvasHeight;
        const float s  = juce::jmin (sx, sy);

        canvas->setTransform (juce::AffineTransform::scale (s));
        canvas->setBounds (0, 0, canvasWidth, canvasHeight);

        uiScale = s;
        editorState().setProperty (ids::editorScale, (double) s, nullptr);
    }

    void NacarEditor::timerCallback()
    {
        // The chassis animates: meters, waveform playhead, voice activity.
        // Each region repaints only what it owns.
        if (canvas != nullptr)
        {
            canvas->bottom().repaint();

            if (page == ui::Page::main)
                canvas->viewport().repaint();
        }
    }

    // -----------------------------------------------------------------------
    //  EditorHost
    // -----------------------------------------------------------------------
    void NacarEditor::setPage (ui::Page p)
    {
        if (page == p)
            return;

        page = p;
        editorState().setProperty (ids::activePage, (int) p, nullptr);

        if (canvas != nullptr)
        {
            canvas->refreshPage (p);
            canvas->bottom().repaint();
        }
    }

    void NacarEditor::setSource (ui::Source s)
    {
        processor.getParameters().setFromUI (PID::sourceMode, (float) (int) s);
        chassisChanged();
    }

    ui::Source NacarEditor::getSource() const
    {
        return (ui::Source) processor.getParameters().choice (PID::sourceMode);
    }

    void NacarEditor::setBrowserOpen (bool shouldBeOpen)
    {
        browserOpen = shouldBeOpen;
        editorState().setProperty (ids::browserOpen, shouldBeOpen, nullptr);

        if (canvas != nullptr)
            canvas->browser().setOpen (shouldBeOpen);
    }

    void NacarEditor::setUIScale (float s)
    {
        s = juce::jlimit (scaleMin, scaleMax, s);
        setSize ((int) std::round (canvasWidth * s), (int) std::round (canvasHeight * s));
    }

    void NacarEditor::selectRelativePreset (int delta)
    {
        juce::ignoreUnused (delta);
        // Wired to PresetManager once the factory library lands.  Until then
        // this is deliberately inert rather than faking a preset change.
    }

    void NacarEditor::chassisChanged()
    {
        if (canvas != nullptr)
            canvas->repaint();
    }

    void NacarEditor::showSettingsMenu (juce::Component& anchor)
    {
        juce::PopupMenu m;
        juce::PopupMenu scaleMenu;

        const std::array<std::pair<const char*, float>, 5> steps {{
            { "75 %",  0.75f }, { "100 %", 1.00f }, { "125 %", 1.25f },
            { "150 %", 1.50f }, { "200 %", 2.00f }
        }};

        for (int i = 0; i < (int) steps.size(); ++i)
            scaleMenu.addItem (100 + i, steps[(size_t) i].first, true,
                               std::abs (uiScale - steps[(size_t) i].second) < 0.02f);

        m.addSectionHeader ("NACAR");
        m.addSubMenu ("Interface Scale", scaleMenu);

        juce::PopupMenu quality;
        const auto qualityNames = ParameterRegistry::choicesOf (PID::qualityMode);
        const int currentQuality = processor.getParameters().choice (PID::qualityMode);

        for (int i = 0; i < qualityNames.size(); ++i)
            quality.addItem (200 + i, qualityNames[i], true, i == currentQuality);

        m.addSubMenu ("Audio Quality", quality);
        m.addSeparator();
        m.addItem (900, "About NACAR");

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                         [this, steps] (int result)
                         {
                             if (result >= 100 && result < 100 + (int) steps.size())
                                 setUIScale (steps[(size_t) (result - 100)].second);
                             else if (result >= 200 && result < 210)
                                 processor.getParameters().setFromUI (PID::qualityMode,
                                                                      (float) (result - 200));
                         });
    }
}
