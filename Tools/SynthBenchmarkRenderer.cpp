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
                   [--cpu] [--attribute] [--presets] [<name-filter>]

        --chain      renders through the whole NACAR chain rather than the
                     synth alone.
        --attribute  renders one patch repeatedly, adding one chain stage at a
                     time, and reports what each stage did to the stereo image.
                     Use it when a mono retention figure fails: the table names
                     the stage instead of leaving it to inference.
        --presets    renders every FACTORY PRESET through the whole instrument
                     - parameters, chain order and modulation matrix - and
                     measures the library rather than the engine.  This is the
                     gate for Source/Presets: silence, clipping, non-finite
                     samples, DC, the low-end rule for BASS and SUB, and
                     whether any two presets have converged on each other.

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
#include "Audio/Modulation/ModMatrix.h"
#include "Presets/FactoryPresets.h"
#include "Presets/PresetManager.h"
#include "Audio/Print/PrintEngine.h"
#include "Mutation/MutationEngine.h"
#include "Mutation/MutationRecipe.h"

#include <map>

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
//  PRESET VERIFICATION  -  the gate for Source/Presets
//
//  The benchmarks above are engineering probes: they ask whether the engine
//  can reach a corner of its own range.  This asks a different question, about
//  content rather than about DSP - is every factory preset audible, unclipped,
//  finite, safe in the low end, and actually different from the others?
//
//  It renders each preset the way the instrument would: the whole parameter
//  block applied through the registry, the preset's own FX chain ORDER
//  published to the engine, and its modulation matrix rebuilt from the same
//  ValueTree shape the MOD page writes.  Anything less would be measuring a
//  patch the preset did not ask for.
//
//  It cannot say whether a preset is any good.  Nobody has listened to any of
//  this and this mode does not pretend otherwise.  What it can do is find the
//  four failures that are not matters of taste: silent, clipped, broken, or a
//  duplicate of something already in the library.
// ===========================================================================
namespace presetcheck
{
    /** A preset rendered and measured, plus the two numbers the similarity
        pass compares: where its energy sits, and how loud it is. */
    struct Result
    {
        juce::String name, category, mood;
        Measurements m;
        float centroidOctaves = 0.0f;   ///< log2 of the centroid, so 200 Hz to
                                        ///< 400 Hz is the same distance as 2 k to 4 k
    };

    /** Renders one factory preset through the whole instrument.

        The BenchHost starts at the parameter list's defaults and
        `PresetManager::applyParameters` writes every parameter - the preset's
        own values where it has them, defaults everywhere else - through the
        same gesture protocol the editor uses.  So this is the patch a user
        would get, not an approximation of it. */
    static juce::AudioBuffer<float> renderPreset (const presets::FactoryPreset& preset,
                                                  double sampleRate, int blockSize)
    {
        BenchHost host;

        const auto payload = PresetManager::payloadOf (preset);
        PresetManager::applyParameters (host.registry, payload);

        NacarEngine engine;
        engine.prepare (sampleRate, blockSize, 2);

        // Display order is DSP order, and a preset that reorders the chain is
        // a different instrument.  Nothing is bypassed: a preset switches a
        // module off with its own `*_on` flag, which is already a parameter.
        engine.setFxOrder (FxOrder::fromState (PresetManager::fxOrderOf (payload), ""));
        engine.rebuildModMatrix (PresetManager::makeModMatrixTree (payload));

        // `master_gain` is applied by NacarProcessor and not by NacarEngine,
        // so a render that stopped at the engine would measure a level the
        // listener never hears - and a preset that trims its own output would
        // look as though the trim did nothing.  This mirrors the processor
        // exactly: the same juce::dsp::Gain, the same 20 ms ramp.
        juce::dsp::Gain<float> outputGain;
        outputGain.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        outputGain.setRampDurationSeconds (0.02);
        outputGain.setGainDecibels (host.registry.userValue (PID::masterGain));

        const auto& a = preset.audition;

        const int totalSamples = (int) (sampleRate * a.seconds);
        juce::AudioBuffer<float> out (2, totalSamples);
        out.clear();

        juce::AudioBuffer<float> block (2, blockSize);

        const int releaseAt = (int) (totalSamples * 0.66);
        const int intervals[4] = { 0, 7, 15, 22 };   // the open minor-ninth voicing
        const int notes = juce::jlimit (1, 4, a.chordNotes);

        TransportInfo transport;
        transport.bpm = 120.0;
        transport.playing = true;        // synced LFOs and Pulse need a running host

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
                for (int i = 0; i < notes; ++i)
                    midi.addEvent (juce::MidiMessage::noteOn (1, a.midiNote + intervals[i],
                                                              a.velocity), 0);
                noteSent = true;
            }

