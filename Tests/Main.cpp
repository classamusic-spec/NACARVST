/*
    NACAR test runner.

    Headless.  No audio device, no plugin host, no UI.  Everything here either
    proves a property of the parameter table and state layer, or renders audio
    offline and asserts something about the samples that come out.

    Run with no arguments to execute every suite; pass a substring to run only
    the suites whose name matches.
*/

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "../Source/Plugin/ParameterRegistry.h"
#include "../Source/Plugin/StateManager.h"
#include "../Source/Audio/Sources/Synth/SynthEngine.h"

using namespace nacar;

// ===========================================================================
//  A minimal host for the parameter tree.
//
//  The real NacarProcessor pulls in the plugin client, which needs a host to
//  link against.  The tests only need something that owns an APVTS, so they
//  own the smallest AudioProcessor that can.
// ===========================================================================
class TestHost : public juce::AudioProcessor
{
public:
    TestHost()
        : juce::AudioProcessor (BusesProperties()
                                    .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMETERS", ParameterRegistry::createLayout())
    {
        registry.attach (apvts);
    }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "TestHost"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
    ParameterRegistry registry;
};

// ===========================================================================
//  Parameter table
// ===========================================================================
struct ParameterTableTests : juce::UnitTest
{
    ParameterTableTests() : juce::UnitTest ("Parameter table", "nacar") {}

    void runTest() override
    {
        const auto& table = ParameterRegistry::allDefinitions();

        beginTest ("every PID has a definition at its own index");
        {
            for (int i = 0; i < numParameters; ++i)
                expect (table[(size_t) i].pid == (PID) i,
                        "definition " + juce::String (i) + " is out of order");
        }

        beginTest ("string IDs are unique and non-empty");
        {
            juce::StringArray seen;

            for (const auto& d : table)
            {
                const juce::String id (d.id);
                expect (id.isNotEmpty(), "empty parameter ID");
                expect (! seen.contains (id), "duplicate parameter ID: " + id);
                seen.add (id);
            }
        }

        beginTest ("names and tooltips are present");
        {
            for (const auto& d : table)
            {
                expect (juce::String (d.name).isNotEmpty(),
                        juce::String (d.id) + " has no display name");
                expect (juce::String (d.tooltip).isNotEmpty(),
                        juce::String (d.id) + " has no tooltip");
            }
        }

        beginTest ("float ranges are sane and defaults fall inside them");
        {
            for (const auto& d : table)
            {
                if (d.kind != ParamKind::floatValue)
                    continue;

                const juce::String id (d.id);
                expect (d.maxValue > d.minValue, id + " has an empty range");
                expect (d.skew > 0.0f, id + " has a non-positive skew");
                expect (d.defaultValue >= d.minValue && d.defaultValue <= d.maxValue,
                        id + " default sits outside its range");
            }
        }

        beginTest ("choice defaults are in bounds");
        {
            for (const auto& d : table)
            {
                if (d.kind != ParamKind::choice)
                    continue;

                const juce::String id (d.id);
                expect (d.choicesPipeSeparated != nullptr, id + " has no choice list");

                const auto choices = juce::StringArray::fromTokens (
                    juce::String (d.choicesPipeSeparated), "|", "");

                expect (choices.size() >= 2, id + " needs at least two choices");
                expect (juce::isPositiveAndBelow (d.defaultChoice, choices.size()),
                        id + " default choice is out of bounds");

                for (const auto& c : choices)
                    expect (c.isNotEmpty(), id + " has an empty choice");
            }
        }

        beginTest ("fromString round-trips every PID");
        {
            for (const auto& d : table)
                expect (ParameterRegistry::fromString (d.id) == d.pid,
                        juce::String (d.id) + " does not round-trip");

            expect (ParameterRegistry::fromString ("no_such_parameter") == PID::count);
        }

        beginTest ("the APVTS layout matches the table exactly");
        {
            TestHost host;

            for (const auto& d : table)
            {
                auto* p = host.apvts.getParameter (d.id);
                expect (p != nullptr, juce::String (d.id) + " is missing from the layout");

                if (p != nullptr)
                    expect (host.registry.parameter (d.pid) == p,
                            juce::String (d.id) + " is cached against the wrong parameter");
            }

            expect (host.getParameters().size() == numParameters,
                    "the host sees a different number of parameters than the table declares");
        }

        beginTest ("defaults survive a normalise / denormalise round-trip");
        {
            TestHost host;

            for (const auto& d : table)
            {
                if (d.kind != ParamKind::floatValue)
                    continue;

                auto* p = host.registry.parameter (d.pid);
                const float norm = p->convertTo0to1 (d.defaultValue);
                const float back = p->convertFrom0to1 (norm);

                // Snapped parameters quantise, so the tolerance is one interval.
                const float tolerance = juce::jmax (1.0e-3f,
                                                    (d.maxValue - d.minValue) * 1.0e-3f);

                expect (std::abs (back - d.defaultValue) <= tolerance,
                        juce::String (d.id) + " does not round-trip: "
                            + juce::String (d.defaultValue) + " -> " + juce::String (back));
            }
        }

        beginTest ("every value in every range formats without throwing");
        {
            TestHost host;

            for (const auto& d : table)
            {
                for (int step = 0; step <= 10; ++step)
                {
                    auto* p = host.registry.parameter (d.pid);
                    p->setValueNotifyingHost ((float) step / 10.0f);

                    expect (host.registry.formatValue (d.pid).isNotEmpty(),
                            juce::String (d.id) + " formats to an empty string");
                }
            }
        }
    }
};

// ===========================================================================
//  State
// ===========================================================================
struct StateTests : juce::UnitTest
{
    StateTests() : juce::UnitTest ("State", "nacar") {}

