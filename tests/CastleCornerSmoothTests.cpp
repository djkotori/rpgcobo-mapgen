#include "CastleMapImageGenerator.h"
#include "TestScale.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Where two nature sides meet (the top with the left or the right), the boundary between nature and
// land is smooth: one more pass of a 7x7 majority filter over the corner window barely changes it.
// (Cells that are neither nature nor open land - walls, floors, roads - do not vote and are left
// alone together with the ground within three cells of them, as in the generator.)
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr int kMaxChangesPerCorner = 10;

int g_worst = 0;

void CheckCorner(const MapPlan& plan, int size, std::uint8_t nature, bool rightCorner, int leftWidth, int rightStart, int topThickness,
                 const std::string& context) {
    const int x0 = rightCorner ? std::max(0, rightStart - 26) : 0;
    const int x1 = rightCorner ? size : std::min(size, leftWidth + 26);
    const int y1 = std::min(size, topThickness + 26);
    const auto kind = [&](int x, int y) {  // 1 nature, 0 open land, -1 anything else
        if (x < 0 || y < 0 || x >= size || y >= size) return -1;
        const std::uint8_t b = plan.biomes[static_cast<std::size_t>(y * size + x)];
        if (b == nature) return 1;
        return (b == PLAIN || b == DESERT) ? 0 : -1;
    };
    int changes = 0;
    for (int y = 0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
        const int here = kind(x, y);
        if (here < 0) continue;
        // The ground right beside the castle or a road is left alone (as in the generator).
        bool frozen = false;
        for (int dy = -3; dy <= 3 && !frozen; ++dy) for (int dx = -3; dx <= 3; ++dx) {
            if (x + dx < 0 || y + dy < 0 || x + dx >= size || y + dy >= size) continue;
            if (kind(x + dx, y + dy) < 0) { frozen = true; break; }
        }
        if (frozen) continue;
        int natureCount = 0, total = 0;
        for (int dy = -3; dy <= 3; ++dy) for (int dx = -3; dx <= 3; ++dx) {
            const int k = kind(x + dx, y + dy);
            if (k < 0) continue;
            ++total;
            natureCount += k;
        }
        const int next = natureCount * 2 > total ? 1 : natureCount * 2 < total ? 0 : here;
        if (next != here && std::getenv("CORNER_DEBUG")) std::cout << "    change " << x << "," << y << " here=" << here << " next=" << next << " " << context << '\n';
        changes += next != here;
    }
    g_worst = std::max(g_worst, changes);
    Require(changes <= kMaxChangesPerCorner, context + ": the corner is jagged (" + std::to_string(changes) + " cells would change)");
}

void Check(int size, int mode, unsigned seed, int terrain) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"lotFacilities", 0}, {"exteriorTerrain", terrain}, {"natureSideCount", 3}};
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    const std::string context = "corner size=" + std::to_string(size) + " mode=" + std::to_string(mode) + " seed=" +
        std::to_string(seed) + " terrain=" + std::to_string(terrain);
    Require(generator.Generate(plan, error), context + ": " + error);
    const std::uint8_t nature = terrain == 2 ? FOREST : TOWN_CLIFF;
    bool left = false, right = false, top = false;
    int leftWidth = 0, rightStart = size, topThickness = 0;
    for (const auto& region : plan.regions) {
        if (region.kind != "natural_terrain") continue;
        if (region.x0 == 0 && region.x1 < size - 1) { left = true; leftWidth = region.x1 + 1; }
        else if (region.x1 == size - 1 && region.x0 > 0) { right = true; rightStart = region.x0; }
        else if (region.y0 == 0) { top = true; topThickness = region.y1 + 1; }
    }
    if (!top) return;
    if (left) CheckCorner(plan, size, nature, false, leftWidth, rightStart, topThickness, context + " (left)");
    if (right) CheckCorner(plan, size, nature, true, leftWidth, rightStart, topThickness, context + " (right)");
}
}  // namespace

int main() {
    try {
        for (const int size : {128, 160}) for (const int mode : {0, 3, 4}) for (unsigned seed = 1; seed <= QuickSeeds(16); ++seed)
            for (const int terrain : {1, 2, 3}) Check(size, mode, seed, terrain);
        std::cout << "  worst corner: " << g_worst << " cells would change\n";
        std::cout << "CASTLE_CORNER_SMOOTH: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_CORNER_SMOOTH: FAIL " << e.what() << '\n';
        return 1;
    }
}
