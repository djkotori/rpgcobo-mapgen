#include "VillageMapImageGenerator.h"

#include "TownMapImageGenerator.h"

#include <utility>

VillageMapImageGenerator::VillageMapImageGenerator(MapGenParams params)
    : MapImageGenerator(std::move(params)) {}

bool VillageMapImageGenerator::Generate(MapPlan& outPlan, std::string& outError) {
    MapGenParams villageParams = params_;
    // The shared settlement planner still owns the footprint, road graph,
    // typed buildings, furniture, and semantic export. The standalone type
    // selects the rural branch and receives village-specific validation in
    // MapQuality after this method returns.
    villageParams.extra["townMode"] = 1.0;
    villageParams.extra["settlementType"] = 1.0;
    TownMapImageGenerator planner(std::move(villageParams));
    if (!planner.Generate(outPlan, outError)) return false;
    outPlan.type = "village";
    outPlan.archetype = "village";
    outPlan.cityType = "village";
    return true;
}

MapGenColor VillageMapImageGenerator::BiomeToColor(std::uint8_t biome) const {
    TownMapImageGenerator palette(params_);
    return palette.BiomeToColor(biome);
}