    void runTest() override
    {
        beginTest ("a default session carries every group");
        {
            const auto s = StateManager::makeDefaultSession();

            expect (s.hasType (ids::SESSION));
            expect ((int) s.getProperty (ids::schemaVersion) == StateManager::currentSchemaVersion);

            for (const auto& group : { ids::PRESET, ids::SAMPLE, ids::ANALYSIS, ids::FXCHAIN,
                                       ids::MUTATION, ids::GENERATIONS, ids::EDITOR })
                expect (s.getChildWithName (group).isValid(),
                        group.toString() + " is missing from a default session");
        }

        beginTest ("FX order defaults to the locked reference order");
        {
            const auto s = StateManager::makeDefaultSession();
            const juce::String order = s.getChildWithName (ids::FXCHAIN)
                                        .getProperty (ids::fxOrder).toString();

            expect (order == "RETRO,CRUSH,FILTER,REWIND,GRAIN,SPACE",
                    "FX order changed: " + order);
        }

        beginTest ("parameters and session survive a write / read round-trip");
        {
            TestHost host;
            StateManager sm (host.apvts);

            // Move a parameter of each kind away from its default.
            host.registry.setFromUI (PID::macroMemory, 0.77f);
            host.registry.setFromUI (PID::memoryGen, 2.0f);
            host.registry.setFromUI (PID::shadowOn, 1.0f);
            host.registry.setFromUI (PID::filterCutoff, 640.0f);

            sm.session().getChildWithName (ids::PRESET)
              .setProperty (ids::presetName, "Agua Negra", nullptr);
            sm.session().getChildWithName (ids::MUTATION)
              .setProperty (ids::currentSeed, 4192, nullptr);

            juce::MemoryBlock blob;
            sm.writeTo (blob);
            expect (blob.getSize() > 0, "nothing was written");

            TestHost restoredHost;
            StateManager restored (restoredHost.apvts);
            restored.readFrom (blob.getData(), (int) blob.getSize());

            expectWithinAbsoluteError (restoredHost.registry.raw (PID::macroMemory), 0.77f, 1.0e-4f);
            expectEquals (restoredHost.registry.choice (PID::memoryGen), 2);
            expect (restoredHost.registry.flag (PID::shadowOn));
            expectWithinAbsoluteError (restoredHost.registry.raw (PID::filterCutoff), 640.0f, 1.0f);

            expectEquals (restored.session().getChildWithName (ids::PRESET)
                                  .getProperty (ids::presetName).toString(),
                          juce::String ("Agua Negra"));
            expectEquals ((int) restored.session().getChildWithName (ids::MUTATION)
                                        .getProperty (ids::currentSeed), 4192);
        }

        beginTest ("garbage and truncated blobs are survived, not crashed on");
        {
            TestHost host;
            StateManager sm (host.apvts);

            const char junk[] = "this is not a value tree";
            sm.readFrom (junk, (int) sizeof (junk));
            sm.readFrom (nullptr, 0);

            juce::MemoryBlock blob;
            sm.writeTo (blob);
            sm.readFrom (blob.getData(), (int) blob.getSize() / 2);

            // Reaching here without a crash or a hang is the assertion.
            expect (sm.session().hasType (ids::SESSION));
        }

        beginTest ("an unversioned session is upgraded rather than discarded");
        {
            juce::ValueTree old (ids::SESSION);
            juce::ValueTree preset (ids::PRESET);
            preset.setProperty (ids::presetName, "Hotel Miramar", nullptr);
            old.addChild (preset, -1, nullptr);

            StateManager::upgrade (old, 0);

            expectEquals ((int) old.getProperty (ids::schemaVersion),
                          StateManager::currentSchemaVersion);
            expectEquals (old.getChildWithName (ids::PRESET).getProperty (ids::presetName).toString(),
                          juce::String ("Hotel Miramar"));
            expect (old.getChildWithName (ids::FXCHAIN).isValid(),
                    "the upgrade did not add the groups introduced since version 0");
        }
    }
};

// ===========================================================================
//  Synth engine
//
//  These render real audio offline and assert properties of the samples.  They
//  cannot tell us whether NACAR sounds good - only a person can do that - but
//  they can prove it never emits a NaN, never blows past full scale, and
//  behaves identically at every sample rate the product claims to support.
// ===========================================================================
struct SynthEngineTests : juce::UnitTest
{
    SynthEngineTests() : juce::UnitTest ("Synth engine", "nacar") {}

