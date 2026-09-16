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
        MUTATE - the one control in the instrument that emits light.

        PillButton::Style::violet already lays the body down with
        theme::accentSurface, which is right and is not re-done here.  What it
        cannot express is the ink: it returns a single colour for the label and
        the glyph together, and that colour is theme::ink.  The reference sets
        both in violet on the pale lilac face, and that violet-on-lilac is most
        of what makes the button read as lit from within rather than as a
        lilac rectangle with a caption on it.

        So this subclass overrides the face rather than the widget: the same
        accentSurface body, the same content layout as every other pill - the
        travel under a press included, so the label stays printed on the face
        it sits on - and violet ink.  Nothing in Widgets.* is touched.
    */
    class MutateButton : public PillButton
    {
    public:
        MutateButton();

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    protected:
        void buttonStateChanged() override;

    private:
        detail::Motion hoverAnim { *this, 0.30f };
        detail::Motion pressAnim { *this, 0.55f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MutateButton)
    };

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

        MutateButton mutateButton;
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
