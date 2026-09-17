#include "FactoryPresets.h"
#include "Factory/Builders.h"

// ===========================================================================
//  HOW THESE WERE VOICED
//
//  Every preset starts from the parameter list's own defaults - PresetManager
//  resets to them before it applies anything - and then says only what makes
//  it itself.  A value that is absent is the default on purpose, not by
//  omission, which is why two presets in the same category can be read side by
//  side and the difference between them is the whole difference.
//
//  Three things were kept deliberately varied across the set, because they are
//  what stops a library being one patch at N cutoffs:
//
//    - the oscillator pair and the wavetable family under it;
//    - the filter model, and whether the creative second filter runs at all;
//    - the FX chain ORDER, which is DSP order.  Ageing a reverb is not the
//      same instrument as reverberating an aged sound, and both are here.
//
//  The presets themselves live one category per file under Factory/, because
//  a single translation unit of this size is a file nobody can work in and
//  two people cannot work in at once.  The shared vocabulary they are written
//  in - the named choice indices, the modulation source strings, the Build
//  helper - is in Factory/PresetBuilder.h.
//
//  Nobody has listened to any of this.  What is claimed in those files is
//  what the parameters are set to and which engine reads them; how it sounds
//  is not a measurable property and is not asserted anywhere.
// ===========================================================================

namespace nacar::presets
{
    juce::String factoryAuthor()
    {
        return juce::String::fromUTF8 ("N\xc3\x81" "CAR");
    }

    // =======================================================================
    //  The library
    // =======================================================================
    const std::vector<FactoryPreset>& factoryLibrary()
    {
        static const std::vector<FactoryPreset> library = []
        {
            using namespace detail;

            std::vector<FactoryPreset> v;
            v.reserve (384);

            buildKeys (v);
            buildPads (v);
            buildPlucks (v);
            buildBells (v);
            buildLeads (v);
            buildBass (v);
            buildSub (v);
            buildVocal (v);
            buildTexture (v);
            buildAtmosphere (v);
            buildDrums (v);
            buildPercussion (v);
            buildSequences (v);

            return v;
        }();

        return library;
    }
}
