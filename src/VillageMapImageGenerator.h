#pragma once

#include "MapImageGenerator.h"

class VillageMapImageGenerator : public MapImageGenerator {
public:
    explicit VillageMapImageGenerator(MapGenParams params);

    bool Generate(MapPlan& outPlan, std::string& outError) override;
    MapGenColor BiomeToColor(std::uint8_t biome) const override;
};
