#include "RewindEngine.h"

/*
    ======================================================================
    REWIND
    ======================================================================

    Specification section 88, and slot four of the signature path in section
    91.  Rewind captures what just happened and plays it back on a transport
    that briefly disagrees with the host.

    ----------------------------------------------------------------------
    THE ALGORITHM
    ----------------------------------------------------------------------

    One stereo circular history (fx::HistoryBuffer) is written with the *input*
    every sample, unconditionally, whether or not the module is doing anything.
    Writing only while enabled would make the first gesture after switching
    Rewind on read silence, which is the one thing a "plays back recent
    history" module must never do.

    Everything else is one read tap, described by two numbers:

        delay   how many samples behind the write head it reads
        rate    how fast it moves through the material, 1 == realtime

    They are tied together exactly:  advancing one sample moves the write head
    forward by one, so the tap's delay evolves as

        delay += (1 - rate)

    rate == 1 holds the delay still (realtime), rate == 0 holds the *position*
    still (a freeze), and a negative rate runs backwards.  All four modes are
    just different trajectories for `rate`:

        REVERSE   rate = -SPEED, shaped linearly by CURVE so the sweep can
                  accelerate or decelerate across the window.  It starts at
                  delay 1 - the present - and digs backwards from there, so
                  there is no jump at the start at all.
        STOP      rate = (1 - u)^p, reaching exactly zero at the end of the
                  window.  p comes from CURVE: below 1 it drops away fast and
                  lingers near the halt, above 1 it holds speed and then falls
                  off a cliff.  That exponent is the whole character of a tape
                  stop.
        DIVE      rate = 2^(-3 * u^p).  A fall, not a halt: it ends three
                  octaves down and still moving, where STOP ends stationary.
                  Expressed in octaves rather than in rate so that the pitch
                  falls linearly when p == 1, which is what a fall sounds like.
        RETURN    jumps back by the window and plays forward at SPEED, then
                  over the last CURVE-sized fraction of the window sets
                  rate = 1 + remainingDelay / remainingSamples, which lands the
                  tap exactly on realtime at the end.  If SPEED closes the gap
                  early it jumps back again, which is what makes this one read
                  as a stutter rather than as a single repeat.

    INTEGRATION NOTE.  Rewind must be given every block, including blocks in
    which rewind_on is false or the slot is bypassed, or its history goes stale
    and the first gesture after it is switched on plays back whatever was in
    the buffer minutes ago.  The engine bypasses itself internally and is
    bit-exact when idle, so calling it unconditionally costs one circular write
    per sample and nothing else.

    ----------------------------------------------------------------------
    CLICK-FREE STRATEGY  -  the hard requirement
    ----------------------------------------------------------------------

    There are exactly three ways this could click, and each has its own fix.

    1.  A DISCONTINUOUS READ POSITION.  Only RETURN creates one: its initial
        jump and its re-jumps.  A re-trigger while a gesture is already running
        creates one too.  Both go through jumpTo(), which copies the live tap
        into a second tap that keeps running at its old rate, points the live
        tap at the new position, and equal-power crossfades from old to new
        over kXfadeMs (6 ms).  The old tap is kept *moving* during the fade
        rather than frozen, because a frozen tap is a held sample and fading
        out of a held sample is itself a discontinuity in the first derivative.

    2.  A DISCONTINUOUS RATE.  A rate step leaves the position continuous but
        puts a corner in it, which is audible as a soft click at large steps.
        Every rate change - gesture start, gesture end, mode trajectory -
        passes through a slew limiter that needs about 3 ms to cross the whole
        rate range.  The mode trajectories are already smooth functions of u,
        so the limiter only ever engages at the two ends of a gesture.

    3.  THE MODULE ITSELF ARRIVING OR LEAVING.  The gesture envelope `env`
        ramps 0 -> 1 over 6 ms at the start and decays 1 -> 0 over the TAIL
        time at the end, and the mix handed to fx::dryWetGains is
        `rewind_mix * env`.  At env == 0 the dry gain is exactly 1 and the wet
        gain exactly 0, so an idle Rewind is bit-identical to its input.  That
        also removes the +3 dB problem an equal-power crossfade has when the
        wet path happens to equal the dry path: here it never does, because at
        the only moment the two are identical the wet gain is zero.

    REVERSE, STOP and DIVE all start at delay 1 with rate 1 - i.e. reading the
    present at realtime, which *is* the input - and move away from there.  They
    need no crossfade at the start at all.

    ----------------------------------------------------------------------
    TAIL
    ----------------------------------------------------------------------

    TAIL is how much of the gesture survives afterwards.  When the window ends
    the tap is not snapped back: its rate slews to 1, which freezes the delay
    and turns it into a fixed echo of wherever the gesture ended up, while env
    decays over 8 ms + TAIL * 1.2 s.  So the gesture dissolves under the
    returning dry signal instead of being cut off.  TAIL at zero still gets the
    8 ms, because a hard cut is a click.

    ----------------------------------------------------------------------
    TRIGGERING
    ----------------------------------------------------------------------

    ParameterList.h has no trigger parameter, so `rewind_on` is treated as a
    gate that re-arms the gesture rather than as a one-shot:

      * rewind_on && rewind_sync   the gesture re-triggers on the musical grid
                                   given by rewind_div, derived per sample from
                                   macros.ppqPosition, macros.hostBpm and
                                   macros.transportPlaying.  A crossing of an
                                   integer multiple of the division fires it.
                                   That is what the division list is for: it
                                   makes Rewind a rhythmic effect locked to the
                                   song rather than a free-running one.
      * rewind_on && !rewind_sync  it re-triggers every rewind_length seconds.
      * a rising edge of macros.pulseAt(i) through 0.5 fires it as well, per
                                   section 88's "Triggers: UI, MIDI, automation,
                                   Pulse".  Gated by rewind_on, because a module
                                   that is off should not be audible.
      * switching rewind_on from false to true fires it immediately, so the
                                   button behaves like a trigger for a user who
                                   is playing it by hand.

    A lockout of a quarter of the window stops a fast grid or a noisy Pulse
    machine-gunning the gesture.

    NOT IMPLEMENTED: a UI or MIDI one-shot trigger.  There is nowhere for one
    to come from - no parameter, and EditorHost is frozen - so it is absent
    rather than invented.  It would need a single atomic flag on the engine set
    by the editor and consumed here.

    ----------------------------------------------------------------------
    MACRO RESPONSE  (added to the parameters, never replacing them)
    ----------------------------------------------------------------------

      macros.pulseAt(i)    triggers the gesture, as above.  Section 88.
      macros.age           += 0.25 * age on TAIL.  Age is "how used-up it
                           sounds"; a longer smear after the gesture is the
                           cheapest honest reading of that.
      macros.movement      +-12% * movement of random variation on the window
                           length at each trigger, so a grid-locked Rewind is
                           not metronomically identical every time.
      macros.alterAmount   +-0.25 * alterAmount on CURVE, drawn once per
                           trigger, which moves the gesture between its
                           early-fall and late-fall shapes.
      macros.breathAt(i)   +-3% on the free-run trigger period.  Breath is
                           organic non-repeating movement and the free period
                           is a slow parameter, which is the only kind it is
                           allowed near.

    Deliberately unused: scale, distance, wetBias (World is Space's business,
    not Rewind's), widthScale (Rewind does nothing to the stereo image - see
    below), grit, memoryGeneration, the LFOs and the other Pulse destinations.

    ----------------------------------------------------------------------
    THE LOW END, AND STEREO
    ----------------------------------------------------------------------

    Sections 38, 40 and 43.  Rewind reads L and R at *identical* positions, so
    whatever L/R correlation the material had at that moment is preserved
    exactly; the gesture is a time behaviour, not a stereo behaviour, and it
    cannot decorrelate the bass.  No band splitting is needed for that.

    What it can do is manufacture subsonic energy: DIVE ends three octaves
    down, which puts a 60 Hz bass note at 7.5 Hz - inaudible cone travel that
    steals headroom from everything downstream.  The wet path therefore runs
    through two cascaded one-pole high passes at 22 Hz (12 dB/octave).  The dry
    path is untouched, so bypass stays exact.

    ----------------------------------------------------------------------
    GAIN STAGING
    ----------------------------------------------------------------------

    Rewind has no gain of its own: the wet path is a read of material that was
    already at the chain's level, and the only gain applied anywhere is
    fx::dryWetGains.  Nothing here is normalised, because nothing here sums.
    The TAIL overlap is the one place two signals coexist, and it is bounded by
    the equal-power pair.

    ----------------------------------------------------------------------
    MEMORY BUDGET
    ----------------------------------------------------------------------

    8 seconds of stereo history is requested.  fx::HistoryBuffer rounds up to a
    power of two, so the real figure is 10.9 s at 96 kHz (8.4 MB), 10.9 s at
    48 kHz (4.2 MB) and 11.9 s at 44.1 kHz (4.2 MB).  Two bars is longer than
    that below about 55 BPM, and the requested window is clamped to what is
    actually there - so at 40 BPM a "2 BAR" rewind reaches back 10.9 s rather
    than 12, which is a shortened gesture rather than a read of stale data.

    ----------------------------------------------------------------------
    KNOWN LIMITATIONS
    ----------------------------------------------------------------------

      * Nobody has listened to this.
      * No UI/MIDI one-shot trigger, for the reason given above.
      * The history read is linear-interpolated (fx::HistoryBuffer::read), so
        playing back at a fractional rate loses a little top end.  At the rates
        Rewind uses - 0.125x to 4x - that is between inaudible and a deliberate
        part of the character, but it is not a transparent varispeed.
      * Playing backwards through material reverses its envelopes but not its
        phase relationships between the two channels; a heavily decorrelated
        input stays decorrelated, it does not become "reverse stereo".
      * A re-trigger during the crossfade of a previous jump abandons the
        older tap.  Two nested jumps inside 6 ms would need a third tap; the
        lockout makes it unreachable in practice but it is not impossible
        through automation of rewind_div.
      * A REVERSE gesture is shortened when SPEED is high: the tap digs back
        at (1 + SPEED) samples per sample, so at SPEED 4 the longest window the
        history can support is about 2.2 s rather than the 2 bars the division
        may be asking for.  The duration is clamped at trigger time, which is
        preferable to clamping the position mid-gesture - that would be a rate
        discontinuity in the middle of the sweep.
      * The SPEED control means "playback rate" in REVERSE and RETURN but
        "how fast the gesture happens" in STOP and DIVE.  That is deliberate -
        a tape stop's speed is its duration - but it does mean one control has
        two meanings depending on the mode.
*/

