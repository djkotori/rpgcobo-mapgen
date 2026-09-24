#pragma once

#include "MapImageGenerator.h"

class TownMapImageGenerator : public MapImageGenerator {
public:
    explicit TownMapImageGenerator(MapGenParams params);

    bool Generate(MapPlan& outPlan, std::string& outError) override;
    MapGenColor BiomeToColor(std::uint8_t biome) const override;
};
