/*
    QUALITY  -  ECO | STUDIO | ULTRA.

    The parameter used to be accepted, stored, recalled and ignored: ULTRA
    behaved exactly like STUDIO, which section 01 of the operating contract
    forbids.  ULTRA now raises the voice's nonlinear core from 2x to 4x.

    What is worth testing here is not that the enum round-trips - the state
    tests already cover that - but the four things that could quietly be wrong:

      * STUDIO and ECO must render EXACTLY what they rendered before the tier
        existed.  A change that improves ULTRA by moving every existing patch is
        a regression wearing a feature's clothes, so the reference is a digest
        of the raw sample bits taken from the build before the work started.
      * ULTRA must actually be cleaner, by a margin, measured rather than
        asserted.  The measurement is NacarBench's: energy that is not near a
        harmonic of the fundamental, in dB relative to the energy that is.
      * turning the knob under a sounding note must not click, crash or emit a
        non-finite sample.
      * every tier must be stable at every sample rate the product claims.
*/

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <chrono>
#include <vector>

#include "../Source/Plugin/ParameterRegistry.h"
#include "../Source/Audio/Sources/Synth/SynthEngine.h"
#include "../Source/Audio/Sources/Synth/SynthVoice.h"
#include "../Source/Audio/Sources/Synth/Halfband.h"

#include "TestHost.h"

namespace
{
    constexpr int kEco = 0, kStudio = 1, kUltra = 2;

