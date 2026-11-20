#include "CastleMapImageGenerator.h"
#include "TestScale.h"

#include <iostream>
#include <stdexcept>
#include <string>

// mainRoadWidth is the width of the castle's main avenue in cells (the straight road from the keep's door
// to the map edge). It accepts 1..20 and the avenue is exactly that wide, even widths included.
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool IsRoad(std::uint8_t biome) { return biome == ROAD || biome == BRIDGE; }

void Check(int size, int mode, unsigned seed, int roadWidth) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    // Plain ground and no nature: nothing but the avenue lies on the bottom row.
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"lotFacilities", 0}, {"exteriorTerrain", 0},
                    {"natureSideCount", 0}, {"mainRoadWidth", roadWidth}};
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    const std::string context = "size=" + std::to_string(size) + " mode=" + std::to_string(mode) +
        " seed=" + std::to_string(seed) + " mainRoadWidth=" + std::to_string(roadWidth);
    Require(generator.Generate(plan, error), context + ": " + error);

    int exitX = -1;
    for (const auto& marker : plan.markers)
        if (marker.kind == "map_exit" && marker.role == "open_castle_approach") exitX = marker.x;
    Require(exitX >= 0, context + ": no map_exit marker for the main avenue");

    const int bottom = size - 1;
    const auto at = [&](int x) { return plan.biomes[static_cast<std::size_t>(bottom * size + x)]; };
    Require(IsRoad(at(exitX)), context + ": the avenue is missing at the map edge");
    int left = exitX, right = exitX;
    while (left > 0 && IsRoad(at(left - 1))) --left;
    while (right + 1 < size && IsRoad(at(right + 1))) ++right;
    const int run = right - left + 1;
    Require(run == roadWidth, context + ": the avenue is " + std::to_string(run) + " cells wide at the map edge");
}
}  // namespace

int main() {
    try {
        for (const int size : {128, 160}) for (int mode = 0; mode < 5; ++mode) for (unsigned seed = 1; seed <= QuickSeeds(2); ++seed)
            for (const int roadWidth : {3, 5, 6, 7, 12, 20}) Check(size, mode, seed, roadWidth);
        std::cout << "CASTLE_ROAD_WIDTH: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_ROAD_WIDTH: FAIL " << e.what() << '\n';
        return 1;
    }
}
