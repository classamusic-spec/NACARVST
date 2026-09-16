#include "SynthVisualizer.h"

#include <cmath>

namespace nacar::ui
{
    SynthVisualizer::SynthVisualizer (NacarProcessor& p)
        : processor (p)
    {
        left.fill (0.0f);
        right.fill (0.0f);
    }

    SynthVisualizer::~SynthVisualizer()
    {
        stopTimer();
    }

    void SynthVisualizer::start()
    {
        if (! isTimerRunning())
            startTimerHz (refreshHz);
    }

    void SynthVisualizer::stop()
    {
        stopTimer();
    }

    void SynthVisualizer::timerCallback()
    {
        // The only audio the editor can legally see.  getMeterLevel() is a
        // decayed per-channel peak of the last processed block, published
        // through a relaxed atomic, so reading it here is lock-free and cannot
        // stall the audio thread.
        const float l = juce::jlimit (0.0f, 1.0f, processor.getMeterLevel (0));
        const float r = juce::jlimit (0.0f, 1.0f, processor.getMeterLevel (1));

        left [(size_t) writeIndex] = l;
        right[(size_t) writeIndex] = r;

        writeIndex = (writeIndex + 1) % historySize;

        peakSeen = juce::jmax (peakSeen * 0.999f, l, r);

        if (onFrame != nullptr)
            onFrame();
    }

    float SynthVisualizer::getCurrentLevel() const noexcept
    {
        const int newest = (writeIndex + historySize - 1) % historySize;
        return juce::jmax (left[(size_t) newest], right[(size_t) newest]);
    }

    bool SynthVisualizer::hasSignal() const noexcept
    {
        return peakSeen > silenceFloor;
    }

    void SynthVisualizer::fillEnvelope (std::vector<std::pair<float, float>>& destination,
                                        int numColumns) const
    {
        numColumns = juce::jmax (1, numColumns);

        if ((int) destination.size() != numColumns)
            destination.resize ((size_t) numColumns);

        const double framesPerColumn = (double) historySize / (double) numColumns;

        for (int c = 0; c < numColumns; ++c)
        {
            // Oldest frame first, so the newest sits at the right edge and the
            // display scrolls leftwards the way a tape transport would.
            const double a = (double) c * framesPerColumn;
            const double b = a + framesPerColumn;

            int i0 = (int) std::floor (a);
            int i1 = (int) std::ceil (b);

            i0 = juce::jlimit (0, historySize - 1, i0);
            i1 = juce::jlimit (i0 + 1, historySize, i1);

            float peakL = 0.0f, peakR = 0.0f;

            if (framesPerColumn >= 1.0)
            {
                for (int i = i0; i < i1; ++i)
                {
                    const size_t idx = (size_t) ((writeIndex + i) % historySize);
                    peakL = juce::jmax (peakL, left[idx]);
                    peakR = juce::jmax (peakR, right[idx]);
                }
            }
            else
            {
                // More columns than frames: interpolate rather than stair-step,
                // so a slow meter still reads as a continuous envelope.
                const double centre = (a + b) * 0.5 - 0.5;
                const int    ia     = juce::jlimit (0, historySize - 1, (int) std::floor (centre));
                const int    ib     = juce::jlimit (0, historySize - 1, ia + 1);
                const float  t      = (float) juce::jlimit (0.0, 1.0, centre - std::floor (centre));

                const size_t sa = (size_t) ((writeIndex + ia) % historySize);
                const size_t sb = (size_t) ((writeIndex + ib) % historySize);

                peakL = left [sa] + (left [sb] - left [sa]) * t;
                peakR = right[sa] + (right[sb] - right[sa]) * t;
            }

            destination[(size_t) c] = { -peakR, peakL };
        }
    }
}
