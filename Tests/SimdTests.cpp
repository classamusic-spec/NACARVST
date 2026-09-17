/*
    SIMD oscillator tests.

    UnisonOscillatorBank exists to render a unison group four or eight
    sub-voices at a time instead of one at a time.  It is only allowed to exist
    if it renders the SAME AUDIO, because 300 factory presets were voiced
    against the scalar oscillator and an optimisation that moves any of them is
    an undisclosed revoice, not an optimisation.

    So the tests here are almost all one test asked in different ways: drive
    AnalogOscillator and UnisonOscillatorBank from identical state with
    identical arguments, and compare what comes out, sample by sample.

    The comparison is `==`, not a tolerance.  The two paths agree exactly, and
    the only slack allowed is the sign of an exact zero: the vector floor comes
    back from truncation as +0.0 where std::floor returns -0.0, which changes
    no sample's value and no sum it takes part in.  A test that accepted a
    tolerance would not notice the day one of these stopped being the other.

    What is deliberately covered:

      * every waveform, including WAVETABLE, which takes the bank's scalar path
      * every unison count from 1 to 8 - the counts that are not a multiple of
        the vector width are where a tail-handling mistake would hide, and a
        5-voice group has to be exactly as right as an 8-voice one
      * detune and spread settings across their real ranges
      * phase modulation, pulse-width modulation and hard sync, because all
        three take branches inside the oscillator that the vector path had to
        turn into selects
      * 44.1, 48, 88.2, 96 and 192 kHz
      * aliasing, measured the way NacarBench measures it, before and after
*/

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <limits>
#include <vector>

#include "../Source/Audio/Sources/Synth/AnalogOscillator.h"
#include "../Source/Audio/Sources/Synth/UnisonEngine.h"
#include "../Source/Audio/Sources/Synth/WavetableBank.h"

using namespace nacar;
using namespace nacar::synth;

namespace
{
    /** Equal as audio: the same number, or both NaN.

        Written without `==` because the build treats -Wfloat-equal as an
        error, and with the NaN cases spelled out because two unordered
        comparisons are both false whether one operand is a NaN or both are -
        which would quietly turn this into a test that passes on anything. */
    bool sameSample (float a, float b) noexcept
    {
        const bool na = std::isnan (a), nb = std::isnan (b);

        if (na || nb)
            return na && nb;

        return ! (a < b) && ! (b < a);
    }

    const char* waveName (Waveform w) noexcept
    {
        switch (w)
        {
            case Waveform::sine:      return "SINE";
            case Waveform::triangle:  return "TRIANGLE";
            case Waveform::saw:       return "SAW";
            case Waveform::pulse:     return "PULSE";
            case Waveform::wavetable: return "WAVETABLE";
        }

        return "?";
    }

    /** One scalar unison group: the array of oscillators and the loop over it
        that SynthVoice::renderOscillator runs today. */
    struct ScalarGroup
    {
        AnalogOscillator sub [kMaxUnison];

        void prepare (double sampleRate) noexcept
        {
            for (auto& o : sub)
                o.prepare (sampleRate);
        }

        void reset (int i, float startPhase) noexcept { sub[i].reset (startPhase); }

        void render (Waveform wave, int n, float f0, float invSr,
                     const float* detuneRatio, float pulseWidth,
                     float phaseMod, float syncFrac, const WavetableContext& wt,
                     const float* panL, const float* panLStep,
                     const float* panR, const float* panRStep, float fi,
                     float& mono, float& left, float& right) noexcept
        {
            for (int v = 0; v < n; ++v)
            {
                const float inc = f0 * detuneRatio[v] * invSr;
                const float y = sub[v].process (wave, inc, pulseWidth, phaseMod, syncFrac, wt);

                mono  += y;
                left  += y * (panL[v] + panLStep[v] * fi);
                right += y * (panR[v] + panRStep[v] * fi);
            }
        }
    };

