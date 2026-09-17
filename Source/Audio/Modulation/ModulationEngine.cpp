#include "ModulationEngine.h"

#include <algorithm>
#include <cmath>

namespace nacar
{
    namespace
    {
        // Seeds.  Constants, never a clock: a session must render identically
        // twice, on two machines, in two years.
        constexpr juce::uint32 kSeedLfo1    = 0x1A2B3C4Du;
        constexpr juce::uint32 kSeedLfo2    = 0x5E6F7A8Bu;
        constexpr juce::uint32 kSeedBreath  = 0x9C0D1E2Fu;
        constexpr juce::uint32 kSeedOrganic = 0x3F4A5B6Cu;

        /** ORGANIC RANDOM's fixed character.  Slower and more irregular than
            Breath's defaults, and only lightly stepped, so the two read as
            different things when both are routed at once. */
        const BreathEngine::Settings kOrganicSettings {
            true,       // enabled
            1.0f,       // amount: the matrix slot owns the depth
            0.31f,      // speed
            0.85f,      // random
            0.30f       // shape
        };
    }

    ModulationEngine::ModulationEngine() = default;
    ModulationEngine::~ModulationEngine() = default;

    // -----------------------------------------------------------------------
    void ModulationEngine::prepare (const EngineSpec& s)
    {
        spec = s;
        spec.sampleRate = juce::jmax (1.0, s.sampleRate);
        spec.maxBlockSize = juce::jmax (1, s.maxBlockSize);

        capacity = spec.maxBlockSize;

        lfoBuffer[0].assign ((size_t) capacity, 0.0f);
        lfoBuffer[1].assign ((size_t) capacity, 0.0f);
        breathBuffer .assign ((size_t) capacity, 0.0f);
        organicBuffer.assign ((size_t) capacity, 0.0f);

        lfo[0].prepare (spec.sampleRate, kSeedLfo1);
        lfo[1].prepare (spec.sampleRate, kSeedLfo2);

        breath .prepare (spec.sampleRate, kSeedBreath);
        organic.prepare (spec.sampleRate, kSeedOrganic);

        pulse.prepare (spec.sampleRate, capacity);

        prepared = true;
    }

    void ModulationEngine::reset()
    {
        lfo[0].reset();
        lfo[1].reset();
        breath.reset();
        organic.reset();
        pulse.reset();

        for (auto& b : lfoBuffer)
            std::fill (b.begin(), b.end(), 0.0f);

        std::fill (breathBuffer .begin(), breathBuffer .end(), 0.0f);
        std::fill (organicBuffer.begin(), organicBuffer.end(), 0.0f);

        modWheel.store (0.0f, std::memory_order_relaxed);
        aftertouch.store (0.0f, std::memory_order_relaxed);
    }

    // -----------------------------------------------------------------------
    void ModulationEngine::noteTriggered() noexcept          { pulse.noteTriggered (0); }
    void ModulationEngine::noteTriggeredAt (int o) noexcept  { pulse.noteTriggered (o); }

    void ModulationEngine::setSidechainInput (const float* mono, int n) noexcept
    {
        pulse.setSidechainInput (mono, n);
    }

    void ModulationEngine::setModWheel (float v) noexcept
    {
        modWheel.store (juce::jlimit (0.0f, 1.0f, v), std::memory_order_relaxed);
    }

    void ModulationEngine::setAftertouch (float v) noexcept
    {
        aftertouch.store (juce::jlimit (0.0f, 1.0f, v), std::memory_order_relaxed);
    }

    const float* ModulationEngine::pulseEnvelope (PulseEngine::Destination d) const noexcept
    {
        return pulse.envelope (d);
    }

    void ModulationEngine::rebuildModMatrix (const juce::ValueTree& tree)
    {
        modMatrix.rebuildFromTree (tree);
    }

    float ModulationEngine::modulationFor (PID p) const noexcept
    {
        return modMatrix.offsetFor (p);
    }

