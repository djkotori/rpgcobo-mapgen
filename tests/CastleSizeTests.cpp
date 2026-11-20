#include "CastleMapImageGenerator.h"
#include "TestScale.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

// The castle uses the map: its rooms span most of the ground that the 5% edge margin leaves.
//  - With no nature on the sides, the rooms span at least 65% of the map's width (whatever the silhouette) and at least
//    80% of its height (the keep goes down to the south margin; a palace keeps its court in front, so it is left out).
//  - The 5% margin stays clear on every side.
//  - A map smaller than 128 x 128 is raised to 128 x 128 (the editor handles no smaller castle).
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
}  // namespace

int main() {
    try {
        int castles = 0, narrow = 0, low = 0;
        double widthSum = 0, heightSum = 0;
        std::string worst;
        for (const int size : {128, 160, 192}) for (const int mode : {0, 1, 2, 3, 4}) for (unsigned seed = 1; seed <= QuickSeeds(12); ++seed) {
            MapGenParams params;
            params.width = params.depth = size;
            params.seed = seed;
            params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"lotFacilities", 0}, {"exteriorTerrain", 0}, {"natureSideCount", 0}};
            CastleMapImageGenerator generator(params);
            MapPlan plan;
            std::string error;
            const std::string context = "size=" + std::to_string(size) + " mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
            Require(generator.Generate(plan, error), context + ": " + error);
            int x0 = size, x1 = 0, y0 = size, y1 = 0;
            for (const auto& r : plan.regions) {
                if (r.kind != "room") continue;
                x0 = std::min(x0, r.x0); x1 = std::max(x1, r.x1); y0 = std::min(y0, r.y0); y1 = std::max(y1, r.y1);
            }
            const double w = (x1 - x0 + 1) / static_cast<double>(size), h = (y1 - y0 + 1) / static_cast<double>(size);
            ++castles;
            widthSum += w; heightSum += h;
            // The edge margin: 5% (the margin falls back to less only where the castle does not fit).
            Require(x0 * 100 >= size * 4 && y0 * 100 >= size * 4 && (size - 1 - x1) * 100 >= size * 4 && (size - 1 - y1) * 100 >= size * 4,
                context + ": the rooms are in the edge margin");
            if (w < 0.65) { ++narrow; worst = context + " width " + std::to_string(w); }
            if (mode != 3 && h < 0.80) ++low;
        }
        std::cout << "  castles " << castles << ", mean width " << widthSum / castles << ", mean height " << heightSum / castles
                  << ", narrower than 65% " << narrow << ", lower than 80% " << low << '\n';
        Require(narrow == 0, "a castle spans less than 65% of the map's width (" + worst + "): " + std::to_string(narrow) + " of " + std::to_string(castles));
        Require(low == 0, "a castle spans less than 80% of the map's height: " + std::to_string(low) + " of " + std::to_string(castles));
        // A map under 128 x 128 is raised to 128 x 128.
        {
            MapGenParams params;
            params.width = 64; params.depth = 96; params.seed = 3;
            params.extra = {{"assetFoundation", 1}, {"lotFacilities", 0}};
            CastleMapImageGenerator generator(params);
            MapPlan plan;
            std::string error;
            Require(generator.Generate(plan, error), "small request: " + error);
            Require(plan.width == 128 && plan.depth == 128, "a map under 128 x 128 was not raised to 128 x 128: " + std::to_string(plan.width) + "x" + std::to_string(plan.depth));
        }
        std::cout << "CASTLE_SIZE: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_SIZE: FAIL " << e.what() << '\n';
        return 1;
    }
}
