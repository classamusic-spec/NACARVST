/*
    ===========================================================================
      THE STEP SEQUENCER
    ===========================================================================

    Source/Audio/Modulation/SequencerEngine advances four lanes of sixteen
    steps against the host transport; NacarEngine applies each lane's current
    step to its target parameter through ParameterRegistry's modulation
    overlay.  The SEQ page edits and persists the tree both of them read.

    These tests go after the things that would be DEFECTS rather than the
    things that are obvious:

      - a lane actually moves its target, measured through `registry.raw()`
        and not through the sequencer's own opinion of itself;
      - lanes of different lengths drift against each other and realign at the
        least common multiple, because per-lane lengths that do not drift are
        four copies of one lane;
      - the same transport position produces the same step every time, so
        pressing play twice is not two different performances;
      - a gate of 0 HOLDS the previous value rather than zeroing it, and holds
        it by looking backwards through the pattern rather than by remembering,
        so the hold is the same wherever playback started;
      - a disabled lane writes nothing at all, and switching a lane off gives
        the parameter back to the knob instead of freezing it at the last step;
      - a lane whose target names a parameter this build does not have is inert
        rather than fatal;
      - a step boundary landing in the middle of a block is not lost.

    Nobody has listened to any of this.  Everything below is a measurement of
    numbers the code produces, not a judgement about how it sounds.
    ===========================================================================
*/

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "../Source/Plugin/ParameterRegistry.h"
#include "../Source/Plugin/StateManager.h"
#include "../Source/Audio/NacarEngine.h"
#include "../Source/Audio/Modulation/SequencerEngine.h"

#include "TestHost.h"

namespace
{
    constexpr int kSixteenth = 2;       ///< seq::divisions index for 1/16
    constexpr double kSixteenthBeats = 0.25;

    /** One SEQLANE, written exactly the way SeqPage writes one. */
    juce::ValueTree makeLane (const juce::String& targetId, bool enabled, int length,
                              const juce::String& values, const juce::String& gates)
    {
        juce::ValueTree lane (ids::SEQLANE);

        lane.setProperty (ids::laneName,    "LANE", nullptr);
        lane.setProperty (ids::laneTarget,  targetId, nullptr);
        lane.setProperty (ids::laneEnabled, enabled, nullptr);
        lane.setProperty (ids::laneLength,  length, nullptr);
        lane.setProperty (ids::laneValues,  values, nullptr);
        lane.setProperty (ids::laneGates,   gates, nullptr);

        return lane;
    }

    juce::ValueTree makeSequencer (int division, const juce::Array<juce::ValueTree>& lanes)
    {
        juce::ValueTree seqTree (ids::SEQUENCER);
        seqTree.setProperty (ids::seqDivision, division, nullptr);

        for (const auto& lane : lanes)
            seqTree.appendChild (lane, nullptr);

        return seqTree;
    }

    /** Sixteen values, `first` on step 0 and rising to `last` on step 15. */
    juce::String rampValues (float first, float last)
    {
        juce::StringArray parts;

        for (int s = 0; s < 16; ++s)
            parts.add (juce::String (first + (last - first) * (float) s / 15.0f, 3));

        return parts.joinIntoString (",");
    }

    juce::String sameValues (float v)
    {
        juce::StringArray parts;

        for (int s = 0; s < 16; ++s)
            parts.add (juce::String (v, 3));

        return parts.joinIntoString (",");
    }

    juce::String allGates (bool on)
    {
        return juce::String::repeatedString (on ? "1" : "0", 16);
    }

    mod::Clock clockAt (double ppq, bool playing, double bpm = 120.0,
                        double sampleRate = 48000.0)
    {
        mod::Clock c;
        c.sampleRate  = sampleRate;
        c.bpm         = bpm;
        c.ppqPosition = ppq;
        c.playing     = playing;
        return c;
    }

    /** Renders `numBlocks` blocks with the transport pinned at one position.
        The sequencer reads position, not elapsed time, so this is a perfectly
        good way to ask "what does it do at bar X". */
    void renderAt (NacarEngine& engine, const ParameterRegistry& params,
                   double ppq, int numBlocks = 2, int blockSize = 256)
    {
        juce::AudioBuffer<float> block (2, blockSize);

        TransportInfo transport;
        transport.bpm = 120.0;
        transport.ppqPosition = ppq;
        transport.playing = true;

        for (int b = 0; b < numBlocks; ++b)
        {
            block.clear();
            juce::MidiBuffer midi;
            engine.process (block, midi, params, transport);
        }
    }
}

