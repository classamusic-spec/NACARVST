/*
    The analysis pass.

    Everything here is measurement.  Where a measurement cannot be made, the
    field is left at the undetermined value AnalysisResult.h documents and the
    confidence beside it is left at zero - see README.md in this directory for
    which ones that happens to and why.

    The spectral machinery follows Tools/SynthBenchmarkRenderer.cpp: a windowed
    FFT over overlapping frames, magnitudes accumulated across frames, centroid
    and rolloff and band fractions taken from the average.  The one deliberate
    departure is the window.  The renderer uses Blackman-Harris because it is
    measuring energy 40 dB down between harmonics and Hann's -31 dB sidelobes
    would swamp it.  Nothing here needs that floor, and Blackman-Harris pays for
    it with a main lobe twice as wide, which smears a semitone into its
    neighbours at the bottom of the chroma range.  Hann is the right trade here.
*/

#include "SampleAnalyser.h"

#include "../Harmony/Harmony.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace nacar
{
    namespace
    {
        // ===================================================================
        //  Thresholds.
        //
        //  Every number in this block is a decision.  The comment beside each
        //  says where it came from; the README repeats them in prose.  None of
        //  them is a parameter and none of them is reachable from the session
        //  tree - the analysis is not something the user tunes.
        // ===================================================================

        /** Analysis window lengths, in seconds, converted to the nearest power
            of two at the file's own rate so that bin width stays roughly
            constant from 44.1 to 192 kHz.

            Onset: 46 ms (2048 at 44.1 kHz), hopping every 5.8 ms.  The window
            is long and the hop is short on purpose, and the reason is measured.
            At 23 ms the bins are 43 Hz wide, so the harmonics of a 220 Hz tone
            sit five bins apart and their leakage skirts overlap; the skirts
            interfere, the bins between the harmonics swing by 100 % from frame
            to frame at the beat rate between them, and the flux ripples at 16 %
            of a real attack.  That ripple produced 31 onsets in a two-second
            held note.  Doubling the window puts those harmonics ten bins apart,
            drops the interference by about 20 dB, and the same note produces
            one onset.  Nothing is lost in timing, because the position of an
            onset is refined against the waveform afterwards rather than being
            read off the frame index.

            Chroma: 186 ms (8192 at 44.1 kHz), 5.4 Hz bins.  A semitone at C2 is
            3.9 Hz, so the bottom of the chroma range is smeared even at this
            length - which is exactly why the range starts at C2 and not lower. */
        constexpr double kOnsetFrameSeconds  = 0.046;
        constexpr double kChromaFrameSeconds = 0.186;

        constexpr int kOnsetHopDivisor  = 8;   ///< 5.8 ms hop at 44.1 kHz
        constexpr int kChromaHopDivisor = 2;   ///< 50 % overlap: 93 ms hop at 44.1 kHz

        constexpr int kMinFftOrder = 8;
        constexpr int kMaxFftOrder = 15;

        /** Below this length no spectral measurement is attempted.  256 samples
            is under 6 ms: there is no spectrum in it, and reporting the
            spectrum of a zero-padded impulse as though it described the file
            would be inventing a measurement. */
        constexpr int kMinSamplesForSpectrum = 256;

        /** A block counts as silence when it sits 45 dB below the file's own
            peak, with an absolute floor at -100 dBFS so that a digitally silent
            file is not compared against zero.  45 dB rather than 60 because
            room tone and tape noise in a real sample sit well above -60 dBFS
            relative to the peak and are not what anyone means by silence. */
        constexpr float kSilenceRelativeDb  = -45.0f;
        constexpr float kSilenceAbsolute    = 1.0e-5f;
        constexpr double kSilenceBlockSeconds = 0.010;

        /** The whole file is treated as silent below this peak (-120 dBFS). */
        constexpr float kSilentFilePeak = 1.0e-6f;

        /** Chroma range: C2 (65.4 Hz) to just above C7 (2093 Hz).  Below C2 the
            FFT cannot separate adjacent semitones; above C7 almost everything
            present is a harmonic of something lower and adds noise to the
            profile rather than evidence. */
        constexpr double kChromaLowHz  = 65.0;
        constexpr double kChromaHighHz = 2100.0;

        /** Log compression of the magnitude spectrum before the onset flux is
            taken from it: c = log(1 + gamma * m).  Standard spectral
            compression (Mueller, Fundamentals of Music Processing, ch. 6); it
            is what stops a crescendo from making every later hit a bigger
            onset than every earlier one.

            Mueller's own figure is 100.  10 is used here because the
            compression is what turns a leakage ripple of a thousandth of full
            scale into a tenth of a real attack: log(1 + 100 m) is thirty times
            steeper at m = 0.006 than it is at m = 0.4.  10 keeps a quiet hit
            visible without magnifying the floor into an event. */
        constexpr float kFluxCompressionGamma = 10.0f;

        /** The flux is taken over log-spaced BANDS, not over raw bins, and this
            is not a refinement - it is the difference between a working onset
            detector and a useless one.

            Measured: a stationary 220 Hz tone with eight harmonics produced 33
            onsets in two seconds when the flux ran per bin.  The leakage skirts
            of neighbouring harmonics interfere, so the hundreds of nearly-empty
            bins between them ripple from frame to frame; the compression above
            lifts each tiny ripple to roughly the same size, and three hundred
            of them add up to a respectable fraction of a real attack.  Summing
            each band first means an empty band stays empty.  The same signal
            now produces one onset.

            48 bands over 30 Hz to 16 kHz is about five per octave.  The exact
            count does not matter much; being far fewer than the bin count is
            what matters. */
        constexpr int    kFluxBands      = 48;
        constexpr double kFluxLowHz      = 30.0;
        constexpr double kFluxHighHz     = 16000.0;

        /** The chroma is built from interpolated spectral PEAKS, not from raw
            bins, which is the one decision in this file that the low end of the
            range depends on.  At A2 (110 Hz) with 5.4 Hz bins a semitone is
            barely more than one bin wide, so assigning whole bins to the
            nearest semitone puts a tone that sits between two bins into the
            wrong pitch class - measured, not imagined: the first version of
            this code read A2 as G sharp.  A parabolically interpolated peak is
            located to a fraction of a bin and lands in the right class.

            Each peak is then spread over the pitch classes within two thirds of
            a semitone of it with a cosine-squared weight - the window from
            Gomez's HPCP (2006), whose whole purpose is this problem. */
        constexpr double kChromaWindowSemitones = 4.0 / 3.0;

        /** Peaks below this fraction of the frame's loudest peak are ignored.
            It is deliberately low: white noise has to keep producing peaks
            everywhere or its chroma stops being flat, and a flat chroma for
            noise is the single most important property this file has. */
        constexpr float kChromaPeakFloor = 1.0e-3f;

        /** How hard a frame on a transient is discounted when building the
            chroma.  A hit is broadband and its pitch content is the least
            reliable part of the file, but a staccato piece is nothing BUT
            transients, so the weight floors at 0.25 rather than reaching zero. */
        constexpr float kChromaTransientDiscount = 0.75f;

        /** The chroma salience gate.  max/mean over the twelve classes: a flat
            profile is 1.0 and carries no key information whatsoever.  It
            multiplies the detector's own confidence and can only reduce it.

            Measured on this file's own test signals: white noise 1.06, a click
            train 1.61, a bar of eighth-note clicks 1.82, a four-chord
            progression 2.72, a single harmonic-rich note 7.8.  The window
            1.25 to 2.00 puts noise at zero, leaves percussion heavily
            discounted, and leaves anything with a pitch in it untouched. */
        constexpr float kSalienceFlat      = 1.25f;
        constexpr float kSalienceConvinced = 2.00f;

        /** Full confidence needs this many chroma frames above the energy gate
            - about 1.5 s at 44.1 kHz.  Below it the confidence is scaled down
            in proportion, because eight frames of a four-bar phrase is not a
            key, it is a chord. */
        constexpr int kChromaFramesForFullConfidence = 8;

        /** Onset peak picking.  A frame is an onset when it is the largest in
            +/- 3 frames, stands kOnsetLocalMultiple above the local mean of the
            detection function, and clears an absolute floor.  The floor is the
            larger of a fraction of the 99th percentile and a fraction of the
            global maximum: the percentile term handles a dense loop where the
            maximum is one hit among hundreds, the maximum term handles a file
            with a single hit in it where the percentile is just noise. */
        constexpr int    kOnsetLocalMaxFrames    = 3;
        constexpr double kOnsetLocalMeanSeconds  = 0.12;
        constexpr float  kOnsetLocalMultiple     = 2.0f;
        constexpr float  kOnsetPercentileFloor   = 0.12f;
        constexpr float  kOnsetGlobalMaxFloor    = 0.10f;

        /** ...and it must also stand this many standard deviations above the
            local mean.  A mean test alone cannot tell a spike from a noisy
            plateau; requiring 2 sigma as well means a detection function that
            is merely restless has to become genuinely exceptional before it
            counts as an event.  2 rather than 3 because a drum roll raises its
            own local sigma and 3 would swallow the roll. */
        constexpr float  kOnsetLocalSigmas       = 2.0f;

        /** Minimum spacing between onsets.  30 ms is 33 hits a second, which is
            faster than any played rhythm and slower than the double-trigger a
            spectral flux detector produces on a kick's own decay. */
        constexpr double kOnsetMinSpacingSeconds = 0.030;

        /** Tempo search range and the prior.  The prior is a Gaussian on log2
            tempo centred at 120 BPM with a standard deviation of 0.9 octaves -
            wide enough that 75 and 170 BPM are barely penalised, narrow enough
            to break a tie between a tempo and its double. */
        constexpr double kTempoMinBpm = 60.0;
        constexpr double kTempoMaxBpm = 200.0;
        constexpr double kTempoPriorCentreBpm = 120.0;
        constexpr double kTempoPriorOctaves   = 0.9;

        /** A tempo is not reported at all below these.  Four onsets is three
            inter-onset intervals, which is the least that can establish a
            period; 1.5 s is two beats at 80 BPM.  A one-shot fails both, and
            that is the point. */
        constexpr int    kTempoMinOnsets  = 4;
        constexpr double kTempoMinSeconds = 1.5;

        /** Confidence mapping for tempo.  Both terms must agree: the ACF peak
            says "the envelope repeats at this lag", the phase concentration
            says "and the onsets actually land on that grid".  White noise
            produces random onsets, which can accidentally give a decent ACF
            peak but never a concentrated phase. */
        constexpr float kTempoAcfZero = 0.20f, kTempoAcfFull = 0.55f;
        constexpr float kTempoPhaseZero = 0.30f, kTempoPhaseFull = 0.70f;

        /** The grid the onsets sit on may be a subdivision of the beat rather
            than the beat itself.  Halves, thirds and quarters are checked. */
        constexpr int kTempoMaxSubdivision = 4;

        /** Percussive ratio: energy inside this window after each onset,
            against the file's total.  60 ms is roughly where a drum hit stops
            being an attack and starts being a tail. */
        constexpr double kPercussiveWindowSeconds = 0.060;

        /** Polyphony.  A partial counts as belonging to a candidate fundamental
            when it is within this many cents of an integer multiple of it - or
            within half an FFT bin, whichever is wider, since the peak
            interpolation cannot do better than that.  25 cents is a quarter of
            a semitone: wide enough for the few cents of inharmonicity a real
            instrument has, narrow enough that an equal-tempered major third
            (14 cents from the 5th harmonic at the octave, 386 vs 400 cents) is
            not mistaken for one. */
        constexpr double kPolyphonyToleranceCents = 25.0;
        constexpr int    kPolyphonyMaxHarmonic    = 16;
        constexpr int    kPolyphonyMaxPeaks       = 20;
        constexpr float  kPolyphonyPeakFloor      = 0.02f;  ///< of the frame's peak
        constexpr float  kPolyphonyPeakToMedian   = 20.0f;  ///< tonal-enough gate

        /** Loopability.  The join step is compared against the file's own
            typical sample-to-sample step, the same way the FX off-edge test
            compares a switch-off step against the programme's - a ratio of 1
            means the wrap is as smooth as the material itself. */
        constexpr float  kLoopStepRatioLimit   = 6.0f;
        constexpr float  kLoopLevelToleranceDb = 12.0f;
        constexpr double kLoopWindowSeconds    = 0.100;

        // ===================================================================
        //  Small helpers
        // ===================================================================

        bool aborted (const SampleAnalyser::AbortCheck& check)
        {
            return check && check();
        }

        void report (const SampleAnalyser::ProgressSink& sink, float value)
        {
            if (sink)
                sink (juce::jlimit (0.0f, 1.0f, value));
        }

        float percentileOf (std::vector<float> values, float fraction)
        {
            if (values.empty())
                return 0.0f;

            const auto index = (size_t) juce::jlimit (0, (int) values.size() - 1,
                                                      juce::roundToInt (fraction * (float) (values.size() - 1)));

            std::nth_element (values.begin(), values.begin() + (std::ptrdiff_t) index, values.end());
            return values[index];
        }

        /** A transposed direct-form-II biquad, used only for the K-weighting. */
        struct Biquad
        {
            double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
            double z1 = 0.0, z2 = 0.0;

            void reset() noexcept { z1 = z2 = 0.0; }

            double process (double x) noexcept
            {
                const double y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };

        /** BS.1770-4 K-weighting, stage 1: a high shelf of +4 dB above about
            1.7 kHz, standing in for the acoustic effect of a head.  The
            standard tabulates coefficients at 48 kHz only; these are the RBJ
            cookbook shelf at the standard's own f0/Q/gain, which reproduces
            that table at 48 kHz and tracks it at every other rate. */
        Biquad kWeightingShelf (double rate)
        {
            const double f0 = 1681.974450955533;
            const double G  = 3.999843853973347;
            const double Q  = 0.7071752369554196;

            const double A  = std::pow (10.0, G / 40.0);
            const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / rate;
            const double c  = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * Q);
            const double sqrtA2alpha = 2.0 * std::sqrt (A) * alpha;

            const double a0 = (A + 1.0) - (A - 1.0) * c + sqrtA2alpha;

            Biquad b;
            b.b0 =  A * ((A + 1.0) + (A - 1.0) * c + sqrtA2alpha) / a0;
            b.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0;
            b.b2 =  A * ((A + 1.0) + (A - 1.0) * c - sqrtA2alpha) / a0;
            b.a1 =  2.0 * ((A - 1.0) - (A + 1.0) * c) / a0;
            b.a2 = ((A + 1.0) - (A - 1.0) * c - sqrtA2alpha) / a0;
            return b;
        }

        /** K-weighting, stage 2: a 38 Hz high-pass (RLB). */
        Biquad kWeightingHighPass (double rate)
        {
            const double f0 = 38.13547087602444;
            const double Q  = 0.5003270373238773;

            const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / rate;
            const double c  = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * Q);
            const double a0 = 1.0 + alpha;

            Biquad b;
            b.b0 =  (1.0 + c) * 0.5 / a0;
            b.b1 = -(1.0 + c) / a0;
            b.b2 =  (1.0 + c) * 0.5 / a0;
            b.a1 = -2.0 * c / a0;
            b.a2 =  (1.0 - alpha) / a0;
            return b;
        }

        // ===================================================================
        //  The short-time transform
        // ===================================================================

        struct Stft
        {
            int size = 0, order = 0, hop = 0;
            double binHz = 0.0;
            std::vector<float> window;
            std::vector<float> scratch;   // 2 * size, as juce::dsp::FFT wants
            std::unique_ptr<juce::dsp::FFT> fft;
            float magScale = 1.0f;        // so a full-scale sine reads about 1.0

            int numBins() const noexcept { return size / 2; }
        };

        /** Nearest power of two to `rate * seconds`, clamped.  Nearest rather
            than next: at 96 kHz "next" would double the onset window to 43 ms
            and halve the timing resolution the transient positions depend on. */
        int orderForSeconds (double rate, double seconds)
        {
            const double target = juce::jmax (1.0, rate * seconds);
            const int order = juce::roundToInt (std::log2 (target));
            return juce::jlimit (kMinFftOrder, kMaxFftOrder, order);
        }

        Stft makeStft (double rate, double frameSeconds, int hopDivisor)
        {
            Stft s;
            s.order = orderForSeconds (rate, frameSeconds);
            s.size  = 1 << s.order;
            s.hop   = juce::jmax (1, s.size / hopDivisor);
            s.binHz = rate / (double) s.size;
            s.fft   = std::make_unique<juce::dsp::FFT> (s.order);
            s.scratch.assign ((size_t) s.size * 2, 0.0f);

            // Hann, written out rather than taken from WindowingFunction so
            // that the normalisation below is provably the matching one.
            s.window.resize ((size_t) s.size);

            for (int i = 0; i < s.size; ++i)
                s.window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                               * (float) i / (float) s.size);

            // Coherent gain of a Hann window is 0.5, and a real sine of
            // amplitude A puts A * size / 2 into its bin before windowing.
            s.magScale = 4.0f / (float) s.size;
            return s;
        }

        /** Fills `scratch` with the windowed mono sum of the frame centred on
            `centreSample`, transforms it, and leaves magnitudes in scratch[0 ..
            numBins).  Reads past either end of the file as zero.  Returns the
            mean square of the unwindowed mono sum over the frame. */
        double transformFrame (Stft& s, const juce::AudioBuffer<float>& b, int centreSample)
        {
            const int len   = b.getNumSamples();
            const int numCh = juce::jmax (1, b.getNumChannels());
            const float inv = 1.0f / (float) numCh;
            const int start = centreSample - s.size / 2;

            std::fill (s.scratch.begin(), s.scratch.end(), 0.0f);

            double energy = 0.0;

            for (int i = 0; i < s.size; ++i)
            {
                const int idx = start + i;

                if (idx < 0 || idx >= len)
                    continue;

                float sum = 0.0f;

                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                {
                    const float x = b.getReadPointer (ch)[idx];
                    sum += std::isfinite (x) ? x : 0.0f;
                }

                sum *= inv;
                energy += (double) sum * sum;
                s.scratch[(size_t) i] = sum * s.window[(size_t) i];
            }

            s.fft->performFrequencyOnlyForwardTransform (s.scratch.data());

            for (int i = 0; i < s.numBins(); ++i)
                s.scratch[(size_t) i] *= s.magScale;

            return energy / (double) s.size;
        }

        // ===================================================================
        //  Spectral peaks, for the polyphony estimate
        // ===================================================================

        struct SpectralPeak
        {
            double freq = 0.0;
            float  mag  = 0.0f;
        };

        void findPeaks (const float* mag, int numBins, double binHz,
                        int firstBin, int lastBin, float floorMag, int maxPeaks,
                        std::vector<SpectralPeak>& out)
        {
            out.clear();

            for (int i = juce::jmax (1, firstBin); i < juce::jmin (numBins - 1, lastBin); ++i)
            {
                const float m = mag[i];

                if (m < floorMag || m < mag[i - 1] || m < mag[i + 1])
                    continue;

                // Parabolic interpolation on the log magnitude - the standard
                // refinement, and worth having: without it a peak is only
                // located to the nearest bin, which at the bottom of the range
                // is a whole semitone.
                const double a = std::log ((double) mag[i - 1] + 1.0e-20);
                const double bb = std::log ((double) m + 1.0e-20);
                const double c = std::log ((double) mag[i + 1] + 1.0e-20);
                const double denom = a - 2.0 * bb + c;

                double delta = 0.0;

                if (std::abs (denom) > 1.0e-12)
                    delta = juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / denom);

                out.push_back ({ ((double) i + delta) * binHz, m });
            }

            if (maxPeaks > 0 && (int) out.size() > maxPeaks)
            {
                std::partial_sort (out.begin(), out.begin() + maxPeaks, out.end(),
                                   [] (const SpectralPeak& x, const SpectralPeak& y) { return x.mag > y.mag; });
                out.resize ((size_t) maxPeaks);
            }
        }

        /** Spreads one peak over the pitch classes near it.  See
            kChromaWindowSemitones for why this is done from a peak's
            interpolated frequency rather than from a bin index. */
        void accumulatePitchWeight (double freq, double magnitude, std::array<double, 12>& out)
        {
            if (! (freq > 0.0) || ! (magnitude > 0.0))
                return;

            const double pitch = 69.0 + 12.0 * std::log2 (freq / 440.0);
            const double nearest = std::round (pitch);

            for (int k = -1; k <= 1; ++k)
            {
                const double target = nearest + (double) k;
                const double distance = std::abs (pitch - target);

                if (distance >= kChromaWindowSemitones * 0.5)
                    continue;

                const double w = std::cos (juce::MathConstants<double>::pi
                                             * distance / kChromaWindowSemitones);

                const int raw = (int) std::llround (target);
                out[(size_t) (((raw % 12) + 12) % 12)] += w * w * magnitude;
            }
        }

        /** 1 - (energy explained by the best single harmonic series).  Zero for
            one note however rich it is; high for two notes however simple. */
        float polyphonyResidual (const std::vector<SpectralPeak>& peaks, double binHz)
        {
            if (peaks.size() < 2)
                return 0.0f;

            float total = 0.0f;

            for (const auto& p : peaks)
                total += p.mag;

            if (total <= 0.0f)
                return 0.0f;

            float best = 0.0f;

            for (const auto& candidate : peaks)
            {
                if (candidate.freq <= 0.0)
                    continue;

                // The tolerance cannot be tighter than the peak interpolation
                // is accurate, which is about half a bin.
                const double halfBinCents = 1200.0 * std::log2 (1.0 + 0.5 * binHz / candidate.freq);
                const double tolerance = juce::jmax (kPolyphonyToleranceCents, halfBinCents);

                float explained = 0.0f;

                for (const auto& p : peaks)
                {
                    const double ratio = p.freq / candidate.freq;

                    if (ratio < 0.98)
                        continue;   // below the candidate: not a harmonic of it

                    const int h = juce::jlimit (1, kPolyphonyMaxHarmonic,
                                                (int) std::llround (ratio));
                    const double cents = 1200.0 * std::log2 (p.freq / (candidate.freq * (double) h));

                    if (std::abs (cents) <= tolerance)
                        explained += p.mag;
                }

                best = juce::jmax (best, explained);
            }

            return juce::jlimit (0.0f, 1.0f, 1.0f - best / total);
        }

        // ===================================================================
        //  Transient position refinement
        // ===================================================================

        /** The flux peak names a frame, not a sample: the detection function
            rises as soon as the attack enters the analysis window, which is up
            to half a window early.  This walks the actual waveform inside that
            window, finds the loudest point, and backs off to where the rise
            started.  Accuracy is the envelope step, half a millisecond. */
        int refineOnsetPosition (const juce::AudioBuffer<float>& b, int frameCentre,
                                 int searchBack, int searchForward, double rate)
        {
            const int len = b.getNumSamples();
            const int lo  = juce::jlimit (0, juce::jmax (0, len - 1), frameCentre - searchBack);
            const int hi  = juce::jlimit (0, len, frameCentre + searchForward);

            const int step = juce::jmax (1, juce::roundToInt (rate * 0.0005));

            if (hi - lo < step * 4)
                return juce::jlimit (0, juce::jmax (0, len - 1), frameCentre);

            const int numSteps = (hi - lo) / step;
            std::vector<float> env ((size_t) numSteps, 0.0f);

            for (int k = 0; k < numSteps; ++k)
            {
                double sumSq = 0.0;
                const int from = lo + k * step;

                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                {
                    const auto* d = b.getReadPointer (ch);

                    for (int i = from; i < from + step; ++i)
                    {
                        const float x = d[i];
                        sumSq += std::isfinite (x) ? (double) x * x : 0.0;
                    }
                }

                env[(size_t) k] = (float) std::sqrt (sumSq / (double) (step * juce::jmax (1, b.getNumChannels())));
            }

            const auto maxIt = std::max_element (env.begin(), env.end());
            const float maxEnv = *maxIt;

            if (maxEnv <= 0.0f)
                return juce::jlimit (0, juce::jmax (0, len - 1), frameCentre);

            int k = (int) std::distance (env.begin(), maxIt);

            while (k > 0 && env[(size_t) (k - 1)] > 0.15f * maxEnv)
                --k;

            return juce::jlimit (0, juce::jmax (0, len - 1), lo + k * step);
        }
    }

    // =======================================================================
    //  The analysis
    // =======================================================================

    AnalysisResult SampleAnalyser::analyse (const SampleBuffer& sample,
                                            Detail* detailOut,
                                            const AbortCheck& abort,
                                            const ProgressSink& progressSink)
    {
        AnalysisResult r;
        Detail detail;

        const auto finish = [&] () -> AnalysisResult
        {
            if (detailOut != nullptr)
                *detailOut = detail;

            // Nothing leaves this function non-finite.  A NaN in the session
            // tree would reach the mutation engine as a transposition.
            const auto clean = [] (float& v, float undetermined)
            {
                if (! std::isfinite (v))
                    v = undetermined;
            };

            clean (r.rootConfidence, 0.0f);
            clean (r.scaleConfidence, 0.0f);
            clean (r.tempoConfidence, 0.0f);
            clean (r.peakLevel, 0.0f);
            clean (r.loudness, -144.0f);
            clean (r.spectralCentroid, 0.0f);
            clean (r.spectralRolloff, 0.0f);
            clean (r.lowEnergy, 0.0f);
            clean (r.highEnergy, 0.0f);
            clean (r.percussiveRatio, 0.0f);
            clean (r.polyphonicLikelihood, 0.0f);
            clean (r.loopability, 0.0f);
            clean (r.silenceRatio, 0.0f);

            if (! std::isfinite (r.tempo))
            {
                r.tempo = 0.0;
                r.tempoConfidence = 0.0f;
            }

            return r;
        };

        const auto& b = sample.audio;
        const int len = b.getNumSamples();
        const int numChannels = b.getNumChannels();
        const double rate = sample.sourceRate > 0.0 ? sample.sourceRate : 44100.0;

        if (len <= 0 || numChannels <= 0)
            return finish();            // nothing was analysed; `analysed` stays false

        report (progressSink, 0.0f);

        // -------------------------------------------------------------------
        //  1.  Time domain: peak, block levels, silence, loudness
        // -------------------------------------------------------------------
        const int blockLen  = juce::jmax (1, juce::roundToInt (rate * kSilenceBlockSeconds));
        const int numBlocks = (len + blockLen - 1) / blockLen;

        std::vector<double> blockEnergy ((size_t) numBlocks, 0.0);
        float peak = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* d = b.getReadPointer (ch);

            for (int i = 0; i < len; ++i)
            {
                const float x = d[i];

                if (! std::isfinite (x))
                    continue;

                peak = juce::jmax (peak, std::abs (x));
                blockEnergy[(size_t) (i / blockLen)] += (double) x * x;
            }

            if (aborted (abort))
            {
                detail.wasAborted = true;
                return finish();
            }
        }

        r.peakLevel = peak;

        std::vector<float> blockRms ((size_t) numBlocks, 0.0f);

        for (int i = 0; i < numBlocks; ++i)
        {
            const int samplesInBlock = juce::jmin (blockLen, len - i * blockLen);
            const double n = (double) juce::jmax (1, samplesInBlock * numChannels);
            blockRms[(size_t) i] = (float) std::sqrt (blockEnergy[(size_t) i] / n);
        }

        const float silenceFloor = juce::jmax (kSilenceAbsolute,
                                               peak * juce::Decibels::decibelsToGain (kSilenceRelativeDb));

        int silentBlocks = 0;

        for (const auto rms : blockRms)
            if (rms < silenceFloor)
                ++silentBlocks;

        r.silenceRatio = numBlocks > 0 ? (float) silentBlocks / (float) numBlocks : 0.0f;

        report (progressSink, 0.05f);

        // A file with nothing in it is analysed - the answer is simply that
        // there is nothing to say about it.  silenceRatio is the one field that
        // is not left undetermined, because 1.0 is the measurement.
        if (peak < kSilentFilePeak)
        {
            detail.wasSilent = true;
            r.silenceRatio = 1.0f;
            r.analysed = true;
            report (progressSink, 1.0f);
            return finish();
        }

        // -- K-weighted loudness, BS.1770-4 ---------------------------------
        {
            const int gateLen  = juce::jmax (1, juce::roundToInt (rate * 0.400));
            const int gateStep = juce::jmax (1, gateLen / 4);      // 75 % overlap

            std::vector<double> blockPower;   // sum over channels of G * meanSquare

            const int usableBlocks = len >= gateLen ? (len - gateLen) / gateStep + 1 : 0;

            if (usableBlocks > 0)
                blockPower.assign ((size_t) usableBlocks, 0.0);
            else
                blockPower.assign (1, 0.0);   // short-file fallback, see below

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto shelf = kWeightingShelf (rate);
                auto hp    = kWeightingHighPass (rate);
                shelf.reset();
                hp.reset();

                // Filter once into a running square accumulator per gate block.
                // A sample belongs to up to four overlapping blocks, so the
                // running sums are kept rather than the filtered signal - a
                // five-minute file at 96 kHz does not want a second copy.
                std::vector<double> running ((size_t) juce::jmax (1, usableBlocks), 0.0);

                for (int i = 0; i < len; ++i)
                {
                    const float raw = b.getReadPointer (ch)[i];
                    const double x = std::isfinite (raw) ? (double) raw : 0.0;
                    const double y = hp.process (shelf.process (x));
                    const double sq = y * y;

                    if (usableBlocks > 0)
                    {
                        const int last  = juce::jmin (usableBlocks - 1, i / gateStep);
                        const int first = juce::jmax (0, (i - gateLen + 1 + gateStep - 1) / gateStep);

                        for (int bIdx = first; bIdx <= last; ++bIdx)
                            running[(size_t) bIdx] += sq;
                    }
                    else
                    {
                        running[0] += sq;
                    }
                }

                const double norm = usableBlocks > 0 ? (double) gateLen : (double) len;

                for (size_t i = 0; i < running.size(); ++i)
                    blockPower[i] += running[i] / norm;      // channel weight G = 1.0

                if (aborted (abort))
                {
                    detail.wasAborted = true;
                    return finish();
                }
            }

            const auto blockLoudness = [] (double power)
            {
                return -0.691 + 10.0 * std::log10 (juce::jmax (1.0e-20, power));
            };

            // Two-stage gate: absolute at -70 LUFS, then relative at -10 LU
            // below the mean of what survived the first.
            double sum = 0.0;
            int count = 0;

            for (const auto p : blockPower)
                if (blockLoudness (p) > -70.0)
                {
                    sum += p;
                    ++count;
                }

            if (count == 0)
            {
                r.loudness = -144.0f;
            }
            else
            {
                const double relative = blockLoudness (sum / (double) count) - 10.0;

                double sum2 = 0.0;
                int count2 = 0;

                for (const auto p : blockPower)
                {
                    const double l = blockLoudness (p);

                    if (l > -70.0 && l > relative)
                    {
                        sum2 += p;
                        ++count2;
                    }
                }

                r.loudness = count2 > 0 ? (float) blockLoudness (sum2 / (double) count2)
                                        : (float) blockLoudness (sum / (double) count);
            }
        }

        report (progressSink, 0.15f);

        const bool spectrumPossible = len >= kMinSamplesForSpectrum;

        // -------------------------------------------------------------------
        //  2.  Onsets: spectral flux, peak picking, position refinement
        // -------------------------------------------------------------------
        std::vector<float> detectionFunction;
        double envelopeRate = 0.0;
        int onsetHop = 1;

        if (spectrumPossible)
        {
            auto stft = makeStft (rate, kOnsetFrameSeconds, kOnsetHopDivisor);
            onsetHop  = stft.hop;
            envelopeRate = rate / (double) stft.hop;

            // -- the band map, built once ------------------------------
            const double topHz = juce::jmin (kFluxHighHz, rate * 0.45);
            std::vector<int> bandFirst, bandLast;

            {
                int previousLast = 0;

                for (int k = 0; k < kFluxBands; ++k)
                {
                    const double lo = kFluxLowHz * std::pow (topHz / kFluxLowHz,
                                                             (double) k / (double) kFluxBands);
                    const double hi = kFluxLowHz * std::pow (topHz / kFluxLowHz,
                                                             (double) (k + 1) / (double) kFluxBands);

                    const int first = juce::jmax (previousLast + 1, (int) std::floor (lo / stft.binHz));
                    const int last  = juce::jmin (stft.numBins() - 1,
                                                  juce::jmax (first, (int) std::floor (hi / stft.binHz) - 1));

                    if (first > stft.numBins() - 1 || first > last)
                        continue;

                    bandFirst.push_back (first);
                    bandLast.push_back (last);
                    previousLast = last;
                }
            }

            const int numBands = (int) bandFirst.size();
            const int numFrames = juce::jmax (1, (len + stft.hop - 1) / stft.hop);

            std::vector<float> previous ((size_t) juce::jmax (1, numBands), 0.0f);
            std::vector<float> flux ((size_t) numFrames, 0.0f);

            for (int f = 0; f < numFrames; ++f)
            {
                transformFrame (stft, b, f * stft.hop);

                float sum = 0.0f;

                for (int band = 0; band < numBands; ++band)
                {
                    float m = 0.0f;

                    for (int i = bandFirst[(size_t) band]; i <= bandLast[(size_t) band]; ++i)
                        m += stft.scratch[(size_t) i];

                    // Flux on the compressed magnitude, not the raw one: a
                    // crescendo would otherwise make every later hit a bigger
                    // onset than every earlier one.
                    const float c = std::log1p (kFluxCompressionGamma * m);
                    const float diff = c - previous[(size_t) band];

                    if (diff > 0.0f)
                        sum += diff;

                    previous[(size_t) band] = c;
                }

                // Frame 0 is NOT forced to zero.  Its "previous frame" is
                // silence, which is the truth - the file has not started yet -
                // and zeroing it loses any onset that sits on sample 0.  A
                // click train whose first click is at sample 0 lost exactly one
                // onset out of sixteen before this was changed.
                flux[(size_t) f] = sum;

                if ((f & 31) == 0)
                {
                    if (aborted (abort))
                    {
                        detail.wasAborted = true;
                        return finish();
                    }

                    report (progressSink, 0.15f + 0.40f * (float) f / (float) numFrames);
                }
            }

            // Subtract a local mean and rectify: what is left is how much this
            // frame stands out from its neighbourhood, which is what an onset
            // is.  A steady loud passage has high flux and no onsets.
            const int meanHalf = juce::jmax (1, (int) std::round (kOnsetLocalMeanSeconds * envelopeRate * 0.5));

            detectionFunction.assign ((size_t) numFrames, 0.0f);

            std::vector<double> prefix ((size_t) numFrames + 1, 0.0);

            for (int f = 0; f < numFrames; ++f)
                prefix[(size_t) f + 1] = prefix[(size_t) f] + (double) flux[(size_t) f];

            for (int f = 0; f < numFrames; ++f)
            {
                const int lo = juce::jmax (0, f - meanHalf);
                const int hi = juce::jmin (numFrames, f + meanHalf + 1);
                const double mean = (prefix[(size_t) hi] - prefix[(size_t) lo]) / (double) juce::jmax (1, hi - lo);

                detectionFunction[(size_t) f] = (float) juce::jmax (0.0, (double) flux[(size_t) f] - mean);
            }

            // -- peak picking -----------------------------------------------
            const float globalMax = detectionFunction.empty()
                                      ? 0.0f
                                      : *std::max_element (detectionFunction.begin(), detectionFunction.end());
            const float p99 = percentileOf (detectionFunction, 0.99f);
            const float absoluteFloor = juce::jmax (kOnsetPercentileFloor * p99,
                                                    kOnsetGlobalMaxFloor * globalMax);

            const int minSpacing = juce::jmax (1, juce::roundToInt (kOnsetMinSpacingSeconds * rate));

            std::vector<double> dPrefix ((size_t) numFrames + 1, 0.0);
            std::vector<double> dPrefixSq ((size_t) numFrames + 1, 0.0);

            for (int f = 0; f < numFrames; ++f)
            {
                const double v = (double) detectionFunction[(size_t) f];
                dPrefix[(size_t) f + 1]   = dPrefix[(size_t) f] + v;
                dPrefixSq[(size_t) f + 1] = dPrefixSq[(size_t) f] + v * v;
            }

            int lastAccepted = std::numeric_limits<int>::min() / 2;
            float lastStrength = 0.0f;

            for (int f = 0; f < numFrames; ++f)
            {
                const float d = detectionFunction[(size_t) f];

                if (d <= 0.0f || d < absoluteFloor)
                    continue;

                bool isLocalMax = true;

                for (int k = juce::jmax (0, f - kOnsetLocalMaxFrames);
                     k < juce::jmin (numFrames, f + kOnsetLocalMaxFrames + 1); ++k)
                    if (detectionFunction[(size_t) k] > d)
                        isLocalMax = false;

                if (! isLocalMax)
                    continue;

                const int lo = juce::jmax (0, f - meanHalf);
                const int hi = juce::jmin (numFrames, f + meanHalf + 1);
                const double count = (double) juce::jmax (1, hi - lo);
                const double localMean = (dPrefix[(size_t) hi] - dPrefix[(size_t) lo]) / count;
                const double localMeanSq = (dPrefixSq[(size_t) hi] - dPrefixSq[(size_t) lo]) / count;
                const double localSigma = std::sqrt (juce::jmax (0.0, localMeanSq - localMean * localMean));

                if ((double) d < kOnsetLocalMultiple * localMean)
                    continue;

                if ((double) d < localMean + (double) kOnsetLocalSigmas * localSigma)
                    continue;

                // The flux peak sits roughly a quarter of a window BEFORE the
                // attack - it is where the windowed energy is rising fastest,
                // not where the attack is - so the search is asymmetric.
                const int position = refineOnsetPosition (b, f * stft.hop,
                                                          stft.size / 4, stft.size / 2, rate);

                if (position - lastAccepted < minSpacing)
                {
                    // Two candidates inside the minimum spacing are one onset.
                    // Keep the stronger of the two rather than whichever the
                    // scan happened to reach first, which on a kick means the
                    // hit itself rather than the shoulder in front of it.
                    if (! r.transients.empty() && d > lastStrength)
                    {
                        r.transients.back() = position;
                        lastAccepted = position;
                        lastStrength = d;
                    }

                    continue;
                }

                r.transients.push_back (position);
                lastAccepted = position;
                lastStrength = d;
            }

            std::sort (r.transients.begin(), r.transients.end());
            r.transients.erase (std::unique (r.transients.begin(), r.transients.end()), r.transients.end());
        }

        detail.onsetEnvelope = detectionFunction;
        detail.envelopeRate  = envelopeRate;

        report (progressSink, 0.55f);

        if (aborted (abort))
        {
            detail.wasAborted = true;
            return finish();
        }

        // -------------------------------------------------------------------
        //  3.  Tempo: autocorrelation of the onset envelope, then a refit
        // -------------------------------------------------------------------
        if (! detectionFunction.empty()
            && (int) r.transients.size() >= kTempoMinOnsets
            && (double) len / rate >= kTempoMinSeconds)
        {
            const int n = (int) detectionFunction.size();

            double mean = 0.0;

            for (const auto v : detectionFunction)
                mean += (double) v;

            mean /= (double) n;

            std::vector<double> x ((size_t) n);
            double variance = 0.0;

            for (int i = 0; i < n; ++i)
            {
                x[(size_t) i] = (double) detectionFunction[(size_t) i] - mean;
                variance += x[(size_t) i] * x[(size_t) i];
            }

            variance /= (double) n;

            const int lagMin = juce::jmax (1, (int) std::floor (envelopeRate * 60.0 / kTempoMaxBpm));
            const int lagMax = juce::jmin (n - 2, (int) std::ceil (envelopeRate * 60.0 / kTempoMinBpm));

            if (variance > 1.0e-18 && lagMax > lagMin)
            {
                std::vector<double> acf ((size_t) (lagMax + 1), 0.0);

                double bestScore = 0.0;
                int bestLag = 0;

                for (int lag = lagMin; lag <= lagMax; ++lag)
                {
                    double sum = 0.0;

                    for (int i = 0; i + lag < n; ++i)
                        sum += x[(size_t) i] * x[(size_t) (i + lag)];

                    const double rr = sum / (double) (n - lag) / variance;
                    acf[(size_t) lag] = rr;

                    const double bpm = 60.0 * envelopeRate / (double) lag;
                    const double octaves = std::log2 (bpm / kTempoPriorCentreBpm) / kTempoPriorOctaves;
                    const double prior = std::exp (-0.5 * octaves * octaves);

                    const double score = rr * prior;

                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestLag = lag;
                    }

                    if ((lag & 15) == 0 && aborted (abort))
                    {
                        detail.wasAborted = true;
                        return finish();
                    }
                }

                if (bestLag > lagMin && bestLag < lagMax && bestScore > 0.0)
                {
                    // Parabolic interpolation: one frame of lag is over 1 % of
                    // the tempo at 120 BPM, which is audible as drift.
                    const double a = acf[(size_t) (bestLag - 1)];
                    const double c = acf[(size_t) (bestLag + 1)];
                    const double denom = a - 2.0 * acf[(size_t) bestLag] + c;

                    double refined = (double) bestLag;

                    if (std::abs (denom) > 1.0e-12)
                        refined += juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / denom);

                    double periodSamples = refined * (double) onsetHop;
                    detail.tempoAutocorrPeak = (float) juce::jlimit (0.0, 1.0, acf[(size_t) bestLag]);
                    detail.tempoBeforePrior = 60.0 * rate / juce::jmax (1.0, periodSamples);

                    // -- phase concentration --------------------------------
                    // Does the envelope's period actually describe where the
                    // onsets are?  The circular mean of each onset's position
                    // modulo the period is 1 when every onset sits at the same
                    // point of the cycle and falls to 1/sqrt(N) for onsets
                    // scattered at random.
                    const auto concentrationAt = [&r] (double period) -> std::pair<float, double>
                    {
                        if (period <= 0.0 || r.transients.size() < 2)
                            return { 0.0f, 0.0 };

                        double sumCos = 0.0, sumSin = 0.0;

                        for (const auto t : r.transients)
                        {
                            const double phase = juce::MathConstants<double>::twoPi
                                                   * (std::fmod ((double) t, period) / period);
                            sumCos += std::cos (phase);
                            sumSin += std::sin (phase);
                        }

                        const double n2 = (double) r.transients.size();
                        const double magnitude = std::sqrt (sumCos * sumCos + sumSin * sumSin) / n2;
                        const double meanPhase = std::atan2 (sumSin, sumCos);

                        return { (float) juce::jlimit (0.0, 1.0, magnitude),
                                 meanPhase / juce::MathConstants<double>::twoPi * period };
                    };

                    // N random onsets already give a concentration of about
                    // 1/sqrt(N) with no grid in them at all, and N is small
                    // enough here for that to matter: sixteen onsets score 0.25
                    // for nothing.  Subtract it.
                    const double onsetCount = (double) juce::jmax (2, (int) r.transients.size());
                    const double chance = 1.0 / std::sqrt (onsetCount);

                    const auto correct = [chance] (float raw)
                    {
                        return (float) juce::jlimit (0.0, 1.0,
                                                     ((double) raw - chance) / (1.0 - chance));
                    };

                    // Onsets do not have to be ON the beat, only on a regular
                    // subdivision of it - eighths at 120 BPM alternate between
                    // phase 0 and phase pi of the beat and score zero against
                    // the beat itself, which is how a working loop came back
                    // with no tempo.  So the grid is looked for at the beat and
                    // at its halves, thirds and quarters, and the period is
                    // refitted against whichever grid the onsets are really on.
                    int bestDivision = 1;
                    float concentration = 0.0f;
                    double offset = 0.0;

                    for (int division = 1; division <= kTempoMaxSubdivision; ++division)
                    {
                        const double sub = periodSamples / (double) division;

                        if (sub < rate * 0.05)      // under 50 ms is a flam, not a grid
                            continue;

                        const auto [raw, off] = concentrationAt (sub);
                        const float corrected = correct (raw);

                        if (corrected > concentration)
                        {
                            concentration = corrected;
                            bestDivision = division;
                            offset = off;
                        }
                    }

                    // Refit the grid against the onsets themselves.  The ACF
                    // resolution is one hop; a least-squares fit over the grid
                    // is limited only by how well the onsets were located.
                    {
                        double gridPeriod = periodSamples / (double) bestDivision;

                        double sumM = 0.0, sumT = 0.0, sumMM = 0.0, sumMT = 0.0;
                        int fitted = 0;

                        for (const auto t : r.transients)
                        {
                            const double m = std::round (((double) t - offset) / gridPeriod);
                            const double predicted = offset + m * gridPeriod;

                            if (std::abs ((double) t - predicted) > 0.25 * gridPeriod)
                                continue;

                            sumM += m;
                            sumT += (double) t;
                            sumMM += m * m;
                            sumMT += m * (double) t;
                            ++fitted;
                        }

                        if (fitted >= kTempoMinOnsets)
                        {
                            const double denom2 = (double) fitted * sumMM - sumM * sumM;

                            if (std::abs (denom2) > 1.0e-9)
                            {
                                const double slope = ((double) fitted * sumMT - sumM * sumT) / denom2;

                                if (slope > 0.0 && std::abs (slope - gridPeriod) < 0.2 * gridPeriod)
                                {
                                    gridPeriod = slope;
                                    concentration = juce::jmax (concentration,
                                                                correct (concentrationAt (gridPeriod).first));
                                    periodSamples = gridPeriod * (double) bestDivision;
                                }
                            }
                        }
                    }

                    detail.tempoPhaseConcentration = concentration;

                    const float acfTerm = juce::jlimit (0.0f, 1.0f,
                        (detail.tempoAutocorrPeak - kTempoAcfZero) / (kTempoAcfFull - kTempoAcfZero));
                    const float phaseTerm = juce::jlimit (0.0f, 1.0f,
                        (concentration - kTempoPhaseZero) / (kTempoPhaseFull - kTempoPhaseZero));

                    const float confidence = acfTerm * phaseTerm;
                    const double bpm = 60.0 * rate / juce::jmax (1.0, periodSamples);

                    // A tempo is only reported when there is some reason to
                    // believe it.  Zero confidence means zero tempo, so a
                    // consumer that reads `tempo` without reading the
                    // confidence beside it still gets "I do not know".
                    if (confidence > 0.0f && bpm >= kTempoMinBpm * 0.95 && bpm <= kTempoMaxBpm * 1.05)
                    {
                        r.tempo = bpm;
                        r.tempoConfidence = confidence;
                    }
                }
            }
        }

        report (progressSink, 0.60f);

        // -------------------------------------------------------------------
        //  4.  Chroma, spectrum, polyphony
        // -------------------------------------------------------------------
        std::array<double, 12> chromaTotal {};
        double chromaWeightTotal = 0.0;
        int qualifyingFrames = 0;

        if (spectrumPossible)
        {
            auto stft = makeStft (rate, kChromaFrameSeconds, kChromaHopDivisor);

            const int numFrames = juce::jmax (1, (len + stft.hop - 1) / stft.hop);
            const int numBins = stft.numBins();

            const int chromaLow  = juce::jmax (1, (int) std::ceil (kChromaLowHz / stft.binHz));
            const int chromaHigh = juce::jmin (numBins - 1, (int) std::floor (kChromaHighHz / stft.binHz));

            std::vector<double> spectrumAccum ((size_t) numBins, 0.0);
            int spectrumFrames = 0;

            const float fluxP95 = detectionFunction.empty() ? 0.0f
                                                            : percentileOf (detectionFunction, 0.95f);

            std::vector<SpectralPeak> peaks;
            std::vector<SpectralPeak> chromaPeaks;
            std::vector<float> sortedMags;
            double polyphonyAccum = 0.0, polyphonyWeight = 0.0;

            // What a perfectly flat spectrum would deposit in each pitch class.
            // Semitones are geometrically spaced, so B covers nearly twice as
            // many FFT bins as C in the same octave and a raw sum would tilt
            // white noise towards B - handing the key detector a profile that
            // looks like evidence.  Dividing by this removes the bias exactly.
            std::array<double, 12> classReference {};

            for (int i = chromaLow; i <= chromaHigh; ++i)
                accumulatePitchWeight ((double) i * stft.binHz, 1.0, classReference);

            const float frameEnergyGate = silenceFloor * silenceFloor;

            for (int f = 0; f < numFrames; ++f)
            {
                const int centre = f * stft.hop;
                const double meanSquare = transformFrame (stft, b, centre);

                const float* mag = stft.scratch.data();

                for (int i = 0; i < numBins; ++i)
                    spectrumAccum[(size_t) i] += (double) mag[i];

                ++spectrumFrames;

                if ((f & 15) == 0)
                {
                    if (aborted (abort))
                    {
                        detail.wasAborted = true;
                        return finish();
                    }

                    report (progressSink, 0.60f + 0.35f * (float) f / (float) numFrames);
                }

                if (meanSquare < (double) frameEnergyGate)
                    continue;

                ++qualifyingFrames;

                // -- this frame's chroma ------------------------------------
                float frameMax = 0.0f;

                for (int i = chromaLow; i <= chromaHigh; ++i)
                    frameMax = juce::jmax (frameMax, mag[i]);

                if (frameMax <= 0.0f)
                    continue;

                findPeaks (mag, numBins, stft.binHz, chromaLow, chromaHigh,
                           kChromaPeakFloor * frameMax, 0, chromaPeaks);

                std::array<double, 12> acc {};

                for (const auto& peak : chromaPeaks)
                    accumulatePitchWeight (peak.freq, (double) peak.mag / frameMax, acc);

                std::array<double, 12> frameChroma {};
                double frameChromaMax = 0.0;

                for (int c = 0; c < 12; ++c)
                {
                    frameChroma[(size_t) c] = classReference[(size_t) c] > 0.0
                                                ? acc[(size_t) c] / classReference[(size_t) c] : 0.0;
                    frameChromaMax = juce::jmax (frameChromaMax, frameChroma[(size_t) c]);
                }

                if (frameChromaMax <= 0.0)
                    continue;

                // Each frame is normalised to its own peak before it is added
                // in, so a loud bar and a quiet bar carry the same weight in the
                // profile and the key is not decided by the loudest chord.
                for (int c = 0; c < 12; ++c)
                    frameChroma[(size_t) c] /= frameChromaMax;

                // Weight the frame towards the sustained part of the file.
                float sustainWeight = 1.0f;

                if (fluxP95 > 0.0f && envelopeRate > 0.0 && ! detectionFunction.empty())
                {
                    const int idx = juce::jlimit (0, (int) detectionFunction.size() - 1,
                                                  (int) std::round ((double) centre / (double) onsetHop));
                    const float normFlux = juce::jlimit (0.0f, 1.0f, detectionFunction[(size_t) idx] / fluxP95);
                    sustainWeight = 1.0f - kChromaTransientDiscount * normFlux;
                }

                for (int c = 0; c < 12; ++c)
                    chromaTotal[(size_t) c] += (double) sustainWeight * frameChroma[(size_t) c];

                chromaWeightTotal += (double) sustainWeight;

                // -- polyphony ----------------------------------------------
                sortedMags.assign (mag + 1, mag + numBins);
                const auto median = percentileOf (sortedMags, 0.5f);

                if (median > 0.0f && frameMax / median >= kPolyphonyPeakToMedian)
                {
                    findPeaks (mag, numBins, stft.binHz,
                               juce::jmax (1, (int) std::floor (50.0 / stft.binHz)),
                               juce::jmin (numBins - 1, (int) std::ceil (5000.0 / stft.binHz)),
                               kPolyphonyPeakFloor * frameMax, kPolyphonyMaxPeaks, peaks);

                    const double w = std::sqrt (meanSquare);
                    polyphonyAccum += w * (double) polyphonyResidual (peaks, stft.binHz);
                    polyphonyWeight += w;
                }
            }

            if (polyphonyWeight > 0.0)
                r.polyphonicLikelihood = (float) juce::jlimit (0.0, 1.0, polyphonyAccum / polyphonyWeight);

            // -- centroid, rolloff, band fractions --------------------------
            if (spectrumFrames > 0)
            {
                double total = 0.0, weighted = 0.0, low = 0.0, high = 0.0;

                for (int i = 1; i < numBins; ++i)
                {
                    const double m = spectrumAccum[(size_t) i] / (double) spectrumFrames;
                    const double e = m * m;
                    const double freq = (double) i * stft.binHz;

                    total += e;
                    weighted += e * freq;

                    if (freq < 200.0)        low  += e;
                    else if (freq >= 4000.0) high += e;
                }

                if (total > 1.0e-24)
                {
                    r.spectralCentroid = (float) (weighted / total);
                    r.lowEnergy  = (float) (low / total);
                    r.highEnergy = (float) (high / total);

                    double running = 0.0;

                    for (int i = 1; i < numBins; ++i)
                    {
                        const double m = spectrumAccum[(size_t) i] / (double) spectrumFrames;
                        running += m * m;

                        if (running >= total * 0.85)
                        {
                            r.spectralRolloff = (float) ((double) i * stft.binHz);
                            break;
                        }
                    }
                }
            }
        }

        detail.chromaFrames = qualifyingFrames;

        // -- normalise the chroma and measure how peaked it is ---------------
        float salience = 0.0f;

        if (chromaWeightTotal > 0.0)
        {
            double maxValue = 0.0, sum = 0.0;

            for (int c = 0; c < 12; ++c)
            {
                chromaTotal[(size_t) c] /= chromaWeightTotal;
                maxValue = juce::jmax (maxValue, chromaTotal[(size_t) c]);
                sum += chromaTotal[(size_t) c];
            }

            if (maxValue > 1.0e-12)
            {
                for (int c = 0; c < 12; ++c)
                    detail.chroma[(size_t) c] = (float) (chromaTotal[(size_t) c] / maxValue);

                const double mean = sum / 12.0;

                if (mean > 1.0e-12)
                {
                    const double crest = maxValue / mean;
                    salience = (float) juce::jlimit (0.0, 1.0,
                                                     (crest - kSalienceFlat) / (kSalienceConvinced - kSalienceFlat));
                }
            }
        }

        detail.chromaSalience = salience;

        // -------------------------------------------------------------------
        //  5.  Key.  The detector is phase 20's; the chroma and the gate on it
        //      are this phase's.
        // -------------------------------------------------------------------
        if (salience > 0.0f && qualifyingFrames > 0)
        {
            const auto detection = harmony::detectKey (detail.chroma);

            if (detection.root >= 0)
            {
                const float coverage = juce::jlimit (0.0f, 1.0f,
                                                     (float) qualifyingFrames
                                                        / (float) kChromaFramesForFullConfidence);

                r.root  = detection.root;
                r.scale = (int) detection.scale;
                r.rootConfidence  = juce::jlimit (0.0f, 1.0f, detection.rootConfidence * salience * coverage);
                r.scaleConfidence = juce::jlimit (0.0f, 1.0f, detection.scaleConfidence * salience * coverage);

                // A root with no confidence left in it is not a root.
                if (r.rootConfidence <= 0.0f)
                {
                    r.root = -1;
                    r.scale = -1;
                    r.scaleConfidence = 0.0f;
                }
            }
        }

        report (progressSink, 0.95f);

        // -------------------------------------------------------------------
        //  6.  Character
        // -------------------------------------------------------------------
        {
            // -- percussive ratio -------------------------------------------
            double totalEnergy = 0.0;

            for (const auto e : blockEnergy)
                totalEnergy += e;

            if (totalEnergy > 0.0 && ! r.transients.empty())
            {
                const int windowBlocks = juce::jmax (1, (int) std::round (kPercussiveWindowSeconds / kSilenceBlockSeconds));
                std::vector<char> masked ((size_t) numBlocks, 0);

                for (const auto t : r.transients)
                {
                    const int firstBlock = juce::jlimit (0, numBlocks - 1, t / blockLen);

                    for (int k = firstBlock; k < juce::jmin (numBlocks, firstBlock + windowBlocks); ++k)
                        masked[(size_t) k] = 1;
                }

                double attackEnergy = 0.0;

                for (int i = 0; i < numBlocks; ++i)
                    if (masked[(size_t) i])
                        attackEnergy += blockEnergy[(size_t) i];

                r.percussiveRatio = (float) juce::jlimit (0.0, 1.0, attackEnergy / totalEnergy);
            }

            // -- loopability -------------------------------------------------
            const int windowLen = juce::jlimit (32, juce::jmax (32, len / 4),
                                                juce::roundToInt (rate * kLoopWindowSeconds));

            if (len >= windowLen * 2 + 2)
            {
                const auto monoAt = [&b, numChannels] (int i)
                {
                    float s = 0.0f;

                    for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    {
                        const float x = b.getReadPointer (ch)[i];
                        s += std::isfinite (x) ? x : 0.0f;
                    }

                    return s / (float) juce::jmax (1, numChannels);
                };

                double headSq = 0.0, tailSq = 0.0, stepSq = 0.0;

                for (int i = 0; i < windowLen; ++i)
                {
                    const float h = monoAt (i);
                    const float t = monoAt (len - windowLen + i);
                    headSq += (double) h * h;
                    tailSq += (double) t * t;
                }

                for (int i = 1; i < windowLen; ++i)
                {
                    const double dh = (double) monoAt (i) - monoAt (i - 1);
                    const double dt = (double) monoAt (len - windowLen + i) - monoAt (len - windowLen + i - 1);
                    stepSq += dh * dh + dt * dt;
                }

                const double headRms = std::sqrt (headSq / windowLen);
                const double tailRms = std::sqrt (tailSq / windowLen);
                const double typicalStep = std::sqrt (stepSq / juce::jmax (1, (windowLen - 1) * 2));

                if (headRms > 0.0 && tailRms > 0.0 && typicalStep > 0.0)
                {
                    const double joinStep = std::abs ((double) monoAt (0) - monoAt (len - 1));
                    const double ratio = joinStep / typicalStep;

                    const double edge = 1.0 - juce::jlimit (0.0, 1.0,
                                                            (ratio - 1.0) / (kLoopStepRatioLimit - 1.0));

                    const double levelDelta = std::abs (20.0 * std::log10 (headRms / tailRms));
                    const double level = 1.0 - juce::jlimit (0.0, 1.0, levelDelta / kLoopLevelToleranceDb);

                    // Spectral agreement between the two ends.  A loop whose
                    // tail is a cymbal wash and whose head is a bass note meets
                    // in level and in waveform and still does not loop.
                    double spectral = 1.0;

                    if (spectrumPossible)
                    {
                        auto edgeStft = makeStft (rate, (double) windowLen / rate, 1);

                        transformFrame (edgeStft, b, windowLen / 2);
                        std::vector<float> headSpec (edgeStft.scratch.begin(),
                                                     edgeStft.scratch.begin() + edgeStft.numBins());

                        transformFrame (edgeStft, b, len - windowLen / 2);

                        double dot = 0.0, na = 0.0, nb = 0.0;

                        for (int i = 1; i < edgeStft.numBins(); ++i)
                        {
                            const double a = headSpec[(size_t) i];
                            const double c = edgeStft.scratch[(size_t) i];
                            dot += a * c;
                            na += a * a;
                            nb += c * c;
                        }

                        if (na > 1.0e-24 && nb > 1.0e-24)
                            spectral = juce::jlimit (0.0, 1.0, dot / std::sqrt (na * nb));
                    }

                    r.loopability = (float) juce::jlimit (0.0, 1.0,
                                                          std::cbrt (juce::jmax (0.0, edge)
                                                                       * juce::jmax (0.0, level)
                                                                       * spectral));
                }
            }
        }

        r.analysed = true;
        report (progressSink, 1.0f);
        return finish();
    }

    // =======================================================================
    //  The background wrapper
    // =======================================================================

    SampleAnalyser::SampleAnalyser()
        : juce::Thread ("NACAR analysis")
    {
    }

    SampleAnalyser::~SampleAnalyser()
    {
        // Stop the worker before the AsyncUpdater base is destroyed, and before
        // anything the worker touches goes away.
        stopThread (4000);
        cancelPendingUpdate();
    }

    void SampleAnalyser::startAnalysis (SampleBuffer::Ptr buffer)
    {
        cancelAnalysis();

        pending = std::move (buffer);

        if (pending == nullptr || pending->isEmpty())
        {
            pending = nullptr;
            return;
        }

        progress.store (0.0f, std::memory_order_relaxed);
        startThread (juce::Thread::Priority::background);
    }

    void SampleAnalyser::cancelAnalysis()
    {
        // signalThreadShouldExit() is what the abort check inside analyse()
        // polls, so this returns as soon as the worker reaches its next check
        // rather than at the end of the file.
        stopThread (4000);
        cancelPendingUpdate();
        pending = nullptr;
        progress.store (0.0f, std::memory_order_relaxed);
    }

    bool SampleAnalyser::isAnalysing() const noexcept
    {
        return isThreadRunning();
    }

    AnalysisResult SampleAnalyser::getResult() const
    {
        const juce::ScopedLock sl (resultLock);
        return finishedResult;
    }

    void SampleAnalyser::run()
    {
        const SampleBuffer::Ptr buffer = pending;

        if (buffer == nullptr)
            return;

        const auto result = analyse (*buffer,
                                     nullptr,
                                     [this] { return threadShouldExit(); },
                                     [this] (float p) { progress.store (p, std::memory_order_relaxed); });

        if (threadShouldExit())
            return;

        {
            // Held for one struct copy and nothing else.  The message thread
            // never waits on the analysis itself.
            const juce::ScopedLock sl (resultLock);
            finishedResult = result;
        }

        triggerAsyncUpdate();
    }

    void SampleAnalyser::handleAsyncUpdate()
    {
        if (onAnalysisFinished)
            onAnalysisFinished (getResult());
    }
}
