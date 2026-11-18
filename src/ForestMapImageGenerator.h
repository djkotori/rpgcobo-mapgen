#pragma once

#include "MapImageGenerator.h"
#include <random>

namespace ForestMapFilters {
void Naturalize(MapPlan& plan, std::mt19937& random);
}

class ForestMapImageGenerator : public MapImageGenerator {
public:
    explicit ForestMapImageGenerator(MapGenParams params);

    bool Generate(MapPlan& outPlan, std::string& outError) override;
    MapGenColor BiomeToColor(std::uint8_t biome) const override;
};
