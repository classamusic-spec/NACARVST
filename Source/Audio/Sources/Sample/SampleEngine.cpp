#include "SampleEngine.h"

#include "../../DspCommon.h"

#include <array>
#include <atomic>
#include <cmath>

namespace nacar
{
    namespace
    {
        /** Note-off fade.  There is no amplitude envelope: `sample_*` reserves
            none, and borrowing the synth's would put one control in charge of
            two sources.  Ten milliseconds is long enough that a gated note does
            not click and short enough that it is not heard as a release. */
        constexpr float kReleaseSeconds = 0.010f;

        /** Recovery from a hard stop - a stolen voice, a sample replaced under
            a sounding note, the end of a one-shot.  The voice's last output is
            decayed to nothing rather than dropped in one sample.  This is a
            declick, not an envelope: it invents no sample data, it only stops
            the one that was there from ending on a step. */
        constexpr float kTailSeconds = 0.004f;

        /** How long the engine takes to fade in or out when `source_mode`
            moves on or off SAMPLE. */
        constexpr float kGateSeconds = 0.015f;

        constexpr float kTailFloor = 1.0e-6f;

        /** Hermite read of one channel at a fractional index.

            The same four-point interpolator the synth's wavetables and every
            fractional delay in the instrument use (`DspCommon.h`), so a sample
            transposed by a semitone and an oscillator transposed by a semitone
            are interpolated by the same arithmetic.

            At an integral index the fraction is zero and Hermite returns y0
            exactly - which is what makes playback at unity rate bit-identical
            to the file rather than approximately equal to it.

            Indices past either end clamp to the first or last sample.  The
            alternative, wrapping, would fold the tail of a sample into its
            own attack at every loop seam. */
        forcedinline float readAt (const float* d, int len, double index) noexcept
        {
            const double floored = std::floor (index);
            const int    i = (int) floored;
            const float  f = (float) (index - floored);

            const int im1 = juce::jlimit (0, len - 1, i - 1);
            const int i0  = juce::jlimit (0, len - 1, i);
            const int i1  = juce::jlimit (0, len - 1, i + 1);
            const int i2  = juce::jlimit (0, len - 1, i + 2);

            return fx::hermite (f, d[im1], d[i0], d[i1], d[i2]);
        }

        /**
            THE PLAYBACK COORDINATE.

            Everything below works in `u`, which counts source samples from the
            start point and always increases, whichever direction the audio is
            being read in:

                forward   index = start + u
                reverse   index = end   - u

            Reverse is therefore not a second code path with its own loop
            handling and its own crossfade to get wrong - it is one line in
            `indexFor`.  At unity rate and integral u the reverse read hits
            exactly the same samples as the forward read, in the opposite
            order, which is what "reverse is exact" has to mean.

            The loop bounds are mapped into u at the same time, which is why
            they are stored here rather than as raw parameters: in reverse, the
            loop's END point is the one the playhead reaches first.
        */
        struct Region
        {
            double start = 0.0;      ///< source index of the start point
            double end   = 0.0;      ///< source index of the end point
            double length = 0.0;     ///< end - start, in source samples
            bool   reverse = false;

            bool   loop   = false;
            double loopLo = 0.0;     ///< in u
            double loopHi = 0.0;     ///< in u
            double xfade  = 0.0;     ///< crossfade length in samples
            double period = 0.0;     ///< (loopHi - loopLo) - xfade

            forcedinline double indexFor (double u) const noexcept
            {
                return reverse ? (end - u) : (start + u);
            }
        };

