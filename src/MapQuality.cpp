#include "MapQuality.h"

#include "MapImageGenerator.h"
#include "CastleProgram.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <set>
#include <utility>

namespace {

bool IsWalkable(const std::string& type, std::uint8_t biome) {
    if (type == "cave") {
        return biome == FLOOR_LV1 || biome == FLOOR_LV2 || biome == LADDER;
    }
    return biome == PLAIN || biome == DESERT || biome == FOREST || biome == ROAD || biome == BRIDGE ||
        (type == "town" || type == "village" || type == "castle") &&
            (biome == TOWN_FLOOR || biome == TOWN_WOOD_FLOOR || biome == TOWN_STONE_FLOOR || biome == TOWN_FURNITURE ||
                biome == TOWN_PAVEMENT || (type == "castle" && biome == CASTLE_WALL_WALK));
}

bool IsFloor(const std::string& type, std::uint8_t biome) {
    if (type == "cave") {
        return biome == FLOOR_LV1 || biome == FLOOR_LV2;
    }
    return biome == PLAIN || biome == DESERT || biome == FOREST ||
        (type == "town" || type == "village" || type == "castle") &&
            (biome == TOWN_FLOOR || biome == TOWN_WOOD_FLOOR || biome == TOWN_STONE_FLOOR || biome == TOWN_PAVEMENT ||
                (type == "castle" && biome == CASTLE_WALL_WALK));
}

bool IsWater(std::uint8_t biome) {
    return biome == RIVER || biome == LAKE || biome == SEA || biome == TOWN_SEA;
}

bool IsDryLand(std::uint8_t biome) {
    return !IsWater(biome);
}

bool IsRoadLike(std::uint8_t biome) {
    return biome == ROAD || biome == BRIDGE;
}

bool InBounds(int x, int y, int width, int depth) {
    return x >= 0 && y >= 0 && x < width && y < depth;
}

int CellIndex(int x, int y, int width) {
    return y * width + x;
}

}  // namespace

