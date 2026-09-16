#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

#include "../Theme.h"
#include "../Layout.h"
#include "../EditorHost.h"
#include "../Components/Widgets.h"
#include "../Components/Icons.h"
#include "../../Plugin/PluginProcessor.h"

#include "IntentSelector.h"

namespace nacar::ui
{
    /**
        The mutate panel - UI spec section 6.

        A raised ceramic panel carrying the whole mutation decision: what kind
        of transformation to aim for (INTENT), how far it may stray harmonically
        (HARMONY), how far it may stray timbrally (DISTANCE), what it must hold
        fixed (PRESERVE), and the four buttons that act on all of that.

        Harmony and Distance are two independent axes and are never coupled:
        Harmony is the musical decision, Distance is the structural one.  That
        is why the reference fills the Harmony selection violet and the Distance
        selection dark, and why they bind to two separate parameters.

        The mutation engine itself is Phase 21 and does not exist yet.  See the
        comment block at the top of MutationPanel.cpp for exactly what this
        panel does and does not do today.
    */
    class MutationPanel : public juce::Component
    {
    public:
        MutationPanel (NacarProcessor&, EditorHost&);
        ~MutationPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        /** MUTATE rolls a fresh recipe from the current controls; AGAIN rolls
            only a new seed and reuses the last recipe's intent, harmony and
            distance. */
        enum class Trigger { mutate, again };

        void runMutation (Trigger);
        void setStatus (juce::String);

        /** SESSION/MUTATION, created on demand by StateManager::group(). */
        juce::ValueTree mutationTree();

        /** SESSION/MUTATION/HISTORY, created if a restored tree lacks it. */
        juce::ValueTree historyTree (juce::ValueTree& mutation);

        /** Master preserve lock: ON locks all seven, OFF leaves them alone. */
        void applyMasterPreserve (bool);

        NacarProcessor& processor;
        EditorHost& host;

        SegmentedControl harmonySelector;
        SegmentedControl distanceSelector;
        IntentSelector   intentSelector;

        PillButton mutateButton;
        PillButton againButton;
        PillButton printButton;
        PillButton makeInstrumentButton;

        static constexpr int numPreserveLocks = 8;   ///< seven holds plus the master
        std::array<std::unique_ptr<PreserveLock>, (size_t) numPreserveLocks> preserveLocks;

        juce::String status;
        juce::Random rng;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MutationPanel)
    };
}
