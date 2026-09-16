#pragma once

#include "../DspCommon.h"
#include "../EngineContext.h"

namespace nacar
{
    /**
        GRAIN  -  specification section 89, and slot five of the signature path
        in section 91.

            SOURCE -> HISTORY BUFFER -> REWIND -> GRAIN -> SPACE

        A real granular engine: a pool of overlapping grains, each with its own
        start position, length, pitch, pan, direction and window, scheduled at
        DENSITY grains per second with JITTER on the timing.  The pool is sized
        at prepare() from the worst case - maximum density times maximum size -
        and never grows.  An exhausted pool drops the new grain rather than
        stealing a sounding one: a dropped grain is inaudible and a stolen one
        clicks.

        The pitch modes are what make this NACAR rather than a generic
        granulator.  A grain's transposition is drawn from a *weighted*
        distribution over the interval set that grain_pitch_mode, scale_type
        and harmony_mode allow, never from a uniform one (section 119).

        Grain keeps its own history.  The chain is reorderable, so Grain must
        granulate whatever actually reaches Grain.

        The grain scheduling, the pitch weighting, the freeze strategy, the
        feedback limiter, the gain staging and the macro response are all
        documented at the top of GrainFX.cpp.
    */
    class GrainFX
    {
    public:
        GrainFX();
        ~GrainFX();

        void prepare (const EngineSpec&);
        void reset();
        void process (juce::AudioBuffer<float>&, const ParameterRegistry&, const MacroState&);

    private:
        // -- windows, in the order of the grain_window choice list ------------
        enum { kHann = 0, kTukey, kGauss, kExpo, kPercussive, kNumWindows };

        static constexpr int kWindowTableSize = 1024;   ///< plus one guard point
        static constexpr int kMaxGrains       = 96;

        /** One sounding grain.  Everything it needs is decided at spawn;
            nothing about it changes afterwards, which is what makes a grain
            safe to start and finish without reference to any parameter. */
        struct Grain
        {
            /*  `cursor` means two different things, and which one is decided
                once at spawn and never again:

                  live grain    samples behind the history's write head, so
                                cursorDelta is (1 - readRate) - the write head
                                moves too, exactly as in RewindEngine.
                  freeze grain  an index into the freeze store, which nothing
                                is writing to, so cursorDelta is the read rate
                                itself.

                Keeping a live grain's cursor as a *delay* rather than as an
                absolute ring index matters for precision: a delay near the
                write head is a small number, and a float's fractional
                resolution at 10^5 is already only 1/64 of a sample.          */
            float cursor      = 0.0f;
            float cursorDelta = 0.0f;
            float phase       = 0.0f;   ///< 0..1 through the window
            float phaseInc    = 0.0f;   ///< 1 / length in samples
            float gainL       = 0.0f;
            float gainR       = 0.0f;
            int   window      = kHann;
            bool  fromFreeze  = false;
            bool  active      = false;
        };

        /** The weighted transposition table, rebuilt once per block. */
        struct PitchTable
        {
            static constexpr int kSpan = 12;                  ///< +-12 semitones
            static constexpr int kSize = 2 * kSpan + 1;

            float cumulative[kSize] {};   ///< running sum; last entry is the total
            float unisonShare = 1.0f;     ///< P(0 semitones), used by the gain law
            bool  unquantised = false;    ///< FREE mode bypasses the table
        };

        void buildWindows();
        void buildPitchTable (int pitchMode, int scaleType, int harmonyMode,
                              float scatter, float alter) noexcept;

        float drawSemitones (float freeSemis, float scatter) noexcept;
        void  spawnGrain (float sizeSamples, float positionOffset, float spread,
                          float direction, float widthScale, int windowIndex,
                          float freeSemis, float scatter, float alter) noexcept;

        void readFreeze (float pos, float& l, float& r) const noexcept;

        // -- fixed configuration ----------------------------------------------
        double sampleRate = 48000.0;
        int    maxBlock = 512;
        float  historySamples = 1.0f;     ///< usable span of the history ring
        int    freezeLength = 1;          ///< samples in the freeze store
        float  panTrim = 1.0f;            ///< makes a centred grain exactly unity

        // -- storage (all allocated in prepare, never afterwards) -------------
        fx::HistoryBuffer history;
        std::vector<float> freezeL, freezeR;
        std::array<std::array<float, (size_t) kWindowTableSize + 1>, (size_t) kNumWindows> windows {};
        std::array<float, (size_t) kNumWindows> windowMean {};
        std::array<float, (size_t) kNumWindows> windowRms {};
        std::array<Grain, (size_t) kMaxGrains> grains {};

        PitchTable pitchTable;

        // -- scheduling --------------------------------------------------------
        float untilNextGrain = 0.0f;
        int   activeGrains = 0;

        // -- freeze -------------------------------------------------------------
        bool  freezeHeld = false;     ///< the parameter, as of the last block
        bool  freezeReady = false;    ///< the store is filled and in use
        int   freezeFill = 0;         ///< samples copied so far
        int   freezeSourceStart = 0;  ///< ring index the copy started from

        // -- feedback ------------------------------------------------------------
        float fbL = 0.0f, fbR = 0.0f;     ///< the limited signal fed back
        float fbPeak = 0.0f;
        float fbAttack = 0.1f, fbRelease = 0.001f;
        fx::OnePoleTPT fbLpL, fbLpR;
        fx::DcBlocker  fbDcL, fbDcR;

        // -- the low band stays mono ----------------------------------------------
        fx::ThreeBand bandL, bandR;

        // -- block state -----------------------------------------------------------
        float prevMix = 0.0f;
        fx::Rng rng;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrainFX)
    };
}
