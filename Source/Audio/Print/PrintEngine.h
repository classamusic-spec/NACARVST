#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

#include "../../Plugin/ParameterRegistry.h"
#include "../NacarEngine.h"
#include "../Sources/Sample/SampleBuffer.h"

namespace nacar
{
    /**
        PRINT - rendering the instrument's own output into a sample.

        NACAR's premise is that a sound is given a history, broken apart and
        reconstructed.  Everything after the first step needs audio to work on,
        and until this existed the only audio the instrument could reach was a
        file the user dropped on it: MUTATE on a patch you had just designed
        refused, correctly, with NOTHING TO MUTATE.  PRINT is what turns a patch
        into that audio.

        WHAT IT RENDERS.  One note, held and then released, through the whole
        chain - the synth, Memory, the six FX slots in the user's own order, the
        atmosphere engines and the output stage - so the printed sample is the
        instrument as it is currently heard and not the voice core alone.

        IT IS OF ONE STATE.  A print reads a SNAPSHOT of the session taken when
        the user asked for it, never the live registry.  A knob moved while the
        render is running would otherwise land partway through the file, and the
        result would be a recording of a patch that never existed at any instant.

        THREADING.  `render()` is a worker-thread call and may take seconds.  It
        allocates freely - it is not the audio thread and has no realtime
        contract - but it must never be called FROM the audio thread, and the
        SampleBuffer it returns is only safe to publish once it is complete.
    */
    class PrintEngine
    {
    public:
        /** What to render.  The defaults are a middle C held for two seconds
            with the tail allowed to run, which is a usable print of almost any
            patch; a pad wants longer, a kick wants less. */
        struct Settings
        {
            int    midiNote   = 60;
            float  velocity   = 0.9f;
            double holdSeconds = 2.0;

            /** How long the tail may run after the note is released before the
                render is cut off regardless.  Space decays up to 30 seconds and
                an amp release up to 20, so an uncapped print of an infinite
                reverb would never finish. */
            double maxTailSeconds = 12.0;

            /** Below this, for `silenceHoldSeconds`, the tail is considered
                over and the render stops early.  Most prints end here rather
                than at the cap. */
            float  silenceDb = -80.0f;
            double silenceHoldSeconds = 0.25;

            double sampleRate = 48000.0;
            int    blockSize  = 512;

            /** Host tempo the render runs at, for synced LFOs, Pulse and
                Rewind.  A printed rhythmic patch is only in time if the
                renderer plays a transport, so this one does. */
            double bpm = 120.0;

            /** What the file is called, minus the timestamp and extension.

                A print is written to disk INSIDE the render and the buffer
                carries the path before it is ever published. That ordering is
                not incidental: the session stores a sample by path, and the
                processor's own listener reloads whenever that path changes, so
                a buffer published without its file would be decoded back off
                the disk a moment after it was made.

                Empty means do not write anything, which is what a test wants
                and what nothing else should. */
            juce::String baseName;
        };

        /** A snapshot of everything a render needs, taken on the message
            thread and then owned by the worker.

            Holding the parameters as plain values rather than as a reference to
            the registry is the whole point: see the class comment. */
        struct Snapshot
        {
            std::array<float, numParameters> values {};
            juce::String   fxOrder;          ///< comma-separated slot names
            juce::ValueTree modMatrix;       ///< a copy, not a reference
        };

        /** Captures the live session.  MESSAGE THREAD ONLY - it reads the
            registry's user values and copies the session's own trees. */
        static Snapshot capture (const ParameterRegistry&, const juce::ValueTree& session);

        struct Result
        {
            bool ok = false;
            juce::String failure;        ///< why not, in words for the user
            SampleBuffer::Ptr audio;
        };

        /** Renders the snapshot.  WORKER THREAD.  Never returns a buffer that
            is silent or non-finite: a print the user cannot hear is a failure
            with a reason, not a file. */
        static Result render (const Snapshot&, const Settings&);

        /** Where prints and instruments are written, created on demand.
            Sibling of the user preset directory. */
        static juce::File generationDirectory();

        /** Writes a buffer as a 24-bit WAV at its own rate.  Returns the file,
            or a default-constructed File if it could not be written. */
        static juce::File writeToDisk (const SampleBuffer&, const juce::String& baseName);

    private:
        PrintEngine() = delete;
    };
}
