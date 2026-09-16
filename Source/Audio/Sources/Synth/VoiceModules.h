#pragma once

#include "Oscillator.h"
#include "SynthCharacter.h"

namespace nacar::synth
{
    // =======================================================================
    //  SUB OSCILLATOR
    //
    //  The sub is the one part of NACAR that is never widened, never detuned
    //  and never modulated.  Everything else in the voice may move; this has
    //  to be the thing the low end is nailed to.
    // =======================================================================
    class SubOscillator
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            leak = std::exp (-kTwoPi * 5.0f / (float) juce::jmax (1.0, sampleRate));
            reset();
        }

        void reset() noexcept { phase = 0.0f; triState = 0.0f; }

        /** @param waveIndex 0 SINE, 1 TRIANGLE, 2 SOFT SQUARE
            @param harmonics 0..1, adds upper harmonics so the sub survives a
                             speaker that cannot reproduce its fundamental */
        forcedinline float process (int waveIndex, float increment, float harmonics) noexcept
        {
            const float inc = juce::jlimit (0.0f, 0.45f, increment);

            float y;

            switch (waveIndex)
            {
                case 1:
                    triState = leak * triState + 4.0f * inc * blepPulse (phase, inc, 0.5f);
                    triState = flush (triState);
                    y = triState;
                    break;

                case 2:
                    // "Soft square" rather than a square: a hard square an
                    // octave down is mostly odd harmonics in the region where
                    // the fundamental already lives, and it muddies everything
                    // above it.  Rounding it keeps the shape without the mud.
                    y = tanhFast (blepPulse (phase, inc, 0.5f) * 0.8f) * 0.9f;
                    break;

                default:
                    y = sineTurns (phase);
                    break;
            }

            phase += inc;
            phase -= std::floor (phase);

            if (harmonics > 0.0001f)
            {
                // Asymmetric shaping, so the harmonics generated are mostly
                // even - the second harmonic sits exactly on the note the sub
                // is an octave below, which is what makes a 40 Hz fundamental
                // still read as that note on a phone.
                const float h = juce::jlimit (0.0f, 1.0f, harmonics);
                const float shaped = tanhFast (y * (1.0f + h * 3.0f));
                const float even   = shaped * shaped - 0.5f;

                y = lerp (y, y * 0.7f + even * 0.6f, h);
            }

            return sane (y) ? y : (phase = 0.0f, triState = 0.0f, 0.0f);
        }

    private:
        float phase = 0.0f, triState = 0.0f, leak = 0.9995f;
    };

    // =======================================================================
    //  NOISE / EXCITER
    //
    //  Every colour is generated, never sampled.  The seven types are the ones
    //  the specification names; each is a different filtering of the same
    //  deterministic white source so that a patch recalls identically.
    // =======================================================================
    class NoiseExciter
    {
    public:
        void prepare (double sampleRate, juce::uint32 seed) noexcept
        {
            sr = (float) juce::jmax (1.0, sampleRate);
            rng.setSeed (seed);

            dark.setCutoff (700.0f, sampleRate);
            airLow.setCutoff (4500.0f, sampleRate);
            textureA.setCutoff (900.0f, sampleRate);
            textureB.setCutoff (2400.0f, sampleRate);

            reset();
        }

        void reset() noexcept
        {
            for (auto& p : pink) p = 0.0f;
            dark.reset(); airLow.reset(); textureA.reset(); textureB.reset();
            dustHold = 0.0f;
            digitalHold = 0.0f;
            digitalCounter = 0;
            texturePhase = 0.0f;
        }

        /** @param type 0 WHITE, 1 PINK, 2 DARK, 3 AIR, 4 DUST, 5 DIGITAL, 6 TEXTURE */
        forcedinline float process (int type) noexcept
        {
            const float w = rng.nextBipolar();

            switch (type)
            {
                case 1:
                {
                    // Three-pole Voss-style pink approximation: about +-0.3 dB
                    // of a true -3 dB/octave slope from 20 Hz to 20 kHz, for
                    // six operations.
                    pink[0] = 0.99765f * pink[0] + w * 0.0990460f;
                    pink[1] = 0.96300f * pink[1] + w * 0.2965164f;
                    pink[2] = 0.57000f * pink[2] + w * 1.0526913f;

                    return (pink[0] + pink[1] + pink[2] + w * 0.1848f) * 0.25f;
                }

                case 2:
                    return dark.lowpass (w) * 2.2f;

                case 3:
                    return airLow.highpass (w) * 0.9f;

                case 4:
                {
                    // Dust: sparse impulses rather than a continuous bed.  The
                    // density is deliberately low - this is a texture for
                    // exciting a resonator or for adding grit to an attack,
                    // not a noise floor.
                    dustHold *= 0.72f;

                    if (rng.next01() < 0.004f)
                        dustHold = rng.nextBipolar();

                    return dustHold;
                }

                case 5:
                {
                    // Digital: sample-and-hold plus hard quantisation, so it
                    // has an audible rate and an audible step size.
                    if (++digitalCounter >= 9)
                    {
                        digitalCounter = 0;
                        digitalHold = std::round (w * 3.0f) * (1.0f / 3.0f);
                    }

                    return digitalHold;
                }

                case 6:
                {
                    // Texture: band-passed noise whose centre drifts slowly, so
                    // a pad's air moves instead of sitting still.
                    texturePhase += 0.37f / sr;
                    texturePhase -= std::floor (texturePhase);

                    const float sweep = 0.5f + 0.5f * sineTurns (texturePhase);
                    const float band  = textureB.lowpass (textureA.highpass (w));

                    return band * (1.2f + sweep * 0.8f);
                }

                default:
                    return w;
            }
        }

    private:
        Rng rng;
        float sr = 48000.0f;
        float pink[3] {};
        OnePoleTPT dark, airLow, textureA, textureB;
        float dustHold = 0.0f, digitalHold = 0.0f, texturePhase = 0.0f;
        int digitalCounter = 0;
    };

    // =======================================================================
    //  DRIFT
    //
    //  Two slow sines at incommensurate rates, summed.  A bounded random walk
    //  is the textbook answer, but a walk has to be clamped, and a clamped walk
    //  spends its time against the rails; two sines whose ratio is irrational
    //  never repeat inside any musically interesting span and are bounded by
    //  construction with no clamp at all.
    // =======================================================================
    class DriftGenerator
    {
    public:
        void prepare (double sampleRate, juce::uint32 seed) noexcept
        {
            sr = (float) juce::jmax (1.0, sampleRate);

            Rng rng (seed);
            phaseA = rng.next01();
            phaseB = rng.next01();
            counter = 0;
            current = 0.0f;
            step = 0.0f;

            // Ratio near the golden mean: the least rational number there is,
            // so the sum has the longest possible period.
            ratio = 1.618034f * (0.85f + rng.next01() * 0.3f);
            weight = 0.55f + rng.next01() * 0.2f;
        }

        void reset() noexcept { counter = 0; current = 0.0f; step = 0.0f; }

        /** @param rateHz base rate of the slower component
            @returns roughly -1..1

            Evaluated once every kControlPeriod samples and interpolated in
            between.  At 48 kHz that is a 750 Hz control rate for a modulator
            whose fastest setting is 8 Hz, so the interpolation error is far
            below anything the pitch of a note could reveal - and it takes two
            sine evaluations per sample down to two per sixteen. */
        forcedinline float process (float rateHz) noexcept
        {
            if (counter <= 0)
            {
                counter = kControlPeriod;

                const float r = juce::jlimit (0.001f, 8.0f, rateHz);
                const float advance = (float) kControlPeriod / sr;

                phaseA += r * advance;
                phaseB += r * ratio * advance;

                phaseA -= std::floor (phaseA);
                phaseB -= std::floor (phaseB);

                const float target = sineTurns (phaseA) * weight
                                   + sineTurns (phaseB) * (1.0f - weight);

                step = (target - current) * (1.0f / (float) kControlPeriod);
            }

            --counter;
            current += step;

            return current;
        }

    private:
        static constexpr int kControlPeriod = 16;

        float sr = 48000.0f;
        float phaseA = 0.0f, phaseB = 0.37f, ratio = 1.618034f, weight = 0.6f;
        float current = 0.0f, step = 0.0f;
        int counter = 0;
    };

    // =======================================================================
    //  ENVELOPE
    //
    //  ADSR with curved stages, sample-rate independent by construction.
    //
    //  The attack is the part worth explaining.  A plain one-pole approaches
    //  its target asymptotically and never arrives, so a "1 ms attack" built
    //  that way is really a 5 ms attack that never quite reaches full level -
    //  which is why so many synths click on short attacks, because the amp
    //  stage jumps the last of the distance when the decay begins.  Here the
    //  pole is aimed at an overshoot target above 1.0 and the stage ends when
    //  the output actually crosses 1.0.  The curve is exponential, the timing
    //  is exact, and the transition to decay is continuous.
    // =======================================================================
    class Envelope
    {
    public:
        enum class Stage { idle, attack, decay, sustain, release };

        void prepare (double sampleRate) noexcept
        {
            sr = juce::jmax (1.0, sampleRate);
            reset();
        }

        void reset() noexcept
        {
            stage = Stage::idle;
            value = 0.0f;
        }

        void setShape (float attackCurveAmount, float decayCurveAmount) noexcept
        {
            // Larger overshoot targets give straighter stages; the defaults per
            // character live in CharacterProfile.
            attackTarget = 1.0f + juce::jlimit (0.02f, 2.0f, attackCurveAmount);
            decayFloor   = -juce::jlimit (0.0005f, 0.5f, decayCurveAmount);
        }

        void setParameters (float attackSec, float decaySec, float sustainLevel,
                            float releaseSec) noexcept
        {
            aCoef = coefficient (attackSec);
            dCoef = coefficient (decaySec);
            rCoef = coefficient (releaseSec);
            sustain = juce::jlimit (0.0f, 1.0f, sustainLevel);
        }

        void noteOn (bool retrigger) noexcept
        {
            // A legato retrigger continues from wherever the envelope is, so a
            // held phrase does not restart its attack on every note.
            if (! retrigger)
                value = 0.0f;

            stage = Stage::attack;
        }

        void noteOff() noexcept
        {
            if (stage != Stage::idle)
                stage = Stage::release;
        }

        /** Immediate, click-free stop, used by voice stealing. */
        void fastRelease (double sampleRate) noexcept
        {
            rCoef = coefficient (0.004f, sampleRate);
            stage = Stage::release;
        }

        forcedinline float process() noexcept
        {
            switch (stage)
            {
                case Stage::attack:
                    value = attackTarget + aCoef * (value - attackTarget);

                    if (value >= 1.0f)
                    {
                        value = 1.0f;
                        stage = Stage::decay;
                    }
                    break;

                case Stage::decay:
                    value = sustain + decayFloor + dCoef * (value - sustain - decayFloor);

                    if (value <= sustain + 0.0005f)
                    {
                        value = sustain;
                        stage = Stage::sustain;
                    }
                    break;

                case Stage::sustain:
                    value = sustain;
                    break;

                case Stage::release:
                    value = decayFloor + rCoef * (value - decayFloor);

                    if (value <= 0.0002f)
                    {
                        value = 0.0f;
                        stage = Stage::idle;
                    }
                    break;

                case Stage::idle:
                default:
                    value = 0.0f;
                    break;
            }

            return (value = flush (juce::jlimit (0.0f, 1.0f, value)));
        }

        bool isActive() const noexcept   { return stage != Stage::idle; }
        bool isReleasing() const noexcept { return stage == Stage::release; }
        float getValue() const noexcept  { return value; }
        Stage getStage() const noexcept  { return stage; }

    private:
        float coefficient (float seconds) const noexcept { return coefficient (seconds, sr); }

        static float coefficient (float seconds, double sampleRate) noexcept
        {
            const double s = juce::jmax (1.0e-5, (double) seconds);
            return (float) std::exp (-1.0 / (s * sampleRate));
        }

        double sr = 48000.0;
        Stage stage = Stage::idle;

        float value = 0.0f, sustain = 0.8f;
        float aCoef = 0.9f, dCoef = 0.999f, rCoef = 0.999f;
        float attackTarget = 1.3f, decayFloor = -0.0025f;
    };

    // =======================================================================
    //  BODY
    //
    //  "At low settings the impression should be BIGGER, not DISTORTED."
    //
    //  That sentence rules out a waveshaper on the full-band signal, because a
    //  waveshaper makes a sound bigger and dirtier at the same rate.  So the
    //  band that carries physical size - roughly 90 Hz to 700 Hz - is split
    //  out, shaped on its own, and mixed back.  The top of the spectrum, where
    //  distortion is audible as distortion, is never touched.
    //
    //  The shaper is asymmetric, and its asymmetry is what generates the even
    //  harmonics: the second harmonic of a 100 Hz fundamental is 200 Hz, which
    //  is squarely in the band a small speaker can reproduce.  bodyTilt moves
    //  the balance between that even-harmonic behaviour and the odd-harmonic
    //  behaviour of a symmetric shaper, which reads as firm rather than warm.
    // =======================================================================
    class SynthBody
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            lowSplit.setCutoff (90.0f, sampleRate);
            midSplit.setCutoff (700.0f, sampleRate);
            dc.prepare (sampleRate);
            comp.setTime (0.012f, sampleRate);
            reset();
        }

        void reset() noexcept
        {
            lowSplit.reset();
            midSplit.reset();
            dc.reset();
            comp.reset();
            comp.setValue (1.0f);
        }

        /** @param amount 0..1
            @param tilt  -1 odd/firm .. +1 even/warm
            @param lowMidGain how hard this character leans on the low mids */
        forcedinline float process (float x, float amount, float tilt, float lowMidGain) noexcept
        {
            if (amount <= 0.0005f)
                return x;

            const float a = juce::jlimit (0.0f, 1.0f, amount);

            // Three-way split.  The low band is left alone: shaping below 90 Hz
            // is how a bass loses its centre.
            const float low  = lowSplit.lowpass (x);
            const float rest = x - low;
            const float mid  = midSplit.lowpass (rest);
            const float high = rest - mid;

            const float drive = 1.0f + a * 2.6f * lowMidGain;
            const float d     = mid * drive;

            const float sym  = tanhFast (d);
            const float asym = tanhFast (d + 0.28f * a) - tanhFast (0.28f * a);

            const float t = juce::jlimit (-1.0f, 1.0f, tilt) * 0.5f + 0.5f;   // 0..1
            float shaped = lerp (sym, asym, t);

            shaped = dc.process (shaped);

            // Gentle level control inside the band, so that adding Body adds
            // density rather than peaks.  Program-dependent, one pole, no
            // lookahead: it is a weight stage, not a limiter.
            const float env  = comp.process (std::abs (shaped));
            const float gain = 1.0f / (1.0f + env * a * 0.55f);

            const float bodied = lerp (mid, shaped * gain / drive * 1.35f, a);

            // The low band gets a small, purely linear lift: the point is mass,
            // and mass is amplitude, not harmonics.
            return low * (1.0f + a * 0.18f * lowMidGain) + bodied + high;
        }

    private:
        OnePoleTPT lowSplit, midSplit;
        DcBlocker dc;
        OnePole comp;
    };

    // =======================================================================
    //  DENSITY
    //
    //  Fullness without width.  A short, fixed, mono comb - one delay of a few
    //  milliseconds summed back in phase - thickens the midrange the way a
    //  doubled take does, and because it is identical on both channels it adds
    //  nothing to the stereo image and nothing to the mono difference signal.
    //
    //  The delay is deliberately short and fixed: modulating it would make it a
    //  chorus, and a chorus is the one thing a centred MASS bass must not be.
    // =======================================================================
    class DensityEngine
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            length = juce::jlimit (2, kMaxDelay - 2, (int) (sampleRate * 0.0043));
            tone.setCutoff (2600.0f, sampleRate);
            reset();
        }

        void reset() noexcept
        {
            buffer.fill (0.0f);
            writeIndex = 0;
            tone.reset();
        }

        forcedinline float process (float x, float amount) noexcept
        {
            buffer[(size_t) writeIndex] = x;

            const int readIndex = (writeIndex - length + kMaxDelay) % kMaxDelay;
            writeIndex = (writeIndex + 1) % kMaxDelay;

            if (amount <= 0.0005f)
                return x;

            const float a = juce::jlimit (0.0f, 1.0f, amount);

            // The copy is darkened before it is summed.  An undarkened copy
            // combs the top end into a metallic ring; darkened, the comb lives
            // where body lives and the top stays clean.
            const float copy = tone.lowpass (buffer[(size_t) readIndex]);

            // Gain compensation: two correlated copies sum to more than one, so
            // the sum is scaled by what it actually adds up to.
            const float mixed = x + copy * a * 0.85f;

            return mixed / (1.0f + a * 0.55f);
        }

    private:
        static constexpr int kMaxDelay = 512;

        std::array<float, (size_t) kMaxDelay> buffer {};
        int writeIndex = 0, length = 190;
        OnePoleTPT tone;
    };

    // =======================================================================
    //  VOICE SATURATOR
    //
    //  Post-filter finishing.  Four curves, chosen so that they differ in
    //  character rather than in amount, and all four are gain-compensated so
    //  that turning Saturation up does not simply turn the voice up.
    // =======================================================================
    class VoiceSaturator
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            dc.prepare (sampleRate);
            pre.setCutoff (240.0f, sampleRate);
            reset();
        }

        void reset() noexcept { dc.reset(); pre.reset(); }

        /** @param mode 0 SOFT, 1 WARM, 2 DENSE, 3 EDGE */
        forcedinline float process (float x, float amount, int mode, SaturationBias bias) noexcept
        {
            if (amount <= 0.0005f)
                return x;

            const float a = juce::jlimit (0.0f, 1.0f, amount);

            const float biasAmount = (bias == SaturationBias::clean) ? 0.0f
                                   : (bias == SaturationBias::warm)  ? 0.12f
                                                                     : 0.25f;

            const float drive = 1.0f + a * (mode == 3 ? 5.5f : 3.0f);
            float y;

            switch (mode)
            {
                case 1:
                    // WARM: asymmetric, so it is second-harmonic led, and the
                    // low end is driven harder than the top - which is what
                    // makes tape sound warm rather than merely distorted.
                    y = tanhFast (x * drive + biasAmount + pre.lowpass (x) * a * 0.5f)
                        - tanhFast (biasAmount);
                    break;

                case 2:
                    // DENSE: two stages, the second gentler, which compresses
                    // the middle of the transfer curve and fills the gaps
                    // between partials.
                    y = tanhFast (tanhFast (x * drive) * (1.0f + a));
                    break;

                case 3:
                    // EDGE: a hard-knee fold, the only curve here with a
                    // discontinuous derivative, which is where the bite is.
                    {
                        const float d = x * drive;
                        y = juce::jlimit (-1.0f, 1.0f, d)
                            - 0.22f * a * (d - juce::jlimit (-1.0f, 1.0f, d));
                    }
                    break;

                default:
                    y = tanhFast (x * drive);
                    break;
            }

            y = dc.process (y);

            // Compensation: divide out the small-signal gain the drive added,
            // then blend, so Saturation changes tone at constant loudness.
            return lerp (x, y / drive * (1.0f + a * 0.85f), a);
        }

    private:
        DcBlocker dc;
        OnePoleTPT pre;
    };
}
