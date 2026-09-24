#include "CastleMapImageGenerator.h"
#include "CastleProgram.h"
#include "MapQuality.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

MapPlan Generate(MapGenParams params) {
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    const std::string context = std::to_string(params.width) + "x" + std::to_string(params.depth) +
        " seed=" + std::to_string(params.seed) + " mode=" + std::to_string(params.extra.count("castleMode") ? params.extra.at("castleMode") : -1);
    Require(generator.Generate(plan, error), context + ": " + error);
    const auto quality = EvaluateMapQuality(plan);
    std::string issues;
    for (const auto& issue : quality.issues) issues += issue + "; ";
    Require(quality.Passed(), context + ": " + issues);
    Require(plan.castleDefenseMask.size() == plan.biomes.size(), "missing defensive construction intent");
    Require(quality.castleReachableRooms == quality.castleRoomCount, "not every chamber is reachable");
    Require(quality.castleReachableFacilities == quality.castleFacilityCount, "not every facility is reachable");
    Require(quality.castleDefenseBypassCount == 0, "a closed gate can be bypassed");
    Require(quality.castleInvalidFacilityDoors == 0, "facility door has no interior/exterior connection");
    Require(std::find(plan.biomes.begin(), plan.biomes.end(), SEA) == plan.biomes.end(), "castle retains black background cells");
    int wallWalkCells = 0;
    int wallAccessCount = 0;
    for (std::size_t cell = 0; cell < plan.biomes.size(); ++cell) {
        if (plan.biomes[cell] != CASTLE_WALL_WALK) continue;
        ++wallWalkCells;
        Require(plan.castleDefenseMask[cell] == 2, "wall walk is beside the curtain instead of on top of it");
    }
    for (const auto& marker : plan.markers) {
        if (marker.kind != "wall_access") continue;
        ++wallAccessCount;
        Require(plan.biomes[static_cast<std::size_t>(marker.y * plan.width + marker.x)] == CASTLE_WALL_WALK,
            "wall access is not on the wall-top walkway");
    }
    Require(wallWalkCells == quality.castleWallWalkCells, "wall-top patrol metric is inconsistent");
    if (plan.outerWallDefenseStrength > 1.1) {
        Require(wallWalkCells > 0 && wallAccessCount > 0,
            "fortified outer wall has no patrol route or gatehouse access");
    } else {
        Require(wallWalkCells == 0 && wallAccessCount == 0,
            "low outer-wall defense unexpectedly has patrol equipment");
    }
    std::set<std::string> roles;
    int throneCount = 0, entranceCount = 0;
    const MapPlanRegion* keep = nullptr;
    const MapPlanRegion* throne = nullptr;
    const MapPlanRegion* entrance = nullptr;
    for (const auto& region : plan.regions) {
        Require(region.x0 >= 0 && region.y0 >= 0 && region.x1 < plan.width && region.y1 < plan.depth,
            "region escapes canvas: " + region.kind);
        Require(!region.role.empty(), "empty semantic role");
        if (region.kind == "keep") keep = &region;
        if (region.kind == "moat") {
            Require(region.x0 == 0 && region.y0 == 0 && region.x1 == plan.width - 1 && region.y1 == plan.depth - 1,
                "castle moat does not use the full canvas");
        }
        if (region.kind == "room" && region.role == "throne_room") { ++throneCount; throne = &region; }
        if (region.kind == "room" && region.role == "entrance_hall") { ++entranceCount; entrance = &region; }
        if (region.kind == "facility") roles.insert(region.role);
        if (region.kind != "room" && region.kind != "facility" && region.kind != "tower") continue;
        for (int y = region.y0 + 1; y < region.y1; ++y) for (int x = region.x0 + 1; x < region.x1; ++x) {
            const auto tile = plan.biomes[y * plan.width + x];
            Require(tile != TOWN_PAVEMENT && tile != FOREST && tile != LAKE, "outdoor decoration entered a building");
        }
    }
    Require(throneCount == 1 && entranceCount == 1 && keep, "the keep needs exactly one throne and entry hall");
    Require(throne->y1 < entrance->y0 && throne->x0 == entrance->x0 && throne->x1 == entrance->x1,
        "public rooms are not arranged along the defended axis");
    if (plan.archetype != "river") {
        Require(std::abs((throne->x0 + throne->x1) - (keep->x0 + keep->x1)) <= 2, "the public spine is off centre");
    }
    const CastleProgram program = GetCastleProgram(plan.archetype);
    for (int facilityIndex = 0; facilityIndex < 4; ++facilityIndex) {
        const std::string& required = program.facilities[static_cast<std::size_t>(facilityIndex)].role;
        Require(roles.count(required) != 0, "missing essential role: " + required);
    }
    return plan;
}

