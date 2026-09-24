#include "MapImageGenerator.h"
#include "TownPaving.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <queue>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Settlement {
    MapPlan plan;
    std::vector<std::uint8_t> footprint, structure, plaza, roadClass;
    Settlement(int w = 96, int h = 96) {
        plan.Reset(w, h, 42, "town");
        plan.biomes.assign(w * h, PLAIN);
        footprint.assign(w * h, 1);
        structure.assign(w * h, 0);
        plaza.assign(w * h, 0);
        roadClass.assign(w * h, 0);
    }
    int At(int x, int y) const { return y * plan.width + x; }
    void Rect(int x0, int y0, int x1, int y1, int tile, int road = 0) {
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
            const int i = At(x, y);
            plan.biomes[i] = static_cast<std::uint8_t>(tile);
            roadClass[i] = static_cast<std::uint8_t>(road);
            structure[i] = tile == MOUNTAIN;
        }
    }
    void Building(int id, int x, int y, int w, int h, const char* role, int residents) {
        Rect(x, y, x + w - 1, y + h - 1, MOUNTAIN);
        plan.regions.push_back({"building", id, x, y, x + w - 1, y + h - 1});
        plan.regions.back().role = role;
        plan.regions.back().residentCount = residents;
        plan.markers.push_back({"building_door", x + w / 2, y + h - 1, id, id});
    }
    MapPlan Pave(double ratio) const {
        MapPlan result = plan;
        result.pavingTargetRatio = ratio;
        ApplyTownPaving(result, footprint, structure, plaza, roadClass, PLAIN);
        return result;
    }
};

void RequireConnectedPaving(const MapPlan& plan) {
    std::queue<int> pending;
    std::vector<bool> seen(plan.biomes.size(), false);
    for (int i = 0; i < static_cast<int>(plan.biomes.size()); ++i) {
        if (plan.biomes[i] == ROAD || plan.biomes[i] == BRIDGE) { seen[i] = true; pending.push(i); }
    }
    while (!pending.empty()) {
        const int i = pending.front(); pending.pop();
        auto visit = [&](int next) {
            if (seen[next] || plan.biomes[next] != TOWN_PAVEMENT) return;
            seen[next] = true; pending.push(next);
        };
        if (i % plan.width) visit(i - 1);
        if (i % plan.width + 1 < plan.width) visit(i + 1);
        if (i >= plan.width) visit(i - plan.width);
        if (i + plan.width < static_cast<int>(plan.biomes.size())) visit(i + plan.width);
    }
    for (int i = 0; i < static_cast<int>(plan.biomes.size()); ++i) {
        Require(plan.biomes[i] != TOWN_PAVEMENT || seen[i], "isolated paving has no paved approach to a road");
    }
}

void TestPriorityAndGrowth() {
    Settlement city;
    city.Rect(0, 8, 95, 13, ROAD, 3);
    city.Rect(8, 14, 11, 95, ROAD, 2);
    city.Rect(12, 72, 95, 75, ROAD, 2);
    city.Rect(20, 28, 88, 31, RIVER);
    city.Building(1, 52, 40, 16, 12, "inn", 4);
    city.Building(2, 24, 82, 6, 6, "house", 3);
    city.Building(3, 34, 82, 6, 6, "house", 3);
    city.Building(4, 44, 82, 6, 6, "house", 3);
    const auto full = city.Pave(1.0);
    Require(full.pavingStages.size() == 6, "missing development stages");
    for (const auto& stage : full.pavingStages) Require(stage.cells > 0, "fixture must exercise every priority");
    int previousStage = 0;
    auto previous = city.Pave(0.0);
    Require(previous.biomes == city.plan.biomes, "zero paving changed the settlement");
    for (int step = 1; step <= 20; ++step) {
        const auto result = city.Pave(step / 20.0);
        Require(result.biomes == city.Pave(step / 20.0).biomes, "paving is nondeterministic");
        Require(result.pavingActualRatio + 0.0001 >= step / 20.0, "paving failed to reach its budget");
        Require(result.pavingActualRatio < step / 20.0 + 0.025, "a work project grossly exceeded the budget");
        int lastStage = 0;
        for (int stage = 0; stage < 6; ++stage) {
            if (result.pavingStages[stage].cells == 0) continue;
            lastStage = stage;
            for (int earlier = 0; earlier < stage; ++earlier) {
                Require(result.pavingStages[earlier].cells == full.pavingStages[earlier].cells,
                    "lower priority was funded before an earlier stage completed");
            }
        }
        Require(lastStage >= previousStage, "development went backwards");
        previousStage = lastStage;
        for (int i = 0; i < static_cast<int>(result.biomes.size()); ++i) {
            Require(previous.biomes[i] != TOWN_PAVEMENT || result.biomes[i] == TOWN_PAVEMENT, "more budget removed existing pavement");
            if (city.plan.biomes[i] != PLAIN) Require(result.biomes[i] == city.plan.biomes[i], "paving overwrote a road, water or building");
        }
        RequireConnectedPaving(result);
        previous = result;
    }
    std::cout << "PASS priority order, budget, deterministic staged growth and paved access\n";
}

void TestBarriersAndParks() {
    Settlement city(64, 48);
    city.Rect(0, 8, 27, 13, ROAD, 3);
    city.Rect(30, 0, 31, 47, MOUNTAIN);
    city.Rect(4, 25, 18, 28, LAKE);
    // A park deliberately shares the normal ground tile.
    city.plan.regions.push_back({"park", 1, 5, 35, 15, 42});
    const auto result = city.Pave(1.0);
    for (int y = 0; y < 48; ++y) for (int x = 0; x < 64; ++x) {
        if (x >= 30 || (x >= 5 && x <= 15 && y >= 35 && y <= 42)) {
            Require(result.biomes[city.At(x, y)] == city.plan.biomes[city.At(x, y)], "paving crossed a wall or overwrote a park");
        }
    }
    RequireConnectedPaving(result);
    std::cout << "PASS inaccessible land, water, walls and same-colour parks preserved\n";
}

void TestStraightFrontageAndForecourt() {
    Settlement city(64, 64);
    city.Rect(0, 28, 63, 33, ROAD, 3);
    const auto result = city.Pave(0.20);
    for (int y : {24, 25, 26, 27, 34, 35}) for (int x = 0; x < 64; ++x) {
        Require(result.biomes[city.At(x, y)] == TOWN_PAVEMENT, "straight avenue frontage became broken strips");
    }
    Settlement facility(80, 80);
    facility.Rect(0, 47, 79, 48, ROAD, 1);
    facility.Building(1, 30, 30, 16, 12, "inn", 0);
    const auto court = facility.Pave(0.05);
    Require(court.biomes[facility.At(37, 46)] == TOWN_PAVEMENT, "entrance forecourt missing");
    Require(court.biomes[facility.At(37, 24)] == PLAIN, "rear was expanded as much as the entrance forecourt");
    RequireConnectedPaving(court);
    std::cout << "PASS unbroken avenue margins and entrance-oriented facility forecourt\n";
}
}

int main() {
    try {
        TestPriorityAndGrowth();
        TestBarriersAndParks();
        TestStraightFrontageAndForecourt();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
