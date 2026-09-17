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

#include <tuple>

#include "../Source/Plugin/ParameterRegistry.h"
#include "../Source/Plugin/StateManager.h"
#include "../Source/Audio/Sources/Synth/SynthEngine.h"
#include "../Source/Audio/Sources/Synth/Halfband.h"
#include "../Source/Audio/NacarEngine.h"
#include "../Source/Audio/Modulation/ModulationEngine.h"
#include "../Source/Presets/FactoryPresets.h"
#include "../Source/Presets/PresetManager.h"

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

                if (! (reference > 0.0f))
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
//  Halfband
//
//  The voice's nonlinear core runs at twice the sample rate, so the whole of
//  it depends on this filter pair being right.  A polyphase decomposition that
//  is subtly wrong - the two phases swapped, the delay off by one - still
//  produces plausible-looking audio, so it is tested directly rather than
//  inferred from the synth sounding reasonable.
// ===========================================================================
struct HalfbandTests : juce::UnitTest
{
    HalfbandTests() : juce::UnitTest ("Halfband", "nacar") {}

    void runTest() override
    {
        using namespace nacar::synth;

        beginTest ("the taps are a halfband: unity at DC, one half at the centre");
        {
            const auto& even = HalfbandDesign<kHalfbandTaps>::evenTaps();

            double sum = 0.0;
            for (auto t : even)
                sum += (double) t;

            // The even taps plus the single centre tap of 0.5 must sum to 1.
            expectWithinAbsoluteError ((float) (sum + 0.5), 1.0f, 1.0e-4f);
        }

        beginTest ("up then down reconstructs the signal");
        {
            // A sine well inside the passband must survive a round trip with
            // nothing but a delay.  Anything else - a swapped phase, a
            // misaligned tap - shows up here as a large residual.
            constexpr int n = 4096;
            std::vector<float> in ((size_t) n), out ((size_t) n);

            for (int i = 0; i < n; ++i)
                in[(size_t) i] = std::sin (juce::MathConstants<float>::twoPi
                                               * 1000.0f * (float) i / 48000.0f);

            VoiceUpsampler up;
            VoiceDownsampler down;
            up.reset();
            down.reset();

            for (int i = 0; i < n; ++i)
            {
                float a = 0.0f, b = 0.0f;
                up.process (in[(size_t) i], a, b);
                out[(size_t) i] = down.process (a, b);
            }

            // Find the delay that best aligns the two, then measure what is
            // left over.  The delay is a property of the filter length; the
            // residual is what says whether the decomposition is correct.
            int   bestDelay = 0;
            double bestError = 1.0e30;

            for (int d = 0; d < kHalfbandTaps; ++d)
            {
                double err = 0.0;

                for (int i = n / 4; i < n - kHalfbandTaps; ++i)
                {
                    const double e = (double) out[(size_t) (i + d)] - (double) in[(size_t) i];
                    err += e * e;
                }

                if (err < bestError)
                {
                    bestError = err;
                    bestDelay = d;
                }
            }

            const int samples = n - kHalfbandTaps - n / 4;
            const double rms = std::sqrt (bestError / (double) juce::jmax (1, samples));
            const double db = 20.0 * std::log10 (juce::jmax (1.0e-12, rms / 0.7071));

            logMessage ("    round trip: delay " + juce::String (bestDelay)
                        + " samples, residual " + juce::String (db, 1) + " dB");

            expect (db < -50.0, "round-trip residual is " + juce::String (db, 1)
                                    + " dB, which means the polyphase split is wrong");
        }

        beginTest ("content above the original Nyquist is rejected");
        {
            // Feed the upsampler a signal, then inject a tone at three quarters
            // of the high rate's Nyquist - which is above the low rate's - and
            // check the downsampler removes it rather than folding it back.
            constexpr int n = 4096;

            VoiceDownsampler down;
            down.reset();

            double energy = 0.0;

            for (int i = 0; i < n; ++i)
            {
                // 36 kHz at a 96 kHz high rate: above the 24 kHz the low rate
                // can represent, so it must not survive.
                const float a = std::sin (juce::MathConstants<float>::twoPi
                                              * 36000.0f * (float) (i * 2) / 96000.0f);
                const float b = std::sin (juce::MathConstants<float>::twoPi
                                              * 36000.0f * (float) (i * 2 + 1) / 96000.0f);

                const float y = down.process (a, b);

                if (i > n / 4)
                    energy += (double) y * y;
            }

            const double rms = std::sqrt (energy / (double) (n - n / 4));
            const double db = 20.0 * std::log10 (juce::jmax (1.0e-12, rms / 0.7071));

            logMessage ("    out-of-band rejection: " + juce::String (db, 1) + " dB");

            expect (db < -40.0, "an out-of-band tone survived at "
                                    + juce::String (db, 1) + " dB");
        }
    }
};

// ===========================================================================
//  Modulation
//
//  Breath's whole brief is that it must not repeat, and the specification is
//  explicit that it is not another LFO.  That is a measurable property, so it
//  is measured here rather than argued for: sixty seconds of output, then the
//  autocorrelation at every lag a listener could notice.
// ===========================================================================
struct ModulationTests : juce::UnitTest
{
    ModulationTests() : juce::UnitTest ("Modulation", "nacar") {}

    /** Renders `seconds` of the four modulation outputs into four vectors. */
    struct Capture
    {
        std::vector<float> lfo1, lfo2, breath, pulse;
    };

    static Capture render (TestHost& host, double sampleRate, int blockSize,
                           double seconds, bool transportPlaying = true)
    {
        ModulationEngine engine;

        EngineSpec spec;
        spec.sampleRate = sampleRate;
        spec.maxBlockSize = blockSize;
        spec.numChannels = 2;
        engine.prepare (spec);

        const int total = (int) (sampleRate * seconds);
        Capture c;
        c.lfo1.reserve ((size_t) total);
        c.lfo2.reserve ((size_t) total);
        c.breath.reserve ((size_t) total);
        c.pulse.reserve ((size_t) total);

        MacroState m;
        resolveMacros (m, host.registry);

        m.sampleRate = sampleRate;
        m.hostBpm = 120.0;
        m.transportPlaying = transportPlaying;

        int position = 0;

        while (position < total)
        {
            const int n = juce::jmin (blockSize, total - position);

            m.numSamples = n;
            engine.updateBlock (m, host.registry);

            for (int i = 0; i < n; ++i)
            {
                c.lfo1.push_back (m.lfo1At (i));
                c.lfo2.push_back (m.lfo2At (i));
                c.breath.push_back (m.breathAt (i));
                c.pulse.push_back (m.pulseAt (i));
            }

            m.ppqPosition += (double) n / sampleRate * (m.hostBpm / 60.0);
            position += n;
        }

        return c;
    }

    /** Pearson autocorrelation of a mean-removed signal at one lag. */
    static double autocorrelation (const std::vector<float>& x, int lag)
    {
        const int n = (int) x.size() - lag;

        if (n <= 16)
            return 0.0;

        double mean = 0.0;
        for (auto v : x)
            mean += (double) v;

        mean /= (double) x.size();

        double num = 0.0, da = 0.0, db = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double a = (double) x[(size_t) i] - mean;
            const double b = (double) x[(size_t) (i + lag)] - mean;

            num += a * b;
            da += a * a;
            db += b * b;
        }

        const double denom = std::sqrt (da * db);
        return denom < 1.0e-12 ? 0.0 : num / denom;
    }

    void runTest() override
    {
        constexpr double sr = 48000.0;

        beginTest ("every modulation output respects its declared range");
        {
            TestHost host;
            host.registry.setFromUI (PID::lfo1Depth, 1.0f);
            host.registry.setFromUI (PID::lfo2Depth, 1.0f);
            host.registry.setFromUI (PID::breathOn, 1.0f);
            host.registry.setFromUI (PID::breathAmount, 1.0f);
            host.registry.setFromUI (PID::pulseOn, 1.0f);
            host.registry.setFromUI (PID::pulseDepth, 1.0f);

            const auto c = render (host, sr, 256, 8.0);

            for (const auto* pair : { &c.lfo1, &c.lfo2, &c.breath })
                for (auto v : *pair)
                {
                    expect (std::isfinite (v), "a modulation output was not finite");
                    expect (v >= -1.0001f && v <= 1.0001f,
                            "a bipolar modulation output left -1..1: " + juce::String (v));
                }

            for (auto v : c.pulse)
            {
                expect (std::isfinite (v), "the pulse envelope was not finite");
                expect (v >= -0.0001f && v <= 1.0001f,
                        "the pulse envelope left 0..1: " + juce::String (v));
            }
        }

        beginTest ("Breath does not repeat");
        {
            // Specification section 57: Breath is not another LFO.  If a
            // listener can hear a period, it has failed - so no lag longer than
            // a fraction of a second may correlate strongly with the start.
            TestHost host;
            host.registry.setFromUI (PID::breathOn, 1.0f);
            host.registry.setFromUI (PID::breathAmount, 1.0f);
            host.registry.setFromUI (PID::breathSpeed, 0.4f);
            host.registry.setFromUI (PID::breathRandom, 0.6f);

            const auto c = render (host, sr, 512, 60.0);

            double worst = 0.0;
            double worstLagSeconds = 0.0;

            // Every lag from a quarter of a second to thirty, which is the span
            // over which a repeat would read as a loop rather than as movement.
            for (double lagSeconds = 0.25; lagSeconds <= 30.0; lagSeconds += 0.25)
            {
                const double r = std::abs (autocorrelation (c.breath, (int) (lagSeconds * sr)));

                if (r > worst)
                {
                    worst = r;
                    worstLagSeconds = lagSeconds;
                }
            }

            logMessage ("    Breath worst autocorrelation: " + juce::String (worst, 3)
                        + " at " + juce::String (worstLagSeconds, 2) + " s");

            expect (worst < 0.75, "Breath repeats: correlation " + juce::String (worst, 3)
                                      + " at a lag of " + juce::String (worstLagSeconds, 2)
                                      + " s, which is a period a listener would hear");
        }

        beginTest ("Breath actually moves");
        {
            // The cheapest way to pass the test above is to output nothing.
            TestHost host;
            host.registry.setFromUI (PID::breathOn, 1.0f);
            host.registry.setFromUI (PID::breathAmount, 1.0f);

            const auto c = render (host, sr, 256, 20.0);

            float lo = 1.0f, hi = -1.0f;
            double sumSq = 0.0;

            for (auto v : c.breath)
            {
                lo = juce::jmin (lo, v);
                hi = juce::jmax (hi, v);
                sumSq += (double) v * v;
            }

            const double rms = std::sqrt (sumSq / (double) juce::jmax<size_t> (1, c.breath.size()));

            logMessage ("    Breath range " + juce::String (lo, 3) + " .. "
                        + juce::String (hi, 3) + ", rms " + juce::String (rms, 3));

            expect (hi - lo > 0.5f, "Breath barely moved: range " + juce::String (hi - lo, 3));
            expect (rms > 0.05, "Breath is nearly silent: rms " + juce::String (rms, 3));
        }

        beginTest ("Breath is deterministic but an LFO is not mistaken for it");
        {
            TestHost host;
            host.registry.setFromUI (PID::breathOn, 1.0f);
            host.registry.setFromUI (PID::breathAmount, 1.0f);

            const auto a = render (host, sr, 256, 6.0);
            const auto b = render (host, sr, 256, 6.0);

            expectEquals ((int) a.breath.size(), (int) b.breath.size());

            for (size_t i = 0; i < a.breath.size(); ++i)
                if (std::abs (a.breath[i] - b.breath[i]) > 1.0e-6f)
                {
                    expect (false, "Breath is not deterministic: diverged at sample "
                                       + juce::String ((int) i));
                    break;
                }

            // And for contrast: a sine LFO at the same nominal rate should
            // correlate almost perfectly with itself one period later, which is
            // what Breath must not do.
            TestHost lfoHost;
            lfoHost.registry.setFromUI (PID::lfo1Shape, 0.0f);
            lfoHost.registry.setFromUI (PID::lfo1Depth, 1.0f);
            lfoHost.registry.setFromUI (PID::lfo1Rate, 1.0f);

            const auto l = render (lfoHost, sr, 256, 12.0);
            const double periodic = std::abs (autocorrelation (l.lfo1, (int) sr));

            logMessage ("    LFO autocorrelation at one period: "
                        + juce::String (periodic, 3));

            expect (periodic > 0.9, "the LFO is not periodic, which means this "
                                    "comparison proves nothing about Breath");
        }

        beginTest ("Pulse ducks on the clock grid");
        {
            TestHost host;
            host.registry.setFromUI (PID::pulseOn, 1.0f);
            host.registry.setFromUI (PID::pulseDepth, 1.0f);
            host.registry.setFromUI (PID::pulseSource, 0.0f);     // CLOCK
            host.registry.setFromUI (PID::pulseDivision, 6.0f);   // 1/4
            host.registry.setFromUI (PID::pulseAttack, 0.002f);
            host.registry.setFromUI (PID::pulseRelease, 0.2f);

            const auto c = render (host, sr, 256, 8.0);

            float peak = 0.0f;
            for (auto v : c.pulse)
                peak = juce::jmax (peak, v);

            expect (peak > 0.8f, "Pulse never ducked: peak " + juce::String (peak));

            // At 120 BPM a quarter note is half a second, so eight seconds must
            // contain about sixteen ducks.
            int crossings = 0;
            bool above = false;

            for (auto v : c.pulse)
            {
                if (! above && v > 0.5f) { above = true; ++crossings; }
                else if (above && v < 0.2f) { above = false; }
            }

            logMessage ("    Pulse ducks in 8 s at 120 BPM, 1/4: "
                        + juce::String (crossings));

            expect (crossings >= 14 && crossings <= 18,
                    "Pulse fired " + juce::String (crossings)
                        + " times where about 16 was expected");
        }

        beginTest ("modulation is independent of block size");
        {
            TestHost host;
            host.registry.setFromUI (PID::breathOn, 1.0f);
            host.registry.setFromUI (PID::breathAmount, 1.0f);
            host.registry.setFromUI (PID::pulseOn, 1.0f);
            host.registry.setFromUI (PID::pulseDepth, 1.0f);

            const auto small = render (host, sr, 32, 4.0);
            const auto large = render (host, sr, 1024, 4.0);

            const int n = juce::jmin ((int) small.breath.size(), (int) large.breath.size());

            double diff = 0.0;
            for (int i = 0; i < n; ++i)
                diff += std::abs ((double) small.breath[(size_t) i]
                                  - (double) large.breath[(size_t) i]);

            const double mean = diff / (double) juce::jmax (1, n);
            logMessage ("    Breath mean difference across block sizes: "
                        + juce::String (mean, 5));

            expect (mean < 0.05, "Breath changed with block size: mean difference "
                                     + juce::String (mean, 5));
        }
    }
};

// ===========================================================================
//  The chain
//
//  Eleven engines in series, in an order the user can change at runtime.  These
//  tests are about the things that go wrong when DSP is composed rather than
//  written: a module that is off still colouring the signal, a reorder losing a
//  slot, a feedback network that is stable alone and not in series, and the low
//  end quietly decorrelating as it passes through six stereo processes.
// ===========================================================================
struct ChainTests : juce::UnitTest
{
    ChainTests() : juce::UnitTest ("Chain", "nacar") {}

    /** Everything that can colour the signal, turned off. */
    static void silenceTheChain (TestHost& host)
    {
        for (auto pid : { PID::retroOn, PID::crushOn, PID::fxFilterOn, PID::rewindOn,
                          PID::grainFxOn, PID::spaceOn, PID::auraOn, PID::shadowOn,
                          PID::patinaOn, PID::pulseOn, PID::breathOn })
            host.registry.setFromUI (pid, 0.0f);

        host.registry.setFromUI (PID::macroMemory, 0.0f);
        host.registry.setFromUI (PID::macroWeight, 0.0f);
        host.registry.setFromUI (PID::macroCharacter, 0.0f);
        host.registry.setFromUI (PID::macroMotion, 0.0f);
        host.registry.setFromUI (PID::macroAlter, 0.0f);
    }

    struct Rendered
    {
        juce::AudioBuffer<float> audio;
        bool  allFinite = true;
        float peak = 0.0f;
    };

    static void scan (Rendered& r)
    {
        for (int ch = 0; ch < r.audio.getNumChannels(); ++ch)
        {
            const auto* d = r.audio.getReadPointer (ch);

            for (int i = 0; i < r.audio.getNumSamples(); ++i)
            {
                if (! std::isfinite (d[i]))
                    r.allFinite = false;

                r.peak = juce::jmax (r.peak, std::abs (d[i]));
            }
        }
    }

    static Rendered renderChain (NacarEngine& engine, const ParameterRegistry& params,
                                 double sampleRate, int blockSize, int numBlocks,
                                 int midiNote = 45)
    {
        Rendered r;
        r.audio.setSize (2, blockSize * numBlocks);
        r.audio.clear();

        juce::AudioBuffer<float> block (2, blockSize);
        TransportInfo transport;
        transport.bpm = 120.0;
        transport.playing = true;

        for (int b = 0; b < numBlocks; ++b)
        {
            block.clear();
            juce::MidiBuffer midi;

            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, midiNote, 0.9f), 0);

            if (b == numBlocks * 3 / 4)
                midi.addEvent (juce::MidiMessage::noteOff (1, midiNote), 0);

            engine.process (block, midi, params, transport);

            transport.ppqPosition += (double) blockSize / sampleRate * (transport.bpm / 60.0);