        /**
            Turns the seven region parameters into a Region for this block.

            THE CROSSFADE.  A loop that jumps from `loopHi` back to `loopLo`
            steps by whatever the difference between those two samples happens
            to be, and on anything but a sample cut at a zero crossing that step
            is a click on every repetition.  The fix is not to fade the output
            down and up - that is a hole, audible as a pulse at the loop rate -
            but to have the signal already be the destination by the time it
            arrives:

                for the last X samples before loopHi, mix the read at u
                against the read at loopLo + (u - (loopHi - X)), crossfading
                from the first to the second; then wrap by (loopHi - loopLo - X)

            At the start of the fade the mix is entirely the outgoing read, so
            it joins what came before with no step.  At the end it is entirely
            the incoming read at loopLo + X, which is exactly where the wrap
            puts the playhead - so the seam is continuous by construction
            rather than by luck.  The loop's period is X shorter than the
            distance between the two markers, which is the price of the method
            and the reason X is clamped to half the loop.
        */
        Region makeRegion (const ParameterRegistry& p, int len) noexcept
        {
            Region r;

            if (len < 2)
                return r;

            const double last = (double) (len - 1);

            double s = (double) juce::jlimit (0.0f, 1.0f, p.raw (PID::sampleStart)) * last;
            double e = (double) juce::jlimit (0.0f, 1.0f, p.raw (PID::sampleEnd))   * last;

            // A user who drags End below Start means the region between them,
            // not an empty one.
            if (e < s)
                std::swap (s, e);

            r.start   = s;
            r.end     = e;
            r.length  = e - s;
            r.reverse = p.flag (PID::sampleReverse);

            double la = (double) juce::jlimit (0.0f, 1.0f, p.raw (PID::sampleLoopStart)) * last;
            double lb = (double) juce::jlimit (0.0f, 1.0f, p.raw (PID::sampleLoopEnd))   * last;

            if (lb < la)
                std::swap (la, lb);

            // The loop cannot leave the region: a playhead outside [start, end]
            // is outside what the user asked to hear.
            la = juce::jlimit (s, e, la);
            lb = juce::jlimit (s, e, lb);

            r.loopLo = r.reverse ? (e - lb) : (la - s);
            r.loopHi = r.reverse ? (e - la) : (lb - s);

            const double loopLength = r.loopHi - r.loopLo;

            r.loop = p.flag (PID::sampleLoop) && loopLength >= 2.0;

            if (r.loop)
            {
                const double maximum = loopLength * 0.5;

                r.xfade = juce::jlimit (0.0, maximum,
                                        (double) juce::jlimit (0.0f, 1.0f, p.raw (PID::sampleCrossfade))
                                            * maximum);

                r.period = loopLength - r.xfade;

                // A loop whose period has been crossfaded away is not a loop.
                if (r.period < 1.0)
                    r.loop = false;
            }

            return r;
        }

        /**
            One sounding note.

            `tail` is the declick described above `kTailSeconds`: a decaying
            copy of the last value the voice emitted, added to whatever the
            voice does next.  It is separate from the playback path on purpose -
            a stolen voice restarts immediately and its tail decays underneath
            the new note rather than being lost with it.
        */
        struct Voice
        {
            bool   active = false;
            bool   releasing = false;
            int    note = 60;
            int    order = 0;
            float  velocity = 1.0f;

            double u = 0.0;
            double ratio = 1.0;

            float releaseGain = 1.0f;
            float lastL = 0.0f, lastR = 0.0f;
            float tailL = 0.0f, tailR = 0.0f;

            bool sounding() const noexcept
            {
                return active || std::abs (tailL) > kTailFloor || std::abs (tailR) > kTailFloor;
            }

            /** Level for the stealing heuristic: how loud this voice is now. */
            float loudness() const noexcept
            {
                return active ? velocity * releaseGain : 0.0f;
            }

            void arm() noexcept
            {
                tailL += lastL;
                tailR += lastR;
                lastL = lastR = 0.0f;
            }

            void stop() noexcept
            {
                if (active)
                    arm();

                active = false;
                releasing = false;
                releaseGain = 1.0f;
            }

            void hardClear() noexcept
            {
                active = releasing = false;
                releaseGain = 1.0f;
                u = 0.0;
                lastL = lastR = tailL = tailR = 0.0f;
            }

            void begin (int n, float v, int newOrder) noexcept
            {
                if (active)
                    arm();

                note = n;
                velocity = v;
                order = newOrder;
                u = 0.0;
                active = true;
                releasing = false;
                releaseGain = 1.0f;
                lastL = lastR = 0.0f;
            }
        };
    }

    // =======================================================================
    struct SampleEngine::Impl
    {
        bool prepared = false;
        EngineSpec spec;

        const SampleSlot* slot = nullptr;

        std::array<Voice, (size_t) SampleEngine::maxVoices> voices {};
        int noteCounter = 0;

        /** Identity of the buffer the voices' positions refer to.  Never
            dereferenced once stale - only compared, so that a sample replaced
            under a sounding note stops that note instead of indexing into a
            buffer of a different length. */
        const SampleBuffer* lastSample = nullptr;
        int lastLength = 0;

        float gateGain = 0.0f;
        float lastGain = 0.0f;
        bool  gainPrimed = false;

        float tailCoeff = 0.0f;
        float releaseStep = 1.0f;

        std::atomic<int>  activeVoices { 0 };
        std::atomic<bool> loaded { false };

