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
#include "../Source/Audio/Sources/Synth/Halfband.h"
#include "../Source/Audio/NacarEngine.h"
#include "../Source/Audio/Modulation/ModulationEngine.h"

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

        beginTest ("the chain does not decorrelate the low end");
        {
            // Specification sections 38, 40 and 43: width is never bought at
            // the cost of the low end.  Six stereo processes in series is where
            // that is most likely to be lost.
            TestHost host;

            for (auto pid : { PID::retroOn, PID::fxFilterOn, PID::spaceOn,
                              PID::auraOn, PID::shadowOn, PID::patinaOn })
                host.registry.setFromUI (pid, 1.0f);

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
static ModulationTests     modulationTests;
static ChainTests          chainTests;
static HalfbandTests       halfbandTests;
static ParameterTableTests parameterTableTests;
static StateTests          stateTests;
static SynthEngineTests    synthEngineTests;

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
