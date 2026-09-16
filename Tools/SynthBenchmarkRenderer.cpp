/*
    NACAR synth benchmark renderer.

    Renders the synth offline and measures the properties the build
    specification makes hard requirements.  It cannot tell anyone whether NACAR
    sounds good - only a person listening can do that - but it can prove or
    disprove the measurable half of the quality gate:

      - aliasing at high notes under drive, sync and FM        (spec 11, 163)
      - mono compatibility of wide patches                      (spec 37, 43, 163)
      - low-frequency phase coherence and centring              (spec 38, 40, 160)
      - gain staging: no level jumps from unison                (spec 17, 148, 163)
      - DC offset and finite output under extremes              (spec 28, 43)
      - spectral balance: dark is not the same as dull          (spec 47)

    Usage:
        NacarBench [--out <dir>] [--rate <hz>] [--no-wav] [--chain]
                   [--cpu] [--attribute] [<patch-name-filter>]

        --chain      renders through the whole NACAR chain rather than the
                     synth alone.
        --attribute  renders one patch repeatedly, adding one chain stage at a
                     time, and reports what each stage did to the stereo image.
                     Use it when a mono retention figure fails: the table names
                     the stage instead of leaving it to inference.

    Writes one WAV per benchmark patch and a measurement table to stdout.
*/

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <functional>
#include <iomanip>
#include <iostream>

#include "Plugin/ParameterRegistry.h"
#include "Audio/Sources/Synth/SynthEngine.h"
#include "Audio/NacarEngine.h"

using namespace nacar;

// ===========================================================================
//  A minimal parameter host
// ===========================================================================
class BenchHost : public juce::AudioProcessor
{
public:
    BenchHost()
        : juce::AudioProcessor (BusesProperties()
                                    .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMETERS", ParameterRegistry::createLayout())
    {
        registry.attach (apvts);
    }

    void set (PID p, float realValue) { registry.setFromUI (p, realValue); }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "BenchHost"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
    ParameterRegistry registry;
};

// ===========================================================================
//  Measurements
// ===========================================================================
struct Measurements
{
    float peakDb        = -144.0f;
    float rmsDb         = -144.0f;
    float crestDb       = 0.0f;
    float dcOffset      = 0.0f;
    float centroidHz    = 0.0f;
    float rolloff85Hz   = 0.0f;
    float lowEnergyPct  = 0.0f;   // below 200 Hz
    float midEnergyPct  = 0.0f;   // 200 Hz - 4 kHz
    float highEnergyPct = 0.0f;   // above 4 kHz
    float monoRetainDb  = 0.0f;   // energy kept when summed to mono
    float lowCorrelation = 1.0f;  // L/R correlation below 150 Hz
    float aliasingDb    = -144.0f;// inharmonic energy relative to harmonic
    bool  aliasingValid = false;  // false when the figure would be meaningless
    bool  allFinite     = true;
};

/** Sums to mono and reports how much energy survives.  A wide patch that loses
    more than about 3 dB here will not translate to a club system. */
static float monoRetentionDb (const juce::AudioBuffer<float>& b)
{
    if (b.getNumChannels() < 2)
        return 0.0f;

    const auto* l = b.getReadPointer (0);
    const auto* r = b.getReadPointer (1);

    double stereo = 0.0, mono = 0.0;

    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double m = 0.5 * ((double) l[i] + (double) r[i]);
        stereo += (double) l[i] * l[i] + (double) r[i] * r[i];
        mono   += m * m * 2.0;
    }

    if (stereo < 1.0e-12)
        return 0.0f;

    return (float) (10.0 * std::log10 (juce::jmax (1.0e-12, mono / stereo)));
}

/** Pearson correlation of the two channels after a one-pole low-pass, which is
    what decides whether the bottom end survives a mono fold. */
static float lowBandCorrelation (const juce::AudioBuffer<float>& b, double sampleRate)
{
    if (b.getNumChannels() < 2)
        return 1.0f;

    const double cutoff = 150.0;
    const double a = std::exp (-2.0 * juce::MathConstants<double>::pi * cutoff / sampleRate);

    double zl = 0.0, zr = 0.0;
    double sll = 0.0, srr = 0.0, slr = 0.0;

    const auto* l = b.getReadPointer (0);
    const auto* r = b.getReadPointer (1);

    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        zl = (1.0 - a) * l[i] + a * zl;
        zr = (1.0 - a) * r[i] + a * zr;

        sll += zl * zl;
        srr += zr * zr;
        slr += zl * zr;
    }

    const double denom = std::sqrt (sll * srr);
    return denom < 1.0e-12 ? 1.0f : (float) (slr / denom);
}