    /** The same group, rendered by the bank. */
    struct VectorGroup
    {
        UnisonOscillatorBank bank;

        void prepare (double sampleRate) noexcept { bank.prepare (sampleRate); }
        void reset (int i, float startPhase) noexcept { bank.reset (i, startPhase); }

        void render (Waveform wave, int n, float f0, float invSr,
                     const float* detuneRatio, float pulseWidth,
                     float phaseMod, float syncFrac, const WavetableContext& wt,
                     const float* panL, const float* panLStep,
                     const float* panR, const float* panRStep, float fi,
                     float& mono, float& left, float& right) noexcept
        {
            for (int v = 0; v < kUnisonPadded; ++v)
            {
                bank.detuneRatio[v] = detuneRatio[v];
                bank.panL[v]        = panL[v];
                bank.panLStep[v]    = panLStep[v];
                bank.panR[v]        = panR[v];
                bank.panRStep[v]    = panRStep[v];
            }

            bank.render (wave, n, f0, invSr, pulseWidth, phaseMod, syncFrac, wt, fi,
                         mono, left, right);
        }
    };

    /** A unison group's worth of block constants, built the way
        SynthVoice::prepareOscBlock builds them. */
    struct GroupSetup
    {
        float detuneRatio [kUnisonPadded] {};
        float panL        [kUnisonPadded] {};
        float panR        [kUnisonPadded] {};
        float panLStep    [kUnisonPadded] {};
        float panRStep    [kUnisonPadded] {};
        float startPhase  [kUnisonPadded] {};

        void build (UnisonTopology topology, int n, float detuneNorm, float spread,
                    float detuneCentsMax, juce::uint32 seed) noexcept
        {
            UnisonLayout layout;
            UnisonEngine::buildLayout (layout, topology, n, detuneNorm, seed);

            for (int v = 0; v < kUnisonPadded; ++v)
            {
                detuneRatio[v] = 1.0f;
                panL[v] = panR[v] = 0.7f;
                panLStep[v] = panRStep[v] = 0.0f;
                startPhase[v] = 0.0f;
            }

            for (int v = 0; v < n; ++v)
            {
                detuneRatio[v] = centsToRatio (layout.detune[v] * detuneCentsMax);

                float l0, r0, l1, r1;
                panGains (juce::jlimit (-1.0f, 1.0f, layout.pan[v] * spread), l0, r0);
                panGains (juce::jlimit (-1.0f, 1.0f, layout.pan[v] * spread * 0.6f), l1, r1);

                panL[v] = l0;
                panR[v] = r0;
                panLStep[v] = (l1 - l0) / 256.0f;
                panRStep[v] = (r1 - r0) / 256.0f;

                startPhase[v] = layout.phase[v];
            }
        }
    };

    /** Peak level, in dB, of everything that is not a harmonic of f0.

        The same measurement NacarBench's ALIAS column makes: a Hann-windowed
        spectrum with every bin within a few of a harmonic excluded, so what is
        left is what folded down from above Nyquist. */
    float aliasingDb (const std::vector<float>& x, double sampleRate, float f0)
    {
        const int order = 13;
        const int fftSize = 1 << order;

        if ((int) x.size() < fftSize)
            return 0.0f;

        juce::dsp::FFT fft (order);

        std::vector<float> buf ((size_t) fftSize * 2, 0.0f);
        const size_t offset = x.size() - (size_t) fftSize;

        for (int i = 0; i < fftSize; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) (fftSize - 1));
            buf[(size_t) i] = x[offset + (size_t) i] * w;
        }

        fft.performFrequencyOnlyForwardTransform (buf.data());

        const double binHz = sampleRate / (double) fftSize;
        const int    bins  = fftSize / 2;

        float fundamental = 0.0f, alias = 0.0f;

        for (int b = 2; b < bins; ++b)
        {
            const double hz = (double) b * binHz;
            const double ratio = hz / (double) f0;
            const double nearest = std::round (ratio);
            const bool   harmonic = nearest >= 1.0 && std::abs (ratio - nearest) < (3.0 * binHz / (double) f0);

            const float mag = buf[(size_t) b];

            if (harmonic) fundamental = juce::jmax (fundamental, mag);
            else          alias       = juce::jmax (alias, mag);
        }

        if (fundamental <= 0.0f)
            return 0.0f;

        return juce::Decibels::gainToDecibels (alias / fundamental, -160.0f);
    }
}