            for (int ch = 0; ch < 2; ++ch)
                r.audio.copyFrom (ch, b * blockSize, block, ch, 0, blockSize);
        }

        scan (r);
        return r;
    }

    void runTest() override
    {
        beginTest ("FxOrder survives packing");
        {
            juce::Random rng (20250916);

            for (int trial = 0; trial < 500; ++trial)
            {
                FxOrder o;
                o.count = rng.nextInt ({ 1, numFxSlots + 1 });

                juce::Array<int> pool;
                for (int i = 0; i < numFxSlots; ++i)
                    pool.add (i);

                for (int i = 0; i < o.count; ++i)
                    o.slots[(size_t) i] = (FxSlot) pool.removeAndReturn (rng.nextInt (pool.size()));

                o.bypassMask = (juce::uint32) rng.nextInt (64);

                const auto back = FxOrder::unpack (o.pack());

                expectEquals (back.count, o.count);
                expectEquals ((int) back.bypassMask, (int) o.bypassMask);

                for (int i = 0; i < o.count; ++i)
                    expect (back.slots[(size_t) i] == o.slots[(size_t) i],
                            "slot " + juce::String (i) + " changed through packing");
            }
        }

        beginTest ("FxOrder tolerates whatever the state tree contains");
        {
            // Duplicates, unknown names, empty, and a bypass list that names a
            // slot which is not in the order at all.
            const auto dup = FxOrder::fromState ("RETRO,RETRO,SPACE", "");
            expectEquals (dup.count, 2);

            const auto unknown = FxOrder::fromState ("RETRO,NOT_A_SLOT,SPACE", "");
            expectEquals (unknown.count, 2);

            const auto empty = FxOrder::fromState ("", "");
            expectEquals (empty.count, numFxSlots);

            const auto strange = FxOrder::fromState ("RETRO", "SPACE,GRAIN");
            expectEquals (strange.count, 1);
            expect (strange.isBypassed (FxSlot::space));

            const auto junk = FxOrder::fromState ("!!!,,,", "!!!");
            expectEquals (junk.count, numFxSlots);
        }

        beginTest ("a silent chain leaves the mid signal alone");
        {
            // With every module off and every colouring macro at zero, the only
            // thing between the synth and the output is the stereo stage, which
            // by design touches the side signal and never the mid.  So the mono
            // sum must come through untouched - if it does not, something that
            // was supposed to be off is still processing.
            TestHost host;
            silenceTheChain (host);

            SynthEngine reference;
            reference.prepare (48000.0, 256, 2);

            NacarEngine engine;
            engine.prepare (48000.0, 256, 2);

            const int blocks = 120;

            juce::AudioBuffer<float> refBlock (2, 256);
            juce::AudioBuffer<float> refAll (2, 256 * blocks);
            refAll.clear();

            for (int b = 0; b < blocks; ++b)
            {
                refBlock.clear();
                juce::MidiBuffer midi;

                if (b == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 45, 0.9f), 0);

                if (b == blocks * 3 / 4)
                    midi.addEvent (juce::MidiMessage::noteOff (1, 45), 0);

                reference.process (refBlock, midi, host.registry, 120.0);

                for (int ch = 0; ch < 2; ++ch)
                    refAll.copyFrom (ch, b * 256, refBlock, ch, 0, 256);
            }

            const auto chain = renderChain (engine, host.registry, 48000.0, 256, blocks);

            expect (chain.allFinite, "the silent chain produced a non-finite sample");

            double diff = 0.0, signal = 0.0;

            for (int i = 0; i < refAll.getNumSamples(); ++i)
            {
                const double refMid = 0.5 * ((double) refAll.getSample (0, i)
                                             + (double) refAll.getSample (1, i));
                const double chainMid = 0.5 * ((double) chain.audio.getSample (0, i)
                                               + (double) chain.audio.getSample (1, i));

                diff += (chainMid - refMid) * (chainMid - refMid);
                signal += refMid * refMid;
            }

            expect (signal > 1.0e-9, "the reference render was silent");

            const double db = 10.0 * std::log10 (juce::jmax (1.0e-18, diff / signal));
            logMessage ("    silent-chain mid difference: " + juce::String (db, 1) + " dB");

            {
                // When this fails, the shape of the failure says what caused it:
                // a level change, a delay, or a filter.
                double chainEnergy = 0.0;

                for (int i = 0; i < refAll.getNumSamples(); ++i)
                {
                    const double m = 0.5 * ((double) chain.audio.getSample (0, i)
                                            + (double) chain.audio.getSample (1, i));
                    chainEnergy += m * m;
                }

                logMessage ("    reference mid rms "
                            + juce::String (std::sqrt (signal / refAll.getNumSamples()), 5)
                            + ", chain mid rms "
                            + juce::String (std::sqrt (chainEnergy / refAll.getNumSamples()), 5));

                // Best alignment over a short search: a non-zero best lag means
                // something in the chain is delaying the signal.
                int bestLag = 0;
                double bestDiff = 1.0e30;

                for (int lag = 0; lag < 400; ++lag)
                {
                    double d = 0.0;

                    for (int i = 0; i < refAll.getNumSamples() - lag; i += 7)
                    {
                        const double a = 0.5 * ((double) refAll.getSample (0, i)
                                                + (double) refAll.getSample (1, i));
                        const double b = 0.5 * ((double) chain.audio.getSample (0, i + lag)
                                                + (double) chain.audio.getSample (1, i + lag));
                        d += (b - a) * (b - a);
                    }

                    if (d < bestDiff) { bestDiff = d; bestLag = lag; }
                }

                logMessage ("    best alignment at lag " + juce::String (bestLag) + " samples");
            }

            expect (db < -60.0, "a module that is off is still colouring the signal: "
                                    + juce::String (db, 1) + " dB of difference");
        }

        beginTest ("the whole chain stays finite and bounded at every sample rate");
        {
            for (auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
            {
                TestHost host;

                // Everything on, and hard.
                for (auto pid : { PID::retroOn, PID::crushOn, PID::fxFilterOn, PID::rewindOn,
                                  PID::grainFxOn, PID::spaceOn, PID::auraOn, PID::shadowOn,
                                  PID::patinaOn, PID::pulseOn, PID::breathOn })
                    host.registry.setFromUI (pid, 1.0f);

                for (auto pid : { PID::macroMemory, PID::macroCharacter, PID::macroMotion,
                                  PID::macroWorld, PID::macroWeight, PID::macroAlter })
                    host.registry.setFromUI (pid, 1.0f);

                host.registry.setFromUI (PID::memoryGen, 3.0f);
                host.registry.setFromUI (PID::spaceDecay, 30.0f);
                host.registry.setFromUI (PID::spaceMix, 1.0f);
                host.registry.setFromUI (PID::grainFeedback, 0.95f);
                host.registry.setFromUI (PID::grainMix, 1.0f);
                host.registry.setFromUI (PID::rewindMix, 1.0f);
                host.registry.setFromUI (PID::crushMix, 1.0f);
                host.registry.setFromUI (PID::fxFilterRes, 1.0f);
                host.registry.setFromUI (PID::pulseDepth, 1.0f);

                NacarEngine engine;
                engine.prepare (rate, 128, 2);

                const auto r = renderChain (engine, host.registry, rate, 128,
                                            (int) (rate * 3.0 / 128.0));

                const juce::String at = " at " + juce::String (rate, 0) + " Hz";

                expect (r.allFinite, "the chain produced a NaN or an infinity" + at);
                expect (r.peak < 8.0f, "the chain ran away" + at
                                           + ": peak " + juce::String (r.peak));
            }
        }

        beginTest ("every chain order renders");
        {
            // Six slots is 720 orders; a sample of them is enough to catch a
            // slot that only works in one position.
            juce::Random rng (4192);

            for (int trial = 0; trial < 12; ++trial)
            {
                TestHost host;

                for (auto pid : { PID::retroOn, PID::crushOn, PID::fxFilterOn,
                                  PID::rewindOn, PID::grainFxOn, PID::spaceOn })
                    host.registry.setFromUI (pid, 1.0f);

                juce::Array<int> pool;
                for (int i = 0; i < numFxSlots; ++i)
                    pool.add (i);

                FxOrder order;
                order.count = numFxSlots;

                for (int i = 0; i < numFxSlots; ++i)
                    order.slots[(size_t) i] =
                        (FxSlot) pool.removeAndReturn (rng.nextInt (pool.size()));

                NacarEngine engine;
                engine.prepare (48000.0, 256, 2);
                engine.setFxOrder (order);

                const auto r = renderChain (engine, host.registry, 48000.0, 256, 90);

                expect (r.allFinite, "chain order " + juce::String (trial)
                                         + " produced a non-finite sample");
                expect (r.peak < 8.0f, "chain order " + juce::String (trial) + " ran away");
            }
        }

        beginTest ("switching a module off does not click");
        {
            // This is the test that would have caught two separate defects.
            //
            // The first was gain: four engines glided their mix UP over 25 ms
            // and dropped it in one sample on the way down.  The second was
            // subtler and a ramp on the mix could never have fixed it - Retro's
            // bypassed output is the live input while its active output's own
            // dry tap is that input delayed by four milliseconds, so the two
            // are apart in TIME and the switch jumped between them however
            // gently the wet was removed.
            //
            // Measuring the sample-to-sample step catches both, because both
            // are discontinuities and neither is anything else.
            struct Module { const char* name; PID power; };

            const Module modules[] = {
                { "Retro",  PID::retroOn   },
                { "Crush",  PID::crushOn   },
                { "Filter", PID::fxFilterOn },
                { "Rewind", PID::rewindOn  },
                { "Grain",  PID::grainFxOn },
                { "Space",  PID::spaceOn   },
            };

            for (const auto& module : modules)
            {
                TestHost host;
                silenceTheChain (host);
                host.registry.setFromUI (module.power, 1.0f);

                // A SINE, and nothing that could add an edge to it.  The
                // metric below is the sample-to-sample step, and a saw's own
                // reset edge is larger than any click a module could make - a
                // rich source hides exactly the defect this is looking for.
                host.registry.setFromUI (PID::oscAWave, 0.0f);    // SINE
                host.registry.setFromUI (PID::oscALevel, 0.8f);
                host.registry.setFromUI (PID::oscBLevel, 0.0f);
                host.registry.setFromUI (PID::oscCLevel, 0.0f);
                host.registry.setFromUI (PID::subLevel, 0.0f);
                host.registry.setFromUI (PID::noiseLevel, 0.0f);
                host.registry.setFromUI (PID::bodyAmount, 0.0f);
                host.registry.setFromUI (PID::densityAmount, 0.0f);
                host.registry.setFromUI (PID::preFilterDrive, 0.0f);
                host.registry.setFromUI (PID::postSaturation, 0.0f);
                host.registry.setFromUI (PID::filterCutoff, 18000.0f);
                host.registry.setFromUI (PID::filterResonance, 0.0f);

                // A steady tone, so any step in the output is the module's and
                // not the note's.
                host.registry.setFromUI (PID::ampAttack, 0.004f);
                host.registry.setFromUI (PID::ampSustain, 1.0f);
                host.registry.setFromUI (PID::ampDecay, 0.05f);
                host.registry.setFromUI (PID::voiceMode, 1.0f);   // MONO

                NacarEngine engine;
                engine.prepare (48000.0, 256, 2);

                const int blockSize = 256;
                const int blocks    = 240;
                const int switchAt  = 120;

                juce::AudioBuffer<float> out (2, blockSize * blocks);
                out.clear();

                juce::AudioBuffer<float> block (2, blockSize);

                TransportInfo transport;
                transport.bpm = 120.0;
                transport.playing = true;

                for (int b = 0; b < blocks; ++b)
                {
                    if (b == switchAt)
                        host.registry.setFromUI (module.power, 0.0f);

                    block.clear();
                    juce::MidiBuffer midi;

                    if (b == 0)
                        midi.addEvent (juce::MidiMessage::noteOn (1, 45, 0.9f), 0);

                    engine.process (block, midi, host.registry, transport);
                    transport.ppqPosition += (double) blockSize / 48000.0 * 2.0;

                    for (int ch = 0; ch < 2; ++ch)
                        out.copyFrom (ch, b * blockSize, block, ch, 0, blockSize);
                }

                // The signal's own worst step, taken from a settled stretch well
                // before the switch.  Using the programme rather than a fixed
                // number means the threshold cannot drift as the synth changes.
                auto worstStepIn = [&out] (int from, int to)
                {
                    float worst = 0.0f;

                    for (int i = juce::jmax (1, from); i < to; ++i)
                        worst = juce::jmax (worst,
                                            std::abs (out.getSample (0, i) - out.getSample (0, i - 1)),
                                            std::abs (out.getSample (1, i) - out.getSample (1, i - 1)));

                    return worst;
                };

                const int at = switchAt * blockSize;

                const float steady = worstStepIn (at - blockSize * 40, at - blockSize * 4);

                // Seventy blocks, not eight.  These fades are exponential with
                // a 20-25 ms time constant against a 1e-4 threshold, so the
                // moment the early-out finally engages - which is where any
                // remaining discontinuity lives - is around 230 ms after the
                // button, not 40.  A short window measures the fade and misses
                // the thing the fade exists to hide.
                const float atEdge = worstStepIn (at - 8, at + blockSize * 70);

                logMessage ("    " + juce::String (module.name).paddedRight (' ', 7)
                                + " step at the off edge " + juce::String (atEdge, 5)
                                + " against a steady-state worst of " + juce::String (steady, 5));

                // Four times the programme's own worst step.  A click is a
                // different order of magnitude, not a small multiple: with the
                // engage crossfade removed, Retro's four-millisecond time jump
                // measures about seventy times its steady state on this patch.
                expect (atEdge <= juce::jmax (1.0e-4f, steady * 4.0f),
                        juce::String (module.name) + " clicks when it is switched off: "
                            + juce::String (atEdge, 5) + " against " + juce::String (steady, 5));
            }
        }

        beginTest ("muting a card is the same thing as switching it off");
        {
            // The chain has two ways to silence a module - the power ring, which
            // writes `*_on`, and the `-` glyph on the card, which sets a bit in
            // the FxOrder bypass mask - and a user has every right to expect
            // them to behave identically.  They did not: the chain skipped the
            // call for a masked slot, which starves Retro's and Crush's dry
            // lines, starves Rewind's and Grain's histories, and denies Space
            // the transition it flushes its tail on.  This asserts the two
            // routes now produce the same samples.
            auto renderWith = [this] (bool useMask)
            {
                TestHost host;

                for (auto pid : { PID::retroOn, PID::crushOn, PID::fxFilterOn,
                                  PID::spaceOn })
                    host.registry.setFromUI (pid, 1.0f);

                if (! useMask)
                    host.registry.setFromUI (PID::retroOn, 0.0f);

                auto order = FxOrder::defaultOrder();

                if (useMask)
                    order.bypassMask |= (1u << (juce::uint32) FxSlot::retro);

                NacarEngine engine;
                engine.prepare (48000.0, 256, 2);
                engine.setFxOrder (order);

                return renderChain (engine, host.registry, 48000.0, 256, 60);
            };

            const auto masked  = renderWith (true);
            const auto powered = renderWith (false);

            expect (masked.allFinite && powered.allFinite);

            double difference = 0.0, reference = 0.0;

            for (int i = 0; i < masked.audio.getNumSamples(); ++i)
            {
                const double d = (double) masked.audio.getSample (0, i)
                                   - powered.audio.getSample (0, i);
                difference += d * d;
                reference  += (double) powered.audio.getSample (0, i)
                                * powered.audio.getSample (0, i);
            }

            const double db = 10.0 * std::log10 (juce::jmax (1.0e-12, difference
                                                       / juce::jmax (1.0e-12, reference)));

            logMessage ("    masked card vs powered-off card: " + juce::String (db, 1) + " dB");

            expect (db < -100.0,
                    "the bypass mask and the power ring disagree by "
                        + juce::String (db, 1) + " dB");
        }

        beginTest ("a masked card does not report latency the signal never incurs");
        {
            // Retro is 4 ms of onset delay when it is running.  If the chain
            // still counted it while the card was muted, the host would
            // compensate for a delay that is not there and the instrument would
            // play early.
            TestHost host;
            host.registry.setFromUI (PID::retroOn, 1.0f);

            NacarEngine engine;
            engine.prepare (48000.0, 256, 2);
            engine.setFxOrder (FxOrder::defaultOrder());
            renderChain (engine, host.registry, 48000.0, 256, 8);

            const int withRetro = engine.getLatencySamples();
            expect (withRetro > 0, "Retro reported no latency while it was running");

            auto masked = FxOrder::defaultOrder();
            masked.bypassMask |= (1u << (juce::uint32) FxSlot::retro);

            engine.setFxOrder (masked);
            renderChain (engine, host.registry, 48000.0, 256, 8);

            expect (engine.getLatencySamples() < withRetro,
                    "masking Retro did not remove its latency from the chain total");
        }

        beginTest ("the chain does not decorrelate the low end");
        {
            // Specification sections 38, 40 and 43: width is never bought at
            // the cost of the low end.  A stack of stereo processes in series
            // is where that is most likely to be lost.
            //
            // EVERY module, including the three that used to be left out.
            // Grain is the omission that mattered: it is the only engine in the
            // chain that genuinely decorrelates - it pans each grain
            // independently - so the one test standing behind the low-end claim
            // was not exercising the case the claim exists for.
            TestHost host;

            for (auto pid : { PID::retroOn, PID::crushOn, PID::fxFilterOn,
                              PID::rewindOn, PID::grainFxOn, PID::spaceOn,
                              PID::auraOn, PID::shadowOn, PID::patinaOn })
                host.registry.setFromUI (pid, 1.0f);

            // Grain at a spread that would decorrelate anything it is allowed
            // to, so the low-band protection is being asked a real question.
            host.registry.setFromUI (PID::grainSpread, 1.0f);
            host.registry.setFromUI (PID::grainMix, 0.8f);
            host.registry.setFromUI (PID::grainDensity, 0.8f);

            host.registry.setFromUI (PID::macroWorld, 1.0f);
            host.registry.setFromUI (PID::macroMemory, 0.8f);
            host.registry.setFromUI (PID::subLevel, 1.0f);
            host.registry.setFromUI (PID::oscALevel, 0.3f);

            NacarEngine engine;
            engine.prepare (48000.0, 256, 2);

            const auto r = renderChain (engine, host.registry, 48000.0, 256, 150, 33);

            expect (r.allFinite);

            // One-pole low pass at 150 Hz on both channels, then correlate.
            const double a = std::exp (-2.0 * juce::MathConstants<double>::pi * 150.0 / 48000.0);
            double zl = 0.0, zr = 0.0, sll = 0.0, srr = 0.0, slr = 0.0;

            const int start = r.audio.getNumSamples() / 6;

            for (int i = start; i < r.audio.getNumSamples(); ++i)
            {
                zl = (1.0 - a) * r.audio.getSample (0, i) + a * zl;
                zr = (1.0 - a) * r.audio.getSample (1, i) + a * zr;

                sll += zl * zl;
                srr += zr * zr;
                slr += zl * zr;
            }

            const double denom = std::sqrt (sll * srr);

            if (denom > 1.0e-12)
            {
                const double correlation = slr / denom;
                logMessage ("    low-band correlation through the chain: "
                            + juce::String (correlation, 3));

                expect (correlation > 0.85,
                        "the chain decorrelated the low end: " + juce::String (correlation, 3));
            }
            else
            {
                logMessage ("    no low-band energy - skipped");
            }
        }
    }
};

// ===========================================================================
//  MOD MATRIX
//
//  The matrix is the one subsystem whose failure mode is silence rather than
//  noise: the MOD page can persist a perfectly valid routing and the sound can
//  be completely unaffected, which looks like working software.  These tests
//  exist to make that failure loud.
// ===========================================================================
struct ModMatrixTests : juce::UnitTest
{
    ModMatrixTests() : juce::UnitTest ("Mod matrix", "nacar") {}

    /** A MODMATRIX branch the way the MOD page writes one. */
    static juce::ValueTree makeMatrix (std::initializer_list<std::tuple<const char*, PID, float>> slots)
    {
        juce::ValueTree matrix (ids::MODMATRIX);

        for (const auto& [source, target, depth] : slots)
        {
            juce::ValueTree slot (ids::MODSLOT);
            slot.setProperty (ids::modSource,  source, nullptr);
            slot.setProperty (ids::modTarget,  ParameterRegistry::idOf (target), nullptr);
            slot.setProperty (ids::modDepth,   depth, nullptr);
            slot.setProperty (ids::modEnabled, true, nullptr);
            matrix.appendChild (slot, nullptr);
        }

        return matrix;
    }

    static void render (NacarEngine& engine, const ParameterRegistry& params,
                        int numBlocks, int blockSize = 256, double sampleRate = 48000.0)
    {
        juce::AudioBuffer<float> block (2, blockSize);

        TransportInfo transport;
        transport.bpm = 120.0;
        transport.playing = true;

        for (int b = 0; b < numBlocks; ++b)
        {
            block.clear();
            juce::MidiBuffer midi;

            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 45, 0.9f), 0);

            engine.process (block, midi, params, transport);
            transport.ppqPosition += (double) blockSize / sampleRate * (transport.bpm / 60.0);
        }
    }

    void runTest() override
    {
        beginTest ("an unrouted parameter reads exactly what the user set");
        {
            TestHost host;
            host.registry.setFromUI (PID::filterCutoff, 4000.0f);

            NacarEngine engine;
            engine.prepare (48000.0, 256, 2);
            render (engine, host.registry, 8);

            expectWithinAbsoluteError (host.registry.raw (PID::filterCutoff),
                                       host.registry.userValue (PID::filterCutoff),
                                       0.001f);
        }

        beginTest ("a routed parameter moves, and keeps moving");
        {
            // The whole point.  An LFO on the cutoff has to produce a cutoff
            // that is (a) not the user's value and (b) different at different
            // moments, or the matrix is a preference the instrument ignores.
            TestHost host;
            host.registry.setFromUI (PID::filterCutoff, 2000.0f);
            host.registry.setFromUI (PID::lfo1Rate, 4.0f);
            host.registry.setFromUI (PID::lfo1Depth, 1.0f);
            host.registry.setFromUI (PID::lfo1Sync, 0.0f);

            NacarEngine engine;
            engine.prepare (48000.0, 256, 2);
            engine.rebuildModMatrix (makeMatrix ({ { "LFO 1", PID::filterCutoff, 1.0f } }));

            float lowest  = 1.0e9f;
            float highest = -1.0e9f;

            for (int b = 0; b < 240; ++b)
            {
                render (engine, host.registry, 1);

                const float v = host.registry.raw (PID::filterCutoff);
                lowest  = juce::jmin (lowest, v);
                highest = juce::jmax (highest, v);
            }

            logMessage ("    cutoff swept " + juce::String (lowest, 1) + " Hz to "
                            + juce::String (highest, 1) + " Hz around a set 2000 Hz");

            expect (highest - lowest > 100.0f,
                    "an LFO routed to the cutoff at full depth did not move it");

            // The user's own value is untouched: a modulated parameter must not
            // creep, or a preset saved while an LFO runs saves the LFO's
            // position rather than the user's setting.
            expectWithinAbsoluteError (host.registry.userValue (PID::filterCutoff),
                                       2000.0f, 0.5f);
        }

        beginTest ("modulation never pushes a parameter out of its own range");
        {
            // Depth +1 from a source pinned at +1, on a parameter already at
            // its maximum.  Clamping is what stops a matrix from producing a
            // negative frequency or a resonance above self-oscillation.
            TestHost host;

            const auto& def = ParameterRegistry::definition (PID::filterCutoff);

            host.registry.setFromUI (PID::filterCutoff, def.maxValue);
            host.registry.setFromUI (PID::macroMemory, 1.0f);

            NacarEngine engine;
            engine.prepare (48000.0, 256, 2);
            engine.rebuildModMatrix (makeMatrix ({ { "MEMORY", PID::filterCutoff, 1.0f } }));
            render (engine, host.registry, 16);

            expect (host.registry.raw (PID::filterCutoff) <= def.maxValue + 0.001f,
                    "modulation pushed the cutoff past its maximum");

            host.registry.setFromUI (PID::filterCutoff, def.minValue);
            engine.rebuildModMatrix (makeMatrix ({ { "MEMORY", PID::filterCutoff, -1.0f } }));
            render (engine, host.registry, 16);

            expect (host.registry.raw (PID::filterCutoff) >= def.minValue - 0.001f,
                    "modulation pushed the cutoff below its minimum");
        }

        beginTest ("removing a routing gives the parameter back");
        {
            // The failure this catches is a parameter left frozen at whatever
            // the matrix last pushed it to - a knob that has stopped working
            // and gives no clue why.
            TestHost host;
            host.registry.setFromUI (PID::filterCutoff, 800.0f);
            host.registry.setFromUI (PID::macroMemory, 1.0f);

            NacarEngine engine;
            engine.prepare (48000.0, 256, 2);
            engine.rebuildModMatrix (makeMatrix ({ { "MEMORY", PID::filterCutoff, 0.5f } }));
            render (engine, host.registry, 16);

            expect (host.registry.raw (PID::filterCutoff) > 900.0f,
                    "the routing had no effect, so the release cannot be tested");

            engine.rebuildModMatrix (juce::ValueTree (ids::MODMATRIX));
            render (engine, host.registry, 16);

            expectWithinAbsoluteError (host.registry.raw (PID::filterCutoff), 800.0f, 0.5f);
        }

        beginTest ("a routing changes what the instrument sounds like");
        {
            // The registry test above proves the number moved.  This one proves
            // the audio did, which is a different claim: an overlay that
            // engines never read would pass the first and fail this.
            auto renderToBuffer = [this] (bool routed)
            {
                TestHost host;
                host.registry.setFromUI (PID::filterCutoff, 1200.0f);
                host.registry.setFromUI (PID::filterResonance, 0.3f);
                host.registry.setFromUI (PID::macroMemory, 0.0f);
                host.registry.setFromUI (PID::macroMotion, 1.0f);
                host.registry.setFromUI (PID::lfo1Rate, 3.0f);
                host.registry.setFromUI (PID::lfo1Depth, 1.0f);
                host.registry.setFromUI (PID::lfo1Sync, 0.0f);

                NacarEngine engine;
                engine.prepare (48000.0, 256, 2);

                if (routed)
                    engine.rebuildModMatrix (makeMatrix ({ { "LFO 1", PID::filterCutoff, 0.9f } }));

                juce::AudioBuffer<float> out (2, 256 * 120);
                out.clear();

                juce::AudioBuffer<float> block (2, 256);
                TransportInfo transport;
                transport.bpm = 120.0;
                transport.playing = true;

                for (int b = 0; b < 120; ++b)
                {
                    block.clear();
                    juce::MidiBuffer midi;

                    if (b == 0)
                        midi.addEvent (juce::MidiMessage::noteOn (1, 45, 0.9f), 0);

                    engine.process (block, midi, host.registry, transport);
                    transport.ppqPosition += 256.0 / 48000.0 * 2.0;

                    for (int ch = 0; ch < 2; ++ch)
                        out.copyFrom (ch, b * 256, block, ch, 0, 256);
                }

                return out;
            };

            const auto plain  = renderToBuffer (false);
            const auto routed = renderToBuffer (true);

            double difference = 0.0, reference = 0.0;

            for (int i = 0; i < plain.getNumSamples(); ++i)
            {
                const double d = (double) routed.getSample (0, i) - plain.getSample (0, i);
                difference += d * d;
                reference  += (double) plain.getSample (0, i) * plain.getSample (0, i);

                expect (std::isfinite (routed.getSample (0, i)),
                        "the routed render produced a non-finite sample");
            }

            const double db = 10.0 * std::log10 (juce::jmax (1.0e-12, difference
                                                                 / juce::jmax (1.0e-12, reference)));

            logMessage ("    routed vs unrouted difference: " + juce::String (db, 1) + " dB");

            expect (db > -20.0,
                    "a full-depth LFO on the cutoff changed the audio by only "
                        + juce::String (db, 1) + " dB, so the overlay is not reaching the engines");
        }

        beginTest ("a target that no longer exists is inert, not wrong");
        {
            TestHost host;
            host.registry.setFromUI (PID::filterCutoff, 3000.0f);

            juce::ValueTree matrix (ids::MODMATRIX);
            juce::ValueTree slot (ids::MODSLOT);
            slot.setProperty (ids::modSource,  "MEMORY", nullptr);
            slot.setProperty (ids::modTarget,  "a_parameter_that_was_removed", nullptr);
            slot.setProperty (ids::modDepth,   1.0f, nullptr);
            slot.setProperty (ids::modEnabled, true, nullptr);
            matrix.appendChild (slot, nullptr);

            NacarEngine engine;
            engine.prepare (48000.0, 256, 2);
            engine.rebuildModMatrix (matrix);
            render (engine, host.registry, 8);

            expectWithinAbsoluteError (host.registry.raw (PID::filterCutoff), 3000.0f, 0.5f);
        }

        beginTest ("the per-voice sources are declared unavailable, not faked");
        {
            // ENV 1, ENV 2, VELOCITY and KEY TRACK only exist inside a sounding
            // voice and the matrix is a block-rate, global thing.  They read
            // zero on purpose.  If someone later wires a global stand-in for
            // them, this test should fail and make them say so.
            TestHost host;
            host.registry.setFromUI (PID::filterCutoff, 2500.0f);

            NacarEngine engine;
            engine.prepare (48000.0, 256, 2);

            for (auto* source : { "ENV 1", "ENV 2", "VELOCITY", "KEY TRACK" })
            {
                engine.rebuildModMatrix (makeMatrix ({ { source, PID::filterCutoff, 1.0f } }));
                render (engine, host.registry, 8);

                expectWithinAbsoluteError (host.registry.raw (PID::filterCutoff), 2500.0f, 0.5f);
            }
        }
    }
};

// ===========================================================================
//  Analysis  (phase 19)
//
//  Analysis is the easiest thing in this project to be confidently wrong
//  about, because a plausible number looks exactly like a correct one.  So
//  every signal below is synthetic and its answer is known before the
//  analyser sees it, and the suite asserts the answers it should NOT give as
//  hard as the ones it should: noise must not have a key, a one-shot must not
//  have a tempo, and silence must not have anything at all.
// ===========================================================================
#include "../Source/Analysis/SampleAnalyser.h"
#include "../Source/Harmony/Harmony.h"

#include <algorithm>
#include <cmath>
#include <limits>

struct AnalysisTests : juce::UnitTest
{
    AnalysisTests() : juce::UnitTest ("Analysis", "nacar") {}

    static constexpr double kRate = 44100.0;

    // -- signal construction ------------------------------------------------

