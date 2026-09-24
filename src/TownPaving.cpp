#include "TownPaving.h"

#include "MapImageGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <vector>

namespace {
constexpr int Unreachable = std::numeric_limits<int>::max() / 4;

struct PavingPlot {
    std::vector<int> cells;
    std::vector<int> neighbors;
    int stage = 5;
    int demandDistance = 0;
    int accessDistance = Unreachable;
    int accessParent = -1;
    bool streetFront = false;
    bool paved = false;
};
}

void ApplyTownPaving(
    MapPlan& plan,
    const std::vector<std::uint8_t>& footprint,
    const std::vector<std::uint8_t>& structure,
    const std::vector<std::uint8_t>& plazaMask,
    const std::vector<std::uint8_t>& roadClass,
    std::uint8_t groundTile) {
    const int width = plan.width;
    const int height = plan.depth;
    const int area = width * height;
    const int scale = std::clamp(std::min(width, height) / 160, 1, 3);
    const int plotSide = 4 * scale;
    const int bankWidth = 5 * scale;
    const int avenueMargin = 4 * scale;
    const int facilityMargin = 3 * scale;
    const int normalMargin = 3 * scale;
    const int densityRadius = 12 * scale;
    plan.pavingModel = "activity_corridors_v2";
    plan.pavingStages = {
        {"avenue_frontage"}, {"waterfront"}, {"large_building_forecourt"},
        {"normal_road_frontage"}, {"dense_housing"}, {"block_infill"},
    };
    plan.pavingActualRatio = 0.0;
    if (plan.pavingTargetRatio <= 0.0) return;

    auto neighbors = [&](int index, const auto& visit) {
        const int x = index % width;
        const int y = index / width;
        if (x > 0) visit(index - 1);
        if (x + 1 < width) visit(index + 1);
        if (y > 0) visit(index - width);
        if (y + 1 < height) visit(index + width);
    };
    std::vector<std::uint8_t> eligible(area, 0), outdoor(area, 0), street(area, 0);
    for (int i = 0; i < area; ++i) {
        const auto tile = plan.biomes[i];
        const bool open = footprint[i] && !structure[i] && tile != RIVER &&
            tile != LAKE && tile != SEA && tile != MOUNTAIN && tile != EXMOUNTAIN &&
            tile != TOWN_SEA && tile != TOWN_CLIFF && tile != TOWN_TREES &&
            tile != TOWN_FURNITURE;
        eligible[i] = open && !plazaMask[i] && tile == groundTile;
        street[i] = open && (tile == ROAD || tile == BRIDGE || tile == TOWN_PAVEMENT ||
            tile == TOWN_STONE_FLOOR || (plazaMask[i] && tile == TOWN_FLOOR));
        outdoor[i] = eligible[i] || street[i];
    }
    // Parks remain parks even when an environment shares their tile colour.
    for (const auto& region : plan.regions) {
        if (region.kind != "park") continue;
        for (int y = std::max(0, region.y0); y <= std::min(height - 1, region.y1); ++y) {
            for (int x = std::max(0, region.x0); x <= std::min(width - 1, region.x1); ++x) {
                const int i = y * width + x;
                eligible[i] = 0;
                outdoor[i] = street[i];
            }
        }
    }
    const int eligibleCount = std::accumulate(eligible.begin(), eligible.end(), 0);
    if (eligibleCount == 0) return;

    // Multi-source ground distances stop at rivers, walls, buildings and parks.
    // In particular neither a facade nor a quay paints through an obstacle.
    auto distances = [&](const std::vector<int>& seeds, int limit) {
        std::vector<int> result(area, Unreachable);
        std::queue<int> pending;
        for (int i : seeds) {
            if (!outdoor[i] || result[i] == 0) continue;
            result[i] = 0;
            pending.push(i);
        }
        while (!pending.empty()) {
            const int current = pending.front();
            pending.pop();
            if (result[current] >= limit) continue;
            neighbors(current, [&](int next) {
                if (!outdoor[next] || result[next] <= result[current] + 1) return;
                result[next] = result[current] + 1;
                pending.push(next);
            });
        }
        return result;
    };
    std::vector<int> avenues, normalRoads, streets, banks, facilities;
    for (int i = 0; i < area; ++i) {
        if (!outdoor[i]) continue;
        if (street[i]) streets.push_back(i);
        if (street[i] && roadClass[i] == 3) avenues.push_back(i);
        if (street[i] && roadClass[i] == 2) normalRoads.push_back(i);
        bool waterAdjacent = false;
        neighbors(i, [&](int next) {
            // Actual coast water is distinct from the outside-of-map sentinel.
            if (plan.biomes[next] == RIVER || plan.biomes[next] == LAKE || plan.biomes[next] == TOWN_SEA) waterAdjacent = true;
        });
        if (waterAdjacent) banks.push_back(i);
    }
    std::vector<int> nearbyHomes(area, 0);
    std::vector<std::uint8_t> forecourt(area, 0);
    for (const auto& building : plan.regions) {
        if (building.kind != "building" && building.kind != "market") continue;
        const int buildingArea = (building.x1 - building.x0 + 1) * (building.y1 - building.y0 + 1);
        const bool large = buildingArea >= 120 * scale * scale || building.role == "manor" ||
            building.role == "mayor_house" || building.role == "inn" ||
            building.role == "market_hall" || building.role == "shrine" || building.role == "caravanserai";
        std::vector<int> frontage;
        for (int y = std::max(0, building.y0 - 1); y <= std::min(height - 1, building.y1 + 1); ++y) {
            for (int x = std::max(0, building.x0 - 1); x <= std::min(width - 1, building.x1 + 1); ++x) {
                if (x >= building.x0 && x <= building.x1 && y >= building.y0 && y <= building.y1) continue;
                const int i = y * width + x;
                if (outdoor[i]) frontage.push_back(i);
            }
        }
        if (large) {
            facilities.insert(facilities.end(), frontage.begin(), frontage.end());
            // The entrance side gets a working forecourt for carts/gatherings;
            // the side and rear receive only the smaller maintenance margin.
            for (const auto& door : plan.markers) {
                if (door.kind != "building_door" || door.regionId != building.id) continue;
                int x0 = building.x0 - 2 * scale, x1 = building.x1 + 2 * scale;
                int y0 = building.y0 - 2 * scale, y1 = building.y1 + 2 * scale;
                if (door.x == building.x0) { x0 = door.x - 6 * scale; x1 = door.x - 1; }
                else if (door.x == building.x1) { x0 = door.x + 1; x1 = door.x + 6 * scale; }
                else if (door.y == building.y0) { y0 = door.y - 6 * scale; y1 = door.y - 1; }
                else { y0 = door.y + 1; y1 = door.y + 6 * scale; }
                const auto access = distances(frontage, 8 * scale);
                for (int y = std::max(0, y0); y <= std::min(height - 1, y1); ++y) {
                    for (int x = std::max(0, x0); x <= std::min(width - 1, x1); ++x) {
                        const int i = y * width + x;
                        if (eligible[i] && access[i] < Unreachable) forecourt[i] = 1;
                    }
                }
            }
        }
        if (building.residentCount <= 0 || building.livingArrangement == "business_only") continue;
        const auto homeDistance = distances(frontage, densityRadius);
        // Count distinct resident buildings, not facade pixels or large empty
        // warehouses. Nearby houses on the other bank do not count as infill.
        for (int y = std::max(0, building.y0 - densityRadius - 1); y <= std::min(height - 1, building.y1 + densityRadius + 1); ++y) {
            for (int x = std::max(0, building.x0 - densityRadius - 1); x <= std::min(width - 1, building.x1 + densityRadius + 1); ++x) {
                const int i = y * width + x;
                if (eligible[i] && homeDistance[i] <= densityRadius) ++nearbyHomes[i];
            }
        }
    }
    const auto avenueDistance = distances(avenues, avenueMargin);
    const auto bankDistance = distances(banks, bankWidth - 1);
    const auto facilityDistance = distances(facilities, facilityMargin - 1);
    const auto normalDistance = distances(normalRoads, normalMargin);
    const auto streetDistance = distances(streets, Unreachable);

    // Small contiguous work plots are indivisible paving projects. They are
    // clipped by real streets/facades/banks, so a budget cutoff cannot select
    // isolated noise-ranked pixels or a one-pixel strip along an entire road.
    std::vector<int> plotAt(area, -1);
    std::vector<PavingPlot> plots;
    for (int start = 0; start < area; ++start) {
        if (!eligible[start] || plotAt[start] >= 0 || streetDistance[start] == Unreachable) continue;
        const int id = static_cast<int>(plots.size());
        plots.emplace_back();
        auto& plot = plots.back();
        std::array<int, 6> votes{};
        std::array<int, 6> distanceSum{};
        std::queue<int> pending;
        plotAt[start] = id;
        pending.push(start);
        while (!pending.empty()) {
            const int current = pending.front();
            pending.pop();
            plot.cells.push_back(current);
            int stage = 5, distance = streetDistance[current];
            if (avenueDistance[current] <= avenueMargin) { stage = 0; distance = avenueDistance[current]; }
            else if (bankDistance[current] < bankWidth) { stage = 1; distance = bankDistance[current]; }
            else if (facilityDistance[current] < facilityMargin || forecourt[current]) { stage = 2; distance = 0; }
            else if (normalDistance[current] <= normalMargin) { stage = 3; distance = normalDistance[current]; }
            else if (nearbyHomes[current] >= 3) { stage = 4; distance = -nearbyHomes[current]; }
            ++votes[stage];
            distanceSum[stage] += distance;
            neighbors(current, [&](int next) {
                if (street[next]) plot.streetFront = true;
                if (!eligible[next] || plotAt[next] >= 0) return;
                if ((next % width) / plotSide != (start % width) / plotSide ||
                    (next / width) / plotSide != (start / width) / plotSide) return;
                plotAt[next] = id;
                pending.push(next);
            });
        }
        // A tiny corner of influence cannot claim a whole otherwise rural plot.
        plot.demandDistance = streetDistance[start];
        for (int stage = 0; stage < 6; ++stage) {
            if (votes[stage] == 0 || (stage < 5 && votes[stage] * 4 < static_cast<int>(plot.cells.size()))) continue;
            plot.stage = stage;
            plot.demandDistance = distanceSum[stage] / votes[stage];
            break;
        }
    }

    std::queue<int> accessQueue;
    for (int id = 0; id < static_cast<int>(plots.size()); ++id) {
        auto& plot = plots[id];
        for (int cell : plot.cells) {
            neighbors(cell, [&](int next) {
                if (plotAt[next] >= 0 && plotAt[next] != id) plot.neighbors.push_back(plotAt[next]);
            });
        }
        std::sort(plot.neighbors.begin(), plot.neighbors.end());
        plot.neighbors.erase(std::unique(plot.neighbors.begin(), plot.neighbors.end()), plot.neighbors.end());
        if (plot.streetFront) {
            plot.accessDistance = 0;
            accessQueue.push(id);
        }
    }
    // A quay or courtyard needs a usable approach before it can be developed.
    // Reserve a shortest sequence of whole plots from the existing street.
    while (!accessQueue.empty()) {
        const int id = accessQueue.front();
        accessQueue.pop();
        for (int next : plots[id].neighbors) {
            if (plots[next].accessDistance <= plots[id].accessDistance + 1) continue;
            plots[next].accessDistance = plots[id].accessDistance + 1;
            plots[next].accessParent = id;
            accessQueue.push(next);
        }
    }
    std::vector<int> order(plots.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int left, int right) {
        const auto& a = plots[left];
        const auto& b = plots[right];
        if (a.stage != b.stage) return a.stage < b.stage;
        if (a.demandDistance != b.demandDistance) return a.demandDistance < b.demandDistance;
        if (a.accessDistance != b.accessDistance) return a.accessDistance < b.accessDistance;
        return left < right;
    });
    const int target = static_cast<int>(std::lround(eligibleCount * plan.pavingTargetRatio));
    int paved = 0;
    for (int id : order) {
        if (paved >= target) break;
        if (plots[id].paved || plots[id].accessDistance == Unreachable) continue;
        auto& stage = plan.pavingStages[plots[id].stage];
        ++stage.projects;
        for (int current = id; current >= 0 && !plots[current].paved; current = plots[current].accessParent) {
            auto& plot = plots[current];
            for (int cell : plot.cells) plan.biomes[cell] = TOWN_PAVEMENT;
            plot.paved = true;
            const int count = static_cast<int>(plot.cells.size());
            paved += count;
            stage.cells += count;
            if (current != id) stage.accessCells += count;
        }
    }
    plan.pavingActualRatio = static_cast<double>(paved) / eligibleCount;
}