void TestMatrix() {
    int samples = 0;
    for (int size : {64, 96, 128, 192, 320}) for (int mode = -1; mode < 5; ++mode) for (unsigned seed = 1; seed <= 4; ++seed) {
        MapGenParams params;
        params.width = params.depth = size;
        params.seed = seed;
        if (mode >= 0) params.extra["castleMode"] = mode;
        const auto plan = Generate(params);
        if (size >= 192) {
            const auto quality = EvaluateMapQuality(plan);
            const CastleProgram program = GetCastleProgram(plan.archetype);
            const std::string caseContext = std::to_string(size) + "x" + std::to_string(size) +
                " seed=" + std::to_string(seed) + " mode=" + std::to_string(mode) +
                " archetype=" + plan.archetype;
            if (mode >= 0) {
                const std::array<std::uint8_t, 5> exteriorBiomes{{PLAIN, MOUNTAIN, RIVER, FOREST, DESERT}};
                const std::array<int, 4> corners{{0, plan.width - 1,
                    (plan.depth - 1) * plan.width, plan.depth * plan.width - 1}};
                for (int corner : corners) {
                    Require(plan.biomes[static_cast<std::size_t>(corner)] == exteriorBiomes[static_cast<std::size_t>(mode)] &&
                        plan.castleDefenseMask[static_cast<std::size_t>(corner)] == 0,
                        caseContext + ": cutback exterior does not use the archetype biome");
                }
            }
            Require(quality.castleInnerGateCount == 1, caseContext + ": a full castle must have a single controlled inner gate");
            Require(std::count_if(plan.regions.begin(), plan.regions.end(), [&](const auto& region) {
                return region.role == program.court0 || region.role == program.court1;
            }) == 2, "working courts were lost to facility placement");
        }
        if (seed == 1) {
            const auto again = Generate(params);
            Require(plan.biomes == again.biomes && plan.castleDefenseMask == again.castleDefenseMask &&
                plan.qualityScore == again.qualityScore, "same parameters are not deterministic");
        }
        ++samples;
    }
    std::cout << "Matrix: " << samples << " cases plus 30 deterministic repeats\n";
}