/** Magnitude spectrum of the mono sum, Hann windowed, averaged over frames. */
static std::vector<float> averageSpectrum (const juce::AudioBuffer<float>& b,
                                           int fftOrder, int hop)
{
    const int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft (fftOrder);
    // Blackman-Harris, not Hann.  Hann's first sidelobe is -31 dB, so spectral
    // leakage from the harmonics themselves lands in the bins between them and
    // the aliasing figure can never read better than about -33 dB however clean
    // the oscillator is.  Blackman-Harris sidelobes are below -90 dB, which puts
    // the measurement floor well under anything the engine produces.
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

/** Energy that is not near a harmonic of the fundamental, in dB relative to the
    energy that is.  This is the aliasing figure: a naive saw at MIDI 96 scores
    badly here, a PolyBLEP one does not. */
static float aliasingFigure (const std::vector<float>& spectrum,
                             double sampleRate, double fundamentalHz)
{
    if (spectrum.empty() || fundamentalHz <= 0.0)
        return -144.0f;

    const double binHz = sampleRate / (double) (spectrum.size() * 2);

    // The tolerance has to be at least as wide as the analysis window's main
    // lobe, or a harmonic's own leakage is counted as aliasing.  Four-term
    // Blackman-Harris has an eight-bin main lobe, so four bins either side plus
    // a margin is the floor; above about 1 kHz the 3 % term takes over.
    const double tolerance = juce::jmax (binHz * 5.0, fundamentalHz * 0.03);

    double harmonic = 0.0, inharmonic = 0.0;

    for (size_t i = 1; i < spectrum.size(); ++i)
    {
        const double f = (double) i * binHz;

        if (f < fundamentalHz * 0.5 || f > sampleRate * 0.47)
            continue;

        const double nearest = std::round (f / fundamentalHz) * fundamentalHz;
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

static Measurements measure (const juce::AudioBuffer<float>& b, double sampleRate,
                             double fundamentalHz, bool wantAliasing)
{
    Measurements m;

    double sum = 0.0, sumSq = 0.0;
    juce::int64 n = 0;
    float peak = 0.0f;

    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const auto* d = b.getReadPointer (ch);

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float x = d[i];

            if (! std::isfinite (x))
                m.allFinite = false;

            peak = juce::jmax (peak, std::abs (x));
            sum += x;
            sumSq += (double) x * x;
            ++n;
        }
    }

    if (n == 0)
        return m;

    const float rms = (float) std::sqrt (sumSq / (double) n);

    m.peakDb   = juce::Decibels::gainToDecibels (peak, -144.0f);
    m.rmsDb    = juce::Decibels::gainToDecibels (rms, -144.0f);
    m.crestDb  = m.peakDb - m.rmsDb;
    m.dcOffset = (float) (sum / (double) n);

    m.monoRetainDb   = monoRetentionDb (b);
    m.lowCorrelation = lowBandCorrelation (b, sampleRate);

    // Spectrum is taken from the sustained middle of the note only.  The
    // attack and the release are broadband by nature, and averaging them in
    // would put energy in every bin and make a clean oscillator look as though
    // it were aliasing.  Peak, RMS, DC and the mono figures above use the whole
    // render, because those are about the note as a whole.
    const int fftOrder = 12;
    const int sustainStart = b.getNumSamples() * 20 / 100;
    const int sustainEnd   = b.getNumSamples() * 60 / 100;

    juce::AudioBuffer<float> sustained;
    const juce::AudioBuffer<float>* spectrumSource = &b;

    if (sustainEnd - sustainStart > (1 << fftOrder))
    {
        sustained.setSize (b.getNumChannels(), sustainEnd - sustainStart);

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            sustained.copyFrom (ch, 0, b, ch, sustainStart, sustainEnd - sustainStart);

        spectrumSource = &sustained;
    }

    const auto spectrum = averageSpectrum (*spectrumSource, fftOrder, 1 << (fftOrder - 1));

    if (! spectrum.empty())
    {
        const double binHz = sampleRate / (double) (spectrum.size() * 2);

        double total = 0.0, weighted = 0.0, low = 0.0, mid = 0.0, high = 0.0;

        for (size_t i = 1; i < spectrum.size(); ++i)
        {
            const double f = (double) i * binHz;
            const double e = (double) spectrum[i] * spectrum[i];

            total    += e;
            weighted += e * f;

            if (f < 200.0)        low  += e;
            else if (f < 4000.0)  mid  += e;
            else                  high += e;
        }

        if (total > 1.0e-18)
        {
            m.centroidHz    = (float) (weighted / total);
            m.lowEnergyPct  = (float) (100.0 * low  / total);
            m.midEnergyPct  = (float) (100.0 * mid  / total);
            m.highEnergyPct = (float) (100.0 * high / total);

            double running = 0.0;
            for (size_t i = 1; i < spectrum.size(); ++i)
            {
                running += (double) spectrum[i] * spectrum[i];

                if (running >= total * 0.85)
                {
                    m.rolloff85Hz = (float) ((double) i * binHz);
                    break;
                }
            }
        }

        if (wantAliasing)
        {
            m.aliasingDb = aliasingFigure (spectrum, sampleRate, fundamentalHz);
            m.aliasingValid = true;
        }
    }

    return m;
}

// ===========================================================================
//  Benchmark patches
//
//  These are the twenty engineering benchmarks the specification requires
//  before mass preset creation (spec sections 62, 135).  They are not presets:
//  they exist to prove the engine can reach each corner of its own range.
// ===========================================================================
struct Benchmark
{
    const char* name;
    const char* family;
    int   midiNote;
    int   chordNotes;        ///< 1 for a single note, 3-4 for a chord
    float velocity;
    double seconds;
    std::function<void (BenchHost&)> setup;

    /** Whether the inharmonic-energy figure means anything for this patch.

        It only does for a single note played by a single, undetuned,
        unmodulated oscillator.  Detuned unison voices are deliberately not at
        harmonics of the nominal fundamental; so are FM sidebands, a hard-synced
        spectrum, and every note of a chord except the root.  Reporting the
        figure for those patches would be measuring the patch, not the
        oscillator, and a large number would look like a defect when it is the
        sound working as designed. */
    bool measureAliasing = false;
};

static void initPatch (BenchHost& h)
{
    // The INIT patch from spec section 60: one saw, subtle Body, a good filter,
    // a musical amp envelope.  Every benchmark starts here and changes only
    // what it needs to, so a difference in the table is attributable.
    h.set (PID::oscAWave, 2.0f);           // SAW
    h.set (PID::oscALevel, 0.85f);
    h.set (PID::oscBLevel, 0.0f);
    h.set (PID::oscCLevel, 0.0f);
    h.set (PID::subLevel, 0.0f);
    h.set (PID::noiseLevel, 0.0f);
    h.set (PID::oscAUnison, 1.0f);
    h.set (PID::oscBUnison, 1.0f);
    h.set (PID::bodyAmount, 0.25f);
    h.set (PID::densityAmount, 0.0f);
    h.set (PID::preFilterDrive, 0.1f);
    h.set (PID::postSaturation, 0.08f);
    h.set (PID::filterModel, 1.0f);        // HAZE
    h.set (PID::filterType, 0.0f);         // LP
    h.set (PID::filterCutoff, 9000.0f);
    h.set (PID::filterResonance, 0.1f);
    h.set (PID::ampAttack, 0.004f);
    h.set (PID::ampDecay, 0.6f);
    h.set (PID::ampSustain, 0.8f);
    h.set (PID::ampRelease, 0.4f);
    h.set (PID::voiceMode, 0.0f);          // POLY
    h.set (PID::synthWidth, 1.0f);
}

static std::vector<Benchmark> makeBenchmarks()
{
    std::vector<Benchmark> b;

    // -- Reese basses -----------------------------------------------------
    b.push_back ({ "reese_submerged", "REESE", 33, 1, 1.0f, 3.0, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 2.0f);              // MASS
        h.set (PID::oscAWave, 2.0f);  h.set (PID::oscALevel, 0.8f);
        h.set (PID::oscBWave, 2.0f);  h.set (PID::oscBLevel, 0.8f);
        h.set (PID::oscBFine, 9.0f);                    // the Reese interval
        h.set (PID::oscCWave, 4.0f);  h.set (PID::oscCLevel, 0.18f);
        h.set (PID::subWave, 0.0f);   h.set (PID::subLevel, 0.55f);
        h.set (PID::subHarmonics, 0.25f);
        h.set (PID::bodyAmount, 0.55f);
        h.set (PID::densityAmount, 0.5f);
        h.set (PID::filterModel, 0.0f);                 // MASS ladder
        h.set (PID::filterCutoff, 900.0f);
        h.set (PID::filterResonance, 0.2f);
        h.set (PID::filterDrive, 0.35f);
        h.set (PID::lowMonoFreq, 150.0f);
        h.set (PID::highWidth, 1.3f);
        h.set (PID::voiceMode, 1.0f);                   // MONO
    }});

    b.push_back ({ "reese_clean_digital", "REESE", 36, 1, 1.0f, 3.0, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 0.0f);              // MIRAGE
        h.set (PID::oscAWave, 4.0f);  h.set (PID::oscAWtTable, 1.0f);
        h.set (PID::oscBWave, 4.0f);  h.set (PID::oscBWtTable, 6.0f);
        h.set (PID::oscBLevel, 0.7f); h.set (PID::oscBFine, 6.0f);
        h.set (PID::subLevel, 0.5f);
        h.set (PID::bodyAmount, 0.35f);
        h.set (PID::filterModel, 1.0f);
        h.set (PID::filterType, 3.0f);                  // NOTCH
        h.set (PID::filterCutoff, 700.0f);
        h.set (PID::voiceMode, 1.0f);
    }});

    // -- Sub and mono basses ----------------------------------------------
    b.push_back ({ "sub_pure", "BASS", 28, 1, 1.0f, 2.5, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 2.0f);
        h.set (PID::oscALevel, 0.15f);
        h.set (PID::subLevel, 1.0f);
        h.set (PID::subHarmonics, 0.35f);
        h.set (PID::filterCutoff, 400.0f);
        h.set (PID::voiceMode, 1.0f);
    }});

    b.push_back ({ "mass_mono_bass", "BASS", 38, 1, 1.0f, 2.5, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 2.0f);
        h.set (PID::oscAWave, 3.0f);  h.set (PID::oscAPulseWidth, 0.35f);
        h.set (PID::oscBWave, 2.0f);  h.set (PID::oscBLevel, 0.6f);
        h.set (PID::subLevel, 0.6f);
        h.set (PID::bodyAmount, 0.6f);
        h.set (PID::preFilterDrive, 0.45f);
        h.set (PID::filterModel, 0.0f);
        h.set (PID::filterCutoff, 1400.0f);
        h.set (PID::filterResonance, 0.25f);
        h.set (PID::ampAttack, 0.001f);
        h.set (PID::ampDecay, 0.25f);
        h.set (PID::ampSustain, 0.55f);
        h.set (PID::voiceMode, 2.0f);                   // LEGATO
        h.set (PID::glideTime, 0.06f);
    }});

    // -- Poly synths -------------------------------------------------------
    b.push_back ({ "haze_poly", "POLY", 52, 4, 0.8f, 3.5, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 1.0f);              // HAZE
        h.set (PID::oscAUnison, 4.0f); h.set (PID::oscADetune, 0.3f);
        h.set (PID::oscBUnison, 3.0f); h.set (PID::oscBLevel, 0.6f);
        h.set (PID::oscBFine, -8.0f);
        h.set (PID::voiceVariation, 0.5f);
        h.set (PID::driftAmount, 0.4f);
        h.set (PID::bodyAmount, 0.4f);
        h.set (PID::filterCutoff, 4200.0f);
        h.set (PID::ampAttack, 0.05f);
    }});

    b.push_back ({ "mirage_poly", "POLY", 57, 4, 0.8f, 3.5, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 0.0f);
        h.set (PID::oscAWave, 4.0f); h.set (PID::oscAWtTable, 0.0f);
        h.set (PID::oscAUnison, 5.0f); h.set (PID::oscASpread, 0.8f);
        h.set (PID::oscBWave, 4.0f); h.set (PID::oscBWtTable, 6.0f);
        h.set (PID::oscBLevel, 0.5f);
        h.set (PID::filterCutoff, 8000.0f);
        h.set (PID::highWidth, 1.4f);
    }});

    // -- Keys --------------------------------------------------------------
    b.push_back ({ "mysterious_key", "KEYS", 60, 3, 0.75f, 3.0, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 1.0f);
        h.set (PID::oscAWave, 1.0f);                    // TRIANGLE
        h.set (PID::oscBWave, 4.0f); h.set (PID::oscBLevel, 0.45f);
        h.set (PID::oscPmAmount, 0.25f);
        h.set (PID::noiseType, 3.0f); h.set (PID::noiseLevel, 0.1f);
        h.set (PID::noiseAttackOnly, 0.8f);
        h.set (PID::ampAttack, 0.002f);
        h.set (PID::ampDecay, 1.4f);
        h.set (PID::ampSustain, 0.25f);
        h.set (PID::filterCutoff, 5200.0f);
        h.set (PID::filterEnvAmount, 0.45f);
    }});

    b.push_back ({ "warm_key", "KEYS", 55, 3, 0.6f, 3.0, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 1.0f);
        h.set (PID::oscAWave, 1.0f);
        h.set (PID::oscBWave, 2.0f); h.set (PID::oscBLevel, 0.35f);
        h.set (PID::oscBOctave, -1.0f);
        h.set (PID::bodyAmount, 0.5f);
        h.set (PID::filterCutoff, 3000.0f);
        h.set (PID::ampDecay, 2.0f);
        h.set (PID::ampSustain, 0.3f);
    }});

    // -- Plucks ------------------------------------------------------------
    b.push_back ({ "haunted_pluck", "PLUCK", 64, 1, 0.9f, 2.0, [] (BenchHost& h)
    {
        h.set (PID::oscAWave, 2.0f);
        h.set (PID::noiseType, 4.0f); h.set (PID::noiseLevel, 0.3f);
        h.set (PID::noiseAttackOnly, 1.0f);
        h.set (PID::ampAttack, 0.0008f);
        h.set (PID::ampDecay, 0.35f);
        h.set (PID::ampSustain, 0.0f);
        h.set (PID::filterCutoff, 3000.0f);
        h.set (PID::filterEnvAmount, 0.8f);
        h.set (PID::env1Decay, 0.12f);
        h.set (PID::env1Sustain, 0.0f);
    }});

    b.push_back ({ "mass_pluck", "PLUCK", 45, 1, 1.0f, 2.0, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 2.0f);
        h.set (PID::oscAWave, 3.0f);
        h.set (PID::subLevel, 0.4f);
        h.set (PID::bodyAmount, 0.65f);
        h.set (PID::ampAttack, 0.0005f);
        h.set (PID::ampDecay, 0.22f);
        h.set (PID::ampSustain, 0.0f);
        h.set (PID::filterModel, 0.0f);
        h.set (PID::filterCutoff, 2200.0f);
        h.set (PID::filterEnvAmount, 0.7f);
    }});

    // -- Leads -------------------------------------------------------------
    b.push_back ({ "emotional_lead", "LEAD", 69, 1, 0.85f, 3.0, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 1.0f);
        h.set (PID::oscAUnison, 3.0f); h.set (PID::oscADetune, 0.18f);
        h.set (PID::bodyAmount, 0.45f);
        h.set (PID::filterCutoff, 6000.0f);
        h.set (PID::voiceMode, 2.0f);
        h.set (PID::glideTime, 0.09f);
        h.set (PID::vibratoDepth, 0.3f);
    }});

    b.push_back ({ "dark_digital_lead", "LEAD", 62, 1, 1.0f, 3.0, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 0.0f);
        h.set (PID::oscAWave, 4.0f); h.set (PID::oscAWtTable, 1.0f);
        h.set (PID::oscSync, 0.5f);
        h.set (PID::oscFmAmount, 0.3f);
        h.set (PID::preFilterDrive, 0.5f);
        h.set (PID::filterModel, 0.0f);
        h.set (PID::filterCutoff, 2600.0f);
        h.set (PID::filterResonance, 0.3f);
        h.set (PID::voiceMode, 1.0f);
    }});

    // -- Pads --------------------------------------------------------------
    b.push_back ({ "atmospheric_pad_a", "PAD", 48, 4, 0.55f, 5.0, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 1.0f);
        h.set (PID::oscAUnison, 6.0f); h.set (PID::oscADetune, 0.35f);
        h.set (PID::oscASpread, 0.8f);
        h.set (PID::oscBWave, 4.0f); h.set (PID::oscBLevel, 0.4f);
        h.set (PID::oscBUnison, 4.0f);
        h.set (PID::noiseType, 3.0f); h.set (PID::noiseLevel, 0.07f);
        h.set (PID::driftAmount, 0.55f);
        h.set (PID::voiceVariation, 0.6f);
        h.set (PID::ampAttack, 0.8f);
        h.set (PID::ampRelease, 3.0f);
        h.set (PID::filterCutoff, 3400.0f);
        h.set (PID::highWidth, 1.5f);
    }});

    b.push_back ({ "atmospheric_pad_b", "PAD", 43, 4, 0.5f, 5.0, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 1.0f);
        h.set (PID::oscAWave, 4.0f); h.set (PID::oscAWtTable, 7.0f);
        h.set (PID::oscAUnison, 5.0f);
        h.set (PID::oscBWave, 4.0f); h.set (PID::oscBWtTable, 5.0f);
        h.set (PID::oscBLevel, 0.5f); h.set (PID::oscBOctave, 1.0f);
        h.set (PID::bodyAmount, 0.5f);
        h.set (PID::filterCutoff, 2400.0f);
        h.set (PID::ampAttack, 1.2f);
        h.set (PID::ampRelease, 4.0f);
    }});

    b.push_back ({ "atmospheric_pad_c", "PAD", 36, 3, 0.5f, 5.0, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 2.0f);
        h.set (PID::oscAUnison, 4.0f);
        h.set (PID::subLevel, 0.4f);
        h.set (PID::bodyAmount, 0.6f);
        h.set (PID::densityAmount, 0.6f);
        h.set (PID::filterModel, 0.0f);
        h.set (PID::filterCutoff, 1600.0f);
        h.set (PID::ampAttack, 1.5f);
        h.set (PID::ampRelease, 5.0f);
        h.set (PID::lowMonoFreq, 160.0f);
    }});

    // -- Bells -------------------------------------------------------------
    b.push_back ({ "vibey_bell", "BELL", 72, 1, 0.9f, 4.0, [] (BenchHost& h)
    {
        h.set (PID::oscAWave, 0.0f);                    // SINE
        h.set (PID::oscBWave, 0.0f); h.set (PID::oscBLevel, 0.6f);
        h.set (PID::oscBSemi, 7.0f);
        h.set (PID::oscPmAmount, 0.65f);
        h.set (PID::oscRingMod, 0.3f);
        h.set (PID::ampAttack, 0.0008f);
        h.set (PID::ampDecay, 2.6f);
        h.set (PID::ampSustain, 0.0f);
        h.set (PID::filterCutoff, 12000.0f);
    }});

    b.push_back ({ "haunted_bell", "BELL", 67, 1, 0.7f, 4.0, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 0.0f);
        h.set (PID::oscAWave, 4.0f); h.set (PID::oscAWtTable, 3.0f);
        h.set (PID::oscBWave, 0.0f); h.set (PID::oscBSemi, 11.0f);
        h.set (PID::oscBLevel, 0.45f);
        h.set (PID::oscFmAmount, 0.4f);
        h.set (PID::ampDecay, 3.2f);
        h.set (PID::ampSustain, 0.0f);
    }});

    // -- Textures ----------------------------------------------------------
    b.push_back ({ "dark_digital_texture", "TEXTURE", 40, 2, 0.6f, 4.0, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 0.0f);
        h.set (PID::oscAWave, 4.0f); h.set (PID::oscAWtTable, 4.0f);
        h.set (PID::oscBWave, 4.0f); h.set (PID::oscBWtTable, 2.0f);
        h.set (PID::oscBLevel, 0.6f);
        h.set (PID::noiseType, 6.0f); h.set (PID::noiseLevel, 0.2f);
        h.set (PID::filter2On, 1.0f);
        h.set (PID::filter2Type, 4.0f);                 // COMB
        h.set (PID::driftAmount, 0.6f);
        h.set (PID::ampAttack, 0.4f);
    }});

    b.push_back ({ "warm_poly_texture", "TEXTURE", 50, 3, 0.55f, 4.0, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 1.0f);
        h.set (PID::oscAUnison, 7.0f); h.set (PID::oscADetune, 0.45f);
        h.set (PID::voiceVariation, 0.8f);
        h.set (PID::driftAmount, 0.7f);
        h.set (PID::bodyAmount, 0.55f);
        h.set (PID::filterCutoff, 2000.0f);
        h.set (PID::ampAttack, 0.9f);
    }});

    b.push_back ({ "cinematic", "TEXTURE", 31, 2, 0.7f, 6.0, [] (BenchHost& h)
    {
        h.set (PID::synthCharacter, 2.0f);
        h.set (PID::oscAWave, 2.0f); h.set (PID::oscAUnison, 6.0f);
        h.set (PID::oscBWave, 4.0f); h.set (PID::oscBWtTable, 7.0f);
        h.set (PID::oscBLevel, 0.6f); h.set (PID::oscBOctave, 2.0f);
        h.set (PID::subLevel, 0.6f);
        h.set (PID::bodyAmount, 0.7f);
        h.set (PID::densityAmount, 0.7f);
        h.set (PID::preFilterDrive, 0.4f);
        h.set (PID::filterModel, 0.0f);
        h.set (PID::filterCutoff, 1200.0f);
        h.set (PID::ampAttack, 1.8f);
        h.set (PID::ampRelease, 6.0f);
    }});

    // -- Oscillator anti-aliasing, isolated --------------------------------
    //
    //  One saw, one sub-voice, no detune, no modulation, filter wide open, at
    //  the top of the keyboard.  Everything that could put energy off the
    //  harmonic series for a legitimate reason is switched off, so whatever is
    //  left between the harmonics is aliasing and nothing else.
    auto pureOscillator = [] (BenchHost& h, float wave)
    {
        h.set (PID::oscAWave, wave);
        h.set (PID::oscALevel, 0.9f);
        h.set (PID::oscBLevel, 0.0f);
        h.set (PID::oscCLevel, 0.0f);
        h.set (PID::subLevel, 0.0f);
        h.set (PID::noiseLevel, 0.0f);
        h.set (PID::oscAUnison, 1.0f);
        h.set (PID::oscADetune, 0.0f);
        h.set (PID::oscSync, 0.0f);
        h.set (PID::oscFmAmount, 0.0f);
        h.set (PID::oscPmAmount, 0.0f);
        h.set (PID::oscRingMod, 0.0f);
        h.set (PID::bodyAmount, 0.0f);
        h.set (PID::densityAmount, 0.0f);
        h.set (PID::preFilterDrive, 0.0f);
        h.set (PID::postSaturation, 0.0f);
        h.set (PID::voiceVariation, 0.0f);
        h.set (PID::driftAmount, 0.0f);
        h.set (PID::filterCutoff, 20000.0f);
        h.set (PID::filterResonance, 0.0f);
        h.set (PID::filterKeyTrack, 0.0f);
        h.set (PID::filterEnvAmount, 0.0f);
        h.set (PID::filterVelAmount, 0.0f);
        h.set (PID::ampAttack, 0.005f);
        h.set (PID::ampSustain, 1.0f);
    };

    b.push_back ({ "alias_saw_c7", "PROBE", 96, 1, 1.0f, 2.0,
                   [pureOscillator] (BenchHost& h) { pureOscillator (h, 2.0f); }, true });

    b.push_back ({ "alias_pulse_c7", "PROBE", 96, 1, 1.0f, 2.0,
                   [pureOscillator] (BenchHost& h)
                   {
                       pureOscillator (h, 3.0f);
                       h.set (PID::oscAPulseWidth, 0.25f);
                   }, true });

    b.push_back ({ "alias_wavetable_c7", "PROBE", 96, 1, 1.0f, 2.0,
                   [pureOscillator] (BenchHost& h)
                   {
                       pureOscillator (h, 4.0f);
                       h.set (PID::oscAWtTable, 3.0f);      // METALLIC: the hardest one
                       h.set (PID::oscAWtPos, 0.8f);
                   }, true });

    // -- Which stage is actually aliasing ----------------------------------
    //
    //  The same pure saw, with one nonlinear stage switched on at a time.  The
    //  difference between these and alias_saw_c7 is that stage's contribution,
    //  which is the only way to know where oversampling would pay for itself.
    b.push_back ({ "alias_saw_body", "PROBE", 96, 1, 1.0f, 2.0,
                   [pureOscillator] (BenchHost& h)
                   {
                       pureOscillator (h, 2.0f);
                       h.set (PID::bodyAmount, 0.5f);
                   }, true });

    b.push_back ({ "alias_saw_drive", "PROBE", 96, 1, 1.0f, 2.0,
                   [pureOscillator] (BenchHost& h)
                   {
                       pureOscillator (h, 2.0f);
                       h.set (PID::preFilterDrive, 0.5f);
                   }, true });

    b.push_back ({ "alias_saw_sat", "PROBE", 96, 1, 1.0f, 2.0,
                   [pureOscillator] (BenchHost& h)
                   {
                       pureOscillator (h, 2.0f);
                       h.set (PID::postSaturation, 0.5f);
                   }, true });

    b.push_back ({ "alias_saw_filterdrive", "PROBE", 96, 1, 1.0f, 2.0,
                   [pureOscillator] (BenchHost& h)
                   {
                       pureOscillator (h, 2.0f);
                       h.set (PID::filterModel, 0.0f);      // MASS ladder
                       h.set (PID::filterDrive, 0.6f);
                       h.set (PID::filterResonance, 0.3f);
                   }, true });

    // -- Stability under everything at once --------------------------------
    //
    //  Spec section 11 asks for high notes under PWM, sync, FM, unison and
    //  drive together.  This patch is that test - but it is a stability and
    //  headroom probe, not an aliasing one, because sync and FM put energy off
    //  the harmonic series by design.
    b.push_back ({ "stress_everything", "PROBE", 100, 1, 1.0f, 2.0, [] (BenchHost& h)
    {
        h.set (PID::oscAWave, 2.0f);
        h.set (PID::oscBWave, 3.0f); h.set (PID::oscBLevel, 0.8f);
        h.set (PID::oscBPulseWidth, 0.2f);
        h.set (PID::oscAUnison, 6.0f); h.set (PID::oscADetune, 0.4f);
        h.set (PID::oscSync, 0.7f);
        h.set (PID::oscFmAmount, 0.5f);
        h.set (PID::preFilterDrive, 0.7f);
        h.set (PID::postSaturation, 0.6f);
        h.set (PID::filterCutoff, 20000.0f);
    }});

    // The INIT patch is the "does it sound premium" gate from specification
    // section 60, not an aliasing probe: voice variation and drift are both on
    // by default and both move the harmonics on purpose, so an inharmonic
    // energy figure would be measuring the patch rather than the oscillator.
    b.push_back ({ "init", "PROBE", 57, 1, 0.8f, 2.0, [] (BenchHost&) {}, false });

    return b;
}

