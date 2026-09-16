#pragma once

#include <memory>

#include "../EngineContext.h"

namespace nacar
{
    /**
        PATINA - surface texture and age.  Specification section 95.

        The last thing in the chain before Weight, and the one that decides
        whether two otherwise identical patches feel like two different physical
        objects.  Three systems in this instrument are neighbours and are
        deliberately kept apart:

            MEMORY  historical identity - how many times this was copied
            RETRO   medium and playback - which machine it came out of
            PATINA  the surface the sound has ended up with

        Memory and Retro are processes the sound went through.  Patina is
        something that has settled *on* it, so it is thin and broad rather than
        deep and specific, it is texture rather than damage, and it is never
        completely still.

        Four controls - TONE, NOISE, WEAR, DRIFT - shape one of six surfaces:
        SOFT, VINTAGE, CHROME, HAZE, SMOKE, CUSTOM.  The algorithm, what each
        surface is, the macro response and the known limitations are all
        documented at the top of PatinaEngine.cpp.

        Realtime: prepare() allocates, nothing else does.  With `patina_on`
        false and the engage fade already at zero, process() returns the input
        sample for sample.
    */
    class PatinaEngine
    {
    public:
        PatinaEngine();
        ~PatinaEngine();

        void prepare (const EngineSpec&);
        void reset();
        void process (juce::AudioBuffer<float>&, const ParameterRegistry&, const MacroState&);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatinaEngine)
    };
}
