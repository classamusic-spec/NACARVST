#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace nacar
{
    NacarProcessor::NacarProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMETERS", ParameterRegistry::createLayout()),
          stateManager (apvts)
    {
        registry.attach (apvts);

        // The chain order lives in the session tree and can change from the
        // editor, from a preset load or from a host restore, so the processor
        // watches the whole session rather than one child: StateManager's
        // restore replaces children wholesale, and a listener attached to a
        // child would be left holding a detached tree.
        stateManager.session().addListener (this);
        publishFxOrder();
    }

    NacarProcessor::~NacarProcessor()
    {
        stateManager.session().removeListener (this);
    }

    // -----------------------------------------------------------------------
    //  FX chain order
    // -----------------------------------------------------------------------
    void NacarProcessor::publishFxOrder()
    {
        const auto chain = stateManager.session().getChildWithName (ids::FXCHAIN);

        if (! chain.isValid())
        {
            engine.setFxOrder (FxOrder::defaultOrder());
            return;
        }

        // `fxBypass` is written by the FX chain view under an identifier of its
        // own because StateManager has none for it.  An unrecognised property
        // round-trips through the session tree unchanged, so reading it back by
        // name here is safe.
        engine.setFxOrder (FxOrder::fromState (chain.getProperty (ids::fxOrder).toString(),
                                               chain.getProperty ("fxBypass").toString()));
    }

    void NacarProcessor::valueTreePropertyChanged (juce::ValueTree& tree,
                                                   const juce::Identifier& property)
    {
        if (tree.hasType (ids::FXCHAIN)
            && (property == ids::fxOrder || property.toString() == "fxBypass"))
            publishFxOrder();
    }

    void NacarProcessor::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)       { publishFxOrder(); }
    void NacarProcessor::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) { publishFxOrder(); }
    void NacarProcessor::valueTreeChildOrderChanged (juce::ValueTree&, int, int)        {}
    void NacarProcessor::valueTreeParentChanged (juce::ValueTree&)                      {}
    void NacarProcessor::valueTreeRedirected (juce::ValueTree&)                         { publishFxOrder(); }

    // -----------------------------------------------------------------------
    //  Lifecycle
    // -----------------------------------------------------------------------
    void NacarProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
    {
        currentSampleRate = sampleRate;
        currentBlockSize  = maximumExpectedSamplesPerBlock;

        const juce::dsp::ProcessSpec spec {
            sampleRate,
            (juce::uint32) juce::jmax (1, maximumExpectedSamplesPerBlock),
            (juce::uint32) juce::jmax (1, getTotalNumOutputChannels())
        };

        engine.prepare (sampleRate, (int) spec.maximumBlockSize, (int) spec.numChannels);

        outputGain.prepare (spec);
        outputGain.setRampDurationSeconds (0.02);

        for (auto& m : meter)
            m.store (0.0f, std::memory_order_relaxed);

        // Keep the scope's time window roughly constant across sample rates:
        // 2048 frames should span about three quarters of a second.
        scopeDecimation = juce::jmax (1, (int) std::round (sampleRate * 0.75 / (double) scopeSize));
        scopeCounter = 0;
        scopePeakL = scopePeakR = 0.0f;
        scope.fill (0.0f);
        scopeWrite.store (0, std::memory_order_relaxed);
    }

    void NacarProcessor::releaseResources()
    {
        engine.reset();
    }

    bool NacarProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();

        // NACAR is a stereo instrument.  Mono output is accepted so that hosts
        // which probe it do not reject the plugin outright.
        return out == juce::AudioChannelSet::stereo()
            || out == juce::AudioChannelSet::mono();
    }

    double NacarProcessor::getTailLengthSeconds() const
    {
        // Worst case is the Space decay plus the Aura tail plus the amp release.
        return 32.0;
    }

    // -----------------------------------------------------------------------
    //  Audio
    // -----------------------------------------------------------------------
    void NacarProcessor::pullTransportInfo()
    {
        transport = TransportInfo();
        transport.bpm = hostBpm.load (std::memory_order_relaxed);

        if (auto* ph = getPlayHead())
        {
            if (const auto pos = ph->getPosition())
            {
                if (const auto bpm = pos->getBpm())
                {
                    transport.bpm = *bpm;
                    hostBpm.store (*bpm, std::memory_order_relaxed);
                }

                if (const auto ppq = pos->getPpqPosition())
                    transport.ppqPosition = *ppq;

                transport.playing = pos->getIsPlaying();
            }
        }
    }

    void NacarProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;

        const int numSamples = buffer.getNumSamples();
        const int numOut     = getTotalNumOutputChannels();

        for (int ch = getTotalNumInputChannels(); ch < numOut; ++ch)
            buffer.clear (ch, 0, numSamples);

        buffer.clear();

        pullTransportInfo();

        // On-screen keyboard and any host MIDI are merged before the engines
        // see them, so a note played from the UI is indistinguishable from one
        // played by the host.
        keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

        engine.process (buffer, midi, registry, transport);

        // -- output stage ---------------------------------------------------
        outputGain.setGainDecibels (registry.raw (PID::masterGain));

        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> context (block);
        outputGain.process (context);

        // Final safety limiter: NACAR never sends NaNs or anything past 0 dBFS
        // into a host, whatever a mutation recipe asked for.
        for (int ch = 0; ch < numOut; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                const float x = d[i];
                d[i] = std::isfinite (x) ? juce::jlimit (-1.0f, 1.0f, x) : 0.0f;
            }
        }

        updateMeters (buffer);
        pushScope (buffer);
    }

    void NacarProcessor::pushScope (const juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        const int numCh = buffer.getNumChannels();

        if (numSamples <= 0 || numCh <= 0)
            return;

        const auto* l = buffer.getReadPointer (0);
        const auto* r = buffer.getReadPointer (numCh > 1 ? 1 : 0);

        int w = scopeWrite.load (std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            // Peak rather than decimate-by-dropping: a scope that skips samples
            // misses transients, which is exactly what a producer looks at.
            scopePeakL = juce::jmax (scopePeakL, std::abs (l[i]));
            scopePeakR = juce::jmax (scopePeakR, std::abs (r[i]));

            if (++scopeCounter >= scopeDecimation)
            {
                scopeCounter = 0;

                scope[(size_t) (w * 2)]     = scopePeakL;
                scope[(size_t) (w * 2 + 1)] = scopePeakR;

                scopePeakL = scopePeakR = 0.0f;
                w = (w + 1) % scopeSize;
            }
        }

        scopeWrite.store (w, std::memory_order_release);
    }

    int NacarProcessor::readScope (float* dest, int maxFrames) const noexcept
    {
        if (dest == nullptr || maxFrames <= 0)
            return 0;

        const int count = juce::jmin (maxFrames, scopeSize);
        const int w = scopeWrite.load (std::memory_order_acquire);

        // Walk backwards from the write head so the newest frame lands last.
        int read = (w - count + scopeSize) % scopeSize;

        for (int i = 0; i < count; ++i)
        {
            dest[i * 2]     = scope[(size_t) (read * 2)];
            dest[i * 2 + 1] = scope[(size_t) (read * 2 + 1)];
            read = (read + 1) % scopeSize;
        }

        return count;
    }

    int NacarProcessor::getActiveVoiceCount() const noexcept
    {
        return engine.getActiveVoiceCount();
    }

    void NacarProcessor::updateMeters (const juce::AudioBuffer<float>& buffer)
    {
        // Peak with an asymmetric ballistic: instant attack, slow release, so
        // the meter reads like hardware rather than like a debug print.
        const int numSamples = buffer.getNumSamples();
        if (numSamples <= 0)
            return;

        const float releaseCoeff =
            std::exp (-1.0f / (float) (currentSampleRate * 0.25) * (float) numSamples);

        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        {
            const float peak = buffer.getMagnitude (ch, 0, numSamples);
            const float prev = meter[ch].load (std::memory_order_relaxed);

            meter[ch].store (juce::jmax (peak, prev * releaseCoeff),
                             std::memory_order_relaxed);
        }

        if (buffer.getNumChannels() == 1)
            meter[1].store (meter[0].load (std::memory_order_relaxed), std::memory_order_relaxed);
    }

    float NacarProcessor::getMeterLevel (int channel) const noexcept
    {
        return meter[juce::jlimit (0, 1, channel)].load (std::memory_order_relaxed);
    }

    // -----------------------------------------------------------------------
    //  State
    // -----------------------------------------------------------------------
    void NacarProcessor::getStateInformation (juce::MemoryBlock& destination)
    {
        stateManager.writeTo (destination);
    }

    void NacarProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        stateManager.readFrom (data, sizeInBytes);

        // The restore rewrites the session tree in place, which the listener
        // above sees - but a host may also restore before the listener is
        // attached, so the order is republished explicitly here too.
        publishFxOrder();
    }

    juce::AudioProcessorEditor* NacarProcessor::createEditor()
    {
        return new NacarEditor (*this);
    }
}

// ---------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new nacar::NacarProcessor();
}
