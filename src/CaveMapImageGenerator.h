#pragma once

#include "MapImageGenerator.h"

class CaveMapImageGenerator : public MapImageGenerator {
public:
    explicit CaveMapImageGenerator(MapGenParams params);

    bool Generate(MapPlan& outPlan, std::string& outError) override;
    MapGenColor BiomeToColor(std::uint8_t biome) const override;
};
