#pragma once

#include "MapImageGenerator.h"

class CastleMapImageGenerator : public MapImageGenerator {
public:
    explicit CastleMapImageGenerator(MapGenParams params);

    bool Generate(MapPlan& outPlan, std::string& outError) override;
    MapGenColor BiomeToColor(std::uint8_t biome) const override;

private:
    // One attempt with the current edge margin (percent of the map kept clear around the castle).
    bool GenerateOnce(MapPlan& outPlan, std::string& outError);
    int edgeMarginPercent_ = 5;
    bool moreCandidates_ = false;  // try twelve layouts instead of the usual number (a castle that could not be furnished)
};