            if (! releaseSent && position + n > releaseAt)
            {
                for (int i = 0; i < notes; ++i)
                    midi.addEvent (juce::MidiMessage::noteOff (1, a.midiNote + intervals[i]),
                                   juce::jmax (0, releaseAt - position));
                releaseSent = true;
            }

            engine.process (block, midi, host.registry, transport);

            {
                juce::dsp::AudioBlock<float> audioBlock (block);
                outputGain.process (juce::dsp::ProcessContextReplacing<float> (audioBlock));
            }

            transport.ppqPosition += (double) n / sampleRate * (transport.bpm / 60.0);

            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, position, block, ch, 0, n);

            position += n;
        }

        return out;
    }

    /** True for the two categories the low-end rule is a hard requirement
        for.  Everything else may legitimately be wide. */
    static bool isLowEndCategory (const juce::String& category)
    {
        return category == "BASS" || category == "SUB";
    }

    /** Everything about the library that can be checked without rendering it.

        These are the mistakes that do not show up as a bad measurement: a
        category word the browser's filter row does not contain, so the preset
        is unreachable through the interface; a value outside a parameter's own
        range, which is silently clamped on apply so the preset is not what it
        says it is; a routing naming one of the four per-voice sources, which
        read zero in the global matrix and would be a preset claiming movement
        it does not have; a chain order missing a slot. */
    static int checkLibraryIntegrity()
    {
        const auto& library = presets::factoryLibrary();

        const auto categories = juce::StringArray::fromTokens (
            "KEYS PADS PLUCKS BELLS LEADS BASS SUB VOCAL-LIKE TEXTURE "
            "ATMOSPHERE DRUMS PERCUSSION SEQUENCES", " ", "");

        const auto moods = juce::StringArray::fromTokens (
            "DARK INTIMATE BROKEN NOSTALGIC AIRY AGGRESSIVE ROMANTIC COLD WARM "
            "CINEMATIC DIRTY DREAMY HAUNTED LUSH MINIMAL MYSTERIOUS", " ", "");

        // The four the modulation matrix documents as reading zero, plus the
        // empty slot.  A preset naming one of these would be a preset with a
        // routing that does nothing.
        const auto deadSources = juce::StringArray::fromTokens (
            "NONE|ENV 1|ENV 2|VELOCITY|KEY TRACK", "|", "");

        juce::StringArray seenNames;
        int problems = 0;

        auto complain = [&problems] (const juce::String& name, const juce::String& what)
        {
            std::cout << "  " << name.toStdString() << ": " << what.toStdString() << "\n";
            ++problems;
        };

        for (const auto& preset : library)
        {
            if (seenNames.contains (preset.name))
                complain (preset.name, "duplicate name");

            seenNames.add (preset.name);

            if (! categories.contains (preset.category))
                complain (preset.name, "category \"" + preset.category
                                       + "\" is not one of the browser's thirteen words");

            if (! moods.contains (preset.mood))
                complain (preset.name, "mood \"" + preset.mood
                                       + "\" is not one of the browser's sixteen words");

            if (preset.tags.size() < 2 || preset.tags.size() > 4)
                complain (preset.name, "has " + juce::String (preset.tags.size())
                                       + " tags; the brief asks for two to four");

            if (preset.blurb.isEmpty())
                complain (preset.name, "has no one-sentence identity");

            // A value outside its range is clamped on apply, so the preset
            // would not be the patch it is written as.
            for (const auto& v : preset.values)
            {
                const auto& d = ParameterRegistry::definition (v.pid);

                if (d.kind == ParamKind::floatValue
                    && (v.value < d.minValue - 1.0e-4f || v.value > d.maxValue + 1.0e-4f))
                    complain (preset.name, juce::String (d.id) + " = " + juce::String (v.value)
                                           + " is outside [" + juce::String (d.minValue) + ", "
                                           + juce::String (d.maxValue) + "]");

                if (d.kind == ParamKind::choice)
                {
                    const int n = ParameterRegistry::choicesOf (v.pid).size();

                    if (v.value < -0.4f || v.value > (float) n - 0.6f)
                        complain (preset.name, juce::String (d.id) + " = " + juce::String (v.value)
                                               + " is not one of its " + juce::String (n) + " choices");
                }
            }

            for (const auto& m : preset.mods)
            {
                const juce::String source (m.source);

                if (deadSources.contains (source))
                    complain (preset.name, "routes " + source
                                           + ", which reads zero in the global matrix");
                else if (modSourceFromName (source) == ModSource::none)
                    complain (preset.name, "routes an unknown source \"" + source + "\"");

                if (m.depth < -1.0f || m.depth > 1.0f)
                    complain (preset.name, "routing depth " + juce::String (m.depth)
                                           + " is outside -1..1");
            }

            if (preset.mods.size() > 8)
                complain (preset.name, "has " + juce::String ((int) preset.mods.size())
                                       + " routings; the matrix holds eight");

            if (preset.fxOrder.isNotEmpty())
            {
                const auto named = juce::StringArray::fromTokens (preset.fxOrder, ",", "");

                if (named.size() != numFxSlots)
                    complain (preset.name, "chain order names " + juce::String (named.size())
                                           + " slots, not " + juce::String (numFxSlots));

                for (int slot = 0; slot < numFxSlots; ++slot)
                {
                    const juce::String canonical (fxSlotName ((FxSlot) slot));

                    if (! named.contains (canonical))
                        complain (preset.name, "chain order leaves " + canonical
                                               + " out, so it would never run");
                }
            }
        }

        std::cout << "\nLIBRARY INTEGRITY  names, vocabulary, parameter ranges, routings, chain order: "
                  << (problems == 0 ? "clean" : "FAILED") << "\n";

        return problems;
    }

    /** Saves a preset to disk and reads it back.

        The file format is the only part of the preset system with no other
        exercise: the factory set never goes through it, so a bug in it would
        only ever be found by a user losing their own patch.  This writes one,
        reads it, compares every parameter and deletes it again.

        It uses the real user preset directory, because a round trip that did
        not use the real path would not be testing the thing that can break. */
    static int checkFileRoundTrip()
    {
        BenchHost host;
        const auto& library = presets::factoryLibrary();

        if (library.empty())
            return 0;

        // A preset with a full chain order, a mod matrix and both extremes of
        // the parameter table in it.
        const auto& source = library[0];
        const auto payload = PresetManager::payloadOf (source);
        PresetManager::applyParameters (host.registry, payload);

        nacar::StateManager state (host.apvts);

        {
            auto chain = state.group (ids::FXCHAIN);
            chain.setProperty (ids::fxOrder, PresetManager::fxOrderOf (payload), nullptr);

            auto matrix = state.group (ids::MODMATRIX);
            matrix.removeAllChildren (nullptr);

            const auto built = PresetManager::makeModMatrixTree (payload);

            for (int i = 0; i < built.getNumChildren(); ++i)
                matrix.addChild (built.getChild (i).createCopy(), -1, nullptr);
        }

        PresetManager manager (host.registry, state);

        const juce::String name ("NacarBench Round Trip");
        const juce::StringArray tags { "verification", "temporary" };

        const int index = manager.saveUserPreset (name, "TEXTURE", "MINIMAL", tags);

        if (index < 0)
        {
            std::cout << "\n  FILE ROUND TRIP: could not write a preset to "
                      << PresetManager::userPresetDirectory().getFullPathName().toStdString()
                      << "\n";
            return 1;
        }

        const auto file = manager[index].file;

        nacar::PresetInfo readBack;
        PresetManager::Payload readPayload;

        int problems = 0;

        if (! PresetManager::readPresetFile (file, readBack, readPayload))
        {
            std::cout << "\n  FILE ROUND TRIP: wrote a preset that cannot be read back\n";
            ++problems;
        }
        else
        {
            if (readBack.name != name)                  { std::cout << "\n  FILE ROUND TRIP: name lost\n";     ++problems; }
            if (readBack.category != "TEXTURE")         { std::cout << "  FILE ROUND TRIP: category lost\n";    ++problems; }
            if (readBack.mood != "MINIMAL")             { std::cout << "  FILE ROUND TRIP: mood lost\n";        ++problems; }
            if (readBack.tags != tags)                  { std::cout << "  FILE ROUND TRIP: tags lost\n";        ++problems; }

            if (readPayload.fxOrder != PresetManager::fxOrderOf (payload))
            {
                std::cout << "  FILE ROUND TRIP: chain order lost\n";
                ++problems;
            }

            if ((int) readPayload.values.size() != numParameters)
            {
                std::cout << "  FILE ROUND TRIP: " << readPayload.values.size()
                          << " parameters survived of " << numParameters << "\n";
                ++problems;
            }

            // Apply what came back to a second host and compare every
            // parameter: that is the property a preset file actually promises.
            BenchHost reloaded;
            PresetManager::applyParameters (reloaded.registry, readPayload);

            int mismatches = 0;

            for (int i = 0; i < numParameters; ++i)
            {
                const auto pid = (PID) i;
                auto* a = host.registry.parameter (pid);
                auto* b = reloaded.registry.parameter (pid);

                if (a == nullptr || b == nullptr)
                    continue;

                if (std::abs (a->convertTo0to1 (host.registry.userValue (pid))
                              - b->convertTo0to1 (reloaded.registry.userValue (pid))) > 1.0e-4f)
                {
                    if (mismatches < 5)
                        std::cout << "  FILE ROUND TRIP: " << ParameterRegistry::idOf (pid)
                                  << " came back as " << reloaded.registry.userValue (pid)
                                  << " instead of " << host.registry.userValue (pid) << "\n";
                    ++mismatches;
                }
            }

            if (mismatches > 0)
            {
                std::cout << "  FILE ROUND TRIP: " << mismatches << " parameter(s) did not survive\n";
                ++problems;
            }

            int enabledRoutings = 0;

            for (const auto& r : readPayload.mods)
                enabledRoutings += (r.enabled && r.source != "NONE") ? 1 : 0;

            if (enabledRoutings != (int) source.mods.size())
            {
                std::cout << "  FILE ROUND TRIP: " << enabledRoutings << " routing(s) survived of "
                          << source.mods.size() << "\n";
                ++problems;
            }
        }

        file.deleteFile();

        std::cout << "\nFILE ROUND TRIP  save -> read -> apply, every parameter by string ID: "
                  << (problems == 0 ? "clean" : "FAILED") << "\n"
                  << "  (written to and removed from "
                  << PresetManager::userPresetDirectory().getFullPathName().toStdString() << ")\n";

        return problems;
    }

    static int run (const juce::String& filter, double sampleRate,
                    const juce::File& outDir, bool writeFiles)
    {
        const auto& library = presets::factoryLibrary();

        std::cout << "NACAR factory preset verification\n"
                     "  sample rate  " << sampleRate << " Hz\n"
                     "  library      " << library.size() << " presets\n"
                     "  output       " << (writeFiles ? outDir.getFullPathName().toStdString()
                                                      : std::string ("(measurement only)"))
                  << "\n\n"
                     "  Each preset is rendered through the whole instrument with its own\n"
                     "  chain order and modulation matrix, on the note it was voiced around.\n"
                     "  These are measurements. Nobody has listened to any of it.\n\n";

        std::cout << std::left
                  << std::setw (23) << "PRESET"
                  << std::setw (12) << "CATEGORY"
                  << std::setw (12) << "MOOD"
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
                  << std::setw (9)  << "DC"
                  << "  FLAGS"
                  << "\n"
                  << std::string (140, '-') << "\n";

        std::vector<Result> results;
        results.reserve (library.size());

        int problems = 0, rendered = 0;

        for (const auto& preset : library)
        {
            if (filter.isNotEmpty()
                && ! (preset.name.containsIgnoreCase (filter)
                      || preset.category.containsIgnoreCase (filter)
                      || preset.mood.containsIgnoreCase (filter)))
                continue;

            const auto audio = renderPreset (preset, sampleRate, 256);

            const double fundamental =
                juce::MidiMessage::getMidiNoteInHertz (preset.audition.midiNote);

            // Aliasing is never reported here: every preset detunes, chords,
            // modulates or granulates by design, so the figure would be
            // measuring the preset rather than the oscillator.
            const auto m = measure (audio, sampleRate, fundamental, false);

            if (writeFiles)
                writeWav (outDir.getChildFile (juce::File::createLegalFileName (preset.name)
                                               + "_preset.wav"),
                          audio, sampleRate);

            juce::StringArray flags;

            if (! m.allFinite)                    { flags.add ("NON-FINITE"); ++problems; }
            if (m.peakDb > 0.0f)                  { flags.add ("CLIP");       ++problems; }
            if (m.peakDb < -40.0f)                { flags.add ("QUIET");      ++problems; }
            if (std::abs (m.dcOffset) > 0.01f)    { flags.add ("DC");         ++problems; }
            if (m.monoRetainDb < -3.0f)           { flags.add ("WIDE");       ++problems; }

            if (isLowEndCategory (preset.category))
            {
                if (m.monoRetainDb < -1.0f)       { flags.add ("LOW-MONO");   ++problems; }
                if (m.lowCorrelation < 0.9f)      { flags.add ("LOW-CORR");   ++problems; }
            }

            std::cout << std::left
                      << std::setw (23) << preset.name.toStdString()
                      << std::setw (12) << preset.category.toStdString()
                      << std::setw (12) << preset.mood.toStdString()
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
                      << std::setprecision (4)
                      << std::setw (9)  << m.dcOffset
                      << "  " << flags.joinIntoString (" ").toStdString()
                      << "\n";

            Result r;
            r.name     = preset.name;
            r.category = preset.category;
            r.mood     = preset.mood;
            r.m        = m;
            r.centroidOctaves = std::log2 (juce::jmax (20.0f, m.centroidHz));

            results.push_back (r);
            ++rendered;
        }

        // -------------------------------------------------------------------
        //  Did they actually come out different?
        //
        //  Two presets that measure alike are not necessarily the same patch -
        //  a bell and a pluck can share a centroid - so this does not fail the
        //  run.  It names the closest pairs so an accidental duplicate, which
        //  is what happens when a preset is copied and only the filter moved,
        //  is visible instead of hiding in fifty-four rows.
        //
        //  The distance is deliberately crude and deliberately perceptual:
        //  octaves of spectral centroid, decibels of RMS and the low/high
        //  balance, each scaled so one unit is about as noticeable as one unit
        //  of the others.
        // -------------------------------------------------------------------
        if (results.size() >= 2)
        {
            struct Pair { float distance; int a, b; };
            std::vector<Pair> pairs;

            for (size_t i = 0; i < results.size(); ++i)
                for (size_t j = i + 1; j < results.size(); ++j)
                {
                    const auto& x = results[i];
                    const auto& y = results[j];

                    const float dCentroid = (x.centroidOctaves - y.centroidOctaves) / 1.0f;
                    const float dLevel    = (x.m.rmsDb - y.m.rmsDb) / 6.0f;
                    const float dLow      = (x.m.lowEnergyPct - y.m.lowEnergyPct) / 25.0f;
                    const float dHigh     = (x.m.highEnergyPct - y.m.highEnergyPct) / 25.0f;
                    const float dCrest    = (x.m.crestDb - y.m.crestDb) / 8.0f;

                    pairs.push_back ({ std::sqrt (dCentroid * dCentroid + dLevel * dLevel
                                                  + dLow * dLow + dHigh * dHigh
                                                  + dCrest * dCrest),
                                       (int) i, (int) j });
                }

            std::sort (pairs.begin(), pairs.end(),
                       [] (const Pair& a, const Pair& b) { return a.distance < b.distance; });

            std::cout << "\nCLOSEST PAIRS  (octaves of centroid, dB of level, spectral balance)\n"
                      << std::left << "  " << std::setw (23) << "A"
                      << std::setw (23) << "B"
                      << std::right << std::setw (10) << "DISTANCE"
                      << std::setw (12) << "dCENTROID"
                      << std::setw (9) << "dRMS" << "\n"
                      << "  " << std::string (75, '-') << "\n";

            const int shown = juce::jmin (6, (int) pairs.size());

            for (int i = 0; i < shown; ++i)
            {
                const auto& x = results[(size_t) pairs[(size_t) i].a];
                const auto& y = results[(size_t) pairs[(size_t) i].b];

                std::cout << std::left << "  "
                          << std::setw (23) << x.name.toStdString()
                          << std::setw (23) << y.name.toStdString()
                          << std::right << std::fixed << std::setprecision (3)
                          << std::setw (10) << pairs[(size_t) i].distance
                          << std::setprecision (2)
                          << std::setw (12) << (x.centroidOctaves - y.centroidOctaves)
                          << std::setprecision (1)
                          << std::setw (9) << (x.m.rmsDb - y.m.rmsDb)
                          << "\n";
            }

            std::cout << "\n  A distance below about 0.25 is worth opening both presets for.\n";
        }

        // -- coverage, because a library is also a distribution --------------
        //
        //  Only for a whole run: a filtered one would print thirteen zeroes and
        //  one number, which says something about the filter and nothing about
        //  the library.
        if (filter.isEmpty())
        {
            juce::StringArray categories = juce::StringArray::fromTokens (
                "KEYS PADS PLUCKS BELLS LEADS BASS SUB VOCAL-LIKE TEXTURE "
                "ATMOSPHERE DRUMS PERCUSSION SEQUENCES", " ", "");

            juce::StringArray moods = juce::StringArray::fromTokens (
                "DARK INTIMATE BROKEN NOSTALGIC AIRY AGGRESSIVE ROMANTIC COLD WARM "
                "CINEMATIC DIRTY DREAMY HAUNTED LUSH MINIMAL MYSTERIOUS", " ", "");

            std::cout << "\nCOVERAGE\n  categories ";

            int emptyCategories = 0, emptyMoods = 0;

            for (const auto& c : categories)
            {
                int n = 0;
                for (const auto& r : results)
                    n += (r.category == c) ? 1 : 0;

                if (n == 0) ++emptyCategories;

                std::cout << c.toStdString() << ":" << n << "  ";
            }

            std::cout << "\n  moods      ";

            for (const auto& mo : moods)
            {
                int n = 0;
                for (const auto& r : results)
                    n += (r.mood == mo) ? 1 : 0;

                if (n == 0) ++emptyMoods;

                std::cout << mo.toStdString() << ":" << n << "  ";
            }

            std::cout << "\n";

            // A category word with no preset is a filter chip that leads
            // nowhere, so it fails the run.  A mood word with none is reported
            // but does not: moods are a colour across the library rather than a
            // promise that each one is filled.
            if (emptyCategories > 0)
            {
                std::cout << "\n  " << emptyCategories << " category word(s) have no preset.\n";
                problems += emptyCategories;
            }

            if (emptyMoods > 0)
                std::cout << "  " << emptyMoods << " mood word(s) have no preset.\n";
        }

        if (filter.isEmpty())
        {
            problems += checkLibraryIntegrity();
            problems += checkFileRoundTrip();
        }

        std::cout << "\n" << rendered << " preset" << (rendered == 1 ? "" : "s") << " rendered, "
                  << problems << " threshold violation" << (problems == 1 ? "" : "s") << "\n\n"
                  << "Thresholds: -40 dBFS < peak <= 0 dBFS | |DC| <= 0.01 | finite everywhere\n"
                  << "            mono retention >= -3 dB, and >= -1 dB with low-band\n"
                  << "            correlation >= 0.90 for BASS and SUB\n"
                  << "            every category word carries at least one preset\n\n"
                  << "These are measurements, not a verdict. Whether a preset is worth\n"
                  << "loading is decided by listening to it, which nobody has done.\n";

        return problems;
    }
}