// ===========================================================================
struct SequencerTests : juce::UnitTest
{
    SequencerTests() : juce::UnitTest ("Step sequencer", "nacar") {}

    void runTest() override
    {
        // -------------------------------------------------------------------
        beginTest ("a lane walks its steps once per division");
        {
            SequencerEngine seq;
            seq.rebuildFromTree (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 16,
                            rampValues (0.0f, 1.0f), allGates (true)) }));

            for (int t = 0; t < 40; ++t)
            {
                seq.beginBlock (1, clockAt ((double) t * kSixteenthBeats, true));

                expectEquals (seq.currentStep (0), t % 16,
                              "lane 0 is not on the step its position says");
            }

            expectEquals (seq.numApplied(), 1, "an enabled, gated, targeted lane wrote nothing");
        }

        // -------------------------------------------------------------------
        beginTest ("lanes of different lengths drift, and realign at the common multiple");
        {
            // The whole musical point of per-lane lengths.  Three against four
            // must disagree for eleven steps and agree again on the twelfth; if
            // they never disagree the length control is decoration.
            SequencerEngine seq;
            seq.rebuildFromTree (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff),  true, 3,
                            rampValues (0.0f, 1.0f), allGates (true)),
                  makeLane (ParameterRegistry::idOf (PID::filter2Cutoff), true, 4,
                            rampValues (0.0f, 1.0f), allGates (true)) }));

            int disagreements = 0;

            for (int t = 0; t <= 12; ++t)
            {
                seq.beginBlock (1, clockAt ((double) t * kSixteenthBeats, true));

                const int three = seq.currentStep (0);
                const int four  = seq.currentStep (1);

                expectEquals (three, t % 3, "the three-step lane lost its cycle");
                expectEquals (four,  t % 4, "the four-step lane lost its cycle");

                if (t > 0 && t < 12 && three != four)
                    ++disagreements;

                if (t == 12)
                {
                    expectEquals (three, 0, "the three-step lane did not realign at 12");
                    expectEquals (four,  0, "the four-step lane did not realign at 12");
                }
            }

            logMessage ("    3 against 4 disagreed on " + juce::String (disagreements)
                            + " of the 11 steps between the realignments");

            expect (disagreements >= 8, "lanes of different lengths are not drifting");
        }

        // -------------------------------------------------------------------
        beginTest ("the same transport position always produces the same step");
        {
            // Pressing play twice over the same bar must not be two different
            // performances.  Run the pattern forwards, then jump back and
            // forwards again in a different order and with a different block
            // length, and demand the same answers.
            SequencerEngine seq;
            seq.rebuildFromTree (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 7,
                            rampValues (0.05f, 0.95f), "1011010110110101") }));

            int   steps[32];
            float values[32];

            for (int t = 0; t < 32; ++t)
            {
                seq.beginBlock (256, clockAt ((double) t * kSixteenthBeats, true));
                steps[t]  = seq.currentStep (0);
                values[t] = seq.numApplied() > 0 ? seq.applied (0).value : -1.0f;
            }

            // Backwards, and with a different block size: neither may matter.
            for (int t = 31; t >= 0; --t)
            {
                seq.beginBlock (64, clockAt ((double) t * kSixteenthBeats, true));

                expectEquals (seq.currentStep (0), steps[t],
                              "the step at ppq " + juce::String (t * 0.25)
                                  + " changed on a second pass");

                const float v = seq.numApplied() > 0 ? seq.applied (0).value : -1.0f;
                expectWithinAbsoluteError (v, values[t], 1.0e-6f);
            }

            // And a fresh engine, which is what pressing play after a reload is.
            SequencerEngine fresh;
            fresh.rebuildFromTree (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 7,
                            rampValues (0.05f, 0.95f), "1011010110110101") }));

            for (int t = 0; t < 32; ++t)
            {
                fresh.beginBlock (256, clockAt ((double) t * kSixteenthBeats, true));
                expectEquals (fresh.currentStep (0), steps[t],
                              "a fresh engine plays a different pattern");
            }
        }

        // -------------------------------------------------------------------
        beginTest ("a gate of 0 holds the previous value rather than zeroing it");
        {
            // A gate that zeroed would slam a cutoff shut on every rest.  The
            // pattern below is "loud, rest, quiet, rest": the rests must read
            // back as the value before them, not as zero.
            juce::StringArray values;
            for (int s = 0; s < 16; ++s)
                values.add ("0.500");

            values.set (0, "0.900");
            values.set (2, "0.100");

            SequencerEngine seq;
            seq.rebuildFromTree (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 4,
                            values.joinIntoString (","), "1010000000000000") }));

            const float expected[4] = { 0.9f, 0.9f, 0.1f, 0.1f };

            for (int t = 0; t < 8; ++t)
            {
                seq.beginBlock (1, clockAt ((double) t * kSixteenthBeats, true));

                expectEquals (seq.numApplied(), 1, "the lane stopped writing on a rest");
                expectWithinAbsoluteError (seq.applied (0).value, expected[t % 4], 1.0e-4f);
            }
        }

        beginTest ("the hold wraps backwards inside the lane, so it has no history");
        {
            // Step 0's gate is off and the only gate in the cycle is step 1.
            // A sequencer that REMEMBERED would output nothing on the very
            // first step of the very first bar; one that looks backwards
            // through the pattern outputs step 1's value, which is what the
            // lane would have been holding had it been running all along.
            juce::StringArray values;
            for (int s = 0; s < 16; ++s)
                values.add ("0.200");

            values.set (1, "0.700");

            SequencerEngine seq;
            seq.rebuildFromTree (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 4,
                            values.joinIntoString (","), "0100000000000000") }));

            seq.beginBlock (1, clockAt (0.0, true));

            expectEquals (seq.numApplied(), 1, "a lane with one gate wrote nothing on step 0");
            expectWithinAbsoluteError (seq.applied (0).value, 0.7f, 1.0e-4f);
        }

        beginTest ("a lane with no gates at all writes nothing");
        {
            // The default pattern the SEQ page creates.  It must not pin a
            // parameter anywhere just because a lane has been switched on.
            SequencerEngine seq;
            seq.rebuildFromTree (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 16,
                            sameValues (0.5f), allGates (false)) }));

            seq.beginBlock (256, clockAt (0.0, true));
            expectEquals (seq.numApplied(), 0, "an ungated lane claimed a parameter");
        }

        // -------------------------------------------------------------------
        beginTest ("a target this build does not have is ignored, not fatal");
        {
            SequencerEngine seq;
            seq.rebuildFromTree (makeSequencer (kSixteenth,
                { makeLane ("a_parameter_from_some_later_build", true, 16,
                            sameValues (0.8f), allGates (true)),
                  makeLane ("", true, 16, sameValues (0.8f), allGates (true)),
                  makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 16,
                            sameValues (0.8f), allGates (true)) }));

            seq.beginBlock (256, clockAt (0.0, true));

            expect (seq.livePattern().lanes[0].target == PID::count,
                    "an unknown string ID resolved to a real parameter");
            expectEquals (seq.numApplied(), 1,
                          "the unknown targets were not skipped, or the good one was");
            expect (seq.applied (0).target == PID::filterCutoff);
            expectEquals (seq.applied (0).lane, 2);
        }

        beginTest ("two lanes on one parameter: the lower-numbered one owns it");
        {
            SequencerEngine seq;
            seq.rebuildFromTree (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 16,
                            sameValues (0.25f), allGates (true)),
                  makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 16,
                            sameValues (0.75f), allGates (true)) }));

            seq.beginBlock (256, clockAt (0.0, true));

            expectEquals (seq.numApplied(), 1, "one parameter took two absolute positions");
            expectEquals (seq.applied (0).lane, 0);
            expectWithinAbsoluteError (seq.applied (0).value, 0.25f, 1.0e-4f);
        }

        // -------------------------------------------------------------------
        beginTest ("a step boundary inside a block is not lost");
        {
            // 1/16 at 120 bpm is 6000 samples at 48 kHz; a 700-sample block
            // never lands on a boundary, so every step change happens in the
            // middle of one.  The failure this catches is a lane that skips a
            // step, or repeats one, because the boundary fell between blocks.
            SequencerEngine seq;
            seq.rebuildFromTree (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 16,
                            rampValues (0.0f, 1.0f), allGates (true)) }));

            constexpr int blockSize = 700;
            const double bps = 120.0 / (60.0 * 48000.0);

            double ppq = 0.0;
            int previous = -1;
            int transitions = 0;
            bool seen[16] = {};
            int blocksOnThisStep = 0;

            for (int b = 0; b < 300; ++b)
            {
                seq.beginBlock (blockSize, clockAt (ppq, true));

                const int step = seq.currentStep (0);
                seen[step] = true;

                if (step != previous)
                {
                    if (previous >= 0)
                    {
                        expectEquals (step, (previous + 1) % 16,
                                      "the lane jumped from step " + juce::String (previous)
                                          + " to step " + juce::String (step));

                        // 6000 / 700 is 8.57, so a step lasts eight or nine
                        // blocks.  Anything else means it is being sampled
                        // against something other than the transport.
                        expect (blocksOnThisStep == 8 || blocksOnThisStep == 9,
                                "a step lasted " + juce::String (blocksOnThisStep) + " blocks");

                        ++transitions;
                    }

                    previous = step;
                    blocksOnThisStep = 0;
                }

                ++blocksOnThisStep;
                ppq += bps * (double) blockSize;
            }

            for (int s = 0; s < 16; ++s)
                expect (seen[s], "step " + juce::String (s) + " was never played");

            logMessage ("    " + juce::String (transitions)
                            + " step boundaries crossed mid-block, none lost");
        }

        // -------------------------------------------------------------------
        beginTest ("a stopped transport free-runs at the tempo-derived rate");
        {
            // Pulse and the synced LFOs already establish this: a user editing
            // a pattern with the transport stopped must still see it move.
            SequencerEngine seq;
            seq.rebuildFromTree (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 16,
                            rampValues (0.0f, 1.0f), allGates (true)) }));

            // 6000 samples is exactly one 1/16 at 120 bpm / 48 kHz, and the
            // position never moves - only the free-running counter does.
            for (int t = 0; t < 20; ++t)
            {
                seq.beginBlock (6000, clockAt (0.0, false));
                expectEquals (seq.currentStep (0), t % 16,
                              "the free-running clock did not advance one step per period");
            }
        }

        // -------------------------------------------------------------------
        //  End to end, through the real overlay.
        // -------------------------------------------------------------------
        beginTest ("a lane moves its target parameter, measured through raw()");
        {
            // The whole point.  Everything above could pass while the lane
            // never reached a parameter at all.
            TestHost host;
            host.registry.setFromUI (PID::filterCutoff, 8000.0f);

            const float userNorm = host.registry.normalisedUserValue (PID::filterCutoff);

            NacarEngine engine;
            engine.prepare (48000.0, 256, 2);
            engine.rebuildSequencer (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 16,
                            rampValues (0.0f, 1.0f), allGates (true)) }));

            // Step 3 of sixteen on a 0..1 ramp is 3/15.
            renderAt (engine, host.registry, 3.0 * kSixteenthBeats);

            expectEquals (engine.getSequencerStep (0), 3, "the engine is not on step 3");

            expectWithinAbsoluteError (host.registry.normalised (PID::filterCutoff),
                                       3.0f / 15.0f, 0.002f);

            logMessage ("    cutoff moved from " + juce::String (host.registry.userValue (PID::filterCutoff), 1)
                            + " Hz to " + juce::String (host.registry.raw (PID::filterCutoff), 1) + " Hz");

            // A step is ABSOLUTE, so a different step is a different position
            // and not a different offset from the same place.
            renderAt (engine, host.registry, 12.0 * kSixteenthBeats);
            expectWithinAbsoluteError (host.registry.normalised (PID::filterCutoff),
                                       12.0f / 15.0f, 0.002f);

            // And the user's own value never moved: a preset saved while the
            // sequencer runs must save the knob, not the step.
            expectWithinAbsoluteError (host.registry.userValue (PID::filterCutoff), 8000.0f, 0.5f);
            expectWithinAbsoluteError (host.registry.normalisedUserValue (PID::filterCutoff),
                                       userNorm, 1.0e-5f);
        }

        beginTest ("a disabled lane writes nothing, and switching one off gives the knob back");
        {
            // The defect this catches is a parameter left frozen at the last
            // step the sequencer played: a knob that has silently stopped
            // working, with nothing on screen to explain why.
            TestHost host;
            host.registry.setFromUI (PID::filterCutoff, 8000.0f);

            NacarEngine engine;
            engine.prepare (48000.0, 256, 2);

            engine.rebuildSequencer (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), false, 16,
                            sameValues (0.05f), allGates (true)) }));

            renderAt (engine, host.registry, 0.0);

            expectWithinAbsoluteError (host.registry.raw (PID::filterCutoff),
                                       host.registry.userValue (PID::filterCutoff), 0.001f);

            // On: it must actually take hold, or the release below proves
            // nothing.
            engine.rebuildSequencer (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 16,
                            sameValues (0.05f), allGates (true)) }));

            renderAt (engine, host.registry, 0.0);

            expect (host.registry.raw (PID::filterCutoff) < 1000.0f,
                    "the lane never took hold, so the release cannot be tested");

            // Off again.
            engine.rebuildSequencer (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), false, 16,
                            sameValues (0.05f), allGates (true)) }));

            renderAt (engine, host.registry, 0.0);

            expectWithinAbsoluteError (host.registry.raw (PID::filterCutoff), 8000.0f, 0.5f);

            // And an empty tree - a preset with no sequencer in it - is the
            // same release by a different route.
            engine.rebuildSequencer (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 16,
                            sameValues (0.05f), allGates (true)) }));
            renderAt (engine, host.registry, 0.0);
            expect (host.registry.raw (PID::filterCutoff) < 1000.0f);

            engine.rebuildSequencer (juce::ValueTree());
            renderAt (engine, host.registry, 0.0);
            expectWithinAbsoluteError (host.registry.raw (PID::filterCutoff), 8000.0f, 0.5f);
        }

        beginTest ("a lane and a matrix routing onto one parameter compose by summing");
        {
            // The documented rule: the lane names an absolute position and the
            // matrix offsets from there.  MEMORY pinned at 1.0 with a depth of
            // 0.25 is a fixed +0.25 of normalised range, so a lane sitting at
            // 0.50 must read back at 0.75 - the routing neither replaces the
            // lane nor is silently discarded by it.
            TestHost host;

            // 2 kHz rather than 8 kHz on purpose: the cutoff's skew puts 8 kHz
            // at 0.795 of normalised range, and 0.795 + 0.25 clamps at the top,
            // which would prove the clamp rather than the sum.
            host.registry.setFromUI (PID::filterCutoff, 2000.0f);
            host.registry.setFromUI (PID::macroMemory, 1.0f);

            juce::ValueTree matrix (ids::MODMATRIX);
            juce::ValueTree slot (ids::MODSLOT);
            slot.setProperty (ids::modSource,  "MEMORY", nullptr);
            slot.setProperty (ids::modTarget,  ParameterRegistry::idOf (PID::filterCutoff), nullptr);
            slot.setProperty (ids::modDepth,   0.25f, nullptr);
            slot.setProperty (ids::modEnabled, true, nullptr);
            matrix.appendChild (slot, nullptr);

            NacarEngine engine;
            engine.prepare (48000.0, 256, 2);

            engine.rebuildSequencer (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 16,
                            sameValues (0.5f), allGates (true)) }));

            // The lane alone.
            renderAt (engine, host.registry, 0.0);
            expectWithinAbsoluteError (host.registry.normalised (PID::filterCutoff), 0.5f, 0.002f);

            // The lane plus the routing.
            engine.rebuildModMatrix (matrix);
            renderAt (engine, host.registry, 0.0);
            expectWithinAbsoluteError (host.registry.normalised (PID::filterCutoff), 0.75f, 0.002f);

            // The routing alone, so the 0.25 really is the routing's and not an
            // artefact of where the knob happens to sit.
            engine.rebuildSequencer (juce::ValueTree());
            renderAt (engine, host.registry, 0.0);

            const float userNorm = host.registry.normalisedUserValue (PID::filterCutoff);
            expect (userNorm + 0.25f < 1.0f, "the routing alone would have clamped");
            expectWithinAbsoluteError (host.registry.normalised (PID::filterCutoff),
                                       userNorm + 0.25f, 0.002f);
        }

        beginTest ("the sum of a lane and a routing still cannot leave the range");
        {
            // A lane already at the top plus a routing pushing further up is
            // the case where "they sum" has to stop being literally true.
            TestHost host;
            host.registry.setFromUI (PID::macroMemory, 1.0f);

            const auto& def = ParameterRegistry::definition (PID::filterCutoff);

            juce::ValueTree matrix (ids::MODMATRIX);
            juce::ValueTree slot (ids::MODSLOT);
            slot.setProperty (ids::modSource,  "MEMORY", nullptr);
            slot.setProperty (ids::modTarget,  ParameterRegistry::idOf (PID::filterCutoff), nullptr);
            slot.setProperty (ids::modDepth,   1.0f, nullptr);
            slot.setProperty (ids::modEnabled, true, nullptr);
            matrix.appendChild (slot, nullptr);

            NacarEngine engine;
            engine.prepare (48000.0, 256, 2);
            engine.rebuildModMatrix (matrix);
            engine.rebuildSequencer (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 16,
                            sameValues (1.0f), allGates (true)) }));

            renderAt (engine, host.registry, 0.0);

            expect (host.registry.raw (PID::filterCutoff) <= def.maxValue + 0.001f,
                    "lane plus routing pushed the cutoff past its maximum");

            // And the same downwards.
            slot.setProperty (ids::modDepth, -1.0f, nullptr);
            engine.rebuildModMatrix (matrix);
            engine.rebuildSequencer (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 16,
                            sameValues (0.0f), allGates (true)) }));

            renderAt (engine, host.registry, 0.0);

            expect (host.registry.raw (PID::filterCutoff) >= def.minValue - 0.001f,
                    "lane plus routing pushed the cutoff below its minimum");
        }

        beginTest ("a target that no longer exists cannot crash the audio thread");
        {
            TestHost host;

            NacarEngine engine;
            engine.prepare (48000.0, 256, 2);
            engine.rebuildSequencer (makeSequencer (kSixteenth,
                { makeLane ("a_parameter_from_some_later_build", true, 16,
                            sameValues (0.9f), allGates (true)) }));

            renderAt (engine, host.registry, 0.0, 8);

            expectWithinAbsoluteError (host.registry.raw (PID::filterCutoff),
                                       host.registry.userValue (PID::filterCutoff), 0.001f);
        }

        beginTest ("the division table and the identifiers have exactly one home");
        {
            // SeqPage used to declare both.  If either is ever declared twice
            // again the two copies will drift and a pattern will play at a
            // tempo the page does not print.
            expectEquals (seq::numDivisions, 9);
            expectWithinAbsoluteError (seq::beatsForDivision (kSixteenth), 0.25, 1.0e-12);
            expect (juce::String (seq::divisions[(size_t) kSixteenth].name) == "1/16");

            // Out-of-range indices come out of session files written by other
            // builds and must clamp rather than read off the end.
            expectWithinAbsoluteError (seq::beatsForDivision (-40), seq::divisions[0].beats, 1.0e-12);
            expectWithinAbsoluteError (seq::beatsForDivision (900),
                                       seq::divisions[(size_t) seq::numDivisions - 1].beats, 1.0e-12);

            expect (ids::SEQUENCER.toString() == "SEQUENCER");
            expect (ids::laneTarget.toString() == "laneTarget");
        }

        beginTest ("a lane length outside 1..16 is clamped rather than trusted");
        {
            SequencerEngine seq;
            seq.rebuildFromTree (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 0,
                            sameValues (0.5f), allGates (true)),
                  makeLane (ParameterRegistry::idOf (PID::filter2Cutoff), true, 99,
                            sameValues (0.5f), allGates (true)) }));

            // livePattern() is the AUDIO thread's copy, so it only exists once
            // a block has picked the publication up.  Reading it before that
            // would be reading the default pattern and calling it a pass.
            seq.beginBlock (1, clockAt (0.0, true));

            expectEquals (seq.livePattern().lanes[0].length, 1);
            expectEquals (seq.livePattern().lanes[1].length, 16);

            for (int t = 0; t < 20; ++t)
            {
                seq.beginBlock (1, clockAt ((double) t * kSixteenthBeats, true));
                expectEquals (seq.currentStep (0), 0, "a one-step lane left step 0");
                expectEquals (seq.currentStep (1), t % 16);
            }
        }

        beginTest ("a negative transport position walks backwards rather than sticking");
        {
            // Hosts report a negative ppq during a count-in.  Truncation
            // towards zero would make steps -1 and 0 both read as step 0 and
            // then jump, which is a count-in that does not line up with the bar.
            SequencerEngine seq;
            seq.rebuildFromTree (makeSequencer (kSixteenth,
                { makeLane (ParameterRegistry::idOf (PID::filterCutoff), true, 4,
                            rampValues (0.0f, 1.0f), allGates (true)) }));

            const int expected[5] = { 0, 3, 2, 1, 0 };     // ppq 0, -1/16, -2/16 ...

            for (int t = 0; t < 5; ++t)
            {
                seq.beginBlock (1, clockAt (-(double) t * kSixteenthBeats, true));
                expectEquals (seq.currentStep (0), expected[t],
                              "a negative position did not walk backwards");
            }
        }
    }
};

static SequencerTests sequencerTests;