        // -------------------------------------------------------------------
        void prepare (const EngineSpec& s)
        {
            spec = s;
            spec.sampleRate = juce::jmax (1.0, s.sampleRate);
            spec.maxBlockSize = juce::jmax (1, s.maxBlockSize);

            tailCoeff   = std::exp (-1.0f / (kTailSeconds * (float) spec.sampleRate));
            releaseStep = 1.0f / juce::jmax (1.0f, kReleaseSeconds * (float) spec.sampleRate);

            reset();
            prepared = true;
        }

        void reset()
        {
            for (auto& v : voices)
                v.hardClear();

            noteCounter = 0;
            lastSample = nullptr;
            lastLength = 0;
            gateGain = 0.0f;
            lastGain = 0.0f;
            gainPrimed = false;

            activeVoices.store (0, std::memory_order_relaxed);
            loaded.store (false, std::memory_order_relaxed);
        }

        // -- voice allocation ------------------------------------------------
        int limitFor (const ParameterRegistry& p) const noexcept
        {
            return juce::jlimit (1, SampleEngine::maxVoices, (int) std::lround (p.raw (PID::polyphony)));
        }

        int findVoice (int limit) noexcept
        {
            // Free first.  A voice still decaying its declick tail counts as
            // busy: reusing it would swallow the tail it exists to produce.
            for (int i = 0; i < limit; ++i)
                if (! voices[(size_t) i].sounding())
                    return i;

            // Then the quietest voice already on its way out.
            int best = -1;
            float quietest = 1.0e9f;

            for (int i = 0; i < limit; ++i)
            {
                const auto& v = voices[(size_t) i];

                if (v.releasing && v.loudness() < quietest)
                {
                    quietest = v.loudness();
                    best = i;
                }
            }

            if (best >= 0)
                return best;

            // Then the oldest.  Stealing the note somebody has just played is
            // the most audible mistake an allocator can make.
            int oldest = 0;

            for (int i = 1; i < limit; ++i)
                if (voices[(size_t) i].order < voices[(size_t) oldest].order)
                    oldest = i;

            return oldest;
        }

        void noteOn (int note, float velocity, const ParameterRegistry& p) noexcept
        {
            const int index = findVoice (limitFor (p));
            auto& v = voices[(size_t) index];

            v.begin (note, velocity, ++noteCounter);

            // Here, not at the top of the next block: a note that started
            // mid-block with the previous note's ratio would read the wrong
            // pitch for the rest of it.
            v.ratio = pitch.ratioFor (note);
        }

        void noteOff (int note) noexcept
        {
            for (auto& v : voices)
                if (v.active && ! v.releasing && v.note == note)
                    v.releasing = true;
        }

        void allNotesOff() noexcept
        {
            for (auto& v : voices)
                if (v.active)
                    v.releasing = true;
        }

        void handleMidi (const juce::MidiMessage& m, const ParameterRegistry& p) noexcept
        {
            if (m.isNoteOn())
                noteOn (m.getNoteNumber(), m.getFloatVelocity(), p);
            else if (m.isNoteOff())
                noteOff (m.getNoteNumber());
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                allNotesOff();
        }

        // -- pitch -----------------------------------------------------------
        /**
            How fast a voice walks the sample, resolved once per block.

            Two things multiply.  The first is the rate conversion: a 44.1 kHz
            file played in a 48 kHz session has to be read at 0.919 samples per
            output sample or it comes out sharp, and nothing else in the
            instrument knows the file's rate.  The second is the musical
            transposition.

            Key tracking is a fraction of the interval from the root, not a
            switch: at 0 every key plays the sample at its own pitch, at 1 the
            keyboard is a keyboard, and in between the sample detunes less than
            the note it was played at.  Tune is added afterwards so that it
            always means the same number of semitones, wherever key tracking
            sits.
        */
        struct PitchContext
        {
            double rateRatio = 1.0;
            double keyTrack  = 1.0;
            double tune      = 0.0;
            double root      = 60.0;

            /** std::exp2 rather than the engine's fast approximation: this runs
                once per voice per block, not per sample, and a few cents of
                error in a sampler's pitch is audible against the synth playing
                the same note. */
            double ratioFor (int note) const noexcept
            {
                const double semitones = keyTrack * ((double) note - root) + tune;

                return juce::jlimit (1.0e-4, 256.0, rateRatio * std::exp2 (semitones / 12.0));
            }
        };

        PitchContext pitch;