// ===========================================================================
//  Rendering
// ===========================================================================
/** Renders through the whole instrument rather than the synth alone, so the
    same measurements can be taken of what a listener would actually hear. */
static juce::AudioBuffer<float> renderThroughChain (const Benchmark& bench,
                                                    double sampleRate, int blockSize,
                                                    const std::function<void (BenchHost&)>& after = {})
{
    BenchHost host;
    initPatch (host);

    if (bench.setup)
        bench.setup (host);

    // The attribution mode uses this to switch one stage off and re-measure,
    // which is the only way to say WHICH stage widened a patch rather than
    // guessing from the chain order.
    if (after)
        after (host);

    NacarEngine engine;
    engine.prepare (sampleRate, blockSize, 2);

    const int totalSamples = (int) (sampleRate * bench.seconds);
    juce::AudioBuffer<float> out (2, totalSamples);
    out.clear();

    juce::AudioBuffer<float> block (2, blockSize);

    const int releaseAt = (int) (totalSamples * 0.66);
    const int intervals[4] = { 0, 7, 15, 22 };

    TransportInfo transport;
    transport.bpm = 120.0;
    transport.playing = true;

    int position = 0;
    bool noteSent = false, releaseSent = false;

    while (position < totalSamples)
    {
        const int n = juce::jmin (blockSize, totalSamples - position);
        block.clear();
        block.setSize (2, n, false, false, true);

        juce::MidiBuffer midi;

        if (! noteSent)
        {
            for (int i = 0; i < juce::jmax (1, bench.chordNotes); ++i)
                midi.addEvent (juce::MidiMessage::noteOn (
                                   1, bench.midiNote + intervals[i % 4], bench.velocity), 0);

            noteSent = true;
        }

        if (! releaseSent && position + n > releaseAt)
        {
            for (int i = 0; i < juce::jmax (1, bench.chordNotes); ++i)
                midi.addEvent (juce::MidiMessage::noteOff (
                                   1, bench.midiNote + intervals[i % 4]),
                               juce::jmax (0, releaseAt - position));

            releaseSent = true;
        }

        engine.process (block, midi, host.registry, transport);

        transport.ppqPosition += (double) n / sampleRate * (transport.bpm / 60.0);

        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, position, block, ch, 0, n);

        position += n;
    }

    return out;
}