    // -----------------------------------------------------------------------
    void ModulationEngine::updateBlock (MacroState& m, const ParameterRegistry& p)
    {
        const int n = m.numSamples;

        if (! prepared || n <= 0)
            return;

        // A host that hands over a longer block than it promised in prepare()
        // would take every consumer off the end of these buffers.  There is no
        // way to grow them here without allocating on the audio thread, so the
        // modulation stops for that block and says so by leaving the pointers
        // null - MacroState's accessors already treat null as silence.
        jassert (n <= capacity);

        if (n > capacity)
        {
            m.lfo1 = m.lfo2 = m.breath = m.pulse = nullptr;
            m.pulseFilter = m.pulseSpace = m.pulseMemory = nullptr;
            return;
        }

        // The sample rate is the one prepare() computed every coefficient at.
        // MacroState carries its own copy; if the two ever disagree it means
        // prepare() was not called for the current rate, and trusting the
        // MacroState copy would only hide that.
        const mod::Clock clock { spec.sampleRate, m.hostBpm, m.ppqPosition, m.transportPlaying };

        // -- the registry, read once ----------------------------------------
        LFO::Settings lfoSettings[2];

        lfoSettings[0] = { p.choice (PID::lfo1Shape), p.flag (PID::lfo1Sync),
                           p.choice (PID::lfo1Division), p.raw (PID::lfo1Rate),
                           p.raw (PID::lfo1Depth), p.raw (PID::lfo1Phase) };

        lfoSettings[1] = { p.choice (PID::lfo2Shape), p.flag (PID::lfo2Sync),
                           p.choice (PID::lfo2Division), p.raw (PID::lfo2Rate),
                           p.raw (PID::lfo2Depth), p.raw (PID::lfo2Phase) };

        const float breathAmount = juce::jlimit (0.0f, 1.0f, p.raw (PID::breathAmount));
        const float movement = juce::jlimit (0.0f, 1.0f, m.movement);

        BreathEngine::Settings breathSettings;
        breathSettings.enabled = p.flag (PID::breathOn);

        // Macro response.  Motion can only add: a patch that asked for Breath
        // keeps it at Motion zero, and a patch that asked for none stays at
        // none only if it also set amount to zero - which is the one case where
        // "still" and "no breath" should agree.
        breathSettings.amount  = juce::jlimit (0.0f, 1.0f,
                                               breathAmount + 0.25f * movement * (1.0f - breathAmount));
        breathSettings.speedHz = p.raw (PID::breathSpeed);
        breathSettings.random  = p.raw (PID::breathRandom);
        breathSettings.shape   = p.raw (PID::breathShape);

        const bool pulseOn = p.flag (PID::pulseOn);

        PulseEngine::Settings pulseSettings;
        pulseSettings.enabled    = pulseOn;
        pulseSettings.source     = p.choice (PID::pulseSource);
        pulseSettings.division   = p.choice (PID::pulseDivision);
        pulseSettings.attackSec  = p.raw (PID::pulseAttack);
        pulseSettings.releaseSec = p.raw (PID::pulseRelease);
        pulseSettings.smooth     = p.raw (PID::pulseSmooth);

        // -- generate --------------------------------------------------------
        lfo[0].process (lfoBuffer[0].data(), n, lfoSettings[0], clock);
        lfo[1].process (lfoBuffer[1].data(), n, lfoSettings[1], clock);

        breath .process (breathBuffer .data(), n, breathSettings);
        organic.process (organicBuffer.data(), n, kOrganicSettings);

        pulse.process (n, pulseSettings, clock);

        // -- publish ---------------------------------------------------------
        m.lfo1   = lfoBuffer[0].data();
        m.lfo2   = lfoBuffer[1].data();
        m.breath = breathBuffer.data();
        m.pulse       = pulse.envelope (PulseEngine::Destination::volume);
        m.pulseFilter = pulse.envelope (PulseEngine::Destination::filter);
        m.pulseSpace  = pulse.envelope (PulseEngine::Destination::space);
        m.pulseMemory = pulse.envelope (PulseEngine::Destination::memory);

        const float depth = pulseOn ? juce::jlimit (0.0f, 1.0f, p.raw (PID::pulseDepth)) : 0.0f;

        m.pulseToVolume = depth * juce::jlimit (0.0f, 1.0f, p.raw (PID::pulseToVolume));
        m.pulseToFilter = depth * juce::jlimit (0.0f, 1.0f, p.raw (PID::pulseToFilter));
        m.pulseToSpace  = depth * juce::jlimit (0.0f, 1.0f, p.raw (PID::pulseToSpace));
        m.pulseToWidth  = depth * juce::jlimit (0.0f, 1.0f, p.raw (PID::pulseToWidth));
        m.pulseToMemory = depth * juce::jlimit (0.0f, 1.0f, p.raw (PID::pulseToMemory));

        // -- the matrix's block-rate source snapshot -------------------------
        //
        // The matrix answers one question per parameter per block, so it latches
        // one value per source.  Sample zero, not an average: it is the value in
        // force when the block begins, which is what a consumer that ramps a
        // parameter across the block wants as its starting point.
        ModMatrix::SourceValues sv {};

        sv[(size_t) ModSource::lfo1]          = lfoBuffer[0][0];
        sv[(size_t) ModSource::lfo2]          = lfoBuffer[1][0];
        sv[(size_t) ModSource::breath]        = breathBuffer[0];
        sv[(size_t) ModSource::organicRandom] = organicBuffer[0];
        sv[(size_t) ModSource::pulse]         = m.pulse[0];

        sv[(size_t) ModSource::modWheel]   = modWheel.load (std::memory_order_relaxed);
        sv[(size_t) ModSource::aftertouch] = aftertouch.load (std::memory_order_relaxed);

        // The macro sources are the knob positions, 0..1, not the derived
        // influences: a user routing MEMORY in the matrix means the control
        // they can see, not this engine's interpretation of it.
        sv[(size_t) ModSource::memory] = m.memory;
        sv[(size_t) ModSource::motion] = m.motion;
        sv[(size_t) ModSource::world]  = m.world;
        sv[(size_t) ModSource::alter]  = m.alter;

        // ENV 1, ENV 2, VELOCITY and KEY TRACK stay at zero.  They are per-voice
        // quantities that only exist inside a sounding voice, and inventing a
        // global stand-in for them would be a lie the user could hear.

        modMatrix.beginBlock (sv);
    }
}