        void updatePitch (const ParameterRegistry& p, double sourceRate) noexcept
        {
            pitch.rateRatio = sourceRate / spec.sampleRate;
            pitch.keyTrack  = (double) juce::jlimit (0.0f, 1.0f, p.raw (PID::sampleKeyTrack));
            pitch.tune      = (double) juce::jlimit (-24.0f, 24.0f, p.raw (PID::sampleTune));
            pitch.root      = (double) juce::jlimit (0.0f, 127.0f, p.raw (PID::sampleRootNote));

            for (auto& v : voices)
                if (v.active)
                    v.ratio = pitch.ratioFor (v.note);
        }

        // -- rendering -------------------------------------------------------

        void renderVoice (Voice& v, const SampleBuffer& s, const Region& region,
                          float* l, float* r, int numSamples,
                          float gain, float gainStep) noexcept
        {
            const int len = s.lengthSamples();
            const int channels = s.numChannels();

            const auto* d0 = s.audio.getReadPointer (0);
            const auto* d1 = channels > 1 ? s.audio.getReadPointer (1) : d0;

            float g = gain;

            for (int i = 0; i < numSamples; ++i, g += gainStep)
            {
                if (v.active)
                {
                    if (region.loop && v.u >= region.loopHi)
                    {
                        v.u -= region.period;

                        // One subtraction is the whole story at any sane rate.
                        // The second branch exists because the loop markers can
                        // move under a sounding note: shrink the loop while the
                        // playhead is past its new end and a `while` here would
                        // spin for as many iterations as the sample is long,
                        // inside one sample of one block.
                        if (v.u >= region.loopHi)
                        {
                            const double over = v.u - region.loopHi;
                            v.u = region.loopHi - region.period
                                + std::fmod (over, region.period);
                        }
                    }
                    else if (! region.loop && v.u > region.length)
                    {
                        // The end point, reached.  A one-shot that stops on a
                        // non-zero sample steps to silence, so it leaves a tail
                        // behind it rather than a corner.
                        v.stop();
                    }
                }

                float outL = 0.0f, outR = 0.0f;

                if (v.active)
                {
                    const double index = region.indexFor (v.u);

                    float a0 = readAt (d0, len, index);
                    float a1 = channels > 1 ? readAt (d1, len, index) : a0;

                    if (region.loop && region.xfade > 0.0
                        && v.u >= region.loopHi - region.xfade)
                    {
                        const double into = v.u - (region.loopHi - region.xfade);
                        const float  t = (float) juce::jlimit (0.0, 1.0, into / region.xfade);

                        const double other = region.indexFor (region.loopLo + into);

                        const float b0 = readAt (d0, len, other);
                        const float b1 = channels > 1 ? readAt (d1, len, other) : b0;

                        a0 += (b0 - a0) * t;
                        a1 += (b1 - a1) * t;
                    }

                    const float amp = v.velocity * v.releaseGain;

                    outL = a0 * amp;
                    outR = a1 * amp;

                    v.lastL = outL;
                    v.lastR = outR;

                    v.u += v.ratio;

                    if (v.releasing)
                    {
                        v.releaseGain -= releaseStep;

                        if (v.releaseGain <= 0.0f)
                        {
                            // Already silent, so no tail is needed and arming
                            // one would add a click where the fade removed it.
                            v.releaseGain = 0.0f;
                            v.active = false;
                            v.releasing = false;
                            v.lastL = v.lastR = 0.0f;
                        }
                    }
                }

                if (std::abs (v.tailL) > kTailFloor || std::abs (v.tailR) > kTailFloor)
                {
                    v.tailL *= tailCoeff;
                    v.tailR *= tailCoeff;

                    if (std::abs (v.tailL) < kTailFloor) v.tailL = 0.0f;
                    if (std::abs (v.tailR) < kTailFloor) v.tailR = 0.0f;

                    outL += v.tailL;
                    outR += v.tailR;
                }

                l[i] += outL * g;

                if (r != nullptr)
                    r[i] += outR * g;
            }
        }

        void renderSpan (juce::AudioBuffer<float>& buffer, int start, int numSamples,
                         const SampleBuffer* s, const Region& region,
                         float gain, float gainStep) noexcept
        {
            if (numSamples <= 0 || s == nullptr)
                return;

            auto* l = buffer.getWritePointer (0, start);
            auto* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : nullptr;

            for (auto& v : voices)
                if (v.sounding())
                    renderVoice (v, *s, region, l, r, numSamples, gain, gainStep);
        }