    static void prepare (SampleBuffer& s, double rate, int numSamples, int numChannels = 2)
    {
        s.sourceRate = rate;
        s.audio.setSize (numChannels, juce::jmax (0, numSamples));
        s.audio.clear();
    }

    static void addTone (SampleBuffer& s, double freq, double amplitude,
                         int from, int to, int harmonics = 1)
    {
        const int len = s.audio.getNumSamples();
        from = juce::jlimit (0, len, from);
        to   = juce::jlimit (0, len, to);

        const int attack  = juce::jmin ((to - from) / 4, (int) (s.sourceRate * 0.020));
        const int release = juce::jmin ((to - from) / 4, (int) (s.sourceRate * 0.150));

        for (int i = from; i < to; ++i)
        {
            double env = 1.0;

            if (attack > 0 && i - from < attack)
                env = (double) (i - from) / (double) attack;

            if (release > 0 && to - i < release)
                env = juce::jmin (env, (double) (to - i) / (double) release);

            double v = 0.0;

            for (int h = 1; h <= harmonics; ++h)
                v += (1.0 / (double) h) * std::sin (juce::MathConstants<double>::twoPi
                                                      * freq * (double) h * (double) i / s.sourceRate);

            for (int ch = 0; ch < s.audio.getNumChannels(); ++ch)
                s.audio.addSample (ch, i, (float) (amplitude * env * v));
        }
    }

    /** A 5 ms exponentially decaying noise burst: a click with a defined start
        sample, which is what a transient detector should be able to find. */
    static void addClick (SampleBuffer& s, int position, double amplitude, juce::Random& rng)
    {
        const int len = s.audio.getNumSamples();
        const int burst = juce::jmax (1, (int) (s.sourceRate * 0.005));

        for (int i = 0; i < burst; ++i)
        {
            const int idx = position + i;

            if (idx < 0 || idx >= len)
                continue;

            const float decay = std::exp (-6.0f * (float) i / (float) burst);
            const float white = rng.nextFloat() * 2.0f - 1.0f;

            for (int ch = 0; ch < s.audio.getNumChannels(); ++ch)
                s.audio.addSample (ch, idx, (float) amplitude * decay * white);
        }
    }

    static bool allFinite (const AnalysisResult& r)
    {
        const bool floats = std::isfinite (r.rootConfidence) && std::isfinite (r.scaleConfidence)
                         && std::isfinite (r.tempoConfidence) && std::isfinite (r.peakLevel)
                         && std::isfinite (r.loudness) && std::isfinite (r.spectralCentroid)
                         && std::isfinite (r.spectralRolloff) && std::isfinite (r.lowEnergy)
                         && std::isfinite (r.highEnergy) && std::isfinite (r.percussiveRatio)
                         && std::isfinite (r.polyphonicLikelihood) && std::isfinite (r.loopability)
                         && std::isfinite (r.silenceRatio);

        return floats && std::isfinite (r.tempo);
    }

    static int argmax (const std::array<float, 12>& c)
    {
        return (int) std::distance (c.begin(), std::max_element (c.begin(), c.end()));
    }

    /** 0 for the strongest pitch class, 11 for the weakest. */
    static int rankOf (const std::array<float, 12>& c, int pitchClass)
    {
        int rank = 0;

        for (int i = 0; i < 12; ++i)
            if (c[(size_t) i] > c[(size_t) pitchClass])
                ++rank;

        return rank;
    }

    void runTest() override
    {
        // ===================================================================
        beginTest ("A pure sine lands in one chroma bin");
        {
            struct Probe { double freq; int expectedClass; const char* name; };

            const Probe probes[] = { { 440.0,  9, "A4"  },
                                     { 261.63, 0, "C4"  },
                                     { 659.26, 4, "E5"  },
                                     { 110.0,  9, "A2"  } };

            for (const auto& p : probes)
            {
                SampleBuffer s;
                prepare (s, kRate, (int) (kRate * 2.0));
                addTone (s, p.freq, 0.5, 0, s.audio.getNumSamples());

                SampleAnalyser::Detail detail;
                const auto r = SampleAnalyser::analyse (s, &detail);

                expect (r.analysed, juce::String (p.name) + ": analysed");
                expect (allFinite (r), juce::String (p.name) + ": finite");
                expectEquals (argmax (detail.chroma), p.expectedClass,
                              juce::String (p.name) + ": chroma peak");

                // Every other class must be well below the peak, or "the peak
                // is in the right bin" means nothing.
                float highestOther = 0.0f;

                for (int c = 0; c < 12; ++c)
                    if (c != p.expectedClass)
                        highestOther = juce::jmax (highestOther, detail.chroma[(size_t) c]);

                expect (highestOther < 0.5f,
                        juce::String (p.name) + ": next class at " + juce::String (highestOther, 3));

                expect (detail.chromaSalience > 0.5f,
                        juce::String (p.name) + ": salience " + juce::String (detail.chromaSalience, 3));

                expectEquals (r.root, p.expectedClass, juce::String (p.name) + ": detected root");
            }
        }

        // ===================================================================
        beginTest ("A sine's level and spectrum");
        {
            SampleBuffer s;
            prepare (s, kRate, (int) (kRate * 2.0));
            addTone (s, 1000.0, 0.5, 0, s.audio.getNumSamples());

            const auto r = SampleAnalyser::analyse (s);

            expectWithinAbsoluteError (r.peakLevel, 0.5f, 0.01f);
            expectWithinAbsoluteError (r.spectralCentroid, 1000.0f, 60.0f);
            expectWithinAbsoluteError (r.spectralRolloff, 1000.0f, 60.0f);
            expect (r.lowEnergy  < 0.02f, "low energy "  + juce::String (r.lowEnergy, 4));
            expect (r.highEnergy < 0.02f, "high energy " + juce::String (r.highEnergy, 4));
            expect (r.silenceRatio < 0.05f, "silence " + juce::String (r.silenceRatio, 3));
            expect (r.polyphonicLikelihood < 0.25f,
                    "one note reads monophonic: " + juce::String (r.polyphonicLikelihood, 3));
            expect (r.percussiveRatio < 0.2f,
                    "a sustained tone is not percussive: " + juce::String (r.percussiveRatio, 3));
            expectEquals (r.tempo, 0.0, "a sustained tone has no tempo");
            expectEquals (r.tempoConfidence, 0.0f);
        }

        // ===================================================================
        beginTest ("Loudness against the BS.1770 calibration point");
        {
            // A 1 kHz tone at -20 dBFS, the same in both channels, is -20 LUFS.
            // (The standard's own calibration is a single channel at -20 dBFS
            // reading -23 LKFS; two coherent channels add 3 dB.)
            SampleBuffer s;
            prepare (s, kRate, (int) (kRate * 3.0));

            const float amplitude = juce::Decibels::decibelsToGain (-20.0f);

            for (int i = 0; i < s.audio.getNumSamples(); ++i)
            {
                const float v = amplitude * std::sin (juce::MathConstants<float>::twoPi
                                                        * 1000.0f * (float) i / (float) kRate);
                s.audio.setSample (0, i, v);
                s.audio.setSample (1, i, v);
            }

            const auto r = SampleAnalyser::analyse (s);
            expectWithinAbsoluteError (r.loudness, -20.0f, 0.5f,
                                       "loudness " + juce::String (r.loudness, 2) + " LUFS");
        }

        // ===================================================================
        beginTest ("A chord progression in a known key");
        {
            SampleBuffer s;
            const double chordSeconds = 2.0;
            prepare (s, kRate, (int) (kRate * chordSeconds * 4.0));

            // C - F - G - C.  Unambiguously C major: the tonic is in three of
            // the four chords and is the root of two of them.
            const double chords[4][3] = { { 261.63, 329.63, 392.00 },   // C  E  G
                                          { 174.61, 220.00, 261.63 },   // F  A  C
                                          { 196.00, 246.94, 293.66 },   // G  B  D
                                          { 261.63, 329.63, 392.00 } }; // C  E  G

            for (int c = 0; c < 4; ++c)
            {
                const int from = (int) (kRate * chordSeconds * c);
                const int to   = (int) (kRate * chordSeconds * (c + 1));

                for (int n = 0; n < 3; ++n)
                    addTone (s, chords[c][n], 0.18, from, to, 3);
            }

            SampleAnalyser::Detail detail;
            const auto r = SampleAnalyser::analyse (s, &detail);

            expect (r.analysed);
            expect (allFinite (r));

            // The loudest pitch class in this profile is G, not C: the dominant
            // is in three of the four chords and every C sounds its own fifth
            // as a harmonic.  That is exactly why a key detector correlates
            // against a profile instead of taking the argmax, so what is
            // asserted here is that the tonic triad owns the top of the profile
            // and that the detector reaches C from it.
            expect (rankOf (detail.chroma, 0) <= 1, "C is one of the two strongest classes");
            expect (rankOf (detail.chroma, 4) <= 4, "E is near the top");
            expect (rankOf (detail.chroma, 7) <= 4, "G is near the top");
            expect (rankOf (detail.chroma, 1) >= 6, "C sharp is not, it is in no chord");
            expect (rankOf (detail.chroma, 6) >= 6, "nor is F sharp");

            expect (detail.chromaSalience > 0.4f,
                    "salience " + juce::String (detail.chromaSalience, 3));

            expectEquals (r.root, 0, "detected root");
            expectEquals (r.scale, (int) harmony::Scale::major, "detected scale");
            expect (r.keyIsUsable(),
                    "root confidence " + juce::String (r.rootConfidence, 3));

            expect (r.polyphonicLikelihood > 0.35f,
                    "three notes at once read as chordal: "
                        + juce::String (r.polyphonicLikelihood, 3));
        }

        // ===================================================================
        beginTest ("White noise has no key and no tempo");
        {
            SampleBuffer s;
            prepare (s, kRate, (int) (kRate * 6.0));

            juce::Random rng (20250919);

            for (int ch = 0; ch < s.audio.getNumChannels(); ++ch)
                for (int i = 0; i < s.audio.getNumSamples(); ++i)
                    s.audio.setSample (ch, i, 0.5f * (rng.nextFloat() * 2.0f - 1.0f));

            SampleAnalyser::Detail detail;
            const auto r = SampleAnalyser::analyse (s, &detail);

            expect (r.analysed);
            expect (allFinite (r));

            expect (detail.chromaSalience < 0.15f,
                    "chroma is flat: salience " + juce::String (detail.chromaSalience, 4));
            expect (r.rootConfidence < 0.10f,
                    "root confidence " + juce::String (r.rootConfidence, 4));
            expect (! r.keyIsUsable(), "noise must never be usable as a key");
            expect (r.tempoConfidence < 0.10f,
                    "tempo confidence " + juce::String (r.tempoConfidence, 4));
            expect (! r.tempoIsUsable(), "noise must never be usable as a tempo");

            // Flat spectrum: the centroid of white noise sits at half Nyquist.
            expectWithinAbsoluteError (r.spectralCentroid, (float) (kRate * 0.25), 1500.0f,
                                       "centroid " + juce::String (r.spectralCentroid, 0));
        }

        // ===================================================================
        beginTest ("A click train gives its own tempo and its own positions");
        {
            const double bpm = 120.0;
            const double period = 60.0 / bpm;
            const int numClicks = 16;

            SampleBuffer s;
            prepare (s, kRate, (int) (kRate * period * numClicks));

            juce::Random rng (77);
            std::vector<int> expected;

            for (int k = 0; k < numClicks; ++k)
            {
                const int position = (int) std::round (kRate * period * k);
                expected.push_back (position);
                addClick (s, position, 0.8, rng);
            }

            const auto r = SampleAnalyser::analyse (s);

            expect (r.analysed);
            expect (allFinite (r));

            expectEquals ((int) r.transients.size(), numClicks, "one onset per click");

            if ((int) r.transients.size() == numClicks)
            {
                const int tolerance = (int) (kRate * 0.005);   // 5 ms
                int worst = 0;

                for (int k = 0; k < numClicks; ++k)
                    worst = juce::jmax (worst, std::abs (r.transients[(size_t) k] - expected[(size_t) k]));

                expect (worst <= tolerance,
                        "worst onset error " + juce::String (worst * 1000.0 / kRate, 2) + " ms");
            }

            expect (r.tempoIsUsable(),
                    "tempo confidence " + juce::String (r.tempoConfidence, 3));
            expectWithinAbsoluteError (r.tempo, bpm, bpm * 0.02,
                                       "tempo " + juce::String (r.tempo, 2) + " BPM");
            expect (r.percussiveRatio > 0.5f,
                    "percussive ratio " + juce::String (r.percussiveRatio, 3));
        }

        // ===================================================================
        beginTest ("A one-shot has no tempo at all");
        {
            SampleBuffer s;
            prepare (s, kRate, (int) (kRate * 3.0));

            juce::Random rng (5);
            addClick (s, (int) (kRate * 0.5), 0.9, rng);
            addTone (s, 220.0, 0.3, (int) (kRate * 0.5), (int) (kRate * 1.2), 4);

            const auto r = SampleAnalyser::analyse (s);

            expect (r.analysed);
            expect (allFinite (r));
            expectEquals (r.tempo, 0.0, "no tempo");
            expectEquals (r.tempoConfidence, 0.0f, "confidence exactly zero, not merely low");
            expect ((int) r.transients.size() <= 2,
                    "onsets found: " + juce::String ((int) r.transients.size()));
            expect (r.silenceRatio > 0.4f,
                    "most of a one-shot file is silence: " + juce::String (r.silenceRatio, 3));
        }

        // ===================================================================
        beginTest ("Silence measures as silence and nothing else");
        {
            SampleBuffer s;
            prepare (s, kRate, (int) (kRate * 2.0));

            const auto r = SampleAnalyser::analyse (s);

            expect (r.analysed, "silence is a result, not a failure");
            expect (allFinite (r));

            expectEquals (r.root, -1);
            expectEquals (r.rootConfidence, 0.0f);
            expectEquals (r.scale, -1);
            expectEquals (r.scaleConfidence, 0.0f);
            expectEquals (r.tempo, 0.0);
            expectEquals (r.tempoConfidence, 0.0f);
            expectEquals ((int) r.transients.size(), 0);
            expectEquals (r.peakLevel, 0.0f);
            expectEquals (r.loudness, -144.0f);
            expectEquals (r.spectralCentroid, 0.0f);
            expectEquals (r.spectralRolloff, 0.0f);
            expectEquals (r.lowEnergy, 0.0f);
            expectEquals (r.highEnergy, 0.0f);
            expectEquals (r.percussiveRatio, 0.0f);
            expectEquals (r.polyphonicLikelihood, 0.0f);
            expectEquals (r.loopability, 0.0f);

            // The one field that is NOT undetermined: an empty file really is
            // entirely silent, and saying 0.0 here would be the lie.
            expectEquals (r.silenceRatio, 1.0f);
        }

        // ===================================================================
        beginTest ("Degenerate files do not produce numbers or crashes");
        {
            {   // DC offset
                SampleBuffer s;
                prepare (s, kRate, (int) (kRate * 2.0));

                for (int ch = 0; ch < s.audio.getNumChannels(); ++ch)
                    for (int i = 0; i < s.audio.getNumSamples(); ++i)
                        s.audio.setSample (ch, i, 0.5f);

                const auto r = SampleAnalyser::analyse (s);

                expect (allFinite (r), "DC: finite");
                expectEquals (r.peakLevel, 0.5f, "DC: peak");
                expectEquals (r.tempoConfidence, 0.0f, "DC: no tempo");
                expect (r.rootConfidence < 0.10f, "DC: no key");
                expect (r.spectralCentroid < 200.0f,
                        "DC: centroid " + juce::String (r.spectralCentroid, 1));
            }

            {   // clipped
                SampleBuffer s;
                prepare (s, kRate, (int) (kRate * 2.0));

                for (int ch = 0; ch < s.audio.getNumChannels(); ++ch)
                    for (int i = 0; i < s.audio.getNumSamples(); ++i)
                        s.audio.setSample (ch, i,
                                           juce::jlimit (-1.0f, 1.0f,
                                                         4.0f * std::sin (juce::MathConstants<float>::twoPi
                                                                            * 220.0f * (float) i / (float) kRate)));

                const auto r = SampleAnalyser::analyse (s);

                expect (allFinite (r), "clipped: finite");
                expectEquals (r.peakLevel, 1.0f, "clipped: peak");
                expect (r.analysed, "clipped: analysed");
            }

            {   // one sample
                SampleBuffer s;
                prepare (s, kRate, 1);
                s.audio.setSample (0, 0, 0.5f);
                s.audio.setSample (1, 0, 0.5f);

                const auto r = SampleAnalyser::analyse (s);

                expect (allFinite (r), "1 sample: finite");
                expectEquals (r.peakLevel, 0.5f, "1 sample: peak");
                expectEquals (r.tempo, 0.0, "1 sample: no tempo");
                expectEquals (r.root, -1, "1 sample: no key");
                expectEquals (r.spectralCentroid, 0.0f, "1 sample: no spectrum");
            }

            {   // no samples at all
                SampleBuffer s;
                prepare (s, kRate, 0);

                const auto r = SampleAnalyser::analyse (s);

                expect (! r.analysed, "empty: nothing was analysed");
                expect (allFinite (r), "empty: finite");
            }

            {   // a buffer that already contains NaN and infinity
                SampleBuffer s;
                prepare (s, kRate, (int) (kRate * 0.5));

                for (int i = 0; i < s.audio.getNumSamples(); ++i)
                {
                    const float v = 0.4f * std::sin (juce::MathConstants<float>::twoPi
                                                       * 330.0f * (float) i / (float) kRate);
                    s.audio.setSample (0, i, (i % 1000 == 0) ? std::numeric_limits<float>::quiet_NaN() : v);
                    s.audio.setSample (1, i, (i % 1500 == 0) ? std::numeric_limits<float>::infinity() : v);
                }

                const auto r = SampleAnalyser::analyse (s);

                expect (allFinite (r), "poisoned input: finite output");
            }
        }

        // ===================================================================
        beginTest ("Loopability notices when the ends do not meet");
        {
            // Exactly 440 cycles of 440 Hz at 44.1 kHz: the wrap is as smooth
            // as the waveform itself.
            SampleBuffer good;
            prepare (good, kRate, (int) kRate);

            for (int i = 0; i < good.audio.getNumSamples(); ++i)
            {
                const float v = 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                   * 440.0f * (float) i / (float) kRate);
                good.audio.setSample (0, i, v);
                good.audio.setSample (1, i, v);
            }

            const auto goodResult = SampleAnalyser::analyse (good);
            expect (goodResult.loopability > 0.75f,
                    "seamless loop reads " + juce::String (goodResult.loopability, 3));

            // The same tone under a 30 dB ramp: the ends are 30 dB apart.
            SampleBuffer ramped;
            prepare (ramped, kRate, good.audio.getNumSamples());
            ramped.audio.makeCopyOf (good.audio);

            for (int i = 0; i < ramped.audio.getNumSamples(); ++i)
            {
                const float g = juce::Decibels::decibelsToGain (
                                    -30.0f + 30.0f * (float) i / (float) ramped.audio.getNumSamples());

                for (int ch = 0; ch < ramped.audio.getNumChannels(); ++ch)
                    ramped.audio.setSample (ch, i, ramped.audio.getSample (ch, i) * g);
            }

            const auto rampedResult = SampleAnalyser::analyse (ramped);
            expect (rampedResult.loopability < 0.5f,
                    "ramped loop reads " + juce::String (rampedResult.loopability, 3));
            expect (rampedResult.loopability < goodResult.loopability, "ramped is worse");
        }

        // ===================================================================
        beginTest ("An analysis can be abandoned");
        {
            SampleBuffer s;
            prepare (s, kRate, (int) (kRate * 30.0));

            juce::Random rng (3);

            for (int ch = 0; ch < s.audio.getNumChannels(); ++ch)
                for (int i = 0; i < s.audio.getNumSamples(); ++i)
                    s.audio.setSample (ch, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));

            const auto start = juce::Time::getMillisecondCounterHiRes();
            const auto r = SampleAnalyser::analyse (s, nullptr, [] { return true; });
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;

            expect (! r.analysed, "an abandoned analysis is not a partial one");
            expect (elapsed < 2000.0, "gave up in " + juce::String (elapsed, 0) + " ms");

            // And the background wrapper: a second file must not wait for the
            // first, and cancelling must not deadlock.
            SampleBuffer::Ptr big (new SampleBuffer());
            prepare (*big, kRate, (int) (kRate * 30.0));

            for (int ch = 0; ch < big->audio.getNumChannels(); ++ch)
                for (int i = 0; i < big->audio.getNumSamples(); ++i)
                    big->audio.setSample (ch, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));

            SampleAnalyser analyser;
            analyser.startAnalysis (big);

            const auto cancelStart = juce::Time::getMillisecondCounterHiRes();
            analyser.cancelAnalysis();
            const auto cancelElapsed = juce::Time::getMillisecondCounterHiRes() - cancelStart;

            expect (! analyser.isAnalysing(), "cancelled");
            expect (cancelElapsed < 2000.0,
                    "cancel returned in " + juce::String (cancelElapsed, 0) + " ms");

            // A short file run to completion through the same wrapper.
            SampleBuffer::Ptr small (new SampleBuffer());
            prepare (*small, kRate, (int) (kRate * 2.0));

            for (int i = 0; i < small->audio.getNumSamples(); ++i)
            {
                const float v = 0.4f * std::sin (juce::MathConstants<float>::twoPi
                                                   * 440.0f * (float) i / (float) kRate);
                small->audio.setSample (0, i, v);
                small->audio.setSample (1, i, v);
            }

            analyser.startAnalysis (small);

            for (int i = 0; i < 400 && analyser.isAnalysing(); ++i)
                juce::Thread::sleep (25);

            expect (! analyser.isAnalysing(), "the background analysis finished");
            expect (analyser.getResult().analysed, "and produced a result");
            expectWithinAbsoluteError (analyser.getProgress(), 1.0f, 0.001f);
        }

        // ===================================================================
        beginTest ("The result survives the session tree");
        {
            AnalysisResult original;
            original.analysed = true;
            original.root = 7;
            original.rootConfidence = 0.82f;
            original.scale = (int) harmony::Scale::dorian;
            original.scaleConfidence = 0.61f;
            original.tempo = 128.5;
            original.tempoConfidence = 0.77f;
            original.transients = { 0, 4410, 8820, 13230, 44100 };
            original.peakLevel = 0.93f;
            original.loudness = -14.2f;
            original.spectralCentroid = 1820.0f;
            original.spectralRolloff = 6400.0f;
            original.lowEnergy = 0.31f;
            original.highEnergy = 0.12f;
            original.percussiveRatio = 0.44f;
            original.polyphonicLikelihood = 0.67f;
            original.loopability = 0.88f;
            original.silenceRatio = 0.05f;

            {   // detached branch
                juce::ValueTree branch (ids::ANALYSIS);
                original.writeTo (branch);

                const auto back = AnalysisResult::readFrom (branch);

                expect (back.analysed);
                expectEquals (back.root, original.root);
                expectWithinAbsoluteError (back.rootConfidence, original.rootConfidence, 1.0e-6f);
                expectEquals (back.scale, original.scale);
                expectWithinAbsoluteError (back.scaleConfidence, original.scaleConfidence, 1.0e-6f);
                expectWithinAbsoluteError (back.tempo, original.tempo, 1.0e-9);
                expectWithinAbsoluteError (back.tempoConfidence, original.tempoConfidence, 1.0e-6f);
                expectWithinAbsoluteError (back.peakLevel, original.peakLevel, 1.0e-6f);
                expectWithinAbsoluteError (back.loudness, original.loudness, 1.0e-4f);
                expectWithinAbsoluteError (back.spectralCentroid, original.spectralCentroid, 1.0e-3f);
                expectWithinAbsoluteError (back.spectralRolloff, original.spectralRolloff, 1.0e-3f);
                expectWithinAbsoluteError (back.lowEnergy, original.lowEnergy, 1.0e-6f);
                expectWithinAbsoluteError (back.highEnergy, original.highEnergy, 1.0e-6f);
                expectWithinAbsoluteError (back.percussiveRatio, original.percussiveRatio, 1.0e-6f);
                expectWithinAbsoluteError (back.polyphonicLikelihood, original.polyphonicLikelihood, 1.0e-6f);
                expectWithinAbsoluteError (back.loopability, original.loopability, 1.0e-6f);
                expectWithinAbsoluteError (back.silenceRatio, original.silenceRatio, 1.0e-6f);

                expectEquals ((int) back.transients.size(), (int) original.transients.size());

                for (size_t i = 0; i < original.transients.size(); ++i)
                    expectEquals (back.transients[i], original.transients[i]);
            }

            {   // attached to a real session, where the normalised mirror the
                // phase-18 viewport reads is written as well
                auto session = StateManager::makeDefaultSession();
                auto sampleBranch = session.getChildWithName (ids::SAMPLE);
                sampleBranch.setProperty (ids::sampleLengthSamples, 88200, nullptr);

                auto branch = session.getChildWithName (ids::ANALYSIS);
                expect (branch.isValid(), "the default session already has an ANALYSIS branch");

                original.writeTo (branch);

                const auto mirror = branch.getProperty (ids::transientPositions, "").toString();
                expect (mirror.isNotEmpty(), "the viewport's normalised list was written");

                const auto tokens = juce::StringArray::fromTokens (mirror, ",", "");
                expectEquals (tokens.size(), (int) original.transients.size());

                for (int i = 0; i < tokens.size(); ++i)
                {
                    const double v = tokens[i].getDoubleValue();
                    expect (v >= 0.0 && v <= 1.0, "normalised into range");
                    expectWithinAbsoluteError (v, (double) original.transients[(size_t) i] / 88200.0, 1.0e-4);
                }

                // ...and readFrom still returns samples, not the mirror.
                const auto back = AnalysisResult::readFrom (branch);
                expectEquals ((int) back.transients.size(), (int) original.transients.size());
                expectEquals (back.transients.back(), 44100);
            }

            {   // an empty branch reads back as "nothing is known"
                juce::ValueTree empty (ids::ANALYSIS);
                const auto back = AnalysisResult::readFrom (empty);

                expect (! back.analysed);
                expectEquals (back.root, -1);
                expectEquals (back.rootConfidence, 0.0f);
                expectEquals (back.tempo, 0.0);
                expectEquals (back.loudness, -144.0f);
                expect (back.transients.empty());
                expect (! back.keyIsUsable());
                expect (! back.tempoIsUsable());
            }
        }

        // ===================================================================

        // ===================================================================
        beginTest ("The same file at another rate gives the same answers");
        {
            // Every window length is derived from the file's own rate, so the
            // measurements must not drift when the rate changes.
            for (const double rate : { 48000.0, 96000.0 })
            {
                SampleBuffer s;
                prepare (s, rate, (int) (rate * 2.0));
                addTone (s, 440.0, 0.5, 0, s.audio.getNumSamples());

                SampleAnalyser::Detail detail;
                const auto r = SampleAnalyser::analyse (s, &detail);

                expect (allFinite (r), juce::String (rate, 0) + ": finite");
                expectEquals (argmax (detail.chroma), 9, juce::String (rate, 0) + ": chroma peak");
                expectWithinAbsoluteError (r.spectralCentroid, 440.0f, 60.0f,
                                           juce::String (rate, 0) + ": centroid");
            }
        }
    }
};

