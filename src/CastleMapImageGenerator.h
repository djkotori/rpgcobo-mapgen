#pragma once

#include "MapImageGenerator.h"

class CastleMapImageGenerator : public MapImageGenerator {
public:
    explicit CastleMapImageGenerator(MapGenParams params);

    bool Generate(MapPlan& outPlan, std::string& outError) override;
    MapGenColor BiomeToColor(std::uint8_t biome) const override;
};
