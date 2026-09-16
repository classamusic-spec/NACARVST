#include "NacarEngine.h"

#include "DspCommon.h"

#include "Memory/MemoryEngine.h"
#include "Modulation/ModulationEngine.h"

#include "FX/RetroEngine.h"
#include "FX/CrushEngine.h"
#include "FX/FilterFX.h"
#include "FX/RewindEngine.h"
#include "FX/GrainFX.h"
#include "FX/SpaceEngine.h"

#include "Atmosphere/AuraEngine.h"
#include "Atmosphere/ShadowEngine.h"
#include "Atmosphere/PatinaEngine.h"

#include "Weight/WeightEngine.h"

namespace nacar
{
    // =======================================================================
    //  FxSlot / FxOrder
    // =======================================================================
    const char* fxSlotName (FxSlot s) noexcept
    {
        switch (s)
        {
            case FxSlot::retro:  return "RETRO";
            case FxSlot::crush:  return "CRUSH";
            case FxSlot::filter: return "FILTER";
            case FxSlot::rewind: return "REWIND";
            case FxSlot::grain:  return "GRAIN";
            case FxSlot::space:  return "SPACE";
            case FxSlot::count:
            default:             return "";
        }
    }

    FxSlot fxSlotFromName (juce::StringRef name) noexcept
    {
        for (int i = 0; i < numFxSlots; ++i)
            if (juce::String (name).equalsIgnoreCase (fxSlotName ((FxSlot) i)))
                return (FxSlot) i;

        return FxSlot::count;
    }

    FxOrder FxOrder::defaultOrder() noexcept
    {
        FxOrder o;

        for (int i = 0; i < numFxSlots; ++i)
            o.slots[(size_t) i] = (FxSlot) i;

        o.count = numFxSlots;
        o.bypassMask = 0;
        return o;
    }

    FxOrder FxOrder::fromState (juce::StringRef order, juce::StringRef bypassed) noexcept
    {
        FxOrder o;
        o.count = 0;
        o.bypassMask = 0;

        const auto names = juce::StringArray::fromTokens (juce::String (order), ",", "");

        for (const auto& n : names)
        {
            const auto slot = fxSlotFromName (n.trim());

            if (slot == FxSlot::count || o.count >= numFxSlots)
                continue;

            // A name repeated in the stored order would process a slot twice,
            // and one engine cannot be in two places in a chain.
            bool already = false;
            for (int i = 0; i < o.count; ++i)
                already = already || (o.slots[(size_t) i] == slot);

            if (! already)
                o.slots[(size_t) o.count++] = slot;
        }

        if (o.count == 0)
            return defaultOrder();

        const auto offNames = juce::StringArray::fromTokens (juce::String (bypassed), ",", "");

        for (const auto& n : offNames)
        {
            const auto slot = fxSlotFromName (n.trim());

            if (slot != FxSlot::count)
                o.bypassMask |= (1u << (juce::uint32) slot);
        }

        return o;
    }

    juce::uint32 FxOrder::pack() const noexcept
    {
        juce::uint32 packed = (juce::uint32) juce::jlimit (0, numFxSlots, count) & 0x7u;

        for (int i = 0; i < numFxSlots; ++i)
        {
            const auto s = (juce::uint32) slots[(size_t) i] & 0x7u;
            packed |= s << (3u + (juce::uint32) i * 3u);
        }

        packed |= (bypassMask & 0x3Fu) << 21u;
        return packed;
    }

    FxOrder FxOrder::unpack (juce::uint32 packed) noexcept
    {
        FxOrder o;
        o.count = (int) (packed & 0x7u);

        for (int i = 0; i < numFxSlots; ++i)
        {
            const auto s = (packed >> (3u + (juce::uint32) i * 3u)) & 0x7u;
            o.slots[(size_t) i] = (FxSlot) juce::jlimit (0, numFxSlots - 1, (int) s);
        }

        o.bypassMask = (packed >> 21u) & 0x3Fu;

        if (o.count <= 0 || o.count > numFxSlots)
            return defaultOrder();

        return o;
    }

    // =======================================================================
    //  Impl
    // =======================================================================
    struct NacarEngine::Impl
    {
        bool prepared = false;
        EngineSpec spec;

        SynthEngine synth;
        ModulationEngine modulation;
        MemoryEngine memory;

        RetroEngine  retro;
        CrushEngine  crush;
        FilterFX     chainFilter;
        RewindEngine rewind;
        GrainFX      grain;
        SpaceEngine  space;