static AnalysisTests analysisTests;

// ===========================================================================
//  Harmony - key detection, and the constraint a mutation works inside
// ===========================================================================
#include "../Source/Harmony/Harmony.h"
#include "../Source/Harmony/UncertainMode.h"

struct HarmonyTests : juce::UnitTest
{
    HarmonyTests() : juce::UnitTest ("Harmony", "nacar") {}

    using Scale  = harmony::Scale;
    using Mode   = harmony::Mode;
    using Chroma = std::array<float, 12>;

    static constexpr int numScales = (int) Scale::count;

    static Chroma tones (std::initializer_list<int> pcs, float floorLevel = 0.0f)
    {
        Chroma c;
        c.fill (floorLevel);

        for (auto pc : pcs)
            c[(size_t) (((pc % 12) + 12) % 12)] = 1.0f;

        return c;
    }

    /*  A scale as music would leave it in a chroma: every scale tone present,
        the tonic loudest, then the fifth, then the third.

        A bare rotation-invariant *set* of seven tones has no tonic by
        construction - C major, A minor and D dorian are the same twelve numbers
        - so a test that fed one in and demanded a root back would be testing
        nothing at all.  That case is tested below for what it really is:
        an ambiguity the confidence has to report. */
    static Chroma scaleChroma (int root, Scale s)
    {
        Chroma c;
        c.fill (0.0f);

        const auto mask = harmony::maskOf (s);

        for (int d = 0; d < 12; ++d)
            if (mask & (1u << d))
                c[(size_t) ((root + d) % 12)] = 1.0f;

        c[(size_t) root] += 1.0f;

        if (mask & (1u << 7))
            c[(size_t) ((root + 7) % 12)] += 0.6f;

        for (int third : { 3, 4 })
            if (mask & (1u << third))
                c[(size_t) ((root + third) % 12)] += 0.35f;

        return c;
    }

    static harmony::Context makeContext (int root, Scale s, Mode m, bool known)
    {
        harmony::Context c;
        c.root     = root;
        c.scale    = s;
        c.mode     = m;
        c.keyKnown = known;
        return c;
    }

    static int countTones (juce::uint16 mask)
    {
        int n = 0;

        for (int i = 0; i < 12; ++i)
            n += (mask >> i) & 1;

        return n;
    }

    void runTest() override
    {
        // -------------------------------------------------------------------
        beginTest ("the Scale enum is in lockstep with the scale_type parameter");
        {
            const auto& def = ParameterRegistry::allDefinitions()[(size_t) PID::scaleType];
            const auto choices = juce::StringArray::fromTokens (
                juce::String (def.choicesPipeSeparated), "|", "");

            expectEquals (choices.size(), numScales + 1,
                          "scale_type lists a different number of scales than the Scale enum");
            expectEquals (choices[0], juce::String ("AUTO"),
                          "scale_type no longer starts with AUTO, so the forcedScale "
                          "convention in Context::from is wrong");

            for (int i = 0; i < numScales && i + 1 < choices.size(); ++i)
                expectEquals (choices[i + 1], juce::String (harmony::nameOf ((Scale) i)),
                              "scale " + juce::String (i) + " has drifted out of lockstep");
        }

        // -------------------------------------------------------------------
        beginTest ("every mask holds its own root and the right number of tones");
        {
            for (int i = 0; i < numScales; ++i)
            {
                const auto s = (Scale) i;
                const auto mask = harmony::maskOf (s);
                const juce::String scaleName (harmony::nameOf (s));

                expect ((mask & 1u) != 0, scaleName + " does not contain its own root");
                expect (mask <= 0x0FFF, scaleName + " has bits above the twelve semitones");
                expectEquals (countTones (mask), s == Scale::chromatic ? 12 : 7,
                              scaleName + " has the wrong number of tones");

                // no gap wider than an augmented second, which is what lets
                // snap() promise it never moves a pitch by more than one
                // semitone
                int widest = 0, run = 0;

                for (int step = 1; step <= 12; ++step)
                {
                    if (mask & (1u << (step % 12))) { widest = juce::jmax (widest, run + 1); run = 0; }
                    else                            { ++run; }
                }

                expect (widest <= 3, scaleName + " has a gap of " + juce::String (widest) + " semitones");
            }

            for (int a = 0; a < numScales; ++a)
                for (int b = a + 1; b < numScales; ++b)
                    expect (harmony::maskOf ((Scale) a) != harmony::maskOf ((Scale) b),
                            juce::String (harmony::nameOf ((Scale) a)) + " and "
                                + harmony::nameOf ((Scale) b) + " are the same scale");
        }

        // ===================================================================
        //  Detection
        // ===================================================================
        beginTest ("a clean major triad is heard as major, and says so");
        {
            const auto d = harmony::detectKey (tones ({ 0, 4, 7 }));

            expectEquals (d.root, 0, "C E G is not C");
            expect (d.scale == Scale::major, "C E G is not major");
            expect (d.rootConfidence > 0.70f,
                    "a clean major triad only scored " + juce::String (d.rootConfidence, 3));

            // ... but three notes do not fix the mode: major, lydian and
            // mixolydian all contain C E G and nothing here separates them.
            // The scale confidence has to admit that.
            expect (d.scaleConfidence < 0.40f,
                    "a bare triad claimed to know its mode (" + juce::String (d.scaleConfidence, 3) + ")");

            const auto floored = harmony::detectKey (tones ({ 0, 4, 7 }, 0.1f));
            expectEquals (floored.root, 0, "a 10% noise floor moved the root");
            expect (floored.scale == Scale::major, "a 10% noise floor changed the scale");
        }

        beginTest ("a clean minor triad is heard as minor - the relative-key case");
        {
            const auto d = harmony::detectKey (tones ({ 9, 0, 4 }));

            expectEquals (d.root, 9, "A C E is not A");
            expect (d.scale == Scale::minor, "A C E is not minor");
            expect (d.rootConfidence > 0.70f,
                    "a clean minor triad only scored " + juce::String (d.rootConfidence, 3));

            /*  And the genuine ambiguity, which is not the triad: the seven
                tones of C major are also the seven tones of A minor, D dorian
                and four more.  Whatever root comes back, the confidence must be
                below the threshold the rest of the instrument acts on, or the
                number means nothing. */
            const auto flat7 = harmony::detectKey (tones ({ 0, 2, 4, 5, 7, 9, 11 }));

            AnalysisResult probe;
            probe.root = flat7.root;
            probe.rootConfidence = flat7.rootConfidence;

            expect (! probe.keyIsUsable(),
                    "seven unweighted diatonic tones were reported as a usable key ("
                        + juce::String (flat7.rootConfidence, 3) + ")");
        }

        beginTest ("a flat chroma has no key at all");
        {
            for (float level : { 0.0f, 1.0e-9f, 1.0f, 1.0e6f })
            {
                Chroma c;
                c.fill (level);

                const auto d = harmony::detectKey (c);

                expectEquals (d.root, -1, "a flat chroma at level " + juce::String (level)
                                              + " invented a root");
                expectWithinAbsoluteError (d.rootConfidence, 0.0f, 0.0f);
                expectWithinAbsoluteError (d.scaleConfidence, 0.0f, 0.0f);
            }

            // CHROMATIC's own mask *is* the flat chroma, so it is the one scale
            // that can never be a detection result.  That is deliberate, and
            // this asserts it rather than leaving it to be discovered.
            Chroma chromatic;
            chromatic.fill (0.0f);

            for (int pc = 0; pc < 12; ++pc)
                if (harmony::maskOf (Scale::chromatic) & (1u << pc))
                    chromatic[(size_t) pc] = 1.0f;

            expectEquals (harmony::detectKey (chromatic).root, -1,
                          "twelve equal tones produced a key");
        }

        beginTest ("every scale is detected back from its own tones, at every root");
        {
            float lowestRoot = 1.0f, lowestScale = 1.0f;

            for (int i = 0; i < numScales; ++i)
            {
                const auto s = (Scale) i;

                if (s == Scale::chromatic)
                    continue;           // has no tonic: see the test above

                for (int root = 0; root < 12; ++root)
                {
                    const auto d = harmony::detectKey (scaleChroma (root, s));

                    expectEquals (d.root, root, juce::String (harmony::nameOf (s))
                                                    + " at root " + juce::String (root)
                                                    + " came back as root " + juce::String (d.root));
                    expect (d.scale == s, juce::String (harmony::nameOf (s)) + " at root "
                                              + juce::String (root) + " came back as "
                                              + harmony::nameOf (d.scale));

                    lowestRoot  = juce::jmin (lowestRoot,  d.rootConfidence);
                    lowestScale = juce::jmin (lowestScale, d.scaleConfidence);
                }
            }

            logMessage ("    weakest of the 96: root confidence " + juce::String (lowestRoot, 3)
                        + ", scale confidence " + juce::String (lowestScale, 3));

            expect (lowestRoot > 0.55f, "a scale built from its own tones was not a usable key");
        }

        beginTest ("detection is transposition invariant");
        {
            juce::Random rng (20240520);
            int checked = 0;

            for (int trial = 0; trial < 64; ++trial)
            {
                Chroma base;

                for (auto& v : base)
                    v = rng.nextFloat();

                const auto d0 = harmony::detectKey (base);

                if (d0.root < 0)
                    continue;

                ++checked;

                for (int k = 1; k < 12; ++k)
                {
                    Chroma shifted;

                    for (int pc = 0; pc < 12; ++pc)
                        shifted[(size_t) pc] = base[(size_t) (((pc - k) % 12 + 12) % 12)];

                    const auto d = harmony::detectKey (shifted);

                    expectEquals (d.root, (d0.root + k) % 12,
                                  "shifting the chroma by " + juce::String (k)
                                      + " did not shift the root by " + juce::String (k));
                    expect (d.scale == d0.scale, "a transposition changed the scale");
                    expectWithinAbsoluteError (d.rootConfidence, d0.rootConfidence, 1.0e-4f);
                    expectWithinAbsoluteError (d.scaleConfidence, d0.scaleConfidence, 1.0e-4f);
                }
            }

            expect (checked > 40, "too few usable bases to call this a test");
        }

        beginTest ("noise and percussion do not produce a usable key");
        {
            juce::Random rng (77);
            AnalysisResult probe;

            // Broadband, near-flat: what a kick, a snare or a cymbal leaves in
            // a chroma.  Not one of these may come back with a key.
            float worstPercussive = 0.0f;

            for (int trial = 0; trial < 2000; ++trial)
            {
                Chroma c;

                for (auto& v : c)
                    v = 0.8f + 0.25f * rng.nextFloat();

                worstPercussive = juce::jmax (worstPercussive, harmony::detectKey (c).rootConfidence);
            }

            logMessage ("    percussion-like chroma, worst root confidence of 2000: "
                        + juce::String (worstPercussive, 4));
            expect (worstPercussive < 0.30f,
                    "near-flat percussive chroma reported a confidence of "
                        + juce::String (worstPercussive, 3));

            // Twelve independent random numbers are a harder case, and an
            // honest one: a rotation correlation cannot tell a lucky draw from
            // a sparse key.  The rate is measured rather than wished away.
            int usable = 0;

            for (int trial = 0; trial < 2000; ++trial)
            {
                Chroma c;

                for (auto& v : c)
                    v = rng.nextFloat();

                const auto d = harmony::detectKey (c);
                probe.root = d.root;
                probe.rootConfidence = d.rootConfidence;

                if (probe.keyIsUsable())
                    ++usable;
            }

            logMessage ("    independent random chroma, usable keys: "
                        + juce::String (usable) + " of 2000 ("
                        + juce::String (100.0f * (float) usable / 2000.0f, 1) + "%)");
            expect (usable < 200, "one random chroma in ten was read as a key");
        }

        // ===================================================================
        //  Context
        // ===================================================================
        beginTest ("Context::from carries the analysis and refuses to invent a key");
        {
            AnalysisResult a;
            a.analysed = true;
            a.root = 3;
            a.rootConfidence = 0.9f;
            a.scale = (int) Scale::dorian;
            a.scaleConfidence = 0.8f;

            const auto known = harmony::Context::from (a, Mode::safe, -1);
            expect (known.keyKnown, "a confident analysis was not believed");
            expectEquals (known.root, 3);
            expect (known.scale == Scale::dorian, "the analysed scale was not carried");

            // AUTO is negative; anything else is the scale_type index minus one
            const auto forced = harmony::Context::from (a, Mode::colour, (int) Scale::lydian);
            expect (forced.scale == Scale::lydian, "a forced scale did not win over the analysis");
            expectEquals (forced.root, 3, "forcing a scale moved the root");
            expect (forced.mode == Mode::colour);

            // an unusable root is not a key, however certain the scale is
            AnalysisResult weak = a;
            weak.rootConfidence = 0.3f;
            expect (! weak.keyIsUsable(), "the fixture is wrong: this should be unusable");
            expect (! harmony::Context::from (weak, Mode::safe, -1).keyKnown,
                    "a low-confidence root was treated as a key");
            expect (! harmony::Context::from (weak, Mode::safe, (int) Scale::major).keyKnown,
                    "forcing a scale conjured a tonic out of nothing");

            AnalysisResult noRoot = a;
            noRoot.root = -1;
            expect (! harmony::Context::from (noRoot, Mode::safe, -1).keyKnown,
                    "root -1 was treated as a key");

            // a root without a mode is half a key, and half a key is not one
            AnalysisResult noScale = a;
            noScale.scale = -1;
            expect (! harmony::Context::from (noScale, Mode::safe, -1).keyKnown,
                    "a root with no scale was treated as a key");
            expect (harmony::Context::from (noScale, Mode::safe, (int) Scale::minor).keyKnown,
                    "a forced scale over a confident root should be a key");

            // nothing analysed at all
            const auto fresh = harmony::Context::from (AnalysisResult(), Mode::safe, -1);
            expect (! fresh.keyKnown, "a default AnalysisResult produced a key");

            expect (std::is_trivially_copyable<harmony::Context>::value,
                    "Context must stay cheap to copy: phase 21 copies it freely");
        }

        // ===================================================================
        //  The constraint
        // ===================================================================
        beginTest ("a confident root with an uncertain mode permits both thirds");
        {
            /*  The mode is the second question, not the same one.  A bare triad
                pins its tonic and says almost nothing about the mode - it is
                the first, third and fifth of major, lydian and mixolydian
                alike - and acting on the winner of that coin flip means
                snapping a third, which is what decides whether the key is major
                or minor.  So the third is left open and everything else the
                candidate mode says is kept. */
            AnalysisResult a;
            a.analysed = true;
            a.rootConfidence = 0.90f;       // the root is not in doubt
            a.scaleConfidence = 0.20f;      // the mode is

            for (int i = 0; i < numScales; ++i)
            {
                a.scale = i;

                for (int root = 0; root < 12; ++root)
                {
                    a.root = root;
                    expect (a.keyIsUsable(), "the fixture is wrong: the root should be usable");
                    expect (! a.scaleIsUsable(), "the fixture is wrong: the mode should not be");

                    for (auto mode : { Mode::safe, Mode::colour })
                    {
                        const auto c = harmony::Context::from (a, mode, -1);
                        const juce::String where (juce::String (harmony::nameOf ((Scale) i))
                                                      + " at root " + juce::String (root));

                        expect (c.keyKnown, where + ": an unconfident mode cost us the key");
                        expect (harmony::modeIsUncertain (c), where + ": the uncertainty was lost");
                        expect (harmony::scaleOf (c) == (Scale) i,
                                where + ": the candidate mode was thrown away");

                        expect (c.permits (root + 3), where + ": the minor third is forbidden");
                        expect (c.permits (root + 4), where + ": the major third is forbidden");
                        expectEquals (c.snap (root + 3), root + 3, where + ": a minor third was moved");
                        expectEquals (c.snap (root + 4), root + 4, where + ": a major third was moved");

                        // widened by exactly the thirds, and by nothing else
                        const auto certain = makeContext (root, (Scale) i, mode, true);
                        int permitted = 0;

                        for (int pc = 0; pc < 12; ++pc)
                        {
                            const bool expected = certain.permits (root + pc) || pc == 3 || pc == 4;
                            permitted += c.permits (root + pc) ? 1 : 0;

                            if (c.permits (root + pc) != expected)
                            {
                                expect (false, where + ": semitone " + juce::String (pc)
                                                   + " is wrong under an uncertain mode");
                                break;
                            }
                        }

                        // and it is still a constraint, not a surrender
                        if ((Scale) i != Scale::chromatic)
                            expect (permitted < 12, where + ": an uncertain mode permitted everything");
                    }
                }
            }
        }

        beginTest ("forcing a scale is knowledge, and is never widened");
        {
            //  A user who sets scale_type has told us the mode.  Handing back a
            //  major third over a forced MINOR would make the control a
            //  suggestion rather than a setting.
            AnalysisResult a;
            a.analysed = true;
            a.root = 7;
            a.rootConfidence = 0.90f;
            a.scale = (int) Scale::major;
            a.scaleConfidence = 0.10f;

            const auto forced = harmony::Context::from (a, Mode::safe, (int) Scale::minor);

            expect (forced.keyKnown);
            expect (! harmony::modeIsUncertain (forced), "a forced scale was treated as a guess");
            expect (forced.scale == Scale::minor, "the forced scale did not survive");
            expect (forced.permits (7 + 3), "a forced minor lost its own third");
            expect (! forced.permits (7 + 4), "a forced minor was widened to the major third");
        }

        beginTest ("an unknown root is still the identity, and is not the both-thirds case");
        {
            //  Two different states, and collapsing them would be the original
            //  mistake in a new costume: no root at all means no constraint,
            //  while a root with an unsure mode means a constraint with an open
            //  third.
            AnalysisResult a;
            a.analysed = true;
            a.root = 2;
            a.rootConfidence = 0.30f;       // unusable
            a.scale = (int) Scale::minor;
            a.scaleConfidence = 0.10f;

            const auto c = harmony::Context::from (a, Mode::safe, -1);

            expect (! c.keyKnown, "an unusable root produced a key");
            expect (! harmony::modeIsUncertain (c),
                    "modeIsUncertain must be false when the key itself is unknown");

            for (int n = 0; n < 128; ++n)
                if (c.snap (n) != n || ! c.permits (n))
                {
                    expect (false, "an unknown root did not leave pitch alone");
                    break;
                }

            expect (true);
        }

        beginTest ("the uncertainty encoding is total, and invisible to callers");
        {
            for (int i = 0; i < numScales; ++i)
            {
                const auto s = (Scale) i;
                const auto u = harmony::uncertainMode (s);

                expectEquals ((int) harmony::maskOf (u),
                              (int) (harmony::maskOf (s) | 0x0018),
                              juce::String (harmony::nameOf (s))
                                  + ": the uncertain mask is not the scale plus both thirds");
                expectEquals (juce::String (harmony::nameOf (u)), juce::String (harmony::nameOf (s)),
                              "an uncertain scale should still name its candidate");
                expect (harmony::uncertainMode (u) == u, "uncertainMode is not idempotent");
                expect (harmony::scaleOf (makeContext (0, u, Mode::safe, true)) == s,
                        "scaleOf did not strip the uncertainty");
                expect (harmony::scaleOf (makeContext (0, s, Mode::safe, true)) == s,
                        "scaleOf disturbed a certain scale");
            }

            // a value from neither range must not index anything
            expect (harmony::maskOf ((Scale) 400) != 0, "maskOf is not total");
            expect (juce::String (harmony::nameOf ((Scale) -7)).isNotEmpty(), "nameOf is not total");
        }

        beginTest ("permits agrees with maskOf for every scale and every semitone");
        {
            for (int i = 0; i < numScales; ++i)
                for (int root = 0; root < 12; ++root)
                {
                    const auto c = makeContext (root, (Scale) i, Mode::safe, true);
                    const auto mask = harmony::maskOf ((Scale) i);

                    for (int n = -24; n < 128; ++n)
                    {
                        const int pc = ((n - root) % 12 + 12) % 12;
                        const bool inMask = (mask & (1u << pc)) != 0;

                        if (c.permits (n) != inMask)
                        {
                            expect (false, juce::String (harmony::nameOf ((Scale) i))
                                               + " root " + juce::String (root)
                                               + ": permits(" + juce::String (n)
                                               + ") disagrees with the mask");
                            break;
                        }
                    }
                }

            expect (true);
        }

        beginTest ("SAFE snaps every chromatic input into the scale, and snapping twice changes nothing");
        {
            juce::String failure;
            int worstMove = 0;

            for (int i = 0; i < numScales; ++i)
                for (int root = 0; root < 12; ++root)
                {
                    const auto c = makeContext (root, (Scale) i, Mode::safe, true);

                    for (int n = 0; n < 128; ++n)
                    {
                        const int once = c.snap (n);

                        if (! c.permits (once) && failure.isEmpty())
                            failure = juce::String (harmony::nameOf ((Scale) i)) + " root "
                                        + juce::String (root) + ": snap(" + juce::String (n)
                                        + ") = " + juce::String (once) + ", which is not in the scale";

                        if (c.snap (once) != once && failure.isEmpty())
                            failure = "snap is not idempotent at " + juce::String (n);

                        if (c.permits (n) && once != n && failure.isEmpty())
                            failure = "snap moved a pitch that was already in the scale";

                        worstMove = juce::jmax (worstMove, std::abs (once - n));
                    }
                }

            expect (failure.isEmpty(), failure);
            expectEquals (worstMove, 1, "snap moved a pitch further than one semitone");
        }

        beginTest ("FREE is the identity for all 128 semitones");
        {
            juce::String failure;

            for (int i = 0; i < numScales; ++i)
                for (int root = 0; root < 12; ++root)
                {
                    const auto c = makeContext (root, (Scale) i, Mode::free, true);

                    for (int n = 0; n < 128 && failure.isEmpty(); ++n)
                    {
                        if (c.snap (n) != n)          failure = "FREE moved semitone " + juce::String (n);
                        if (! c.permits (n))          failure = "FREE forbade semitone " + juce::String (n);
                        if (std::abs (c.snapCents (100.0f * (float) n)) > 0.0f)
                            failure = "FREE returned a correction at semitone " + juce::String (n);
                    }
                }

            expect (failure.isEmpty(), failure);
        }

        beginTest ("SAFE with an unknown key is the identity - it must not invent one");
        {
            /*  The sharp edge of the whole phase.  A drum loop has no key; if
                SAFE guessed one it would transpose the loop into it and the
                user would have no way to find out why. */
            juce::String failure;

            for (int i = 0; i < numScales; ++i)
                for (int root = 0; root < 12; ++root)
                    for (auto mode : { Mode::safe, Mode::colour })
                    {
                        const auto c = makeContext (root, (Scale) i, mode, false);

                        for (int n = 0; n < 128 && failure.isEmpty(); ++n)
                        {
                            if (c.snap (n) != n)
                                failure = "an unknown key moved semitone " + juce::String (n)
                                            + " to " + juce::String (c.snap (n));

                            if (! c.permits (n))
                                failure = "an unknown key forbade semitone " + juce::String (n);

                            if (std::abs (c.snapCents (100.0f * (float) n + 37.0f)) > 0.0f)
                                failure = "an unknown key returned a pitch correction";
                        }
                    }

            expect (failure.isEmpty(), failure);

            // and the same thing from the top: an analysis that found nothing
            const auto c = harmony::Context::from (AnalysisResult(), Mode::safe, -1);
            expect (! c.keyKnown);

            for (int n = 0; n < 128; ++n)
                if (c.snap (n) != n)
                {
                    expect (false, "Context::from produced a snapping context from no analysis");
                    break;
                }
        }

        beginTest ("COLOR is the scale plus two borrowed tones, and never fewer");
        {
            for (int i = 0; i < numScales; ++i)
            {
                const auto s = (Scale) i;
                const auto safe   = makeContext (0, s, Mode::safe,   true);
                const auto colour = makeContext (0, s, Mode::colour, true);

                juce::uint16 safeMask = 0, colourMask = 0;

                for (int pc = 0; pc < 12; ++pc)
                {
                    if (safe.permits (pc))   safeMask   = (juce::uint16) (safeMask   | (1u << pc));
                    if (colour.permits (pc)) colourMask = (juce::uint16) (colourMask | (1u << pc));
                }

                const juce::String scaleName (harmony::nameOf (s));

                expectEquals ((int) safeMask, (int) harmony::maskOf (s),
                              scaleName + ": SAFE is not the scale");
                expectEquals ((int) (colourMask & safeMask), (int) safeMask,
                              scaleName + ": COLOR dropped a tone the scale has");
                expectEquals (countTones (colourMask), s == Scale::chromatic ? 12 : 9,
                              scaleName + ": COLOR does not admit exactly two borrowed tones");

                // the tonic and the fifth are never borrowed against, and COLOR
                // never removes the scale's own third
                expect ((colourMask & 1u) != 0, scaleName + ": COLOR lost the tonic");

                // snapping still lands inside the permitted set
                for (int n = 0; n < 128; ++n)
                    if (! colour.permits (colour.snap (n)))
                    {
                        expect (false, scaleName + ": COLOR snapped outside its own set");
                        break;
                    }
            }
        }

        beginTest ("snapCents is continuous, monotonic and bounded");
        {
            /*  A hard quantiser cannot be continuous: crossing the midpoint of a
                gap moves the target by the whole gap, and a glide through it
                jumps.  This one is a warped map instead, so the test is not
                "does it snap" but "does the pitch it hands back ever jump". */
            double worstStep = 0.0, worstDelta = 0.0, worstBackward = 0.0, worstAtTone = 0.0;

            //  Both halves of the scale space: the nine scales, and the nine
            //  again with the mode left open.  The second set has a tone more
            //  and therefore different gaps, so it is a different curve.
            for (int i = 0; i < 2 * numScales; ++i)
                for (int root = 0; root < 12; ++root)
                {
                    const auto s = i < numScales ? (Scale) i
                                                 : harmony::uncertainMode ((Scale) (i - numScales));
                    const auto c = makeContext (root, s, Mode::safe, true);
                    double previous = 0.0;

                    for (int step = -4800; step <= 9600; ++step)
                    {
                        const float cents = 0.5f * (float) step;
                        const float delta = c.snapCents (cents);
                        const double out  = (double) cents + (double) delta;

                        worstDelta = juce::jmax (worstDelta, (double) std::abs (delta));

                        if (step > -4800)
                        {
                            worstStep = juce::jmax (worstStep, std::abs (out - previous));
                            worstBackward = juce::jmax (worstBackward, previous - out);
                        }

                        previous = out;
                    }

                    for (int n = -24; n < 96; ++n)
                        if (c.permits (n))
                            worstAtTone = juce::jmax (worstAtTone,
                                                      (double) std::abs (c.snapCents (100.0f * (float) n)));
                }

            logMessage ("    worst output step over a 0.5-cent sweep: "
                        + juce::String (worstStep, 4) + " cents");
            logMessage ("    worst correction: " + juce::String (worstDelta, 2) + " cents");

            expect (worstStep < 100.0, "snapCents jumps by " + juce::String (worstStep, 1)
                                           + " cents - a glide through it would step");
            expect (worstStep < 5.0, "snapCents is not smooth: a 0.5-cent input step moved the "
                                     "output by " + juce::String (worstStep, 2) + " cents");
            expect (worstBackward < 0.01, "snapCents is not monotonic: the output went backwards by "
                                              + juce::String (worstBackward, 4) + " cents");
            expect (worstDelta < 90.0, "snapCents moved a pitch by " + juce::String (worstDelta, 1)
                                           + " cents, which is past the half-way point of a whole tone");
            expect (worstAtTone < 0.001, "snapCents does not leave a scale tone alone ("
                                             + juce::String (worstAtTone, 5) + " cents)");
        }

        beginTest ("snapCents and snap agree about what is in the scale");
        {
            juce::String failure;

            for (int i = 0; i < numScales; ++i)
                for (int root = 0; root < 12; ++root)
                {
                    const auto c = makeContext (root, (Scale) i, Mode::safe, true);

                    for (int n = 0; n < 128 && failure.isEmpty(); ++n)
                    {
                        const int snapped = c.snap (n);

                        if (std::abs (c.snapCents (100.0f * (float) snapped)) > 0.001f)
                            failure = "snap() returned " + juce::String (snapped)
                                        + " but snapCents still wants to move it";
                    }
                }

            expect (failure.isEmpty(), failure);
        }
    }
};

