#pragma once

#include "MapImageGenerator.h"

class WorldMapImageGenerator : public MapImageGenerator {
public:
    explicit WorldMapImageGenerator(MapGenParams params);

    bool Generate(MapPlan& outPlan, std::string& outError) override;
    MapGenColor BiomeToColor(std::uint8_t biome) const override;
};
