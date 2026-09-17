#pragma once

#include "PageSurface.h"

namespace nacar::ui
{
    /** One lane's step well.  Defined in SeqPage.cpp. */
    class SeqLaneEditor;

    /**
        SEQ - the step sequencer (master spec section 129).

        THE IDENTIFIERS USED TO LIVE HERE.  They are now in
        Source/Plugin/StateManager.h with the rest of the session schema,
        because Source/Audio must not include Source/UI and the trigger engine
        needs the same names this page writes.  There is one copy: `ids::`.
        The division table moved with them, as `seq::divisions`, so the name
        this page prints and the beats the engine runs at cannot disagree.

        Every control on this page is live.  The step well edits values and
        gates, the target pill chooses a parameter by its permanent string ID,
        the length pill sets 1..16 steps so lanes of different lengths drift
        against each other, the enable toggle decides whether a lane writes
        anything at all, and the division pill sets the shared step length
        against the host tempo.  SequencerEngine advances all of it and
        NacarEngine applies it through the modulation overlay.

        PREVIEW shows the playhead.  It no longer runs a clock of its own: the
        step it draws is the step the audio thread is on, read back from the
        engine.  The lanes advance whether or not it is switched on - it is a
        display toggle, not a transport.
    */
    class SeqPage : public PageSurface
    {
    public:
        SeqPage (NacarProcessor&, EditorHost&);
        ~SeqPage() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void tick() override;

        void fetchTree();
        void refreshLanes (bool force = false);
        void chooseTarget (int lane);
        void chooseLength (int lane);
        void chooseDivision();

        static constexpr int numLanes = 4;
        static constexpr int numSteps = 16;

        struct Lane
        {
            juce::ValueTree tree;

            SeqLaneEditor* editor = nullptr;
            PillButton*    target = nullptr;
            PillButton*    length = nullptr;
            ToggleSwitch*  enable = nullptr;

            juce::Rectangle<int> header, well;
        };

        Lane lanes[numLanes];

        juce::ValueTree seqTree;
        juce::OwnedArray<juce::Component> extras;   ///< controls with no PID behind them

        PillButton* previewButton  = nullptr;
        PillButton* divisionButton = nullptr;

        bool previewRunning = false;

        juce::Rectangle<int> lanesBounds, transportBounds;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeqPage)
    };
}
