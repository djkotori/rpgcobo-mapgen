#include "MapImageGenerator.h"
#include "MapQuality.h"
#include "CastleMapImageGenerator.h"
#include "CaveMapImageGenerator.h"
#include "DungeonMapImageGenerator.h"
#include "TownMapImageGenerator.h"
#include "WorldMapImageGenerator.h"
#include "VillageMapImageGenerator.h"
#include "utils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct CliOptions {
    std::string type = "world";
    int width = 256;
    int depth = 256;
    bool widthProvided = false;
    bool depthProvided = false;
    std::uint32_t seed = 0;
    bool seedProvided = false;
    std::unordered_map<std::string, double> extra;
    std::filesystem::path outDir = ".";
    std::string outName = "out";
    bool writePng = true;
    bool writeData = false;
    bool writeReport = false;
    bool writePlan = false;
    bool strictQuality = false;
};

struct MapTypeSpec {
    const char* id;
    int defaultWidth;
    int defaultDepth;
    std::function<std::unique_ptr<MapImageGenerator>(MapGenParams)> create;
};

const std::vector<MapTypeSpec>& MapTypes() {
    static const std::vector<MapTypeSpec> types = {
        {"world", 256, 256, [](MapGenParams p) { return std::unique_ptr<MapImageGenerator>(std::make_unique<WorldMapImageGenerator>(std::move(p))); }},
        {"dungeon", 160, 160, [](MapGenParams p) { return std::unique_ptr<MapImageGenerator>(std::make_unique<DungeonMapImageGenerator>(std::move(p))); }},
        {"cave", 160, 160, [](MapGenParams p) { return std::unique_ptr<MapImageGenerator>(std::make_unique<CaveMapImageGenerator>(std::move(p))); }},
        {"town", 160, 160, [](MapGenParams p) { return std::unique_ptr<MapImageGenerator>(std::make_unique<TownMapImageGenerator>(std::move(p))); }},
        {"village", 160, 160, [](MapGenParams p) { return std::unique_ptr<MapImageGenerator>(std::make_unique<VillageMapImageGenerator>(std::move(p))); }},
        {"castle", 160, 160, [](MapGenParams p) { return std::unique_ptr<MapImageGenerator>(std::make_unique<CastleMapImageGenerator>(std::move(p))); }},
    };
    return types;
}

const MapTypeSpec* FindMapType(const std::string& id) {
    for (const MapTypeSpec& type : MapTypes()) {
        if (id == type.id) return &type;
    }
    return nullptr;
}

bool ParseParamKV(const std::string& token, std::string& outKey, double& outValue) {
    const std::size_t eq = token.find('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 >= token.size()) return false;
    outKey = token.substr(0, eq);
    try {
        outValue = std::stod(token.substr(eq + 1));
    } catch (...) {
        return false;
    }
    return true;
}

bool ParseArgs(int argc, char** argv, CliOptions& opt, std::string& outError) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto requireNext = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                outError = std::string("Missing value for ") + flag;
                return nullptr;
            }
            return argv[++i];
        };

        if (a == "-type") {
            const char* v = requireNext("-type");
            if (v == nullptr) return false;
            opt.type = v;
        } else if (a == "-w") {
            const char* v = requireNext("-w");
            if (v == nullptr) return false;
            opt.width = std::stoi(v);
            opt.widthProvided = true;
        } else if (a == "-d") {
            const char* v = requireNext("-d");
            if (v == nullptr) return false;
            opt.depth = std::stoi(v);
            opt.depthProvided = true;
        } else if (a == "-seed") {
            const char* v = requireNext("-seed");
            if (v == nullptr) return false;
            opt.seed = static_cast<std::uint32_t>(std::stoul(v));
            opt.seedProvided = true;
        } else if (a == "-p") {
            const char* v = requireNext("-p");
            if (v == nullptr) return false;
            std::string key;
            double value = 0.0;
            if (!ParseParamKV(v, key, value)) {
                outError = std::string("Invalid -p format: ") + v + " (expected key=value)";
                return false;
            }
            opt.extra[key] = value;
        } else if (a == "-dir") {
            const char* v = requireNext("-dir");
            if (v == nullptr) return false;
            opt.outDir = v;
        } else if (a == "-out") {
            const char* v = requireNext("-out");
            if (v == nullptr) return false;
            opt.outName = v;
        } else if (a == "--png") {
            opt.writePng = true;
        } else if (a == "--data") {
            opt.writeData = true;
        } else if (a == "--report") {
            opt.writeReport = true;
        } else if (a == "--plan") {
            opt.writePlan = true;
        } else if (a == "--strict") {
            opt.strictQuality = true;
        } else {
            outError = "Unknown argument: " + a;
            return false;
        }
    }

    const MapTypeSpec* type = FindMapType(opt.type);
    if (type == nullptr) {
        outError = "Unknown map type: " + opt.type + ". Available types: world, dungeon, cave, town, village, castle";
        return false;
    }
    if (!opt.widthProvided) opt.width = type->defaultWidth;
    if (!opt.depthProvided) opt.depth = type->defaultDepth;
    if (opt.width < 64 || opt.width > 1024 || opt.depth < 64 || opt.depth > 1024) {
        outError = "-w and -d must be in range [64, 1024]";
        return false;
    }

    if (!opt.seedProvided) {
        const std::uint64_t t = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        std::seed_seq seq{static_cast<std::uint32_t>(t & 0xffffffffU), static_cast<std::uint32_t>(t >> 32)};
        std::mt19937 rng(seq);
        opt.seed = rng();
    }
    return true;
}