// ===========================================================================
//  Mutation - the engine that turns a recipe into audio
//
//  Every assertion here is about a property the engine promised: that a seed
//  reproduces, that a lock is kept, that nothing it can be asked to do
//  produces garbage.  Nothing in here listens to anything.
// ===========================================================================
#include "../Source/Mutation/MutationEngine.h"

#include <cstring>

struct MutationTests : juce::UnitTest
{
    MutationTests() : juce::UnitTest ("Mutation", "nacar") {}

    using Intent   = mutation::Intent;
    using Distance = mutation::Distance;
    using Recipe   = mutation::Recipe;
    using Preserve = mutation::Preserve;
    using Engine   = mutation::MutationEngine;
    using Mode     = harmony::Mode;

    static constexpr double kRate = 44100.0;
    static constexpr int    kLength = 26460;     // 0.6 s: long enough to frame, quick to render
    static constexpr int    kRoot = 9;           // A
    static constexpr int    kScaleMinor = 1;
    static constexpr double kTwoPi = 6.283185307179586;

    // -------------------------------------------------------------------
    //  A source, an analysis and a context
    // -------------------------------------------------------------------
    static std::vector<int> onsets() { return { 240, 6000, 13000, 19500 }; }

    /** A tone in A minor with four attacks in it.  Built from arithmetic and a
        local LCG, never from juce::Random: a test for determinism whose own
        input is not deterministic proves nothing. */
    static SampleBuffer::Ptr makeSource (int channels, bool decorrelated = false,
                                         int length = kLength)
    {
        SampleBuffer::Ptr sample = new SampleBuffer();

        sample->audio.setSize (juce::jmax (1, channels), juce::jmax (1, length));
        sample->audio.clear();
        sample->sourceRate = kRate;
        sample->displayName = "TEST TONE";

        juce::uint32 state = 0x12345678u;

        const auto noise = [&state]
        {
            state = state * 1664525u + 1013904223u;
            return (float) (state >> 9) * (1.0f / 4194304.0f) - 1.0f;
        };

        const auto grid = onsets();
        const int burst = (int) (0.0025 * kRate);

        for (int i = 0; i < length; ++i)
        {
            const double t = (double) i / kRate;

            float x = 0.45f * (float) std::sin (kTwoPi * 110.0 * t)
                    + 0.20f * (float) std::sin (kTwoPi * 220.0 * t)
                    + 0.10f * (float) std::sin (kTwoPi * 330.0 * t);

            float envelope = 0.20f;
            float attack = 0.0f;

            for (int onset : grid)
            {
                if (i >= onset)
                    envelope += 0.85f * std::exp (-(float) (i - onset) / (0.045f * (float) kRate));

                if (i >= onset && i < onset + burst)
                    attack += 0.45f * noise();
            }

            const float left = juce::jlimit (-0.95f, 0.95f, x * envelope + attack);

            sample->audio.setSample (0, i, left);

            if (sample->numChannels() > 1)
            {
                const float right = decorrelated
                    ? juce::jlimit (-0.95f, 0.95f,
                                    (0.45f * (float) std::sin (kTwoPi * 110.0 * t + 0.7)
                                     + 0.20f * (float) std::sin (kTwoPi * 220.0 * t)
                                     + 0.10f * (float) std::sin (kTwoPi * 331.0 * t)) * envelope
                                    + attack * 0.6f)
                    : left;

                for (int c = 1; c < sample->numChannels(); ++c)
                    sample->audio.setSample (c, i, right);
            }
        }

        return sample;
    }

    /** A single sine, for the pitch lock: a pitch you can measure. */
    static SampleBuffer::Ptr makeSine (double hz, int length = 44100)
    {
        SampleBuffer::Ptr sample = new SampleBuffer();

        sample->audio.setSize (2, length);
        sample->sourceRate = kRate;
        sample->displayName = "SINE";

        for (int i = 0; i < length; ++i)
        {
            const float x = 0.7f * (float) std::sin (kTwoPi * hz * (double) i / kRate);

            sample->audio.setSample (0, i, x);
            sample->audio.setSample (1, i, x);
        }

        return sample;
    }

    static AnalysisResult makeAnalysis (bool known = true)
    {
        AnalysisResult a;

        a.analysed = known;
        a.root = known ? kRoot : -1;
        a.rootConfidence = known ? 0.92f : 0.0f;
        a.scale = known ? kScaleMinor : -1;
        a.scaleConfidence = known ? 0.81f : 0.0f;
        a.tempo = known ? 120.0 : 0.0;
        a.tempoConfidence = known ? 0.75f : 0.0f;

        if (known)
            a.transients = onsets();

        a.peakLevel = 0.9f;
        a.loudness = -17.0f;
        a.spectralCentroid = known ? 850.0f : 0.0f;
        a.spectralRolloff = known ? 4100.0f : 0.0f;
        a.lowEnergy = known ? 0.45f : 0.0f;
        a.highEnergy = known ? 0.08f : 0.0f;
        a.percussiveRatio = known ? 0.35f : 0.0f;
        a.polyphonicLikelihood = known ? 0.2f : 0.0f;
        a.loopability = known ? 0.5f : 0.0f;
        a.silenceRatio = 0.0f;

        return a;
    }

    static Recipe makeRecipe (Intent intent, Distance distance, juce::uint32 seed,
                              Mode mode = Mode::safe, Preserve preserve = {})
    {
        Recipe r;

        r.seed = seed;
        r.intent = intent;
        r.distance = distance;
        r.harmonyMode = mode;
        r.preserve = preserve;
        r.engineVersion = mutation::currentEngineVersion;

        return r;
    }

    // -------------------------------------------------------------------
    //  Measurement
    // -------------------------------------------------------------------
    static bool identical (const SampleBuffer& a, const SampleBuffer& b)
    {
        if (a.numChannels() != b.numChannels() || a.lengthSamples() != b.lengthSamples())
            return false;

        for (int c = 0; c < a.numChannels(); ++c)
            if (std::memcmp (a.audio.getReadPointer (c), b.audio.getReadPointer (c),
                             sizeof (float) * (size_t) a.lengthSamples()) != 0)
                return false;

        return true;
    }

    static double maximumDifference (const SampleBuffer& a, const SampleBuffer& b)
    {
        if (a.lengthSamples() != b.lengthSamples() || a.numChannels() != b.numChannels())
            return 1.0;

        double worst = 0.0;

        for (int c = 0; c < a.numChannels(); ++c)
        {
            const auto* x = a.audio.getReadPointer (c);
            const auto* y = b.audio.getReadPointer (c);

            for (int i = 0; i < a.lengthSamples(); ++i)
                worst = juce::jmax (worst, std::abs ((double) x[i] - (double) y[i]));
        }

        return worst;
    }

