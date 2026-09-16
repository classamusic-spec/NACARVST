#include "PulseEngine.h"

#include <algorithm>
#include <cmath>

namespace nacar
{
    namespace
    {
        /** Per-destination shaping.  See the header for the reasoning; these
            five rows are the psychoacoustic model, and they are the only place
            it is expressed. */
        struct DuckShape { float attackScale, releaseScale, smoothBias; };

        constexpr DuckShape kShapes[PulseEngine::numDestinations] = {
            { 1.00f, 1.00f,  0.00f },   // VOLUME
            { 0.80f, 0.70f, -0.15f },   // FILTER
            { 1.20f, 1.60f,  0.20f },   // SPACE
            { 0.85f, 0.60f, -0.20f },   // WIDTH
            { 1.00f, 1.30f,  0.10f }    // MEMORY
        };

        // Transient detector.  Fixed times: this is a trigger, not a tone
        // control, and exposing four more knobs for a source nothing can reach
        // yet would be four more things to get wrong.
        constexpr float kFastAttackSec  = 0.0005f;
        constexpr float kFastReleaseSec = 0.040f;
        constexpr float kSlowSec        = 0.180f;
        constexpr float kTriggerRatio   = 2.00f;   ///< fast over slow to fire
        constexpr float kRearmRatio     = 1.25f;   ///< and to re-arm
        constexpr float kFloor          = 0.002f;  ///< about -54 dBFS

        forcedinline float attackCurve (float p, float smooth) noexcept
        {
            const float fast = 1.0f - (1.0f - p) * (1.0f - p);   // straight in
            return fx::lerp (fast, mod::smoothStep (p), smooth); // or an S
        }

        forcedinline float releaseCurve (float p, float smooth) noexcept
        {
            const float q = 1.0f - p;
            const float fast = q * q;                            // out quickly
            return fx::lerp (fast, mod::smoothStep (q), smooth); // or an S
        }

        /** exp(-1/(t*fs)) as a one-pole coefficient, expressed as the amount of
            the way to the target per sample. */
        float followerAlpha (float seconds, double sampleRate) noexcept
        {
            const double s = juce::jmax (1.0e-5, (double) seconds);
            return (float) (1.0 - std::exp (-1.0 / (s * juce::jmax (1.0, sampleRate))));
        }
    }

    // -----------------------------------------------------------------------
    float PulseEngine::Duck::advance (float attackInc, float releaseInc, float smooth) noexcept
    {
        switch (stage)
        {
            case Stage::attack:
                pos += attackInc;

                if (pos >= 1.0f)
                {
                    level = 1.0f;
                    pos = 0.0f;
                    stage = Stage::release;
                }
                else
                {
                    level = from + (1.0f - from) * attackCurve (pos, smooth);
                }
                break;

            case Stage::release:
                pos += releaseInc;

                if (pos >= 1.0f)
                {
                    level = 0.0f;
                    pos = 0.0f;
                    stage = Stage::idle;
                }
                else
                {
                    level = releaseCurve (pos, smooth);
                }
                break;

            case Stage::idle:
            default:
                level = 0.0f;
                break;
        }

        return level;
    }

    // -----------------------------------------------------------------------
    void PulseEngine::prepare (double sr, int maxBlockSize)
    {
        sampleRate = juce::jmax (1.0, sr);
        capacity = juce::jmax (1, maxBlockSize);

        for (auto& b : buffers)
            b.assign ((size_t) capacity, 0.0f);

        fastAttack  = followerAlpha (kFastAttackSec,  sampleRate);
        fastRelease = followerAlpha (kFastReleaseSec, sampleRate);
        slowCoef    = followerAlpha (kSlowSec,        sampleRate);

        reset();
    }