    // -----------------------------------------------------------------------
    //  A digest of the raw sample bits.
    //
    //  Not of rounded values: the claim being tested is bit-identity, and a
    //  digest that tolerates a least significant bit would not test it.  FNV-1a
    //  over the four bytes of every float, in channel then sample order.
    // -----------------------------------------------------------------------
    juce::uint64 digestOf (const juce::AudioBuffer<float>& b) noexcept
    {
        juce::uint64 h = 1469598103934665603ull;

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const auto* d = b.getReadPointer (ch);

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                juce::uint32 bits;
                std::memcpy (&bits, &d[i], sizeof (bits));

                for (int k = 0; k < 4; ++k)
                {
                    h ^= (juce::uint64) ((bits >> (k * 8)) & 0xFFu);
                    h *= 1099511628211ull;
                }
            }
        }

        return h;
    }

    /** Magnitude spectrum of the mono sum, averaged over frames.

        Blackman-Harris rather than Hann, for the reason NacarBench gives: Hann
        leaks -31 dB into the bins between the harmonics, which would put the
        measurement floor above the thing being measured. */
    std::vector<float> averageSpectrum (const juce::AudioBuffer<float>& b, int fftOrder)
    {
        const int fftSize = 1 << fftOrder;
        const int hop = fftSize / 2;

        juce::dsp::FFT fft (fftOrder);
        juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
                                                    juce::dsp::WindowingFunction<float>::blackmanHarris);

        std::vector<float> accum ((size_t) fftSize / 2, 0.0f);
        std::vector<float> scratch ((size_t) fftSize * 2, 0.0f);
        int frames = 0;

        for (int start = 0; start + fftSize <= b.getNumSamples(); start += hop)
        {
            std::fill (scratch.begin(), scratch.end(), 0.0f);

            for (int i = 0; i < fftSize; ++i)
            {
                float s = 0.0f;

                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    s += b.getReadPointer (ch)[start + i];

                scratch[(size_t) i] = s / (float) juce::jmax (1, b.getNumChannels());
            }

            window.multiplyWithWindowingTable (scratch.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (scratch.data());

            for (size_t i = 0; i < accum.size(); ++i)
                accum[i] += scratch[i];

            ++frames;
        }

        if (frames > 0)
            for (auto& v : accum)
                v /= (float) frames;

        return accum;
    }

    /** Energy off the harmonic series, in dB relative to the energy on it.
        This is NacarBench's aliasingFigure(), tolerance and exclusions
        included, so the two tools report the same number for the same render. */
    float aliasingFigure (const std::vector<float>& spectrum, double sampleRate, double f0)
    {
        if (spectrum.empty() || f0 <= 0.0)
            return -144.0f;

        const double binHz = sampleRate / (double) (spectrum.size() * 2);
        const double tolerance = juce::jmax (binHz * 5.0, f0 * 0.03);

        double harmonic = 0.0, inharmonic = 0.0;

        for (size_t i = 1; i < spectrum.size(); ++i)
        {
            const double f = (double) i * binHz;

            if (f < f0 * 0.5 || f > sampleRate * 0.47)
                continue;

            const double nearest = std::round (f / f0) * f0;
            const double e = (double) spectrum[i] * spectrum[i];

            if (nearest > 0.0 && std::abs (f - nearest) <= tolerance)
                harmonic += e;
            else
                inharmonic += e;
        }

        if (harmonic < 1.0e-18)
            return -144.0f;

        return (float) (10.0 * std::log10 (juce::jmax (1.0e-18, inharmonic / harmonic)));
    }

    // -----------------------------------------------------------------------
    //  Patches
    // -----------------------------------------------------------------------

    /** Everything that could put energy off the harmonic series for a
        legitimate reason is switched off: one oscillator, no unison, no detune,
        no variation, no drift.  Whatever is inharmonic afterwards is aliasing. */
    void cleanPatch (TestHost& h)
    {
        auto& r = h.registry;

        r.setFromUI (PID::synthCharacter, 1.0f);      // HAZE
        r.setFromUI (PID::oscAWave, 2.0f);            // SAW
        r.setFromUI (PID::oscALevel, 0.9f);
        r.setFromUI (PID::oscBLevel, 0.0f);
        r.setFromUI (PID::oscCLevel, 0.0f);
        r.setFromUI (PID::subLevel, 0.0f);
        r.setFromUI (PID::noiseLevel, 0.0f);
        r.setFromUI (PID::oscAUnison, 1.0f);
        r.setFromUI (PID::oscADetune, 0.0f);
        r.setFromUI (PID::oscSync, 0.0f);
        r.setFromUI (PID::oscFmAmount, 0.0f);
        r.setFromUI (PID::oscPmAmount, 0.0f);
        r.setFromUI (PID::oscRingMod, 0.0f);
        r.setFromUI (PID::bodyAmount, 0.0f);
        r.setFromUI (PID::densityAmount, 0.0f);
        r.setFromUI (PID::postSaturation, 0.0f);
        r.setFromUI (PID::voiceVariation, 0.0f);
        r.setFromUI (PID::driftAmount, 0.0f);
        r.setFromUI (PID::filterKeyTrack, 0.0f);
        r.setFromUI (PID::filterEnvAmount, 0.0f);
        r.setFromUI (PID::filterVelAmount, 0.0f);
        r.setFromUI (PID::ampAttack, 0.005f);
        r.setFromUI (PID::ampDecay, 0.2f);
        r.setFromUI (PID::ampSustain, 1.0f);
        r.setFromUI (PID::ampRelease, 0.2f);
        r.setFromUI (PID::voiceMode, 0.0f);           // POLY
        r.setFromUI (PID::synthWidth, 1.0f);
        r.setFromUI (PID::highWidth, 1.0f);

        r.setFromUI (PID::preFilterDrive, 0.6f);
        r.setFromUI (PID::filterModel, 0.0f);         // MASS ladder
        r.setFromUI (PID::filterType, 0.0f);          // LP
        r.setFromUI (PID::filterCutoff, 12000.0f);
        r.setFromUI (PID::filterResonance, 0.35f);
        r.setFromUI (PID::filterDrive, 0.7f);
    }

    /** A SINE into a hard-knee fold.
        A sine has no inharmonic energy of its own at any note or sample rate,
        so every bin off the harmonic series in this render was made by the
        nonlinear core - which is the only thing oversampling can change.  Run
        the same measurement on a saw and the oscillator's own aliasing, an
        order of magnitude larger, hides the entire effect. */
    void coreOnlyPatch (TestHost& h)
    {
        cleanPatch (h);

        auto& r = h.registry;

        r.setFromUI (PID::oscAWave, 0.0f);            // SINE
        r.setFromUI (PID::oscALevel, 1.0f);
        r.setFromUI (PID::preFilterDrive, 0.0f);
        r.setFromUI (PID::filterModel, 1.0f);         // HAZE, undriven
        r.setFromUI (PID::filterCutoff, 20000.0f);
        r.setFromUI (PID::filterResonance, 0.0f);
        r.setFromUI (PID::filterDrive, 0.0f);
        r.setFromUI (PID::postSaturation, 1.0f);
        r.setFromUI (PID::postSatMode, 3.0f);         // EDGE
    }

    /** The whole voice path lit up, for the regression digest: wavetable,
        unison, body, density, ring, PM, both filters, saturation, variation and
        drift.  If anything at all moved, this render says so. */
    void richPatch (TestHost& h)
    {
        auto& r = h.registry;

        r.setFromUI (PID::synthCharacter, 2.0f);      // MASS
        r.setFromUI (PID::oscAWave, 4.0f);            // WAVETABLE
        r.setFromUI (PID::oscAWtTable, 3.0f);
        r.setFromUI (PID::oscAWtPos, 0.62f);
        r.setFromUI (PID::oscALevel, 0.8f);
        r.setFromUI (PID::oscAUnison, 4.0f);
        r.setFromUI (PID::oscADetune, 0.35f);
        r.setFromUI (PID::oscBWave, 2.0f);
        r.setFromUI (PID::oscBLevel, 0.6f);
        r.setFromUI (PID::oscBFine, 7.0f);
        r.setFromUI (PID::oscCWave, 1.0f);
        r.setFromUI (PID::oscCLevel, 0.25f);
        r.setFromUI (PID::subLevel, 0.4f);
        r.setFromUI (PID::subHarmonics, 0.3f);
        r.setFromUI (PID::noiseLevel, 0.12f);
        r.setFromUI (PID::oscRingMod, 0.15f);
        r.setFromUI (PID::oscPmAmount, 0.2f);
        r.setFromUI (PID::bodyAmount, 0.45f);
        r.setFromUI (PID::bodyTilt, 0.2f);
        r.setFromUI (PID::densityAmount, 0.5f);
        r.setFromUI (PID::preFilterDrive, 0.45f);
        r.setFromUI (PID::postSaturation, 0.4f);
        r.setFromUI (PID::postSatMode, 2.0f);
        r.setFromUI (PID::filterModel, 0.0f);
        r.setFromUI (PID::filterType, 0.0f);
        r.setFromUI (PID::filterCutoff, 2200.0f);
        r.setFromUI (PID::filterResonance, 0.4f);
        r.setFromUI (PID::filterDrive, 0.55f);
        r.setFromUI (PID::filterEnvAmount, 0.5f);
        r.setFromUI (PID::filter2On, 1.0f);
        r.setFromUI (PID::filter2Type, 3.0f);
        r.setFromUI (PID::filter2Cutoff, 900.0f);
        r.setFromUI (PID::filter2Res, 0.5f);
        r.setFromUI (PID::filter2Mix, 0.6f);
        r.setFromUI (PID::voiceVariation, 0.5f);
        r.setFromUI (PID::driftAmount, 0.4f);
        r.setFromUI (PID::ampAttack, 0.01f);
        r.setFromUI (PID::ampDecay, 0.5f);
        r.setFromUI (PID::ampSustain, 0.7f);
        r.setFromUI (PID::ampRelease, 0.4f);
        r.setFromUI (PID::voiceMode, 0.0f);
        r.setFromUI (PID::synthWidth, 1.0f);
        r.setFromUI (PID::highWidth, 1.0f);
    }

    // -----------------------------------------------------------------------
    //  Rendering
    // -----------------------------------------------------------------------
    struct Render
    {
        juce::AudioBuffer<float> audio;
        bool  allFinite = true;
        float peak = 0.0f;
        double seconds = 0.0;
    };

    /** Renders `lengthSeconds` with `numNotes` struck at the start.
        `flipQualityAt`, if it is not negative, switches the quality parameter
        that many seconds in - from the message thread's point of view, between
        two processBlock calls, which is exactly how a host delivers a knob. */
    Render render (TestHost& host, double sampleRate, int quality, int midiNote,
                   double lengthSeconds, int numNotes = 1, int blockSize = 512,
                   double flipQualityAt = -1.0, int flipTo = kUltra,
                   double newNoteAt = -1.0, int newNote = 60)
    {
        host.registry.setFromUI (PID::qualityMode, (float) quality);

        SynthEngine synth;
        synth.prepare (sampleRate, blockSize, 2);

        const int numBlocks = (int) (sampleRate * lengthSeconds) / blockSize;

        Render out;
        out.audio.setSize (2, numBlocks * blockSize);
        out.audio.clear();

        juce::AudioBuffer<float> block (2, blockSize);

        const int flipBlock = flipQualityAt >= 0.0
                                ? (int) (sampleRate * flipQualityAt) / blockSize : -1;
        const int noteBlock = newNoteAt >= 0.0
                                ? (int) (sampleRate * newNoteAt) / blockSize : -1;

        const auto t0 = std::chrono::steady_clock::now();

        for (int b = 0; b < numBlocks; ++b)
        {
            if (b == flipBlock)
                host.registry.setFromUI (PID::qualityMode, (float) flipTo);

            block.clear();
            juce::MidiBuffer midi;

            if (b == 0)
                for (int n = 0; n < numNotes; ++n)
                    midi.addEvent (juce::MidiMessage::noteOn (1, midiNote + n * 3, 0.9f), n);

            if (b == noteBlock)
                midi.addEvent (juce::MidiMessage::noteOn (1, newNote, 0.9f), 0);

            synth.process (block, midi, host.registry, 120.0);

            for (int ch = 0; ch < 2; ++ch)
                out.audio.copyFrom (ch, b * blockSize, block, ch, 0, blockSize);
        }

        out.seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < out.audio.getNumSamples(); ++i)
            {
                const float x = out.audio.getReadPointer (ch)[i];

                if (! std::isfinite (x))
                    out.allFinite = false;

                out.peak = juce::jmax (out.peak, std::abs (x));
            }

        return out;
    }

    float measureAliasing (const Render& r, double sampleRate, int midiNote)
    {
        // The sustained middle only.  An attack and a release are broadband by
        // nature and would put energy in every bin.
        const int n = r.audio.getNumSamples();
        const int a = n * 20 / 100, z = n * 60 / 100;

        if (z - a <= (1 << 12))
            return -144.0f;

        juce::AudioBuffer<float> mid (2, z - a);

        for (int ch = 0; ch < 2; ++ch)
            mid.copyFrom (ch, 0, r.audio, ch, a, z - a);

        const double f0 = 440.0 * std::pow (2.0, (midiNote - 69) / 12.0);

        return aliasingFigure (averageSpectrum (mid, 12), sampleRate, f0);
    }

    /** The largest step between neighbouring samples, over a window.  A filter
        whose state has been cleared under a sounding note shows up here as a
        step an order of magnitude past anything the signal does on its own. */
    float largestStep (const juce::AudioBuffer<float>& b, int from, int to) noexcept
    {
        float worst = 0.0f;

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const auto* d = b.getReadPointer (ch);

            for (int i = juce::jmax (1, from); i < juce::jmin (b.getNumSamples(), to); ++i)
                worst = juce::jmax (worst, std::abs (d[i] - d[i - 1]));
        }

        return worst;
    }
}