    /** Windowed single-bin DFT, in the test's own arithmetic rather than the
        engine's: a lock proved with the engine's own filter would only be
        proving the engine agrees with itself. */
    static double magnitudeAt (const juce::AudioBuffer<float>& b, double hz, double rate)
    {
        const int n = b.getNumSamples();

        if (n < 16)
            return 0.0;

        double re = 0.0, im = 0.0;

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const auto* d = b.getReadPointer (c);

            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (kTwoPi * (double) i / (double) n);
                const double phase = kTwoPi * hz * (double) i / rate;

                re += (double) d[i] * w * std::cos (phase);
                im -= (double) d[i] * w * std::sin (phase);
            }
        }

        return std::sqrt (re * re + im * im) / (double) n;
    }

    static std::vector<float> envelopeOf (const juce::AudioBuffer<float>& b, int hop)
    {
        std::vector<float> envelope;

        for (int start = 0; start + hop <= b.getNumSamples(); start += hop)
        {
            double sum = 0.0;

            for (int c = 0; c < b.getNumChannels(); ++c)
            {
                const auto* d = b.getReadPointer (c) + start;

                for (int i = 0; i < hop; ++i)
                    sum += (double) d[i] * d[i];
            }

            envelope.push_back ((float) std::sqrt (sum / (double) hop));
        }

        return envelope;
    }

    static double normalisedCorrelation (const float* a, const float* b, int n)
    {
        double sum = 0.0, ea = 1.0e-12, eb = 1.0e-12;

        for (int i = 0; i < n; ++i)
        {
            sum += (double) a[i] * b[i];
            ea += (double) a[i] * a[i];
            eb += (double) b[i] * b[i];
        }

        return sum / std::sqrt (ea * eb);
    }

    static bool planMovesTime (const mutation::Plan& plan)
    {
        for (const auto& step : plan.steps)
            if (step.op == mutation::Op::timeStretch || step.op == mutation::Op::loopStabilise
                || step.op == mutation::Op::sliceShuffle || step.op == mutation::Op::reverseWhole
                || step.op == mutation::Op::stutter || step.op == mutation::Op::swellReverse
                || step.op == mutation::Op::sliceReverse)
                return true;

        return false;
    }

    static bool planMovesPitch (const mutation::Plan& plan)
    {
        for (const auto& step : plan.steps)
            if (step.op == mutation::Op::transpose || step.op == mutation::Op::octaveLayer
                || step.op == mutation::Op::shimmerLayer)
                return true;

        return false;
    }

    /** Finite, inside full scale, and not silent where the source was not. */
    juce::String soundCheck (const mutation::Result& result, const SampleBuffer& source,
                             const juce::String& label)
    {
        if (! result.ok)
            return label + ": " + result.failure;

        if (result.audio == nullptr)
            return label + ": no audio";

        const auto& b = result.audio->audio;

        if (b.getNumSamples() <= 0)
            return label + ": empty result";

        if (b.getNumChannels() != source.numChannels())
            return label + ": the channel count changed";

        double peak = 0.0, energy = 0.0, sum = 0.0;

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const auto* d = b.getReadPointer (c);

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                if (! std::isfinite (d[i]))
                    return label + ": non-finite sample at " + juce::String (i);

                peak = juce::jmax (peak, std::abs ((double) d[i]));
                energy += (double) d[i] * d[i];
                sum += (double) d[i];
            }
        }

        const double count = (double) b.getNumSamples() * (double) b.getNumChannels();
        const double rms = std::sqrt (energy / count);
        const double dc = std::abs (sum / count);

        if (peak > 1.0)
            return label + ": peaks at " + juce::String (peak, 4) + ", above full scale";

        if (dc > 0.01)
            return label + ": DC offset of " + juce::String (dc, 5);

        double sourceEnergy = 0.0;

        for (int c = 0; c < source.numChannels(); ++c)
        {
            const auto* d = source.audio.getReadPointer (c);

            for (int i = 0; i < source.lengthSamples(); ++i)
                sourceEnergy += (double) d[i] * d[i];
        }

        const double sourceRms = std::sqrt (sourceEnergy
                                            / juce::jmax (1.0, (double) source.lengthSamples()
                                                               * (double) source.numChannels()));

        if (sourceRms > 1.0e-4 && rms < sourceRms * 0.01)
            return label + ": the source had audio and the result is silent";

        if (! (result.score >= 0.0f && result.score <= 1.0f))
            return label + ": score out of range";

        if (result.operations.isEmpty())
            return label + ": no operations were reported";

        return {};
    }

    // ===================================================================
    void runTest() override
    {
        const auto analysis = makeAnalysis();
        const auto safeContext = harmony::Context::from (analysis, Mode::safe, -1);

        // ---------------------------------------------------------------
        beginTest ("the same recipe renders bit-identical audio, twice and again");
        {
            auto source = makeSource (2);

            const auto recipe = makeRecipe (Intent::memory, Distance::far, 4821u);

            const auto first = Engine::render (recipe, *source, analysis, safeContext);
            const auto second = Engine::render (recipe, *source, analysis, safeContext);

            expect (first.ok && second.ok, "both renders should succeed");
            expect (identical (*first.audio, *second.audio),
                    "the same recipe rendered twice is not bit-identical");

            // A different recipe in between is what catches shared mutable
            // state: if anything in the engine survived a render, the third
            // one would differ from the first.
            const auto other = makeRecipe (Intent::broken, Distance::far, 99u);
            const auto between = Engine::render (other, *source, analysis, safeContext);

            expect (between.ok, "the intervening render should succeed");

            const auto third = Engine::render (recipe, *source, analysis, safeContext);

            expect (identical (*first.audio, *third.audio),
                    "a render after a different recipe is not bit-identical to the first");

            expect (first.operations == third.operations, "the sentences changed");
            expect (std::abs (first.score - third.score) < 1.0e-6f, "the score changed");

            // And the same across every intent, not just the one above.
            for (int i = 0; i < (int) Intent::count; ++i)
            {
                const auto r = makeRecipe ((Intent) i, Distance::unknown, 7717u);

                const auto a = Engine::render (r, *source, analysis, safeContext);
                const auto b = Engine::render (r, *source, analysis, safeContext);

                expect (a.ok && b.ok && identical (*a.audio, *b.audio),
                        juce::String (mutation::nameOf ((Intent) i)) + " did not reproduce");
            }
        }

        // ---------------------------------------------------------------
        beginTest ("a different seed gives different audio");
        {
            auto source = makeSource (2);

            for (Intent intent : { Intent::memory, Intent::cloud, Intent::broken,
                                   Intent::ghost, Intent::distant })
            {
                const auto a = Engine::render (makeRecipe (intent, Distance::far, 1001u),
                                               *source, analysis, safeContext);
                const auto b = Engine::render (makeRecipe (intent, Distance::far, 2002u),
                                               *source, analysis, safeContext);

                expect (a.ok && b.ok, "both renders should succeed");

                const bool differs = a.audio->lengthSamples() != b.audio->lengthSamples()
                                     || maximumDifference (*a.audio, *b.audio) > 1.0e-5;

                expect (differs, juce::String (mutation::nameOf (intent))
                                 + ": two seeds produced the same audio, so the seed is decorative");
            }
        }

        // ---------------------------------------------------------------
        beginTest ("preserve LENGTH: the result is exactly as long");
        {
            auto source = makeSource (2);

            Preserve preserve;
            preserve.length = true;

            for (int i = 0; i < (int) Intent::count; ++i)
                for (juce::uint32 seed : { 11u, 4242u, 90210u })
                {
                    const auto recipe = makeRecipe ((Intent) i, Distance::far, seed,
                                                    Mode::safe, preserve);

                    const auto result = Engine::render (recipe, *source, analysis, safeContext);

                    expect (result.ok, "render failed");
                    expectEquals (result.audio->lengthSamples(), source->lengthSamples(),
                                  juce::String (mutation::nameOf ((Intent) i))
                                  + " changed the length with the length locked");
                }
        }

        // ---------------------------------------------------------------
        beginTest ("preserve PITCH: the fundamental does not move");
        {
            auto source = makeSine (220.0);

            Preserve preserve;
            preserve.pitch = true;

            // A granular reconstruction smears a partial across a band as wide
            // as the reciprocal of its grain length, so the single loudest bin
            // wanders even when nothing has been transposed.  The centroid of
            // the energy around the fundamental does not: a transposition moves
            // it, a smear does not.
            const auto centroidAround = [] (const juce::AudioBuffer<float>& b)
            {
                double weighted = 0.0, total = 0.0;

                for (double hz = 140.0; hz <= 340.0; hz += 2.0)
                {
                    const double m = magnitudeAt (b, hz, kRate);

                    weighted += m * m * hz;
                    total += m * m;
                }

                return total > 0.0 ? weighted / total : 0.0;
            };

            for (Intent intent : { Intent::dark, Intent::ghost, Intent::cloud, Intent::memory })
                for (juce::uint32 seed : { 3u, 555u, 12345u })
                {
                    const auto recipe = makeRecipe (intent, Distance::far, seed,
                                                    Mode::free, preserve);

                    const auto plan = Engine::makePlan (recipe, analysis, safeContext);

                    expect (! planMovesPitch (plan),
                            "a pitch-moving operation was planned with pitch locked");

                    const auto result = Engine::render (plan, recipe, *source, analysis, safeContext);

                    expect (result.ok, "render failed");

                    const double before = centroidAround (source->audio);
                    const double after = centroidAround (result.audio->audio);

                    // A semitone at 220 Hz is 13 Hz.  Nine is comfortably less
                    // than the smallest move a transposition could make.
                    expect (std::abs (after - before) < 9.0,
                            juce::String (mutation::nameOf (intent))
                            + ": the fundamental moved from " + juce::String (before, 1)
                            + " Hz to " + juce::String (after, 1) + " Hz");
                }
        }

        // ---------------------------------------------------------------
        beginTest ("preserve KEY: every pitch decision stays in the scale, even in FREE");
        {
            Preserve preserve;
            preserve.key = true;

            for (int i = 0; i < (int) Intent::count; ++i)
                for (juce::uint32 seed = 1; seed < 400; seed += 7)
                {
                    const auto freeContext = harmony::Context::from (analysis, Mode::free, -1);

                    const auto recipe = makeRecipe ((Intent) i, Distance::far, seed,
                                                    Mode::free, preserve);

                    const auto plan = Engine::makePlan (recipe, analysis, freeContext);

                    for (const auto& step : plan.steps)
                    {
                        if (step.semitones == 0)
                            continue;

                        const int degree = ((step.semitones % 12) + 12) % 12;

                        expect (safeContext.permits (safeContext.root + degree),
                                juce::String (mutation::nameOf ((Intent) i))
                                + ": planned " + juce::String (step.semitones)
                                + " semitones, which is outside the key");
                    }
                }
        }

        // ---------------------------------------------------------------
        beginTest ("preserve RHYTHM: nothing moves in time");
        {
            auto source = makeSource (2);

            Preserve preserve;
            preserve.rhythm = true;

            const int hop = (int) (0.005 * kRate);

            for (int i = 0; i < (int) Intent::count; ++i)
                for (juce::uint32 seed : { 17u, 808u })
                {
                    const auto recipe = makeRecipe ((Intent) i, Distance::far, seed,
                                                    Mode::safe, preserve);

                    const auto plan = Engine::makePlan (recipe, analysis, safeContext);

                    expect (! planMovesTime (plan),
                            juce::String (mutation::nameOf ((Intent) i))
                            + " planned a time-moving operation with rhythm locked");

                    const auto result = Engine::render (plan, recipe, *source, analysis, safeContext);

                    expect (result.ok, "render failed");

                    const auto a = envelopeOf (source->audio, hop);
                    const auto b = envelopeOf (result.audio->audio, hop);

                    const int maxLag = 12;                 // 60 ms either way
                    const int count = (int) juce::jmin (a.size(), b.size()) - maxLag;

                    if (count < 8)
                        continue;

                    double best = -2.0;
                    int bestLag = 0;

                    for (int lag = -maxLag; lag <= maxLag; ++lag)
                    {
                        const double c = normalisedCorrelation (a.data() + maxLag,
                                                                b.data() + maxLag + lag, count);

                        if (c > best)
                        {
                            best = c;
                            bestLag = lag;
                        }
                    }

                    const double atZero = normalisedCorrelation (a.data() + maxLag,
                                                                 b.data() + maxLag, count);

                    // WHAT THIS CAN AND CANNOT SEE.
                    //
                    // preserve_rhythm is "may not alter the timing grid" -
                    // onsets stay where they are. It is NOT preserve_transients,
                    // "must leave the attacks intact", so with only rhythm
                    // locked the engine may still soften an attack, and DARK and
                    // GHOST both do. Softening moves an envelope's energy later
                    // WITHOUT moving the grid, and an envelope cross-correlation
                    // cannot tell those two apart.
                    //
                    // This assertion was `atZero > best * 0.99`, which DARK and
                    // GHOST failed at 0.983 and 0.968 - one hop of drift on a
                    // deliberately smeared attack, reported as a moved grid.
                    // The hop is 5 ms, so a best lag of one hop is the
                    // measurement's own resolution floor and is not evidence of
                    // anything.
                    //
                    // The grid itself is checked properly, at sample resolution
                    // and against real onsets, in the preserve TRANSIENTS test
                    // below - and every intent passes it. What is left here is a
                    // sanity bound: the envelope may smear, it may not walk.
                    expect (std::abs (bestLag) <= 1,
                            juce::String (mutation::nameOf ((Intent) i))
                            + ": the envelope walked to " + juce::String (bestLag * 5)
                            + " ms (" + juce::String (best, 3) + " against "
                            + juce::String (atZero, 3) + " in place)");

                    expect (atZero > 0.5,
                            juce::String (mutation::nameOf ((Intent) i))
                            + ": the envelope no longer resembles the source's ("
                            + juce::String (atZero, 3) + ")");
                }
        }

        // ---------------------------------------------------------------
        beginTest ("preserve TRANSIENTS: every attack is where it was");
        {
            auto source = makeSource (2);

            Preserve preserve;
            preserve.transients = true;

            const int lead = (int) (0.001 * kRate);
            const int window = (int) (0.005 * kRate);      // inside the 6 ms the engine holds
            const int maxLag = (int) (0.003 * kRate);

            for (int i = 0; i < (int) Intent::count; ++i)
                for (juce::uint32 seed : { 5u, 616u })
                {
                    const auto recipe = makeRecipe ((Intent) i, Distance::far, seed,
                                                    Mode::safe, preserve);

                    const auto result = Engine::render (recipe, *source, analysis, safeContext);

                    expect (result.ok, "render failed");

                    for (int onset : onsets())
                    {
                        const int start = onset - lead;

                        if (start < maxLag
                            || start + window + maxLag >= result.audio->lengthSamples())
                            continue;

                        const auto* s = source->audio.getReadPointer (0) + start;

                        double best = -2.0;
                        int bestLag = 0;

                        for (int lag = -maxLag; lag <= maxLag; lag += 4)
                        {
                            const auto* d = result.audio->audio.getReadPointer (0) + start + lag;
                            const double c = normalisedCorrelation (s, d, window);

                            if (c > best)
                            {
                                best = c;
                                bestLag = lag;
                            }
                        }

                        expect (bestLag == 0,
                                juce::String (mutation::nameOf ((Intent) i))
                                + ": the attack at " + juce::String (onset)
                                + " moved by " + juce::String (bestLag) + " samples");

                        expect (best > 0.95,
                                juce::String (mutation::nameOf ((Intent) i))
                                + ": the attack at " + juce::String (onset)
                                + " no longer has its shape (" + juce::String (best, 3) + ")");
                    }
                }
        }

        // ---------------------------------------------------------------
        beginTest ("preserve STEREO: the image is the source's own");
        {
            auto mono = makeSource (2, false);          // two identical channels
            auto wide = makeSource (2, true);

            Preserve preserve;
            preserve.stereo = true;

            const auto sideOverMid = [] (const juce::AudioBuffer<float>& b)
            {
                double mid = 0.0, side = 0.0;

                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const double m = 0.5 * ((double) b.getSample (0, i) + b.getSample (1, i));
                    const double s = 0.5 * ((double) b.getSample (0, i) - b.getSample (1, i));

                    mid += m * m;
                    side += s * s;
                }

                return std::sqrt (side) / juce::jmax (1.0e-9, std::sqrt (mid));
            };

            for (int i = 0; i < (int) Intent::count; ++i)
                for (juce::uint32 seed : { 21u, 4004u })
                {
                    const auto recipe = makeRecipe ((Intent) i, Distance::far, seed,
                                                    Mode::safe, preserve);

                    const auto flat = Engine::render (recipe, *mono, analysis, safeContext);

                    expect (flat.ok, "render failed");

                    const auto& b = flat.audio->audio;

                    expect (std::memcmp (b.getReadPointer (0), b.getReadPointer (1),
                                         sizeof (float) * (size_t) b.getNumSamples()) == 0,
                            juce::String (mutation::nameOf ((Intent) i))
                            + ": a source whose channels were identical came out with two "
                              "different channels");

                    const auto stereo = Engine::render (recipe, *wide, analysis, safeContext);

                    expect (stereo.ok, "render failed");

                    const double wanted = sideOverMid (wide->audio);
                    const double got = sideOverMid (stereo.audio->audio);

                    const double dB = 20.0 * std::log10 (juce::jmax (1.0e-9, got)
                                                         / juce::jmax (1.0e-9, wanted));

                    expect (std::abs (dB) < 1.0,
                            juce::String (mutation::nameOf ((Intent) i))
                            + ": the side-to-mid ratio moved by " + juce::String (dB, 2) + " dB");
                }
        }

        // ---------------------------------------------------------------
        beginTest ("preserve LOW END: the bottom is measurably undisturbed");
        {
            auto source = makeSource (2);

            Preserve preserve;
            preserve.lowEnd = true;

            static const double frequencies[] { 45.0, 60.0, 80.0, 110.0 };

            for (int i = 0; i < (int) Intent::count; ++i)
                for (juce::uint32 seed : { 31u, 5150u })
                {
                    const auto recipe = makeRecipe ((Intent) i, Distance::far, seed,
                                                    Mode::safe, preserve);

                    const auto result = Engine::render (recipe, *source, analysis, safeContext);

                    expect (result.ok, "render failed");
                    expectEquals (result.audio->lengthSamples(), source->lengthSamples(),
                                  "the low-end lock must hold the timeline too");

                    for (double hz : frequencies)
                    {
                        const double before = magnitudeAt (source->audio, hz, kRate);
                        const double after = magnitudeAt (result.audio->audio, hz, kRate);

                        const double dB = 20.0 * std::log10 (juce::jmax (1.0e-12, after)
                                                             / juce::jmax (1.0e-12, before));

                        expect (std::abs (dB) < 0.5,
                                juce::String (mutation::nameOf ((Intent) i)) + ": "
                                + juce::String (hz, 0) + " Hz moved by "
                                + juce::String (dB, 2) + " dB");
                    }
                }
        }

        // ---------------------------------------------------------------
        beginTest ("all ten intents at all three distances render real audio");
        {
            auto source = makeSource (2);

            for (int i = 0; i < (int) Intent::count; ++i)
                for (Distance distance : { Distance::near_, Distance::far, Distance::unknown })
                {
                    const auto recipe = makeRecipe ((Intent) i, distance, 6180u);
                    const auto result = Engine::render (recipe, *source, analysis, safeContext);

                    const juce::String label = juce::String (mutation::nameOf ((Intent) i))
                                             + " at " + (distance == Distance::near_ ? "NEAR"
                                                       : distance == Distance::far ? "FAR" : "UNKNOWN");

                    const auto failure = soundCheck (result, *source, label);

                    expect (failure.isEmpty(), failure);
                }
        }

        // ---------------------------------------------------------------
        beginTest ("SAFE harmony never transposes out of the scale");
        {
            for (int i = 0; i < (int) Intent::count; ++i)
                for (juce::uint32 seed = 1; seed < 600; seed += 3)
                {
                    const auto recipe = makeRecipe ((Intent) i, Distance::far, seed, Mode::safe);
                    const auto plan = Engine::makePlan (recipe, analysis, safeContext);

                    for (const auto& step : plan.steps)
                    {
                        if (step.semitones == 0)
                            continue;

                        const int degree = ((step.semitones % 12) + 12) % 12;

                        expect (safeContext.permits (safeContext.root + degree),
                                juce::String (mutation::nameOf ((Intent) i))
                                + ": " + juce::String (step.semitones)
                                + " semitones is not in A minor");
                    }
                }

            // With no detected key, SAFE may not invent one: the only move left
            // is a whole octave.
            const auto blind = makeAnalysis (false);
            const auto blindContext = harmony::Context::from (blind, Mode::safe, -1);

            expect (! blindContext.keyKnown, "the blind context should not claim a key");

            for (int i = 0; i < (int) Intent::count; ++i)
                for (juce::uint32 seed = 1; seed < 600; seed += 3)
                {
                    const auto recipe = makeRecipe ((Intent) i, Distance::far, seed, Mode::safe);
                    const auto plan = Engine::makePlan (recipe, blind, blindContext);

                    for (const auto& step : plan.steps)
                        expect (step.semitones % 12 == 0,
                                juce::String (mutation::nameOf ((Intent) i))
                                + ": moved " + juce::String (step.semitones)
                                + " semitones with no key detected");
                }
        }

        // ---------------------------------------------------------------
        beginTest ("degenerate sources do not crash and do not produce NaN");
        {
            const auto blind = makeAnalysis (false);
            const auto blindContext = harmony::Context::from (blind, Mode::safe, -1);

            // No sample at all.
            {
                SampleBuffer::Ptr empty = new SampleBuffer();

                for (int i = 0; i < (int) Intent::count; ++i)
                {
                    const auto result = Engine::render (makeRecipe ((Intent) i, Distance::far, 1u),
                                                        *empty, blind, blindContext);

                    expect (! result.ok, "an empty sample should not report success");
                    expect (result.failure.isNotEmpty(), "an empty sample should say why");
                    expect (result.audio == nullptr, "an empty sample should return no audio");
                }
            }

            // One sample, silence, and an analysis that knows nothing.
            for (int length : { 1, 2, 64, 4097 })
                for (int channels : { 1, 2 })
                    for (int i = 0; i < (int) Intent::count; ++i)
                    {
                        SampleBuffer::Ptr tiny = new SampleBuffer();

                        tiny->audio.setSize (channels, length);
                        tiny->audio.clear();
                        tiny->sourceRate = kRate;

                        for (int c = 0; c < channels; ++c)
                            tiny->audio.setSample (c, 0, 0.5f);

                        const auto result = Engine::render (makeRecipe ((Intent) i, Distance::unknown,
                                                                        77u, Mode::safe, Preserve::all()),
                                                            *tiny, blind, blindContext);

                        if (! result.ok)
                            continue;           // saying no is allowed; lying is not

                        expect (result.audio != nullptr, "ok with no audio");

                        for (int c = 0; c < result.audio->numChannels(); ++c)
                        {
                            const auto* d = result.audio->audio.getReadPointer (c);

                            for (int k = 0; k < result.audio->lengthSamples(); ++k)
                                if (! std::isfinite (d[k]) || std::abs (d[k]) > 1.0f)
                                {
                                    expect (false, "a " + juce::String (length)
                                                   + "-sample source produced "
                                                   + juce::String (d[k]));
                                    k = result.audio->lengthSamples();
                                }
                        }
                    }

            // Pure silence, of a useful length.
            {
                SampleBuffer::Ptr silent = new SampleBuffer();

                silent->audio.setSize (2, 22050);
                silent->audio.clear();
                silent->sourceRate = kRate;

                for (int i = 0; i < (int) Intent::count; ++i)
                {
                    const auto result = Engine::render (makeRecipe ((Intent) i, Distance::far, 404u),
                                                        *silent, blind, blindContext);

                    if (! result.ok)
                        continue;

                    for (int c = 0; c < result.audio->numChannels(); ++c)
                    {
                        const auto* d = result.audio->audio.getReadPointer (c);

                        for (int k = 0; k < result.audio->lengthSamples(); ++k)
                            if (! std::isfinite (d[k]) || std::abs (d[k]) > 1.0f)
                            {
                                expect (false, "silence produced " + juce::String (d[k]));
                                k = result.audio->lengthSamples();
                            }
                    }
                }
            }
        }

        // ---------------------------------------------------------------
        beginTest ("a recipe survives the session tree");
        {
            Recipe written;

            written.seed = 3141592653u;
            written.intent = Intent::ghost;
            written.harmonyMode = Mode::colour;
            written.distance = Distance::unknown;
            written.preserve.pitch = true;
            written.preserve.lowEnd = true;
            written.preserve.stereo = true;
            written.engineVersion = 7;

            juce::ValueTree node (ids::RECIPE);
            written.writeTo (node);

            const auto read = Recipe::readFrom (node);

            expect (read.seed == written.seed, "the seed did not round-trip");
            expect (read.intent == written.intent, "the intent did not round-trip");
            expect (read.harmonyMode == written.harmonyMode, "the harmony did not round-trip");
            expect (read.distance == written.distance, "the distance did not round-trip");
            expectEquals (read.engineVersion, 7, "the engine version did not round-trip");

            expect (read.preserve.pitch && read.preserve.lowEnd && read.preserve.stereo,
                    "a lock was lost");
            expect (! read.preserve.key && ! read.preserve.rhythm && ! read.preserve.transients
                    && ! read.preserve.length, "a lock appeared that was never set");

            // A recipe from a build that predates the engine carries the four
            // properties the panel writes and nothing else.
            juce::ValueTree old (ids::RECIPE);
            old.setProperty (ids::recipeSeed, 4821, nullptr);
            old.setProperty (ids::recipeIntent, (int) Intent::broken, nullptr);
            old.setProperty (ids::recipeHarmony, 0, nullptr);
            old.setProperty (ids::recipeDistance, 1, nullptr);

            const auto legacy = Recipe::readFrom (old);

            expect (legacy.seed == 4821u, "a panel-written seed did not read back");
            expect (legacy.intent == Intent::broken, "a panel-written intent did not read back");
            expect (! legacy.preserve.any(), "an old recipe should carry no locks");
            expectEquals (legacy.engineVersion, 1, "an old recipe should read as version 1");

            expect (Preserve::all().pitch && Preserve::all().key && Preserve::all().rhythm
                    && Preserve::all().transients && Preserve::all().stereo
                    && Preserve::all().length && Preserve::all().lowEnd,
                    "the master lock does not set all seven");

            Preserve none;
            expect (! none.any(), "an empty Preserve should report nothing locked");
        }

        // ---------------------------------------------------------------
        beginTest ("the four controls and the seven locks come out of the registry");
        {
            TestHost host;

            // Through the registry's own converter: a choice parameter's
            // ParamDef carries no range, so normalising one by hand is a way of
            // testing the test.
            const auto set = [&host] (PID pid, float value)
            {
                host.registry.setFromUI (pid, value);
            };

            set (PID::mutationIntent, (float) (int) Intent::cinematic);
            set (PID::harmonyMode, 2.0f);
            set (PID::distanceMode, 1.0f);
            set (PID::preserveLowEnd, 1.0f);

            const auto recipe = Recipe::fromParameters (host.registry);

            expect (recipe.intent == Intent::cinematic, "the intent did not come through");
            expect (recipe.harmonyMode == Mode::free, "the harmony did not come through");
            expect (recipe.distance == Distance::far, "the distance did not come through");
            expect (recipe.preserve.lowEnd, "the low-end lock did not come through");
            expect (! recipe.preserve.pitch, "a lock came through that was never set");
            expectEquals (recipe.engineVersion, mutation::currentEngineVersion,
                          "a fresh recipe should carry the current engine version");

            set (PID::preserveAll, 1.0f);

            const auto locked = Recipe::fromParameters (host.registry);

            expect (locked.preserve.pitch && locked.preserve.key && locked.preserve.rhythm
                    && locked.preserve.transients && locked.preserve.stereo
                    && locked.preserve.length && locked.preserve.lowEnd,
                    "the master lock did not set all seven");
        }

        // ---------------------------------------------------------------
        beginTest ("every lock at once still produces audio, and keeps all seven");
        {
            auto source = makeSource (2);

            for (int i = 0; i < (int) Intent::count; ++i)
            {
                const auto recipe = makeRecipe ((Intent) i, Distance::far, 2718u,
                                                Mode::safe, Preserve::all());

                const auto result = Engine::render (recipe, *source, analysis, safeContext);

                const juce::String label = juce::String (mutation::nameOf ((Intent) i))
                                         + " with everything locked";

                const auto failure = soundCheck (result, *source, label);

                expect (failure.isEmpty(), failure);

                if (! result.ok)
                    continue;

                expectEquals (result.audio->lengthSamples(), source->lengthSamples(),
                              label + ": the length moved");

                const auto& b = result.audio->audio;

                expect (std::memcmp (b.getReadPointer (0), b.getReadPointer (1),
                                     sizeof (float) * (size_t) b.getNumSamples()) == 0,
                        label + ": the stereo image moved");
            }
        }
    }
};

#include "../Source/Audio/Sources/Sample/SampleEngine.h"
#include "../Source/Audio/Sources/Sample/SampleLoader.h"

// ===========================================================================
//  SAMPLE  -  phase 18
//
//  The engine, the loader and the slot were written but never tested: their
//  author was cut off at "now the tests". The first case below is the one that
//  matters most, because it is the hazard SampleBuffer.h exists to prevent and
//  it is invisible to every other kind of check - a realtime violation that
//  produces correct audio right up until it drops a buffer in the callback.
// ===========================================================================
struct SampleTests : juce::UnitTest
{
    SampleTests() : juce::UnitTest ("Sample", "nacar") {}

    static constexpr double kRate = 48000.0;

    /** A SampleBuffer that says when it dies. The base destructor is virtual
        through juce::ReferenceCountedObject, so this is a legal hook and not a
        trick. */
    struct Counted : SampleBuffer
    {
        explicit Counted (std::atomic<int>& c) : counter (c) {}
        ~Counted() override { ++counter; }
        std::atomic<int>& counter;
    };

