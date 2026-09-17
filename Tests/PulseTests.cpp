// ===========================================================================
//  PULSE'S FIVE DESTINATIONS
//
//  Pulse computes five envelopes from one trigger, with different time
//  scalings and different curvature, because the five things it ducks recover
//  at different speeds. PulseEngine.h states the design as a table:
//
//    destination  attack  release   why
//    VOLUME        1.00    1.00     the duck everyone knows
//    FILTER        0.80    0.70     HF masked first, back first
//    WIDTH         0.85    0.60     the image recovers fastest
//    SPACE         1.20    1.60     tails stay masked longest
//    MEMORY        1.00    1.30     a texture change, not a gate
//
//  All five were generated. Only VOLUME and WIDTH were ever READ: the chain's
//  output stage applies those two itself, and the three that belong to engines
//  - the chain filter, the reverb, Memory - were handed the VOLUME envelope
//  instead. The depth controls worked, so the feature looked finished; what
//  was thrown away was every shape in the table above, which is the entire
//  reason there are five envelopes rather than one.
//
//  These tests assert the table is real: that the three envelopes differ from
//  VOLUME in the direction the table claims, and that each engine now moves
//  with its own.
// ===========================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <vector>

#include "TestHost.h"

#include "../Source/Audio/Modulation/ModulationEngine.h"
#include "../Source/Audio/Modulation/PulseEngine.h"

namespace
{
    constexpr double kRate  = 48000.0;
    constexpr int    kBlock = 512;

    using Dest = PulseEngine::Destination;

    /** Runs Pulse alone and returns one destination's envelope over `blocks`,
        triggered once at the start. */
    std::vector<float> envelopeOf (Dest destination, int blocks,
                                   float attackSec = 0.004f, float releaseSec = 0.30f,
                                   int division = 8)
    {
        PulseEngine pulse;
        pulse.prepare (kRate, kBlock);

        PulseEngine::Settings settings;

        // `enabled` defaults to FALSE, and a disabled Pulse writes silence and
        // returns the envelopes to idle - which is correct, and which made the
        // first version of this file measure a peak of zero from all five
        // destinations and conclude the feature did not exist.
        settings.enabled    = true;
        settings.source     = 0;        // CLOCK
        settings.division   = division;
        settings.attackSec  = attackSec;
        settings.releaseSec = releaseSec;

        mod::Clock clock;
        clock.sampleRate = kRate;
        clock.bpm = 120.0;
        clock.playing = true;
        clock.ppqPosition = 0.0;

        std::vector<float> out;
        out.reserve ((size_t) (blocks * kBlock));

        for (int b = 0; b < blocks; ++b)
        {
            pulse.process (kBlock, settings, clock);

            const auto* env = pulse.envelope (destination);

            for (int i = 0; i < kBlock; ++i)
                out.push_back (env != nullptr ? env[i] : 0.0f);

            clock.ppqPosition += (double) kBlock / kRate * clock.bpm / 60.0;
        }

        return out;
    }

    /** How long, in samples, the envelope stays above `level` after its peak.
        A longer release holds the duck longer, which is what the table means. */
    int samplesAbove (const std::vector<float>& env, float level)
    {
        int count = 0;

        for (float v : env)
            if (v > level)
                ++count;

        return count;
    }

    float peakOf (const std::vector<float>& env)
    {
        float peak = 0.0f;

        for (float v : env)
            peak = juce::jmax (peak, v);

        return peak;
    }
}

class PulseTests : public juce::UnitTest
{
public:
    PulseTests() : juce::UnitTest ("Pulse", "nacar") {}

