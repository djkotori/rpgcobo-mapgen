#include "CastleMapImageGenerator.h"
#include "CastleOdds.h"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

// natureSideCount left to the generator ("random" in the editor) draws how many of the
// left / right / top sides get nature from castle_odds::kNatureSideCountWeights.
// The numbers are 0 sides 15%, 1 side 40%, 2 sides 30%, 3 sides 15%.
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// The table itself: the weights and the roll-to-count mapping are exactly 15 / 40 / 30 / 15 over 100 rolls.
void TestTable() {
    std::array<int, 4> hits{};
    for (int roll = 0; roll < 100; ++roll) ++hits[static_cast<std::size_t>(castle_odds::PickByWeight(castle_odds::kNatureSideCountWeights, roll))];
    Require(hits[0] == 15 && hits[1] == 40 && hits[2] == 30 && hits[3] == 15,
        "nature side weights are " + std::to_string(hits[0]) + "/" + std::to_string(hits[1]) + "/" +
        std::to_string(hits[2]) + "/" + std::to_string(hits[3]) + ", expected 15/40/30/15");
}

// The generator really uses the table: over many seeds the observed shares stay close to it.
void TestGeneratedShares() {
    constexpr int kSeeds = 500;
    constexpr double kTolerancePercent = 7.0;  // about 3 standard deviations at 500 seeds
    std::array<int, 4> counted{};
    for (int seed = 1; seed <= kSeeds; ++seed) {
        MapGenParams params;
        params.width = params.depth = 128;
        params.seed = static_cast<std::uint32_t>(seed);
        // Forest is a terrain that never forces a side, so the count is exactly what was drawn.
        params.extra = {{"assetFoundation", 1}, {"lotFacilities", 0}, {"exteriorTerrain", 2}};
        CastleMapImageGenerator generator(params);
        MapPlan plan;
        std::string error;
        Require(generator.Generate(plan, error), "seed=" + std::to_string(seed) + ": " + error);
        int natural = 0;
        for (const auto& region : plan.regions) natural += region.kind == "natural_terrain";
        Require(natural >= 0 && natural <= 3, "seed=" + std::to_string(seed) + ": " + std::to_string(natural) + " nature sides");
        ++counted[static_cast<std::size_t>(natural)];
    }
    const std::array<double, 4> expected{15.0, 40.0, 30.0, 15.0};
    for (std::size_t sides = 0; sides < 4; ++sides) {
        const double share = 100.0 * counted[sides] / kSeeds;
        Require(std::abs(share - expected[sides]) <= kTolerancePercent,
            std::to_string(sides) + " nature sides appeared in " + std::to_string(share) + "% of castles, expected " +
            std::to_string(expected[sides]) + "% (+-" + std::to_string(kTolerancePercent) + ")");
    }
}
}  // namespace

int main() {
    try {
        TestTable();
        TestGeneratedShares();
        std::cout << "CASTLE_NATURE_ODDS: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_NATURE_ODDS: FAIL " << e.what() << '\n';
        return 1;
    }
}
