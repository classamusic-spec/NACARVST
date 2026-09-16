#include "IntentSelector.h"

namespace nacar::ui
{
    namespace mut = layout::mut;

    // =======================================================================
    //  Numbers the reference implies but Layout.h does not carry.
    // =======================================================================
    namespace
    {
        /// How often the row re-reads PID::mutationIntent so that host
        /// automation, a preset load or an undo moves the highlight.  A choice
        /// parameter changes at human speed; 12 Hz is invisible and costs
        /// nothing.
        constexpr int pollHz = 12;

        /// Second probe used to measure how PillButton::preferredWidth() scales
        /// with its padding argument.  Any non-zero value works; the full
        /// padding from Layout.h is the one we care about being exact at.
        constexpr float paddingProbe = mut::intentPadding;
    }

    // =======================================================================
    //  IntentSelector
    // =======================================================================
    IntentSelector::IntentSelector (const ParameterRegistry& p)
        : params (p)
    {
        // The labels are the parameter's own choices, in the parameter's own
        // order: MEMORY CLOUD BROKEN REVERSE DISTANT RHYTHMIC DARK GHOST
        // PLAYABLE CINEMATIC.  Taking them from the table is what guarantees
        // pill index == choice index.
        const auto choices = ParameterRegistry::choicesOf (PID::mutationIntent);
        jassert (choices.size() == numIntents);

        const auto& def = ParameterRegistry::definition (PID::mutationIntent);

        for (int i = 0; i < numIntents; ++i)
        {
            const juce::String name = i < choices.size() ? choices[i] : juce::String (i);

            auto pill = std::make_unique<PillButton> (name, PillButton::Style::ceramic);

            pill->setTextSize (mut::intentSize, mut::intentTrack);
            pill->setCornerRadius (layout::radiusPill);
            pill->setTooltip (juce::String (def.name) + ": " + name + "\n"
                              + juce::String (def.tooltip));

            pill->onClick = [this, i] { setSelectedIndex (i, juce::sendNotification); };

            addAndMakeVisible (*pill);
            pills[(size_t) i] = std::move (pill);
        }

        // Start on whatever the parameter already says, silently.
        setSelectedIndex (params.choice (PID::mutationIntent), juce::dontSendNotification);

        startTimerHz (pollHz);
    }

    IntentSelector::~IntentSelector()
    {
        stopTimer();
    }

    void IntentSelector::setSelectedIndex (int newIndex, juce::NotificationType notification)
    {
        newIndex = juce::jlimit (0, numIntents - 1, newIndex);

        if (newIndex == selectedIndex && pills[(size_t) newIndex] != nullptr
            && pills[(size_t) newIndex]->isSelected())
            return;

        selectedIndex = newIndex;

        for (int i = 0; i < numIntents; ++i)
            if (auto* pill = pills[(size_t) i].get())
                pill->setSelected (i == selectedIndex);

        if (notification == juce::dontSendNotification)
            return;

        params.setFromUI (PID::mutationIntent, (float) selectedIndex);

        if (onSelect != nullptr)
            onSelect (selectedIndex);
    }

    float IntentSelector::preferredWidth() const
    {
        float total = mut::intentGap * (float) (numIntents - 1);

        for (const auto& pill : pills)
            if (pill != nullptr)
                total += pill->preferredWidth (mut::intentPadding);

        return total;
    }

    void IntentSelector::resized()
    {
        // ------------------------------------------------------------------
        //  One line, always.
        //
        //  The reference has all ten pills on a single row, so wrapping to a
        //  second row or letting the last pill run off the panel are both
        //  wrong.  The only thing that may give is the padding, and it has to
        //  give by the same amount on every pill or the row stops reading as a
        //  set.
        //
        //  Widgets.h does not say whether preferredWidth() applies its padding
        //  once or on both sides, so rather than guess we measure: pill width
        //  is affine in the padding, so two probes give an intercept (the
        //  label plus its icons) and a slope (how many times the padding
        //  counts).  Summing those over the row turns
        //
        //      sum(intercept) + padding * sum(slope) + gaps  =  available
        //
        //  into a single solve for the padding.  Compute the total first, then
        //  derive the padding - never the other way round.
        // ------------------------------------------------------------------
        const float available = (float) getWidth();
        const float gaps      = mut::intentGap * (float) (numIntents - 1);

        float intercept = 0.0f;
        float slope     = 0.0f;

        for (const auto& pill : pills)
        {
            if (pill == nullptr)
                continue;

            const float w0 = pill->preferredWidth (0.0f);
            const float w1 = pill->preferredWidth (paddingProbe);

            intercept += w0;
            slope     += (w1 - w0) / paddingProbe;
        }

        float padding = mut::intentPadding;

        if (slope > 0.0f)
            padding = juce::jlimit (0.0f, mut::intentPadding,
                                    (available - gaps - intercept) / slope);

        // If the labels alone overrun the row there is nothing left to take:
        // the type size is fixed by the spec and shrinking it would break the
        // family of 8.5 pt tracked labels the whole chassis is set in.  At that
        // point the row simply runs long, which is loud and visible, rather
        // than silently clipping a label.
        jassert (intercept + gaps <= available);

        float x = 0.0f;

        for (const auto& pill : pills)
        {
            if (pill == nullptr)
                continue;

            const float w = pill->preferredWidth (padding);

            pill->setBounds (juce::Rectangle<float> (x, 0.0f, w, (float) getHeight())
                                 .toNearestInt());

            x += w + mut::intentGap;
        }
    }

    void IntentSelector::timerCallback()
    {
        setSelectedIndex (params.choice (PID::mutationIntent), juce::dontSendNotification);
    }
}