        // -------------------------------------------------------------------
        void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                      const ParameterRegistry& p, const MacroState& macros)
        {
            juce::ignoreUnused (macros);

            if (! prepared || buffer.getNumChannels() < 1)
                return;

            const int total = buffer.getNumSamples();

            if (total <= 0)
                return;

            // One acquire per block, before anything else.  See SampleBuffer.h:
            // this hands the audio thread a reference the slot also holds, so
            // releasing it at the end of this function can never be the release
            // that frees it.
            const SampleBuffer::Ptr sample = slot != nullptr ? slot->acquire()
                                                             : SampleBuffer::Ptr();

            const auto* s = sample.get();
            const int   len = s != nullptr ? s->lengthSamples() : 0;

            const bool usable = s != nullptr && len >= 2 && s->numChannels() >= 1;

            if (s != lastSample || len != lastLength)
            {
                // A different sample under a sounding note: the voices' read
                // positions mean nothing in the new buffer, so they stop, and
                // stop with a tail rather than a step.
                for (auto& v : voices)
                    v.stop();

                lastSample = s;
                lastLength = len;
            }

            loaded.store (usable, std::memory_order_relaxed);

            // -- the gate ----------------------------------------------------
            //
            // `source_mode` says which engine makes the sound.  The engine is
            // still called every block whatever it says - that is the chain's
            // rule and this is a source, so there is no history to starve - but
            // it contributes nothing unless SAMPLE is selected, and it arrives
            // and leaves on a ramp rather than a step.
            const bool selected = p.choice (PID::sourceMode) == 1;
            const float gateTarget = selected ? 1.0f : 0.0f;

            const float maxStep = (float) ((double) total / (kGateSeconds * spec.sampleRate));

            if (! gainPrimed)
                gateGain = gateTarget;      // first block: jump, do not fade in

            const float gateEnd = gateGain
                                + juce::jlimit (-maxStep, maxStep, gateTarget - gateGain);

            const float sampleGain = juce::Decibels::decibelsToGain (
                juce::jlimit (-24.0f, 24.0f, p.raw (PID::sampleGain)));

            const float gainEnd   = sampleGain * gateEnd;
            const float gainStart = gainPrimed ? lastGain : gainEnd;

            gateGain = gateEnd;
            lastGain = gainEnd;
            gainPrimed = true;

            const float gainStep = (gainEnd - gainStart) / (float) total;

            // Nothing to hear and nothing to keep warm.  The voices are cleared
            // outright rather than released: the gate is already at zero, so
            // there is nothing for a fade to hide.
            if (! usable || (gainStart <= 0.0f && gainEnd <= 0.0f))
            {
                for (auto& v : voices)
                    v.hardClear();

                activeVoices.store (0, std::memory_order_relaxed);
                return;
            }

            const Region region = makeRegion (p, len);

            updatePitch (p, s->sourceRate);

            // -- sample-accurate MIDI ----------------------------------------
            int position = 0;

            for (const auto metadata : midi)
            {
                const int eventTime = juce::jlimit (0, total, metadata.samplePosition);

                if (eventTime > position)
                {
                    renderSpan (buffer, position, eventTime - position, s, region,
                                gainStart + gainStep * (float) position, gainStep);
                    position = eventTime;
                }

                handleMidi (metadata.getMessage(), p);
            }

            if (position < total)
                renderSpan (buffer, position, total - position, s, region,
                            gainStart + gainStep * (float) position, gainStep);

            int sounding = 0;

            for (const auto& v : voices)
                if (v.sounding())
                    ++sounding;

            activeVoices.store (sounding, std::memory_order_relaxed);
        }
    };

    // =======================================================================
    SampleEngine::SampleEngine() : impl (std::make_unique<Impl>()) {}
    SampleEngine::~SampleEngine() = default;

    void SampleEngine::prepare (const EngineSpec& spec) { impl->prepare (spec); }
    void SampleEngine::reset()                          { impl->reset(); }

    void SampleEngine::setSlot (const SampleSlot* slot) noexcept { impl->slot = slot; }

    void SampleEngine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                                const ParameterRegistry& params, const MacroState& macros)
    {
        impl->process (buffer, midi, params, macros);
    }

    void SampleEngine::allNotesOff() { impl->allNotesOff(); }

    int SampleEngine::getActiveVoiceCount() const noexcept
    {
        return impl->activeVoices.load (std::memory_order_relaxed);
    }

    bool SampleEngine::hasSample() const noexcept
    {
        return impl->loaded.load (std::memory_order_relaxed);
    }
}