        ShadowEngine shadow;
        AuraEngine   aura;
        PatinaEngine patina;
        WeightEngine weight;

        std::atomic<juce::uint32> packedOrder { FxOrder::defaultOrder().pack() };

        // Every engine works on stereo.  When the host gives us a mono bus we
        // render here and fold at the end rather than making eleven engines
        // each handle a case that almost never happens.
        juce::AudioBuffer<float> stereo;

        // The output stage's band split, for the width destination.  Kept here
        // rather than in an engine because it is the chain's own last act.
        fx::ThreeBand widthSplitL, widthSplitR;
        fx::OnePole pulseGainSmoother;

        // -------------------------------------------------------------------
        void prepare (double sampleRate, int maxBlock, int numChannels)
        {
            spec.sampleRate = juce::jmax (1.0, sampleRate);
            spec.maxBlockSize = juce::jmax (1, maxBlock);
            spec.numChannels = juce::jmax (1, numChannels);

            synth.prepare (spec.sampleRate, spec.maxBlockSize, 2);

            modulation.prepare (spec);
            memory.prepare (spec);

            retro.prepare (spec);
            crush.prepare (spec);
            chainFilter.prepare (spec);
            rewind.prepare (spec);
            grain.prepare (spec);
            space.prepare (spec);

            shadow.prepare (spec);
            aura.prepare (spec);
            patina.prepare (spec);
            weight.prepare (spec);

            stereo.setSize (2, spec.maxBlockSize, false, true, true);
            stereo.clear();

            widthSplitL.prepare (140.0f, 2600.0f, spec.sampleRate);
            widthSplitR.prepare (140.0f, 2600.0f, spec.sampleRate);

            pulseGainSmoother.setTime (0.002f, spec.sampleRate);
            pulseGainSmoother.setValue (1.0f);

            prepared = true;
        }

        void reset()
        {
            synth.reset();
            modulation.reset();
            memory.reset();

            retro.reset();
            crush.reset();
            chainFilter.reset();
            rewind.reset();
            grain.reset();
            space.reset();

            shadow.reset();
            aura.reset();
            patina.reset();
            weight.reset();

            stereo.clear();
            widthSplitL.reset();
            widthSplitR.reset();
            pulseGainSmoother.reset();
            pulseGainSmoother.setValue (1.0f);
        }

        // -------------------------------------------------------------------
        void runFxSlot (FxSlot slot, juce::AudioBuffer<float>& b,
                        const ParameterRegistry& p, const MacroState& m)
        {
            switch (slot)
            {
                case FxSlot::retro:
                    if (p.flag (PID::retroOn))    retro.process (b, p, m);
                    break;

                case FxSlot::crush:
                    if (p.flag (PID::crushOn))    crush.process (b, p, m);
                    break;

                case FxSlot::filter:
                    if (p.flag (PID::fxFilterOn)) chainFilter.process (b, p, m);
                    break;

                case FxSlot::rewind:
                    if (p.flag (PID::rewindOn))   rewind.process (b, p, m);
                    break;

                case FxSlot::grain:
                    if (p.flag (PID::grainFxOn))  grain.process (b, p, m);
                    break;

                case FxSlot::space:
                    if (p.flag (PID::spaceOn))    space.process (b, p, m);
                    break;

                case FxSlot::count:
                default:
                    break;
            }
        }

        /**
            The output stage.

            Two of Pulse's five destinations land here rather than inside an
            engine, because they are properties of the finished sound rather
            than of any one process: how loud it is, and how wide.  The other
            three - filter, space and memory - are applied by the engines that
            own those behaviours, because a duck that darkens has to happen
            where the darkening happens.
        */
        void outputStage (juce::AudioBuffer<float>& b, const MacroState& m, int numSamples)
        {
            auto* l = b.getWritePointer (0);
            auto* r = b.getNumChannels() > 1 ? b.getWritePointer (1) : l;

            const bool stereoOut = b.getNumChannels() > 1;

            for (int i = 0; i < numSamples; ++i)
            {
                const float duck = m.pulseAt (i);

                // Volume: smoothed, because the duck envelope is fast and an
                // unsmoothed gain step at a block boundary is a click.
                const float target = 1.0f - duck * m.pulseToVolume;
                const float gain = pulseGainSmoother.process (juce::jlimit (0.0f, 1.0f, target));

                if (! stereoOut)
                {
                    l[i] = fx::guard (l[i] * gain);
                    continue;
                }

                // Width: the low band is never touched.  Narrowing a mix by
                // collapsing its bass is how a duck turns into a hole.
                float lowL, midL, highL, lowR, midR, highR;
                widthSplitL.split (l[i], lowL, midL, highL);
                widthSplitR.split (r[i], lowR, midR, highR);

                const float width = juce::jlimit (0.0f, 2.0f,
                                                  m.widthScale * (1.0f - duck * m.pulseToWidth));

                const float midMid  = (midL + midR) * 0.5f;
                const float midSide = (midL - midR) * 0.5f * width;

                const float hiMid  = (highL + highR) * 0.5f;
                const float hiSide = (highL - highR) * 0.5f * width;

                const float lowMono = (lowL + lowR) * 0.5f;

                l[i] = fx::guard ((lowMono + midMid + midSide + hiMid + hiSide) * gain);
                r[i] = fx::guard ((lowMono + midMid - midSide + hiMid - hiSide) * gain);
            }
        }