// ===========================================================================
//  THE GOLDEN SET  -  phases 25 and 26
//
//  A golden preset is one the instrument is JUDGED BY: if the engine changes
//  and these move, something has been broken or improved and somebody has to
//  say which. That judgement is a listening judgement and this tool cannot
//  make it. What it can do - and what "golden" means to a build rather than
//  to a listener - is notice the movement.
//
//  So this mode does two things:
//
//    1. renders a fixed set deterministically and fingerprints it, comparing
//       against a stored baseline. Drift beyond tolerance fails the run. That
//       catches the class of defect that is otherwise invisible: an engine
//       change that quietly revoices the whole library.
//
//    2. writes the audio out, named and ordered, so a person can sit down and
//       audition it. THAT is the part that accepts a golden preset. Nothing
//       here does it and nothing here claims to.
//
//  The mutations are golden in a stronger sense than the presets, because a
//  mutation is reproducible from a seed: print a preset, mutate it with a
//  fixed recipe, and the result is a function of the engine alone. A change to
//  the mutation engine shows up here as a number, not as an opinion.
// ===========================================================================
namespace golden
{
    using namespace presetcheck;

    struct Entry
    {
        const char* preset;          ///< a name from the factory library
        const char* claim;           ///< what a listener is asked to confirm
        bool        mutate;          ///< print it and mutate it as well
        juce::uint32 seed;
        mutation::Intent intent;
        mutation::Distance distance;
    };

