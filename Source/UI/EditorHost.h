#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace nacar::ui
{
    /** The five bottom-navigation pages. */
    enum class Page { main = 0, mod, fx, seq, mix };

    /** The five source engines. */
    enum class Source { synth = 0, sample, grain, resonator, spectral };

    /**
        What a region component is allowed to ask of the editor.

        Region components never reach for their parent by casting; they talk to
        this interface, which keeps the chassis and its contents decoupled.
    */
    struct EditorHost
    {
        virtual ~EditorHost() = default;

        virtual void setPage (Page) = 0;
        virtual Page getPage() const = 0;

        virtual void setSource (Source) = 0;
        virtual Source getSource() const = 0;

        virtual void setBrowserOpen (bool) = 0;
        virtual bool isBrowserOpen() const = 0;

        virtual void setUIScale (float) = 0;
        virtual float getUIScale() const = 0;

        /** Opens the settings menu anchored to a component. */
        virtual void showSettingsMenu (juce::Component& anchor) = 0;

        /** Steps the preset selection. */
        virtual void selectRelativePreset (int delta) = 0;

        /** Something changed that the whole chassis should redraw for. */
        virtual void chassisChanged() = 0;
    };
}