bool WriteDataFile(const std::filesystem::path& path, int w, int h, const std::vector<std::uint8_t>& biomes, std::string& outError) {
    std::ofstream ofs(path);
    if (!ofs) {
        outError = "Failed to open data output: " + path.string();
        return false;
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (x != 0) ofs << ',';
            ofs << static_cast<int>(biomes[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]);
        }
        ofs << '\n';
    }
    return true;
}

const char* BiomeLabel(const std::string& type, int biome) {
    if (type == "cave") {
        static const std::array<const char*, 10> labels = {"FLOOR_LV1", "FLOOR_LV2", "UNUSED_2", "WALL", "INNERWALL", "UNUSED_5", "LADDER", "RIVER", "LAKE", "SEA"};
        return biome >= 0 && biome < static_cast<int>(labels.size()) ? labels[static_cast<std::size_t>(biome)] : "UNKNOWN";
    }
    if (type == "dungeon") {
        static const std::array<const char*, 10> labels = {"FLOOR1", "FLOOR2", "UPPERFLOOR", "WALL", "INNERWALL", "CORRIDOR", "DOOR", "RIVER", "LAKE", "BACKGROUND"};
        return biome >= 0 && biome < static_cast<int>(labels.size()) ? labels[static_cast<std::size_t>(biome)] : "UNKNOWN";
    }
    static const std::array<const char*, 19> labels = {"PLAIN", "DESERT", "FOREST", "MOUNTAIN", "EXMOUNTAIN", "ROAD", "BRIDGE", "RIVER", "LAKE", "SEA", "TOWN_FLOOR", "TOWN_WOOD_FLOOR", "TOWN_STONE_FLOOR", "TOWN_FURNITURE", "TOWN_PAVEMENT", "TOWN_SEA", "TOWN_CLIFF", "TOWN_TREES", "CASTLE_WALL_WALK"};
    return biome >= 0 && biome < static_cast<int>(labels.size()) ? labels[static_cast<std::size_t>(biome)] : "UNKNOWN";
}