static juce::AudioBuffer<float> renderBenchmark (const Benchmark& bench,
                                                 double sampleRate, int blockSize)
{
    BenchHost host;
    initPatch (host);

    if (bench.setup)
        bench.setup (host);

    SynthEngine synth;
    synth.prepare (sampleRate, blockSize, 2);

    const int totalSamples = (int) (sampleRate * bench.seconds);
    juce::AudioBuffer<float> out (2, totalSamples);
    out.clear();

    juce::AudioBuffer<float> block (2, blockSize);

    // Hold for two thirds, then release, so the tail is in the measurement.
    const int releaseAt = (int) (totalSamples * 0.66);

    // A minor-ninth voicing: the harmonic language the specification asks the
    // factory content to favour (open fifths, minor 9 colours, modal ambiguity).
    const int intervals[4] = { 0, 7, 15, 22 };

    int position = 0;
    bool noteSent = false, releaseSent = false;

    while (position < totalSamples)
    {
        const int n = juce::jmin (blockSize, totalSamples - position);
        block.clear();
        block.setSize (2, n, false, false, true);

        juce::MidiBuffer midi;

        if (! noteSent)
        {
            for (int i = 0; i < juce::jmax (1, bench.chordNotes); ++i)
                midi.addEvent (juce::MidiMessage::noteOn (
                                   1, bench.midiNote + intervals[i % 4], bench.velocity), 0);

            noteSent = true;
        }

        if (! releaseSent && position + n > releaseAt)
        {
            for (int i = 0; i < juce::jmax (1, bench.chordNotes); ++i)
                midi.addEvent (juce::MidiMessage::noteOff (
                                   1, bench.midiNote + intervals[i % 4]),
                               juce::jmax (0, releaseAt - position));

            releaseSent = true;
        }

        synth.process (block, midi, host.registry, 120.0);

        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, position, block, ch, 0, n);

        position += n;
    }

    return out;
}