    /** Chosen to cover the instrument rather than to flatter it: each of the
        three characters, each filter model, the tape and digital degradation
        paths, the granular and reverb tails, a bass that has to hold its low
        end and a drum that has to hit. If an engine change cannot be heard in
        one of these, it probably cannot be heard. */
    static const std::array<Entry, 12> entries {{
        { "Niebla en la Ciudad", "the default patch: wet, minor, unhurried",              true,  11u, mutation::Intent::memory,   mutation::Distance::near_ },
        { "Cassette Rhodes",     "a tine whose index decays while the filter opens",      true,  23u, mutation::Intent::broken,   mutation::Distance::near_ },
        { "Long Hammer",         "an 808 whose pitch falls on the attack",                false, 0u,  mutation::Intent::memory,   mutation::Distance::near_ },
        { "Tunnel Reese",        "two detuned saws that stay mono underneath",            false, 0u,  mutation::Intent::memory,   mutation::Distance::near_ },
        { "Foundation",          "a sub that is a fundamental and nothing else",          false, 0u,  mutation::Intent::memory,   mutation::Distance::near_ },
        { "Iron Temple",         "a noise burst into a tuned comb",                       true,  47u, mutation::Intent::ghost,    mutation::Distance::far },
        { "Crystal Vespers",     "fixed inharmonic resonances over a spectral table",     false, 0u,  mutation::Intent::memory,   mutation::Distance::near_ },
        { "Ghost Arpeggio",      "a held note articulated by the grid",                   true,  59u, mutation::Intent::rhythmic, mutation::Distance::near_ },
        { "Tape Dust",           "degradation as the audible content",                    true,  71u, mutation::Intent::distant,  mutation::Distance::far },
        { "Waiting Room",        "a room rather than a note",                             true,  83u, mutation::Intent::cloud,    mutation::Distance::far },
        { "Glass Hat",           "high-passed noise that has to stay a transient",        false, 0u,  mutation::Intent::memory,   mutation::Distance::near_ },
        { "Salt Water Piano",    "a struck body where the comb carries the tone",         true,  97u, mutation::Intent::dark,     mutation::Distance::far },
    }};

