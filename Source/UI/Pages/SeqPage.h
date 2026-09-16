#pragma once

#include "PageSurface.h"

namespace nacar::ui
{
    /**
        The sequencer's corner of the session tree.

        StateManager.h is frozen and has no sequencer identifiers, so they are
        declared here instead, following the same convention: the identifier's
        name and its string are the same word, and they live under SESSION as a
        SEQUENCER child holding one SEQLANE per lane.

        A lane stores its sixteen step values as one comma-separated string and
        its sixteen gates as a sixteen-character mask.  Packing a short list
        into a single property is already the house pattern - see
        ids::transientPositions - and it keeps the tree small enough to live
        inside a preset.
    */
    namespace seqIds
    {
        inline const juce::Identifier SEQUENCER   ("SEQUENCER");
        inline const juce::Identifier SEQLANE     ("SEQLANE");

        inline const juce::Identifier seqDivision ("seqDivision");   // index into divisionNames()

        inline const juce::Identifier laneName    ("laneName");
        inline const juce::Identifier laneTarget  ("laneTarget");    // permanent parameter string ID
        inline const juce::Identifier laneEnabled ("laneEnabled");
        inline const juce::Identifier laneLength  ("laneLength");    // 1..16 steps per cycle
        inline const juce::Identifier laneValues  ("laneValues");    // "0.50,0.25,..." x16
        inline const juce::Identifier laneGates   ("laneGates");     // "1011..." x16
    }

    /** One lane's step well.  Defined in SeqPage.cpp. */
    class SeqLaneEditor;

    /**
        SEQ - the step sequencer foundation (master spec section 129).

        THIS PAGE EDITS AND PERSISTS STEP DATA.  NOTHING READS IT YET.

        The trigger engine - what actually advances a lane against the host
        clock and applies a step to its target - is Phase 29-adjacent work and
        is deliberately not attempted here: the master spec is explicit that
        sequencing must not delay core sound quality.  What this page does is
        real: four lanes of sixteen steps, each with a value, a gate, a length
        and a target parameter, all saved with the session and the preset.  The
        PREVIEW playhead is an editing aid driven by the UI clock and the host
        tempo; it is not the transport and it is labelled as such on the page.
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
        double stepSeconds() const;

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

        bool   previewRunning = false;
        int    playhead       = 0;
        double previewStartMs = 0.0;

        juce::Rectangle<int> lanesBounds, transportBounds;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeqPage)
    };
}
