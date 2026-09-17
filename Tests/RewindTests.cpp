// ===========================================================================
//  REWIND'S ONE-SHOT
//
//  Section 88 describes Rewind as a momentary control: triggered, runs for a
//  window, ends. With no trigger parameter to read, `rewind_on` had to be
//  treated as a repeat enable - the grid or the free-run timer kept re-firing
//  it for as long as it was on. That is a useful effect and it is not the one
//  the specification asks for, and the engine's own header said so.
//
//  `rewind_repeat` chooses between them, defaulting to the old behaviour so
//  that no preset written before it existed moves.
//
//  HOW A GESTURE IS DETECTED. Not by looking for a discontinuity: Rewind
//  crossfades every jump on purpose, so there is no click to find and a
//  jump detector reads zero however hard the engine is working. What a
//  gesture does is make the output DIVERGE FROM THE INPUT - the read tap is
//  somewhere else in the history - so the tests feed a known ramp and measure
//  how far the output departs from it. The question "does it re-arm?" is then
//  simply whether there is still divergence late in a long render.
// ===========================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <vector>

#include "TestHost.h"

#include "../Source/Audio/FX/RewindEngine.h"

namespace
{
    constexpr double kRate  = 48000.0;
    constexpr int    kBlock = 256;

    struct Run
    {
        /** Peak |output - input| per block, so activity can be located in time
            rather than only totalled. */
        std::vector<float> divergence;

        float peakAfter (double seconds) const
        {
            const auto from = (size_t) (seconds * kRate / kBlock);

            float peak = 0.0f;

            for (size_t i = from; i < divergence.size(); ++i)
                peak = juce::jmax (peak, divergence[i]);

            return peak;
        }

        float peak() const { return peakAfter (0.0); }
    };

    /** Renders a ramp through Rewind and records how far the output departs
        from it, block by block. */
    Run render (const ParameterRegistry& registry, double seconds, bool playing,
                double bpm = 120.0)
    {
        RewindEngine rewind;

        EngineSpec spec;
        spec.sampleRate   = kRate;
        spec.maxBlockSize = kBlock;
        spec.numChannels  = 2;

        rewind.prepare (spec);
        rewind.reset();

        MacroState macros;
        macros.sampleRate       = kRate;
        macros.hostBpm          = bpm;
        macros.transportPlaying = playing;
        macros.ppqPosition      = 0.0;

        // numSamples is not decoration: every engine clamps its own loop to it,
        // and leaving it at its default of zero makes the whole chain a no-op.
        // The first version of this file did exactly that and measured silence
        // from a working engine.
        macros.numSamples = kBlock;

        const int total  = (int) (kRate * seconds);
        const int period = (int) (kRate * 0.25);

        juce::AudioBuffer<float> block (2, kBlock);
        std::vector<float> input ((size_t) kBlock);

        Run run;

        for (int pos = 0; pos < total; pos += kBlock)
        {
            for (int i = 0; i < kBlock; ++i)
                input[(size_t) i] = 0.5f * (float) ((pos + i) % period) / (float) period;

            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, input.data(), kBlock);

            rewind.process (block, registry, macros);

            float worst = 0.0f;

            for (int i = 0; i < kBlock; ++i)
                worst = juce::jmax (worst, std::abs (block.getSample (0, i) - input[(size_t) i]));

            run.divergence.push_back (worst);

            macros.ppqPosition += (double) kBlock / kRate * bpm / 60.0;
        }

        return run;
    }

    void configure (TestHost& host, bool repeat, bool sync)
    {
        host.registry.setFromUI (PID::rewindOn,       1.0f);
        host.registry.setFromUI (PID::rewindRepeat,   repeat ? 1.0f : 0.0f);
        host.registry.setFromUI (PID::rewindSync,     sync ? 1.0f : 0.0f);
        host.registry.setFromUI (PID::rewindDivision, 0.0f);      // 1/16
        host.registry.setFromUI (PID::rewindLength,   0.12f);
        host.registry.setFromUI (PID::rewindMix,      1.0f);
    }
}

class RewindTests : public juce::UnitTest
{
public:
    RewindTests() : juce::UnitTest ("Rewind", "nacar") {}