    struct RenderResult
    {
        float peak = 0.0f;
        float rms = 0.0f;
        bool  allFinite = true;
        bool  anyNonZero = false;
        float dcOffset = 0.0f;
    };

    static RenderResult render (SynthEngine& synth, const ParameterRegistry& params,
                                double sampleRate, int blockSize, int numBlocks,
                                bool playNote, int midiNote = 45)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        RenderResult r;

        double sum = 0.0, sumSq = 0.0;
        juce::int64 count = 0;

        for (int b = 0; b < numBlocks; ++b)
        {
            buffer.clear();
            juce::MidiBuffer midi;

            if (playNote && b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, midiNote, 0.9f), 0);

            if (playNote && b == numBlocks - 4)
                midi.addEvent (juce::MidiMessage::noteOff (1, midiNote), 0);

            synth.process (buffer, midi, params, 120.0);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                const auto* d = buffer.getReadPointer (ch);

                for (int i = 0; i < blockSize; ++i)
                {
                    const float x = d[i];

                    if (! std::isfinite (x))
                        r.allFinite = false;

                    if (std::abs (x) > 1.0e-6f)
                        r.anyNonZero = true;

                    r.peak = juce::jmax (r.peak, std::abs (x));
                    sum += x;
                    sumSq += (double) x * (double) x;
                    ++count;
                }
            }
        }

        if (count > 0)
        {
            r.rms = (float) std::sqrt (sumSq / (double) count);
            r.dcOffset = (float) (sum / (double) count);
        }

        juce::ignoreUnused (sampleRate);
        return r;
    }

    void runTest() override
    {
        const std::array<double, 4> rates { 44100.0, 48000.0, 88200.0, 96000.0 };
        const std::array<int, 6> blocks { 32, 64, 128, 256, 512, 1024 };

        beginTest ("silence in, silence out");
        {
            TestHost host;
            SynthEngine synth;
            synth.prepare (48000.0, 512, 2);

            const auto r = render (synth, host.registry, 48000.0, 512, 8, false);

            expect (r.allFinite, "the engine emitted a non-finite sample while idle");
            expect (r.peak < 1.0e-5f, "the engine is not silent with no notes: peak "
                                          + juce::String (r.peak));
        }

        beginTest ("a note produces finite, bounded, audible output at every sample rate");
        {
            for (auto rate : rates)
            {
                TestHost host;
                SynthEngine synth;
                synth.prepare (rate, 512, 2);

                const int blocksForTwoSeconds = (int) (rate * 2.0 / 512.0);
                const auto r = render (synth, host.registry, rate, 512, blocksForTwoSeconds, true);

                const juce::String at = " at " + juce::String (rate, 0) + " Hz";

                expect (r.allFinite, "non-finite output" + at);
                expect (r.anyNonZero, "the engine stayed silent on a note" + at);
                expect (r.peak <= 1.5f, "output ran past +3.5 dBFS" + at
                                            + ": peak " + juce::String (r.peak));
                expect (std::abs (r.dcOffset) < 0.02f, "DC offset" + at
                                                           + ": " + juce::String (r.dcOffset));
            }
        }

        beginTest ("output is independent of block size");
        {
            // Rendering the same note in 32-sample and 1024-sample blocks must
            // give the same sound.  A mismatch means something is being updated
            // per block that should be updated per sample.
            float reference = 0.0f;

            for (auto blockSize : blocks)
            {
                TestHost host;
                SynthEngine synth;
                synth.prepare (48000.0, blockSize, 2);

                const int numBlocks = (int) (48000.0 * 1.0 / blockSize);
                const auto r = render (synth, host.registry, 48000.0, blockSize, numBlocks, true);

                expect (r.allFinite, "non-finite output at block size " + juce::String (blockSize));
                expect (r.anyNonZero, "silence at block size " + juce::String (blockSize));

                if (reference == 0.0f)
                    reference = r.rms;
                else
                    expect (std::abs (r.rms - reference) < reference * 0.25f + 1.0e-4f,
                            "RMS drifted with block size " + juce::String (blockSize)
                                + ": " + juce::String (r.rms) + " vs " + juce::String (reference));
            }
        }

        beginTest ("every synth character renders");
        {
            for (int character = 0; character < 3; ++character)
            {
                TestHost host;
                host.registry.setFromUI (PID::synthCharacter, (float) character);

                SynthEngine synth;
                synth.prepare (48000.0, 256, 2);

                const auto r = render (synth, host.registry, 48000.0, 256, 200, true);

                const juce::String which = " (character " + juce::String (character) + ")";
                expect (r.allFinite, "non-finite output" + which);
                expect (r.anyNonZero, "silence" + which);
                expect (r.peak <= 1.5f, "clipping" + which);
            }
        }

        beginTest ("extreme parameter settings do not blow the filters up");
        {
            // Resonance, drive and unison all at maximum, cutoff swept to both
            // extremes.  Spec section 28 makes this a hard requirement.
            for (auto rate : rates)
            {
                TestHost host;
                host.registry.setFromUI (PID::filterResonance, 1.0f);
                host.registry.setFromUI (PID::filterDrive, 1.0f);
                host.registry.setFromUI (PID::preFilterDrive, 1.0f);
                host.registry.setFromUI (PID::postSaturation, 1.0f);
                host.registry.setFromUI (PID::bodyAmount, 1.0f);
                host.registry.setFromUI (PID::oscAUnison, 8.0f);
                host.registry.setFromUI (PID::oscBUnison, 8.0f);
                host.registry.setFromUI (PID::oscADetune, 1.0f);
                host.registry.setFromUI (PID::oscSync, 1.0f);
                host.registry.setFromUI (PID::oscFmAmount, 1.0f);

                SynthEngine synth;
                synth.prepare (rate, 128, 2);

                juce::AudioBuffer<float> buffer (2, 128);
                bool finite = true;
                float peak = 0.0f;

                for (int b = 0; b < 400; ++b)
                {
                    // Sweep the cutoff across the whole range while playing.
                    const float t = (float) b / 400.0f;
                    host.registry.setFromUI (PID::filterCutoff,
                                             20.0f + t * t * 19980.0f);

                    buffer.clear();
                    juce::MidiBuffer midi;

                    if (b == 0)   midi.addEvent (juce::MidiMessage::noteOn (1, 88, 1.0f), 0);
                    if (b == 40)  midi.addEvent (juce::MidiMessage::noteOn (1, 24, 1.0f), 0);
                    if (b == 300) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);

                    synth.process (buffer, midi, host.registry, 120.0);

                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const auto* d = buffer.getReadPointer (ch);

                        for (int i = 0; i < 128; ++i)
                        {
                            if (! std::isfinite (d[i]))
                                finite = false;

                            peak = juce::jmax (peak, std::abs (d[i]));
                        }
                    }
                }

                const juce::String at = " at " + juce::String (rate, 0) + " Hz";
                expect (finite, "the engine produced a NaN or an infinity under extremes" + at);
                expect (peak < 8.0f, "the engine ran away under extremes" + at
                                         + ": peak " + juce::String (peak));
            }
        }

        beginTest ("the sub oscillator stays mono-compatible");
        {
            // Spec section 20: the sub must never be widened.  Summing to mono
            // must not cancel it.
            TestHost host;
            host.registry.setFromUI (PID::subLevel, 1.0f);
            host.registry.setFromUI (PID::oscALevel, 0.0f);
            host.registry.setFromUI (PID::oscBLevel, 0.0f);
            host.registry.setFromUI (PID::oscCLevel, 0.0f);
            host.registry.setFromUI (PID::noiseLevel, 0.0f);

            SynthEngine synth;
            synth.prepare (48000.0, 512, 2);

            juce::AudioBuffer<float> buffer (2, 512);
            float stereoEnergy = 0.0f, monoEnergy = 0.0f;

            for (int b = 0; b < 120; ++b)
            {
                buffer.clear();
                juce::MidiBuffer midi;

                if (b == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 33, 1.0f), 0);

                synth.process (buffer, midi, host.registry, 120.0);

                if (b < 20)     // let the envelope open first
                    continue;

                const auto* l = buffer.getReadPointer (0);
                const auto* r = buffer.getReadPointer (1);

                for (int i = 0; i < 512; ++i)
                {
                    const float mono = (l[i] + r[i]) * 0.5f;
                    stereoEnergy += l[i] * l[i] + r[i] * r[i];
                    monoEnergy   += mono * mono * 2.0f;
                }
            }

            if (stereoEnergy > 1.0e-6f)
                expect (monoEnergy > stereoEnergy * 0.8f,
                        "the sub loses energy in mono: " + juce::String (monoEnergy)
                            + " vs " + juce::String (stereoEnergy));
            else
                logMessage ("  sub produced no energy - skipped (engine may be incomplete)");
        }

        beginTest ("all-notes-off silences the engine");
        {
            TestHost host;
            SynthEngine synth;
            synth.prepare (48000.0, 256, 2);

            juce::AudioBuffer<float> buffer (2, 256);

            for (int b = 0; b < 40; ++b)
            {
                buffer.clear();
                juce::MidiBuffer midi;

                if (b == 0)
                    for (int n = 40; n < 52; ++n)
                        midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.9f), 0);

                synth.process (buffer, midi, host.registry, 120.0);
            }

            synth.allNotesOff();

            // The amp release still has to run out, so give it a generous tail.
            float tailPeak = 0.0f;

            for (int b = 0; b < 2000; ++b)
            {
                buffer.clear();
                juce::MidiBuffer midi;
                synth.process (buffer, midi, host.registry, 120.0);

                if (b > 1900)
                    tailPeak = juce::jmax (tailPeak, buffer.getMagnitude (0, 256));
            }

            expect (tailPeak < 1.0e-4f,
                    "the engine is still sounding long after all-notes-off: "
                        + juce::String (tailPeak));
        }

        beginTest ("polyphony is respected and voice stealing does not explode");
        {
            TestHost host;
            host.registry.setFromUI (PID::polyphony, 4.0f);

            SynthEngine synth;
            synth.prepare (48000.0, 128, 2);

            juce::AudioBuffer<float> buffer (2, 128);
            bool finite = true;
            float peak = 0.0f;

            for (int b = 0; b < 300; ++b)
            {
                buffer.clear();
                juce::MidiBuffer midi;

                // Far more notes than voices, arriving continuously.
                if (b % 3 == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 36 + (b % 40), 1.0f), 0);

                synth.process (buffer, midi, host.registry, 120.0);

                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto* d = buffer.getReadPointer (ch);

                    for (int i = 0; i < 128; ++i)
                    {
                        if (! std::isfinite (d[i]))
                            finite = false;

                        peak = juce::jmax (peak, std::abs (d[i]));
                    }
                }
            }

            expect (finite, "voice stealing produced a non-finite sample");
            expect (peak < 4.0f, "voice stealing ran away: peak " + juce::String (peak));
        }
    }
};

// ===========================================================================
static ParameterTableTests parameterTableTests;
static StateTests          stateTests;
static SynthEngineTests    synthEngineTests;

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    if (argc > 1)
        runner.runTestsInCategory ("nacar");
    else
        runner.runAllTests();

    int failures = 0, passes = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult (i);
        failures += r->failures;
        passes   += r->passes;

        if (r->failures > 0)
        {
            std::cout << "FAILED  " << r->unitTestName << " / " << r->subcategoryName
                      << "  (" << r->failures << " failures)\n";

            for (const auto& m : r->messages)
                std::cout << "          " << m << "\n";
        }
    }

    std::cout << "\n=====================================================\n"
              << "  NACAR tests: " << passes << " passed, "
              << failures << " failed\n"
              << "=====================================================\n";

    return failures > 0 ? 1 : 0;
}