static void writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer,
                      double sampleRate)
{
    file.deleteFile();

    juce::WavAudioFormat format;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

    if (stream == nullptr)
        return;

    std::unique_ptr<juce::AudioFormatWriter> writer (
        format.createWriterFor (stream.get(), sampleRate,
                                (unsigned int) buffer.getNumChannels(), 24, {}, 0));

    if (writer == nullptr)
        return;

    stream.release();                               // the writer owns it now
    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

// ===========================================================================
//  CPU
//
//  Specification section 162 asks for CPU measurements alongside the benchmark
//  renders.  This is a realtime factor, not a percentage: 40x means the engine
//  renders forty seconds of audio per second of wall clock, so one instance
//  costs about 1/40th of the core it ran on.
// ===========================================================================
/** Cost per block, for the synth alone or for the whole chain.

    Both figures matter and they answer different questions.  The synth-only
    number says what the voice core costs and scales with polyphony; the chain
    number adds the chain engines, whose cost is per block rather than per
    voice, so it barely moves with voice count and dominates at low polyphony.
    A user holding one pad voice pays almost entirely for the chain. */
static void measureCpu (double sampleRate, int blockSize, bool throughChain)
{
    struct Case { const char* name; int voices; int unison; int character; };

    const std::array<Case, 6> cases {{
        { "1 voice, no unison",      1, 1, 1 },
        { "1 voice, 8x unison",      1, 8, 1 },
        { "8 voices, no unison",     8, 1, 1 },
        { "8 voices, 4x unison",     8, 4, 1 },
        { "16 voices, 4x unison",   16, 4, 1 },
        { "32 voices, 8x unison",   32, 8, 0 }
    }};

    std::cout << "\nCPU  (" << (throughChain ? "whole chain" : "synth only") << ")\n"
              << "  " << sampleRate << " Hz, block " << blockSize << "\n\n"
              << std::left << std::setw (26) << "  CASE"
              << std::right << std::setw (12) << "REALTIME"
              << std::setw (12) << "ONE CORE" << "\n"
              << std::string (62, '-') << "\n";

    for (const auto& c : cases)
    {
        BenchHost host;
        initPatch (host);
        host.set (PID::synthCharacter, (float) c.character);
        host.set (PID::polyphony, (float) c.voices);
        host.set (PID::oscAUnison, (float) c.unison);
        host.set (PID::oscBUnison, (float) c.unison);
        host.set (PID::oscBLevel, 0.6f);
        host.set (PID::oscCLevel, 0.3f);
        host.set (PID::subLevel, 0.4f);
        host.set (PID::noiseLevel, 0.1f);
        host.set (PID::ampSustain, 1.0f);
        host.set (PID::ampRelease, 8.0f);

        SynthEngine synth;
        NacarEngine chain;

        if (throughChain) chain.prepare (sampleRate, blockSize, 2);
        else              synth.prepare (sampleRate, blockSize, 2);

        TransportInfo transport;
        transport.bpm = 120.0;
        transport.playing = true;

        juce::AudioBuffer<float> block (2, blockSize);

        auto render = [&] (juce::MidiBuffer& midi)
        {
            block.clear();

            if (throughChain) chain.process (block, midi, host.registry, transport);
            else              synth.process (block, midi, host.registry, 120.0);

            transport.ppqPosition += (double) blockSize / sampleRate * (transport.bpm / 60.0);
        };

        // Fill every voice, then render three seconds of all of them sounding.
        {
            juce::MidiBuffer midi;

            for (int v = 0; v < c.voices; ++v)
                midi.addEvent (juce::MidiMessage::noteOn (1, 36 + v, 0.9f), 0);

            render (midi);
        }

        const int blocks = (int) (sampleRate * 3.0 / blockSize);
        const double start = juce::Time::getMillisecondCounterHiRes();

        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            render (midi);
        }

        const double elapsed = (juce::Time::getMillisecondCounterHiRes() - start) * 0.001;
        const double audio   = (double) (blocks * blockSize) / sampleRate;
        const double factor  = elapsed > 0.0 ? audio / elapsed : 0.0;

        std::cout << std::left << "  " << std::setw (24) << c.name
                  << std::right << std::fixed << std::setprecision (1)
                  << std::setw (11) << factor << "x"
                  << std::setw (11) << (factor > 0.0 ? 100.0 / factor : 0.0) << "%"
                  << "\n";
    }

    std::cout << "\n  Measured on whatever core this ran on, single threaded,\n"
                 "  with every voice sounding at once - which is the worst case,\n"
                 "  not a typical one.\n";

    if (! throughChain)
        std::cout << "  This is the voice core alone.  Add --chain for the figure a\n"
                     "  user actually pays, which includes every chain engine.\n";
}