void TestConstraintsAndDiversity() {
    {
        MapGenParams params;
        params.width = params.depth = 192;
        params.seed = 4242;
        params.extra = {{"castleMode", 1}, {"outerWallDefenseStrength", 0.8}};
        const auto plan = Generate(params);
        std::vector<int> leftWallByRow;
        for (int y = 0; y < plan.depth / 2; ++y) {
            int leftWall = plan.width;
            for (int x = 0; x < plan.width / 2; ++x) {
                if (plan.castleDefenseMask[static_cast<std::size_t>(y * plan.width + x)] == 2) {
                    leftWall = x;
                    break;
                }
            }
            if (leftWall < plan.width) leftWallByRow.push_back(leftWall);
        }
        const int verticalWall = *std::min_element(leftWallByRow.begin(), leftWallByRow.end());
        std::set<int> diagonalOffsets;
        int y = 0;
        for (int leftWall : leftWallByRow) {
            if (leftWall > verticalWall + 2) diagonalOffsets.insert(leftWall + y);
            ++y;
        }
        Require(diagonalOffsets.size() >= 3, "castle cutback contour lost its natural variation");
    }
    for (int mode = 0; mode < 5; ++mode) {
        std::set<std::vector<std::uint8_t>> shapes;
        for (unsigned seed = 1; seed <= 8; ++seed) {
            MapGenParams params;
            params.width = 192; params.depth = 256; params.seed = seed;
            params.extra["castleMode"] = mode;
            auto plan = Generate(params);
            for (auto& cell : plan.castleDefenseMask) {
                // Ignore gate position and the inner ward; compare the real
                // outer masonry/moat geometry within one archetype.
                if (cell == 4) cell = 1;
                if (cell == 5) cell = 2;
                if (cell >= 3) cell = 0;
            }
            shapes.insert(std::move(plan.castleDefenseMask));
        }
        Require(shapes.size() >= 6, "seeds repeat the same outer castle shape");
    }
    for (int mode = 0; mode < 5; ++mode) for (int roomCount : {4, 5, 8, 17, 32}) {
        MapGenParams params;
        params.width = 320; params.depth = 256; params.seed = 4242;
        params.extra = {{"castleMode", mode}, {"towerCountMin", 12}, {"towerCountMax", 12},
            {"keepRoomCountMin", roomCount}, {"keepRoomCountMax", roomCount},
            {"outerFacilityCountMin", 6}, {"outerFacilityCountMax", 6},
            {"gateCountMin", 1}, {"gateCountMax", 1}, {"outerWallDefenseStrength", 2.0}};
        const auto plan = Generate(params);
        const auto quality = EvaluateMapQuality(plan);
        Require(quality.castleRoomCount == roomCount && quality.castleTowerCount == 12 &&
            quality.castleFacilityCount == 6 && quality.castleGateCount == 1, "explicit count was overridden");
    }
    for (const auto& dimensions : {std::pair<int, int>{96,128}, {512,384}, {768,512}, {1024,1024}}) {
        MapGenParams params;
        params.width = dimensions.first; params.depth = dimensions.second; params.seed = 4242;
        Generate(params);
    }
    MapGenParams compact;
    compact.width = compact.depth = 64; compact.seed = 4242;
    compact.extra = {{"pavingRatio", 0}, {"wallThickness", 1}, {"moatWidthMin", 2}, {"moatWidthMax", 2},
        {"gateCountMin", 3}, {"gateCountMax", 3}, {"keepWidth", 16}, {"keepHeight", 16},
        {"outerWallDefenseStrength", 2.0}};
    const auto compactPlan = Generate(compact);
    Require(std::find(compactPlan.biomes.begin(), compactPlan.biomes.end(), TOWN_PAVEMENT) == compactPlan.biomes.end(),
        "explicit zero paving was ignored");
    compact.width = compact.depth = 192;
    compact.extra = {{"wallThickness", 3}, {"moatWidthMin", 5}, {"moatWidthMax", 5}, {"pavingRatio", 1}};
    Generate(compact);
    std::cout << "Diversity: 40 rectangular cases; explicit constraints: 25 cases\n";
}