    struct Fingerprint
    {
        juce::String name;
        float peakDb = 0.0f, rmsDb = 0.0f, centroidHz = 0.0f;
        float lowPct = 0.0f, highPct = 0.0f, monoDb = 0.0f;
        int   lengthSamples = 0;
    };

    static juce::File baselineFile()
    {
        return juce::File (__FILE__).getParentDirectory().getChildFile ("golden-baseline.txt");
    }

    static juce::String formatLine (const Fingerprint& f)
    {
        return f.name + "\t" + juce::String (f.peakDb, 2) + "\t" + juce::String (f.rmsDb, 2)
             + "\t" + juce::String (f.centroidHz, 1) + "\t" + juce::String (f.lowPct, 2)
             + "\t" + juce::String (f.highPct, 2) + "\t" + juce::String (f.monoDb, 2)
             + "\t" + juce::String (f.lengthSamples);
    }

    static Fingerprint fingerprintOf (const juce::String& name,
                                      const juce::AudioBuffer<float>& audio, double rate)
    {
        const auto m = measure (audio, rate, 261.63, false);

        Fingerprint f;
        f.name = name;
        f.peakDb = m.peakDb;
        f.rmsDb = m.rmsDb;
        f.centroidHz = m.centroidHz;
        f.lowPct = m.lowEnergyPct;
        f.highPct = m.highEnergyPct;
        f.monoDb = m.monoRetainDb;
        f.lengthSamples = audio.getNumSamples();
        return f;
    }