bool WriteReportFile(const std::filesystem::path& path, const MapPlan& plan, const MapQualityMetrics& quality, std::string& outError) {
    std::array<int, 19> count{};
    for (std::uint8_t b : plan.biomes) {
        if (b < count.size()) ++count[static_cast<std::size_t>(b)];
    }
    std::ofstream ofs(path);
    if (!ofs) {
        outError = "Failed to open report output: " + path.string();
        return false;
    }
    const int total = std::max(1, plan.width * plan.depth);
    const auto pct = [total](int value) { return 100.0 * static_cast<double>(value) / static_cast<double>(total); };
    ofs << "type=" << plan.type << '\n' << "seed=" << plan.seed << '\n';
    ofs << "size=" << plan.width << "x" << plan.depth << '\n';
    ofs << "archetype=" << plan.archetype << '\n';
    ofs << "cityType=" << plan.cityType << '\n';
    ofs << "settlementSite=" << plan.settlementSite << '\n';
    ofs << "siteRotation=" << plan.siteRotation << '\n';
    ofs << "localEconomy=" << plan.localEconomy << '\n';
    ofs << "terrainModel=" << plan.terrainModel << '\n';
    for (const auto& stage : plan.terrainHistory) {
        ofs << "terrainHistory." << stage.process << "=" << stage.affectedCells << '\n';
    }
    ofs << "countryWealth=" << plan.countryWealth << '\n';
    ofs << "wealthClass=" << plan.wealthClass << '\n';
    ofs << "outerWallDefenseStrength=" << plan.outerWallDefenseStrength << '\n';
    ofs << "castleWallForm=" << plan.castleWallForm << '\n';
    ofs << "castleDefenseLevel=" << plan.castleDefenseLevel << '\n';
    ofs << "castleDevelopmentModel=" << plan.castleDevelopmentModel << '\n';
    for (const auto& stage : plan.castleHistory) {
        ofs << "castleHistory." << stage.phase << "=" << stage.change
            << " (additions=" << stage.additions << ", condition=" << stage.condition << ")\n";
    }
    ofs << "plazaEnabled=" << (plan.plazaEnabled ? "true" : "false") << '\n';
    ofs << "plazaStyle=" << plan.plazaStyle << '\n';
    ofs << "pavingTargetRatio=" << plan.pavingTargetRatio << '\n';
    ofs << "pavingActualRatio=" << plan.pavingActualRatio << '\n';
    ofs << "pavingModel=" << plan.pavingModel << '\n';
    for (const auto& stage : plan.pavingStages) {
        ofs << "pavingStage." << stage.role << "=" << stage.cells
            << " (projects=" << stage.projects << ", accessCells=" << stage.accessCells << ")\n";
    }
    ofs << "environment=" << plan.environment << '\n';
    ofs << "qualityScore=" << plan.qualityScore << '\n';
    ofs << "selectionReason=" << plan.selectionReason << "\n\n";
    for (int biome = 0; biome < static_cast<int>(count.size()); ++biome) {
        if (count[static_cast<std::size_t>(biome)] == 0 && biome != SEA) continue;
        ofs << BiomeLabel(plan.type, biome) << "=" << count[static_cast<std::size_t>(biome)] << " (" << pct(count[static_cast<std::size_t>(biome)]) << "%)\n";
    }
    ofs << "\nQUALITY_PASS=" << (quality.Passed() ? "true" : "false") << '\n';
    ofs << std::fixed << std::setprecision(4);
    ofs << "walkableRatio=" << quality.walkableRatio << '\n';
    ofs << "landRatio=" << quality.landRatio << '\n';
    ofs << "largestWalkableComponentRatio=" << quality.largestWalkableComponentRatio << '\n';
    ofs << "floorRatio=" << quality.floorRatio << '\n';
    ofs << "corridorRatio=" << quality.corridorRatio << '\n';
    ofs << "deadEndRatio=" << quality.deadEndRatio << '\n';
    ofs << "oneCellCorridorCells=" << quality.oneCellCorridorCells << '\n';
    ofs << "doorOrLadderCells=" << quality.doorCells << '\n';
    ofs << "dryLandRatio=" << quality.dryLandRatio << '\n';
    ofs << "dryLandRegions=" << quality.dryLandRegions << '\n';
    ofs << "largestDryLandRegionRatio=" << quality.largestDryLandRegionRatio << '\n';
    ofs << "waterRatio=" << quality.waterRatio << '\n';
    ofs << "waterComponents=" << quality.waterComponents << '\n';
    ofs << "roadRatio=" << quality.roadRatio << '\n';
    ofs << "roadComponents=" << quality.roadComponents << '\n';
    ofs << "largestRoadComponentRatio=" << quality.largestRoadComponentRatio << '\n';
    ofs << "roadEndpointCells=" << quality.roadEndpointCells << '\n';
    ofs << "roadJunctionCells=" << quality.roadJunctionCells << '\n';
    ofs << "townBuildingCount=" << quality.townBuildingCount << '\n';
    ofs << "townDistrictCount=" << quality.townDistrictCount << '\n';
    ofs << "townLandmarkCount=" << quality.townLandmarkCount << '\n';
    ofs << "townEntranceCount=" << quality.townEntranceCount << '\n';
    ofs << "townCenterMarkerCount=" << quality.townCenterMarkerCount << '\n';
    ofs << "townBuildingDoorCount=" << quality.townBuildingDoorCount << '\n';
    ofs << "townReachableBuildingDoors=" << quality.townReachableBuildingDoors << '\n';
    ofs << "townDevelopedWalkableCells=" << quality.townDevelopedWalkableCells << '\n';
    ofs << "townReachableDevelopedCells=" << quality.townReachableDevelopedCells << '\n';
    ofs << "townDevelopmentReachableRatio=" << quality.townDevelopmentReachableRatio << '\n';
    ofs << "townRoadRatio=" << quality.townRoadRatio << '\n';
    ofs << "townPlazaCells=" << quality.townPlazaCells << '\n';
    ofs << "townBuildingCells=" << quality.townBuildingCells << '\n';
    ofs << "townPavedCells=" << quality.townPavedCells << '\n';
    ofs << "townBuildingKindCount=" << quality.townBuildingKindCount << '\n';
    ofs << "townInvalidRoadWidths=" << quality.townInvalidRoadWidths << '\n';
    ofs << "townParkCells=" << quality.townParkCells << '\n';
    ofs << "townWaterFeatureCells=" << quality.townWaterFeatureCells << '\n';
    ofs << "townInvalidBuildingDoors=" << quality.townInvalidBuildingDoors << '\n';
    ofs << "townInvalidEntrances=" << quality.townInvalidEntrances << '\n';
    ofs << "invalidSemanticMarkers=" << quality.invalidSemanticMarkers << '\n';
    ofs << "townPlazaRatio=" << quality.townPlazaRatio << '\n';
    ofs << "townBuildingRatio=" << quality.townBuildingRatio << '\n';
    ofs << "townPavingRatio=" << quality.townPavingRatio << '\n';
    ofs << "townPavingTargetDelta=" << quality.townPavingTargetDelta << '\n';
    ofs << "townParkWaterRatio=" << quality.townParkWaterRatio << '\n';
    ofs << "castleTowerCount=" << quality.castleTowerCount << '\n';
    ofs << "castleGateCount=" << quality.castleGateCount << '\n';
    ofs << "castleWatchTowerCount=" << quality.castleWatchTowerCount << '\n';
    ofs << "castleMainGateCount=" << quality.castleMainGateCount << '\n';
    ofs << "castlePosternGateCount=" << quality.castlePosternGateCount << '\n';
    ofs << "castleWallWalkCells=" << quality.castleWallWalkCells << '\n';
    ofs << "castleRoomCount=" << quality.castleRoomCount << '\n';
    ofs << "castleFacilityCount=" << quality.castleFacilityCount << '\n';
    ofs << "castleReachableRooms=" << quality.castleReachableRooms << '\n';
    ofs << "castleReachableFacilities=" << quality.castleReachableFacilities << '\n';
    ofs << "castleGatehouseCount=" << quality.castleGatehouseCount << '\n';
    ofs << "castleGardenCells=" << quality.castleGardenCells << '\n';
    ofs << "castleTrainingGroundCells=" << quality.castleTrainingGroundCells << '\n';
    ofs << "castleInvalidRoomDoors=" << quality.castleInvalidRoomDoors << '\n';
    ofs << "castleInvalidGateMarkers=" << quality.castleInvalidGateMarkers << '\n';
    ofs << "castleInvalidWaterCrossings=" << quality.castleInvalidWaterCrossings << '\n';
    ofs << "castleMoatCells=" << quality.castleMoatCells << '\n';
    ofs << "castleMoatCoverage=" << quality.castleMoatCoverage << '\n';
    ofs << "castleMoatGapCells=" << quality.castleMoatGapCells << '\n';
    ofs << "castleWallGapCells=" << quality.castleWallGapCells << '\n';
    ofs << "castleInnerGateCount=" << quality.castleInnerGateCount << '\n';
    ofs << "castleDefenseBypassCount=" << quality.castleDefenseBypassCount << '\n';
    ofs << "castleInvalidFacilityDoors=" << quality.castleInvalidFacilityDoors << '\n';
    ofs << "castleFacilityRoleCount=" << quality.castleFacilityRoleCount << '\n';
    ofs << "castleProgramMissingRoles=" << quality.castleProgramMissingRoles << '\n';
    ofs << "castleFootprintCells=" << quality.castleFootprintCells << '\n';
    ofs << "castleKeepFootprintCells=" << quality.castleKeepFootprintCells << '\n';
    ofs << "castleHallCells=" << quality.castleHallCells << '\n';
    ofs << "castleKitchenCells=" << quality.castleKitchenCells << '\n';
    ofs << "castleEquipmentCount=" << quality.castleEquipmentCount << '\n';
    ofs << "castleCourtyardEmptyCells=" << quality.castleCourtyardEmptyCells << '\n';
    ofs << "castleCourtyardCells=" << quality.castleCourtyardCells << '\n';
    ofs << "castleCourtyardEmptyRatio=" << quality.castleCourtyardEmptyRatio << '\n';
    ofs << "castleKeepCenterOffset=" << quality.castleKeepCenterOffset << '\n';
    ofs << "markers=" << quality.markerCount << '\n';
    ofs << "reachableMarkers=" << quality.reachableMarkers << '\n';
    for (const std::string& issue : quality.issues) ofs << "issue=" << issue << '\n';
    return true;
}

