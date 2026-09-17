#include "MutationOps.h"

#include "../Audio/Sources/Synth/SynthFilter.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace nacar::mutation::ops
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        /** Periodic hann, evaluated rather than tabulated: these run offline
            and a table would only make the file longer. */
        inline float hann (int i, int n) noexcept
        {
            if (n <= 1)
                return 1.0f;

            return 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) n);
        }

        /** Hermite read from a linear buffer, with the edges clamped. */
        inline float readAt (const float* d, int n, double position) noexcept
        {
            if (n <= 0)
                return 0.0f;

            const int    i = (int) std::floor (position);
            const float  f = (float) (position - (double) i);

            const auto at = [d, n] (int k) noexcept
            {
                return d[(size_t) juce::jlimit (0, n - 1, k)];
            };

            return fx::hermite (f, at (i - 1), at (i), at (i + 1), at (i + 2));
        }

        /** One forward-backward one-pole pass. The backward pass cancels the
            forward pass's phase, so the result is not delayed. */
        void zeroPhasePass (float* d, int n, float hz, double rate)
        {
            if (n <= 0)
                return;

            fx::OnePoleTPT forward;
            forward.setCutoff (hz, rate);
            forward.s = d[0];                    // primed: no edge transient

            for (int i = 0; i < n; ++i)
                d[i] = forward.lowpass (d[i]);

            fx::OnePoleTPT backward;
            backward.setCutoff (hz, rate);
            backward.s = d[n - 1];

            for (int i = n - 1; i >= 0; --i)
                d[i] = backward.lowpass (d[i]);
        }

        /** The corner a cascade of `poles` forward-and-backward one-poles has
            to be set to for its -3 dB point to land on `hz`.  Without this a
            "260 Hz" band split is really a 78 Hz one, and the low-end lock
            would be protecting the wrong band. */
        float compensatedCorner (float hz, int poles) noexcept
        {
            const double order = 2.0 * (double) juce::jmax (1, poles);
            const double k = std::sqrt (std::pow (2.0, 1.0 / order) - 1.0);

            return (float) ((double) hz / juce::jmax (1.0e-3, k));
        }
    }

    // =======================================================================
    //  Seeds
    // =======================================================================
    juce::uint32 streamSeed (juce::uint32 seed, juce::uint32 salt) noexcept
    {
        // splitmix-style avalanche.  Two salts that differ by one must give two
        // streams that share nothing, or every operation in a plan would hear a
        // shifted copy of its neighbour's noise.
        juce::uint32 x = seed + 0x9E3779B9u * (salt + 1u);

        x ^= x >> 16; x *= 0x7FEB352Du;
        x ^= x >> 15; x *= 0x846CA68Bu;
        x ^= x >> 16;

        return x | 1u;
    }

    // =======================================================================
    //  Measurement and hygiene
    // =======================================================================
    float peakOf (const Buffer& b) noexcept
    {
        float p = 0.0f;

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const auto* d = b.getReadPointer (c);

            for (int i = 0; i < b.getNumSamples(); ++i)
                if (std::isfinite (d[i]))
                    p = juce::jmax (p, std::abs (d[i]));
        }

        return p;
    }

    float rmsOf (const Buffer& b) noexcept
    {
        const int n = b.getNumSamples(), ch = b.getNumChannels();

        if (n <= 0 || ch <= 0)
            return 0.0f;

        double sum = 0.0;

        for (int c = 0; c < ch; ++c)
        {
            const auto* d = b.getReadPointer (c);

            for (int i = 0; i < n; ++i)
                if (std::isfinite (d[i]))
                    sum += (double) d[i] * (double) d[i];
        }

        return (float) std::sqrt (sum / ((double) n * (double) ch));
    }

    bool isFinite (const Buffer& b) noexcept
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const auto* d = b.getReadPointer (c);

            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (d[i]))
                    return false;
        }

        return true;
    }

    int sanitise (Buffer& b) noexcept
    {
        int found = 0;

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getWritePointer (c);

            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (d[i]))
                {
                    d[i] = 0.0f;
                    ++found;
                }
        }

        return found;
    }

    void applyGain (Buffer& b, float g) noexcept
    {
        if (std::isfinite (g))
            b.applyGain (g);
    }

    void mixInto (Buffer& destination, const Buffer& source, float gain) noexcept
    {
        const int n  = juce::jmin (destination.getNumSamples(), source.getNumSamples());
        const int ch = destination.getNumChannels();

        for (int c = 0; c < ch; ++c)
            destination.addFrom (c, 0, source, juce::jmin (c, source.getNumChannels() - 1),
                                 0, n, gain);
    }

    void setLengthExactly (Buffer& b, int samples)
    {
        samples = juce::jmax (0, samples);

        if (b.getNumSamples() == samples)
            return;

        const int ch = juce::jmax (1, b.getNumChannels());

        Buffer next (ch, juce::jmax (1, samples));
        next.clear();

        const int n = juce::jmin (samples, b.getNumSamples());

        for (int c = 0; c < juce::jmin (ch, b.getNumChannels()); ++c)
            next.copyFrom (c, 0, b, c, 0, n);

        if (samples == 0)
            next.setSize (ch, 0);

        b = std::move (next);
    }

    void fadeEdges (Buffer& b, float milliseconds, double rate)
    {
        const int n = b.getNumSamples();
        const int f = juce::jlimit (1, juce::jmax (1, n / 2),
                                    (int) (milliseconds * 0.001 * rate));

        if (n < 4)
            return;

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getWritePointer (c);

            for (int i = 0; i < f; ++i)
            {
                const float w = (float) i / (float) f;

                d[i]         *= w;
                d[n - 1 - i] *= w;
            }
        }
    }

    // =======================================================================
    //  Zero-phase band splitting
    // =======================================================================
    void lowBandInto (const Buffer& in, Buffer& out, float hz, double rate, int poles)
    {
        out.makeCopyOf (in);

        const int n = out.getNumSamples();

        if (n <= 0)
            return;

        poles = juce::jlimit (1, 8, poles);

        const float corner = compensatedCorner (hz, poles);

        for (int c = 0; c < out.getNumChannels(); ++c)
        {
            auto* d = out.getWritePointer (c);

            for (int p = 0; p < poles; ++p)
                zeroPhasePass (d, n, corner, rate);
        }
    }

    void removeLowBand (Buffer& b, float hz, double rate, int poles)
    {
        Buffer low;
        lowBandInto (b, low, hz, rate, poles);

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getWritePointer (c);
            const auto* l = low.getReadPointer (c);

            for (int i = 0; i < b.getNumSamples(); ++i)
                d[i] -= l[i];
        }
    }

    bool spliceLowBand (Buffer& target, const Buffer& source, float hz, double rate)
    {
        const int n = target.getNumSamples();

        if (n <= 0 || source.getNumSamples() != n)
            return false;

        int order = 1;

        while ((1 << order) < n && order < kMaxSpliceOrder)
            ++order;

        if ((1 << order) < n)
            return false;                       // too long to transform: say so

        const int size = 1 << order;

        juce::dsp::FFT fft (order);

        const double binHz = rate / (double) size;

        const double lower = (double) hz * 0.80;
        const double upper = (double) hz * 1.25;

        std::vector<float> a ((size_t) (2 * size), 0.0f);
        std::vector<float> b ((size_t) (2 * size), 0.0f);

        for (int c = 0; c < target.getNumChannels(); ++c)
        {
            const int sourceChannel = juce::jmin (c, source.getNumChannels() - 1);

            std::fill (a.begin(), a.end(), 0.0f);
            std::fill (b.begin(), b.end(), 0.0f);

            std::copy (target.getReadPointer (c), target.getReadPointer (c) + n, a.begin());
            std::copy (source.getReadPointer (sourceChannel),
                       source.getReadPointer (sourceChannel) + n, b.begin());

            fft.performRealOnlyForwardTransform (a.data(), true);
            fft.performRealOnlyForwardTransform (b.data(), true);

            for (int bin = 0; bin <= size / 2; ++bin)
            {
                const double f = (double) bin * binHz;

                float fromSource;

                if (f <= lower)        fromSource = 1.0f;
                else if (f >= upper)   fromSource = 0.0f;
                else                   fromSource = 0.5f + 0.5f * std::cos ((float) (kPi * (f - lower)
                                                                                     / (upper - lower)));

                if (fromSource <= 0.0f)
                    continue;

                const float keep = 1.0f - fromSource;

                a[(size_t) (2 * bin)]     = a[(size_t) (2 * bin)] * keep
                                          + b[(size_t) (2 * bin)] * fromSource;
                a[(size_t) (2 * bin + 1)] = a[(size_t) (2 * bin + 1)] * keep
                                          + b[(size_t) (2 * bin + 1)] * fromSource;
            }

            fft.performRealOnlyInverseTransform (a.data());

            auto* d = target.getWritePointer (c);

            for (int i = 0; i < n; ++i)
                d[i] = fx::guard (a[(size_t) i]);
        }

        return true;
    }

    void removeDc (Buffer& b, double rate)
    {
        // 8 Hz, and the corner matters: a DC blocker set too high takes a
        // measurable bite out of the bottom octave.  At 12 Hz this one was
        // costing 0.6 dB at 45 Hz, which is small everywhere except inside a
        // promise that the low end is untouched.
        removeLowBand (b, 8.0f, rate, 2);
    }

    // =======================================================================
    //  Time and pitch
    // =======================================================================
    void resampleBy (const Buffer& in, Buffer& out, double ratio)
    {
        ratio = juce::jlimit (0.03125, 32.0, ratio);

        const int inLen = in.getNumSamples();
        const int ch    = juce::jmax (1, in.getNumChannels());
        const int outLen = juce::jmax (1, (int) std::llround ((double) inLen / ratio));

        out.setSize (ch, outLen);
        out.clear();

        if (inLen <= 0)
            return;

        for (int c = 0; c < ch; ++c)
        {
            const auto* s = in.getReadPointer (juce::jmin (c, in.getNumChannels() - 1));
            auto* d = out.getWritePointer (c);

            for (int i = 0; i < outLen; ++i)
                d[i] = readAt (s, inLen, (double) i * ratio);
        }
    }

    void timeStretch (const Buffer& in, Buffer& out, double factor, double rate)
    {
        factor = juce::jlimit (0.125, 12.0, factor);

        const int inLen = in.getNumSamples();
        const int ch    = juce::jmax (1, in.getNumChannels());
        const int outLen = juce::jmax (1, (int) std::llround ((double) inLen * factor));

        out.setSize (ch, outLen);
        out.clear();

        if (inLen < 64)
        {
            // Too short to frame.  Resampling it is the honest fallback: the
            // pitch moves, but there is no periodicity left to protect.
            Buffer resampled;
            resampleBy (in, resampled, 1.0 / factor);

            for (int c = 0; c < ch; ++c)
                out.copyFrom (c, 0, resampled,
                              juce::jmin (c, resampled.getNumChannels() - 1),
                              0, juce::jmin (outLen, resampled.getNumSamples()));

            return;
        }

        // 60 ms frames at 50% overlap: long enough to hold a low fundamental,
        // short enough that the alignment search still finds a periodicity.
        const int frame = juce::jlimit (256, juce::jmax (256, inLen / 2),
                                        (int) (0.060 * rate) & ~1);
        const int synthHop = frame / 2;
        const double analysisHop = (double) synthHop / factor;

        // The alignment search.  Correlating the mono sum and applying the
        // result to every channel is what stops a stretch from decorrelating a
        // stereo pair.
        const int search  = juce::jlimit (0, frame / 4, (int) (0.010 * rate));
        const int corrLen = juce::jmin (frame / 2, 512);

        std::vector<float> mono ((size_t) inLen, 0.0f);

        for (int c = 0; c < in.getNumChannels(); ++c)
        {
            const auto* s = in.getReadPointer (c);

            for (int i = 0; i < inLen; ++i)
                mono[(size_t) i] += s[i];
        }

        int previousEnd = -1;      // where the last frame's tail came from

        for (int k = 0; ; ++k)
        {
            const int outPos = k * synthHop;

            if (outPos >= outLen)
                break;

            int analysisPos = (int) std::llround ((double) k * analysisHop);
            analysisPos = juce::jlimit (0, juce::jmax (0, inLen - 1), analysisPos);

            if (k > 0 && search > 0 && previousEnd >= 0)
            {
                // Match this frame's head against the material that should
                // follow the previous frame's tail.
                double best = -1.0e30;
                int bestOffset = 0;

                for (int offset = -search; offset <= search; offset += 2)
                {
                    const int a = analysisPos + offset;

                    if (a < 0 || a + corrLen >= inLen || previousEnd + corrLen >= inLen)
                        continue;

                    double sum = 0.0, energy = 1.0e-9;

                    for (int i = 0; i < corrLen; i += 2)
                    {
                        const double x = mono[(size_t) (a + i)];
                        const double y = mono[(size_t) (previousEnd + i)];

                        sum += x * y;
                        energy += x * x;
                    }

                    const double score = sum / std::sqrt (energy);

                    if (score > best)
                    {
                        best = score;
                        bestOffset = offset;
                    }
                }

                analysisPos = juce::jlimit (0, juce::jmax (0, inLen - 1),
                                            analysisPos + bestOffset);
            }

            const int count = juce::jmin (frame, juce::jmax (0, inLen - analysisPos));

            for (int c = 0; c < ch; ++c)
            {
                const auto* s = in.getReadPointer (juce::jmin (c, in.getNumChannels() - 1));
                auto* d = out.getWritePointer (c);

                for (int i = 0; i < count; ++i)
                {
                    const int o = outPos + i;

                    if (o >= outLen)
                        break;

                    d[o] += s[analysisPos + i] * hann (i, frame);
                }
            }

            previousEnd = juce::jlimit (0, juce::jmax (0, inLen - 1), analysisPos + synthHop);
        }
    }

    void pitchShiftSemitones (const Buffer& in, Buffer& out, double semitones, double rate)
    {
        if (std::abs (semitones) < 1.0e-6)
        {
            out.makeCopyOf (in);
            return;
        }

        const double ratio = std::pow (2.0, juce::jlimit (-36.0, 36.0, semitones) / 12.0);

        Buffer stretched;
        timeStretch (in, stretched, ratio, rate);
        resampleBy (stretched, out, ratio);

        setLengthExactly (out, in.getNumSamples());
    }

    // =======================================================================
    //  Granular reconstruction
    //
    //  The output's timeline is mapped onto the input's, so asking for a
    //  longer output stretches the cloud rather than simply appending silence.
    //  Every per-grain decision - size, position, pitch, direction, pan - is
    //  taken once and used for all channels, so with `spread` at zero a mono
    //  source stays mono to the sample.
    // =======================================================================
    void granulate (const Buffer& in, Buffer& out, const GrainSettings& gs,
                    Rng& rng, double rate)
    {
        const int inLen = in.getNumSamples();
        const int ch    = juce::jmax (1, in.getNumChannels());
        const int outLen = gs.outputLength > 0 ? gs.outputLength : inLen;

        out.setSize (ch, juce::jmax (1, outLen));
        out.clear();

        if (inLen < 8 || outLen <= 0)
        {
            for (int c = 0; c < ch; ++c)
                out.copyFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1),
                              0, juce::jmin (inLen, out.getNumSamples()));

            return;
        }

        const int grainLen = juce::jlimit (32, juce::jmax (32, inLen),
                                           (int) (gs.grainMs * 0.001 * rate));
        const float overlap = juce::jlimit (1.0f, 16.0f, gs.overlap);
        const double hop = juce::jmax (8.0, (double) grainLen / (double) overlap);

        const double timeMap = (double) inLen / (double) outLen;
        const double scatter = gs.scatterMs * 0.001 * rate;

        // Coherent overlap-add of hann windows at this overlap sums to
        // overlap/2; scattered grains sum incoherently and come out quieter.
        // The engine's own level match closes the remaining gap.
        const float grainGain = 2.0f / overlap;

        for (double t = -(double) grainLen; t < (double) outLen; t += hop)
        {
            const int size = juce::jlimit (32, grainLen * 2,
                                           (int) ((float) grainLen
                                                  * (1.0f + gs.sizeJitter * rng.nextBipolar() * 0.5f)));

            double source = t * timeMap + scatter * (double) rng.nextBipolar();
            source = juce::jlimit (0.0, (double) juce::jmax (1, inLen - 1), source);

            double ratio = 1.0;

            if (! gs.pitches.empty() && rng.next01() < gs.pitchProbability)
            {
                const int index = (int) (rng.nextUint() % (juce::uint32) gs.pitches.size());
                ratio = std::pow (2.0, (double) gs.pitches[(size_t) index] / 12.0);
            }

            const bool reversed = rng.next01() < gs.reverseProbability;

            float panL = 1.0f, panR = 1.0f;

            if (gs.spread > 0.0f)
                fx::panGains (gs.spread * rng.nextBipolar(), panL, panR);
            else
                panL = panR = fx::cosineTurns (0.125f);     // the same centre gain

            for (int c = 0; c < ch; ++c)
            {
                const auto* s = in.getReadPointer (juce::jmin (c, in.getNumChannels() - 1));
                auto* d = out.getWritePointer (c);

                const float channelGain = grainGain * (ch >= 2 && c == 1 ? panR : panL)
                                                    * (ch >= 2 ? 1.41421356f : 1.0f);

                for (int i = 0; i < size; ++i)
                {
                    const int o = (int) t + i;

                    if (o < 0)
                        continue;

                    if (o >= outLen)
                        break;

                    const double offset = reversed ? (double) (size - 1 - i) * ratio
                                                   : (double) i * ratio;

                    d[o] += readAt (s, inLen, source + offset) * hann (i, size) * channelGain;
                }
            }
        }
    }

    // =======================================================================
    //  Spectral
    // =======================================================================
    namespace
    {
        /**
            One short-time Fourier transform, hann in and hann out at 75%
            overlap, with the source's own phases kept.

            The buffer is padded by a frame at each end and the interior
            window-power sum is constant, so the transform reconstructs an
            unmodified signal to within float rounding rather than fading at
            the edges.
        */
        class Stft
        {
        public:
            explicit Stft (int order)
                : fft (order), size (1 << order), hop ((1 << order) / 4)
            {
                window.resize ((size_t) size);

                for (int i = 0; i < size; ++i)
                    window[(size_t) i] = hann (i, size);

                // The constant the hann-squared overlap-add sums to.
                double power = 0.0;

                for (int k = 0; k * hop < size; ++k)
                    power += (double) window[(size_t) (k * hop)] * window[(size_t) (k * hop)];

                normalise = (float) (1.0 / juce::jmax (1.0e-6, power));
            }

            int frameSize() const noexcept { return size; }
            int numBins()   const noexcept { return size / 2 + 1; }

            /** `fn (magnitudes, numBins, frameIndex)` may modify magnitudes in
                place.  Phases are kept. */
            template <typename FrameFn>
            void processChannel (float* data, int n, FrameFn&& fn)
            {
                if (n < size)
                    return;

                const int padded = n + 2 * size;

                std::vector<float> input ((size_t) padded, 0.0f);
                std::vector<float> output ((size_t) padded, 0.0f);

                std::copy (data, data + n, input.begin() + size);

                std::vector<float> scratch ((size_t) (2 * size), 0.0f);
                std::vector<float> mags ((size_t) numBins(), 0.0f);
                std::vector<float> phases ((size_t) numBins(), 0.0f);

                int frameIndex = 0;

                for (int start = 0; start + size <= padded; start += hop, ++frameIndex)
                {
                    std::fill (scratch.begin(), scratch.end(), 0.0f);

                    for (int i = 0; i < size; ++i)
                        scratch[(size_t) i] = input[(size_t) (start + i)] * window[(size_t) i];

                    fft.performRealOnlyForwardTransform (scratch.data(), true);

                    for (int b = 0; b < numBins(); ++b)
                    {
                        const float re = scratch[(size_t) (2 * b)];
                        const float im = scratch[(size_t) (2 * b + 1)];

                        mags  [(size_t) b] = std::sqrt (re * re + im * im);
                        phases[(size_t) b] = std::atan2 (im, re);
                    }

                    fn (mags.data(), numBins(), frameIndex);

                    std::fill (scratch.begin(), scratch.end(), 0.0f);

                    for (int b = 0; b < numBins(); ++b)
                    {
                        const float m = mags[(size_t) b];
                        const float p = phases[(size_t) b];

                        scratch[(size_t) (2 * b)]     = m * std::cos (p);
                        scratch[(size_t) (2 * b + 1)] = m * std::sin (p);

                        if (b > 0 && b < size / 2)
                        {
                            scratch[(size_t) (2 * (size - b))]     =  m * std::cos (p);
                            scratch[(size_t) (2 * (size - b) + 1)] = -m * std::sin (p);
                        }
                    }

                    fft.performRealOnlyInverseTransform (scratch.data());

                    for (int i = 0; i < size; ++i)
                        output[(size_t) (start + i)] += scratch[(size_t) i]
                                                        * window[(size_t) i] * normalise;
                }

                for (int i = 0; i < n; ++i)
                    data[i] = fx::guard (output[(size_t) (i + size)]);
            }

        private:
            juce::dsp::FFT fft;
            int size, hop;
            float normalise = 1.0f;
            std::vector<float> window;
        };

        /** 2048 at 48 kHz, scaled so the frame stays about 43 ms at any rate.
            Long frames smear transients; short ones cannot resolve a bass
            partial. */
        int spectralOrder (double rate) noexcept
        {
            int order = 11;

            while (order < 13 && (double) (1 << order) / rate < 0.030)
                ++order;

            while (order > 9 && (double) (1 << order) / rate > 0.070)
                --order;

            return order;
        }
    }

    void spectralBlur (Buffer& b, float amount, double rate)
    {
        amount = juce::jlimit (0.0f, 0.98f, amount);

        if (amount <= 0.001f)
            return;

        Stft stft (spectralOrder (rate));

        if (b.getNumSamples() < stft.frameSize())
            return;

        std::vector<float> held;

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            held.assign ((size_t) stft.numBins(), 0.0f);

            stft.processChannel (b.getWritePointer (c), b.getNumSamples(),
                                 [&held, amount] (float* mags, int bins, int frame)
            {
                if (frame == 0)
                    std::fill (held.begin(), held.end(), 0.0f);

                for (int i = 0; i < bins; ++i)
                {
                    // A decaying hold, then a blend back toward the frame that
                    // is actually there: the partials stop starting and
                    // stopping and start hanging in the air instead.
                    held[(size_t) i] = juce::jmax (mags[i], held[(size_t) i] * amount);
                    mags[i] = fx::lerp (mags[i], held[(size_t) i], amount);
                }
            });
        }
    }

    void spectralGate (Buffer& b, float depth, float keepFraction, double rate)
    {
        depth = juce::jlimit (0.0f, 1.0f, depth);
        keepFraction = juce::jlimit (0.01f, 1.0f, keepFraction);

        if (depth <= 0.001f || keepFraction >= 0.999f)
            return;

        Stft stft (spectralOrder (rate));

        if (b.getNumSamples() < stft.frameSize())
            return;

        std::vector<float> scratch;

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            stft.processChannel (b.getWritePointer (c), b.getNumSamples(),
                                 [&scratch, depth, keepFraction] (float* mags, int bins, int)
            {
                scratch.assign (mags, mags + bins);

                const int keep = juce::jlimit (1, bins, (int) ((float) bins * keepFraction));
                const size_t index = (size_t) (bins - keep);

                std::nth_element (scratch.begin(), scratch.begin() + (long) index, scratch.end());

                const float threshold = scratch[index];
                const float floorGain = 1.0f - depth;

                for (int i = 0; i < bins; ++i)
                    if (mags[i] < threshold)
                        mags[i] *= floorGain;
            });
        }
    }

    // =======================================================================
    //  Slicing
    // =======================================================================
    namespace
    {
        /** Zero-phase smoothing of a control curve.  A gate smoothed with a
            causal one-pole arrives late, which would move the very onsets the
            transient lock is supposed to hold still. */
        void smoothCurve (std::vector<float>& curve, float hz, double rate)
        {
            if (curve.size() < 4)
                return;

            zeroPhasePass (curve.data(), (int) curve.size(),
                           compensatedCorner (hz, 1), rate);
        }

        void applyCurve (Buffer& b, const std::vector<float>& curve) noexcept
        {
            const int n = juce::jmin (b.getNumSamples(), (int) curve.size());

            for (int c = 0; c < b.getNumChannels(); ++c)
            {
                auto* d = b.getWritePointer (c);

                for (int i = 0; i < n; ++i)
                    d[i] *= curve[(size_t) i];
            }
        }
    }

    std::vector<int> gridFrom (const std::vector<int>& transients, int length,
                               double rate, double tempo, double timeScale,
                               float minSliceMs)
    {
        std::vector<int> grid;

        if (length <= 0)
            return grid;

        const int minGap = juce::jmax (16, (int) (minSliceMs * 0.001 * rate));

        grid.push_back (0);

        for (int t : transients)
        {
            const int p = (int) std::llround ((double) t * timeScale);

            if (p > grid.back() + minGap && p < length - minGap)
                grid.push_back (p);
        }

        if (grid.size() < 2)
        {
            // No usable onsets.  A tempo gives a musical division; with neither,
            // a quarter of a second is an honest default and is documented as
            // one rather than dressed up as analysis.
            const double stepSeconds = (tempo >= 20.0 && tempo <= 300.0)
                                           ? 60.0 / tempo * 0.5
                                           : 0.25;

            const int step = juce::jmax (minGap, (int) (stepSeconds * rate));

            for (int p = step; p < length - minGap; p += step)
                grid.push_back (p);
        }

        grid.push_back (length);

        return grid;
    }

    void sliceShuffle (Buffer& b, const std::vector<int>& grid, const SliceSettings& ss,
                       Rng& rng, double rate)
    {
        const int n = b.getNumSamples();
        const int ch = b.getNumChannels();

        if (n <= 0 || ch <= 0 || grid.size() < 3)
            return;

        const int slots = (int) grid.size() - 1;
        const int fade = juce::jmax (1, (int) (ss.crossfadeMs * 0.001 * rate));

        // Every decision is taken once, before any channel is touched.
        std::vector<int>  source ((size_t) slots, 0);
        std::vector<bool> reversed ((size_t) slots, false);
        std::vector<bool> dropped ((size_t) slots, false);

        int previous = 0;

        for (int i = 0; i < slots; ++i)
        {
            int j = i;

            if (i > 0 && rng.next01() < ss.repeat)          j = previous;
            else if (rng.next01() < ss.shuffle)             j = (int) (rng.nextUint() % (juce::uint32) slots);

            source[(size_t) i]   = j;
            reversed[(size_t) i] = rng.next01() < ss.reverse;
            dropped[(size_t) i]  = rng.next01() < ss.drop;

            previous = j;
        }

        Buffer scratch;
        scratch.makeCopyOf (b);

        for (int i = 0; i < slots; ++i)
        {
            const int start = grid[(size_t) i];
            const int slot  = grid[(size_t) (i + 1)] - start;

            if (slot <= 0)
                continue;

            const int j = source[(size_t) i];
            const int from = grid[(size_t) j];
            const int piece = juce::jmax (1, grid[(size_t) (j + 1)] - from);

            const bool changed = (j != i) || reversed[(size_t) i] || dropped[(size_t) i];

            for (int c = 0; c < ch; ++c)
            {
                const auto* s = b.getReadPointer (c);
                auto* d = scratch.getWritePointer (c);

                for (int k = 0; k < slot; ++k)
                {
                    if (dropped[(size_t) i])
                    {
                        d[start + k] = 0.0f;
                        continue;
                    }

                    const int inPiece = k % piece;
                    const int index = from + (reversed[(size_t) i] ? piece - 1 - inPiece : inPiece);

                    float x = s[juce::jlimit (0, n - 1, index)];

                    if (changed)
                    {
                        // Only a slot whose content moved is faded: leaving the
                        // untouched slots alone keeps a light shuffle from
                        // putting a dip at every boundary in the file.
                        const int f = juce::jmin (fade, slot / 2);

                        if (f > 0)
                        {
                            if (k < f)              x *= (float) k / (float) f;
                            else if (k >= slot - f) x *= (float) (slot - 1 - k) / (float) f;
                        }
                    }

                    d[start + k] = x;
                }
            }
        }

        b.makeCopyOf (scratch);
    }

    void reverseWhole (Buffer& b)
    {
        const int n = b.getNumSamples();

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getWritePointer (c);

            for (int i = 0, j = n - 1; i < j; ++i, --j)
                std::swap (d[i], d[j]);
        }
    }

    void reverseSlices (Buffer& b, const std::vector<int>& grid, float probability,
                        Rng& rng, double rate)
    {
        if (grid.size() < 3 || probability <= 0.0f)
            return;

        const int slots = (int) grid.size() - 1;
        const int fade = juce::jmax (1, (int) (0.003 * rate));

        std::vector<bool> flip ((size_t) slots, false);

        for (int i = 0; i < slots; ++i)
            flip[(size_t) i] = rng.next01() < probability;

        for (int i = 0; i < slots; ++i)
        {
            if (! flip[(size_t) i])
                continue;

            const int start = grid[(size_t) i];
            const int len = grid[(size_t) (i + 1)] - start;

            if (len < 8)
                continue;

            const int f = juce::jmin (fade, len / 2);

            for (int c = 0; c < b.getNumChannels(); ++c)
            {
                auto* d = b.getWritePointer (c) + start;

                for (int x = 0, y = len - 1; x < y; ++x, --y)
                    std::swap (d[x], d[y]);

                for (int k = 0; k < f; ++k)
                {
                    const float w = (float) k / (float) f;

                    d[k]           *= w;
                    d[len - 1 - k] *= w;
                }
            }
        }
    }

    // =======================================================================
    //  Space
    // =======================================================================
    void appendSilence (Buffer& b, double seconds, double rate)
    {
        const int extra = juce::jmax (0, (int) (seconds * rate));

        if (extra <= 0)
            return;

        const int n = b.getNumSamples();

        b.setSize (b.getNumChannels(), n + extra, true, true, true);
        b.clear (n, extra);
    }

    void prependSilence (Buffer& b, double seconds, double rate)
    {
        const int extra = juce::jmax (0, (int) (seconds * rate));

        if (extra <= 0)
            return;

        const int n = b.getNumSamples();

        Buffer next (juce::jmax (1, b.getNumChannels()), n + extra);
        next.clear();

        for (int c = 0; c < b.getNumChannels(); ++c)
            next.copyFrom (c, extra, b, c, 0, n);

        b = std::move (next);
    }

    void diffuse (Buffer& b, const DiffuseSettings& ds, Rng& rng, double rate)
    {
        const int n = b.getNumSamples();
        const int ch = b.getNumChannels();

        if (n <= 0 || ch <= 0)
            return;

        const float sizeScale = juce::jlimit (0.2f, 4.0f, ds.sizeMs / 60.0f);
        const float decay = juce::jlimit (0.05f, 30.0f, ds.decaySeconds);
        const float damping = juce::jlimit (0.0f, 1.0f, ds.damping);

        static constexpr float allpassMs[4] = { 8.13f, 13.71f, 21.19f, 33.37f };
        static constexpr float combMs[3]    = { 41.73f, 53.91f, 67.31f };

        // Drawn once, outside the channel loop, so the RNG stream does not
        // depend on how many channels the source has.
        float jitter[3];

        for (auto& j : jitter)
            j = 1.0f + 0.05f * rng.nextBipolar();

        float dryGain, wetGain;
        fx::dryWetGains (juce::jlimit (0.0f, 1.0f, ds.amount), dryGain, wetGain);

        const int predelaySamples = juce::jmax (0, (int) (ds.predelayMs * 0.001 * rate));

        for (int c = 0; c < ch; ++c)
        {
            const float detune = ds.stereoFree ? (1.0f + 0.031f * (float) c) : 1.0f;
            const float sign = (ds.stereoFree && (c & 1)) ? -1.0f : 1.0f;

            fx::DelayLine predelay;
            predelay.prepare (predelaySamples + 8);

            fx::Allpass allpass[4];
            fx::DelayLine comb[3];
            fx::OnePoleTPT damp[3];
            float combState[3] = { 0.0f, 0.0f, 0.0f };
            float feedback[3];
            int combLengths[3];

            for (int i = 0; i < 4; ++i)
            {
                const float samples = allpassMs[i] * sizeScale * detune * 0.001f * (float) rate;

                allpass[i].prepare ((int) samples + 8);
                allpass[i].setDelay (samples);
                allpass[i].setCoefficient (sign * 0.62f);
            }

            for (int i = 0; i < 3; ++i)
            {
                const float seconds = combMs[i] * sizeScale * detune * jitter[i] * 0.001f;
                const int samples = juce::jmax (8, (int) (seconds * rate));

                comb[i].prepare (samples + 8);
                combLengths[i] = samples;

                damp[i].setCutoff (fx::lerp (15000.0f, 1400.0f, damping), rate);

                feedback[i] = std::pow (10.0f, -3.0f * seconds / decay);
                feedback[i] = juce::jlimit (0.0f, 0.93f, feedback[i]);
            }

            auto* d = b.getWritePointer (c);

            for (int i = 0; i < n; ++i)
            {
                const float dry = d[i];

                float x = dry;

                if (predelaySamples > 0)
                {
                    predelay.write (x);
                    x = predelay.readInt (predelaySamples);
                }

                for (auto& a : allpass)
                    x = a.process (x);

                float wet = 0.0f;

                for (int k = 0; k < 3; ++k)
                {
                    const float delayed = comb[k].readInt (combLengths[k]);

                    combState[k] = damp[k].lowpass (delayed);
                    comb[k].write (fx::guard (x + combState[k] * feedback[k]));

                    wet += delayed;
                }

                wet *= 0.4f;

                d[i] = fx::guard (dry * dryGain + wet * wetGain);
            }
        }
    }

    // =======================================================================
    //  Degradation
    // =======================================================================
    void degrade (Buffer& b, const DegradeSettings& gs, Rng& rng, double rate)
    {
        const int n = b.getNumSamples();
        const int ch = b.getNumChannels();

        if (n <= 0 || ch <= 0)
            return;

        // The noise bed is generated once and shared, so it is common-mode
        // unless the caller has said the image may move - and so that the RNG
        // stream does not depend on the channel count.
        std::vector<float> noise;

        const float noiseLevel = juce::jlimit (0.0f, 1.0f, gs.noise) * 0.02f;

        if (noiseLevel > 0.0f)
        {
            noise.resize ((size_t) n);

            for (int i = 0; i < n; ++i)
                noise[(size_t) i] = rng.nextBipolar() * noiseLevel;
        }

        std::vector<float> noiseB;

        if (noiseLevel > 0.0f && gs.stereoFree && ch > 1)
        {
            noiseB.resize ((size_t) n);

            for (int i = 0; i < n; ++i)
                noiseB[(size_t) i] = rng.nextBipolar() * noiseLevel;
        }

        // Slow pitch instability.  Two incommensurate oscillators with phases
        // taken from the recipe's own stream, read through one delay line, and
        // identical on every channel: a copy drifts, it does not wander apart.
        const float driftPhase1 = rng.next01();
        const float driftPhase2 = rng.next01();

        const float driftCents = juce::jlimit (0.0f, 50.0f, gs.driftCents);
        const double driftRate1 = 0.31, driftRate2 = 0.73;

        // Deviation in cents is proportional to the read position's rate of
        // change, so the depth a given cents figure needs depends on the rate.
        const auto depthFor = [rate, driftCents] (double hz)
        {
            const double d = (double) driftCents * rate
                                 / (1731.0 * 2.0 * 3.14159265358979 * juce::jmax (0.01, hz) * 2.0);

            return (float) juce::jlimit (0.0, 0.016 * rate, d);
        };

        const float driftDepth1 = depthFor (driftRate1);
        const float driftDepth2 = depthFor (driftRate2);

        const float bandwidthHz = fx::lerp (20000.0f, 2200.0f,
                                            juce::jlimit (0.0f, 1.0f, gs.bandwidth));

        const int hold = 1 + (int) (juce::jlimit (0.0f, 1.0f, gs.decimation) * 11.0f);
        const float reconstruction = juce::jlimit (0.0f, 1.0f, gs.decimation);

        const float levels = std::pow (2.0f, fx::lerp (20.0f, 4.0f,
                                                       juce::jlimit (0.0f, 1.0f, gs.bits)));

        const float drive = 1.0f + juce::jlimit (0.0f, 1.0f, gs.saturation) * 4.0f;
        const float makeup = 1.0f / std::tanh (drive * 0.7f) * 0.7f;

        // The drift is read straight out of a copy of the channel rather than
        // through a delay line.  A delay line would impose its own base delay -
        // 20 ms of it - on everything the operation touches, which is a timing
        // shift dressed up as pitch instability.  Offline there is no reason to
        // accept that: the read can move either side of the current sample.
        std::vector<float> copy;

        for (int c = 0; c < ch; ++c)
        {
            fx::OnePoleTPT lp1, lp2;
            lp1.setCutoff (bandwidthHz, rate);
            lp2.setCutoff (bandwidthHz, rate);

            fx::Tilt tilt;
            tilt.prepare (rate);

            fx::DcBlocker dc;
            dc.prepare (rate);

            float heldSample = 0.0f, previousHeld = 0.0f;
            int holdCounter = 0;

            auto* d = b.getWritePointer (c);

            if (driftCents > 0.0f)
                copy.assign (d, d + n);

            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / rate;

                float x = d[i];

                if (driftCents > 0.0f)
                {
                    const float wobble = driftDepth1 * fx::sineTurns ((float) (t * driftRate1) + driftPhase1)
                                       + driftDepth2 * fx::sineTurns ((float) (t * driftRate2) + driftPhase2);

                    x = readAt (copy.data(), n, (double) i + (double) wobble);
                }

                x = lp2.lowpass (lp1.lowpass (x));

                if (hold > 1)
                {
                    if (holdCounter == 0)
                    {
                        previousHeld = heldSample;
                        heldSample = x;
                    }

                    // Zero-order hold blended with linear reconstruction: the
                    // artefact is reconstruction error, not quantisation.
                    const float frac = (float) holdCounter / (float) hold;
                    const float linear = fx::lerp (previousHeld, heldSample, frac);

                    x = fx::lerp (linear, heldSample, reconstruction);

                    holdCounter = (holdCounter + 1) % hold;
                }

                if (gs.bits > 0.001f)
                    x = std::round (x * levels) / levels;

                if (noiseLevel > 0.0f)
                    x += (gs.stereoFree && c > 0 && ! noiseB.empty())
                             ? noiseB[(size_t) i] : noise[(size_t) i];

                if (gs.saturation > 0.001f)
                    x = fx::tanhFast (x * drive) * makeup;

                x = dc.process (x);
                x = tilt.process (x, -juce::jlimit (0.0f, 1.0f, gs.tiltDark));

                d[i] = fx::guard (x);
            }
        }
    }

    // =======================================================================
    //  Dynamics and rhythm
    // =======================================================================
    void transientShape (Buffer& b, float attack, float sustain, double rate)
    {
        const int n = b.getNumSamples();
        const int ch = b.getNumChannels();

        if (n <= 0 || ch <= 0)
            return;

        attack = juce::jlimit (-1.0f, 1.0f, attack);
        sustain = juce::jlimit (-1.0f, 1.0f, sustain);

        if (std::abs (attack) < 0.001f && std::abs (sustain) < 0.001f)
            return;

        const auto coeff = [rate] (double ms)
        {
            return (float) std::exp (-1.0 / juce::jmax (1.0, ms * 0.001 * rate));
        };

        const float fastUp = coeff (1.0), fastDown = coeff (30.0);
        const float slowUp = coeff (25.0), slowDown = coeff (220.0);

        float fast = 0.0f, slow = 0.0f;

        std::vector<float> curve ((size_t) n, 1.0f);

        for (int i = 0; i < n; ++i)
        {
            float mono = 0.0f;

            for (int c = 0; c < ch; ++c)
                mono += b.getReadPointer (c)[i];

            const float level = std::abs (mono / (float) ch);

            fast = level > fast ? level + (fast - level) * fastUp
                                : level + (fast - level) * fastDown;
            slow = level > slow ? level + (slow - level) * slowUp
                                : level + (slow - level) * slowDown;

            const float sum = fast + slow + 1.0e-9f;

            const float attackness = juce::jlimit (0.0f, 1.0f, (fast - slow) / sum);
            const float decayness  = juce::jlimit (0.0f, 1.0f, (slow - fast) / sum);

            curve[(size_t) i] = juce::jlimit (0.05f, 8.0f,
                                              std::pow (2.0f, attack * 2.0f * attackness)
                                              * std::pow (2.0f, sustain * 2.0f * decayness));
        }

        applyCurve (b, curve);
    }

    void rhythmicGate (Buffer& b, const std::vector<int>& grid, float depth,
                       float dutyCycle, Rng& rng, double rate)
    {
        const int n = b.getNumSamples();

        if (n <= 0 || grid.size() < 3 || depth <= 0.001f)
            return;

        depth = juce::jlimit (0.0f, 1.0f, depth);

        const int slots = (int) grid.size() - 1;

        std::vector<float> curve ((size_t) n, 1.0f);

        for (int i = 0; i < slots; ++i)
        {
            const bool open = rng.next01() < juce::jlimit (0.05f, 1.0f, dutyCycle);

            if (open)
                continue;

            const int start = juce::jlimit (0, n, grid[(size_t) i]);
            const int end   = juce::jlimit (0, n, grid[(size_t) (i + 1)]);

            for (int k = start; k < end; ++k)
                curve[(size_t) k] = 1.0f - depth;
        }

        smoothCurve (curve, 60.0f, rate);
        applyCurve (b, curve);
    }

    void dropouts (Buffer& b, float density, float depth, Rng& rng, double rate)
    {
        const int n = b.getNumSamples();

        if (n <= 0 || density <= 0.0f || depth <= 0.001f)
            return;

        const double seconds = (double) n / rate;
        const int count = juce::jlimit (0, 4096, (int) (seconds * (double) density * 6.0));

        std::vector<float> curve ((size_t) n, 1.0f);

        for (int k = 0; k < count; ++k)
        {
            const int length = juce::jlimit (8, juce::jmax (8, n / 4),
                                             (int) ((0.005 + 0.055 * (double) rng.next01()) * rate));
            const int start = (int) (rng.next01() * (float) juce::jmax (1, n - length));

            for (int i = start; i < juce::jmin (n, start + length); ++i)
                curve[(size_t) i] = 1.0f - juce::jlimit (0.0f, 1.0f, depth);
        }

        smoothCurve (curve, 120.0f, rate);
        applyCurve (b, curve);
    }

    void stutter (Buffer& b, const std::vector<int>& grid, float amount, Rng& rng, double rate)
    {
        const int n = b.getNumSamples();

        if (n <= 0 || grid.size() < 3 || amount <= 0.001f)
            return;

        const int slots = (int) grid.size() - 1;
        const int fade = juce::jmax (1, (int) (0.002 * rate));

        for (int i = 0; i < slots; ++i)
        {
            if (rng.next01() >= amount)
                continue;

            const int start = grid[(size_t) i];
            const int length = grid[(size_t) (i + 1)] - start;

            if (length < 64)
                continue;

            const int repeats = 2 + (int) (rng.nextUint() % 6u);
            const int piece = juce::jmax (16, length / repeats);

            for (int c = 0; c < b.getNumChannels(); ++c)
            {
                auto* d = b.getWritePointer (c) + start;

                std::vector<float> head (d, d + piece);

                for (int k = piece; k < length; ++k)
                    d[k] = head[(size_t) (k % piece)];

                for (int r = 1; r * piece < length; ++r)
                {
                    const int edge = r * piece;
                    const int f = juce::jmin (fade, piece / 2);

                    for (int k = 0; k < f && edge + k < length; ++k)
                        d[edge + k] *= (float) k / (float) f;
                }
            }
        }
    }

    void filterSweep (Buffer& b, float depth, float lfoHz, float centreHz, double rate)
    {
        const int n = b.getNumSamples();

        if (n <= 0 || depth <= 0.001f)
            return;

        depth = juce::jlimit (0.0f, 1.0f, depth);

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            fx::OnePoleTPT lp1, lp2;

            auto* d = b.getWritePointer (c);

            for (int i = 0; i < n; ++i)
            {
                if ((i & 31) == 0)
                {
                    const float phase = (float) ((double) i / rate * (double) lfoHz);
                    const float mod = fx::sineTurns (phase);
                    const float hz = centreHz * std::pow (2.0f, mod * depth * 2.5f);

                    lp1.setCutoff (hz, rate);
                    lp2.setCutoff (hz, rate);
                }

                d[i] = fx::guard (lp2.lowpass (lp1.lowpass (d[i])));
            }
        }
    }

    void tilt (Buffer& b, float amount, double rate)
    {
        if (std::abs (amount) < 0.001f)
            return;

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            fx::Tilt t;
            t.prepare (rate);

            auto* d = b.getWritePointer (c);

            for (int i = 0; i < b.getNumSamples(); ++i)
                d[i] = fx::guard (t.process (d[i], amount));
        }
    }

    void lowPass (Buffer& b, float hz, double rate, int poles)
    {
        poles = juce::jlimit (1, 8, poles);

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            std::vector<fx::OnePoleTPT> stages ((size_t) poles);

            for (auto& s : stages)
                s.setCutoff (hz, rate);

            auto* d = b.getWritePointer (c);

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                float x = d[i];

                for (auto& s : stages)
                    x = s.lowpass (x);

                d[i] = fx::guard (x);
            }
        }
    }

    void highPass (Buffer& b, float hz, double rate, int poles)
    {
        poles = juce::jlimit (1, 8, poles);

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            std::vector<fx::OnePoleTPT> stages ((size_t) poles);

            for (auto& s : stages)
                s.setCutoff (hz, rate);

            auto* d = b.getWritePointer (c);

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                float x = d[i];

                for (auto& s : stages)
                    x = s.highpass (x);

                d[i] = fx::guard (x);
            }
        }
    }

    void harmonicReinforce (Buffer& b, const std::vector<float>& frequencies,
                            float amount, double rate)
    {
        const int n = b.getNumSamples();

        if (n <= 0 || frequencies.empty() || amount <= 0.001f)
            return;

        amount = juce::jlimit (0.0f, 1.0f, amount);

        const float perVoice = amount / (float) frequencies.size();

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            std::vector<synth::StateVariableFilter> filters (frequencies.size());

            for (size_t f = 0; f < frequencies.size(); ++f)
            {
                filters[f].prepare (rate);
                filters[f].setCutoff (juce::jlimit (20.0f, (float) (rate * 0.4), frequencies[f]));
                filters[f].setResonance (0.82f);
                filters[f].setDrive (0.0f);
            }

            auto* d = b.getWritePointer (c);

            for (int i = 0; i < n; ++i)
            {
                float sum = 0.0f;

                for (auto& f : filters)
                    sum += f.process (d[i], synth::FilterType::bandpass);

                d[i] = fx::guard (d[i] + sum * perVoice);
            }
        }
    }

    void widen (Buffer& b, float amount, double rate)
    {
        const int n = b.getNumSamples();

        if (n <= 0 || b.getNumChannels() < 2 || amount <= 0.001f)
            return;

        amount = juce::jlimit (0.0f, 1.0f, amount);

        fx::Allpass left[2], right[2];

        static constexpr float delays[2] = { 0.0011f, 0.0023f };

        for (int i = 0; i < 2; ++i)
        {
            const int samples = juce::jmax (2, (int) (delays[i] * rate));

            left[i].prepare (samples + 8);
            left[i].setDelay ((float) samples);
            left[i].setCoefficient (0.62f);

            right[i].prepare (samples + 8);
            right[i].setDelay ((float) samples);
            right[i].setCoefficient (-0.62f);
        }

        // The side channel is high passed above the band the low-end rule
        // protects, for the same reason Memory does it: a wide bottom octave
        // is the one stereo decision that is always wrong.
        fx::OnePoleTPT sideHigh1, sideHigh2;
        sideHigh1.setCutoff (165.0f, rate);
        sideHigh2.setCutoff (165.0f, rate);

        auto* l = b.getWritePointer (0);
        auto* r = b.getWritePointer (1);

        for (int i = 0; i < n; ++i)
        {
            const float mid = 0.5f * (l[i] + r[i]);
            float side = 0.5f * (l[i] - r[i]);

            float decorrelated = 0.5f * (left[1].process (left[0].process (mid))
                                         - right[1].process (right[0].process (mid)));

            side = side * (1.0f + amount) + decorrelated * amount;
            side = sideHigh2.highpass (sideHigh1.highpass (side));

            l[i] = fx::guard (mid + side);
            r[i] = fx::guard (mid - side);
        }
    }

    void applySwell (Buffer& b, float curve)
    {
        const int n = b.getNumSamples();

        if (n < 2)
            return;

        curve = juce::jlimit (0.25f, 8.0f, curve);

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getWritePointer (c);

            for (int i = 0; i < n; ++i)
                d[i] *= std::pow ((float) i / (float) (n - 1), curve);
        }
    }

    void applyDecay (Buffer& b, float seconds, double rate)
    {
        const int n = b.getNumSamples();

        if (n < 2 || seconds <= 0.0f)
            return;

        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getWritePointer (c);

            for (int i = 0; i < n; ++i)
                d[i] *= std::pow (10.0f, -3.0f * (float) ((double) i / rate) / seconds);
        }
    }

    int mostStableWindow (const Buffer& b, int windowSamples, double rate)
    {
        const int n = b.getNumSamples();

        if (n <= windowSamples || windowSamples <= 0)
            return 0;

        const int hop = juce::jmax (64, (int) (0.020 * rate));
        const int frames = (n - 1) / hop + 1;

        std::vector<float> energy ((size_t) frames, 0.0f);
        std::vector<float> bright ((size_t) frames, 0.0f);

        fx::OnePoleTPT split;
        split.setCutoff (1200.0f, rate);

        for (int f = 0; f < frames; ++f)
        {
            const int start = f * hop;
            const int count = juce::jmin (hop, n - start);

            double sum = 0.0, high = 0.0;

            for (int c = 0; c < b.getNumChannels(); ++c)
            {
                const auto* d = b.getReadPointer (c) + start;

                for (int i = 0; i < count; ++i)
                {
                    const float x = d[i];
                    const float h = x - split.lowpass (x);

                    sum += (double) x * x;
                    high += (double) h * h;
                }
            }

            energy[(size_t) f] = (float) std::sqrt (sum / juce::jmax (1, count));
            bright[(size_t) f] = (float) std::sqrt (high / juce::jmax (1, count));
        }

        const int windowFrames = juce::jmax (1, windowSamples / hop);

        float best = -1.0e30f;
        int bestStart = 0;

        for (int f = 0; f + windowFrames <= frames; ++f)
        {
            double mean = 0.0, flux = 0.0;

            for (int k = 0; k < windowFrames; ++k)
                mean += energy[(size_t) (f + k)];

            mean /= juce::jmax (1, windowFrames);

            for (int k = 1; k < windowFrames; ++k)
                flux += std::abs (energy[(size_t) (f + k)] - energy[(size_t) (f + k - 1)])
                      + std::abs (bright[(size_t) (f + k)] - bright[(size_t) (f + k - 1)]);

            flux /= juce::jmax (1, windowFrames - 1);

            // Loud and steady beats loud and moving: what we want is a region
            // that can be looped without the loop announcing itself.
            const float score = (float) (mean - flux * 2.0);

            if (score > best)
            {
                best = score;
                bestStart = f * hop;
            }
        }

        return juce::jlimit (0, juce::jmax (0, n - windowSamples), bestStart);
    }

    void loopRegion (const Buffer& in, Buffer& out, int start, int windowSamples,
                     float crossfadeMs, double rate)
    {
        const int n = out.getNumSamples();
        const int inLen = in.getNumSamples();

        if (n <= 0 || inLen <= 0 || windowSamples <= 0)
            return;

        out.clear();

        windowSamples = juce::jmin (windowSamples, inLen - juce::jlimit (0, inLen - 1, start));
        windowSamples = juce::jmax (8, windowSamples);

        const int fade = juce::jlimit (1, windowSamples / 2,
                                       (int) (crossfadeMs * 0.001 * rate));

        const int stride = windowSamples - fade;

        for (int c = 0; c < out.getNumChannels(); ++c)
        {
            const auto* s = in.getReadPointer (juce::jmin (c, in.getNumChannels() - 1));
            auto* d = out.getWritePointer (c);

            for (int offset = 0; offset < n + stride; offset += stride)
            {
                for (int i = 0; i < windowSamples; ++i)
                {
                    const int o = offset + i;

                    if (o < 0 || o >= n)
                        continue;

                    float w = 1.0f;

                    if (i < fade)                        w = (float) i / (float) fade;
                    else if (i >= windowSamples - fade)  w = (float) (windowSamples - 1 - i) / (float) fade;

                    // Equal power, so a crossfade loop does not dip at the seam.
                    d[o] += s[juce::jlimit (0, inLen - 1, start + i)] * std::sqrt (juce::jlimit (0.0f, 1.0f, w));
                }
            }
        }
    }
}