namespace nacar
{
    namespace
    {
        // Layout-free constants.  Every one of these describes a *time*, so it
        // is converted against the sample rate in prepare() and none of them
        // assumes 44.1 kHz.
        constexpr double kHistorySeconds = 8.0;   ///< see MEMORY BUDGET above
        constexpr float  kXfadeMs        = 6.0f;  ///< read-position crossfade
        constexpr float  kRateSlewMs     = 3.0f;  ///< full rate range crossing
        constexpr float  kEnvRiseMs      = 6.0f;
        constexpr float  kMinTailMs      = 8.0f;
        constexpr float  kMaxTailSeconds = 1.2f;
        constexpr float  kDiveOctaves    = 3.0f;  ///< DIVE's total fall
        constexpr float  kSubsonicHz     = 22.0f;
        constexpr float  kMinWindowSec   = 0.02f;
        constexpr float  kMaxWindowSec   = 8.0f;

        /** x^p for x in [0,1], through the house fast transcendentals. */
        forcedinline float powCurve (float x, float p) noexcept
        {
            return fx::exp2Fast (p * fx::log2Fast (juce::jmax (1.0e-5f, x)));
        }

        /** CURVE 0..1 -> an exponent in [0.35, 2.83], 0.5 giving exactly 1. */
        forcedinline float curveExponent (float curve) noexcept
        {
            return fx::exp2Fast ((juce::jlimit (0.0f, 1.0f, curve) - 0.5f) * 3.0f);
        }
    }

