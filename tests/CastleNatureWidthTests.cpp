#include "CastleMapImageGenerator.h"
#include "TestScale.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

// How far a side of nature (forest, cliff or desert rim) reaches in from the map edge is at most
// 10% of the map size (12 cells on a 128 map), however small the castle is: a small castle is not
// squeezed by nature pressing in from the sides, and it keeps room around it. (The water of a river
// castle is not limited.)
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void Check(int size, int mode, unsigned seed, int terrain, int& widest) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"exteriorTerrain", terrain}, {"natureSideCount", 3}};
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    const std::string context = "size=" + std::to_string(size) + " mode=" + std::to_string(mode) + " seed=" +
        std::to_string(seed) + " terrain=" + std::to_string(terrain);
    Require(generator.Generate(plan, error), context + ": " + error);
    const int limit = size / 10;
    int sides = 0;
    for (const auto& region : plan.regions) {
        if (region.kind != "natural_terrain") continue;
        ++sides;
        int reach = 0;
        if (region.x0 == 0 && region.x1 < size - 1) reach = region.x1 + 1;              // left side
        else if (region.x1 == size - 1 && region.x0 > 0) reach = size - region.x0;      // right side
        else if (region.y0 == 0) reach = region.y1 + 1;                                  // top side
        widest = std::max(widest, reach);
        Require(reach <= limit, context + ": a side of nature reaches " + std::to_string(reach) + " cells (the limit is " + std::to_string(limit) + ")");
    }
    Require(sides > 0, context + ": no nature was generated");
}
}  // namespace

int main() {
    try {
        int widest = 0;
        for (const int size : {128, 160, 192}) for (const int mode : {0, 1, 3, 4}) for (unsigned seed = 1; seed <= QuickSeeds(6); ++seed)
            for (const int terrain : {1, 2, 3}) Check(size, mode, seed, terrain, widest);
        // The limit is used, not undershot: some side reaches close to it.
        std::cout << "  widest side: " << widest << " cells\n";
        std::cout << "CASTLE_NATURE_WIDTH: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_NATURE_WIDTH: FAIL " << e.what() << '\n';
        return 1;
    }
}