        // -------------------------------------------------------------------
        void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                      const ParameterRegistry& p, const TransportInfo& transport)
        {
            if (! prepared)
                return;

            const int numSamples = buffer.getNumSamples();

            if (numSamples <= 0)
                return;

            // Pulse can be triggered by MIDI.  The trigger lands at the top of
            // the block rather than at the note's exact offset: CLOCK is the
            // sample-accurate source, and a duck arriving a few milliseconds
            // early reads as intent rather than as an error.
            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn())
                    modulation.noteTriggered();

            // -- source ------------------------------------------------------
            const bool renderMono = buffer.getNumChannels() < 2;

            juce::AudioBuffer<float>& work = renderMono ? stereo : buffer;

            if (renderMono)
            {
                stereo.clear (0, numSamples);
                stereo.clear (1, numSamples);
            }

            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), 2, numSamples);
            view.clear();

            synth.process (view, midi, p, transport.bpm);

            // -- macros and modulation ---------------------------------------
            MacroState macros;
            resolveMacros (macros, p);

            macros.numSamples      = numSamples;
            macros.sampleRate      = spec.sampleRate;
            macros.hostBpm         = transport.bpm;
            macros.ppqPosition     = transport.ppqPosition;
            macros.transportPlaying = transport.playing;

            modulation.updateBlock (macros, p);

            // -- the chain ---------------------------------------------------
            memory.process (view, p, macros);

            const auto order = FxOrder::unpack (packedOrder.load (std::memory_order_relaxed));

            for (int i = 0; i < order.count; ++i)
            {
                const auto slot = order.slots[(size_t) i];

                if (! order.isBypassed (slot))
                    runFxSlot (slot, view, p, macros);
            }

            if (p.flag (PID::shadowOn))  shadow.process (view, p, macros);
            if (p.flag (PID::auraOn))    aura.process (view, p, macros);
            if (p.flag (PID::patinaOn))  patina.process (view, p, macros);

            weight.process (view, p, macros);

            outputStage (view, macros, numSamples);

            // -- fold, if the host asked for mono ----------------------------
            if (renderMono)
            {
                auto* out = buffer.getWritePointer (0);
                const auto* l = stereo.getReadPointer (0);
                const auto* r = stereo.getReadPointer (1);

                for (int i = 0; i < numSamples; ++i)
                    out[i] = 0.5f * (l[i] + r[i]);
            }
        }
    };

    // =======================================================================
    //  NacarEngine
    // =======================================================================
    NacarEngine::NacarEngine() : impl (std::make_unique<Impl>()) {}
    NacarEngine::~NacarEngine() = default;

    void NacarEngine::prepare (double sampleRate, int maximumBlockSize, int numChannels)
    {
        impl->prepare (sampleRate, maximumBlockSize, numChannels);
    }

    void NacarEngine::reset() { impl->reset(); }

    void NacarEngine::setOfflineRendering (bool offline) noexcept
    {
        impl->synth.setOfflineRendering (offline);
    }

    void NacarEngine::setFxOrder (const FxOrder& order) noexcept
    {
        impl->packedOrder.store (order.pack(), std::memory_order_relaxed);
    }

    void NacarEngine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                               const ParameterRegistry& params, const TransportInfo& transport)
    {
        impl->process (buffer, midi, params, transport);
    }

    void NacarEngine::allNotesOff() { impl->synth.allNotesOff(); }

    int NacarEngine::getActiveVoiceCount() const noexcept
    {
        return impl->synth.getActiveVoiceCount();
    }

    float NacarEngine::getLastPeak() const noexcept
    {
        return impl->synth.getLastPeak();
    }

    SynthEngine& NacarEngine::getSynth() noexcept { return impl->synth; }
}