void TestWealthDevelopment() {
    MapGenParams poorParams;
    poorParams.width = poorParams.depth = 320;
    poorParams.seed = 8128;
    poorParams.extra = {{"castleMode", 0}, {"wealth", 0.7}, {"outerWallDefenseStrength", 1.8}};
    MapGenParams richParams = poorParams;
    richParams.extra["wealth"] = 2.5;
    const auto poor = Generate(poorParams);
    const auto rich = Generate(richParams);
    const auto poorQuality = EvaluateMapQuality(poor);
    const auto richQuality = EvaluateMapQuality(rich);
    Require(std::abs(poor.countryWealth - 0.7) < 0.0001 && poor.wealthClass == "strained", "poor wealth override was not preserved");
    Require(std::abs(rich.countryWealth - 2.5) < 0.0001 && rich.wealthClass == "opulent", "rich wealth override was not preserved");
    Require(richQuality.castleRoomCount > poorQuality.castleRoomCount, "wealth did not expand the keep");
    Require(richQuality.castleFacilityCount > poorQuality.castleFacilityCount, "wealth did not add bailey facilities");
    Require(rich.outerWallDefenseStrength == poor.outerWallDefenseStrength &&
        richQuality.castleTowerCount >= 4 && poorQuality.castleTowerCount >= 4,
        "wealth changed the independent outer-defense profile");
    Require(richQuality.castleEquipmentCount > poorQuality.castleEquipmentCount, "wealth did not increase furnishing density");
    Require(rich.pavingTargetRatio > poor.pavingTargetRatio, "wealth did not increase paving investment");
    Require(rich.castleHistory.size() > poor.castleHistory.size(), "wealth did not produce additional construction phases");
    Require(rich.castleDefenseLevel >= poor.castleDefenseLevel && !rich.castleWallForm.empty(), "castle defense profile is missing");
    Require(richQuality.castleWallWalkCells > 0 && richQuality.castleWatchTowerCount > 0,
        "defensive wall equipment is missing from quality metrics");
    Require(richQuality.castleMainGateCount == 1 && richQuality.castlePosternGateCount >= 1,
        "gate hierarchy is missing from quality metrics");
    const auto hasRegionRole = [](const MapPlan& plan, const std::string& kind, const std::string& rolePart) {
        return std::any_of(plan.regions.begin(), plan.regions.end(), [&](const auto& region) {
            return region.kind == kind && region.role.find(rolePart) != std::string::npos;
        });
    };
    const auto markerRoleCount = [](const MapPlan& plan, const std::string& role) {
        return std::count_if(plan.markers.begin(), plan.markers.end(), [&](const auto& marker) { return marker.role == role; });
    };
    Require(hasRegionRole(poor, "wall_walk", "wall_walk") && hasRegionRole(rich, "wall_walk", "wall_walk"),
        "curtain wall has no walkable patrol route");
    Require(markerRoleCount(rich, "main_gate") == 1 && markerRoleCount(rich, "postern_gate") >= 1,
        "main avenue and minor paths do not have distinct gates");
    Require(hasRegionRole(rich, "facility", "annex") || hasRegionRole(rich, "facility", "merchant"),
        "wealthy castle has no commercial or military expansion");
    Require(std::count_if(poor.regions.begin(), poor.regions.end(), [](const auto& region) {
        return region.kind == "keep_expansion";
    }) == 0, "strained castle received a wealthy keep wing");
    Require(std::count_if(rich.regions.begin(), rich.regions.end(), [](const auto& region) {
        return region.kind == "keep_expansion";
    }) == 2, "opulent castle does not show two connected keep wings");
    for (const std::string& rolePart : {"tavern", "guild", "inn"}) {
        Require(hasRegionRole(rich, "facility", rolePart), "opulent castle is missing wealthy civic facilities");
    }
    Require(hasRegionRole(rich, "garden", "green") || hasRegionRole(rich, "garden", "garden"),
        "opulent castle is missing a castle-specific park");
    Require(hasRegionRole(rich, "street_tree", "tree"), "opulent castle is missing street trees");
    Require(richQuality.castleCourtyardEmptyRatio < poorQuality.castleCourtyardEmptyRatio,
        "wealth did not reduce unused castle grounds");
    const auto richAgain = Generate(richParams);
    Require(rich.countryWealth == richAgain.countryWealth && rich.castleHistory.size() == richAgain.castleHistory.size() &&
        rich.castleWallForm == richAgain.castleWallForm, "wealth development metadata is not deterministic");
    for (const std::uint32_t seed : {753740342U, 753811613U}) {
        MapGenParams denseParams;
        denseParams.width = denseParams.depth = 160;
        denseParams.seed = seed;
        denseParams.extra = {{"wealth", 2.5}};
        Generate(denseParams);
    }
    std::cout << "Wealth: poor/rich construction, defense, paving and furnishing differ\n";
}