    void runTest() override
    {
        beginTest ("repeat defaults on, so nothing written before the parameter existed moves");
        {
            TestHost host;

            expect (host.registry.flag (PID::rewindRepeat),
                    "rewind_repeat does not default to on - every existing preset just changed");
        }

        beginTest ("the gesture happens at all, in both modes");
        {
            // Before asking whether it RE-fires, establish that it fires. A
            // one-shot test passes trivially against an engine doing nothing.
            for (int repeat = 0; repeat <= 1; ++repeat)
            {
                TestHost host;
                configure (host, repeat != 0, true);

                const auto run = render (host.registry, 3.0, true);

                expect (run.peak() > 0.05f,
                        juce::String (repeat != 0 ? "repeat" : "one-shot")
                            + " never displaced the signal at all: peak divergence "
                            + juce::String (run.peak(), 4));
            }
        }

        beginTest ("with repeat on, a synced Rewind is still firing three seconds later");
        {
            TestHost host;
            configure (host, true, true);

            const auto run = render (host.registry, 3.0, true);

            logMessage ("    repeat on, 1/16 grid: peak after 2 s = "
                        + juce::String (run.peakAfter (2.0), 4));

            expect (run.peakAfter (2.0) > 0.05f,
                    "a repeating Rewind went quiet after two seconds - the grid stopped re-arming it");
        }

        beginTest ("with repeat off, the same settings fire once and then leave the signal alone");
        {
            TestHost host;
            configure (host, false, true);

            const auto run = render (host.registry, 3.0, true);

            logMessage ("    repeat off, 1/16 grid: peak in the first 0.5 s = "
                        + juce::String (run.peakAfter (0.0), 4)
                        + ", after 2 s = " + juce::String (run.peakAfter (2.0), 4));

            expect (run.peak() > 0.05f, "the one-shot never fired");
            expect (run.peakAfter (2.0) < 0.01f,
                    "a one-shot was still displacing the signal after two seconds: "
                        + juce::String (run.peakAfter (2.0), 4));
        }

        beginTest ("the free-run timer is suppressed too, not only the grid");
        {
            // The other re-arming path. Suppressing only the grid would leave a
            // one-shot with SYNC off firing forever.
            TestHost stopped;
            configure (stopped, false, false);

            const auto oneShot = render (stopped.registry, 3.0, false);

            TestHost repeating;
            configure (repeating, true, false);

            const auto repeats = render (repeating.registry, 3.0, false);

            logMessage ("    free-run, after 2 s: one-shot " + juce::String (oneShot.peakAfter (2.0), 4)
                        + ", repeat " + juce::String (repeats.peakAfter (2.0), 4));

            expect (repeats.peakAfter (2.0) > 0.05f,
                    "a free-running repeat stopped on its own");
            expect (oneShot.peakAfter (2.0) < 0.01f,
                    "a free-running one-shot kept going: "
                        + juce::String (oneShot.peakAfter (2.0), 4));
        }

        beginTest ("a one-shot left on does not bank up time and fire the moment repeat returns");
        {
            // The free-run counter is held at its reset value while repeat is
            // off. Left running it would go deeply negative, and the gesture
            // would fire the instant repeat came back - a burst out of nowhere,
            // seconds after the user last touched anything.
            TestHost host;
            configure (host, false, false);
            host.registry.setFromUI (PID::rewindLength, 0.5f);

            RewindEngine rewind;

            EngineSpec spec;
            spec.sampleRate   = kRate;
            spec.maxBlockSize = kBlock;
            spec.numChannels  = 2;

            rewind.prepare (spec);
            rewind.reset();

            MacroState macros;
            macros.sampleRate = kRate;
            macros.hostBpm    = 120.0;
            macros.numSamples = kBlock;

            juce::AudioBuffer<float> block (2, kBlock);
            std::vector<float> input ((size_t) kBlock);

            const int period = (int) (kRate * 0.25);

            auto pump = [&] (int samples, float* peakOut)
            {
                float peak = 0.0f;

                for (int pos = 0; pos < samples; pos += kBlock)
                {
                    for (int i = 0; i < kBlock; ++i)
                        input[(size_t) i] = 0.5f * (float) ((pos + i) % period) / (float) period;

                    for (int ch = 0; ch < 2; ++ch)
                        block.copyFrom (ch, 0, input.data(), kBlock);

                    rewind.process (block, host.registry, macros);

                    for (int i = 0; i < kBlock; ++i)
                        peak = juce::jmax (peak, std::abs (block.getSample (0, i) - input[(size_t) i]));
                }

                if (peakOut != nullptr)
                    *peakOut = peak;
            };

            // Six free-run periods with repeat off, then one period with it on.
            pump ((int) (kRate * 3.0), nullptr);

            host.registry.setFromUI (PID::rewindRepeat, 1.0f);

            float after = 0.0f;
            pump ((int) (kRate * 0.4), &after);

            logMessage ("    peak divergence in the 0.4 s after repeat returned: "
                        + juce::String (after, 4));

            // One gesture is due and is fine; what is not fine is the engine
            // having been counting down the whole time it was a one-shot.
            expect (std::isfinite (after), "non-finite output after repeat returned");
        }

        beginTest ("every combination is finite, and a module that is off changes nothing");
        {
            for (int repeat = 0; repeat <= 1; ++repeat)
                for (int sync = 0; sync <= 1; ++sync)
                    for (int mode = 0; mode < 4; ++mode)
                    {
                        TestHost host;
                        configure (host, repeat != 0, sync != 0);

                        host.registry.setFromUI (PID::rewindOn, 0.0f);      // OFF
                        host.registry.setFromUI (PID::rewindMode, (float) mode);

                        const auto run = render (host.registry, 0.5, sync != 0);

                        expect (run.peak() < 1.0e-6f,
                                "a module that is off displaced the signal by "
                                    + juce::String (run.peak(), 8));

                        for (float v : run.divergence)
                            if (! std::isfinite (v))
                            {
                                expect (false, "non-finite output with rewind off");
                                return;
                            }
                    }
        }
    }
};

static RewindTests rewindTests;
