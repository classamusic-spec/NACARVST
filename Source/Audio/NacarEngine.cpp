#include "NacarEngine.h"

#include "DspCommon.h"

#include "Memory/MemoryEngine.h"
#include "Modulation/ModulationEngine.h"

#include "Sources/Sample/SampleEngine.h"

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

        /** The second SOURCE.  It sits beside the synth rather than after it:
            both add into the same buffer before Memory sees anything, so every
            stage downstream ages, colours and places the sample exactly as it
            does the synth.  Which of the two is actually heard is `source_mode`,
            and the sample engine reads that itself - see SampleEngine.cpp. */
        SampleEngine sampleSource;

        ModulationEngine modulation;
        MemoryEngine memory;

        /** The parameters the overlay is currently overriding, so the next
            block can take back the ones the matrix has stopped targeting. */
        PID activeTargets[ModMatrix::numSlots] {};
        int numActiveTargets = 0;

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

        // -------------------------------------------------------------------
        //  Latency
        //
        //  Two modules delay the signal while they are active - Retro by its
        //  transport's centre tap, Crush by its oversampling round trip - and
        //  both are zero while bypassed, so the figure moves as the user
        //  switches modules on and off.  It is published for the processor to
        //  report, which it does from the message thread: setLatencySamples()
        //  notifies the host and is not safe to call from here.
        // -------------------------------------------------------------------
        std::atomic<int> latencySamples { 0 };

        void updateLatency (const ParameterRegistry& p) noexcept
        {
            int total = 0;

            if (p.flag (PID::retroOn))
                total += retro.getLatencySamples();

            if (p.flag (PID::crushOn))
                total += crush.getLatencySamples();

            // Memory has no on/off: it reports zero when its macro is at zero,
            // and a generation-dependent onset delay of about 3 ms per copy
            // otherwise.  That figure MOVES when the user changes generation,
            // which is why the processor reports latency through an
            // AsyncUpdater rather than only at prepareToPlay.
            total += memory.getLatencySamples();

            latencySamples.store (total, std::memory_order_relaxed);
        }

        // Every engine works on stereo.  When the host gives us a mono bus we
        // render here and fold at the end rather than making eleven engines
        // each handle a case that almost never happens.
        juce::AudioBuffer<float> stereo;

        // The output stage's band split, for the width destination.  Kept here
        // rather than in an engine because it is the chain's own last act.
        fx::ThreeBand widthSplitL, widthSplitR;

        // -------------------------------------------------------------------
        void prepare (double sampleRate, int maxBlock, int numChannels)
        {
            spec.sampleRate = juce::jmax (1.0, sampleRate);
            spec.maxBlockSize = juce::jmax (1, maxBlock);
            spec.numChannels = juce::jmax (1, numChannels);

            synth.prepare (spec.sampleRate, spec.maxBlockSize, 2);
            sampleSource.prepare (spec);

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

            prepared = true;
        }

        void reset()
        {
            synth.reset();
            sampleSource.reset();
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
            latencySamples.store (0, std::memory_order_relaxed);
        }

        // -------------------------------------------------------------------
        /**
            Every engine is called on every block, whatever its enable says.

            Each one owns its own bypass and each one is exact - the buffer
            comes back sample for sample - but what they do *while* bypassed
            differs, and that difference is the reason the chain must not gate
            them:

              Retro and Crush keep their delay lines fed, because both read a
              dry tap from a delayed line.  Stop writing and the first few
              milliseconds after switching back on are whatever was in the line
              when it was switched off, which is an audible click on the power
              button.

              Space, Aura and Shadow flush their tails on the transition
              instead, so that switching one back on does not replay audio from
              minutes ago.  They can only do that if they see the transition.

            The chain gating them would have broken both behaviours at once.
        */
        /** The power parameter behind each card.  The `*_on` flags and the
            FxOrder bypass mask are two controls over the same thing - the ring
            on the card and the `-` glyph beside it - and the engines only know
            about the first. */
        static PID fxPowerPid (FxSlot slot) noexcept
        {
            switch (slot)
            {
                case FxSlot::retro:  return PID::retroOn;
                case FxSlot::crush:  return PID::crushOn;
                case FxSlot::filter: return PID::fxFilterOn;
                case FxSlot::rewind: return PID::rewindOn;
                case FxSlot::grain:  return PID::grainFxOn;
                case FxSlot::space:  return PID::spaceOn;
                case FxSlot::count:
                default:             return PID::count;
            }
        }

        /**
            Applies the chain's bypass MASK the same way the power ring works:
            by telling the engine it is off, not by refusing to call it.

            This used to be `if (! order.isBypassed (slot)) runFxSlot (...)`,
            which is the exact mistake the note on `runFxSlot` below warns
            against, made a second time through a different control.  Muting a
            card from the chain view starved Retro's and Crush's dry lines,
            starved Rewind's and Grain's histories, and denied Space the
            transition on which it flushes its tail - so the `-` glyph and the
            power ring, which are the same idea to a user, behaved differently.

            Forcing the flag instead means one code path: every engine sees a
            power-off exactly as it always has, `updateLatency` stops reporting
            a delay for a module that is muted, and the chain keeps running.
        */
        void applyFxBypass (const ParameterRegistry& p, const FxOrder& order)
        {
            for (int i = 0; i < numFxSlots; ++i)
            {
                const auto slot = (FxSlot) i;
                const auto pid  = fxPowerPid (slot);

                if (pid == PID::count)
                    continue;

                // A masked card, or one the order does not contain at all: an
                // engine the chain will not reach must not keep sounding.
                bool reached = false;

                for (int j = 0; j < order.count && ! reached; ++j)
                    reached = order.slots[(size_t) j] == slot;

                // The overlay is shared with the mod matrix, and this write
                // wins.  In practice the two cannot collide: the MOD page's
                // target menu offers float parameters only, so a routing can
                // never name a power flag.  If that ever changes, this is the
                // line that would silently discard it.
                if (reached && ! order.isBypassed (slot))
                    p.clearModulation (pid);
                else
                    p.setModulation (pid, -1.0f);   // clamps to the flag's minimum
            }
        }

        void runFxSlot (FxSlot slot, juce::AudioBuffer<float>& b,
                        const ParameterRegistry& p, const MacroState& m)
        {
            switch (slot)
            {
                case FxSlot::retro:  retro.process (b, p, m);       break;
                case FxSlot::crush:  crush.process (b, p, m);       break;
                case FxSlot::filter: chainFilter.process (b, p, m); break;
                case FxSlot::rewind: rewind.process (b, p, m);      break;
                case FxSlot::grain:  grain.process (b, p, m);       break;
                case FxSlot::space:  space.process (b, p, m);       break;

                case FxSlot::count:
                default:                                            break;
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

            // MacroState carries the VOLUME envelope, which is the reference
            // shape.  Width gets its own: specification section 83 is that a
            // kick makes a sound quieter AND narrower AND darker AND drier, and
            // the point of generating five differently-shaped envelopes is lost
            // if the consumer applies one of them to two destinations.  The
            // image recovers faster than the level does, and here is where that
            // difference becomes audible.
            const float* widthEnv =
                modulation.pulseEnvelope (PulseEngine::Destination::width);

            for (int i = 0; i < numSamples; ++i)
            {
                const float duck = m.pulseAt (i);

                // No smoothing here.  The envelope is already an envelope -
                // shaped attack, shaped release, its own curve control - and a
                // one-pole on top of it would quietly cap the attack at the
                // smoother's own time constant, which is exactly the fast end
                // of the control the user is reaching for.
                const float gain = juce::jlimit (0.0f, 1.0f, 1.0f - duck * m.pulseToVolume);

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

                const float widthDuck = widthEnv != nullptr ? widthEnv[i] : duck;

                const float width = juce::jlimit (0.0f, 2.0f,
                                                  m.widthScale * (1.0f - widthDuck * m.pulseToWidth));

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
        /**
            Publishes this block's modulation into the registry's overlay, so
            that every `p.raw()` below - in the synth, in all eleven chain
            engines, in the macro resolver - returns the value actually in
            force.  This is what makes the MOD page's matrix audible, and it is
            deliberately the only place that writes the overlay.

            Two things matter here.  The first is that an override that stops
            being targeted must be REMOVED: leaving it installed would freeze
            that parameter at whatever the matrix last pushed it to, and the
            user would find a knob that no longer does anything.  The second is
            that this runs before the synth renders, so a routing takes effect
            in the same block it was computed for rather than the next one.
        */
        void applyModulation (const ParameterRegistry& p)
        {
            const auto& routings = modulation.matrix().liveRoutings();

            PID targets[ModMatrix::numSlots];
            int numTargets = 0;

            for (const auto& r : routings)
            {
                if (! r.enabled || r.target == PID::count || r.source == ModSource::none)
                    continue;

                // offsetFor() already sums every routing that points at this
                // parameter, so a target that two slots share is written once.
                bool seen = false;

                for (int i = 0; i < numTargets && ! seen; ++i)
                    seen = targets[i] == r.target;

                if (! seen)
                    targets[numTargets++] = r.target;
            }

            // Drop last block's overrides that this block no longer wants.
            for (int i = 0; i < numActiveTargets; ++i)
            {
                bool stillTargeted = false;

                for (int j = 0; j < numTargets && ! stillTargeted; ++j)
                    stillTargeted = targets[j] == activeTargets[i];

                if (! stillTargeted)
                    p.clearModulation (activeTargets[i]);
            }

            for (int i = 0; i < numTargets; ++i)
                p.setModulation (targets[i], modulation.modulationFor (targets[i]));

            std::copy (targets, targets + numTargets, activeTargets);
            numActiveTargets = numTargets;
        }

        void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                      const ParameterRegistry& p, const TransportInfo& transport)
        {
            if (! prepared)
                return;

            const int numSamples = buffer.getNumSamples();

            if (numSamples <= 0)
                return;

            // Pulse's MIDI trigger, and the two live controllers the matrix
            // needs.  The trigger carries its sample offset: quantising a duck
            // to the top of the block is up to twenty-one milliseconds late at
            // a 1024-sample buffer, which on a kick is the difference between a
            // duck and a flam.
            for (const auto metadata : midi)
            {
                const auto message = metadata.getMessage();
                const int offset = juce::jlimit (0, numSamples - 1, metadata.samplePosition);

                if (message.isNoteOn())
                    modulation.noteTriggeredAt (offset);
                else if (message.isController() && message.getControllerNumber() == 1)
                    modulation.setModWheel ((float) message.getControllerValue() / 127.0f);
                else if (message.isChannelPressure())
                    modulation.setAftertouch ((float) message.getChannelPressureValue() / 127.0f);
                else if (message.isAftertouch())
                    modulation.setAftertouch ((float) message.getAfterTouchValue() / 127.0f);
            }

            const bool renderMono = buffer.getNumChannels() < 2;

            juce::AudioBuffer<float>& work = renderMono ? stereo : buffer;

            if (renderMono)
            {
                stereo.clear (0, numSamples);
                stereo.clear (1, numSamples);
            }

            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), 2, numSamples);
            view.clear();

            // -- macros and modulation ---------------------------------------
            //
            // Before the synth renders, not after: the matrix has to be able to
            // reach an oscillator, and a modulation that arrives a block late
            // is a modulation that flams against the note that triggered it.
            MacroState macros;
            resolveMacros (macros, p);

            macros.numSamples      = numSamples;
            macros.sampleRate      = spec.sampleRate;
            macros.hostBpm         = transport.bpm;
            macros.ppqPosition     = transport.ppqPosition;
            macros.transportPlaying = transport.playing;

            modulation.updateBlock (macros, p);
            applyModulation (p);

            // Again, now that the overlay is in place.  The matrix's macro
            // SOURCES are the knob positions - a user routing MEMORY means the
            // control they can see - but a macro is also a legal TARGET, and
            // without this second pass it would be the one dead entry in the
            // target menu.  There is no loop: the sources were latched above,
            // from the unmodulated values.
            resolveMacros (macros, p);

            // -- source ------------------------------------------------------
            //
            // Both sources, every block, adding into the same buffer.  The
            // sample engine is called whether or not `source_mode` selects it,
            // for the same reason every FX module is: it owns its own bypass
            // and its own fade off, and a source that is skipped rather than
            // gated cannot fade anything.  With no sample loaded it adds
            // silence - not a placeholder tone, not a click.
            synth.process (view, midi, p, transport.bpm);
            sampleSource.process (view, midi, p, macros);

            // -- the chain ---------------------------------------------------
            const auto order = FxOrder::unpack (packedOrder.load (std::memory_order_relaxed));

            // Before updateLatency, because a masked module must not report a
            // delay the signal does not actually incur.
            applyFxBypass (p, order);
            updateLatency (p);

            memory.process (view, p, macros);

            // Unconditionally, every block, in the user's order.  See
            // runFxSlot and applyFxBypass above for why there is no `if` here.
            for (int i = 0; i < order.count; ++i)
                runFxSlot (order.slots[(size_t) i], view, p, macros);

            // And then the slots the order does not contain, for the same
            // reason.  Removing a card from the chain view used to stop its
            // engine being called at all, which starves exactly the history and
            // delay lines the unconditional call above exists to keep fed - so
            // dragging a card out and back in produced the click that dragging
            // it out was supposed to avoid.  applyFxBypass has already forced
            // each of these off, so every one takes its own exact-bypass path
            // and does nothing but keep its lines current.  Their order among
            // themselves is irrelevant: a bypassed engine does not touch the
            // buffer.
            for (int i = 0; i < numFxSlots; ++i)
            {
                const auto slot = (FxSlot) i;

                bool inOrder = false;

                for (int j = 0; j < order.count && ! inOrder; ++j)
                    inOrder = order.slots[(size_t) j] == slot;

                if (! inOrder)
                    runFxSlot (slot, view, p, macros);
            }

            shadow.process (view, p, macros);
            aura.process   (view, p, macros);
            patina.process (view, p, macros);
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

    void NacarEngine::allNotesOff()
    {
        impl->synth.allNotesOff();
        impl->sampleSource.allNotesOff();
    }

    int NacarEngine::getActiveVoiceCount() const noexcept
    {
        return impl->synth.getActiveVoiceCount();
    }

    float NacarEngine::getLastPeak() const noexcept
    {
        return impl->synth.getLastPeak();
    }

    SynthEngine& NacarEngine::getSynth() noexcept { return impl->synth; }

    void NacarEngine::setSampleSlot (const SampleSlot* slot) noexcept
    {
        impl->sampleSource.setSlot (slot);
    }

    int NacarEngine::getSampleVoiceCount() const noexcept
    {
        return impl->sampleSource.getActiveVoiceCount();
    }

    int NacarEngine::getLatencySamples() const noexcept
    {
        return impl->latencySamples.load (std::memory_order_relaxed);
    }

    void NacarEngine::rebuildModMatrix (const juce::ValueTree& tree)
    {
        impl->modulation.rebuildModMatrix (tree);
    }
}