    RewindEngine::RewindEngine() { rng.setSeed (0x5257'4E44u); }   // 'RWND'
    RewindEngine::~RewindEngine() = default;

    void RewindEngine::prepare (const EngineSpec& spec)
    {
        sampleRate = juce::jmax (8000.0, spec.sampleRate);

        history.prepare (sampleRate, kHistorySeconds);

        // Leave room for the interpolator's two-sample reach and for the
        // clamp inside HistoryBuffer::read, which stops at mask - 2.
        maxDelay = (float) juce::jmax (64, history.capacity() - 8);

        xfadeSamples = juce::jmax (4.0f, (float) (sampleRate * kXfadeMs * 0.001));
        maxRateDelta = 5.0f / juce::jmax (1.0f, (float) (sampleRate * kRateSlewMs * 0.001));

        envRise = 1.0f / juce::jmax (1.0f, (float) (sampleRate * kEnvRiseMs * 0.001));
        envFall = envRise;

        subL1.setCutoff (kSubsonicHz, sampleRate);
        subL2.setCutoff (kSubsonicHz, sampleRate);
        subR1.setCutoff (kSubsonicHz, sampleRate);
        subR2.setCutoff (kSubsonicHz, sampleRate);

        reset();
    }

    void RewindEngine::reset()
    {
        history.reset();

        curDelay = oldDelay = 1.0f;
        curRate = oldRate = targetRate = 1.0f;
        xfadeRemaining = 0.0f;

        active = false;
        mode = Mode::reverse;
        gesturePos = 0.0f;
        gestureLen = 1.0f;
        gestureSpeed = 1.0f;
        gestureCurve = 1.0f;
        jumpSamples = 0.0f;
        catchFraction = 0.35f;

        env = 0.0f;
        lastGridPhase = -1.0;
        freeCounter = 0.0f;
        lockout = 0.0f;
        prevPulse = 0.0f;
        prevOn = false;
        prevMix = 0.0f;

        subL1.reset(); subL2.reset();
        subR1.reset(); subR2.reset();
    }

    void RewindEngine::jumpTo (float newDelay) noexcept
    {
        const float target = juce::jlimit (1.0f, maxDelay, newDelay);

        // Below a couple of samples the "jump" is inside the interpolator's
        // own reach and crossfading it would cost more than it saves.
        if (std::abs (target - curDelay) < 2.0f)
        {
            curDelay = target;
            return;
        }

        oldDelay = curDelay;
        oldRate  = curRate;          // the retiring tap keeps moving, see (1)
        xfadeRemaining = xfadeSamples;

        curDelay = target;
    }

    float RewindEngine::rateFor (float u) const noexcept
    {
        switch (mode)
        {
            case Mode::reverse:
            {
                // CURVE tilts the sweep linearly about its average, so the
                // window still covers the same amount of material whatever
                // the shape: 1 + k(2u - 1) has mean exactly 1 for any k.
                const float k = juce::jlimit (-0.7f, 0.7f, (gestureCurve - 1.0f) * 0.5f);
                return -gestureSpeed * (1.0f + k * (2.0f * u - 1.0f));
            }

            case Mode::stop:
                // Reaches exactly zero at u == 1: a halt, not a slow crawl.
                return powCurve (1.0f - u, gestureCurve);

            case Mode::dive:
                // Linear in octaves, so the pitch falls rather than the rate.
                return fx::exp2Fast (-kDiveOctaves * powCurve (u, gestureCurve));

            case Mode::ret:
            default:
                return gestureSpeed;   // the catch-up is computed in process()
        }
    }

    void RewindEngine::trigger (Mode m, int windowSamples, float speed, float curve) noexcept
    {
        mode = m;
        gestureSpeed = juce::jlimit (0.1f, 4.0f, speed);
        gestureCurve = curveExponent (curve);
        gesturePos = 0.0f;

        // Bounds first, and ordered, so that jlimit can never be handed a
        // lower limit above its upper one at a small history size.
        const float upperWindow = juce::jmax (64.0f,
            juce::jmin ((float) (kMaxWindowSec * sampleRate), maxDelay - 4.0f));
        const float lowerWindow = juce::jmin (upperWindow,
            (float) (kMinWindowSec * sampleRate));

        const float window = juce::jlimit (lowerWindow, upperWindow, (float) windowSamples);

        switch (m)
        {
            case Mode::reverse:
                // The tap digs backwards at (1 + speed) samples per sample, so
                // the window has to be short enough that it never runs off the
                // end of the history.  Clamping the *duration* here is better
                // than clamping the position later: the position clamp would
                // be a rate discontinuity in the middle of the gesture.
                gestureLen = juce::jmin (window, (maxDelay - 4.0f) / (1.0f + gestureSpeed));
                jumpSamples = 0.0f;
                break;

            case Mode::stop:
            case Mode::dive:
                // SPEED here means "how fast the gesture happens", because a
                // tape stop's speed is its duration.  The tap falls behind by
                // at most the gesture length, which is already clamped.
                gestureLen = juce::jlimit (lowerWindow, upperWindow,
                                           window / gestureSpeed);
                jumpSamples = 0.0f;
                break;

            case Mode::ret:
            default:
                gestureLen = window;
                jumpSamples = window;
                // CURVE decides how much of the window is spent catching up:
                // a small fraction is a late scramble, a large one is a gentle
                // drift back into time.
                catchFraction = juce::jlimit (0.15f, 0.85f, 0.2f + 0.6f * curve);
                break;
        }

        gestureLen = juce::jmax (8.0f, gestureLen);

        // Only RETURN starts somewhere else.  The other three start at the
        // present, reading the input at realtime, so they need no crossfade.
        if (m == Mode::ret)
            jumpTo (jumpSamples);
        else
            jumpTo (1.0f);

        targetRate = rateFor (0.0f);

        if (! active)
        {
            // Coming from idle: the wet-path filters have been sitting on
            // stale state since the last gesture.  Clear them rather than let
            // a decaying offset thump under the new one.
            subL1.reset(); subL2.reset();
            subR1.reset(); subR2.reset();
        }

        active = true;
        lockout = gestureLen * 0.25f;
    }

    void RewindEngine::process (juce::AudioBuffer<float>& buffer,
                                const ParameterRegistry& p,
                                const MacroState& macros)
    {
        const int n = juce::jmin (macros.numSamples, buffer.getNumSamples());
        const int numCh = juce::jmin (2, buffer.getNumChannels());

        if (n <= 0 || numCh <= 0)
            return;

        float* left  = buffer.getWritePointer (0);
        float* right = (numCh > 1) ? buffer.getWritePointer (1) : left;

        // -- the registry, read exactly once ---------------------------------
        const bool  on       = p.flag (PID::rewindOn);
        const int   modeIx   = juce::jlimit (0, 3, p.choice (PID::rewindMode));
        const int   divIx    = juce::jlimit (0, 5, p.choice (PID::rewindDivision));
        const bool  sync     = p.flag (PID::rewindSync);
        const float freeSec  = juce::jlimit (0.01f, 4.0f,  p.raw (PID::rewindLength));
        const float curveRaw = juce::jlimit (0.0f,  1.0f,  p.raw (PID::rewindCurve));
        const float speed    = juce::jlimit (0.1f,  4.0f,  p.raw (PID::rewindSpeed));
        const float mixParam = juce::jlimit (0.0f,  1.0f,  p.raw (PID::rewindMix));

        // MACRO: Memory + Character ("how used-up it sounds") lengthens the
        // tail.  Added to the control, so a patch that asks for no tail at
        // full Memory still gets a short one rather than being overridden.
        const float tail = juce::jlimit (0.0f, 1.0f,
                                         p.raw (PID::rewindTail) + 0.25f * macros.age);

        const float tailSeconds = kMinTailMs * 0.001f + tail * kMaxTailSeconds;
        envFall = 1.0f / juce::jmax (1.0f, (float) (sampleRate * (double) tailSeconds));

        // -- how far back the gesture reaches --------------------------------
        //  SYNC uses the shared division table so that "1/4" means the same
        //  thing here as it does in Pulse and the LFOs.  The request is then
        //  clamped to the history that actually exists: at 40 BPM two bars is
        //  12 s and the buffer holds 10.9 s, which shortens the gesture rather
        //  than reading stale or uninitialised material.
        const float windowSec = sync
            ? fx::beatsToSeconds (fx::kRewindDivisionBeats[divIx], macros.hostBpm)
            : freeSec;

        const int availableSamples = juce::jmax (64, history.available() - 8);
        const int windowSamples = juce::jlimit (32, availableSamples,
                                                (int) (windowSec * sampleRate));

        // -- triggering, prepared once ---------------------------------------
        const bool   useGrid = sync && macros.transportPlaying && macros.hostBpm > 1.0;
        const double ppqPerSample = macros.hostBpm / (60.0 * sampleRate);
        const double divBeats = (double) juce::jmax (0.0625f, fx::kRewindDivisionBeats[divIx]);
        const float  freePeriod = juce::jmax (32.0f, freeSec * (float) sampleRate);
        const bool   onEdge = on && ! prevOn;
        const Mode   requested = (Mode) modeIx;

        // BYPASS: at zero mix the module is inaudible, so the gesture is
        // abandoned rather than run for nothing.  Every sample then takes the
        // idle path below, which writes the history and leaves the buffer
        // untouched - bit-identical, not merely close.
        if (mixParam <= 0.0f && prevMix <= 0.0f)
        {
            active = false;
            env = 0.0f;
        }

        fx::Ramp mixRamp;
        mixRamp.set (prevMix, mixParam, n);

        for (int i = 0; i < n; ++i)
        {
            const float inL = left[i];
            const float inR = (numCh > 1) ? right[i] : inL;

            // The history is written unconditionally.  See the note at the top
            // of this file: an early-out that skipped the write would make the
            // first gesture after enabling Rewind read silence.
            history.write (inL, inR);

            if (lockout > 0.0f)
                lockout -= 1.0f;

            // ---- triggers --------------------------------------------------
            const double gridPhase = useGrid
                ? (macros.ppqPosition + (double) i * ppqPerSample) / divBeats
                : -1.0;

            const float pulse = macros.pulseAt (i);

            bool fire = false;
            bool manual = false;    // a hand press, which the lockout must not eat

            if (on)
            {
                // The button itself, so that a user playing rewind_on by hand
                // gets a gesture on the press rather than on the next grid line.
                if (onEdge && i == 0)
                    fire = manual = true;

                // The musical grid.  A crossing of an integer multiple of the
                // division fires; lastGridPhase < 0 means "no reference yet",
                // which happens on the first block and whenever the transport
                // stops, and only arms the comparison.
                if (useGrid && lastGridPhase >= 0.0
                    && std::floor (gridPhase) > std::floor (lastGridPhase))
                    fire = true;

                // No grid available - SYNC off, or the transport stopped -
                // so it free-runs.  MACRO: Breath puts +-3 % on the period,
                // which is a slow parameter and therefore the only kind Breath
                // is allowed near.
                if (! useGrid)
                {
                    freeCounter -= 1.0f;

                    if (freeCounter <= 0.0f)
                    {
                        fire = true;
                        freeCounter = juce::jmax (32.0f, freePeriod
                                                  * (1.0f + 0.03f * macros.breathAt (i)));
                    }
                }

                // MACRO: Pulse, per section 88's "Triggers: ... Pulse".  A
                // rising edge through half depth.  Gated by rewind_on, because
                // a module that is off must stay silent.
                if (pulse > 0.5f && prevPulse <= 0.5f)
                    fire = true;
            }

            lastGridPhase = gridPhase;      // -1 when there is no grid: re-arms
            prevPulse = pulse;

            if (fire && (manual || lockout <= 0.0f))
            {
                // MACRO: Motion varies the window by +-12 % per trigger, so a
                // grid-locked Rewind is not metronomically identical each time.
                const float wobble = 1.0f + 0.12f * macros.movement * rng.nextBipolar();

                // MACRO: Alter moves the gesture between its early-fall and
                // late-fall shapes.
                const float curve = juce::jlimit (0.0f, 1.0f,
                    curveRaw + 0.25f * macros.alterAmount * rng.nextBipolar());

                trigger (requested, (int) ((float) windowSamples * wobble), speed, curve);
            }

            // ---- idle: exact bypass ----------------------------------------
            if (! active && env <= 0.0f)
            {
                // Snap the tap to the present so the next gesture starts from
                // a known place, and leave the buffer alone.  No gain is
                // applied, so the output is the input sample for sample.
                curDelay = oldDelay = 1.0f;
                curRate = oldRate = targetRate = 1.0f;
                xfadeRemaining = 0.0f;
                continue;
            }

            // ---- the transport ---------------------------------------------
            if (active)
            {
                const float u = juce::jlimit (0.0f, 1.0f, gesturePos / gestureLen);

                if (mode == Mode::ret)
                {
                    if (u >= 1.0f - catchFraction)
                    {
                        // Close whatever delay is left over whatever time is
                        // left.  Capped at 2x so the catch-up can never
                        // transpose by more than an octave; anything it fails
                        // to close is removed by the tail instead.
                        const float remaining = juce::jmax (1.0f, gestureLen - gesturePos);
                        targetRate = juce::jlimit (0.25f, 2.0f,
                                                   1.0f + (curDelay - 1.0f) / remaining);
                    }
                    else
                    {
                        targetRate = gestureSpeed;

                        // Caught up early: jump back and do it again.  This is
                        // the stutter, and it is the one discontinuity in the
                        // middle of a gesture - jumpTo() crossfades it.
                        if (curDelay <= 2.0f && gestureSpeed > 1.0f)
                            jumpTo (jumpSamples);
                    }
                }
                else
                {
                    targetRate = rateFor (u);
                }

                gesturePos += 1.0f;

                if (gesturePos >= gestureLen)
                {
                    // The gesture is over.  The tap is not snapped back: its
                    // rate slews to realtime, which holds the delay still and
                    // turns it into a fixed echo of wherever it ended up, and
                    // the envelope decays over the TAIL time underneath the
                    // returning dry signal.
                    active = false;
                    targetRate = 1.0f;
                }
            }
            else
            {
                targetRate = 1.0f;
            }

            // ---- rate slew: no instantaneous speed changes anywhere ---------
            curRate += juce::jlimit (-maxRateDelta, maxRateDelta, targetRate - curRate);

            // delay and rate are tied exactly: the write head moves one sample
            // per sample, so a tap running at `rate` falls behind at (1 - rate).
            curDelay = juce::jlimit (1.0f, maxDelay, curDelay + (1.0f - curRate));

            float wetL, wetR;
            history.read (curDelay, wetL, wetR);

            // ---- crossfade away from a discontinuous read position ----------
            if (xfadeRemaining > 0.0f)
            {
                oldDelay = juce::jlimit (1.0f, maxDelay, oldDelay + (1.0f - oldRate));

                float oldL, oldR;
                history.read (oldDelay, oldL, oldR);

                const float t = 1.0f - xfadeRemaining / xfadeSamples;   // 0 -> 1
                const float gNew = fx::sineTurns (0.25f * t);
                const float gOld = fx::cosineTurns (0.25f * t);

                wetL = gNew * wetL + gOld * oldL;
                wetR = gNew * wetR + gOld * oldR;

                xfadeRemaining -= 1.0f;
            }

            // ---- subsonic guard, wet path only ------------------------------
            //  Sections 38/40/43.  DIVE ends three octaves down, which puts a
            //  60 Hz bass at 7.5 Hz.  Two cascaded one-poles at 22 Hz are
            //  12 dB/octave, about 19 dB of rejection there.  L and R are read
            //  at identical positions, so nothing here decorrelates the low
            //  band - the gesture is a time behaviour, not a stereo one.
            wetL = subL2.highpass (subL1.highpass (wetL));
            wetR = subR2.highpass (subR1.highpass (wetR));

            // ---- envelope and mix -------------------------------------------
            env = active ? juce::jmin (1.0f, env + envRise)
                         : juce::jmax (0.0f, env - envFall);

            const float effMix = juce::jlimit (0.0f, 1.0f, mixRamp.at (i) * env);

            float dryGain, wetGain;
            fx::dryWetGains (effMix, dryGain, wetGain);

            const float outL = fx::guard (dryGain * inL + wetGain * wetL);
            const float outR = fx::guard (dryGain * inR + wetGain * wetR);

            left[i] = outL;

            if (numCh > 1)
                right[i] = outR;
        }

        prevOn = on;
        prevMix = mixParam;
    }
}