std::string JsonEscape(const std::string& value) {
    std::string result;
    for (char c : value) {
        if (c == '\\' || c == '"') result.push_back('\\');
        result.push_back(c);
    }
    return result;
}

bool WritePlanFile(const std::filesystem::path& path, const MapPlan& plan, const MapQualityMetrics& quality, std::string& outError) {
    std::ofstream ofs(path);
    if (!ofs) {
        outError = "Failed to open plan output: " + path.string();
        return false;
    }
    ofs << "{\n  \"type\": \"" << JsonEscape(plan.type) << "\",\n";
    ofs << "  \"archetype\": \"" << JsonEscape(plan.archetype) << "\",\n";
    ofs << "  \"cityType\": \"" << JsonEscape(plan.cityType) << "\",\n";
    ofs << "  \"settlementSite\": \"" << JsonEscape(plan.settlementSite) << "\",\n";
    ofs << "  \"siteRotation\": " << plan.siteRotation << ",\n";
    ofs << "  \"localEconomy\": \"" << JsonEscape(plan.localEconomy) << "\",\n";
    ofs << "  \"terrainModel\": \"" << JsonEscape(plan.terrainModel) << "\",\n";
    ofs << "  \"terrainHistory\": [";
    for (std::size_t i = 0; i < plan.terrainHistory.size(); ++i) {
        if (i > 0) ofs << ',';
        const auto& stage = plan.terrainHistory[i];
        ofs << "{\"process\": \"" << JsonEscape(stage.process) << "\", \"affectedCells\": " << stage.affectedCells << '}';
    }
    ofs << "],\n";
    ofs << "  \"countryWealth\": " << plan.countryWealth << ",\n";
    ofs << "  \"wealthClass\": \"" << JsonEscape(plan.wealthClass) << "\",\n";
    ofs << "  \"outerWallDefenseStrength\": " << plan.outerWallDefenseStrength << ",\n";
    ofs << "  \"castleWallForm\": \"" << JsonEscape(plan.castleWallForm) << "\",\n";
    ofs << "  \"castleDefenseLevel\": " << plan.castleDefenseLevel << ",\n";
    ofs << "  \"castleDevelopmentModel\": \"" << JsonEscape(plan.castleDevelopmentModel) << "\",\n";
    ofs << "  \"castleHistory\": [";
    for (std::size_t i = 0; i < plan.castleHistory.size(); ++i) {
        if (i > 0) ofs << ',';
        const auto& stage = plan.castleHistory[i];
        ofs << "{\"phase\": \"" << JsonEscape(stage.phase) << "\", \"change\": \""
            << JsonEscape(stage.change) << "\", \"additions\": " << stage.additions
            << ", \"condition\": " << stage.condition << '}';
    }
    ofs << "],\n";
    ofs << "  \"plazaEnabled\": " << (plan.plazaEnabled ? "true" : "false") << ",\n";
    ofs << "  \"plazaStyle\": \"" << JsonEscape(plan.plazaStyle) << "\",\n";
    ofs << "  \"pavingTargetRatio\": " << plan.pavingTargetRatio << ",\n";
    ofs << "  \"pavingActualRatio\": " << plan.pavingActualRatio << ",\n";
    ofs << "  \"pavingModel\": \"" << JsonEscape(plan.pavingModel) << "\",\n";
    ofs << "  \"pavingStages\": [";
    for (std::size_t i = 0; i < plan.pavingStages.size(); ++i) {
        const auto& stage = plan.pavingStages[i];
        if (i > 0) ofs << ',';
        ofs << "{\"role\": \"" << JsonEscape(stage.role) << "\", \"priority\": " << i + 1
            << ", \"projects\": " << stage.projects << ", \"cells\": " << stage.cells
            << ", \"accessCells\": " << stage.accessCells << '}';
    }
    ofs << "],\n";
    ofs << "  \"environment\": \"" << JsonEscape(plan.environment) << "\",\n";
    ofs << "  \"seed\": " << plan.seed << ",\n  \"width\": " << plan.width << ",\n  \"depth\": " << plan.depth << ",\n";
    ofs << "  \"qualityPass\": " << (quality.Passed() ? "true" : "false") << ",\n";
    ofs << "  \"qualityScore\": " << plan.qualityScore << ",\n";
    ofs << "  \"selectionReason\": \"" << JsonEscape(plan.selectionReason) << "\",\n";
    ofs << "  \"quality\": {\"walkableRatio\": " << quality.walkableRatio
        << ", \"landRatio\": " << quality.landRatio
        << ", \"largestWalkableComponentRatio\": " << quality.largestWalkableComponentRatio
        << ", \"dryLandRatio\": " << quality.dryLandRatio
        << ", \"largestDryLandRegionRatio\": " << quality.largestDryLandRegionRatio
        << ", \"waterRatio\": " << quality.waterRatio
        << ", \"roadRatio\": " << quality.roadRatio
        << ", \"largestRoadComponentRatio\": " << quality.largestRoadComponentRatio
        << ", \"roadEndpointCells\": " << quality.roadEndpointCells
        << ", \"roadJunctionCells\": " << quality.roadJunctionCells
        << ", \"townBuildingCount\": " << quality.townBuildingCount
        << ", \"townDistrictCount\": " << quality.townDistrictCount
        << ", \"townLandmarkCount\": " << quality.townLandmarkCount
        << ", \"townEntranceCount\": " << quality.townEntranceCount
        << ", \"townCenterMarkerCount\": " << quality.townCenterMarkerCount
        << ", \"townBuildingDoorCount\": " << quality.townBuildingDoorCount
        << ", \"townReachableBuildingDoors\": " << quality.townReachableBuildingDoors
        << ", \"townDevelopedWalkableCells\": " << quality.townDevelopedWalkableCells
        << ", \"townReachableDevelopedCells\": " << quality.townReachableDevelopedCells
        << ", \"townDevelopmentReachableRatio\": " << quality.townDevelopmentReachableRatio
        << ", \"townRoadRatio\": " << quality.townRoadRatio
        << ", \"townPlazaCells\": " << quality.townPlazaCells
        << ", \"townBuildingCells\": " << quality.townBuildingCells
        << ", \"townPavedCells\": " << quality.townPavedCells
        << ", \"townBuildingKindCount\": " << quality.townBuildingKindCount
        << ", \"townInvalidRoadWidths\": " << quality.townInvalidRoadWidths
        << ", \"townParkCells\": " << quality.townParkCells
        << ", \"townWaterFeatureCells\": " << quality.townWaterFeatureCells
        << ", \"townInvalidBuildingDoors\": " << quality.townInvalidBuildingDoors
        << ", \"townInvalidEntrances\": " << quality.townInvalidEntrances
        << ", \"invalidSemanticMarkers\": " << quality.invalidSemanticMarkers
        << ", \"townPlazaRatio\": " << quality.townPlazaRatio
        << ", \"townBuildingRatio\": " << quality.townBuildingRatio
        << ", \"townPavingRatio\": " << quality.townPavingRatio
        << ", \"townPavingTargetDelta\": " << quality.townPavingTargetDelta
        << ", \"townParkWaterRatio\": " << quality.townParkWaterRatio
        << ", \"castleTowerCount\": " << quality.castleTowerCount
        << ", \"castleGateCount\": " << quality.castleGateCount
        << ", \"castleWatchTowerCount\": " << quality.castleWatchTowerCount
        << ", \"castleMainGateCount\": " << quality.castleMainGateCount
        << ", \"castlePosternGateCount\": " << quality.castlePosternGateCount
        << ", \"castleWallWalkCells\": " << quality.castleWallWalkCells
        << ", \"castleRoomCount\": " << quality.castleRoomCount
        << ", \"castleFacilityCount\": " << quality.castleFacilityCount
        << ", \"castleReachableRooms\": " << quality.castleReachableRooms
        << ", \"castleReachableFacilities\": " << quality.castleReachableFacilities
        << ", \"castleGatehouseCount\": " << quality.castleGatehouseCount
        << ", \"castleGardenCells\": " << quality.castleGardenCells
        << ", \"castleTrainingGroundCells\": " << quality.castleTrainingGroundCells
        << ", \"castleInvalidRoomDoors\": " << quality.castleInvalidRoomDoors
        << ", \"castleInvalidGateMarkers\": " << quality.castleInvalidGateMarkers
        << ", \"castleInvalidWaterCrossings\": " << quality.castleInvalidWaterCrossings
        << ", \"castleMoatCells\": " << quality.castleMoatCells
        << ", \"castleMoatCoverage\": " << quality.castleMoatCoverage
        << ", \"castleMoatGapCells\": " << quality.castleMoatGapCells
        << ", \"castleWallGapCells\": " << quality.castleWallGapCells
        << ", \"castleInnerGateCount\": " << quality.castleInnerGateCount
        << ", \"castleDefenseBypassCount\": " << quality.castleDefenseBypassCount
        << ", \"castleInvalidFacilityDoors\": " << quality.castleInvalidFacilityDoors
        << ", \"castleFacilityRoleCount\": " << quality.castleFacilityRoleCount
        << ", \"castleProgramMissingRoles\": " << quality.castleProgramMissingRoles
        << ", \"castleFootprintCells\": " << quality.castleFootprintCells
        << ", \"castleKeepFootprintCells\": " << quality.castleKeepFootprintCells
        << ", \"castleHallCells\": " << quality.castleHallCells
        << ", \"castleKitchenCells\": " << quality.castleKitchenCells
        << ", \"castleEquipmentCount\": " << quality.castleEquipmentCount
        << ", \"castleCourtyardEmptyCells\": " << quality.castleCourtyardEmptyCells
        << ", \"castleCourtyardCells\": " << quality.castleCourtyardCells
        << ", \"castleCourtyardEmptyRatio\": " << quality.castleCourtyardEmptyRatio
        << ", \"castleKeepCenterOffset\": " << quality.castleKeepCenterOffset
        << ", \"issues\": [";
    for (std::size_t i = 0; i < quality.issues.size(); ++i) {
        if (i != 0) ofs << ',';
        ofs << "\"" << JsonEscape(quality.issues[i]) << "\"";
    }
    ofs << "]},\n  \"biomes\": [";
    for (std::size_t i = 0; i < plan.biomes.size(); ++i) {
        if (i != 0) ofs << ',';
        if (i % static_cast<std::size_t>(std::max(1, plan.width)) == 0) ofs << "\n    ";
        ofs << static_cast<int>(plan.biomes[i]);
    }
    ofs << "\n  ],\n  \"settlementMask\": [";
    for (std::size_t i = 0; i < plan.settlementMask.size(); ++i) {
        if (i != 0) ofs << ',';
        if (i % static_cast<std::size_t>(std::max(1, plan.width)) == 0) ofs << "\n    ";
        ofs << static_cast<int>(plan.settlementMask[i]);
    }
    ofs << "\n  ],\n  \"castleDefenseMask\": [";
    for (std::size_t i = 0; i < plan.castleDefenseMask.size(); ++i) {
        if (i != 0) ofs << ',';
        if (i % static_cast<std::size_t>(std::max(1, plan.width)) == 0) ofs << "\n    ";
        ofs << static_cast<int>(plan.castleDefenseMask[i]);
    }
    ofs << "\n  ],\n  \"markers\": [\n";
    const auto regionArea = [](const MapPlanRegion& region) {
        return std::max(0, region.x1 - region.x0 + 1) * std::max(0, region.y1 - region.y0 + 1);
    };
    const auto markerInsideRegion = [](const MapPlanMarker& marker, const MapPlanRegion& region) {
        return marker.x >= region.x0 && marker.x <= region.x1 && marker.y >= region.y0 && marker.y <= region.y1;
    };
    const auto markerRegionMatches = [&](const MapPlanMarker& marker, const MapPlanRegion& region) {
        if (!markerInsideRegion(marker, region)) return false;
        if (plan.type == "town" || plan.type == "village") {
            return (marker.kind == "building_door" || marker.kind == "landmark") &&
                (region.kind == "building" || region.kind == "market" || region.kind == "plaza");
        }
        if (marker.kind == "room_door") return region.kind == "room";
        if (marker.kind == "stairs") return region.kind == "tower";
        if (marker.kind == "wall_access") return region.kind == "wall_walk" || region.kind == "gatehouse";
        if (marker.kind == "service_entry") {
            return region.kind == "facility" || region.kind == "keep" || region.kind == "inner_ward";
        }
        if (marker.kind == "gate" || marker.kind == "entrance" || marker.kind == "water_crossing") {
            return region.kind == "gatehouse";
        }
        if (marker.kind == "landmark") {
            return region.kind == "tower" || region.kind == "room" || region.kind == "facility" || region.kind == "keep";
        }
        return false;
    };
    for (std::size_t i = 0; i < plan.markers.size(); ++i) {
        const MapPlanMarker& marker = plan.markers[i];
        const bool hasRegionLink = marker.kind == "building_door" || marker.kind == "room_door" ||
            marker.kind == "landmark" || marker.kind == "stairs" || marker.kind == "wall_access" || marker.kind == "service_entry" ||
            (plan.type == "castle" && (marker.kind == "gate" || marker.kind == "entrance" || marker.kind == "water_crossing"));
        int regionId = marker.regionId >= 0 ? marker.regionId : -1;
        int regionIndex = marker.regionIndex;
        if (regionIndex < 0 && hasRegionLink) {
            int smallestRegionArea = std::numeric_limits<int>::max();
            for (std::size_t candidateIndex = 0; candidateIndex < plan.regions.size(); ++candidateIndex) {
                const MapPlanRegion& candidate = plan.regions[candidateIndex];
                if (!markerRegionMatches(marker, candidate)) continue;
                const int area = regionArea(candidate);
                if (area < smallestRegionArea) {
                    smallestRegionArea = area;
                    regionIndex = static_cast<int>(candidateIndex);
                    regionId = candidate.id;
                }
            }
        }
        if (regionId < 0 && hasRegionLink) regionId = marker.id;
        const std::string role = marker.role.empty() ? marker.kind : marker.role;
        if (i != 0) ofs << ",\n";
        ofs << "    {\"kind\": \"" << JsonEscape(marker.kind) << "\", \"role\": \"" << JsonEscape(role)
            << "\", \"x\": " << marker.x << ", \"y\": " << marker.y << ", \"id\": " << marker.id
            << ", \"regionId\": " << regionId << ", \"regionIndex\": " << regionIndex << "}";
    }
    ofs << "\n  ],\n  \"regions\": [\n";
    const auto containsRegionCenter = [](const MapPlanRegion& parent, const MapPlanRegion& child) {
        const int centerX = (child.x0 + child.x1) / 2;
        const int centerY = (child.y0 + child.y1) / 2;
        return centerX >= parent.x0 && centerX <= parent.x1 && centerY >= parent.y0 && centerY <= parent.y1;
    };
    for (std::size_t i = 0; i < plan.regions.size(); ++i) {
        const MapPlanRegion& region = plan.regions[i];
        const std::string role = region.role.empty() ? region.kind : region.role;
        int parentId = region.parentId;
        int parentIndex = region.parentIndex;
        if (parentId < 0 && region.kind != "town_footprint" && region.kind != "fortress") {
            int smallestParentArea = std::numeric_limits<int>::max();
            for (std::size_t parentIndexCandidate = 0; parentIndexCandidate < plan.regions.size(); ++parentIndexCandidate) {
                const MapPlanRegion& parent = plan.regions[parentIndexCandidate];
                if (parent.id == region.id && parent.kind == region.kind) continue;
                const bool eligible = plan.type == "town" || plan.type == "village"
                    ? (parent.kind == "town_footprint" || parent.kind == "district" || parent.kind == "plaza")
                    : (parent.kind == "fortress" || parent.kind == "outer_bailey" || parent.kind == "inner_ward" || parent.kind == "keep");
                if (!eligible || !containsRegionCenter(parent, region)) continue;
                const int area = regionArea(parent);
                if (area > regionArea(region) && area < smallestParentArea) {
                    smallestParentArea = area;
                    parentId = parent.id;
                    parentIndex = static_cast<int>(parentIndexCandidate);
                }
            }
        }
        if (i != 0) ofs << ",\n";
        ofs << "    {\"kind\": \"" << JsonEscape(region.kind) << "\", \"id\": " << region.id
            << ", \"x0\": " << region.x0 << ", \"y0\": " << region.y0
            << ", \"x1\": " << region.x1 << ", \"y1\": " << region.y1
            << ", \"parentId\": " << parentId << ", \"parentIndex\": " << parentIndex
            << ", \"role\": \"" << JsonEscape(role) << "\""
            << ", \"householdId\": " << region.householdId
            << ", \"residentProfile\": \"" << JsonEscape(region.residentProfile) << "\""
            << ", \"residentCount\": " << region.residentCount
            << ", \"workerCount\": " << region.workerCount
            << ", \"livingArrangement\": \"" << JsonEscape(region.livingArrangement) << "\""
            << ", \"waterSource\": \"" << JsonEscape(region.waterSource) << "\""
            << ", \"hearth\": \"" << JsonEscape(region.hearth) << "\""
            << ", \"linkedRegionId\": " << region.linkedRegionId
            << ", \"width\": " << region.width << "}";
    }
    ofs << "\n  ]\n}\n";
    return true;
}

