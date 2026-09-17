#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Presets/PresetManager.h"
#include "../Audio/Print/PrintEngine.h"
#include "../Audio/Print/InstrumentBuilder.h"

namespace nacar
{
    NacarProcessor::NacarProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMETERS", ParameterRegistry::createLayout()),
          stateManager (apvts)
    {
        registry.attach (apvts);

        // Open on the patch the default session names.
        //
        // Without this the header bar says "Niebla en la Ciudad" over the
        // parameter list's raw defaults, which is the instrument telling the
        // user something untrue about itself before they have touched it.
        // The name lives in StateManager so there is one of it; if it ever
        // stops matching a factory preset this returns false and the session
        // is left on the defaults - honestly wrong rather than dishonestly
        // right - and the test suite fails, which is where that should surface.
        PresetManager::applyFactoryPresetByName (registry, stateManager,
                                                 StateManager::defaultPresetName);

        // The sample SOURCE reads whatever the loader has published.  Pointed
        // at the slot here, once: nothing below this line changes it, and the
        // slot is declared before the engine so it outlives it.
        engine.setSampleSlot (&pipeline.slot());

        pipeline.onLoadFinished = [this] (const SampleLoader::Result& result)
        {
            // What the decoder measured, written back where the interface
            // reads it.  On failure these are cleared rather than left at the
            // previous file's figures - the SAMPLE branch must never describe
            // audio the instrument does not hold.
            auto sample = stateManager.group (ids::SAMPLE);

            sample.setProperty (ids::sampleRate,          result.ok ? result.sourceRate : 0.0, nullptr);
            sample.setProperty (ids::sampleLengthSamples, result.ok ? result.lengthSamples : 0, nullptr);
            sample.setProperty (ids::sampleChannels,      result.ok ? result.numChannels : 0, nullptr);
        };

        pipeline.onAnalysisChanged = [this] (const AnalysisResult& result)
        {
            // Including the cleared one that a new file produces: the branch
            // has to stop describing the previous sample the moment the
            // previous sample stops being what is loaded.
            auto branch = stateManager.group (ids::ANALYSIS);
            result.writeTo (branch);

            if (onAnalysisFinished != nullptr)
                onAnalysisFinished();
        };

        pipeline.onPrintFinished = [this] (const PrintEngine::Result& result)
        {
            if (result.ok && result.audio != nullptr)
            {
                // A print is written to disk before it is described, because
                // the SAMPLE branch stores a PATH: the audio is not in the
                // host's blob, so a session that named a print it had not
                // written would restore to silence.
                // Already written, by the render, before it was published.
                const auto file = result.audio->file;

                const int index = recordGeneration (file, result.audio->displayName);

                auto sample = stateManager.group (ids::SAMPLE);

                sample.setProperty (ids::sampleFile,          file.getFullPathName(), nullptr);
                sample.setProperty (ids::sampleDisplayName,
                                    "PRINT " + juce::String (index + 1), nullptr);
                sample.setProperty (ids::sampleRate,          result.audio->sourceRate, nullptr);
                sample.setProperty (ids::sampleLengthSamples, result.audio->lengthSamples(), nullptr);
                sample.setProperty (ids::sampleChannels,      result.audio->numChannels(), nullptr);
            }

            if (onPrintFinished != nullptr)
                onPrintFinished (result);
        };

        pipeline.onMutationFinished = [this] (const mutation::Result& result)
        {
            // A successful mutation is already in the slot by now, so the
            // SAMPLE branch is describing the wrong audio until this runs.
            // The file path is deliberately left alone: what is playing is no
            // longer that file, and claiming otherwise in a saved session
            // would restore the original and call it the mutation.
            if (result.ok && result.audio != nullptr)
            {
                auto sample = stateManager.group (ids::SAMPLE);

                sample.setProperty (ids::sampleRate,          result.audio->sourceRate, nullptr);
                sample.setProperty (ids::sampleLengthSamples, result.audio->audio.getNumSamples(), nullptr);
                sample.setProperty (ids::sampleChannels,      result.audio->audio.getNumChannels(), nullptr);
            }

            if (onMutationFinished != nullptr)
                onMutationFinished (result);
        };

        // The chain order lives in the session tree and can change from the
        // editor, from a preset load or from a host restore, so the processor
        // watches the whole session rather than one child: StateManager's
        // restore replaces children wholesale, and a listener attached to a
        // child would be left holding a detached tree.
        stateManager.session().addListener (this);
        publishFxOrder();
        publishModMatrix();

        // 20 Hz is fast enough that a decode feels immediate and slow enough
        // that it costs nothing.  See timerCallback(): this is the processor's
        // clock, not the editor's, on purpose.
        startTimerHz (20);
    }

    NacarProcessor::~NacarProcessor()
    {
        stopTimer();
        cancelPendingUpdate();
        stateManager.session().removeListener (this);
    }

    void NacarProcessor::timerCallback()
    {
        pipeline.poll();
    }