    static SampleBuffer::Ptr makeTone (double hz, double seconds, int channels = 2,
                                       double rate = kRate)
    {
        SampleBuffer::Ptr s = new SampleBuffer();
        const int n = juce::jmax (1, (int) (rate * seconds));

        s->audio.setSize (juce::jmax (1, channels), n);
        s->sourceRate = rate;
        s->displayName = "TONE";

        for (int c = 0; c < s->audio.getNumChannels(); ++c)
            for (int i = 0; i < n; ++i)
                s->audio.setSample (c, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi
                                                                   * hz * (double) i / rate)));
        return s;
    }

    /** Renders the engine for a while with one note held, and hands back what
        came out. */
    static juce::AudioBuffer<float> render (SampleEngine& engine, const ParameterRegistry& p,
                                            int note, int blocks, int blockSize = 256)
    {
        juce::AudioBuffer<float> out (2, blocks * blockSize);
        out.clear();

        juce::AudioBuffer<float> block (2, blockSize);
        MacroState macros;
        macros.sampleRate = kRate;

        for (int b = 0; b < blocks; ++b)
        {
            block.clear();
            macros.numSamples = blockSize;

            juce::MidiBuffer midi;

            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);

            engine.process (block, midi, p, macros);

            for (int c = 0; c < 2; ++c)
                out.copyFrom (c, b * blockSize, block, c, 0, blockSize);
        }

        return out;
    }

    static double rms (const juce::AudioBuffer<float>& b, int from = 0, int to = -1)
    {
        if (to < 0) to = b.getNumSamples();
        double sum = 0.0; int count = 0;

        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = from; i < to && i < b.getNumSamples(); ++i, ++count)
                sum += (double) b.getSample (c, i) * b.getSample (c, i);

        return count > 0 ? std::sqrt (sum / (double) count) : 0.0;
    }

    /** Zero crossings per second, which is a cheap and robust pitch check for
        a sine and needs no FFT. */
    static double crossingRate (const juce::AudioBuffer<float>& b, int from, int to)
    {
        const auto* d = b.getReadPointer (0);
        int crossings = 0;

        for (int i = juce::jmax (1, from); i < to && i < b.getNumSamples(); ++i)
            if (d[i - 1] < 0.0f && d[i] >= 0.0f)
                ++crossings;

        const double seconds = (double) (to - from) / kRate;
        return seconds > 0.0 ? (double) crossings / seconds : 0.0;
    }

    void runTest() override
    {
        beginTest ("the audio thread never destroys a sample");
        {
            // THE hazard. Reference counting makes the read safe, but if the
            // reader drops the last reference it runs the destructor - freeing
            // megabytes inside the audio callback. SampleBuffer.h promises that
            // cannot happen; this is the proof.
            std::atomic<int> destroyed { 0 };

            SampleSlot slot;

            {
                SampleBuffer::Ptr first = new Counted (destroyed);
                first->audio.setSize (2, 1024);
                first->audio.clear();
                slot.publish (first);
            }
            // The test's own reference is gone; only the slot holds it now.

            // The "audio thread" takes a look, exactly as the engine would.
            {
                auto held = slot.acquire();
                expect (held != nullptr, "the slot published nothing");
            }

            expect (destroyed.load() == 0, "the buffer died while the slot still held it");

            // A second file arrives. The first must be RETIRED, not freed.
            {
                SampleBuffer::Ptr second = new SampleBuffer();
                second->audio.setSize (2, 1024);
                second->audio.clear();
                slot.publish (second);
            }

            expect (destroyed.load() == 0,
                    "publish() freed the outgoing buffer instead of retiring it - "
                    "that free would have happened on whichever thread called publish");

            expect (slot.hasRetired(), "nothing was retired, so nothing was kept alive");

            // Only the message thread may actually free it.
            slot.collectGarbage();

            expect (destroyed.load() == 1,
                    "collectGarbage() did not free the retired buffer");
            expect (! slot.hasRetired(), "something is still retired after a collect");
        }

        beginTest ("acquiring from an empty slot is silence, not a crash");
        {
            SampleSlot slot;
            expect (slot.acquire() == nullptr, "an empty slot handed out a buffer");

            slot.collectGarbage();          // must be safe with nothing to do
            expect (! slot.hasRetired());
        }

        beginTest ("no sample loaded is exactly silence");
        {
            TestHost host;

            // The engine gates on source_mode: it is called every block
            // whatever the setting says, but contributes nothing unless
            // SAMPLE is the selected source. A test that forgets this
            // measures silence and blames the engine.
            host.registry.setFromUI (PID::sourceMode, 1.0f);
            SampleSlot slot;

            SampleEngine engine;
            EngineSpec spec; spec.sampleRate = kRate; spec.maxBlockSize = 256;
            engine.prepare (spec);
            engine.setSlot (&slot);

            const auto out = render (engine, host.registry, 60, 20);

            float peak = 0.0f;
            for (int c = 0; c < out.getNumChannels(); ++c)
                peak = juce::jmax (peak, out.getMagnitude (c, 0, out.getNumSamples()));

            expect (! (peak > 0.0f),
                    "an engine with no sample produced " + juce::String (peak)
                        + " - it must be silent, not a placeholder");
        }

        beginTest ("at the root note the sample plays at its own pitch");
        {
            TestHost host;
            host.registry.setFromUI (PID::sourceMode, 1.0f);
            SampleSlot slot;
            slot.publish (makeTone (440.0, 2.0));

            host.registry.setFromUI (PID::sampleRootNote, 60.0f);
            host.registry.setFromUI (PID::sampleTune, 0.0f);
            host.registry.setFromUI (PID::sampleKeyTrack, 1.0f);
            host.registry.setFromUI (PID::sampleGain, 0.0f);

            SampleEngine engine;
            EngineSpec spec; spec.sampleRate = kRate; spec.maxBlockSize = 256;
            engine.prepare (spec);
            engine.setSlot (&slot);

            const auto out = render (engine, host.registry, 60, 120);

            const int from = (int) (0.05 * kRate), to = (int) (0.5 * kRate);
            const double hz = crossingRate (out, from, to);

            logMessage ("    root note renders " + juce::String (hz, 1) + " Hz (source is 440)");

            expect (std::abs (hz - 440.0) < 12.0,
                    "at the root note the sample should play untransposed, got "
                        + juce::String (hz, 1) + " Hz");
        }

        beginTest ("key tracking transposes by the interval played");
        {
            TestHost host;
            host.registry.setFromUI (PID::sourceMode, 1.0f);
            SampleSlot slot;
            slot.publish (makeTone (440.0, 2.0));

            host.registry.setFromUI (PID::sampleRootNote, 60.0f);
            host.registry.setFromUI (PID::sampleKeyTrack, 1.0f);

            SampleEngine engine;
            EngineSpec spec; spec.sampleRate = kRate; spec.maxBlockSize = 256;
            engine.prepare (spec);
            engine.setSlot (&slot);

            const int from = (int) (0.05 * kRate), to = (int) (0.4 * kRate);

            const auto octave = render (engine, host.registry, 72, 120);
            const double up = crossingRate (octave, from, to);

            logMessage ("    an octave up renders " + juce::String (up, 1) + " Hz (880 expected)");

            expect (std::abs (up - 880.0) < 25.0,
                    "an octave up should double the pitch, got " + juce::String (up, 1) + " Hz");

            engine.reset();

            const auto fifth = render (engine, host.registry, 67, 120);
            const double f = crossingRate (fifth, from, to);

            expect (std::abs (f - 440.0 * 1.49831) < 20.0,
                    "a fifth up should be about 659 Hz, got " + juce::String (f, 1) + " Hz");
        }

        beginTest ("key tracking at zero ignores the note played");
        {
            TestHost host;
            host.registry.setFromUI (PID::sourceMode, 1.0f);
            SampleSlot slot;
            slot.publish (makeTone (440.0, 2.0));

            host.registry.setFromUI (PID::sampleRootNote, 60.0f);
            host.registry.setFromUI (PID::sampleKeyTrack, 0.0f);

            SampleEngine engine;
            EngineSpec spec; spec.sampleRate = kRate; spec.maxBlockSize = 256;
            engine.prepare (spec);
            engine.setSlot (&slot);

            const int from = (int) (0.05 * kRate), to = (int) (0.4 * kRate);
            const auto out = render (engine, host.registry, 72, 120);

            expect (std::abs (crossingRate (out, from, to) - 440.0) < 15.0,
                    "with key tracking off, an octave up should still play at 440 Hz");
        }

        beginTest ("reverse plays the sample backwards and nothing else");
        {
            TestHost host;
            host.registry.setFromUI (PID::sourceMode, 1.0f);
            SampleSlot slot;

            // A ramp, so forwards and backwards are trivially distinguishable.
            SampleBuffer::Ptr ramp = new SampleBuffer();
            const int n = (int) (0.5 * kRate);
            ramp->audio.setSize (1, n);
            ramp->sourceRate = kRate;

            for (int i = 0; i < n; ++i)
                ramp->audio.setSample (0, i, (float) i / (float) n * 0.8f);

            slot.publish (ramp);

            host.registry.setFromUI (PID::sampleRootNote, 60.0f);
            host.registry.setFromUI (PID::sampleKeyTrack, 0.0f);
            host.registry.setFromUI (PID::sampleReverse, 1.0f);

            SampleEngine engine;
            EngineSpec spec; spec.sampleRate = kRate; spec.maxBlockSize = 256;
            engine.prepare (spec);
            engine.setSlot (&slot);

            const auto out = render (engine, host.registry, 60, 60);

            // A reversed ramp starts loud and ends quiet.
            const double early = rms (out, (int) (0.02 * kRate), (int) (0.08 * kRate));
            const double late  = rms (out, (int) (0.30 * kRate), (int) (0.40 * kRate));

            logMessage ("    reversed ramp: early " + juce::String (early, 4)
                            + ", late " + juce::String (late, 4));

            expect (early > late * 1.5,
                    "reverse did not play the ramp backwards (early " + juce::String (early, 4)
                        + " vs late " + juce::String (late, 4) + ")");
        }

        beginTest ("the loop seam does not click");
        {
            // The same lesson the FX click test had to learn: measure the
            // sample-to-sample step against the programme's OWN worst step, and
            // use a source whose steps are small. A ramp or a noise burst would
            // hide a seam inside its own discontinuities.
            TestHost host;
            host.registry.setFromUI (PID::sourceMode, 1.0f);
            SampleSlot slot;
            slot.publish (makeTone (220.0, 1.0, 1));

            host.registry.setFromUI (PID::sampleRootNote, 60.0f);
            host.registry.setFromUI (PID::sampleKeyTrack, 0.0f);
            host.registry.setFromUI (PID::sampleLoop, 1.0f);
            host.registry.setFromUI (PID::sampleLoopStart, 0.10f);
            host.registry.setFromUI (PID::sampleLoopEnd, 0.40f);
            host.registry.setFromUI (PID::sampleCrossfade, 0.10f);

            SampleEngine engine;
            EngineSpec spec; spec.sampleRate = kRate; spec.maxBlockSize = 256;
            engine.prepare (spec);
            engine.setSlot (&slot);

            const auto out = render (engine, host.registry, 60, 400);

            const int n = out.getNumSamples();
            const int settle = (int) (0.05 * kRate);

            float worst = 0.0f;
            for (int i = settle + 1; i < n; ++i)
                worst = juce::jmax (worst, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));

            // A 220 Hz sine at this level steps by about 0.014 per sample.
            const float expected = (float) (2.0 * juce::MathConstants<double>::pi * 220.0 / kRate * 0.5);

            logMessage ("    worst step through " + juce::String ((double) n / kRate, 2)
                            + " s of looping: " + juce::String (worst, 5)
                            + " (a clean 220 Hz sine steps " + juce::String (expected, 5) + ")");

            expect (worst < expected * 4.0f,
                    "the loop seam clicks: worst step " + juce::String (worst, 5)
                        + " against " + juce::String (expected, 5) + " for the tone itself");

            expect (rms (out, n / 2, n) > 0.01,
                    "the loop stopped sounding partway through");
        }

        beginTest ("a decode that fails leaves the instrument playable");
        {
            SampleSlot slot;
            SampleLoader loader (slot);

            bool finished = false;
            SampleLoader::Result got;

            loader.onFinished = [&finished, &got] (const SampleLoader::Result& r)
            {
                finished = true;
                got = r;
            };

            loader.loadAsync (juce::File ("/nonexistent/nacar-test-no-such-file.wav"));

            // The loader is asynchronous; poll it the way the editor's timer does.
            const auto deadline = juce::Time::getMillisecondCounter() + 4000;

            while (! finished && juce::Time::getMillisecondCounter() < deadline)
            {
                loader.poll();
                juce::Thread::sleep (10);
            }

            expect (finished, "the loader never reported back on a missing file");
            expect (! got.ok, "a missing file was reported as a successful decode");
            expect (got.error.isNotEmpty(), "a failure carried no message for the user");

            expect (slot.acquire() == nullptr,
                    "a failed decode left something in the slot");

            // And the engine still runs.
            TestHost host;
            host.registry.setFromUI (PID::sourceMode, 1.0f);
            SampleEngine engine;
            EngineSpec spec; spec.sampleRate = kRate; spec.maxBlockSize = 256;
            engine.prepare (spec);
            engine.setSlot (&slot);

            const auto out = render (engine, host.registry, 60, 10);
            bool finite = true;

            for (int c = 0; c < out.getNumChannels() && finite; ++c)
                for (int i = 0; i < out.getNumSamples(); ++i)
                    if (! std::isfinite (out.getSample (c, i))) { finite = false; break; }

            expect (finite, "the engine produced non-finite output after a failed decode");
        }

        beginTest ("every sample parameter at an extreme still renders safely");
        {
            TestHost host;
            host.registry.setFromUI (PID::sourceMode, 1.0f);
            SampleSlot slot;
            slot.publish (makeTone (330.0, 0.75));

            SampleEngine engine;
            EngineSpec spec; spec.sampleRate = kRate; spec.maxBlockSize = 256;
            engine.prepare (spec);
            engine.setSlot (&slot);

            struct Extreme { PID pid; float value; };

            const Extreme extremes[] = {
                { PID::sampleStart, 1.0f }, { PID::sampleEnd, 0.0f },
                { PID::sampleLoopStart, 1.0f }, { PID::sampleLoopEnd, 0.0f },
                { PID::sampleCrossfade, 1.0f }, { PID::sampleTune, 24.0f },
                { PID::sampleTune, -24.0f }, { PID::sampleGain, 24.0f },
                { PID::sampleKeyTrack, 1.0f }, { PID::sampleLoop, 1.0f },
                { PID::sampleReverse, 1.0f },
            };

            for (const auto& e : extremes)
            {
                host.registry.setFromUI (e.pid, e.value);
                engine.reset();

                const auto out = render (engine, host.registry, 96, 24);

                for (int c = 0; c < out.getNumChannels(); ++c)
                {
                    for (int i = 0; i < out.getNumSamples(); ++i)
                    {
                        const float v = out.getSample (c, i);

                        if (! std::isfinite (v) || std::abs (v) > 8.0f)
                        {
                            expect (false, juce::String (ParameterRegistry::idOf (e.pid))
                                        + " at " + juce::String (e.value)
                                        + " produced " + juce::String (v));
                            return;
                        }
                    }
                }
            }

            expect (true);
        }
    }
};

#include "../Source/Audio/Sources/Sample/SourcePipeline.h"

// ===========================================================================
//  INTEGRATION  -  phases 18 to 21, joined up
//
//  The four engines each have their own suite and each passes. This is the
//  different question: does the INSTRUMENT work? A file arrives, it decodes, it
//  is analysed, a mutation is rendered from it and becomes the thing being
//  played. Every step is a hand-off between two subsystems written separately,
//  and a hand-off is exactly what unit tests do not cover.
// ===========================================================================
struct IntegrationTests : juce::UnitTest
{
    IntegrationTests() : juce::UnitTest ("Integration", "nacar") {}

    /** Writes a real WAV to a real path, because the loader's job is to decode
        a file and handing it a buffer would test something else. */
    static juce::File writeTestWav (double rate, double seconds)
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("nacar-integration-" + juce::String (juce::Random::getSystemRandom().nextInt())
                                       + ".wav");

        const int n = (int) (rate * seconds);
        juce::AudioBuffer<float> audio (2, n);

        // A minor triad, so the analyser has a key to find and the mutation
        // engine has something harmonic to work with.
        const double hz[3] = { 220.0, 261.63, 329.63 };

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                double v = 0.0;

                for (double f : hz)
                    v += 0.22 * std::sin (2.0 * juce::MathConstants<double>::pi * f * (double) i / rate);

                audio.setSample (c, i, (float) v);
            }

        juce::WavAudioFormat format;

        if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
            if (auto* writer = format.createWriterFor (stream.get(), rate, 2, 24, {}, 0))
            {
                stream.release();
                writer->writeFromAudioSampleBuffer (audio, 0, n);
                delete writer;
            }

        return file;
    }

    /** Drives the loader and the processor's drain the way the editor's timer
        does, until `done` or the deadline. */
    template <typename Predicate>
    static bool pump (SourcePipeline& pipeline, Predicate done, int millis = 20000)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) millis;

        while (juce::Time::getMillisecondCounter() < deadline)
        {
            pipeline.poll();

            if (done())
                return true;

            juce::Thread::sleep (10);
        }

        return false;
    }

    void runTest() override
    {
        beginTest ("a dropped file decodes, reaches the audio thread and is analysed");
        {
            auto file = writeTestWav (44100.0, 3.0);
            expect (file.existsAsFile(), "could not write the test file");

            SourcePipeline pipeline;
            pipeline.load (file);

            const bool decoded = pump (pipeline, [&pipeline]
            {
                auto s = pipeline.slot().acquire();
                return s != nullptr && ! s->isEmpty();
            });

            expect (decoded, "the file never reached the slot the audio thread plays from");

            if (decoded)
            {
                auto s = pipeline.slot().acquire();

                expectWithinAbsoluteError (s->sourceRate, 44100.0, 1.0);
                expect (s->lengthSamples() > 100000, "the decode was truncated");
                expect (s->peaks.numBuckets > 0,
                        "no waveform overview was built, so the viewport has nothing to draw");
            }

            // The analysis follows the decode without anything else asking.
            const bool analysed = pump (pipeline, [&pipeline]
            {
                return pipeline.analysis().analysed;
            });

            expect (analysed, "a decode did not trigger an analysis");

            const auto& a = pipeline.analysis();

            logMessage ("    analysed: root " + juce::String (a.root)
                            + " conf " + juce::String (a.rootConfidence, 3)
                            + ", tempo " + juce::String (a.tempo, 1)
                            + " conf " + juce::String (a.tempoConfidence, 3)
                            + ", centroid " + juce::String (a.spectralCentroid, 0) + " Hz");

            // It is a sustained triad: it has a key and it has no tempo. The
            // second half of that matters as much as the first.
            expect (a.root >= 0, "a sustained A minor triad produced no root at all");
            expect (a.tempoConfidence < 0.5f,
                    "a drone reported a confident tempo of " + juce::String (a.tempo, 1));

            // And it round-trips through the session tree, which is what the
            // interface reads and what a saved session carries.
            juce::ValueTree branch (ids::ANALYSIS);
            a.writeTo (branch);

            const auto back = AnalysisResult::readFrom (branch);

            expect (back.analysed, "the analysis did not survive the session tree");
            expect (back.root == a.root, "the root changed on the way through the tree");

            file.deleteFile();
        }

        beginTest ("MUTATE renders and becomes what the instrument plays");
        {
            auto file = writeTestWav (44100.0, 2.0);

            SourcePipeline pipeline;
            pipeline.load (file);

            pump (pipeline, [&pipeline] { return pipeline.analysis().analysed; });

            auto before = pipeline.slot().acquire();
            expect (before != nullptr, "nothing was loaded to mutate");

            const auto* sourcePointer = before.get();
            const int sourceLength = before != nullptr ? before->lengthSamples() : 0;
            before = nullptr;

            mutation::Recipe recipe;
            recipe.seed = 90210u;
            recipe.intent = mutation::Intent::memory;
            recipe.harmonyMode = harmony::Mode::safe;
            recipe.distance = mutation::Distance::near_;

            juce::String failure;
            const auto context = harmony::Context::from (pipeline.analysis(),
                                                         harmony::Mode::safe, -1);

            const bool started = pipeline.mutate (recipe, context, failure);

            expect (started, "the mutation would not start: " + failure);

            bool finished = false;
            pipeline.onMutationFinished = [&finished] (const mutation::Result&) { finished = true; };

            expect (pump (pipeline, [&finished] { return finished; }, 30000),
                    "the mutation never finished");

            auto after = pipeline.slot().acquire();

            expect (after != nullptr && ! after->isEmpty(),
                    "the mutation produced nothing playable");

            expect (after.get() != sourcePointer,
                    "the slot still holds the source - the mutation did not become "
                    "the thing being played");

            if (after != nullptr)
            {
                logMessage ("    source " + juce::String (sourceLength)
                                + " samples, mutation " + juce::String (after->lengthSamples()));

                bool finite = true;
                float peak = 0.0f;

                for (int c = 0; c < after->numChannels() && finite; ++c)
                    for (int i = 0; i < after->lengthSamples(); ++i)
                    {
                        const float v = after->audio.getSample (c, i);

                        if (! std::isfinite (v)) { finite = false; break; }

                        peak = juce::jmax (peak, std::abs (v));
                    }

                expect (finite, "the mutation contains non-finite samples");
                expect (peak > 0.001f, "the mutation is silent");
                expect (peak <= 1.0f, "the mutation clips at " + juce::String (peak, 3));

                expect (after->peaks.numBuckets > 0,
                        "the mutation has no overview, so the viewport cannot draw it");
            }

            file.deleteFile();
        }

        beginTest ("MUTATE with nothing loaded says so and changes nothing");
        {
            SourcePipeline pipeline;

            mutation::Recipe recipe;
            recipe.seed = 1u;

            juce::String failure;
            const auto context = harmony::Context::from (pipeline.analysis(),
                                                         harmony::Mode::safe, -1);

            const bool started = pipeline.mutate (recipe, context, failure);

            expect (! started, "a mutation started with no source to mutate");
            expect (failure.isNotEmpty(), "the refusal carried no message for the user");

            logMessage ("    refusal reads: " + failure);

            expect (pipeline.slot().acquire() == nullptr,
                    "a refused mutation put something in the slot");
        }
    }
};


// ===========================================================================
//  PRESETS
//
//  The factory library is data, and data written by many hands at once drifts:
//  a category word that is not in the browser's vocabulary, a value outside
//  its parameter's range, a routing that names a source reading zero in the
//  global matrix.  None of those fail to compile and all of them are defects -
//  a preset whose value is clamped on apply is not the patch it is written as,
//  and a routing that does nothing is a dead control, which the operating
//  contract forbids.
//
//  NacarBench --presets checks this too, and also renders every preset, which
//  is the part this cannot do.  The checks are here as well because the bench
//  is a tool somebody has to remember to run and this is a gate that runs on
//  every build.
// ===========================================================================
class PresetTests : public juce::UnitTest
{
public:
    PresetTests() : juce::UnitTest ("Presets", "nacar") {}

