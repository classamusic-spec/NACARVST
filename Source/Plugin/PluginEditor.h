#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "../UI/Theme.h"
#include "../UI/Layout.h"
#include "../UI/EditorHost.h"

namespace nacar
{
    namespace ui
    {
        class HeaderBar;
        class MacroPanel;
        class OpticalViewport;
        class MutationPanel;
        class FXChainView;
        class AtmospherePanel;
        class BottomBar;
        class PresetBrowser;
        class ModPage;
        class FXPage;
        class SeqPage;
        class MixPage;
    }

    /**
        The NACAR chassis.

        Everything is laid out on a fixed 1536 x 1024 logical canvas taken
        straight from the locked reference image; the editor applies a single
        uniform transform to reach the window size.  Nothing reflows - NACAR is
        a machined front panel, not a responsive page.
    */
    class NacarCanvas : public juce::Component
    {
    public:
        NacarCanvas (NacarProcessor&, ui::EditorHost&);
        ~NacarCanvas() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Shows the centre column appropriate to the active page. */
        void refreshPage (ui::Page);

        ui::HeaderBar&       header()     noexcept { return *headerBar; }
        ui::AtmospherePanel& atmosphere() noexcept { return *atmospherePanel; }
        ui::OpticalViewport& viewport()   noexcept { return *opticalViewport; }
        ui::BottomBar&       bottom()     noexcept { return *bottomBar; }
        ui::PresetBrowser&   browser()    noexcept { return *presetBrowser; }

    private:
        NacarProcessor& processor;
        ui::EditorHost& host;

        std::unique_ptr<ui::HeaderBar>       headerBar;
        std::unique_ptr<ui::MacroPanel>      macroPanel;
        std::unique_ptr<ui::OpticalViewport> opticalViewport;
        std::unique_ptr<ui::MutationPanel>   mutationPanel;
        std::unique_ptr<ui::FXChainView>     fxChainView;
        std::unique_ptr<ui::AtmospherePanel> atmospherePanel;
        std::unique_ptr<ui::BottomBar>       bottomBar;
        std::unique_ptr<ui::PresetBrowser>   presetBrowser;

        std::unique_ptr<ui::ModPage> modPage;
        std::unique_ptr<ui::FXPage>  fxPage;
        std::unique_ptr<ui::SeqPage> seqPage;
        std::unique_ptr<ui::MixPage> mixPage;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NacarCanvas)
    };

    // -----------------------------------------------------------------------
    class NacarEditor : public juce::AudioProcessorEditor,
                        public ui::EditorHost,
                        private juce::Timer
    {
    public:
        explicit NacarEditor (NacarProcessor&);
        ~NacarEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // -- EditorHost -----------------------------------------------------
        void setPage (ui::Page) override;
        ui::Page getPage() const override { return page; }

        void setSource (ui::Source) override;
        ui::Source getSource() const override;

        void setBrowserOpen (bool) override;
        bool isBrowserOpen() const override { return browserOpen; }

        void setUIScale (float) override;
        float getUIScale() const override { return uiScale; }

        void showSettingsMenu (juce::Component& anchor) override;
        void selectRelativePreset (int delta) override;
        void chassisChanged() override;

    private:
        void timerCallback() override;
        void applyTransform();
        juce::ValueTree editorState();

        NacarProcessor& processor;
        theme::NacarLookAndFeel lookAndFeel;
        juce::TooltipWindow tooltips { this, 700 };

        std::unique_ptr<NacarCanvas> canvas;

        ui::Page page = ui::Page::main;
        float uiScale = 1.0f;
        bool browserOpen = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NacarEditor)
    };
}