void TestOuterDefenseStrength() {
    auto generateAt = [](double strength) {
        MapGenParams params;
        params.width = params.depth = 160;
        params.seed = 4242;
        params.extra = {{"castleMode", 0}, {"wealth", 2.0}, {"outerWallDefenseStrength", strength}};
        return Generate(params);
    };
    const auto open = generateAt(0.4);
    const auto wallBoundary = generateAt(0.5);
    const auto dryWall = generateAt(0.8);
    const auto ordinaryWall = generateAt(0.9);
    const auto equipmentBoundary = generateAt(1.1);
    const auto fortifiedDryWall = generateAt(1.2);
    const auto belowMoatBoundary = generateAt(1.49);
    const auto moatBoundary = generateAt(1.5);
    const auto regionCount = [](const MapPlan& plan, const std::string& kind) {
        return std::count_if(plan.regions.begin(), plan.regions.end(), [&](const auto& region) { return region.kind == kind; });
    };
    const auto openQuality = EvaluateMapQuality(open);
    const auto dryQuality = EvaluateMapQuality(dryWall);
    const auto fortifiedDryQuality = EvaluateMapQuality(fortifiedDryWall);
    const auto moatQuality = EvaluateMapQuality(moatBoundary);
    Require(open.outerWallDefenseStrength == 0.4 && regionCount(open, "moat") == 0 &&
        regionCount(open, "curtain_wall") == 0 && openQuality.castleGateCount == 0,
        "outer defense below 0.5 retained a moat, curtain wall or gate");
    Require(regionCount(dryWall, "curtain_wall") == 1 && regionCount(dryWall, "moat") == 0 &&
        dryQuality.castleGateCount > 0, "outer defense 0.5-0.89 did not produce a dry curtain wall");
    Require(regionCount(wallBoundary, "curtain_wall") == 1 && regionCount(wallBoundary, "moat") == 0,
        "outer wall threshold 0.5 is not inclusive");
    Require(regionCount(ordinaryWall, "curtain_wall") == 1 && regionCount(ordinaryWall, "moat") == 0,
        "ordinary outer defense unexpectedly produced a moat");
    Require(EvaluateMapQuality(equipmentBoundary).castleWallWalkCells == 0,
        "outer defense equipment threshold 1.1 is not exclusive");
    Require(regionCount(fortifiedDryWall, "moat") == 0 && fortifiedDryQuality.castleWallWalkCells > 0 &&
        fortifiedDryQuality.castleTowerCount >= 4 &&
        fortifiedDryQuality.castleGatehouseCount == fortifiedDryQuality.castleGateCount,
        "outer defense above 1.1 did not produce fortified dry walls");
    Require(regionCount(belowMoatBoundary, "moat") == 0,
        "outer defense below 1.5 unexpectedly produced a moat");
    Require(regionCount(moatBoundary, "moat") == 1 && moatQuality.castleMoatCells > 0 &&
        moatQuality.castleWallWalkCells > 0, "moat threshold 1.5 is not inclusive");
    Require(open.countryWealth == dryWall.countryWealth && dryWall.countryWealth == moatBoundary.countryWealth,
        "outer defense changed country wealth");
    std::cout << "Outer defense: open, dry wall, fortified dry wall and moated tiers differ\n";
}

void TestCorruptionDetection() {
    MapGenParams params;
    params.width = params.depth = 192; params.seed = 4242;
    params.extra["outerWallDefenseStrength"] = 1.8;
    const auto plan = Generate(params);
    for (int intent : {1, 2, 3}) {
        auto broken = plan;
        auto it = std::find(broken.castleDefenseMask.begin(), broken.castleDefenseMask.end(), intent);
        Require(it != broken.castleDefenseMask.end(), "missing tested defense layer");
        const auto index = static_cast<std::size_t>(it - broken.castleDefenseMask.begin());
        broken.biomes[index] = BRIDGE;
        const auto quality = EvaluateMapQuality(broken);
        Require(!quality.Passed(), "an unauthorized bridge/opening passed validation");
        Require(intent == 1 ? quality.castleMoatGapCells > 0 : quality.castleWallGapCells > 0,
            "defense corruption was not detected by its structural metric");
    }
    auto broken = plan;
    for (const auto& region : plan.regions) if (region.kind == "inner_ward") {
        const int y = (region.y0 + region.y1) / 2;
        broken.biomes[y * plan.width + region.x0] = BRIDGE;
        break;
    }
    Require(EvaluateMapQuality(broken).castleDefenseBypassCount > 0, "an actual inner-ward bypass was not detected");
    broken = plan;
    for (const auto& region : broken.regions) if (region.kind == "room") {
        for (int y = region.y0; y <= region.y1; ++y) for (int x = region.x0; x <= region.x1; ++x)
            if (broken.biomes[y * broken.width + x] == BRIDGE) broken.biomes[y * broken.width + x] = MOUNTAIN;
        break;
    }
    const auto quality = EvaluateMapQuality(broken);
    Require(!quality.Passed() && quality.castleReachableRooms < quality.castleRoomCount,
        "duplicate door counts hid a sealed chamber");
    std::cout << "Corruption: moat, outer wall, inner wall and sealed chamber detected\n";
}
}

int main() {
    try {
        TestMatrix(); TestConstraintsAndDiversity(); TestWealthDevelopment(); TestOuterDefenseStrength(); TestCorruptionDetection();
        std::cout << "CASTLE_LAYOUT: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CASTLE_LAYOUT: FAIL " << error.what() << '\n';
        return 1;
    }
}
