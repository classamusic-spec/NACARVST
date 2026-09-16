#pragma once

#include <memory>

#include "../EngineContext.h"

namespace nacar
{
    /**
        WEIGHT - the last stage in the instrument.  Specification section 74.

        It does one thing: it makes the sound feel physically larger in a chosen
        part of the spectrum.  `weight_mode` chooses where, `macro_weight`
        chooses how much, and `weight_harmonics` and `weight_compress` shape it.

            SUB   the bottom, by harmonic reinforcement rather than level -
                  adding energy below 60 Hz does nothing on most playback
                  systems and eats all the headroom, so SUB generates the
                  harmonics that let the ear infer the note instead.
            BODY  the low mids, 150 - 700 Hz, split out and shaped on their own
                  so that size and dirt do not arrive together.
            AIR   the top, as detail rather than level: a gentle shelf plus
                  harmonics generated above it, because boosting 12 kHz on a
                  signal with nothing at 12 kHz only raises noise.

        The three modes crossfade over 12 ms, because `weight_mode` is a
        discrete choice a user will automate.

        Gain staging is the thing this stage has to get right: a weight stage
        that makes everything louder is indistinguishable from a gain knob.
        The compensation is documented in full at the top of WeightEngine.cpp.

        Realtime: prepare() allocates, nothing else does.  With `macro_weight`
        at zero and the fade already run out, process() returns the input sample
        for sample.
    */
    class WeightEngine
    {
    public:
        WeightEngine();
        ~WeightEngine();

        void prepare (const EngineSpec&);
        void reset();
        void process (juce::AudioBuffer<float>&, const ParameterRegistry&, const MacroState&);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WeightEngine)
    };
}
