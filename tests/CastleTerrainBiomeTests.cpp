#include "CastleMapImageGenerator.h"
#include "TestScale.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// How the castle's outside nature is told apart in the biome map:
//  - a natural mountain is a cliff (TOWN_CLIFF), never the wall's own tile (MOUNTAIN);
//    walls are the only MOUNTAIN / EXMOUNTAIN cells, so they sit right next to a room;
//  - the desert (exteriorTerrain 1) has desert ground everywhere (no PLAIN left) and
//    a cliff rim; forest keeps plain ground and forest nature; mountain keeps plain
//    ground and a cliff rim;
//  - cliff appears only with the desert and mountain terrains.
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// sides: -1 leaves the number of nature sides to the generator (it must not matter here
// when a terrain is requested and the count is fixed to 2).
void Check(int size, int mode, unsigned seed, int terrain, int sides = 2) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"lotFacilities", 0}, {"exteriorTerrain", terrain},
                    {"natureSideCount", sides}};
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    const std::string context = "size=" + std::to_string(size) + " mode=" + std::to_string(mode) +
        " seed=" + std::to_string(seed) + " terrain=" + std::to_string(terrain);
    Require(generator.Generate(plan, error), context + ": " + error);

    std::vector<std::uint8_t> nearRoom(static_cast<std::size_t>(size * size), 0);
    for (const auto& r : plan.regions) {
        if (r.kind != "room" && r.kind != "keep_expansion") continue;
        for (int y = std::max(0, r.y0 - 3); y <= std::min(size - 1, r.y1 + 3); ++y)
            for (int x = std::max(0, r.x0 - 3); x <= std::min(size - 1, r.x1 + 3); ++x)
                nearRoom[static_cast<std::size_t>(y * size + x)] = 1;
    }
    int plain = 0, desert = 0, cliff = 0, forest = 0, strayWall = 0;
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        const std::uint8_t b = plan.biomes[static_cast<std::size_t>(y * size + x)];
        plain += b == PLAIN;
        desert += b == DESERT;
        cliff += b == TOWN_CLIFF;
        forest += b == FOREST;
        if ((b == MOUNTAIN || b == EXMOUNTAIN) && !nearRoom[static_cast<std::size_t>(y * size + x)]) ++strayWall;
    }
    // A wall tile far from any room would be a natural mountain painted as a wall.
    Require(strayWall == 0, context + ": " + std::to_string(strayWall) + " wall-tile cells are far from every room");

    if (terrain == 1) {
        Require(plain == 0, context + ": desert ground left " + std::to_string(plain) + " plain cells");
        Require(desert > size * size / 8,context + ": desert ground is missing (" + std::to_string(desert) + " cells)");
        Require(cliff > 128, context + ": the desert has no cliff rim (" + std::to_string(cliff) + " cells)");
        Require(forest == 0 || forest < 64, context + ": forest appeared in the desert");
    } else if (terrain == 2) {
        Require(plain > 0, context + ": forest terrain lost its plain ground");
        Require(forest > 128, context + ": forest nature is missing");
        Require(cliff == 0, context + ": cliff appeared with forest");
    } else if (terrain == 3) {
        Require(plain > 0, context + ": mountain terrain lost its plain ground");
        Require(cliff > 128, context + ": the mountain is not cliff (" + std::to_string(cliff) + " cells)");
    } else {
        Require(plain > 0, context + ": plain terrain lost its ground");
        Require(cliff == 0, context + ": cliff appeared without nature");
    }
}
// (A few forest tiles are castle gardens, so forest only has to stay small.)
// natureSideCount decides how many of the left / right / top sides get nature before
// the terrain decides what the nature is: 0 means no nature at all, 1..3 that many sides.
void CheckSideCount(int size, int mode, unsigned seed, int terrain, int sides) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"lotFacilities", 0}, {"exteriorTerrain", terrain},
                    {"natureSideCount", sides}};
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    const std::string context = "sides=" + std::to_string(sides) + " size=" + std::to_string(size) +
        " mode=" + std::to_string(mode) + " seed=" + std::to_string(seed) + " terrain=" + std::to_string(terrain);
    Require(generator.Generate(plan, error), context + ": " + error);
    int natural = 0;
    for (const auto& r : plan.regions) natural += r.kind == "natural_terrain";
    // (the doorstep outside a service door is a small patch of sand: it is not nature)
    std::vector<std::uint8_t> doorstep(plan.biomes.size(), 0);
    for (const auto& r : plan.regions) {
        if (r.kind != "castle_side_door") continue;
        for (int y = r.y0 - 8; y <= r.y1 + 8; ++y) for (int x = r.x0 - 8; x <= r.x1 + 8; ++x)
            if (x >= 0 && y >= 0 && x < plan.width && y < plan.depth) doorstep[static_cast<std::size_t>(y * plan.width + x)] = 1;
    }
    int desert = 0, cliff = 0, forest = 0;
    for (std::size_t i = 0; i < plan.biomes.size(); ++i) {
        const std::uint8_t b = plan.biomes[i];
        desert += b == DESERT && !doorstep[i];
        cliff += b == TOWN_CLIFF;
        forest += b == FOREST;
    }
    Require(natural == sides, context + ": expected " + std::to_string(sides) + " nature sides, got " + std::to_string(natural));
    if (sides == 0) {
        Require(desert == 0 && cliff == 0 && forest < 64,
            context + ": nature tiles appeared with no nature side (desert=" + std::to_string(desert) +
            " cliff=" + std::to_string(cliff) + " forest=" + std::to_string(forest) + ")");
    } else {
        Require(desert + cliff + forest > 0, context + ": no nature tiles with nature sides");
    }
}
}  // namespace

int main() {
    try {
        for (const int size : {128, 160}) for (const int mode : {0, 1, 3, 4}) for (unsigned seed = 1; seed <= QuickSeeds(3); ++seed)
            for (const int terrain : {1, 2, 3}) for (const int sides : {0, 1, 2, 3}) CheckSideCount(size, mode, seed, terrain, sides);
        for (const int size : {128, 160}) for (const int mode : {0, 1, 3, 4}) for (unsigned seed = 1; seed <= QuickSeeds(3); ++seed)
            for (const int terrain : {0, 1, 2, 3}) Check(size, mode, seed, terrain);
        std::cout << "CASTLE_TERRAIN_BIOME: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_TERRAIN_BIOME: FAIL " << e.what() << '\n';
        return 1;
    }
}