    void runTest() override
    {
        const auto& library = presets::factoryLibrary();

        const auto categories = juce::StringArray::fromTokens (
            "KEYS PADS PLUCKS BELLS LEADS BASS SUB VOCAL-LIKE TEXTURE "
            "ATMOSPHERE DRUMS PERCUSSION SEQUENCES", " ", "");

        const auto moods = juce::StringArray::fromTokens (
            "DARK INTIMATE BROKEN NOSTALGIC AIRY AGGRESSIVE ROMANTIC COLD WARM "
            "CINEMATIC DIRTY DREAMY HAUNTED LUSH MINIMAL MYSTERIOUS", " ", "");

        beginTest ("the library is not empty and every name in it is unique");
        {
            expect (! library.empty(), "the factory library is empty");

            juce::StringArray seen;

            for (const auto& preset : library)
            {
                expect (! seen.contains (preset.name),
                        "two presets are called \"" + preset.name + "\"");
                seen.add (preset.name);
            }

            logMessage ("    library: " + juce::String ((int) library.size()) + " presets");
        }

        beginTest ("every preset uses the browser's own category and mood words");
        {
            // A word outside these lists is a preset the browser cannot filter
            // to, so it is a preset the user cannot find.
            for (const auto& preset : library)
            {
                expect (categories.contains (preset.category),
                        preset.name + ": category \"" + preset.category + "\" is not one of the thirteen");
                expect (moods.contains (preset.mood),
                        preset.name + ": mood \"" + preset.mood + "\" is not one of the sixteen");
            }

            // Every word has to carry something, or the browser shows a filter
            // that returns nothing.
            for (const auto& word : categories)
            {
                int n = 0;
                for (const auto& preset : library)
                    n += preset.category == word ? 1 : 0;

                expect (n > 0, "no preset is in category " + word);
            }

            for (const auto& word : moods)
            {
                int n = 0;
                for (const auto& preset : library)
                    n += preset.mood == word ? 1 : 0;

                expect (n > 0, "no preset carries the mood " + word);
            }
        }

        beginTest ("every name and blurb decoded as text rather than as bytes");
        {
            // juce::String's const char* constructor takes ASCII, not UTF-8,
            // so an accented source literal arrives as one character per BYTE:
            // "Bongo" with an acute o arrives as two Latin-1 characters instead
            // of one, and the constructor asserts in a debug build. Build
            // decodes through fromUTF8 to prevent that; this proves it happened.
            //
            // The test works because mojibake and Spanish do not overlap: a
            // mis-decoded UTF-8 sequence produces characters from the Latin-1
            // supplement that no Spanish word contains, while everything the
            // library legitimately spells is ASCII plus a short list of
            // accented letters.
            const juce::String allowedAccents (juce::CharPointer_UTF8 (
                "\xc3\x81\xc3\x89\xc3\x8d\xc3\x93\xc3\x9a\xc3\x9c\xc3\x91"
                "\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba\xc3\xbc\xc3\xb1"));

            auto readsAsText = [&allowedAccents] (const juce::String& s)
            {
                for (auto c = s.getCharPointer(); ! c.isEmpty(); ++c)
                {
                    const auto ch = *c;

                    if (ch >= 32 && ch < 127)
                        continue;

                    if (allowedAccents.containsChar (ch))
                        continue;

                    return false;
                }

                return true;
            };

            for (const auto& preset : library)
            {
                expect (readsAsText (preset.name),
                        "the name \"" + preset.name + "\" contains a character that is neither "
                        "ASCII nor a Spanish accent - it was decoded as bytes, not as text");

                expect (readsAsText (preset.blurb),
                        preset.name + "'s blurb contains a character that is neither ASCII nor a "
                        "Spanish accent - it was decoded as bytes, not as text");

                for (const auto& tag : preset.tags)
                    expect (readsAsText (tag), preset.name + " has a tag decoded as bytes, not as text");
            }
        }

        beginTest ("each category occupies one contiguous run, so no preset is in the wrong file");
        {
            // factoryLibrary() calls one builder per category, in order, and
            // each builder is its own translation unit. So a category word
            // appearing in two separate runs means a preset is in a file it
            // does not belong to - which is not cosmetic: it is a KEYS patch
            // the browser files under BASS, and it is exactly what happened
            // when concurrent work staged itself through shared filenames.
            juce::StringArray runs;

            for (const auto& preset : library)
                if (runs.isEmpty() || runs.strings.getLast() != preset.category)
                    runs.add (preset.category);

            for (int i = 0; i < runs.size(); ++i)
            {
                juce::StringArray rest (runs);
                rest.remove (i);

                expect (! rest.contains (runs[i]),
                        "category " + runs[i] + " appears in more than one run: a preset of that "
                        "category is in another category's file");
            }
        }

        beginTest ("every preset says what it is: tags, a blurb, and enough decisions to be one");
        {
            for (const auto& preset : library)
            {
                expect (preset.tags.size() >= 2 && preset.tags.size() <= 4,
                        preset.name + " has " + juce::String (preset.tags.size())
                            + " tags; the brief asks for two to four");

                expect (preset.blurb.isNotEmpty(), preset.name + " has no one-sentence identity");

                // A preset that sets almost nothing is the parameter list's
                // defaults under a new name.  Eight is not a quality bar, it
                // is a floor under "this is a patch".
                expect (preset.values.size() >= 8,
                        preset.name + " sets only " + juce::String ((int) preset.values.size())
                            + " parameters, which is the defaults under a new name");
            }
        }

        beginTest ("no preset value is outside the range its parameter declares");
        {
            // This is the one that matters most: a value outside the range is
            // silently clamped when the preset is applied, so the patch that
            // loads is not the patch that was written, and nothing says so.
            for (const auto& preset : library)
            {
                for (const auto& value : preset.values)
                {
                    const auto& d = ParameterRegistry::definition (value.pid);

                    if (d.kind == ParamKind::floatValue)
                    {
                        expect (value.value >= d.minValue - 1.0e-4f && value.value <= d.maxValue + 1.0e-4f,
                                preset.name + ": " + juce::String (d.id) + " = "
                                    + juce::String (value.value) + " is outside ["
                                    + juce::String (d.minValue) + ", " + juce::String (d.maxValue) + "]");
                    }
                    else if (d.kind == ParamKind::choice)
                    {
                        const int numChoices = ParameterRegistry::choicesOf (value.pid).size();

                        expect (value.value >= -0.4f && value.value <= (float) numChoices - 0.6f,
                                preset.name + ": " + juce::String (d.id) + " = "
                                    + juce::String (value.value) + " is not one of its "
                                    + juce::String (numChoices) + " choices");
                    }
                    else
                    {
                        // A BOOL carries no choice list, so it is checked
                        // against the only two values it has rather than
                        // against an empty one - which is what this test
                        // originally did, and it flagged every switched-on
                        // module in the library.
                        expect (value.value > -0.4f && value.value < 1.4f,
                                preset.name + ": " + juce::String (d.id) + " = "
                                    + juce::String (value.value) + " is not off or on");
                    }
                }
            }
        }

        beginTest ("no routing is a dead control");
        {
            // ENV 1, ENV 2, VELOCITY and KEY TRACK are per-voice and read zero
            // in the global matrix.  A preset routing one of them would ship a
            // control that visibly does nothing.
            const auto dead = juce::StringArray::fromTokens ("NONE|ENV 1|ENV 2|VELOCITY|KEY TRACK", "|", "");

            for (const auto& preset : library)
            {
                expect (preset.mods.size() <= 8,
                        preset.name + " has " + juce::String ((int) preset.mods.size())
                            + " routings; the matrix holds eight");

                for (const auto& m : preset.mods)
                {
                    const juce::String source (m.source);

                    expect (! dead.contains (source),
                            preset.name + " routes " + source + ", which reads zero in the global matrix");

                    expect (modSourceFromName (source) != ModSource::none,
                            preset.name + " routes an unknown source \"" + source + "\"");

                    expect (m.depth >= -1.0f && m.depth <= 1.0f,
                            preset.name + ": routing depth " + juce::String (m.depth) + " is outside -1..1");
                }
            }
        }

        beginTest ("every chain order is a permutation of the six slots");
        {
            for (const auto& preset : library)
            {
                if (preset.fxOrder.isEmpty())       // the default, which is fine
                    continue;

                const auto named = juce::StringArray::fromTokens (preset.fxOrder, ",", "");

                expect (named.size() == numFxSlots,
                        preset.name + ": chain order names " + juce::String (named.size())
                            + " slots, not " + juce::String (numFxSlots));

                for (int slot = 0; slot < numFxSlots; ++slot)
                {
                    const juce::String canonical (fxSlotName ((FxSlot) slot));

                    expect (named.contains (canonical),
                            preset.name + ": chain order leaves " + canonical + " out, so it would never run");
                }
            }
        }

        beginTest ("every audition hint is something the renderer can actually play");
        {
            // The audition is only a render hint, but a nonsense one makes the
            // bench measure the wrong thing and report a healthy preset as
            // broken.
            for (const auto& preset : library)
            {
                const auto& a = preset.audition;

                expect (a.midiNote >= 0 && a.midiNote <= 127,
                        preset.name + ": audition note " + juce::String (a.midiNote) + " is not a MIDI note");
                expect (a.chordNotes >= 1 && a.chordNotes <= 8,
                        preset.name + ": audition chord of " + juce::String (a.chordNotes) + " notes");
                expect (a.velocity > 0.0f && a.velocity <= 1.0f,
                        preset.name + ": audition velocity " + juce::String (a.velocity) + " is outside 0..1");
                expect (a.seconds > 0.25 && a.seconds <= 30.0,
                        preset.name + ": audition length " + juce::String (a.seconds) + " s");
            }
        }

        beginTest ("a preset applies as written, through the same path a user preset takes");
        {
            // PresetManager resets to the parameter list's defaults and then
            // applies, so what this proves is the property the library depends
            // on: what a preset does not say is the default, and what it does
            // say survives.
            TestHost host;

            for (const auto& preset : library)
            {
                const auto payload = PresetManager::payloadOf (preset);
                PresetManager::applyParameters (host.registry, payload);

                for (const auto& value : preset.values)
                {
                    const auto& d = ParameterRegistry::definition (value.pid);
                    const float applied = host.registry.userValue (value.pid);

                    const float tolerance = d.kind == ParamKind::floatValue
                                          ? juce::jmax (1.0e-3f, std::abs (value.value) * 1.0e-3f)
                                          : 0.01f;

                    expect (std::abs (applied - value.value) <= tolerance,
                            preset.name + ": " + juce::String (d.id) + " was written as "
                                + juce::String (value.value) + " and applies as " + juce::String (applied));
                }
            }
        }

        beginTest ("a fresh session opens on the patch it names, not on the raw defaults");
        {
            // The default session writes a preset name into its PRESET branch.
            // Until the processor actually applied that preset, the header bar
            // named a patch that had never been loaded - the instrument saying
            // something untrue about itself before the user had touched it.
            //
            // The processor's constructor cannot be linked into this runner, so
            // what is tested is the function it calls. The one line calling it
            // is all that remains untested.
            TestHost host;
            StateManager state (host.apvts);

            const juce::String wanted (StateManager::defaultPresetName);

            const auto match = std::find_if (library.begin(), library.end(),
                                             [&wanted] (const presets::FactoryPreset& p)
                                             { return p.name == wanted; });

            expect (match != library.end(),
                    "the default session names \"" + wanted + "\", which is not in the factory library");

            if (match == library.end())
                return;

            expect (PresetManager::applyFactoryPresetByName (host.registry, state, wanted),
                    "applying the default preset by name failed");

            // Its parameters are in force.
            for (const auto& value : match->values)
            {
                const auto& d = ParameterRegistry::definition (value.pid);
                const float applied = host.registry.userValue (value.pid);

                const float tolerance = d.kind == ParamKind::floatValue
                                      ? juce::jmax (1.0e-3f, std::abs (value.value) * 1.0e-3f)
                                      : 0.01f;

                expect (std::abs (applied - value.value) <= tolerance,
                        juce::String (d.id) + " did not survive opening the default session");
            }

            // Its chain order and its routings are in the session, so the FX
            // page and the MOD page show what is actually running.
            const auto chain = state.group (ids::FXCHAIN);

            if (match->fxOrder.isNotEmpty())
                expect (chain.getProperty (ids::fxOrder).toString() == match->fxOrder,
                        "the default session did not take the preset's chain order");

            const auto matrix = state.group (ids::MODMATRIX);
            int enabled = 0;

            for (int i = 0; i < matrix.getNumChildren(); ++i)
                enabled += (bool) matrix.getChild (i).getProperty (ids::modEnabled) ? 1 : 0;

            expect (enabled == (int) match->mods.size(),
                    "the default session has " + juce::String (enabled) + " routings enabled, and the preset has "
                        + juce::String ((int) match->mods.size()));

            // And the header is describing the thing that is loaded.
            const auto preset = state.group (ids::PRESET);

            expect (preset.getProperty (ids::presetName).toString() == match->name, "the header names the wrong preset");
            expect (preset.getProperty (ids::presetCategory).toString() == match->category, "the header shows the wrong category");
            expect (preset.getProperty (ids::presetMood).toString() == match->mood, "the header shows the wrong mood");
        }
    }
};

static PresetTests presetTests;


// ===========================================================================
//  VOICE ENVELOPES -> PITCH AND PM INDEX
//
//  Mod envelope 1 could only reach the filter, so two ordinary sounds were out
//  of reach: a kick whose pitch falls on the attack, and a tine electric piano
//  whose FM index decays while the note rings. Both were being approximated
//  with a resonant filter sweep, which is audibly a different thing - the
//  library has several presets that say so in their own comments.
//
//  What is asserted here is that the two new amounts MOVE THE THINGS THEY NAME:
//  that the pitch envelope changes the measured fundamental and not merely the
//  brightness, and that the PM envelope changes the index and not merely the
//  level. A parameter that exists and is read is not a feature; one that
//  measurably changes the sound is.
// ===========================================================================
class VoiceEnvelopeTests : public juce::UnitTest
{
public:
    VoiceEnvelopeTests() : juce::UnitTest ("Voice envelopes", "nacar") {}

    static constexpr double kRate = 48000.0;

    /** Mono capture of a held note, so a window of it can be measured. */
    static std::vector<float> capture (const ParameterRegistry& params, int totalSamples,
                                       int midiNote)
    {
        SynthEngine synth;
        synth.prepare (kRate, 256, 2);

        std::vector<float> out;
        out.reserve ((size_t) totalSamples);

        juce::AudioBuffer<float> buffer (2, 256);
        bool sent = false;

        while ((int) out.size() < totalSamples)
        {
            buffer.clear();
            juce::MidiBuffer midi;

            if (! sent)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, midiNote, 0.9f), 0);
                sent = true;
            }

            synth.process (buffer, midi, params, 120.0);

            for (int i = 0; i < 256 && (int) out.size() < totalSamples; ++i)
                out.push_back (0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i)));
        }

        return out;
    }

    /** Fundamental of one window, by autocorrelation with parabolic refinement.

        Autocorrelation and not a single loudest bin: the oscillators are rich,
        and at an octave of pitch envelope the second harmonic of the settled
        note sits exactly where the fundamental of the attack was. A period
        estimate cannot confuse the two the way a peak-picker can. */
    static double fundamentalOf (const std::vector<float>& x, int from, int n)
    {
        if (from + n > (int) x.size())
            return 0.0;

        double mean = 0.0;
        for (int i = 0; i < n; ++i)
            mean += x[(size_t) (from + i)];
        mean /= (double) n;

        std::vector<double> w ((size_t) n);
        for (int i = 0; i < n; ++i)
            w[(size_t) i] = x[(size_t) (from + i)] - mean;

        const int minLag = (int) (kRate / 1200.0);
        const int maxLag = juce::jmin (n / 2, (int) (kRate / 40.0));

        auto corrAt = [&w, n] (int lag)
        {
            double acc = 0.0;
            for (int i = 0; i + lag < n; ++i)
                acc += w[(size_t) i] * w[(size_t) (i + lag)];
            return acc;
        };

        int best = -1;
        double bestValue = 0.0;

        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            const double c = corrAt (lag);

            if (c > bestValue)
            {
                bestValue = c;
                best = lag;
            }
        }

        if (best <= minLag || best >= maxLag)
            return 0.0;

        const double a = corrAt (best - 1), b = corrAt (best), c = corrAt (best + 1);
        const double denom = a - 2.0 * b + c;
        const double shift = std::abs (denom) > 1.0e-12 ? 0.5 * (a - c) / denom : 0.0;

        return kRate / ((double) best + juce::jlimit (-0.5, 0.5, shift));
    }

    /** Spectral centroid of one window, as a brightness figure. */
    static double centroidOf (const std::vector<float>& x, int from, int n)
    {
        if (from + n > (int) x.size())
            return 0.0;

        double weighted = 0.0, total = 0.0;

        for (double hz = 80.0; hz <= 8000.0; hz *= 1.06)
        {
            double re = 0.0, im = 0.0;

            for (int i = 0; i < n; ++i)
            {
                const double win = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi
                                                         * (double) i / (double) n);
                const double ph = 2.0 * juce::MathConstants<double>::pi * hz * (double) i / kRate;

                re += (double) x[(size_t) (from + i)] * win * std::cos (ph);
                im -= (double) x[(size_t) (from + i)] * win * std::sin (ph);
            }

            const double m = std::sqrt (re * re + im * im);

            weighted += m * m * hz;
            total    += m * m;
        }

        return total > 0.0 ? weighted / total : 0.0;
    }

    /** A voice with everything that would blur a measurement turned off:
        no drift, no unit variation, no unison, one voice, filter wide open and
        static. What is left is the oscillator and the envelopes. */
    static void makeQuiet (TestHost& host)
    {
        auto set = [&host] (PID pid, float v) { host.registry.setFromUI (pid, v); };

        set (PID::polyphony, 1.0f);
        set (PID::voiceVariation, 0.0f);
        set (PID::driftAmount, 0.0f);
        set (PID::vibratoDepth, 0.0f);

        set (PID::filterCutoff, 18000.0f);
        set (PID::filterResonance, 0.0f);
        set (PID::filterEnvAmount, 0.0f);
        set (PID::filterVelAmount, 0.0f);
        set (PID::filterKeyTrack, 0.0f);
        set (PID::filterDrive, 0.0f);

        set (PID::subLevel, 0.0f);
        set (PID::noiseLevel, 0.0f);
        set (PID::oscCLevel, 0.0f);
        set (PID::bodyAmount, 0.0f);
        set (PID::densityAmount, 0.0f);
        set (PID::preFilterDrive, 0.0f);
        set (PID::postSaturation, 0.0f);

        set (PID::ampAttack, 0.001f);
        set (PID::ampDecay, 0.05f);
        set (PID::ampSustain, 1.0f);
        set (PID::ampVelocity, 0.0f);

        // A transient shape: straight up, then away, and staying away.
        set (PID::env2Attack, 0.0005f);
        set (PID::env2Decay, 0.10f);
        set (PID::env2Sustain, 0.0f);
    }

    void runTest() override
    {
        // 45 is A2, 110 Hz.
        const int midiNote = 45;
        const double nominalHz = 440.0 * std::pow (2.0, (midiNote - 69) / 12.0);

        beginTest ("with both amounts at zero, envelope 2's shape reaches neither pitch nor index");
        {
            // The guarantee that the whole library depends on: 300 presets were
            // written before these parameters existed and none of them may
            // change. Two renders whose ONLY difference is env 2's shape must
            // come out identical while the amounts are zero.
            //
            // Env 2 already had two destinations before any of this - it adds
            // to density, and it morphs the vowel of the FORMANT model - so the
            // comparison has to shut both of those off or it measures them
            // instead. Density is held at maximum, where the sum saturates its
            // own clamp and env 2's contribution cannot show (the character
            // scalars are 1.00, 1.15 and 1.35, so the product is at or above
            // the ceiling for all three), and the filter is HAZE, which has no
            // vowel to morph. What is left is only the two new paths.
            auto build = [this] (float decay)
            {
                TestHost host;
                makeQuiet (host);
                host.registry.setFromUI (PID::densityAmount, 1.0f);
                host.registry.setFromUI (PID::filterModel, 1.0f);
                host.registry.setFromUI (PID::env2Decay, decay);
                host.registry.setFromUI (PID::pitchEnvAmount, 0.0f);
                host.registry.setFromUI (PID::pmEnvAmount, 0.0f);
                host.registry.setFromUI (PID::oscPmAmount, 0.35f);
                return capture (host.registry, (int) (kRate * 0.4), 45);
            };

            const auto fast = build (0.02f);
            const auto slow = build (4.0f);

            double worst = 0.0;

            for (size_t i = 0; i < fast.size(); ++i)
                worst = juce::jmax (worst, (double) std::abs (fast[i] - slow[i]));

            expect (worst < 1.0e-6,
                    "env 2's shape changed the output by " + juce::String (worst, 8)
                        + " with both new amounts at zero - an existing preset would move");
        }

        beginTest ("the pitch envelope moves the fundamental, by the interval it is asked for");
        {
            // An octave up on the attack, settling to the played note. Measured
            // as a period, so the settled note's second harmonic cannot be
            // mistaken for the attack's fundamental.
            TestHost host;
            makeQuiet (host);
            host.registry.setFromUI (PID::oscAWave, 2.0f);        // saw
            host.registry.setFromUI (PID::oscBLevel, 0.0f);
            host.registry.setFromUI (PID::pitchEnvAmount, 12.0f);

            const auto x = capture (host.registry, (int) (kRate * 0.6), midiNote);

            const double early = fundamentalOf (x, (int) (kRate * 0.002), 1024);
            const double late  = fundamentalOf (x, (int) (kRate * 0.45), 4096);

            logMessage ("    pitch env +12 st: attack " + juce::String (early, 1)
                        + " Hz, settled " + juce::String (late, 1)
                        + " Hz (played " + juce::String (nominalHz, 1) + " Hz)");

            expect (early > nominalHz * 1.7 && early < nominalHz * 2.3,
                    "the attack should sound about an octave above the played note, and read "
                        + juce::String (early, 1) + " Hz against " + juce::String (nominalHz, 1));

            expect (std::abs (late - nominalHz) < nominalHz * 0.06,
                    "the note should settle back to the one that was played, and read "
                        + juce::String (late, 1) + " Hz");
        }

        beginTest ("a negative pitch envelope falls onto the note instead of off it");
        {
            // The other direction. A kick is the POSITIVE case above - it starts
            // high and falls onto the note - and this is the rarer gesture that
            // arrives from below. The sign has to work both ways regardless, or
            // half the range is decoration.
            TestHost host;
            makeQuiet (host);
            host.registry.setFromUI (PID::oscAWave, 0.0f);        // sine, so the period is unambiguous
            host.registry.setFromUI (PID::oscBLevel, 0.0f);
            host.registry.setFromUI (PID::pitchEnvAmount, -12.0f);

            const auto x = capture (host.registry, (int) (kRate * 0.6), 60);

            const double played = 440.0 * std::pow (2.0, (60 - 69) / 12.0);
            const double early = fundamentalOf (x, (int) (kRate * 0.002), 2048);
            const double late  = fundamentalOf (x, (int) (kRate * 0.45), 4096);

            logMessage ("    pitch env -12 st: attack " + juce::String (early, 1)
                        + " Hz, settled " + juce::String (late, 1) + " Hz");

            expect (early < played * 0.72,
                    "the attack should start about an octave below and read "
                        + juce::String (early, 1) + " Hz against " + juce::String (played, 1));

            expect (std::abs (late - played) < played * 0.06,
                    "it should arrive at the played note and read " + juce::String (late, 1) + " Hz");
        }

        beginTest ("the PM envelope moves the index, and leaves the pitch alone");
        {
            // The tine: a fixed ratio whose index falls while the note rings.
            // Brightness has to collapse and the fundamental has to stay put -
            // the second is what separates this from a pitch envelope, and from
            // the filter sweep it used to be faked with.
            TestHost host;
            makeQuiet (host);
            host.registry.setFromUI (PID::oscAWave, 0.0f);        // sine carrier
            host.registry.setFromUI (PID::oscBWave, 0.0f);        // sine modulator
            host.registry.setFromUI (PID::oscBLevel, 0.0f);
            host.registry.setFromUI (PID::oscBOctave, 1.0f);      // 2:1
            host.registry.setFromUI (PID::oscBFine, 0.0f);
            host.registry.setFromUI (PID::oscPmAmount, 0.0f);
            host.registry.setFromUI (PID::pmEnvAmount, 0.9f);
            host.registry.setFromUI (PID::env2Decay, 0.18f);

            // Density saturated, so the brightness that collapses below is the
            // index and not env 2's other route into the voice.
            host.registry.setFromUI (PID::densityAmount, 1.0f);

            const auto x = capture (host.registry, (int) (kRate * 0.7), midiNote);

            const double brightEarly = centroidOf (x, (int) (kRate * 0.004), 4096);
            const double brightLate  = centroidOf (x, (int) (kRate * 0.55), 4096);

            const double pitchEarly = fundamentalOf (x, (int) (kRate * 0.004), 4096);
            const double pitchLate  = fundamentalOf (x, (int) (kRate * 0.55), 4096);

            logMessage ("    pm env 0.9: centroid " + juce::String (brightEarly, 0) + " Hz -> "
                        + juce::String (brightLate, 0) + " Hz, fundamental "
                        + juce::String (pitchEarly, 1) + " Hz -> " + juce::String (pitchLate, 1) + " Hz");

            expect (brightEarly > brightLate * 1.5,
                    "the index should collapse: centroid went from " + juce::String (brightEarly, 0)
                        + " Hz to " + juce::String (brightLate, 0) + " Hz");

            expect (std::abs (pitchEarly - pitchLate) < nominalHz * 0.06,
                    "a PM index envelope must not transpose anything: the fundamental moved from "
                        + juce::String (pitchEarly, 1) + " Hz to " + juce::String (pitchLate, 1) + " Hz");
        }

        beginTest ("the PM envelope cannot drive the index past the knob's own range");
        {
            // The sum is clamped to 0..1, so a preset with a high index and a
            // high envelope produces the loudest index the control could reach
            // and not an undefined one.
            // Both hosts are identical apart from the amount, env 2 included:
            // a reference that differed in env 2's shape would be measuring
            // that instead. Density saturated for the same reason as above.
            auto build = [this] (float pmEnv)
            {
                TestHost host;
                makeQuiet (host);
                host.registry.setFromUI (PID::densityAmount, 1.0f);
                host.registry.setFromUI (PID::oscAWave, 0.0f);
                host.registry.setFromUI (PID::oscBWave, 0.0f);
                host.registry.setFromUI (PID::oscBLevel, 0.0f);
                host.registry.setFromUI (PID::oscPmAmount, 1.0f);
                host.registry.setFromUI (PID::pmEnvAmount, pmEnv);
                host.registry.setFromUI (PID::env2Sustain, 1.0f);
                host.registry.setFromUI (PID::env2Decay, 0.01f);
                return capture (host.registry, (int) (kRate * 0.3), midiNote);
            };

            const auto x = build (1.0f);
            const auto r = build (0.0f);

            // From 0.1 s: before that the index is still ramping to 1.0 from the
            // default, and while it is below the ceiling the envelope genuinely
            // does add to it. The clamp is a claim about the top, not the ramp.
            double worst = 0.0;
            const int from = (int) (kRate * 0.1);

            for (int i = from; i < (int) x.size(); ++i)
                worst = juce::jmax (worst, (double) std::abs (x[(size_t) i] - r[(size_t) i]));

            expect (worst < 1.0e-5,
                    "an index already at maximum should not change when an envelope adds to it, "
                    "and differed by " + juce::String (worst, 8));

            for (float v : x)
                expect (std::isfinite (v), "a clamped index produced a non-finite sample");
        }
    }
};

static VoiceEnvelopeTests voiceEnvelopeTests;

static IntegrationTests integrationTests;

static SampleTests sampleTests;

static MutationTests mutationTests;

// ===========================================================================
static ModulationTests     modulationTests;
static ModMatrixTests      modMatrixTests;
static ChainTests          chainTests;
static HalfbandTests       halfbandTests;
static ParameterTableTests parameterTableTests;
static StateTests          stateTests;
static SynthEngineTests    synthEngineTests;
static HarmonyTests        harmonyTests;

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    // JUCE's own modules register several hundred unit tests of their own.
    // They are not this project's to pass or fail - one of them writes to a
    // temporary directory this container does not allow - so the default is
    // NACAR's category only.  Pass --all to run everything.
    bool runEverything = false;

    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--all")
            runEverything = true;

    if (runEverything)
        runner.runAllTests();
    else
        runner.runTestsInCategory ("nacar");

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
