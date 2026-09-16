#pragma once

#include "PageSurface.h"

namespace nacar::ui
{
    /** The routing table at the foot of the page.  Defined in ModPage.cpp
        because nothing outside the MOD page has any business with it. */
    class ModMatrix;

    /**
        MOD - deep modulation editing (master spec section 127).

        Two LFOs with live shape previews, the three envelopes with live ADSR
        previews, Breath, Pulse and its psychoacoustic destination group, and an
        eight-slot modulation matrix.

        Everything on this page except the matrix is bound to a real host
        parameter.  The matrix has no parameters behind it - see ModPage.cpp.
    */
    class ModPage : public PageSurface
    {
    public:
        ModPage (NacarProcessor&, EditorHost&);
        ~ModPage() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void tick() override;
        void applySyncVisibility (int index);

        // -- LFO 1 / LFO 2 --------------------------------------------------
        struct Lfo
        {
            PID shape, sync, division, rate, depth, phase;
            juce::String caption;

            SegmentedControl* shapeControl = nullptr;
            PreserveLock*     syncLock     = nullptr;
            ChoicePill*       divisionPill = nullptr;
            NacarKnob*        rateKnob     = nullptr;
            NacarKnob*        depthKnob    = nullptr;
            NacarKnob*        phaseKnob    = nullptr;

            juce::Rectangle<int> bounds, preview, rateCell;
            float previewHash = -1.0f;
            int   syncState   = -1;
        };

        // -- AMP ENV / MOD ENV 1 / MOD ENV 2 --------------------------------
        struct Env
        {
            PID attack, decay, sustain, release;
            juce::String caption;

            juce::Rectangle<int> bounds, preview;
            float previewHash = -1.0f;
        };

        Lfo lfo[2];
        Env env[3];

        NacarKnob*   velocityKnob = nullptr;
        PreserveLock* breathLock  = nullptr;
        PowerButton* pulsePower   = nullptr;

        SegmentedControl* pulseSourceSeg   = nullptr;
        SegmentedControl* pulseDivisionSeg = nullptr;

        juce::Rectangle<int> breathBounds, breathPreview, pulseBounds, matrixBounds;
        juce::Rectangle<int> pulseDestCaption, pulseDestRule;
        float breathHash = -1.0f;

        std::unique_ptr<ModMatrix> matrix;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModPage)
    };
}
