#include "PrintEngine.h"
#include "../Sources/Sample/SampleLoader.h"
#include "../../Plugin/StateManager.h"

#include <cmath>

namespace nacar
{
    PrintEngine::Snapshot PrintEngine::capture (const ParameterRegistry& registry,
                                                const juce::ValueTree& session)
    {
        Snapshot s;

        // userValue and not raw: a print taken while an LFO happens to be at
        // the top of its cycle would otherwise bake that position in as the
        // patch's cutoff.  The render runs its own modulation from the matrix
        // below, so the movement is reproduced rather than frozen.
        for (int i = 0; i < numParameters; ++i)
            s.values[(size_t) i] = registry.userValue ((PID) i);

        if (const auto chain = session.getChildWithName (ids::FXCHAIN); chain.isValid())
            s.fxOrder = chain.getProperty (ids::fxOrder).toString();

        if (const auto matrix = session.getChildWithName (ids::MODMATRIX); matrix.isValid())
            s.modMatrix = matrix.createCopy();

        return s;
    }

    juce::File PrintEngine::generationDirectory()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("NACAR")
                       .getChildFile ("Generations");

        if (! dir.isDirectory())
            dir.createDirectory();

        return dir;
    }

    juce::File PrintEngine::writeToDisk (const SampleBuffer& buffer, const juce::String& baseName)
    {
        if (buffer.isEmpty())
            return {};

        // A generation is referenced by path from the session, and a session
        // outlives the one that made it, so the name has to stay unique
        // without depending on what is in the directory at any moment.
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");

        auto file = generationDirectory()
                        .getChildFile (juce::File::createLegalFileName (baseName + " " + stamp)
                                       + ".wav");

        file = file.getNonexistentSibling();

        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

        if (stream == nullptr)
            return {};

        juce::WavAudioFormat format;

        std::unique_ptr<juce::AudioFormatWriter> writer (
            format.createWriterFor (stream.get(), buffer.sourceRate,
                                    (unsigned int) buffer.numChannels(), 24, {}, 0));

        if (writer == nullptr)
            return {};

        stream.release();       // the writer owns it now

        if (! writer->writeFromAudioSampleBuffer (buffer.audio, 0, buffer.lengthSamples()))
        {
            writer.reset();
            file.deleteFile();
            return {};
        }

        writer.reset();         // flushes and closes before anything reads it
        return file;
    }

    namespace
    {
        /** The same min/max overview the decoder builds, so the viewport draws
            a print exactly as it draws a decoded file. */
        void buildPeaks (SampleBuffer& buffer)
        {
            const int length   = buffer.lengthSamples();
            const int channels = buffer.numChannels();

            if (length <= 0 || channels <= 0)
                return;

            const int bucketSamples =
                juce::jmax (1, (length + SampleLoader::targetPeakBuckets - 1)
                               / SampleLoader::targetPeakBuckets);
            const int numBuckets = (length + bucketSamples - 1) / bucketSamples;

            auto& peaks = buffer.peaks;
            peaks.bucketSamples = bucketSamples;
            peaks.numBuckets    = numBuckets;
            peaks.numChannels   = channels;
            peaks.minimum.assign ((size_t) (numBuckets * channels), 0.0f);
            peaks.maximum.assign ((size_t) (numBuckets * channels), 0.0f);

            for (int b = 0; b < numBuckets; ++b)
            {
                const int from = b * bucketSamples;
                const int to   = juce::jmin (length, from + bucketSamples);

                for (int ch = 0; ch < channels; ++ch)
                {
                    const auto* d = buffer.audio.getReadPointer (ch);

                    float lo = 0.0f, hi = 0.0f;

                    if (to > from)
                    {
                        lo = hi = d[from];

                        for (int i = from + 1; i < to; ++i)
                        {
                            lo = juce::jmin (lo, d[i]);
                            hi = juce::jmax (hi, d[i]);
                        }
                    }

                    peaks.minimum[(size_t) (b * channels + ch)] = lo;
                    peaks.maximum[(size_t) (b * channels + ch)] = hi;
                }
            }
        }
    }

    PrintEngine::Result PrintEngine::render (const Snapshot& snapshot, const Settings& settings)
    {
        Result result;

        const double sr = juce::jlimit (8000.0, 192000.0, settings.sampleRate);
        const int blockSize = juce::jlimit (16, 8192, settings.blockSize);

        // The registry the render reads.  `storage` is a local, and the
        // registry points into it, so both die together at the end of this
        // function and neither can be reached by anything else.
        std::array<std::atomic<float>, numParameters> storage;

        for (int i = 0; i < numParameters; ++i)
            storage[(size_t) i].store (snapshot.values[(size_t) i], std::memory_order_relaxed);

        ParameterRegistry registry;
        registry.attachSnapshot (storage);

        NacarEngine engine;
        engine.prepare (sr, blockSize, 2);
        engine.setFxOrder (FxOrder::fromState (snapshot.fxOrder, ""));
        engine.rebuildModMatrix (snapshot.modMatrix);

        // master_gain is applied by the processor and not by the engine, so a
        // render that stopped at the engine would print a level the player
        // never hears.  This mirrors the processor: the same gain, the same
        // 20 ms ramp.
        juce::dsp::Gain<float> outputGain;
        outputGain.prepare ({ sr, (juce::uint32) blockSize, 2 });
        outputGain.setRampDurationSeconds (0.02);
        outputGain.setGainDecibels (snapshot.values[(size_t) PID::masterGain]);

        const int holdSamples = (int) (sr * juce::jmax (0.05, settings.holdSeconds));
        const int tailCap     = (int) (sr * juce::jmax (0.0, settings.maxTailSeconds));
        const int hardCap     = holdSamples + tailCap;

        const float silenceFloor = juce::Decibels::decibelsToGain (settings.silenceDb, -120.0f);
        const int silenceHold    = (int) (sr * juce::jmax (0.0, settings.silenceHoldSeconds));

        juce::AudioBuffer<float> block (2, blockSize);
        juce::AudioBuffer<float> captured (2, hardCap);
        captured.clear();

        TransportInfo transport;
        transport.bpm = settings.bpm;
        transport.playing = true;       // synced LFOs, Pulse and Rewind need one

        const int note = juce::jlimit (0, 127, settings.midiNote);
        const float velocity = juce::jlimit (0.01f, 1.0f, settings.velocity);

        int position = 0;
        int quietFor = 0;
        bool noteSent = false, releaseSent = false;
        bool sawAnything = false;
        bool allFinite = true;

        while (position < hardCap)
        {
            const int n = juce::jmin (blockSize, hardCap - position);

            block.clear();
            block.setSize (2, n, false, false, true);

            juce::MidiBuffer midi;

            if (! noteSent)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), 0);
                noteSent = true;
            }

            if (! releaseSent && position + n > holdSamples)
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, note),
                               juce::jmax (0, holdSamples - position));
                releaseSent = true;
            }

            engine.process (block, midi, registry, transport);

            juce::dsp::AudioBlock<float> audioBlock (block);
            juce::dsp::ProcessContextReplacing<float> context (audioBlock);
            outputGain.process (context);

            float blockPeak = 0.0f;

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* src = block.getReadPointer (ch);

                for (int i = 0; i < n; ++i)
                {
                    const float x = src[i];

                    if (! std::isfinite (x))
                        allFinite = false;

                    blockPeak = juce::jmax (blockPeak, std::abs (x));
                }

                captured.copyFrom (ch, position, block, ch, 0, n);
            }

            position += n;

            if (blockPeak > silenceFloor)
            {
                sawAnything = true;
                quietFor = 0;
            }
            else
            {
                quietFor += n;

                // Only after the release: a patch with a slow attack is silent
                // at the start and stopping there would print nothing.
                if (releaseSent && sawAnything && quietFor >= silenceHold)
                    break;
            }

            transport.ppqPosition += (double) n / sr * settings.bpm / 60.0;
        }

        if (! allFinite)
        {
            result.failure = "The render produced non-finite audio and was discarded.";
            return result;
        }

        if (! sawAnything)
        {
            // Almost always a patch whose levels are down rather than a bug,
            // and saying which is more useful than a file of silence.
            result.failure = "Nothing was audible to print. Check the output level and that a "
                             "source is switched on.";
            return result;
        }

        // Trim the silence the tail detector ran past, but keep a short fade
        // out so a cut at the floor is not a click.
        const int length = juce::jmax (1, position - juce::jmax (0, quietFor - silenceHold / 2));

        auto buffer = new SampleBuffer();
        buffer->audio.setSize (2, length, false, true, false);

        for (int ch = 0; ch < 2; ++ch)
            buffer->audio.copyFrom (ch, 0, captured, ch, 0, length);

        const int fade = juce::jmin (length, (int) (sr * 0.005));

        if (fade > 1)
            buffer->audio.applyGainRamp (length - fade, fade, 1.0f, 0.0f);

        buffer->sourceRate  = sr;
        buffer->displayName = "PRINT";

        buildPeaks (*buffer);

        // Written here, while the buffer is still this function's private
        // property and before anything can publish it: see Settings::baseName.
        if (settings.baseName.isNotEmpty())
        {
            buffer->file = writeToDisk (*buffer, settings.baseName);

            if (! buffer->file.existsAsFile())
            {
                result.failure = "Could not write the print to "
                                 + generationDirectory().getFullPathName() + ".";
                return result;
            }
        }

        result.audio = buffer;
        result.ok = true;
        return result;
    }
}