// ===========================================================================
struct SimdTests : juce::UnitTest
{
    SimdTests() : juce::UnitTest ("SIMD oscillator", "nacar") {}

    struct Case
    {
        Waveform wave;
        int      count;
        float    detuneNorm;
        float    spread;
        float    pulseWidth;
        UnisonTopology topology;
    };

    /** Drives both paths through `numSamples` and reports the worst
        disagreement it saw, or -1 if there was none at all. */
    double compare (const Case& c, double sampleRate, int numSamples,
                    bool modulatePhase, bool modulateWidth, bool sync,
                    const WavetableBank* bank, juce::String& where)
    {
        GroupSetup setup;
        setup.build (c.topology, c.count, c.detuneNorm, c.spread, 24.0f, 0x51ED2701u);

        ScalarGroup scalar;
        VectorGroup vector;

        scalar.prepare (sampleRate);
        vector.prepare (sampleRate);

        for (int v = 0; v < kMaxUnison; ++v)
        {
            scalar.reset (v, setup.startPhase[v]);
            vector.reset (v, setup.startPhase[v]);
        }

        WavetableContext wt;
        wt.bank = bank;
        wt.family = 2;
        wt.framePosition = 0.37f;
        wt.hermitePhase = true;

        const float invSr = 1.0f / (float) sampleRate;
        const float f0    = 220.0f;

        double worst = -1.0;
        float masterPhase = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float fi = (float) (i % 256);

            // A phase-modulation term that sweeps the whole range the engine
            // can produce, including negative, so that the wrap either side of
            // zero is exercised rather than assumed.
            const float phaseMod = modulatePhase
                ? 1.7f * std::sin (2.0f * kPi * 3.1f * (float) i * invSr)
                : 0.0f;

            const float pw = modulateWidth
                ? juce::jlimit (0.02f, 0.98f, 0.5f + 0.46f * std::sin (2.0f * kPi * 0.7f * (float) i * invSr))
                : c.pulseWidth;

            float syncFrac = -1.0f;

            if (sync)
            {
                const float masterInc = f0 * 2.37f * invSr;
                const float next = masterPhase + masterInc;

                if (next >= 1.0f)
                    syncFrac = juce::jlimit (0.0f, 0.9999f, (next - 1.0f) / masterInc);

                masterPhase = next - std::floor (next);
            }

            // A pitch that moves, so the increment is never the same twice and
            // the BLEP branch is entered at a different phase every cycle.
            const float hz = f0 * (1.0f + 0.35f * std::sin (2.0f * kPi * 1.3f * (float) i * invSr));

            float sMono = 0.0f, sL = 0.0f, sR = 0.0f;
            float vMono = 0.0f, vL = 0.0f, vR = 0.0f;

            scalar.render (c.wave, c.count, hz, invSr, setup.detuneRatio, pw,
                           phaseMod, syncFrac, wt,
                           setup.panL, setup.panLStep, setup.panR, setup.panRStep, fi,
                           sMono, sL, sR);

            vector.render (c.wave, c.count, hz, invSr, setup.detuneRatio, pw,
                           phaseMod, syncFrac, wt,
                           setup.panL, setup.panLStep, setup.panR, setup.panRStep, fi,
                           vMono, vL, vR);

            const float pairs[3][2] = { { sMono, vMono }, { sL, vL }, { sR, vR } };
            const char* names[3] = { "mono", "left", "right" };

            for (int k = 0; k < 3; ++k)
            {
                if (! sameSample (pairs[k][0], pairs[k][1]))
                {
                    const double d = std::abs ((double) pairs[k][0] - (double) pairs[k][1]);

                    if (d > worst)
                    {
                        worst = d;
                        where = juce::String (waveName (c.wave)) + " x" + juce::String (c.count)
                              + " " + names[k] + " at sample " + juce::String (i)
                              + ": scalar " + juce::String (pairs[k][0], 9)
                              + " vector " + juce::String (pairs[k][1], 9);
                    }
                }
            }
        }

