#include "CastleMapImageGenerator.h"
#include "TestScale.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

// The castle's outer wall keeps clear of the edges of the map: no wall cell lies within the outer 5%
// of the map on any side (7 cells on a 128 map), whatever the castle type, the terrain or the seed.
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void Check(int size, int mode, unsigned seed, int& tightest) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"lotFacilities", 0}};  // terrain and nature sides are drawn by the seed
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    const std::string context = "size=" + std::to_string(size) + " mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
    Require(generator.Generate(plan, error), context + ": " + error);
    const int margin = (size * 5 + 99) / 100;  // 5% of the map, rounded up
    int left = size, right = size, top = size, bottom = size;
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        const std::uint8_t b = plan.biomes[static_cast<std::size_t>(y * size + x)];
        if (b != MOUNTAIN && b != EXMOUNTAIN) continue;
        left = std::min(left, x); right = std::min(right, size - 1 - x);
        top = std::min(top, y); bottom = std::min(bottom, size - 1 - y);
    }
    const int nearest = std::min(std::min(left, right), std::min(top, bottom));
    tightest = std::min(tightest, nearest);
    Require(nearest >= margin, context + ": the outer wall comes within " + std::to_string(nearest) + " cells of the edge (the margin is " +
        std::to_string(margin) + ": left " + std::to_string(left) + ", right " + std::to_string(right) + ", top " + std::to_string(top) +
        ", bottom " + std::to_string(bottom) + ")");
}
}  // namespace

int main() {
    try {
        int tightest = 1 << 20;
        for (const int size : {128, 160, 192}) for (const int mode : {0, 1, 2, 3, 4}) for (unsigned seed = 1; seed <= QuickSeeds(12); ++seed)
            Check(size, mode, seed, tightest);
        std::cout << "  tightest wall-to-edge distance: " << tightest << " cells\n";
        std::cout << "CASTLE_MARGIN: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_MARGIN: FAIL " << e.what() << '\n';
        return 1;
    }
}
