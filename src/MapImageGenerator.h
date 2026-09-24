#pragma once

#include "MapPlan.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum MapGenBiomeID {
    FLOOR_LV1 = 0,
    FLOOR_LV2 = 1,
    PLAIN = 0,
    DESERT = 1,
    FOREST = 2,
    WALL = 3,
    INNERWALL = 4,
    MOUNTAIN = 3,
    EXMOUNTAIN = 4,
    ROAD = 5,
    BRIDGE = 6,
    LADDER = 6,
    RIVER = 7,
    LAKE = 8,
    SEA = 9,
    // Settlement semantic tiles. Existing biome IDs remain unchanged.
    TOWN_FLOOR = 10,
    TOWN_WOOD_FLOOR = 11,
    TOWN_STONE_FLOOR = 12,
    TOWN_FURNITURE = 13,
    TOWN_PAVEMENT = 14,
    TOWN_SEA = 15,
    TOWN_CLIFF = 16,
    TOWN_TREES = 17,
    CASTLE_WALL_WALK = 18,
};

struct MapGenParams {
    int width = 256;
    int depth = 256;
    std::uint32_t seed = 0;
    std::unordered_map<std::string, double> extra;
};

struct MapGenColor {
    std::uint8_t r = 255;
    std::uint8_t g = 0;
    std::uint8_t b = 255;
};

class MapImageGenerator {
public:
    explicit MapImageGenerator(MapGenParams params) : params_(std::move(params)) {}
    virtual ~MapImageGenerator() = default;

    virtual bool Generate(MapPlan& outPlan, std::string& outError) = 0;
    virtual MapGenColor BiomeToColor(std::uint8_t biome) const = 0;

    const MapGenParams& Params() const { return params_; }

protected:
    double GetParam(const std::string& key, double defaultValue) const {
        auto it = params_.extra.find(key);
        if (it == params_.extra.end()) {
            return defaultValue;
        }
        return it->second;
    }

    MapGenParams params_;
};
