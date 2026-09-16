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
        This is *not* a sample-accurate oscilloscope.  NacarProcessor exposes
        exactly one window onto the audio it produces:

            float getMeterLevel (int channel) const noexcept;

        which is the decayed peak of the last processed block, per channel.
        SynthEngine::getActiveVoiceCount() and getLastPeak() exist, but the
        `SynthEngine synth` member of NacarProcessor is private and there is no
        accessor for it, so from the UI they are unreachable.  PluginProcessor.h
        is frozen, so nothing is added to it here.

        A true scope would need a lock-free FIFO in the processor - a
        single-producer / single-consumer ring the audio thread writes samples
        into and the editor drains on its timer.  The frozen header exposes no
        such FIFO in this phase.  When one lands (alongside the sample engine in
        Phases 18-19) this class is the only thing that has to change: the ring
        below simply gets filled from the FIFO instead of from the meter, and
        every consumer of fillEnvelope() keeps working unchanged.

        Until then: a 30 Hz poll of the two meter channels, kept in a fixed-size
        ring.  Genuinely the instrument's output level over time - accurate,
        just coarse.  Roughly 34 seconds of history at the default ring size.

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

        /** Ring length in frames.  1024 / 30 Hz is about 34 seconds of history,
            and comfortably exceeds the 840 px width of layout::vp::waveField so
            the main field never has to invent columns it has no data for. */
        static constexpr int historySize = 1024;

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