// ===========================================================================
struct QualityTests : juce::UnitTest
{
    QualityTests() : juce::UnitTest ("Quality", "nacar") {}

    void runTest() override
    {
        using namespace nacar::synth;

        // -------------------------------------------------------------------
        beginTest ("the 4x cascade reconstructs what goes into it");
        {
            // Same question as the 2x round-trip test, asked differently.
            //
            // The 2x pair's delay is a whole number of samples, so that test
            // aligns the two signals by searching integer delays.  A 4x cascade
            // cannot: its second stage contributes `centre` samples at four
            // times the rate, `centre` is always odd for a halfband, and the
            // total therefore always lands on a half sample.  A constant half
            // sample of delay is not an error - it is latency - but it makes an
            // integer alignment read as 24 dB of residual on a signal that is
            // in fact perfect.
            //
            // So the tone is fitted instead of shifted: least squares for the
            // amplitude and phase of a sinusoid at the input frequency, and
            // then everything the fit does not explain is the error.  A swapped
            // polyphase pair or a tap read one place out is not a delay and
            // does not disappear into the fit.
            constexpr int n = 8192;
            const double rate = 48000.0;

            for (double toneHz : { 100.0, 1000.0, 6000.0 })
            {
                VoiceUpsampler4x up;
                VoiceDownsampler4x down;
                up.reset();
                down.reset();

                const double w = juce::MathConstants<double>::twoPi * toneHz / rate;

                double sc = 0.0, ss = 0.0, cc = 0.0, sy = 0.0, cy = 0.0, yy = 0.0;
                int counted = 0;

                for (int i = 0; i < n; ++i)
                {
                    float q[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    up.process ((float) std::sin (w * (double) i), q);

                    const double y = (double) down.process (q);

                    // Skip the first quarter: the converters start empty and
                    // their own impulse response is not the thing being tested.
                    if (i < n / 4)
                        continue;

                    const double c = std::cos (w * (double) i);
                    const double s2 = std::sin (w * (double) i);

                    cc += c * c;  ss += s2 * s2;  sc += s2 * c;
                    cy += c * y;  sy += s2 * y;   yy += y * y;
                    ++counted;
                }

                const double det = cc * ss - sc * sc;
                const double a = (cy * ss - sy * sc) / det;      // cosine part
                const double b = (sy * cc - cy * sc) / det;      // sine part

                const double explained = a * cy + b * sy;
                const double residual = juce::jmax (0.0, yy - explained);

                const double gain = std::sqrt (a * a + b * b);
                const double db = 10.0 * std::log10 (juce::jmax (1.0e-14,
                                        residual / juce::jmax (1.0e-14, explained)));

                logMessage ("    4x round trip at " + juce::String (toneHz, 0)
                            + " Hz: gain " + juce::String (gain, 4)
                            + ", residual " + juce::String (db, 1) + " dB");

                expect (db < -60.0, "the 4x round-trip residual at "
                                        + juce::String (toneHz, 0) + " Hz is "
                                        + juce::String (db, 1)
                                        + " dB, so the cascade is wrong somewhere");

                expect (std::abs (gain - 1.0) < 0.02,
                        "the 4x round trip is not unity in its passband at "
                            + juce::String (toneHz, 0) + " Hz: " + juce::String (gain, 4));

                juce::ignoreUnused (counted);
            }
        }

        // -------------------------------------------------------------------
        beginTest ("the 4x downsampler rejects what the 2x one cannot represent");
        {
            // A tone at 1.6 times the voice's Nyquist, presented at the 4x
            // rate.  Nothing of it may survive: it is above what the 2x core
            // could hold, let alone the output.
            constexpr int n = 8192;
            const double voiceRate = 48000.0;
            const double toneHz = voiceRate * 0.8;      // 1.6 x the output Nyquist

            VoiceDownsampler4x down;
            down.reset();

            double energy = 0.0;

            for (int i = 0; i < n; ++i)
            {
                float q[4];

                for (int k = 0; k < 4; ++k)
                    q[k] = std::sin ((float) (juce::MathConstants<double>::twoPi * toneHz
                                                  * (double) (i * 4 + k) / (voiceRate * 4.0)));

                const float y = down.process (q);

                if (i > n / 4)
                    energy += (double) y * y;
            }

            const double rms = std::sqrt (energy / (double) (n - n / 4));
            const double db = 20.0 * std::log10 (juce::jmax (1.0e-12, rms / 0.7071));

            logMessage ("    4x out-of-band rejection: " + juce::String (db, 1) + " dB");

            expect (db < -60.0, "an out-of-band tone survived the 4x downsampler at "
                                    + juce::String (db, 1) + " dB");
        }

        // -------------------------------------------------------------------
        beginTest ("ULTRA raises the core's rate, and only at a note's start");
        {
            SynthVoice voice;
            voice.prepare (48000.0, 0);

            SynthBlockParams p;
            p.sampleRate = 48000.0;
            p.numSamples = 64;
            p.ampSustain = 1.0f;
            p.ampRelease = 1.0f;

            expectEquals (voice.getOversamplingFactor(), 2,
                          "a freshly prepared voice is not at the rate ECO and STUDIO use");

            p.quality = Quality::studio;
            voice.noteOn (60, 0.9f, false, p);
            expectEquals (voice.getOversamplingFactor(), 2, "STUDIO moved the core's rate");

            p.quality = Quality::eco;
            voice.noteOn (60, 0.9f, false, p);
            expectEquals (voice.getOversamplingFactor(), 2, "ECO moved the core's rate");

            // The knob moves while the note is sounding: the voice must not
            // follow it, because following it means clearing every filter under
            // a note that can hear it.
            p.quality = Quality::ultra;
            voice.updateBlock (p);
            expect (voice.isActive(), "the test's own note stopped sounding");
            expectEquals (voice.getOversamplingFactor(), 2,
                          "the core's rate changed under a sounding note");

            // A legato continuation is the same note, so it must not either.
            voice.noteOn (62, 0.9f, true, p);
            expectEquals (voice.getOversamplingFactor(), 2,
                          "a legato continuation restarted the core at a new rate");

            // The next note played is where it takes effect.
            voice.noteOn (64, 0.9f, false, p);
            expectEquals (voice.getOversamplingFactor(), 4, "ULTRA did not reach the core");

            p.quality = Quality::studio;
            voice.noteOn (65, 0.9f, false, p);
            expectEquals (voice.getOversamplingFactor(), 2, "the core stayed at ULTRA's rate");
        }

        // -------------------------------------------------------------------
        beginTest ("STUDIO and ECO render exactly what they rendered before ULTRA existed");
        {
            // These digests were taken from the build immediately before the
            // ULTRA work started, with the same patches, sample rate, block
            // size and note. They are of the raw float bits, so the tolerance
            // is zero: if this fails, either STUDIO moved or the toolchain's
            // floating point did, and the two are told apart by rebuilding the
            // previous commit and re-running this test.
            struct Case { const char* what; void (*patch) (TestHost&); int quality;
                          int note; int notes; juce::uint64 digest; };

            const Case cases[] = {
                { "saw into a driven ladder, ECO",    cleanPatch, kEco,    96, 1,
                  14792485042347847519ull },
                { "saw into a driven ladder, STUDIO", cleanPatch, kStudio, 96, 1,
                  14792485042347847519ull },
                { "the whole voice path, ECO",        richPatch,  kEco,    45, 3,
                  11959610688099770841ull },
                { "the whole voice path, STUDIO",     richPatch,  kStudio, 45, 3,
                  8682626877724404651ull }
            };

            for (const auto& c : cases)
            {
                TestHost host;
                c.patch (host);

                const auto r = render (host, 48000.0, c.quality, c.note, 2.0, c.notes);

                expect (r.allFinite, juce::String (c.what) + " emitted a non-finite sample");
                expectEquals (juce::String (digestOf (r.audio)), juce::String (c.digest),
                              juce::String (c.what) + " no longer renders what it used to");
            }
        }

        // -------------------------------------------------------------------
        beginTest ("ULTRA measurably reduces the core's aliasing");
        {
            // Sine in, hard-knee fold, top of the keyboard.  The source has no
            // inharmonic energy of its own, so the whole figure belongs to the
            // core.
            struct Point { double rate; int note; };
            const Point points[] = { { 44100.0, 96 }, { 48000.0, 96 }, { 48000.0, 84 },
                                     { 96000.0, 96 } };

            for (const auto& pt : points)
            {
                TestHost studioHost, ultraHost;
                coreOnlyPatch (studioHost);
                coreOnlyPatch (ultraHost);

                const auto s = render (studioHost, pt.rate, kStudio, pt.note, 2.0);
                const auto u = render (ultraHost,  pt.rate, kUltra,  pt.note, 2.0);

                expect (s.allFinite && u.allFinite, "a non-finite sample in the aliasing probe");

                const float sDb = measureAliasing (s, pt.rate, pt.note);
                const float uDb = measureAliasing (u, pt.rate, pt.note);

                const juce::String where (juce::String (pt.rate, 0) + " Hz, MIDI "
                                              + juce::String (pt.note));

                logMessage ("    " + where + ": STUDIO " + juce::String (sDb, 2)
                            + " dB, ULTRA " + juce::String (uDb, 2)
                            + " dB, improvement " + juce::String (sDb - uDb, 2) + " dB");

                expect (uDb < sDb - 6.0f,
                        "ULTRA is only " + juce::String (sDb - uDb, 2)
                            + " dB cleaner than STUDIO at " + where
                            + ", which is not worth what it costs");
            }
        }

        // -------------------------------------------------------------------
        beginTest ("turning the knob under a sounding note changes nothing about it");
        {
            // The strongest form of "it does not click": a note that is already
            // sounding must be BIT-IDENTICAL whether or not the quality
            // parameter moved underneath it.  A voice that re-prepared its
            // filters mid-note could not pass this however gentle the fade.
            for (double rate : { 44100.0, 48000.0, 96000.0 })
            {
                TestHost quiet, flipped;
                cleanPatch (quiet);
                cleanPatch (flipped);

                const auto a = render (quiet,   rate, kStudio, 57, 2.0);
                const auto b = render (flipped, rate, kStudio, 57, 2.0, 1, 512, 1.0, kUltra);

                expect (b.allFinite, "the flipped render emitted a non-finite sample at "
                                         + juce::String (rate, 0) + " Hz");
                expectEquals (juce::String (digestOf (b.audio)), juce::String (digestOf (a.audio)),
                              "a quality change disturbed a sounding note at "
                                  + juce::String (rate, 0) + " Hz");
            }
        }

        // -------------------------------------------------------------------
        beginTest ("a note started at the new quality mixes with one still at the old");
        {
            // The case the design allows and has to survive: the knob moves,
            // the held note keeps the 2x core, and the next note gets the 4x
            // one, so both are sounding at once through one output.
            for (double rate : { 44100.0, 48000.0, 192000.0 })
            {
                TestHost host;
                cleanPatch (host);

                const auto r = render (host, rate, kStudio, 45, 3.0, 1, 512,
                                       0.5, kUltra, 1.0, 64);

                expect (r.allFinite, "a non-finite sample with two core rates sounding at "
                                         + juce::String (rate, 0) + " Hz");
                expect (r.peak < 1.5f, "the mix ran past +3.5 dBFS at "
                                           + juce::String (rate, 0) + " Hz: "
                                           + juce::String (r.peak));

                // Nothing may step at the moment the knob moved.  The new note
                // an octave later is an attack and is allowed to.
                const int flip = (int) (rate * 0.5);
                const float atFlip = largestStep (r.audio, flip - 256, flip + 256);
                const float typical = largestStep (r.audio, flip - 4096, flip - 512);

                expect (atFlip <= typical * 2.0f + 1.0e-4f,
                        "the output stepped by " + juce::String (atFlip)
                            + " where it normally steps by " + juce::String (typical)
                            + " at " + juce::String (rate, 0) + " Hz");
            }
        }

        // -------------------------------------------------------------------
        beginTest ("every quality is finite and bounded at every supported rate");
        {
            for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
                for (int q : { kEco, kStudio, kUltra })
                {
                    TestHost host;
                    richPatch (host);

                    // Four notes, a resonant driven ladder, a creative filter
                    // and full drift: the corner, not the middle.
                    const auto r = render (host, rate, q, 45, 1.5, 4);

                    const juce::String where (juce::String (rate, 0) + " Hz, quality "
                                                  + juce::String (q));

                    expect (r.allFinite, "non-finite output at " + where);
                    expect (r.peak > 1.0e-4f, "silence at " + where);
                    expect (r.peak < 1.5f, "output ran past +3.5 dBFS at " + where
                                               + ": " + juce::String (r.peak));
                }
        }

        // -------------------------------------------------------------------
        beginTest ("ECO stays cheaper than STUDIO");
        {
            // ECO's saving is the wavetable interpolation, so it is only
            // visible on a patch that uses one - and it must still be a
            // different render, or the tier is as dead as ULTRA was.
            TestHost ecoHost, studioHost;
            richPatch (ecoHost);
            richPatch (studioHost);

            const auto e = render (ecoHost,    48000.0, kEco,    45, 1.0, 4);
            const auto s = render (studioHost, 48000.0, kStudio, 45, 1.0, 4);

            expect (digestOf (e.audio) != digestOf (s.audio),
                    "ECO renders exactly what STUDIO does, so it is not doing less work");

            // Best of three, because a shared build machine is not a quiet one.
            // The margin is deliberately loose: this is here to catch ECO
            // becoming more expensive than STUDIO, not to police percentages.
            double eco = 1.0e9, studio = 1.0e9;

            for (int i = 0; i < 3; ++i)
            {
                eco    = juce::jmin (eco,    render (ecoHost,    48000.0, kEco,    45, 1.0, 4).seconds);
                studio = juce::jmin (studio, render (studioHost, 48000.0, kStudio, 45, 1.0, 4).seconds);
            }

            logMessage ("    1 s of four voices: ECO " + juce::String (eco, 4)
                        + " s, STUDIO " + juce::String (studio, 4) + " s");

            expect (eco < studio * 2.0,
                    "ECO now costs " + juce::String (eco / juce::jmax (1.0e-9, studio), 2)
                        + " times STUDIO, so it is no longer the cheap tier");
        }
    }
};

static QualityTests qualityTests;