MapQualityMetrics EvaluateMapQuality(const MapPlan& plan) {
    MapQualityMetrics result;
    result.totalCells = std::max(0, plan.width * plan.depth);
    if (plan.width <= 0 || plan.depth <= 0 ||
        plan.biomes.size() != static_cast<std::size_t>(result.totalCells)) {
        result.issues.push_back("biome grid size does not match plan dimensions");
        return result;
    }

    const int total = result.totalCells;
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(total), 0);
    const std::array<int, 4> dx{1, -1, 0, 0};
    const std::array<int, 4> dy{0, 0, 1, -1};

    int floorCells = 0;
    int corridorCells = 0;
    int landCells = 0;
    for (std::uint8_t biome : plan.biomes) {
        if (IsDryLand(biome)) {
            ++landCells;
        }
        if (IsWater(biome)) {
            ++result.waterCells;
        }
        if (IsRoadLike(biome)) {
            ++result.roadCells;
        }
        if (IsWalkable(plan.type, biome)) {
            ++result.walkableCells;
        }
        if (IsFloor(plan.type, biome)) {
            ++floorCells;
        }
        if (biome == ROAD) {
            ++corridorCells;
        }
        if (biome == BRIDGE || (plan.type == "cave" && biome == LADDER)) {
            ++result.doorCells;
        }
    }

    // Measure semantic regions separately from the gameplay walkable component.
    // This catches fragmented islands, water pockets, and broken road networks.
    const auto measureComponents = [&](auto belongs) {
        std::vector<std::uint8_t> regionVisited(static_cast<std::size_t>(total), 0);
        int components = 0;
        int largest = 0;
        for (int start = 0; start < total; ++start) {
            if (regionVisited[static_cast<std::size_t>(start)] != 0 ||
                !belongs(start)) {
                continue;
            }
            ++components;
            int componentSize = 0;
            std::queue<int> pending;
            pending.push(start);
            regionVisited[static_cast<std::size_t>(start)] = 1;
            while (!pending.empty()) {
                const int current = pending.front();
                pending.pop();
                ++componentSize;
                const int cx = current % plan.width;
                const int cy = current / plan.width;
                for (int direction = 0; direction < 4; ++direction) {
                    const int nx = cx + dx[static_cast<std::size_t>(direction)];
                    const int ny = cy + dy[static_cast<std::size_t>(direction)];
                    if (!InBounds(nx, ny, plan.width, plan.depth)) {
                        continue;
                    }
                    const int next = CellIndex(nx, ny, plan.width);
                    if (regionVisited[static_cast<std::size_t>(next)] == 0 &&
                        belongs(next)) {
                        regionVisited[static_cast<std::size_t>(next)] = 1;
                        pending.push(next);
                    }
                }
            }
            largest = std::max(largest, componentSize);
        }
        return std::pair<int, int>{components, largest};
    };

    const auto dryLandRegions = measureComponents([&](int index) {
        return IsDryLand(plan.biomes[static_cast<std::size_t>(index)]);
    });
    result.dryLandRegions = dryLandRegions.first;
    result.largestDryLandRegion = dryLandRegions.second;
    const auto waterRegions = measureComponents([&](int index) {
        return IsWater(plan.biomes[static_cast<std::size_t>(index)]);
    });
    result.waterComponents = waterRegions.first;

    std::vector<std::uint8_t> roadNetworkMask(static_cast<std::size_t>(total), 0);
    std::vector<std::uint8_t> bridgeVisited(static_cast<std::size_t>(total), 0);
    int roadNetworkCells = 0;
    for (int index = 0; index < total; ++index) {
        if (plan.biomes[static_cast<std::size_t>(index)] == ROAD) {
            roadNetworkMask[static_cast<std::size_t>(index)] = 1;
            ++roadNetworkCells;
        }
    }
    for (int start = 0; start < total; ++start) {
        if (bridgeVisited[static_cast<std::size_t>(start)] != 0 ||
            plan.biomes[static_cast<std::size_t>(start)] != BRIDGE) continue;
        bool touchesRoad = false;
        std::vector<int> component;
        std::queue<int> pending;
        pending.push(start);
        bridgeVisited[static_cast<std::size_t>(start)] = 1;
        while (!pending.empty()) {
            const int current = pending.front();
            pending.pop();
            component.push_back(current);
            const int cx = current % plan.width;
            const int cy = current / plan.width;
            for (int direction = 0; direction < 4; ++direction) {
                const int nx = cx + dx[static_cast<std::size_t>(direction)];
                const int ny = cy + dy[static_cast<std::size_t>(direction)];
                if (!InBounds(nx, ny, plan.width, plan.depth)) continue;
                const int next = CellIndex(nx, ny, plan.width);
                const std::uint8_t biome = plan.biomes[static_cast<std::size_t>(next)];
                if (biome == ROAD) touchesRoad = true;
                if (biome != BRIDGE || bridgeVisited[static_cast<std::size_t>(next)] != 0) continue;
                bridgeVisited[static_cast<std::size_t>(next)] = 1;
                pending.push(next);
            }
        }
        if (!touchesRoad) continue;
        for (int index : component) roadNetworkMask[static_cast<std::size_t>(index)] = 1;
        roadNetworkCells += static_cast<int>(component.size());
    }
    const auto roadRegions = measureComponents([&](int index) {
        return roadNetworkMask[static_cast<std::size_t>(index)] != 0;
    });
    result.roadComponents = roadRegions.first;
    result.largestRoadComponent = roadRegions.second;

    for (int y = 0; y < plan.depth; ++y) {
        for (int x = 0; x < plan.width; ++x) {
            const int current = CellIndex(x, y, plan.width);
            if (!IsRoadLike(plan.biomes[static_cast<std::size_t>(current)])) {
                continue;
            }
            int roadNeighbors = 0;
            for (int direction = 0; direction < 4; ++direction) {
                const int nx = x + dx[static_cast<std::size_t>(direction)];
                const int ny = y + dy[static_cast<std::size_t>(direction)];
                if (InBounds(nx, ny, plan.width, plan.depth) &&
                    IsRoadLike(plan.biomes[static_cast<std::size_t>(CellIndex(nx, ny, plan.width))])) {
                    ++roadNeighbors;
                }
            }
            if (roadNeighbors == 1) {
                ++result.roadEndpointCells;
            } else if (roadNeighbors >= 3) {
                ++result.roadJunctionCells;
            }
        }
    }

    result.dryLandCells = landCells;

    for (int y = 0; y < plan.depth; ++y) {
        for (int x = 0; x < plan.width; ++x) {
            const int start = CellIndex(x, y, plan.width);
            if (visited[static_cast<std::size_t>(start)] != 0 ||
                !IsWalkable(plan.type, plan.biomes[static_cast<std::size_t>(start)])) {
                continue;
            }
            ++result.walkableComponents;
            int componentSize = 0;
            std::queue<int> pending;
            pending.push(start);
            visited[static_cast<std::size_t>(start)] = 1;
            while (!pending.empty()) {
                const int current = pending.front();
                pending.pop();
                ++componentSize;
                const int cx = current % plan.width;
                const int cy = current / plan.width;
                for (int direction = 0; direction < 4; ++direction) {
                    const int nx = cx + dx[static_cast<std::size_t>(direction)];
                    const int ny = cy + dy[static_cast<std::size_t>(direction)];
                    if (!InBounds(nx, ny, plan.width, plan.depth)) {
                        continue;
                    }
                    const int next = CellIndex(nx, ny, plan.width);
                    if (visited[static_cast<std::size_t>(next)] != 0 ||
                        !IsWalkable(plan.type, plan.biomes[static_cast<std::size_t>(next)])) {
                        continue;
                    }
                    visited[static_cast<std::size_t>(next)] = 1;
                    pending.push(next);
                }
            }
            result.largestWalkableComponent = std::max(result.largestWalkableComponent, componentSize);
        }
    }

    for (int y = 0; y < plan.depth; ++y) {
        for (int x = 0; x < plan.width; ++x) {
            const int current = CellIndex(x, y, plan.width);
            if (!IsWalkable(plan.type, plan.biomes[static_cast<std::size_t>(current)])) {
                continue;
            }
            int neighbors = 0;
            bool horizontal = false;
            bool vertical = false;
            for (int direction = 0; direction < 4; ++direction) {
                const int nx = x + dx[static_cast<std::size_t>(direction)];
                const int ny = y + dy[static_cast<std::size_t>(direction)];
                if (InBounds(nx, ny, plan.width, plan.depth) &&
                    IsWalkable(plan.type, plan.biomes[static_cast<std::size_t>(CellIndex(nx, ny, plan.width))])) {
                    ++neighbors;
                    if (direction < 2) {
                        horizontal = true;
                    } else {
                        vertical = true;
                    }
                }
            }
            if (neighbors == 1) {
                ++result.deadEndCells;
            }
            if (neighbors == 2 && horizontal != vertical) {
                ++result.oneCellCorridorCells;
            }
        }
    }

    result.walkableRatio = static_cast<double>(result.walkableCells) / static_cast<double>(std::max(1, total));
    result.landRatio = static_cast<double>(landCells) / static_cast<double>(std::max(1, total));
    result.largestWalkableComponentRatio = static_cast<double>(result.largestWalkableComponent) /
        static_cast<double>(std::max(1, result.walkableCells));
    result.floorRatio = static_cast<double>(floorCells) / static_cast<double>(std::max(1, total));
    result.corridorRatio = static_cast<double>(corridorCells) / static_cast<double>(std::max(1, total));
    result.deadEndRatio = static_cast<double>(result.deadEndCells) /
        static_cast<double>(std::max(1, result.walkableCells));
    result.dryLandRatio = static_cast<double>(result.dryLandCells) / static_cast<double>(std::max(1, total));
    result.largestDryLandRegionRatio = static_cast<double>(result.largestDryLandRegion) /
        static_cast<double>(std::max(1, result.dryLandCells));
    result.waterRatio = static_cast<double>(result.waterCells) / static_cast<double>(std::max(1, total));
    result.roadRatio = static_cast<double>(result.roadCells) / static_cast<double>(std::max(1, total));
    result.largestRoadComponentRatio = static_cast<double>(result.largestRoadComponent) /
        static_cast<double>(std::max(1, roadNetworkCells));

    const auto regionContains = [](const MapPlanRegion& region, int x, int y) {
        return x >= region.x0 && x <= region.x1 && y >= region.y0 && y <= region.y1;
    };
    const auto regionArea = [&](const MapPlanRegion& region) {
        const int x0 = std::max(0, region.x0);
        const int y0 = std::max(0, region.y0);
        const int x1 = std::min(plan.width - 1, region.x1);
        const int y1 = std::min(plan.depth - 1, region.y1);
        if (x0 > x1 || y0 > y1) return 0;
        return (x1 - x0 + 1) * (y1 - y0 + 1);
    };
    const auto regionBiomeCount = [&](const MapPlanRegion& region, std::uint8_t biomeA, std::uint8_t biomeB) {
        int count = 0;
        const int x0 = std::max(0, region.x0);
        const int y0 = std::max(0, region.y0);
        const int x1 = std::min(plan.width - 1, region.x1);
        const int y1 = std::min(plan.depth - 1, region.y1);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const std::uint8_t biome = plan.biomes[static_cast<std::size_t>(CellIndex(x, y, plan.width))];
                if (biome == biomeA || biome == biomeB) ++count;
            }
        }
        return count;
    };

    result.markerCount = static_cast<int>(plan.markers.size());
    if (plan.type == "town" || plan.type == "village" || plan.type == "castle") {
        for (const MapPlanMarker& marker : plan.markers) {
            const bool requiresBridge = marker.kind == "entrance" || marker.kind == "gate" ||
                marker.kind == "water_crossing" || marker.kind == "building_door" ||
                marker.kind == "room_door" || marker.kind == "service_entry" || marker.kind == "stairs";
            if (!requiresBridge || !InBounds(marker.x, marker.y, plan.width, plan.depth) ||
                plan.biomes[static_cast<std::size_t>(CellIndex(marker.x, marker.y, plan.width))] != BRIDGE) {
                if (requiresBridge) ++result.invalidSemanticMarkers;
            }
        }
    }
    if (plan.type == "town" || plan.type == "village") {
        std::set<std::string> buildingKinds;
        int developmentCells = 0;
        int developmentRoadCells = 0;
        for (int index = 0; index < total; ++index) {
            const std::uint8_t biome = plan.biomes[static_cast<std::size_t>(index)];
            if (biome == TOWN_PAVEMENT) ++result.townPavedCells;
            if (plan.settlementMask.size() == plan.biomes.size() &&
                plan.settlementMask[static_cast<std::size_t>(index)] != 0) {
                ++developmentCells;
                if (IsRoadLike(biome)) ++developmentRoadCells;
            }
        }
        for (const MapPlanRegion& region : plan.regions) {
            if (region.kind == "building" || region.kind == "market") {
                ++result.townBuildingCount;
                result.townBuildingCells += regionArea(region);
                if (!region.role.empty()) buildingKinds.insert(region.role);
            } else if (region.kind == "district") {
                ++result.townDistrictCount;
            }
            if (region.kind == "road" && (region.width < 2 || region.width > 6)) ++result.townInvalidRoadWidths;
            if (region.kind == "plaza") result.townPlazaCells += regionArea(region);
            if (region.kind == "park") result.townParkCells += regionArea(region);
            if (region.kind == "waterway") {
                result.townWaterFeatureCells += regionBiomeCount(region, RIVER, LAKE);
                result.townWaterFeatureCells += regionBiomeCount(region, TOWN_SEA, TOWN_SEA);
            }
        }
        result.townBuildingKindCount = static_cast<int>(buildingKinds.size());
        for (const MapPlanMarker& marker : plan.markers) {
            if (marker.kind == "landmark") ++result.townLandmarkCount;
            if (marker.kind == "entrance") ++result.townEntranceCount;
            if (marker.kind == "building_door") ++result.townBuildingDoorCount;
            if (marker.role == "town_center") ++result.townCenterMarkerCount;
        }
        for (const MapPlanMarker& marker : plan.markers) {
            if (marker.kind != "entrance") continue;
            bool hasOutside = false;
            bool hasInside = false;
            for (int direction = 0; direction < 4; ++direction) {
                const int nx = marker.x + dx[static_cast<std::size_t>(direction)];
                const int ny = marker.y + dy[static_cast<std::size_t>(direction)];
                if (!InBounds(nx, ny, plan.width, plan.depth)) {
                    hasOutside = true;
                    continue;
                }
                const std::uint8_t biome = plan.biomes[static_cast<std::size_t>(CellIndex(nx, ny, plan.width))];
                const int i = CellIndex(nx, ny, plan.width);
                if (plan.settlementMask.size() == plan.biomes.size()) {
                    if (!plan.settlementMask[i] && IsRoadLike(biome)) hasOutside = true;
                    if (plan.settlementMask[i] && IsWalkable(plan.type, biome)) hasInside = true;
                } else {
                    if (biome == SEA) hasOutside = true;
                    if (IsWalkable(plan.type, biome) && biome != SEA) hasInside = true;
                }
            }
            if (!hasOutside || !hasInside) ++result.townInvalidEntrances;
        }
        for (const MapPlanMarker& marker : plan.markers) {
            if (marker.kind != "building_door") continue;
            const MapPlanRegion* building = nullptr;
            for (const MapPlanRegion& region : plan.regions) {
                if ((region.kind == "building" || region.kind == "market") && region.id == marker.id) {
                    building = &region;
                    break;
                }
            }
            bool hasInteriorNeighbor = false;
            bool hasExteriorNeighbor = false;
            if (building != nullptr) {
                for (int direction = 0; direction < 4; ++direction) {
                    const int nx = marker.x + dx[static_cast<std::size_t>(direction)];
                    const int ny = marker.y + dy[static_cast<std::size_t>(direction)];
                    if (!InBounds(nx, ny, plan.width, plan.depth)) continue;
                    const bool inside = regionContains(*building, nx, ny);
                    if (inside) {
                        hasInteriorNeighbor = true;
                    } else if (IsWalkable(plan.type, plan.biomes[static_cast<std::size_t>(CellIndex(nx, ny, plan.width))])) {
                        hasExteriorNeighbor = true;
                    }
                }
            }
            if (building == nullptr || !hasInteriorNeighbor || !hasExteriorNeighbor) ++result.townInvalidBuildingDoors;
        }
        result.townPlazaRatio = static_cast<double>(result.townPlazaCells) / static_cast<double>(std::max(1, total));
        result.townRoadRatio = plan.settlementMask.size() == plan.biomes.size()
            ? static_cast<double>(developmentRoadCells) / static_cast<double>(std::max(1, developmentCells))
            : result.roadRatio;
        result.townBuildingRatio = static_cast<double>(result.townBuildingCells) / static_cast<double>(std::max(1, total));
        result.townPavingRatio = plan.pavingTargetRatio > 0.0 || plan.pavingActualRatio > 0.0
            ? plan.pavingActualRatio
            : static_cast<double>(result.townPavedCells) / static_cast<double>(std::max(1, total));
        result.townPavingTargetDelta = std::abs(result.townPavingRatio - plan.pavingTargetRatio);
        result.townParkWaterRatio = static_cast<double>(result.townParkCells + result.townWaterFeatureCells) /
            static_cast<double>(std::max(1, total));
    }
    if (plan.type == "castle") {
        MapPlanRegion fortress;
        MapPlanRegion moat;
        MapPlanRegion bailey;
        MapPlanRegion keep;
        bool hasFortress = false;
        bool hasMoat = false;
        bool hasBailey = false;
        bool hasKeep = false;
        std::vector<MapPlanRegion> occupiedRegions;
        std::set<std::string> facilityRoles;
        std::set<std::string> roomRoles;
        const auto program = GetCastleProgram(plan.archetype);
        std::vector<std::uint8_t> keepFootprint(total, 0);
        bool hasInnerWard = false;
        int throneRoomCount = 0;
        int entranceHallCount = 0;
        for (const MapPlanRegion& region : plan.regions) {
            if (region.kind == "tower") {
                ++result.castleTowerCount;
                if (region.role.find("watch_tower") != std::string::npos) ++result.castleWatchTowerCount;
            }
            if (region.kind == "room") ++result.castleRoomCount;
            if (region.kind == "facility" || region.kind == "hall") ++result.castleFacilityCount;
            if (region.kind == "gatehouse") ++result.castleGatehouseCount;
            if (region.kind == "garden") result.castleGardenCells += regionArea(region);
            if (region.kind == "training_ground") result.castleTrainingGroundCells += regionArea(region);
            if (region.kind == "fortress") { fortress = region; hasFortress = true; }
            if (region.kind == "moat") { moat = region; hasMoat = true; }
            if (region.kind == "outer_bailey") { bailey = region; hasBailey = true; }
            if (region.kind == "keep") { keep = region; hasKeep = true; }
            if (region.kind == "inner_ward") hasInnerWard = true;
            if (region.kind == "facility") facilityRoles.insert(region.role);
            if (region.kind == "furniture") ++result.castleEquipmentCount;
            if (region.kind == "room") {
                roomRoles.insert(region.role);
                if (region.role == "throne_room") ++throneRoomCount;
                if (region.role == "entrance_hall") {
                    ++entranceHallCount;
                    result.castleHallCells += std::max(0, region.x1 - region.x0 - 1) * std::max(0, region.y1 - region.y0 - 1);
                }
                for (int y = std::max(0, region.y0 - 2); y <= std::min(plan.depth - 1, region.y1 + 2); ++y)
                    for (int x = std::max(0, region.x0 - 2); x <= std::min(plan.width - 1, region.x1 + 2); ++x) keepFootprint[CellIndex(x,y,plan.width)] = 1;
            }
            if ((region.kind == "room" || region.kind == "facility") && region.role.find("kitchen") != std::string::npos)
                result.castleKitchenCells += std::max(0, region.x1 - region.x0 - 1) * std::max(0, region.y1 - region.y0 - 1);
            if (region.kind == "tower" || region.kind == "room" || region.kind == "facility" || region.kind == "hall" || region.kind == "keep" ||
                region.kind == "keep_expansion" || region.kind == "street_tree" ||
                region.kind == "training_ground" || region.kind == "service_court" || region.kind == "garden" || region.kind == "cistern") {
                occupiedRegions.push_back(region);
            }
        }
        if (plan.castleDefenseMask.size() == plan.biomes.size()) {
            for (int cell = 0; cell < total; ++cell)
                if (plan.biomes[static_cast<std::size_t>(cell)] == CASTLE_WALL_WALK &&
                    plan.castleDefenseMask[static_cast<std::size_t>(cell)] == 2) ++result.castleWallWalkCells;
        }
        result.castleFacilityRoleCount = static_cast<int>(facilityRoles.size());
        if (throneRoomCount != 1) ++result.castleProgramMissingRoles;
        if (entranceHallCount != 1) ++result.castleProgramMissingRoles;
        for (const auto& role : {program.chambers[0], program.chambers[1]})
            if (!roomRoles.count(role)) ++result.castleProgramMissingRoles;
        for (int i = 0; i < 4; ++i) if (!facilityRoles.count(program.facilities[i].role)) ++result.castleProgramMissingRoles;
        for (int i = 0; i < total; ++i) result.castleKeepFootprintCells += keepFootprint[i];
        if (hasFortress) for (int y = fortress.y0; y <= fortress.y1; ++y) for (int x = fortress.x0; x <= fortress.x1; ++x) {
            if (InBounds(x,y,plan.width,plan.depth) && !IsWater(plan.biomes[CellIndex(x,y,plan.width)])) ++result.castleFootprintCells;
        }
        for (const MapPlanMarker& marker : plan.markers) {
            if (marker.kind == "gate") {
                ++result.castleGateCount;
                if (marker.role == "main_gate") ++result.castleMainGateCount;
                if (marker.role == "postern_gate") ++result.castlePosternGateCount;
            }
            if (marker.kind == "service_entry" && marker.role == "inner_ward_entry") ++result.castleInnerGateCount;
        }
        for (const MapPlanMarker& marker : plan.markers) {
            if (marker.kind == "water_crossing") {
                bool adjacentWater = false;
                for (int direction = 0; direction < 4; ++direction) {
                    const int nx = marker.x + dx[static_cast<std::size_t>(direction)];
                    const int ny = marker.y + dy[static_cast<std::size_t>(direction)];
                    if (InBounds(nx, ny, plan.width, plan.depth) &&
                        IsWater(plan.biomes[static_cast<std::size_t>(CellIndex(nx, ny, plan.width))])) adjacentWater = true;
                }
                if (!adjacentWater) ++result.castleInvalidWaterCrossings;
            }
            if (marker.kind != "gate" && marker.kind != "entrance") continue;
            bool adjacentWalkable = false;
            bool adjacentWater = false;
            for (int direction = 0; direction < 4; ++direction) {
                const int nx = marker.x + dx[static_cast<std::size_t>(direction)];
                const int ny = marker.y + dy[static_cast<std::size_t>(direction)];
                if (!InBounds(nx, ny, plan.width, plan.depth)) continue;
                const std::uint8_t biome = plan.biomes[static_cast<std::size_t>(CellIndex(nx, ny, plan.width))];
                adjacentWalkable = adjacentWalkable || IsWalkable(plan.type, biome);
                adjacentWater = adjacentWater || IsWater(biome);
            }
            if (marker.kind == "entrance" && plan.outerWallDefenseStrength >= 1.5 && !adjacentWater) {
                for (int oy = -3; oy <= 3 && !adjacentWater; ++oy) {
                    for (int ox = -3; ox <= 3; ++ox) {
                        if (std::abs(ox) + std::abs(oy) > 3) continue;
                        const int nx = marker.x + ox;
                        const int ny = marker.y + oy;
                        if (InBounds(nx, ny, plan.width, plan.depth) &&
                            IsWater(plan.biomes[static_cast<std::size_t>(CellIndex(nx, ny, plan.width))])) {
                            adjacentWater = true;
                            break;
                        }
                    }
                }
            }
            if (!adjacentWalkable || (marker.kind == "entrance" && plan.outerWallDefenseStrength >= 1.5 && !adjacentWater)) {
                ++result.castleInvalidGateMarkers;
            }
        }
        for (const MapPlanMarker& marker : plan.markers) {
            const bool facilityDoor = marker.kind == "service_entry" && marker.id >= 100;
            if (marker.kind != "room_door" && !facilityDoor) continue;
            const MapPlanRegion* room = nullptr;
            for (const MapPlanRegion& region : plan.regions) {
                if (region.kind == (facilityDoor ? "facility" : "room") && region.id == marker.id) {
                    room = &region;
                    break;
                }
            }
            bool hasInteriorNeighbor = false;
            bool hasExteriorNeighbor = false;
            if (room != nullptr) {
                for (int direction = 0; direction < 4; ++direction) {
                    const int nx = marker.x + dx[static_cast<std::size_t>(direction)];
                    const int ny = marker.y + dy[static_cast<std::size_t>(direction)];
                    if (!InBounds(nx, ny, plan.width, plan.depth)) continue;
                    const bool inside = regionContains(*room, nx, ny);
                    if (inside && IsWalkable(plan.type, plan.biomes[static_cast<std::size_t>(CellIndex(nx, ny, plan.width))])) {
                        hasInteriorNeighbor = true;
                    } else if (!inside && IsWalkable(plan.type, plan.biomes[static_cast<std::size_t>(CellIndex(nx, ny, plan.width))])) {
                        hasExteriorNeighbor = true;
                    }
                }
            }
            if (room == nullptr || !hasInteriorNeighbor || !hasExteriorNeighbor) {
                if (facilityDoor) ++result.castleInvalidFacilityDoors;
                else ++result.castleInvalidRoomDoors;
            }
        }
        if (plan.castleDefenseMask.size() == plan.biomes.size()) {
            int water = 0;
            for (int i = 0; i < total; ++i) {
                const auto intent = plan.castleDefenseMask[i];
                const auto tile = plan.biomes[i];
                if (intent == 1) {
                    ++result.castleMoatCells;
                    if (tile == RIVER || tile == LAKE) ++water;
                    else ++result.castleMoatGapCells;
                }
                if ((intent == 2 || intent == 3) && tile != MOUNTAIN && tile != EXMOUNTAIN &&
                    !(intent == 2 && tile == CASTLE_WALL_WALK)) ++result.castleWallGapCells;
                if (intent >= 4 && intent <= 6 && tile != BRIDGE) ++result.castleInvalidGateMarkers;
            }
            result.castleMoatCoverage = static_cast<double>(water) / std::max(1, result.castleMoatCells);
            // Close the designed gates and attempt to reach the keep from
            // outside that defensive layer. A decorative ring must not pass
            // merely because its individual marker tiles look correct.
            for (int layer = 0; layer < (hasInnerWard ? 2 : 1); ++layer) {
                std::vector<std::uint8_t> seen(total, 0);
                std::queue<int> frontier;
                for (const auto& marker : plan.markers) {
                    if (marker.kind != (layer == 0 ? "entrance" : "gate") ||
                        !InBounds(marker.x, marker.y, plan.width, plan.depth)) continue;
                    const int i = CellIndex(marker.x, marker.y, plan.width);
                    if (!IsWalkable(plan.type, plan.biomes[i]) || seen[i] ||
                        plan.castleDefenseMask[i] == (layer == 0 ? 5 : 6)) continue;
                    frontier.push(i); seen[i] = 1;
                }
                while (!frontier.empty()) {
                    const int i = frontier.front(); frontier.pop();
                    for (int direction = 0; direction < 4; ++direction) {
                        const int x = i % plan.width + dx[direction], y = i / plan.width + dy[direction];
                        if (!InBounds(x, y, plan.width, plan.depth)) continue;
                        const int next = CellIndex(x, y, plan.width);
                        if (seen[next] || plan.castleDefenseMask[next] == (layer == 0 ? 5 : 6) ||
                            !IsWalkable(plan.type, plan.biomes[next])) continue;
                        seen[next] = 1; frontier.push(next);
                    }
                }
                bool bypass = false;
                if (hasKeep) for (int y = keep.y0 + 1; y < keep.y1; ++y) for (int x = keep.x0 + 1; x < keep.x1; ++x) {
                    if (InBounds(x, y, plan.width, plan.depth) && seen[CellIndex(x, y, plan.width)]) bypass = true;
                }
                if (bypass) ++result.castleDefenseBypassCount;
            }
        } else if (hasMoat && hasFortress) {
            const auto isTowerCell = [&](int x, int y) {
                for (const MapPlanRegion& region : plan.regions) {
                    if (region.kind == "tower" && x >= region.x0 && x <= region.x1 && y >= region.y0 && y <= region.y1) return true;
                }
                return false;
            };
            int moatArea = 0;
            int moatWater = 0;
            for (int y = moat.y0; y <= moat.y1; ++y) {
                for (int x = moat.x0; x <= moat.x1; ++x) {
                    if (!InBounds(x, y, plan.width, plan.depth) ||
                        (x >= fortress.x0 && x <= fortress.x1 && y >= fortress.y0 && y <= fortress.y1)) continue;
                    if (isTowerCell(x, y)) continue;
                    const std::uint8_t biome = plan.biomes[static_cast<std::size_t>(CellIndex(x, y, plan.width))];
                    if (biome == RIVER || biome == LAKE || biome == BRIDGE) ++moatArea;
                    if (biome == RIVER || biome == LAKE) ++moatWater;
                }
            }
            result.castleMoatCells = moatArea;
            result.castleMoatGapCells = 0;
            for (int y = moat.y0; y <= moat.y1; ++y) {
                for (int x = moat.x0; x <= moat.x1; ++x) {
                    if (!InBounds(x, y, plan.width, plan.depth) ||
                        (x >= fortress.x0 && x <= fortress.x1 && y >= fortress.y0 && y <= fortress.y1)) continue;
                    if (isTowerCell(x, y)) continue;
                    const std::uint8_t biome = plan.biomes[static_cast<std::size_t>(CellIndex(x, y, plan.width))];
                    if (biome != RIVER && biome != LAKE && biome != BRIDGE) ++result.castleMoatGapCells;
                }
            }
            result.castleMoatCoverage = static_cast<double>(moatWater) / static_cast<double>(std::max(1, moatArea - result.castleMoatGapCells));
        }
        if (hasFortress && plan.castleDefenseMask.size() != plan.biomes.size()) {
            for (int x = fortress.x0; x <= fortress.x1; ++x) {
                for (int y : {fortress.y0, fortress.y1}) {
                    if (!InBounds(x, y, plan.width, plan.depth)) continue;
                    const std::uint8_t biome = plan.biomes[static_cast<std::size_t>(CellIndex(x, y, plan.width))];
                    if (biome != MOUNTAIN && biome != EXMOUNTAIN && biome != BRIDGE) ++result.castleWallGapCells;
                }
            }
            for (int y = fortress.y0 + 1; y < fortress.y1; ++y) {
                for (int x : {fortress.x0, fortress.x1}) {
                    if (!InBounds(x, y, plan.width, plan.depth)) continue;
                    const std::uint8_t biome = plan.biomes[static_cast<std::size_t>(CellIndex(x, y, plan.width))];
                    if (biome != MOUNTAIN && biome != EXMOUNTAIN && biome != BRIDGE) ++result.castleWallGapCells;
                }
            }
        }
        if (hasBailey) {
            // Empty ground is measured by distance from actual uses, not its
            // colour. Painting a checkerboard or setting pavingRatio=1 must
            // not turn an unused bailey into a high-quality working courtyard.
            std::vector<int> useDistance(total, total);
            std::queue<int> uses;
            auto addUse = [&](int x, int y) {
                if (!InBounds(x, y, plan.width, plan.depth)) return;
                const int i = CellIndex(x, y, plan.width);
                if (useDistance[i] == 0) return;
                useDistance[i] = 0; uses.push(i);
            };
            for (const auto& region : occupiedRegions) {
                for (int y = region.y0; y <= region.y1; ++y) for (int x = region.x0; x <= region.x1; ++x) addUse(x, y);
            }
            for (int i = 0; i < total; ++i) if (IsRoadLike(plan.biomes[i])) addUse(i % plan.width, i / plan.width);
            const int useRadius = std::max(4, std::min(plan.width, plan.depth) / 15);
            while (!uses.empty()) {
                const int i = uses.front(); uses.pop();
                if (useDistance[i] >= useRadius) continue;
                for (int direction = 0; direction < 4; ++direction) {
                    const int x = i % plan.width + dx[direction], y = i / plan.width + dy[direction];
                    if (!InBounds(x, y, plan.width, plan.depth)) continue;
                    const int next = CellIndex(x, y, plan.width);
                    if (useDistance[next] <= useDistance[i] + 1 || !IsWalkable(plan.type, plan.biomes[next])) continue;
                    useDistance[next] = useDistance[i] + 1; uses.push(next);
                }
            }
            for (int y = bailey.y0; y <= bailey.y1; ++y) {
                for (int x = bailey.x0; x <= bailey.x1; ++x) {
                    if (!InBounds(x, y, plan.width, plan.depth) || !IsWalkable(plan.type, plan.biomes[static_cast<std::size_t>(CellIndex(x, y, plan.width))])) continue;
                    ++result.castleCourtyardCells;
                    bool occupied = false;
                    for (const MapPlanRegion& region : occupiedRegions) {
                        if (x >= region.x0 && x <= region.x1 && y >= region.y0 && y <= region.y1) {
                            occupied = true;
                            break;
                        }
                    }
                    if (!occupied && useDistance[CellIndex(x, y, plan.width)] > useRadius) ++result.castleCourtyardEmptyCells;
                }
            }
            result.castleCourtyardEmptyRatio = static_cast<double>(result.castleCourtyardEmptyCells) /
                static_cast<double>(std::max(1, result.castleCourtyardCells));
        }
        if (hasKeep) {
            const double offsetX = static_cast<double>((keep.x0 + keep.x1) / 2 - plan.width / 2);
            const double offsetY = static_cast<double>((keep.y0 + keep.y1) / 2 - plan.depth / 2);
            result.castleKeepCenterOffset = std::sqrt(offsetX * offsetX + offsetY * offsetY) /
                static_cast<double>(std::max(1, std::min(plan.width, plan.depth)));
        }
    }
    if (result.walkableCells > 0) {
        std::vector<std::uint8_t> reachable(static_cast<std::size_t>(total), 0);
        int firstWalkable = -1;
        for (int i = 0; i < total; ++i) {
            if (IsWalkable(plan.type, plan.biomes[static_cast<std::size_t>(i)])) {
                firstWalkable = i;
                break;
            }
        }
        // A full-canvas settlement may include an uninhabited island in the
        // first corner. Judge landmark access from the settlement's nucleus.
        if (plan.type == "town" || plan.type == "village") {
            for (const auto& marker : plan.markers) {
                if (marker.role != "town_center" || !InBounds(marker.x, marker.y, plan.width, plan.depth)) continue;
                const int i = CellIndex(marker.x, marker.y, plan.width);
                if (IsWalkable(plan.type, plan.biomes[i])) { firstWalkable = i; break; }
            }
        }
        if (plan.type == "castle") {
            for (const auto& marker : plan.markers) {
                if (marker.kind != "entrance" || !InBounds(marker.x, marker.y, plan.width, plan.depth)) continue;
                const int i = CellIndex(marker.x, marker.y, plan.width);
                if (IsWalkable(plan.type, plan.biomes[i])) { firstWalkable = i; break; }
            }
        }
        if (firstWalkable >= 0) {
            std::queue<int> pending;
            pending.push(firstWalkable);
            reachable[static_cast<std::size_t>(firstWalkable)] = 1;
            while (!pending.empty()) {
            const int current = pending.front();
            pending.pop();
            const int cx = current % plan.width;
            const int cy = current / plan.width;
            for (int direction = 0; direction < 4; ++direction) {
                const int nx = cx + dx[static_cast<std::size_t>(direction)];
                const int ny = cy + dy[static_cast<std::size_t>(direction)];
                if (!InBounds(nx, ny, plan.width, plan.depth)) {
                    continue;
                }
                const int next = CellIndex(nx, ny, plan.width);
                if (reachable[static_cast<std::size_t>(next)] == 0 &&
                    IsWalkable(plan.type, plan.biomes[static_cast<std::size_t>(next)])) {
                    reachable[static_cast<std::size_t>(next)] = 1;
                    pending.push(next);
                }
            }
            }
        }
        for (const MapPlanMarker& marker : plan.markers) {
            if (InBounds(marker.x, marker.y, plan.width, plan.depth) &&
                reachable[static_cast<std::size_t>(CellIndex(marker.x, marker.y, plan.width))] != 0) {
                ++result.reachableMarkers;
                if ((plan.type == "town" || plan.type == "village") && marker.kind == "building_door") {
                    ++result.townReachableBuildingDoors;
                }
            }
        }
        if (plan.type == "castle") {
            // Count distinct reachable interiors, not door markers (a room
            // with several doors must not hide an inaccessible chamber).
            for (const auto& region : plan.regions) {
                if (region.kind != "room" && region.kind != "facility") continue;
                bool hasReachableFloor = false;
                for (int y = region.y0 + 1; y < region.y1 && !hasReachableFloor; ++y) {
                    for (int x = region.x0 + 1; x < region.x1; ++x) {
                        if (InBounds(x, y, plan.width, plan.depth) && reachable[CellIndex(x, y, plan.width)]) {
                            hasReachableFloor = true; break;
                        }
                    }
                }
                if (hasReachableFloor && region.kind == "room") ++result.castleReachableRooms;
                if (hasReachableFloor && region.kind == "facility") ++result.castleReachableFacilities;
            }
        }
        if ((plan.type == "town" || plan.type == "village") && plan.settlementMask.size() == plan.biomes.size()) {
            for (int i = 0; i < total; ++i) {
                if (!plan.settlementMask[i] || !IsWalkable(plan.type, plan.biomes[i])) continue;
                ++result.townDevelopedWalkableCells;
                if (reachable[i]) ++result.townReachableDevelopedCells;
            }
            result.townDevelopmentReachableRatio = static_cast<double>(result.townReachableDevelopedCells) /
                std::max(1, result.townDevelopedWalkableCells);
        }
    }

    auto addIssue = [&result](const std::string& message) {
        result.issues.push_back(message);
    };
    if (result.walkableCells == 0) {
        addIssue("no walkable cells");
    }
    if (plan.type == "dungeon" && result.largestWalkableComponentRatio < 0.95) {
        addIssue("walkable area is split into disconnected components");
    }
    if (plan.type == "world" && result.largestWalkableComponentRatio < 0.65) {
        addIssue("largest passable component is below 65% of passable cells");
    }
    if (plan.type == "world" && (result.landRatio < 0.35 || result.landRatio > 0.70)) {
        addIssue("world land ratio is outside [35%, 70%]");
    }
    if (plan.type == "cave") {
        if (result.largestWalkableComponentRatio < 0.95) {
            addIssue("cave floor is not at least 95% connected");
        }
        if (result.floorRatio < 0.30 || result.floorRatio > 0.55) {
            addIssue("cave floor ratio is outside [30%, 55%]");
        }
    }
    if (plan.type == "dungeon") {
        if (result.floorRatio < 0.28 || result.floorRatio > 0.55) {
            addIssue("dungeon room floor ratio is outside [28%, 55%]");
        }
        if (result.corridorRatio < 0.12 || result.corridorRatio > 0.32) {
            addIssue("dungeon corridor ratio is outside [12%, 32%]");
        }
        if (result.doorCells == 0) {
            addIssue("dungeon has no doors");
        }
    }
    if ((plan.type == "town" || plan.type == "village" || plan.type == "castle") && result.markerCount > 0 &&
        result.reachableMarkers != result.markerCount) {
        addIssue("one or more entrances or landmarks are unreachable");
    }
    if ((plan.type == "town" || plan.type == "village" || plan.type == "castle") && result.invalidSemanticMarkers > 0) {
        addIssue("one or more semantic markers do not match their biome tile");
    }
    if (plan.type == "town" || plan.type == "village") {
        const bool village = plan.type == "village";
        // Full-canvas nature may contain uninhabited land across a river/lake.
        // The 95% gate still applies to developed land, from the town centre;
        // all markers and the complete road network remain mandatory too.
        if (plan.settlementMask.size() != plan.biomes.size() ||
            std::any_of(plan.settlementMask.begin(), plan.settlementMask.end(), [](auto value) { return value > 1; }))
            addIssue("settlement development mask is missing or invalid");
        if (result.townDevelopmentReachableRatio < 0.95) addIssue("developed settlement land is not at least 95% connected to its centre");
        if (std::find(plan.biomes.begin(), plan.biomes.end(), SEA) != plan.biomes.end()) addIssue("settlement has blank background cells");
        int mapExits = 0;
        for (const auto& marker : plan.markers) {
            if (marker.kind != "map_exit") continue;
            ++mapExits;
            if (!InBounds(marker.x, marker.y, plan.width, plan.depth) ||
                (marker.x != 0 && marker.y != 0 && marker.x != plan.width - 1 && marker.y != plan.depth - 1) ||
                !IsRoadLike(plan.biomes[CellIndex(marker.x, marker.y, plan.width)])) addIssue("map exit is not on a road at the image edge");
        }
        if (mapExits < 2) addIssue("settlement needs at least two map-edge road exits");
        if (result.townBuildingCount < 4) addIssue(village ? "village has fewer than four buildings" : "town has fewer than four buildings");
        if (result.townDistrictCount < 2) addIssue(village ? "village has fewer than two districts" : "town has fewer than two districts");
        if (result.townEntranceCount < 2) addIssue(village ? "village has fewer than two entrances" : "town has fewer than two entrances");
        if (result.townLandmarkCount < 2) addIssue(village ? "village has fewer than two landmark facilities" : "town has fewer than two landmark facilities");
        if (result.townCenterMarkerCount == 0) addIssue(village ? "village has no town centre" : "town has no town centre");
        if (plan.plazaEnabled && result.townPlazaCells == 0) addIssue("plan declares a plaza but has no plaza region");
        if (result.townBuildingDoorCount == 0) addIssue(village ? "village has no building doors" : "town has no building doors");
        if (result.townBuildingDoorCount != result.townReachableBuildingDoors) addIssue("one or more building doors are unreachable");
        if (result.townInvalidBuildingDoors > 0) addIssue("one or more building doors have invalid geometry");
        if (result.townInvalidEntrances > 0) addIssue(village ? "one or more village entrances are not connected to both outside and inside" : "one or more town entrances are not connected to both outside and inside");
        if (result.townBuildingRatio < 0.01 || result.townBuildingRatio > 0.40) addIssue(village ? "village building area ratio is outside [1%, 40%]" : "town building area ratio is outside [1%, 40%]");
        if (result.townBuildingKindCount < (village ? 2 : 3)) addIssue(village ? "village has too little building role variety" : "town has too little building role variety");
        if (result.townInvalidRoadWidths > 0) addIssue("one or more town road regions have invalid widths");
        if (plan.pavingTargetRatio > 0.05 && result.townPavingRatio < plan.pavingTargetRatio * 0.55) addIssue("town paving coverage is substantially below its target");
        if (result.roadCells > 0 && result.roadComponents != 1) addIssue(village ? "village road network is split into disconnected components" : "town road network is split into disconnected components");
        if (result.townRoadRatio < (village ? 0.025 : 0.04) || result.townRoadRatio > 0.35) addIssue(village ? "village road ratio is outside [2.5%, 35%]" : "town road ratio is outside [4%, 35%]");
    }
    if (plan.type == "castle") {
        const bool hasOuterWall = plan.outerWallDefenseStrength >= 0.5;
        const bool hasMoat = plan.outerWallDefenseStrength >= 1.5;
        const bool hasOuterDefenseEquipment = plan.outerWallDefenseStrength > 1.1;
        if (hasOuterWall) {
            if (result.castleGateCount < 1 || result.castleGateCount > 3) addIssue("castle gate count is outside [1, 3]");
            if (result.castleMainGateCount != 1 || result.castlePosternGateCount != result.castleGateCount - 1)
                addIssue("castle main gate and postern hierarchy is inconsistent");
        } else if (result.castleGateCount != 0 || result.castleGatehouseCount != 0) {
            addIssue("open castle unexpectedly has an outer gate or gatehouse");
        }
        if (hasOuterDefenseEquipment) {
            if (result.castleWallWalkCells == 0) addIssue("fortified castle curtain wall has no walkable wall-top patrol route");
            if (std::min(plan.width, plan.depth) >= 96 && result.castleWatchTowerCount == 0)
                addIssue("high outer-wall defense has no watch tower");
            if (result.castleTowerCount < 4 || result.castleTowerCount > 12) addIssue("fortified castle tower count is outside [4, 12]");
            if (result.castleGatehouseCount != result.castleGateCount) addIssue("castle gatehouse count does not match gate count");
        } else {
            if (result.castleWallWalkCells != 0 || result.castleTowerCount != 0 || result.castleGatehouseCount != 0)
                addIssue("low outer-wall defense unexpectedly has fortified equipment");
        }
        if (result.castleRoomCount < 4 || result.castleRoomCount > 32) addIssue("castle room count is outside [4, 32]");
        if (result.castleFacilityCount < 4 || result.castleFacilityCount > 32) addIssue("castle facility count is outside [4, 32]");
        if (result.castleReachableRooms != result.castleRoomCount) addIssue("one or more castle rooms are unreachable");
        if (result.castleReachableFacilities < result.castleFacilityCount) addIssue("one or more castle facilities are unreachable");
        if (result.castleInvalidRoomDoors > 0) addIssue("one or more castle room doors have invalid geometry");
        if (result.castleInvalidFacilityDoors > 0) addIssue("one or more castle facility doors have invalid geometry");
        if (result.castleFacilityRoleCount < 4) addIssue("castle lacks distinct military and supply facilities");
        if (result.castleProgramMissingRoles > 0) addIssue("castle is missing rooms or facilities required by its architectural program");
        if (result.castleDefenseBypassCount > 0) addIssue("castle keep can be reached while a defensive gate is closed");
        if (result.castleInvalidGateMarkers > 0) addIssue("one or more castle gate markers have invalid moat or walkable adjacency");
        if (result.castleInvalidWaterCrossings > 0) addIssue("one or more castle water crossings are not adjacent to water");
        if (!hasMoat && result.castleMoatCells != 0) addIssue("low outer-wall defense unexpectedly has a moat");
        if (hasMoat && result.castleMoatCells == 0) addIssue("outer-wall defense requires a moat but none was generated");
        if (result.castleMoatGapCells > 0) addIssue("castle moat has unintended gaps");
        if (result.castleMoatCells > 0 && result.castleMoatCoverage < 0.95) addIssue("castle moat water coverage is below 95%");
        if (result.castleWallGapCells > 0) addIssue("castle curtain or inner wall has unintended openings");
        if (result.castleKeepCenterOffset > 0.35) addIssue("castle keep is too far from the centre");
        if (result.castleCourtyardEmptyRatio > 0.45) addIssue("castle courtyard has excessive empty space");
        // BRIDGE is also used for room doors in the compatibility biome set;
        // tiny bridge-only components are semantic doors, not broken roads.
        if (result.roadCells > 0 && result.largestRoadComponentRatio < 0.95) addIssue("castle road network is split into disconnected components");
    }

    return result;
}