    void runTest() override
    {
        beginTest ("all five destinations produce an envelope at all");
        {
            for (auto d : { Dest::volume, Dest::filter, Dest::space, Dest::width, Dest::memory })
            {
                const auto env = envelopeOf (d, 8);

                expect (peakOf (env) > 0.5f,
                        "a destination never ducked: peak " + juce::String (peakOf (env), 4));

                for (float v : env)
                {
                    expect (std::isfinite (v), "non-finite envelope sample");
                    expect (v >= -0.001f && v <= 1.001f,
                            "envelope left 0..1: " + juce::String (v, 4));
                }
            }
        }

        beginTest ("the three that engines read are genuinely different envelopes, not copies");
        {
            // If they were copies - which is what the engines were effectively
            // getting - this is the test that would fail, and it is the whole
            // point of the change.
            const auto volume = envelopeOf (Dest::volume, 12);
            const auto filter = envelopeOf (Dest::filter, 12);
            const auto space  = envelopeOf (Dest::space,  12);
            const auto memory = envelopeOf (Dest::memory, 12);

            auto differs = [&volume] (const std::vector<float>& other)
            {
                double worst = 0.0;

                for (size_t i = 0; i < juce::jmin (volume.size(), other.size()); ++i)
                    worst = juce::jmax (worst, (double) std::abs (volume[i] - other[i]));

                return worst;
            };

            logMessage ("    peak difference from VOLUME - filter "
                        + juce::String (differs (filter), 4)
                        + ", space " + juce::String (differs (space), 4)
                        + ", memory " + juce::String (differs (memory), 4));

            expect (differs (filter) > 0.02, "FILTER is the same envelope as VOLUME");
            expect (differs (space)  > 0.02, "SPACE is the same envelope as VOLUME");
            expect (differs (memory) > 0.02, "MEMORY is the same envelope as VOLUME");
        }

        beginTest ("each envelope holds its duck for as long as the table says");
        {
            // FILTER releases at 0.70 of the user's, so it lets go SOONER than
            // VOLUME. SPACE at 1.60 and MEMORY at 1.30 hold on LONGER. That
            // ordering is the design, and it is what an engine reading the
            // wrong envelope destroys.
            // ONE trigger, measured before the next. At a 1/4 division the
            // longer envelopes never finish releasing before they are
            // retriggered, so SPACE and MEMORY both saturate and the ordering
            // the table describes disappears into the ceiling - which is what
            // the first version of this test measured. 1/1 at 120 BPM is two
            // seconds, and the longest release here is 0.48 s.
            const int blocks = (int) (kRate * 1.5) / kBlock;

            const int volume = samplesAbove (envelopeOf (Dest::volume, blocks), 0.25f);
            const int filter = samplesAbove (envelopeOf (Dest::filter, blocks), 0.25f);
            const int space  = samplesAbove (envelopeOf (Dest::space,  blocks), 0.25f);
            const int memory = samplesAbove (envelopeOf (Dest::memory, blocks), 0.25f);

            logMessage ("    samples held above 0.25 - filter " + juce::String (filter)
                        + ", volume " + juce::String (volume)
                        + ", memory " + juce::String (memory)
                        + ", space " + juce::String (space));

            expect (filter < volume,
                    "FILTER (0.70 release) held longer than VOLUME: "
                        + juce::String (filter) + " against " + juce::String (volume));

            expect (memory > volume,
                    "MEMORY (1.30 release) let go sooner than VOLUME: "
                        + juce::String (memory) + " against " + juce::String (volume));

            expect (space > memory,
                    "SPACE (1.60 release) let go sooner than MEMORY: "
                        + juce::String (space) + " against " + juce::String (memory));
        }

        beginTest ("the modulation engine publishes all five, and three of them are new");
        {
            // The plumbing, not the shapes: before this change MacroState had
            // nowhere to put them, so an engine could not have read its own
            // envelope even if it wanted to.
            TestHost host;

            host.registry.setFromUI (PID::pulseOn, 1.0f);
            host.registry.setFromUI (PID::pulseDepth, 1.0f);
            host.registry.setFromUI (PID::pulseToFilter, 1.0f);
            host.registry.setFromUI (PID::pulseToSpace, 1.0f);
            host.registry.setFromUI (PID::pulseToMemory, 1.0f);

            ModulationEngine modulation;

            EngineSpec spec;
            spec.sampleRate   = kRate;
            spec.maxBlockSize = kBlock;
            spec.numChannels  = 2;

            modulation.prepare (spec);

            MacroState macros;
            macros.sampleRate       = kRate;
            macros.numSamples       = kBlock;
            macros.hostBpm          = 120.0;
            macros.ppqPosition      = 0.0;
            macros.transportPlaying = true;

            modulation.updateBlock (macros, host.registry);

            expect (macros.pulse != nullptr, "VOLUME was not published");
            expect (macros.pulseFilter != nullptr, "FILTER was not published");
            expect (macros.pulseSpace  != nullptr, "SPACE was not published");
            expect (macros.pulseMemory != nullptr, "MEMORY was not published");

            expect (macros.pulseFilter != macros.pulse,
                    "FILTER points at the VOLUME buffer - the engines would read the wrong one");
            expect (macros.pulseSpace  != macros.pulse, "SPACE points at the VOLUME buffer");
            expect (macros.pulseMemory != macros.pulse, "MEMORY points at the VOLUME buffer");

            expect (macros.pulseToFilter > 0.9f, "the FILTER depth did not survive");
            expect (macros.pulseToSpace  > 0.9f, "the SPACE depth did not survive");
            expect (macros.pulseToMemory > 0.9f, "the MEMORY depth did not survive");
        }

        beginTest ("a null envelope reads as no duck rather than crashing");
        {
            // MacroState's accessors treat null as silence, and the modulation
            // engine nulls every pointer when a block is larger than it
            // prepared for. An engine indexing one blindly would read freed
            // memory on the first oversized buffer a host sent.
            MacroState macros;

            for (int i = 0; i < 64; ++i)
            {
                // Ordered rather than an equality test: -Wfloat-equal is on
                // for this build and it is right to be.
                expect (! (std::abs (macros.pulseFilterAt (i)) > 0.0f), "null FILTER did not read as zero");
                expect (! (std::abs (macros.pulseSpaceAt  (i)) > 0.0f), "null SPACE did not read as zero");
                expect (! (std::abs (macros.pulseMemoryAt (i)) > 0.0f), "null MEMORY did not read as zero");
            }
        }
    }
};

static PulseTests pulseTests;