    void PulseEngine::reset() noexcept
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);

        for (auto& d : ducks)
            d.clear();

        freeBeats = 0.0;

        midiRead = midiWrite.load (std::memory_order_relaxed);

        sidechain = nullptr;
        sidechainSamples = 0;

        fastEnv = slowEnv = 0.0f;
        armed = true;
    }

    // -----------------------------------------------------------------------
    void PulseEngine::noteTriggered (int sampleOffset) noexcept
    {
        const int w = midiWrite.load (std::memory_order_relaxed);
        midiOffsets[(size_t) (w % kMidiRing)] = juce::jmax (0, sampleOffset);
        midiWrite.store (w + 1, std::memory_order_release);
    }

    void PulseEngine::setSidechainInput (const float* mono, int numSamples) noexcept
    {
        sidechain = mono;
        sidechainSamples = (mono != nullptr) ? juce::jmax (0, numSamples) : 0;
    }

    const float* PulseEngine::envelope (Destination d) const noexcept
    {
        const int i = juce::jlimit (0, numDestinations - 1, (int) d);
        return buffers[(size_t) i].data();
    }

    // -----------------------------------------------------------------------
    //  Trigger sources
    // -----------------------------------------------------------------------
    void PulseEngine::collectClock (Triggers* t, int numSamples, int division,
                                    const mod::Clock& clock) noexcept
    {
        const double beats = (double) fx::kPulseDivisionBeats[juce::jlimit (0, 8, division)];
        const double bps   = clock.beatsPerSample();     // always > 0

        // Playing: the song position, so a pulse lands on the same sample of
        // the same bar every pass, and a loop or a scrub lands correctly with
        // no resynchronisation state at all.  Stopped: an internal beat counter
        // running at the host tempo, so the user can audition Pulse without
        // pressing play.  freeBeats is kept level with the song position while
        // the transport runs, so stopping continues from where the song was.
        const double start = clock.playing ? clock.ppqPosition : freeBeats;
        const double end   = start + bps * (double) numSamples;

        // ceil() with a small negative bias so that a block starting exactly on
        // the grid fires at offset 0 rather than one period late.  The window is
        // half open, [start, end), and the next block starts at this block's
        // end, so no beat is counted twice and none is missed.
        double k = std::ceil (start / beats - 1.0e-9);

        for (int guard = 0; guard < maxTriggersPerBlock; ++guard)
        {
            const double beat = k * beats;

            if (! (beat < end))
                break;

            if (t != nullptr)
                t->add (juce::jlimit (0, numSamples - 1, (int) ((beat - start) / bps)));

            k += 1.0;
        }

        freeBeats = end;
    }

    void PulseEngine::collectMidi (Triggers& t, int numSamples) noexcept
    {
        const int w = midiWrite.load (std::memory_order_acquire);

        // Anything older than one ring's worth was overrun by a stream of notes
        // faster than a block; dropping the oldest is the right failure.
        if (w - midiRead > kMidiRing)
            midiRead = w - kMidiRing;

        while (midiRead < w)
        {
            t.add (juce::jlimit (0, numSamples - 1, midiOffsets[(size_t) (midiRead % kMidiRing)]));
            ++midiRead;
        }
    }

    bool PulseEngine::collectSidechain (Triggers& t, int numSamples) noexcept
    {
        if (sidechain == nullptr || sidechainSamples <= 0)
            return false;

        const int n = juce::jmin (numSamples, sidechainSamples);

        for (int i = 0; i < n; ++i)
        {
            const float x = std::abs (fx::guard (sidechain[i]));

            fastEnv += (x - fastEnv) * (x > fastEnv ? fastAttack : fastRelease);
            slowEnv += (x - slowEnv) * slowCoef;

            fastEnv = fx::flush (fastEnv);
            slowEnv = fx::flush (slowEnv);

            // Ratio against a slow average rather than an absolute threshold,
            // so the detector works at any input level above the noise floor
            // and does not need a sensitivity control.  Schmitt hysteresis
            // stops one kick from firing five times on its way up.
            if (armed)
            {
                if (fastEnv > slowEnv * kTriggerRatio + kFloor)
                {
                    t.add (i);
                    armed = false;
                }
            }
            else if (fastEnv < slowEnv * kRearmRatio + kFloor * 0.5f)
            {
                armed = true;
            }
        }

        return true;
    }

    // -----------------------------------------------------------------------
    void PulseEngine::process (int numSamples, const Settings& s,
                               const mod::Clock& clock) noexcept
    {
        const int n = juce::jlimit (0, capacity, numSamples);

        if (n <= 0)
            return;

        // Whatever happens below, the grid and the note queue are kept
        // current: a source that is not selected must not accumulate a backlog
        // that bursts out when the user selects it, and the clock must not have
        // to catch up when Pulse is switched on mid-bar.
        Triggers triggers;
        Triggers discarded;

        const bool useMidi      = s.enabled && s.source == 2;
        const bool trySidechain = s.enabled && s.source == 1;

        collectMidi (useMidi ? triggers : discarded, n);

        const bool sidechainFired = trySidechain && collectSidechain (triggers, n);

        // SIDECHAIN with nothing connected falls back to CLOCK rather than
        // silently doing nothing.  Nothing in NACAR calls setSidechainInput(),
        // so today that fallback is the only path SIDECHAIN ever takes.
        const bool useClock = s.enabled && (s.source == 0 || (trySidechain && ! sidechainFired));

        collectClock (useClock ? &triggers : nullptr, n, s.division, clock);

        // The sidechain pointer is borrowed for one block only and never held.
        sidechain = nullptr;
        sidechainSamples = 0;

        if (! s.enabled)
        {
            for (auto& b : buffers)
                juce::FloatVectorOperations::clear (b.data(), n);

            for (auto& d : ducks)
                d.clear();

            return;
        }

        const float smooth = juce::jlimit (0.0f, 1.0f, s.smooth);

        float attackInc[numDestinations], releaseInc[numDestinations], curve[numDestinations];

        for (int d = 0; d < numDestinations; ++d)
        {
            const auto& shape = kShapes[d];

            const float a = juce::jmax (1.0e-4f, s.attackSec  * shape.attackScale);
            const float r = juce::jmax (1.0e-3f, s.releaseSec * shape.releaseScale);

            attackInc [d] = 1.0f / juce::jmax (1.0f, a * (float) sampleRate);
            releaseInc[d] = 1.0f / juce::jmax (1.0f, r * (float) sampleRate);
            curve     [d] = juce::jlimit (0.0f, 1.0f, smooth + shape.smoothBias);
        }

        int next = 0;

        for (int i = 0; i < n; ++i)
        {
            while (next < triggers.count && triggers.offsets[next] <= i)
            {
                for (auto& d : ducks)
                    d.trigger();

                ++next;
            }

            for (int d = 0; d < numDestinations; ++d)
                buffers[(size_t) d][(size_t) i] =
                    juce::jlimit (0.0f, 1.0f,
                                  fx::guard (ducks[d].advance (attackInc[d], releaseInc[d], curve[d])));
        }
    }
}