void PrintQuality(const MapQualityMetrics& quality) {
    std::cout << "QUALITY: " << (quality.Passed() ? "PASS" : "WARN")
              << " walkable=" << quality.walkableRatio
              << " land=" << quality.landRatio
              << " largest_component=" << quality.largestWalkableComponentRatio
              << " floor=" << quality.floorRatio
              << " corridor=" << quality.corridorRatio
              << " road=" << quality.roadRatio
              << " water=" << quality.waterRatio
              << " markers=" << quality.reachableMarkers << "/" << quality.markerCount << '\n';
    for (const std::string& issue : quality.issues) std::cout << "QUALITY_ISSUE: " << issue << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    CliOptions opt;
    std::string error;
    try {
        if (!ParseArgs(argc, argv, opt, error)) {
            std::cerr << "Argument error: " << error << '\n';
            std::cerr << "Usage: mapimggen.exe -type world|dungeon|cave|town|village|castle -w 256 -d 256 -seed 12345 [-p key=value] -dir out -out name [--data --png --report --plan --strict]\n";
            return 1;
        }
    } catch (const std::exception& ex) {
        std::cerr << "Argument parse exception: " << ex.what() << '\n';
        return 1;
    }

    MapGenParams params;
    params.width = opt.width;
    params.depth = opt.depth;
    params.seed = opt.seed;
    params.extra = opt.extra;
    const MapTypeSpec* type = FindMapType(opt.type);
    std::unique_ptr<MapImageGenerator> generator = type->create(std::move(params));

    MapPlan plan;
    if (!generator->Generate(plan, error)) {
        std::cerr << "Generation failed: " << error << '\n';
        return 2;
    }
    plan.type = opt.type;
    const MapQualityMetrics quality = EvaluateMapQuality(plan);
    PrintQuality(quality);

    std::filesystem::create_directories(opt.outDir);
    if (opt.writePng) {
        std::vector<mapgen::Rgb> rgb;
        rgb.reserve(plan.biomes.size());
        for (std::uint8_t b : plan.biomes) {
            const MapGenColor c = generator->BiomeToColor(b);
            rgb.push_back({c.r, c.g, c.b});
        }
        const std::filesystem::path pngPath = opt.outDir / (opt.outName + ".png");
        if (!mapgen::WritePngRgb(pngPath.string(), plan.width, plan.depth, rgb, error)) {
            std::cerr << "PNG write failed: " << error << '\n';
            return 3;
        }
        std::cout << "PNG: " << pngPath.string() << '\n';
    }
    if (opt.writeData) {
        const std::filesystem::path dataPath = opt.outDir / (opt.outName + "_biome.csv");
        if (!WriteDataFile(dataPath, plan.width, plan.depth, plan.biomes, error)) {
            std::cerr << "Data write failed: " << error << '\n';
            return 4;
        }
        std::cout << "DATA: " << dataPath.string() << '\n';
    }
    if (opt.writeReport) {
        const std::filesystem::path reportPath = opt.outDir / (opt.outName + "_report.txt");
        if (!WriteReportFile(reportPath, plan, quality, error)) {
            std::cerr << "Report write failed: " << error << '\n';
            return 5;
        }
        std::cout << "REPORT: " << reportPath.string() << '\n';
    }
    if (opt.writePlan) {
        const std::filesystem::path planPath = opt.outDir / (opt.outName + "_plan.json");
        if (!WritePlanFile(planPath, plan, quality, error)) {
            std::cerr << "Plan write failed: " << error << '\n';
            return 6;
        }
        std::cout << "PLAN: " << planPath.string() << '\n';
    }
    if (opt.strictQuality && !quality.Passed()) {
        std::cerr << "Strict quality validation failed for map type " << opt.type << '\n';
        return 7;
    }
    std::cout << "Done. type=" << opt.type << " seed=" << opt.seed << '\n';
    return 0;
}