// ===========================================================================
//  Stage attribution
// ===========================================================================
/** Mid and side energy, which is what mono retention is actually made of.

    monoRetention = 10*log10(M2 / (M2 + S2)), so a figure worse than -3 dB is
    not "wide" - it is a signal whose side energy exceeds its mid energy, and
    naming the stage that put it there is the whole point of this mode. */
struct MidSide
{
    double mid = 0.0, side = 0.0;

    float retentionDb() const
    {
        const double total = mid + side;
        return total < 1.0e-18 ? 0.0f
                               : (float) (10.0 * std::log10 (juce::jmax (1.0e-12, mid / total)));
    }

    float sideOverMidDb() const
    {
        return mid < 1.0e-18 ? 99.0f
                             : (float) (10.0 * std::log10 (juce::jmax (1.0e-12, side / mid)));
    }
};

static MidSide midSideEnergy (const juce::AudioBuffer<float>& b)
{
    MidSide ms;

    if (b.getNumChannels() < 2)
        return ms;

    const auto* l = b.getReadPointer (0);
    const auto* r = b.getReadPointer (1);

    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double m = 0.5 * ((double) l[i] + (double) r[i]);
        const double s = 0.5 * ((double) l[i] - (double) r[i]);
        ms.mid  += m * m * 2.0;
        ms.side += s * s * 2.0;
    }

    return ms;
}

