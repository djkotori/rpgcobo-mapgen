#include "CastleMapImageGenerator.h"
#include "CastleProgram.h"
#include "MapQuality.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

MapPlan Generate(int size, int mode, unsigned seed) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"lotFacilities", 0}, {"outerWallDefenseStrength", 2}};
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    Require(generator.Generate(plan, error), std::to_string(size) + " mode=" + std::to_string(mode) +
        " seed=" + std::to_string(seed) + ": " + error);
    return plan;
}

void TestInteriorOnlyMatrix() {
    int cases = 0;
    for (int size : {128, 160}) for (int mode = 0; mode < 5; ++mode) for (unsigned seed : {1U, 42U, 4242U}) {
        const auto plan = Generate(size, mode, seed);
        const std::string context = std::to_string(size) + " mode=" + std::to_string(mode) +
            " seed=" + std::to_string(seed);
        Require(plan.type == "castle", context + ": generator changed map type");
        Require(plan.outerWallDefenseStrength == 0.0 && plan.castleDefenseLevel == 0,
            context + ": legacy exterior-defense settings affected castle metadata");
        Require(plan.cityType.empty() && plan.settlementSite.empty() && plan.settlementMask.empty(),
            context + ": castle output contains settlement identity or mask");
        Require(plan.castleGroundMask.size() == plan.biomes.size(), context + ": neutral castle ground mask missing");
        Require(plan.castleDefenseMask.size() == plan.biomes.size(), context + ": defensive intent mask missing");
        Require(std::none_of(plan.castleDefenseMask.begin(), plan.castleDefenseMask.end(),
            [](std::uint8_t cell) { return cell != 0; }), context + ": outer defense mask is not empty");

        int rooms = 0, facilities = 0, courts = 0, keepCells = 0;
        std::vector<std::uint8_t> core(static_cast<std::size_t>(size * size), 0);
        const auto program = GetCastleProgram(plan.archetype);
        for (const auto& region : plan.regions) {
            Require(region.kind != "moat" && region.kind != "curtain_wall" && region.kind != "gatehouse" &&
                region.kind != "tower" && region.kind != "wall_walk" && region.kind != "fortress" &&
                region.kind != "outer_bailey" && region.kind != "town_footprint" &&
                region.kind != "inner_ward",
                context + ": exterior defense or settlement region remains: " + region.kind);
            Require(region.kind.find("town_") != 0, context + ": castle town region remains: " + region.kind);
            if (region.kind == "keep") {
                keepCells = (region.x1 - region.x0 + 1) * (region.y1 - region.y0 + 1);
            }
            if (region.kind == "room") ++rooms;
            if (region.kind == "facility") ++facilities;
            if (region.role == program.court0 || region.role == program.court1) ++courts;
            if (region.kind == "keep" || region.kind == "keep_expansion" || region.kind == "inner_ward" ||
                region.kind == "facility" || region.kind == "service_court" || region.kind == "garden" ||
                region.kind == "cistern" || region.kind == "keep_court" || region.kind == "court") {
                for (int y = region.y0; y <= region.y1; ++y) for (int x = region.x0; x <= region.x1; ++x)
                    if (x >= 0 && y >= 0 && x < size && y < size) core[static_cast<std::size_t>(y * size + x)] = 1;
            }
        }
        Require(rooms >= 4 && courts == 0,
            context + ": keep rooms were lost or an outdoor court appeared (rooms=" + std::to_string(rooms) +
                ", courts=" + std::to_string(courts) + ")");
        // The throne room has generous floor in front of the throne, and an
        // antechamber (waiting room) sits between it and the entrance hall.
        const MapPlanRegion* throne = nullptr; const MapPlanRegion* ante = nullptr; const MapPlanRegion* hall = nullptr;
        int anteCount = 0, keepHeight = 0;
        for (const auto& region : plan.regions) {
            if (region.kind == "keep") keepHeight = region.y1 - region.y0 + 1;
            if (region.kind != "room") continue;
            if (region.role == "throne_room") throne = &region;
            if (region.role == "antechamber") { ante = &region; ++anteCount; }
            if (region.role == "entrance_hall") hall = &region;
        }
        // Nothing on the axis is certain: the throne room, the entrance hall and the antechamber may
        // each be left out. What is there must be in order, and the antechamber needs a throne room.
        Require(anteCount <= 1 && (anteCount == 0 || throne != nullptr), context + ": more than one antechamber, or one without a throne room");
        if (ante != nullptr && throne != nullptr && hall != nullptr) {
            Require(ante->y0 >= throne->y1 - 1 && hall->y0 >= ante->y1 - 1 &&
                ante->x0 < throne->x1 && ante->x1 > throne->x0 && ante->x0 < hall->x1 && ante->x1 > hall->x0,
                context + ": the antechamber is not between the throne room and the entrance hall");
        }
        if (ante != nullptr) Require(ante->y1 - ante->y0 + 1 >= (size < 112 ? 5 : 8), context + ": the antechamber is too shallow to wait in");
        if (throne != nullptr && size >= 112) {
            Require((throne->y1 - throne->y0 + 1) * 100 >= keepHeight * 28,
                context + ": the throne room lacks room in front of the throne (" +
                    std::to_string(throne->y1 - throne->y0 + 1) + " of " + std::to_string(keepHeight) + ")");
        }
        // Doors form a chain hall -> antechamber -> throne room: each of those
        // two rooms is entered through a door of its own.
        bool anteDoor = false, throneDoor = false;
        for (const auto& marker : plan.markers) {
            if (marker.kind != "room_door") continue;
            if (marker.role == "antechamber_door") anteDoor = true;
            if (marker.role == "throne_room_door") throneDoor = true;
        }
        Require((ante == nullptr || anteDoor) && (throne == nullptr || throneDoor), context + ": the antechamber or the throne room has no door");
        // The private suite (when there is one) is a handful of rooms: a bedchamber, a dressing
        // room, a study, a queen's bedchamber (one to four in all).
        int suiteRooms = 0;
        for (const auto& region : plan.regions) {
            if (region.kind != "room") continue;
            if (region.role != "royal_bedchamber" && region.role != "royal_dressing_room" && region.role != "royal_study" &&
                region.role != "queens_bedchamber") continue;
            ++suiteRooms;
        }
        Require(suiteRooms <= 4, context + ": too many private rooms");
        if (throne != nullptr) for (const auto& marker : plan.markers) {
            if (marker.kind != "room_door") continue;
            if (marker.role != "royal_bedchamber_door" && marker.role != "royal_study_door" && marker.role != "queens_bedchamber_door") continue;
            // Doors from the throne room open beside the dais, never directly behind it.
            if (marker.y >= throne->y0 - 1 && marker.y <= throne->y0 + 1)
                Require(std::abs(marker.x - (throne->x0 + throne->x1) / 2) >= 3,
                    context + ": a private door opens directly behind the throne");
        }
        // Walls between rooms are a single cell thick: no 2x2 block of wall tiles.
        for (const auto& region : plan.regions) {
            if (region.kind != "keep") continue;
            // Only cells next to a room count: a notch in the silhouette shows the
            // surrounding terrain (mountains for a hill castle), which is not a wall.
            std::vector<std::uint8_t> nearRoom(static_cast<std::size_t>(size * size), 0);
            for (const auto& room : plan.regions) {
                if (room.kind != "room") continue;
                for (int y = std::max(0, room.y0 - 1); y <= std::min(size - 1, room.y1 + 1); ++y)
                    for (int x = std::max(0, room.x0 - 1); x <= std::min(size - 1, room.x1 + 1); ++x)
                        nearRoom[static_cast<std::size_t>(y * size + x)] = 1;
            }
            const auto isWall = [&](int x, int y) {
                const std::uint8_t b = plan.biomes[static_cast<std::size_t>(y * size + x)];
                return nearRoom[static_cast<std::size_t>(y * size + x)] && (b == MOUNTAIN || b == EXMOUNTAIN);
            };
            for (int y = region.y0; y < region.y1; ++y) for (int x = region.x0; x < region.x1; ++x)
                Require(!(isWall(x, y) && isWall(x + 1, y) && isWall(x, y + 1) && isWall(x + 1, y + 1)),
                    context + ": a wall is thicker than one cell at " + std::to_string(x) + "," + std::to_string(y));
        }
        // The main gate is large: at least 7 cells wide, centred on the avenue, and open.
        const MapPlanRegion* gateRegion = nullptr;
        for (const auto& region : plan.regions)
            if (region.kind == "castle_front_door" && region.role == "great_castle_gate") gateRegion = &region;
        Require(gateRegion != nullptr, context + ": the main gate is missing");
        Require(gateRegion->x1 - gateRegion->x0 + 1 >= 7, context + ": the main gate is too small");
        for (int x = gateRegion->x0; x <= gateRegion->x1; ++x)
            for (int y = gateRegion->y0; y <= gateRegion->y1; ++y) {
                const std::uint8_t b = plan.biomes[static_cast<std::size_t>(y * size + x)];
                Require(b != MOUNTAIN && b != EXMOUNTAIN, context + ": the main gate is blocked by a wall");
            }
        const int coreCells = static_cast<int>(std::count(core.begin(), core.end(), 1));
        const double coverage = static_cast<double>(coreCells) / static_cast<double>(size * size);
        const double keepShare = static_cast<double>(keepCells) / static_cast<double>(size * size);
        std::cout << "  size=" << size << " mode=" << mode << " seed=" << seed << " coverage=" << coverage
                  << " keep=" << keepShare << " facilities=" << facilities << '\n';
        if (size >= 112) {
            // The castle itself, not an enclosure or a town around it, fills most of the map.
            Require(keepShare >= 0.35, context + ": keep occupies too little of the map");
            Require(facilities <= 6, context + ": too many detached annexes (town-like sprawl): " + std::to_string(facilities));
        }
        for (const auto& stage : plan.castleHistory) {
            Require(stage.change != "commercial_and_garrison_annexes" && stage.phase != "economic_expansion",
                context + ": castle history still records a town-like annex expansion");
        }
        Require(coverage >= (size < 112 ? 0.30 : 0.45) && coverage <= 0.95,
            context + ": semantic castle core does not occupy enough of the map");

        for (const auto& marker : plan.markers) {
            Require(marker.role != "inner_ward_entry", context + ": inner ward gate remains");
            Require(marker.kind != "gate" && marker.kind != "castle_gate" && marker.kind != "entrance" &&
                marker.kind != "wall_access" && marker.kind != "water_crossing",
                context + ": exterior defense marker remains: " + marker.kind);
        }
        // Outside the castle body only the main avenue may exist: no houses,
        // yards, service buildings or side streets.
        Require(facilities == 0, context + ": detached facilities were generated outside the castle");
        std::vector<std::uint8_t> body(static_cast<std::size_t>(size * size), 0);
        for (const auto& region : plan.regions) {
            Require(region.kind != "service_court" && region.kind != "training_ground" && region.kind != "court_access",
                context + ": outdoor yard or side street region remains: " + region.kind);
            Require(region.kind != "garden" && region.kind != "park" && region.kind != "cistern",
                context + ": castle-unrelated park or pond remains: " + region.kind);
            if (region.kind != "keep" && region.kind != "keep_expansion") continue;
            for (int y = region.y0 - 1; y <= region.y1 + 1; ++y) for (int x = region.x0 - 1; x <= region.x1 + 1; ++x)
                if (x >= 0 && y >= 0 && x < size && y < size) body[static_cast<std::size_t>(y * size + x)] = 1;
        }
        int roadMinX = size, roadMaxX = -1, roadCells = 0;
        for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
            const std::size_t i = static_cast<std::size_t>(y * size + x);
            if (body[i]) continue;
            const std::uint8_t biome = plan.biomes[i];
            Require(biome != TOWN_WOOD_FLOOR && biome != TOWN_STONE_FLOOR && biome != TOWN_FURNITURE,
                context + ": building floor outside the castle at " + std::to_string(x) + "," + std::to_string(y));
            if (biome == ROAD || biome == BRIDGE || biome == TOWN_PAVEMENT) {
                roadMinX = std::min(roadMinX, x); roadMaxX = std::max(roadMaxX, x); ++roadCells;
            }
        }
        Require(roadCells > 0, context + ": no main avenue leaves the castle");
        // Every road cell outside the castle belongs to one connected avenue.
        std::vector<std::uint8_t> seen(static_cast<std::size_t>(size * size), 0);
        int avenueParts = 0;
        const auto isRoad = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= size || y >= size) return false;
            const std::size_t i = static_cast<std::size_t>(y * size + x);
            const std::uint8_t biome = plan.biomes[i];
            return !body[i] && (biome == ROAD || biome == BRIDGE || biome == TOWN_PAVEMENT);
        };
        for (int sy = 0; sy < size; ++sy) for (int sx = 0; sx < size; ++sx) {
            if (!isRoad(sx, sy) || seen[static_cast<std::size_t>(sy * size + sx)]) continue;
            ++avenueParts;
            std::vector<std::pair<int, int>> stack{{sx, sy}};
            seen[static_cast<std::size_t>(sy * size + sx)] = 1;
            while (!stack.empty()) {
                const auto [cx, cy] = stack.back(); stack.pop_back();
                for (const auto& step : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                    const int nx = cx + step.first, ny = cy + step.second;
                    if (!isRoad(nx, ny) || seen[static_cast<std::size_t>(ny * size + nx)]) continue;
                    seen[static_cast<std::size_t>(ny * size + nx)] = 1;
                    stack.push_back({nx, ny});
                }
            }
        }
        Require(avenueParts == 1, context + ": roads outside the castle are not a single avenue (" +
            std::to_string(avenueParts) + " parts, x " + std::to_string(roadMinX) + ".." + std::to_string(roadMaxX) + ")");
        // The avenue is a straight line from the keep's public door to the
        // map edge: it never bends, so it stays on the door's centre axis.
        int doorX = -1, doorY = -1, exits = 0;
        for (const auto& marker : plan.markers)
            if (marker.role == "keep_public_entry") { doorX = marker.x; doorY = marker.y; }
        for (const auto& marker : plan.markers) {
            if (marker.kind == "map_exit") {
                ++exits;
                Require(marker.y == size - 1 && marker.x == doorX && marker.role == "open_castle_approach",
                    context + ": map exit is not at the foot of the straight avenue");
            }
        }
        Require(doorX >= 0 && exits == 1, context + ": missing keep door or map exit");
        Require(roadMinX >= doorX - 2 && roadMaxX <= doorX + 2,
            context + ": the avenue bends away from the door axis (x " + std::to_string(roadMinX) + ".." +
                std::to_string(roadMaxX) + ", door x " + std::to_string(doorX) + ")");
        for (int y = doorY + 1; y < size; ++y) {
            const std::uint8_t biome = plan.biomes[static_cast<std::size_t>(y * size + doorX)];
            Require(biome == ROAD || biome == BRIDGE || biome == TOWN_PAVEMENT,
                context + ": the avenue is interrupted at row " + std::to_string(y));
        }
        bool avenueReachesEdge = false;
        for (int x = 0; x < size; ++x) {
            const std::uint8_t biome = plan.biomes[static_cast<std::size_t>((size - 1) * size + x)];
            avenueReachesEdge = avenueReachesEdge || biome == ROAD || biome == BRIDGE || biome == TOWN_PAVEMENT;
        }
        Require(avenueReachesEdge, context + ": main avenue does not reach the map edge");
        const auto quality = EvaluateMapQuality(plan);
        std::string issues;
        for (const auto& issue : quality.issues) issues += " [" + issue + "]";
        Require(quality.Passed(), context + ": castle failed map quality validation:" + issues);
        Require(quality.castleReachableRooms == quality.castleRoomCount &&
            quality.castleReachableFacilities == quality.castleFacilityCount,
            context + ": a room or facility is unreachable");
        ++cases;
    }
    std::cout << "Interior-only castle matrix: " << cases << " cases\n";
}
}

