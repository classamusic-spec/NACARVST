#pragma once

#include "../FactoryPresets.h"

namespace nacar::presets::detail
{
    // One builder per category word in the browser's vocabulary, one
    // translation unit each.  They are called in this order by
    // factoryLibrary(), and that order is the order the browser shows.
    void buildKeys (std::vector<FactoryPreset>&);
    void buildPads (std::vector<FactoryPreset>&);
    void buildPlucks (std::vector<FactoryPreset>&);
    void buildBells (std::vector<FactoryPreset>&);
    void buildLeads (std::vector<FactoryPreset>&);
    void buildBass (std::vector<FactoryPreset>&);
    void buildSub (std::vector<FactoryPreset>&);
    void buildVocal (std::vector<FactoryPreset>&);
    void buildTexture (std::vector<FactoryPreset>&);
    void buildAtmosphere (std::vector<FactoryPreset>&);
    void buildDrums (std::vector<FactoryPreset>&);
    void buildPercussion (std::vector<FactoryPreset>&);
    void buildSequences (std::vector<FactoryPreset>&);
}