/** One line of the attribution table: a chain configuration and what it did to
    the stereo image. */
struct Attribution
{
    const char* label;
    std::function<void (BenchHost&)> configure;
};

static void runAttribution (const juce::String& filter, double sampleRate)
{
    // Cumulative, in chain order, so each row's delta is that stage's own
    // contribution rather than a comparison against a patch nothing has
    // touched.  SHADOW is off by default and is included only so the table
    // says so explicitly.
    const Attribution rows[] =
    {
        { "synth only (no chain)", {} },   // handled specially below
        { "chain, all width off",  [] (BenchHost& h)
          {
              h.set (PID::retroOn, 0.0f);   h.set (PID::fxFilterOn, 0.0f);
              h.set (PID::spaceOn, 0.0f);   h.set (PID::auraOn, 0.0f);
              h.set (PID::shadowOn, 0.0f);  h.set (PID::patinaOn, 0.0f);
              h.set (PID::macroMemory, 0.0f);
          }},
        { "+ memory",              [] (BenchHost& h)
          {
              h.set (PID::retroOn, 0.0f);   h.set (PID::fxFilterOn, 0.0f);
              h.set (PID::spaceOn, 0.0f);   h.set (PID::auraOn, 0.0f);
              h.set (PID::shadowOn, 0.0f);  h.set (PID::patinaOn, 0.0f);
          }},
        { "+ retro",               [] (BenchHost& h)
          {
              h.set (PID::fxFilterOn, 0.0f);
              h.set (PID::spaceOn, 0.0f);   h.set (PID::auraOn, 0.0f);
              h.set (PID::shadowOn, 0.0f);  h.set (PID::patinaOn, 0.0f);
          }},
        { "+ fx filter",           [] (BenchHost& h)
          {
              h.set (PID::spaceOn, 0.0f);   h.set (PID::auraOn, 0.0f);
              h.set (PID::shadowOn, 0.0f);  h.set (PID::patinaOn, 0.0f);
          }},
        { "+ space",               [] (BenchHost& h)
          {
              h.set (PID::auraOn, 0.0f);
              h.set (PID::shadowOn, 0.0f);  h.set (PID::patinaOn, 0.0f);
          }},
        { "+ aura",                [] (BenchHost& h)
          {
              h.set (PID::shadowOn, 0.0f);  h.set (PID::patinaOn, 0.0f);
          }},
        { "+ patina (= default)",  [] (BenchHost& h)
          {
              h.set (PID::shadowOn, 0.0f);
          }},
        { "+ shadow (off by dflt)",[] (BenchHost& h)
          {
              h.set (PID::shadowOn, 1.0f);
          }},
    };

    std::cout << "NACAR stereo attribution\n"
                 "  sample rate  " << sampleRate << " Hz\n\n"
                 "  Each row adds one stage to the one above it, so the change in\n"
                 "  MONO is that stage's own contribution to the stereo image.\n"
                 "  SIDE/MID above 0 dB means the side channel carries more energy\n"
                 "  than the centre, which is what a mono retention worse than\n"
                 "  -3 dB actually means.\n\n";

    for (const auto& bench : makeBenchmarks())
    {
        if (filter.isNotEmpty() && ! juce::String (bench.name).containsIgnoreCase (filter))
            continue;

        std::cout << bench.name << "  (" << bench.family << ")\n"
                  << std::left << "  " << std::setw (26) << "STAGE"
                  << std::right << std::setw (9) << "MONO"
                  << std::setw (10) << "SIDE/MID"
                  << std::setw (9) << "DELTA"
                  << std::setw (9) << "RMS"
                  << "\n  " << std::string (61, '-') << "\n";

        float previous = 0.0f;
        bool  havePrevious = false;

        for (const auto& row : rows)
        {
            const auto audio = (row.configure == nullptr)
                                 ? renderBenchmark (bench, sampleRate, 256)
                                 : renderThroughChain (bench, sampleRate, 256, row.configure);

            const auto ms = midSideEnergy (audio);
            const float retention = ms.retentionDb();

            std::cout << std::left << "  " << std::setw (26) << row.label
                      << std::right << std::fixed << std::setprecision (2)
                      << std::setw (9) << retention
                      << std::setw (10) << ms.sideOverMidDb();

            if (havePrevious)
                std::cout << std::setw (8) << (retention - previous) << " ";
            else
                std::cout << std::setw (9) << "--";

            std::cout << std::setprecision (1)
                      << std::setw (9)
                      << juce::Decibels::gainToDecibels (audio.getRMSLevel (0, 0, audio.getNumSamples()))
                      << "\n";

            previous = retention;
            havePrevious = true;
        }

        std::cout << "\n";
    }
}