// The map editor always sends the exterior-oriented sliders (facility count and density,
// road width, extra road connections, paving, tower and gate counts). A castle has no
// castle town, so none of them may bring detached buildings, courts or paving back.
void TestEditorSlidersDoNotBuildATown() {
    for (const int size : {128, 160}) for (int mode = 0; mode < 5; ++mode) for (unsigned seed : {12345U, 7U}) {
        MapGenParams params;
        params.width = params.depth = size;
        params.seed = seed;
        params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"lotFacilities", 0}, {"wallHeight", 6}, {"outerFacilityCountMin", 6},
            {"outerFacilityCountMax", 16}, {"outerFacilityDensity", 3}, {"mainRoadWidth", 5}, {"roadExtraEdgeRate", 0.55},
            {"pavingRatio", 0.85}, {"outerWallDefenseStrength", 2}, {"towerCountMin", 4}, {"towerCountMax", 6},
            {"gateCountMin", 2}, {"gateCountMax", 3}, {"moatWidthMin", 2}, {"moatWidthMax", 5}, {"candidateCount", 6},
            {"wealth", seed == 7U ? 2.5 : 1.0}};
        CastleMapImageGenerator generator(params);
        MapPlan plan;
        std::string error;
        const std::string context = std::to_string(size) + " mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
        Require(generator.Generate(plan, error), context + ": " + error);
        for (const auto& region : plan.regions)
            Require(region.kind != "facility" && region.kind != "service_court" && region.kind != "garden" &&
                region.kind != "cistern" && region.kind != "court" && region.kind != "moat" && region.kind != "tower" &&
                region.kind != "furniture",
                context + ": the editor sliders brought back an outside region: " + region.kind);
        for (const std::uint8_t biome : plan.biomes)
            Require(biome != TOWN_PAVEMENT && biome != TOWN_FURNITURE, context + ": paving or the room floor pattern (furniture) was output");
    }
}

int main() {
    try {
        TestInteriorOnlyMatrix();
        TestEditorSlidersDoNotBuildATown();
        std::cout << "CASTLE_INTERIOR_ONLY: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CASTLE_INTERIOR_ONLY: FAIL " << error.what() << '\n';
        return 1;
    }
}
