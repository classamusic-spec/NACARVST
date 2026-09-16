#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <functional>
#include <utility>
#include <vector>

#include "../../Plugin/PluginProcessor.h"

namespace nacar::ui
{
    /**
        A rolling display envelope of the instrument's recent output.

        The optical viewport is the heart of NACAR and it must never be an empty
        black rectangle.  V1 Phase 2 has no sample engine, so with nothing loaded
        there is no waveform to draw - but the synth is running, and its output
        level over time is something true we can show.

        ---------------------------------------------------------------------
        WHAT THIS IS, HONESTLY
        ---------------------------------------------------------------------
        It is a real waveform, and it is decimated.

        NacarProcessor keeps a lock-free ring of the instrument's output.  The
        audio thread writes one frame per `getScopeDecimation()` samples, and
        each frame is the *peak* of the samples it covers rather than one of
        them - a scope that decimated by dropping samples would miss exactly
        the transients a producer looks at.  The editor copies the whole ring
        on its timer through an acquire load.

        So the shape below is genuinely the instrument's output over the last
        three quarters of a second, at the resolution the ring holds.  What it
        is not is a per-sample oscilloscope: at 48 kHz each frame covers about
        eighteen samples, so a single cycle of a high note is one frame wide.
        Reading individual cycles would need the ring to carry raw samples and
        the field to zoom into it, which is a different feature.

        The envelope is handed to WaveformView, which renders it in exactly the
        same violet visual language as an audio waveform (mirrored fill, crest
        line, centre rule), so the viewport reads as one instrument rather than
        as a waveform editor with a debug plot bolted on.  WaveformView labels
        it as the synth's live output, never as a loaded sample.
    */
    class SynthVisualizer : private juce::Timer
    {
    public:
        explicit SynthVisualizer (NacarProcessor&);
        ~SynthVisualizer() override;

        /** Poll rate.  Matches the editor's own repaint timer. */
        static constexpr int refreshHz = 30;

        /** Ring length in frames.  Mirrors the processor's scope exactly, so a
            frame here is a frame there and no resampling happens on the way in.
            It also comfortably exceeds the 840 px width of
            layout::vp::waveField, so the field never has to invent columns it
            has no data for. */
        static constexpr int historySize = NacarProcessor::scopeSize;

        void start();
        void stop();
        bool isRunning() const noexcept { return isTimerRunning(); }

        /** Resamples the ring into `numColumns` min/max pairs, oldest first.

            The pair is { -right, +left }: the left channel drives the upper half
            of the mirrored envelope and the right channel the lower half, so the
            shape is genuinely stereo rather than a duplicated mono trace.
            Values are in -1..1 and the vector is resized to numColumns. */
        void fillEnvelope (std::vector<std::pair<float, float>>& destination,
                           int numColumns) const;

        /** The most recent frame, as max (left, right). */
        float getCurrentLevel() const noexcept;

        /** True once any frame in the ring is above the silence floor. */
        bool hasSignal() const noexcept;

        /** Called on the message thread after every push. */
        std::function<void()> onFrame;

    private:
        void timerCallback() override;

        /** Below this a frame counts as silence for hasSignal(). */
        static constexpr float silenceFloor = 0.0008f;

        NacarProcessor& processor;

        std::array<float, (size_t) historySize> left  {};
        std::array<float, (size_t) historySize> right {};
        int writeIndex = 0;

        float peakSeen = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthVisualizer)
    };
}