    static int run (double sampleRate, const juce::File& outDir, bool writeFiles, bool rewrite)
    {
        const auto& library = presets::factoryLibrary();

        std::cout << "NACAR golden set\n"
                     "  " << entries.size() << " presets, each rendered; those marked below are\n"
                     "  also printed and mutated from a fixed seed.\n\n"
                     "  This compares against a stored fingerprint and reports DRIFT. It does\n"
                     "  not decide whether anything sounds right - that is what the audio in\n"
                     "  the output directory is for, and nobody has listened to it.\n\n";

        std::vector<Fingerprint> prints;
        int missing = 0;

        for (const auto& e : entries)
        {
            const juce::String wanted (e.preset);

            const auto found = std::find_if (library.begin(), library.end(),
                                             [&wanted] (const presets::FactoryPreset& p)
                                             { return p.name == wanted; });

            if (found == library.end())
            {
                std::cout << "  MISSING  " << wanted.toStdString()
                          << " is not in the factory library\n";
                ++missing;
                continue;
            }

            const auto audio = renderPreset (*found, sampleRate, 256);
            prints.push_back (fingerprintOf (wanted, audio, sampleRate));

            if (writeFiles)
            {
                outDir.createDirectory();
                writeWav (outDir.getChildFile ("golden-" + juce::File::createLegalFileName (wanted) + ".wav"),
                          audio, sampleRate);
            }

            if (! e.mutate)
                continue;

            // A golden MUTATION: the printed preset, mutated from a fixed
            // seed. Reproducible from the engine alone, so a change to the
            // mutation engine is a number here rather than an opinion.
            PrintEngine::Snapshot snapshot;

            {
                BenchHost host;
                const auto payload = PresetManager::payloadOf (*found);
                PresetManager::applyParameters (host.registry, payload);

                for (int i = 0; i < numParameters; ++i)
                    snapshot.values[(size_t) i] = host.registry.userValue ((PID) i);

                snapshot.fxOrder = PresetManager::fxOrderOf (payload);
                snapshot.modMatrix = PresetManager::makeModMatrixTree (payload);
            }

            PrintEngine::Settings settings;
            settings.sampleRate = sampleRate;
            settings.blockSize = 256;
            settings.holdSeconds = 1.5;
            settings.maxTailSeconds = 4.0;
            settings.baseName = {};        // measured, not kept

            const auto printed = PrintEngine::render (snapshot, settings);

            if (! printed.ok || printed.audio == nullptr)
            {
                std::cout << "  PRINT FAILED  " << wanted.toStdString() << ": "
                          << printed.failure.toStdString() << "\n";
                ++missing;
                continue;
            }

            mutation::Recipe recipe;
            recipe.seed = e.seed;
            recipe.intent = e.intent;
            recipe.distance = e.distance;
            recipe.harmonyMode = harmony::Mode::safe;

            AnalysisResult analysis;
            harmony::Context context;

            const auto result = mutation::MutationEngine::render (recipe, *printed.audio,
                                                                  analysis, context);

            if (! result.ok || result.audio == nullptr)
            {
                std::cout << "  MUTATION FAILED  " << wanted.toStdString() << ": "
                          << result.failure.toStdString() << "\n";
                ++missing;
                continue;
            }

            const auto label = wanted + " / " + mutation::nameOf (e.intent)
                               + " " + juce::String ((int) e.seed);

            prints.push_back (fingerprintOf (label, result.audio->audio, sampleRate));

            if (writeFiles)
                writeWav (outDir.getChildFile ("golden-mutation-"
                                               + juce::File::createLegalFileName (label) + ".wav"),
                          result.audio->audio, sampleRate);
        }

        // -- compare against the baseline ------------------------------------
        const auto file = baselineFile();

        if (rewrite || ! file.existsAsFile())
        {
            juce::StringArray lines;

            lines.add ("# NACAR golden fingerprints.");
            lines.add ("# Regenerated with: NacarBench --golden --rewrite");
            lines.add ("# A change here is a change to what the instrument SOUNDS LIKE.");
            lines.add ("# Nothing may update this file without somebody having listened.");
            lines.add ("# name\tpeakDb\trmsDb\tcentroidHz\tlow%\thigh%\tmonoDb\tsamples");

            for (const auto& f : prints)
                lines.add (formatLine (f));

            file.replaceWithText (lines.joinIntoString ("\n") + "\n");

            std::cout << "  Wrote " << prints.size() << " fingerprints to "
                      << file.getFileName().toStdString() << ".\n"
                      << "  THIS IS NOT ACCEPTANCE. It records what the engine does today so a\n"
                      << "  later change can be noticed. Somebody still has to listen.\n";
            return missing;
        }

        juce::StringArray stored;
        stored.addLines (file.loadFileAsString());

        std::map<juce::String, Fingerprint> baseline;

        for (const auto& line : stored)
        {
            if (line.startsWithChar ('#') || line.trim().isEmpty())
                continue;

            auto parts = juce::StringArray::fromTokens (line, "\t", "");

            if (parts.size() < 8)
                continue;

            Fingerprint f;
            f.name = parts[0];
            f.peakDb = parts[1].getFloatValue();
            f.rmsDb = parts[2].getFloatValue();
            f.centroidHz = parts[3].getFloatValue();
            f.lowPct = parts[4].getFloatValue();
            f.highPct = parts[5].getFloatValue();
            f.monoDb = parts[6].getFloatValue();
            f.lengthSamples = parts[7].getIntValue();

            baseline[f.name] = f;
        }

        std::cout << std::left << "  " << std::setw (46) << "ENTRY"
                  << std::right << std::setw (10) << "dPEAK" << std::setw (10) << "dRMS"
                  << std::setw (12) << "dCENTROID" << std::setw (10) << "dMONO" << "  VERDICT\n"
                  << "  " << std::string (96, '-') << "\n";

        int drifted = 0;

        for (const auto& f : prints)
        {
            const auto it = baseline.find (f.name);

            if (it == baseline.end())
            {
                std::cout << std::left << "  " << std::setw (46) << f.name.toStdString()
                          << "  NEW - no fingerprint stored\n";
                continue;
            }

            const auto& b = it->second;

            const float dPeak = f.peakDb - b.peakDb;
            const float dRms  = f.rmsDb - b.rmsDb;
            const float dCent = b.centroidHz > 1.0f ? (f.centroidHz / b.centroidHz - 1.0f) * 100.0f
                                                    : 0.0f;
            const float dMono = f.monoDb - b.monoDb;

            // Generous enough to absorb floating-point reassociation from a
            // compiler change, tight enough that a revoice cannot hide.
            const bool moved = std::abs (dPeak) > 0.10f || std::abs (dRms) > 0.10f
                            || std::abs (dCent) > 1.0f  || std::abs (dMono) > 0.10f
                            || f.lengthSamples != b.lengthSamples;

            if (moved)
                ++drifted;

            std::cout << std::left << "  " << std::setw (46) << f.name.toStdString()
                      << std::right << std::fixed << std::setprecision (2)
                      << std::setw (10) << dPeak
                      << std::setw (10) << dRms
                      << std::setw (11) << dCent << "%"
                      << std::setw (10) << dMono
                      << "  " << (moved ? "DRIFTED" : "held") << "\n";
        }

        std::cout << "\n  " << prints.size() << " entries, " << drifted << " drifted, "
                  << missing << " missing.\n\n";

        if (drifted > 0)
            std::cout << "  DRIFT IS NOT AUTOMATICALLY A DEFECT - an intended improvement drifts\n"
                         "  too. It means somebody has to listen to the audio and decide, and\n"
                         "  then regenerate the baseline with --rewrite if the new sound is the\n"
                         "  one we want. What it must never be is updated without listening.\n\n";

        std::cout << "  Nobody has listened to any of this.\n";

        return drifted + missing;
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
    bool presetMode = false;
    bool goldenMode = false;
    bool rewriteGolden = false;
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
        else if (arg == "--presets")               presetMode = true;
        else if (arg == "--golden")                goldenMode = true;
        else if (arg == "--rewrite")               rewriteGolden = true;
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

    if (goldenMode)
    {
        const int problems = golden::run (sampleRate, outDir, writeFiles, rewriteGolden);
        return problems > 0 ? 1 : 0;
    }

    if (presetMode)
    {
        if (writeFiles)
            outDir.createDirectory();

        return presetcheck::run (filter, sampleRate, outDir, writeFiles) > 0 ? 1 : 0;
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
