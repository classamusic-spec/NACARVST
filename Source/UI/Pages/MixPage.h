#pragma once

#include "PageSurface.h"

namespace nacar::ui
{
    /**
        MIX - gain staging and balance (master spec section 130).

        The specification's wish list for this page is "source layer gain, pan,
        width, FX balance, Aura, Shadow, output, gain staging".  Most of that
        exists as parameters; some of it does not, and this page is built from
        what is really there rather than from what would look symmetrical.

        Concretely: there are no per-source gain or pan parameters in
        ParameterList.h, so there is no five-channel console here.  The SYNTH
        strip is the voice mixer, which is the real place a synth layer's level
        is set; the SAMPLE, RESONATOR and SPECTRAL strips are thin because their
        engines currently expose one or three parameters each.  Widening them
        means adding parameters, not adding controls - see the note the page
        draws on itself.
    */
    class MixPage : public PageSurface
    {
    public:
        MixPage (NacarProcessor&, EditorHost&);
        ~MixPage() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void tick() override;
        void paintMeter (juce::Graphics&, juce::Rectangle<float>) const;

        struct Strip
        {
            juce::String caption;
            juce::String note;          ///< small print under the caption, may be empty
            int  numKnobs = 0;
            int  columns = 3;
            juce::Rectangle<int> bounds;
        };

        static constexpr int numStrips = 7;
        Strip strips[numStrips];

        juce::Rectangle<int> meterBounds;
        float meterLevel[2] { 0.0f, 0.0f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixPage)
    };
}