// ===========================================================================
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile ("Renders");
    double sampleRate = 48000.0;
    bool writeFiles = true;
    bool cpuOnly = false;
    bool throughChain = false;
    bool attribute = false;
    juce::String filter;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);

        if (arg == "--out" && i + 1 < argc)        outDir = juce::File (juce::String (argv[++i]));
        else if (arg == "--rate" && i + 1 < argc)  sampleRate = juce::String (argv[++i]).getDoubleValue();
        else if (arg == "--no-wav")                writeFiles = false;
        else if (arg == "--cpu")                   cpuOnly = true;
        else if (arg == "--chain")                 throughChain = true;
        else if (arg == "--attribute")             attribute = true;
        else if (! arg.startsWith ("--"))          filter = arg;
    }

    if (cpuOnly)
    {
        measureCpu (sampleRate, 256, throughChain);
        return 0;
    }

    if (attribute)
    {
        runAttribution (filter, sampleRate);
        return 0;
    }

    if (writeFiles)
        outDir.createDirectory();

    std::cout << (throughChain ? "NACAR full-chain benchmark\n"
                               : "NACAR synth benchmark\n")
              << "  sample rate  " << sampleRate << " Hz\n"
              << "  output       " << (writeFiles ? outDir.getFullPathName().toStdString()
                                                  : std::string ("(measurement only)"))
              << "\n\n";

    std::cout << std::left
              << std::setw (24) << "PATCH"
              << std::setw (9)  << "FAMILY"
              << std::right
              << std::setw (8)  << "PEAK"
              << std::setw (8)  << "RMS"
              << std::setw (7)  << "CREST"
              << std::setw (9)  << "CENTR"
              << std::setw (7)  << "LOW%"
              << std::setw (7)  << "MID%"
              << std::setw (7)  << "HIGH%"
              << std::setw (8)  << "MONO"
              << std::setw (8)  << "LOWCOR"
              << std::setw (9)  << "ALIAS"
              << std::setw (9)  << "DC"
              << "\n"
              << std::string (126, '-') << "\n";

    int rendered = 0, problems = 0;

    for (const auto& bench : makeBenchmarks())
    {
        if (filter.isNotEmpty() && ! juce::String (bench.name).containsIgnoreCase (filter))
            continue;

        const auto audio = throughChain ? renderThroughChain (bench, sampleRate, 256)
                                        : renderBenchmark (bench, sampleRate, 256);
        const double fundamental = juce::MidiMessage::getMidiNoteInHertz (bench.midiNote);
        const auto m = measure (audio, sampleRate, fundamental,
                                bench.measureAliasing && ! throughChain);

        if (writeFiles)
            writeWav (outDir.getChildFile (juce::String (bench.name)
                                               + (throughChain ? "_chain" : "") + ".wav"),
                      audio, sampleRate);

        std::cout << std::left
                  << std::setw (24) << bench.name
                  << std::setw (9)  << bench.family
                  << std::right << std::fixed << std::setprecision (1)
                  << std::setw (8)  << m.peakDb
                  << std::setw (8)  << m.rmsDb
                  << std::setw (7)  << m.crestDb
                  << std::setw (9)  << m.centroidHz
                  << std::setw (7)  << m.lowEnergyPct
                  << std::setw (7)  << m.midEnergyPct
                  << std::setw (7)  << m.highEnergyPct
                  << std::setw (8)  << m.monoRetainDb
                  << std::setprecision (2)
                  << std::setw (8)  << m.lowCorrelation
                  << std::setprecision (1)
                  << std::setw (9)  << (m.aliasingValid ? juce::String (m.aliasingDb, 1).toStdString()
                                                        : std::string ("--"))
                  << std::setprecision (4)
                  << std::setw (9)  << m.dcOffset
                  << (m.allFinite ? "" : "   NON-FINITE")
                  << "\n";

        ++rendered;

        // The specification's own thresholds.
        if (! m.allFinite)                         ++problems;
        if (m.peakDb > 0.0f)                       ++problems;
        if (std::abs (m.dcOffset) > 0.01f)         ++problems;
        if (m.monoRetainDb < -3.0f)                ++problems;
        if (juce::String (bench.family) == "REESE" && m.lowCorrelation < 0.9f) ++problems;
        if (m.aliasingValid && m.aliasingDb > -30.0f) ++problems;
    }

    std::cout << "\n"
              << rendered << " benchmarks rendered, "
              << problems << " threshold violation" << (problems == 1 ? "" : "s") << "\n\n"
              << "Thresholds: peak <= 0 dBFS | |DC| <= 0.01 | mono retention >= -3 dB\n"
              << "            Reese low-band correlation >= 0.90 | alias probes <= -30 dB\n"
              << "ALIAS reads -- where the figure would be meaningless: detuned unison,\n"
              << "FM, hard sync and chords all put energy off the harmonic series by design.\n"
              << "\nThese are measurements, not a verdict. Whether NACAR sounds\n"
              << "expensive is decided by listening to the WAVs, not by this table.\n";

    return problems > 0 ? 1 : 0;
}
