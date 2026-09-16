#pragma once

#include "../DspCommon.h"
#include "../EngineContext.h"

namespace nacar
{
    /**
        REWIND  -  specification section 88, and slot four of the signature
        path in section 91.

            SOURCE -> HISTORY BUFFER -> REWIND -> GRAIN -> SPACE

        Rewind is a *gesture*, not a continuous effect.  It is triggered, it
        runs for a window, and it ends: a transport that briefly disagrees with
        the host and then catches back up.  Four transports are offered.

            REVERSE   the playhead runs backwards through the history at SPEED
            STOP      the playhead decelerates to a halt, a tape stop
            DIVE      the playhead falls in pitch rather than halting
            RETURN    jumps back by the window and plays forward, catching up

        Everything the engine reads comes from its own circular history, which
        it writes every block whether or not it is doing anything.  The chain is
        reorderable, so Rewind must remember what actually reached Rewind - it
        deliberately does not share a buffer with Grain.

        The click-free strategy, the triggering rules and the macro response are
        documented at the top of RewindEngine.cpp.
    */
    class RewindEngine
    {
    public:
        RewindEngine();
        ~RewindEngine();

        void prepare (const EngineSpec&);
        void reset();
        void process (juce::AudioBuffer<float>&, const ParameterRegistry&, const MacroState&);

    private:
        // -- the four transports, in the order of the rewind_mode choice list -
        enum class Mode { reverse = 0, stop, dive, ret };

        /** Starts, or restarts, the gesture.  Called from process() only. */
        void trigger (Mode, int windowSamples, float speed, float curve) noexcept;

        /** Moves the read tap to a new delay, crossfading away from the old
            one.  The only place a discontinuous read position is created. */
        void jumpTo (float newDelay) noexcept;

        /** The rate the transport should be running at, given how far through
            the gesture it is.  0..1 in, a playback rate out; negative is
            backwards, 1 is realtime. */
        float rateFor (float u) const noexcept;

        // -- fixed configuration --------------------------------------------
        //  EngineSpec::maxBlockSize is deliberately not stored: Rewind needs no
        //  per-block scratch, so there is nothing for it to size.
        double sampleRate = 48000.0;

        float  xfadeSamples = 288.0f;      ///< read-position crossfade, 6 ms
        float  maxRateDelta = 0.035f;      ///< per sample; full range in ~3 ms
        float  maxDelay = 1000.0f;         ///< clamp, set from the history size

        // -- storage ---------------------------------------------------------
        fx::HistoryBuffer history;

        // -- the read tap, and the tap it is fading away from -----------------
        float curDelay = 1.0f, curRate = 1.0f, targetRate = 1.0f;
        float oldDelay = 1.0f, oldRate = 1.0f;
        float xfadeRemaining = 0.0f;

        // -- the gesture ------------------------------------------------------
        bool  active = false;
        Mode  mode = Mode::reverse;
        float gesturePos = 0.0f;           ///< samples elapsed
        float gestureLen = 1.0f;           ///< samples, never zero
        float gestureSpeed = 1.0f;
        float gestureCurve = 1.0f;         ///< the curve exponent, not the raw control
        float jumpSamples = 0.0f;          ///< RETURN only
        float catchFraction = 0.35f;       ///< RETURN only

        // -- the envelope that fades the whole module in and out --------------
        float env = 0.0f;                  ///< 0 dry, 1 full gesture
        float envRise = 0.005f;            ///< per sample
        float envFall = 0.005f;            ///< per sample, set from TAIL

        // -- triggering -------------------------------------------------------
        double lastGridPhase = -1.0;       ///< previous sample's ppq / division
        float  freeCounter = 0.0f;         ///< samples until the next free trigger
        float  lockout = 0.0f;             ///< samples before a re-trigger is allowed
        float  prevPulse = 0.0f;
        bool   prevOn = false;

        // -- wet-path hygiene --------------------------------------------------
        //  DIVE transposes down by up to three octaves, which puts a 60 Hz bass
        //  at 7.5 Hz.  Two cascaded one-pole high passes keep that out.
        fx::OnePoleTPT subL1, subL2, subR1, subR2;

        // -- mix, ramped across the block rather than smoothed per sample,
        //    so that a mix of zero at both ends of a block is exactly zero ---
        float prevMix = 0.0f;

        fx::Rng rng;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RewindEngine)
    };
}