    void NacarProcessor::handleAsyncUpdate()
    {
        const int latency = engine.getLatencySamples();

        if (latency != getLatencySamples())
            setLatencySamples (latency);
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

    void NacarProcessor::publishModMatrix()
    {
        engine.rebuildModMatrix (stateManager.session().getChildWithName (ids::MODMATRIX));
    }

    // -----------------------------------------------------------------------
    //  Sample loading
    // -----------------------------------------------------------------------
    void NacarProcessor::startSampleLoad (const juce::File& file)
    {
        // An empty path is how the interface says "there is no sample", and
        // the pipeline treats it as one: it empties the slot and clears the
        // analysis rather than leaving either describing a sound that has
        // gone.
        pipeline.load (file);
    }

    // -----------------------------------------------------------------------
    //  Mutation
    // -----------------------------------------------------------------------
    bool NacarProcessor::requestMutation (const mutation::Recipe& recipe, juce::String& failure)
    {
        // The harmony context is assembled here because it is the only place
        // that has both halves of it: what the analyser found, and what the
        // user asked for on the SCALE control.  Choice 0 is AUTO, so a forced
        // scale is the index minus one and AUTO arrives as -1 - which
        // Context::from reads as "use the detected one, if there is one".
        const auto context = harmony::Context::from (pipeline.analysis(),
                                                     recipe.harmonyMode,
                                                     registry.choice (PID::scaleType) - 1);

        return pipeline.mutate (recipe, context, failure);
    }

    // -----------------------------------------------------------------------
    //  Print and generations
    // -----------------------------------------------------------------------
    bool NacarProcessor::requestPrint (juce::String& failure)
    {
        // Captured HERE, on the message thread, and not inside the render: a
        // print is of one state, and a knob moved while the worker runs must
        // not land halfway through the file.
        const auto snapshot = PrintEngine::capture (registry, stateManager.session());

        PrintEngine::Settings settings;
        settings.sampleRate = currentSampleRate;
        settings.blockSize  = currentBlockSize;
        settings.bpm        = hostBpm.load (std::memory_order_relaxed);

        // The note the analyser will later call this sample's root, so a print
        // played back by MAKE INSTRUMENT sits at the pitch it was rendered at.
        settings.midiNote = 60;

        const auto name = stateManager.group (ids::PRESET)
                              .getProperty (ids::presetName).toString();

        settings.baseName = name.isNotEmpty() ? name : juce::String ("PRINT");

        return pipeline.print (snapshot, settings, failure);
    }

    int NacarProcessor::recordGeneration (const juce::File& file, const juce::String& displayName)
    {
        return InstrumentBuilder::recordGeneration (stateManager, file, displayName);
    }

    // -----------------------------------------------------------------------
    //  Make instrument
    // -----------------------------------------------------------------------
    bool NacarProcessor::makeInstrument (juce::String& failure)
    {
        auto source = pipeline.slot().acquire();

        if (source == nullptr || source->isEmpty())
        {
            failure = "NOTHING TO MAKE AN INSTRUMENT FROM";
            return false;
        }

        if (pipeline.isMutating() || pipeline.isPrinting() || pipeline.isLoading())
        {
            failure = "STILL WORKING";
            return false;
        }

        const auto result = InstrumentBuilder::build (*source, pipeline.analysis(), registry,
                                                      stateManager,
                                                      stateManager.group (ids::PRESET)
                                                          .getProperty (ids::presetName).toString());

        if (! result.ok)
        {
            failure = result.failure;
            return false;
        }

        if (onInstrumentMade != nullptr)
            onInstrumentMade (result.name);

        return true;
    }

    void NacarProcessor::valueTreePropertyChanged (juce::ValueTree& tree,
                                                   const juce::Identifier& property)
    {
        // The viewport records a dropped file's path and stops there; this is
        // where the path becomes audio.  Only `sampleFile` is watched, so the
        // rate, length and channel count the decoder writes back below do not
        // start a second decode.
        if (tree.hasType (ids::SAMPLE) && property == ids::sampleFile)
        {
            const juce::File named (tree.getProperty (ids::sampleFile).toString());

            // A print and a MAKE INSTRUMENT both write the file first and then
            // name it here, so by the time this fires the slot is already
            // holding exactly that audio. Decoding it back off the disk would
            // be correct and completely wasteful - and it would empty the slot
            // for as long as the decode took, which the viewport would show.
            const auto held = pipeline.slot().acquire();
            const bool alreadyLoaded = held != nullptr && ! held->isEmpty()
                                       && held->file == named && named.existsAsFile();

            if (! alreadyLoaded)
                startSampleLoad (named);
        }

        if (tree.hasType (ids::FXCHAIN)
            && (property == ids::fxOrder || property.toString() == "fxBypass"))
            publishFxOrder();

        if (tree.hasType (ids::MODSLOT) || tree.hasType (ids::MODMATRIX))
            publishModMatrix();
    }

    void NacarProcessor::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)
    {
        publishFxOrder();
        publishModMatrix();
    }

    void NacarProcessor::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)
    {
        publishFxOrder();
        publishModMatrix();
    }
    void NacarProcessor::valueTreeChildOrderChanged (juce::ValueTree&, int, int)        {}
    void NacarProcessor::valueTreeParentChanged (juce::ValueTree&)                      {}
    void NacarProcessor::valueTreeRedirected (juce::ValueTree&)
    {
        publishFxOrder();
        publishModMatrix();
    }

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

        // The chain's latency changes when a module is switched on or off.
        // Noticing it here is lock-free; reporting it is not, so that happens
        // on the message thread.
        const int latency = engine.getLatencySamples();

        if (latency != reportedLatency.exchange (latency, std::memory_order_relaxed))
            triggerAsyncUpdate();
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
        // attached, so both are republished explicitly here too.
        publishFxOrder();
        publishModMatrix();

        // A session that names a sample has to decode it again: the audio is
        // not in the host's blob, only the path is.  Asynchronously, like any
        // other load, so restoring a project does not stall the host.
        startSampleLoad (juce::File (stateManager.group (ids::SAMPLE)
                                         .getProperty (ids::sampleFile).toString()));
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