        return worst;
    }

    void runTest() override
    {
        const WavetableBank& bank = WavetableBank::shared();

        const Waveform waves[] = { Waveform::sine, Waveform::triangle,
                                   Waveform::saw, Waveform::pulse, Waveform::wavetable };

        // ------------------------------------------------------------------
        beginTest ("scalar and vector agree, every waveform, every unison count");
        {
            for (auto w : waves)
            {
                for (int n = 1; n <= kMaxUnison; ++n)
                {
                    Case c { w, n, 0.6f, 0.8f, 0.35f, UnisonTopology::wide };
                    juce::String where;

                    const double worst = compare (c, 48000.0, 4000, false, false, false,
                                                  &bank, where);

                    expect (worst < 0.0,
                            juce::String (waveName (w)) + " x" + juce::String (n)
                              + " disagrees: " + where);
                }
            }
        }

        // ------------------------------------------------------------------
        beginTest ("the tail of the vector loop is as right as the body");
        {
            // kVecWidth is 4 on SSE and 8 on AVX.  The counts that are not a
            // multiple of it are the ones a tail-handling mistake would show
            // up in, so they are asserted individually and by name.
            for (int n = 1; n <= kMaxUnison; ++n)
            {
                if (n % kVecWidth == 0)
                    continue;

                for (auto w : waves)
                {
                    Case c { w, n, 1.0f, 1.0f, 0.5f, UnisonTopology::cloud };
                    juce::String where;

                    const double worst = compare (c, 48000.0, 3000, true, true, false,
                                                  &bank, where);

                    expect (worst < 0.0,
                            "partial vector, " + juce::String (waveName (w))
                              + " x" + juce::String (n) + ": " + where);
                }
            }
        }

        // ------------------------------------------------------------------
        beginTest ("a spread of detune, spread and topology");
        {
            const UnisonTopology topologies[] = { UnisonTopology::tight, UnisonTopology::dense,
                                                  UnisonTopology::wide,  UnisonTopology::haze,
                                                  UnisonTopology::cloud };

            const float detunes[] = { 0.0f, 0.02f, 0.25f, 0.7f, 1.0f };
            const float spreads[] = { 0.0f, 0.5f, 1.0f };

            for (auto t : topologies)
                for (float d : detunes)
                    for (float s : spreads)
                    {
                        Case c { Waveform::saw, 7, d, s, 0.5f, t };
                        juce::String where;

                        const double worst = compare (c, 48000.0, 1500, false, false, false,
                                                      &bank, where);

                        expect (worst < 0.0, "detune " + juce::String (d)
                                               + " spread " + juce::String (s) + ": " + where);
                    }
        }

        // ------------------------------------------------------------------
        beginTest ("phase modulation, pulse-width modulation and hard sync");
        {
            for (auto w : waves)
            {
                Case c { w, 5, 0.5f, 0.7f, 0.5f, UnisonTopology::haze };

                juce::String a, b, d;

                expect (compare (c, 48000.0, 4000, true,  false, false, &bank, a) < 0.0,
                        "phase modulation: " + a);
                expect (compare (c, 48000.0, 4000, false, true,  false, &bank, b) < 0.0,
                        "pulse width modulation: " + b);
                expect (compare (c, 48000.0, 4000, true,  true,  true,  &bank, d) < 0.0,
                        "hard sync: " + d);
            }
        }

        // ------------------------------------------------------------------
        beginTest ("every sample rate the product claims");
        {
            const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };

            for (double sr : rates)
                for (auto w : waves)
                {
                    Case c { w, 8, 0.8f, 1.0f, 0.3f, UnisonTopology::wide };
                    juce::String where;

                    const double worst = compare (c, sr, 4000, true, true, false, &bank, where);

                    expect (worst < 0.0, juce::String (sr) + " Hz: " + where);
                }
        }

        // ------------------------------------------------------------------
        beginTest ("output is finite and bounded at every sample rate");
        {
            const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };

            for (double sr : rates)
                for (auto w : waves)
                {
                    GroupSetup setup;
                    setup.build (UnisonTopology::wide, 8, 0.9f, 1.0f, 24.0f, 0x9E3779B1u);

                    VectorGroup g;
                    g.prepare (sr);

                    for (int v = 0; v < kMaxUnison; ++v)
                        g.reset (v, setup.startPhase[v]);

                    WavetableContext wt;
                    wt.bank = &bank;
                    wt.family = 1;
                    wt.framePosition = 0.5f;

                    const float invSr = 1.0f / (float) sr;
                    float peak = 0.0f;
                    bool finite = true;

                    for (int i = 0; i < 20000; ++i)
                    {
                        float mono = 0.0f, l = 0.0f, r = 0.0f;

                        g.render (w, 8, 2000.0f, invSr, setup.detuneRatio, 0.5f,
                                  0.0f, -1.0f, wt,
                                  setup.panL, setup.panLStep, setup.panR, setup.panRStep,
                                  0.0f, mono, l, r);

                        finite = finite && std::isfinite (mono) && std::isfinite (l)
                                        && std::isfinite (r);
                        peak = juce::jmax (peak, std::abs (mono));
                    }

                    expect (finite, juce::String (waveName (w)) + " at " + juce::String (sr)
                                      + " Hz produced a non-finite sample");
                    expect (peak > 0.01f && peak < 40.0f,
                            juce::String (waveName (w)) + " at " + juce::String (sr)
                              + " Hz: peak " + juce::String (peak));
                }
        }

        // ------------------------------------------------------------------
        beginTest ("a NaN through phase modulation silences one sub-voice, not the group");
        {
            // The scalar oscillator's defence: a NaN read phase zeroes that
            // oscillator's state and returns 0 rather than poisoning the phase
            // for the rest of the session.  The vector path has to do the same,
            // through a mask rather than a branch.
            GroupSetup setup;
            setup.build (UnisonTopology::wide, 8, 0.5f, 1.0f, 24.0f, 0x1234u);

            ScalarGroup scalar;
            VectorGroup vector;

            scalar.prepare (48000.0);
            vector.prepare (48000.0);

            for (int v = 0; v < kMaxUnison; ++v)
            {
                scalar.reset (v, setup.startPhase[v]);
                vector.reset (v, setup.startPhase[v]);
            }

            WavetableContext wt;
            const float invSr = 1.0f / 48000.0f;
            const float nan = std::numeric_limits<float>::quiet_NaN();

            bool recovered = true;

            for (int i = 0; i < 600; ++i)
            {
                const float pm = (i == 300) ? nan : 0.0f;

                float sMono = 0.0f, sL = 0.0f, sR = 0.0f;
                float vMono = 0.0f, vL = 0.0f, vR = 0.0f;

                scalar.render (Waveform::saw, 8, 220.0f, invSr, setup.detuneRatio, 0.5f,
                               pm, -1.0f, wt, setup.panL, setup.panLStep,
                               setup.panR, setup.panRStep, 0.0f, sMono, sL, sR);

                vector.render (Waveform::saw, 8, 220.0f, invSr, setup.detuneRatio, 0.5f,
                               pm, -1.0f, wt, setup.panL, setup.panLStep,
                               setup.panR, setup.panRStep, 0.0f, vMono, vL, vR);

                if (i > 301)
                    recovered = recovered && sameSample (sMono, vMono)
                                          && std::isfinite (vMono);
            }

            expect (recovered, "the vector path did not recover from a NaN the way the scalar one does");
        }

        // ------------------------------------------------------------------
        beginTest ("aliasing is no worse than the scalar oscillator's");
        {
            // Measured the way NacarBench measures it, on the two waveforms
            // whose band limiting is a BLEP correction at a discontinuity -
            // the thing a vectorised path that dropped or misplaced a
            // correction would break first.  Sine has nothing to fold and
            // wavetable's limiting is the mip pyramid, so neither is informative
            // here; they are covered by the bit-exactness tests above.
            const float notes[] = { 72.0f, 84.0f, 96.0f };

            for (float note : notes)
                for (auto w : { Waveform::saw, Waveform::pulse, Waveform::triangle })
                {
                    const double sr = 48000.0;
                    const float hz = midiNoteToHz (note);
                    const float invSr = 1.0f / (float) sr;

                    GroupSetup setup;
                    setup.build (UnisonTopology::tight, 1, 0.0f, 0.0f, 24.0f, 7u);

                    ScalarGroup s;  s.prepare (sr);  s.reset (0, 0.0f);
                    VectorGroup v;  v.prepare (sr);  v.reset (0, 0.0f);

                    WavetableContext wt;

                    std::vector<float> sOut, vOut;
                    sOut.reserve (16384);
                    vOut.reserve (16384);

                    for (int i = 0; i < 16384; ++i)
                    {
                        float sm = 0.0f, sl = 0.0f, srr = 0.0f;
                        float vm = 0.0f, vl = 0.0f, vr = 0.0f;

                        s.render (w, 1, hz, invSr, setup.detuneRatio, 0.35f, 0.0f, -1.0f, wt,
                                  setup.panL, setup.panLStep, setup.panR, setup.panRStep,
                                  0.0f, sm, sl, srr);
                        v.render (w, 1, hz, invSr, setup.detuneRatio, 0.35f, 0.0f, -1.0f, wt,
                                  setup.panL, setup.panLStep, setup.panR, setup.panRStep,
                                  0.0f, vm, vl, vr);

                        sOut.push_back (sm);
                        vOut.push_back (vm);
                    }

                    const float sDb = aliasingDb (sOut, sr, hz);
                    const float vDb = aliasingDb (vOut, sr, hz);

                    expect (vDb <= sDb + 0.01f,
                            juce::String (waveName (w)) + " at note " + juce::String (note)
                              + ": scalar aliasing " + juce::String (sDb, 2)
                              + " dB, vector " + juce::String (vDb, 2) + " dB");
                }
        }

        // ------------------------------------------------------------------
        beginTest ("the vector helpers reproduce their scalar originals");
        {
            // The primitives, checked directly, so that a failure above can be
            // told apart from a failure here.
            alignas (64) float in [(size_t) kVecWidth] {};
            alignas (64) float out [(size_t) kVecWidth] {};

            juce::Random rng (20260917);
            bool sineOk = true, floorOk = true;

            for (int trial = 0; trial < 4000; ++trial)
            {
                for (int i = 0; i < kVecWidth; ++i)
                    in[(size_t) i] = (rng.nextFloat() - 0.5f) * 8.0f;

                const Vec v = Vec::fromRawArray (in);

                vSineTurns (v).copyToRawArray (out);

                for (int i = 0; i < kVecWidth; ++i)
                    sineOk = sineOk && sameSample (out[(size_t) i], sineTurns (in[(size_t) i]));

                vfloor (v).copyToRawArray (out);

                for (int i = 0; i < kVecWidth; ++i)
                    floorOk = floorOk && sameSample (out[(size_t) i], std::floor (in[(size_t) i]));
            }

            expect (sineOk, "vSineTurns does not match sineTurns");
            expect (floorOk, "vfloor does not match std::floor");
        }
    }
};

static SimdTests simdTests;
