#include "TownMapImageGenerator.h"

#include "MapQuality.h"
#include "TownPaving.h"
#include "SettlementSite.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Point {
    int x = 0;
    int y = 0;
};

struct Rect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;

    int Width() const { return x1 - x0; }
    int Height() const { return y1 - y0; }
    int CenterX() const { return (x0 + x1) / 2; }
    int CenterY() const { return (y0 + y1) / 2; }
};

struct HouseholdProfile {
    int householdId = -1;
    std::string residentProfile;
    int residentCount = 0;
    int workerCount = 0;
    std::string livingArrangement = "none";
    std::string waterSource = "none";
    std::string hearth = "none";
    bool hasLivingSpace = false;
    bool requiresSeparateResidence = false;
    bool isResidence = false;
    int linkedRegionId = -1;
};

struct Building {
    std::string kind;
    Rect rect;
    Point door;
    Point approach;
    int regionId = -1;
    bool landmark = false;
    HouseholdProfile household;
};

struct BuildingProfile {
    int minWidth = 6;
    int maxWidth = 10;
    int minHeight = 6;
    int maxHeight = 9;
    int furnitureCount = 2;
    bool commercial = false;
};

struct TownNode {
    Point point;
    // 0 = alley, 1 = normal district road, 2 = main road.
    int roadClass = 0;
    std::string roadReason;
};

enum class TownMode {
    Grid,
    Village,
    River,
    Walled,
    Market,
    Organic,
};

enum class TownCityType {
    MedievalCity,
    CastleTown,
    PavedCity,
    WalledCity,
};

struct TownVisualStyle {
    const char* plazaStyle = "civic_square";
    double plazaChance = 0.65;
    double pavingRatio = 0.35;
    std::uint8_t plazaTile = TOWN_STONE_FLOOR;
};

std::uint32_t CandidateSeed(std::uint32_t seed, int candidate);

enum class TownTerrain {
    Meadow,
    Arid,
    Woodland,
};

const char* TownTerrainName(TownTerrain terrain) {
    switch (terrain) {
        case TownTerrain::Meadow: return "meadow";
        case TownTerrain::Arid: return "arid";
        case TownTerrain::Woodland: return "woodland";
    }
    return "meadow";
}

std::uint8_t TownTerrainTile(TownTerrain terrain) {
    switch (terrain) {
        case TownTerrain::Meadow: return PLAIN;
        case TownTerrain::Arid: return DESERT;
        case TownTerrain::Woodland: return FOREST;
    }
    return PLAIN;
}

BuildingProfile ProfileForBuilding(const std::string& kind, bool urban, int shortSide) {
    const double scale = std::clamp(static_cast<double>(shortSide) / 160.0, 0.65, 2.5);
    BuildingProfile profile;
    if (kind == "cottage") {
        profile = {5, 9, 5, 8, 2, false};
    } else if (kind == "house") {
        profile = {6, 11, 6, 9, 3, false};
    } else if (kind == "barn") {
        profile = {8, 14, 6, 10, 2, false};
    } else if (kind == "weapon_shop" || kind == "workshop") {
        profile = {8, 14, 7, 11, 4, true};
    } else if (kind == "general_store" || kind == "market_hall" || kind == "market") {
        profile = {9, 16, 7, 11, 5, true};
    } else if (kind == "inn") {
        profile = {12, 19, 9, 14, 7, true};
    } else if (kind == "mayor_house" || kind == "manor") {
        profile = {13, 22, 10, 16, 8, true};
    } else if (kind == "shrine") {
        profile = {9, 16, 9, 14, 5, true};
    } else if (kind == "warehouse") {
        profile = {10, 17, 7, 12, 3, false};
    } else if (kind == "fishery") {
        profile = {9, 15, 7, 11, 4, true};
    } else if (kind == "lumbermill") {
        profile = {11, 18, 8, 13, 5, true};
    } else if (kind == "mine_office") {
        profile = {9, 15, 8, 12, 4, true};
    } else if (kind == "caravanserai") {
        profile = {14, 21, 11, 16, 7, true};
    }
    if (!urban) {
        profile.minWidth = std::max(5, profile.minWidth - 1);
        profile.maxWidth = std::max(profile.minWidth, profile.maxWidth - 2);
        profile.minHeight = std::max(5, profile.minHeight - 1);
        profile.maxHeight = std::max(profile.minHeight, profile.maxHeight - 2);
    }
    profile.minWidth = std::clamp(static_cast<int>(std::lround(profile.minWidth * scale)), 5, 30);
    profile.maxWidth = std::clamp(static_cast<int>(std::lround(profile.maxWidth * scale)), profile.minWidth, 36);
    profile.minHeight = std::clamp(static_cast<int>(std::lround(profile.minHeight * scale)), 5, 26);
    profile.maxHeight = std::clamp(static_cast<int>(std::lround(profile.maxHeight * scale)), profile.minHeight, 30);
    return profile;
}

HouseholdProfile PlanHousehold(
    const std::string& kind,
    bool villageLayout,
    bool urban,
    int householdId,
    std::mt19937& rng) {
    HouseholdProfile profile;
    profile.householdId = householdId;
    const int povertyRoll = std::uniform_int_distribution<int>(0, 99)(rng);
    const bool poor = povertyRoll < (villageLayout ? 35 : 20);
    const bool veryPoor = povertyRoll < (villageLayout ? 12 : 7);
    const auto residents = [&](int minimum, int maximum) {
        const int upper = std::max(minimum, maximum + (urban ? 1 : 0));
        return std::uniform_int_distribution<int>(minimum, upper)(rng);
    };
    const auto setLiving = [&](const char* residentProfile, int minimum, int maximum, bool integrated) {
        profile.residentProfile = residentProfile;
        profile.residentCount = residents(minimum, maximum);
        profile.livingArrangement = integrated ? "integrated" : "separate_residence";
        profile.hasLivingSpace = true;
        profile.isResidence = true;
        profile.waterSource = veryPoor ? "none" : (villageLayout ? "well" : "water_pump");
        profile.hearth = veryPoor ? "none" : "hearth";
        if (poor && !veryPoor && std::uniform_int_distribution<int>(0, 99)(rng) < 35) profile.waterSource = "water_barrel";
        if (poor && !veryPoor && std::uniform_int_distribution<int>(0, 99)(rng) < 25) profile.hearth = "none";
    };

    if (kind == "cottage") {
        setLiving(villageLayout ? "farm_family" : "laborer_family", 2, 4, true);
    } else if (kind == "house") {
        setLiving(villageLayout ? "craft_family" : "town_family", 2, 5, true);
    } else if (kind == "barn") {
        setLiving("farm_family", 2, 5, true);
    } else if (kind == "inn" || kind == "caravanserai") {
        setLiving(kind == "inn" ? "innkeeper_household" : "caravan_host_household", 2, 5, true);
        profile.workerCount = residents(1, 3);
    } else if (kind == "fishery" || kind == "lumbermill" || kind == "mine_office") {
        setLiving(kind == "fishery" ? "fisher_household" : (kind == "lumbermill" ? "forester_household" : "miner_household"), 2, 5, true);
        profile.workerCount = residents(2, 5);
    } else if (kind == "mayor_house" || kind == "manor") {
        setLiving(kind == "mayor_house" ? "mayor_household" : "manor_household", 3, 7, true);
        profile.workerCount = residents(1, 4);
    } else if (kind == "shrine") {
        setLiving("caretaker_household", 1, 2, true);
    } else if (kind == "workshop" || kind == "weapon_shop") {
        profile.residentProfile = villageLayout ? "artisan_family" : "artisan_household";
        profile.residentCount = residents(1, 4);
        profile.workerCount = residents(1, 3);
        profile.waterSource = "water_barrel";
        profile.hearth = "forge";
        if (villageLayout) {
            setLiving("artisan_family", 1, 4, true);
            profile.workerCount = residents(1, 3);
            profile.hearth = "hearth";
        } else {
            profile.livingArrangement = "business_only";
            profile.requiresSeparateResidence = true;
        }
    } else if (kind == "general_store" || kind == "market_hall") {
        profile.residentProfile = villageLayout ? "trader_family" : "merchant_household";
        profile.residentCount = residents(1, 4);
        profile.workerCount = residents(1, kind == "market_hall" ? 5 : 3);
        profile.waterSource = villageLayout ? "water_barrel" : "none";
        if (villageLayout) {
            setLiving("trader_family", 1, 4, true);
            profile.workerCount = residents(1, 3);
        } else {
            profile.livingArrangement = "business_only";
            profile.requiresSeparateResidence = true;
        }
    } else if (kind == "warehouse") {
        profile.residentProfile = "warehouse_workers";
        profile.workerCount = residents(1, 4);
        profile.livingArrangement = "business_only";
        profile.waterSource = "water_barrel";
    } else {
        setLiving("town_family", 1, 4, true);
    }
    return profile;
}

std::uint8_t BuildingFloorTile(const std::string& kind) {
    if (kind == "shrine" || kind == "mayor_house" || kind == "manor") return TOWN_STONE_FLOOR;
    if (kind == "house" || kind == "cottage" || kind == "barn" || kind == "inn" || kind == "fishery" || kind == "lumbermill") return TOWN_WOOD_FLOOR;
    if (kind == "mine_office" || kind == "caravanserai") return TOWN_STONE_FLOOR;
    return TOWN_FLOOR;
}

bool IsCommercialBuilding(const std::string& kind) {
    return kind == "weapon_shop" || kind == "workshop" || kind == "general_store" ||
        kind == "market" || kind == "market_hall" || kind == "inn" || kind == "fishery" ||
        kind == "lumbermill" || kind == "mine_office" || kind == "caravanserai";
}

const char* TownModeName(TownMode mode) {
    switch (mode) {
        case TownMode::Grid: return "grid";
        case TownMode::Village: return "village";
        case TownMode::River: return "river";
        case TownMode::Walled: return "walled";
        case TownMode::Market: return "market";
        case TownMode::Organic: return "organic";
    }
    return "unknown";
}

const char* TownCityTypeName(TownCityType cityType) {
    switch (cityType) {
        case TownCityType::MedievalCity: return "medieval_city";
        case TownCityType::CastleTown: return "castle_town";
        case TownCityType::PavedCity: return "paved_city";
        case TownCityType::WalledCity: return "walled_city";
    }
    return "medieval_city";
}

TownVisualStyle TownVisualStyleFor(TownCityType cityType, bool villageLayout) {
    if (villageLayout) return {"village_green", 0.55, 0.0, TOWN_FLOOR};
    switch (cityType) {
        case TownCityType::MedievalCity: return {"civic_square", 0.68, 0.36, TOWN_STONE_FLOOR};
        case TownCityType::CastleTown: return {"castle_courtyard", 0.76, 0.48, TOWN_STONE_FLOOR};
        case TownCityType::PavedCity: return {"paved_forum", 0.62, 0.84, TOWN_PAVEMENT};
        case TownCityType::WalledCity: return {"market_square", 0.66, 0.56, TOWN_STONE_FLOOR};
    }
    return {"civic_square", 0.68, 0.36, TOWN_STONE_FLOOR};
}

TownCityType ChooseCityType(const MapGenParams& params) {
    const auto requestedIt = params.extra.find("townCityType");
    if (requestedIt != params.extra.end() && requestedIt->second >= 0.0) {
        const int cityType = std::clamp(static_cast<int>(std::lround(requestedIt->second)), 0, 3);
        return static_cast<TownCityType>(cityType);
    }
    // Select the city identity once per generation, before candidate layout
    // scoring. This makes density, paving, walls, and facilities properties
    // of the same city rather than independent visual afterthoughts.
    return static_cast<TownCityType>(CandidateSeed(params.seed, 701) % 4U);
}

inline int Index(int x, int y, int width) {
    return y * width + x;
}

inline bool InBounds(int x, int y, int width, int height) {
    return x >= 0 && y >= 0 && x < width && y < height;
}

inline bool SamePoint(const Point& a, const Point& b) {
    return a.x == b.x && a.y == b.y;
}

inline int Manhattan(const Point& a, const Point& b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

inline bool Contains(const Rect& rect, int x, int y) {
    return x >= rect.x0 && x < rect.x1 && y >= rect.y0 && y < rect.y1;
}

bool IsWater(std::uint8_t biome) {
    return biome == RIVER || biome == LAKE || biome == SEA || biome == TOWN_SEA;
}

bool IsNaturalObstacle(std::uint8_t biome) {
    return biome == TOWN_SEA || biome == TOWN_CLIFF || biome == TOWN_TREES;
}

bool IsRoadLike(std::uint8_t biome) {
    return biome == ROAD || biome == BRIDGE;
}

std::uint32_t CandidateSeed(std::uint32_t seed, int candidate) {
    std::uint32_t value = seed + 0x9e3779b9U * static_cast<std::uint32_t>(candidate + 1);
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

TownMode ChooseMode(std::mt19937& rng, const MapGenParams& params) {
    const auto getParam = [&params](const char* key, double fallback) {
        const auto it = params.extra.find(key);
        return it == params.extra.end() ? fallback : it->second;
    };
    const auto requestedIt = params.extra.find("townMode");
    if (requestedIt != params.extra.end() && requestedIt->second >= 0.0) {
        const int mode = std::clamp(static_cast<int>(std::lround(requestedIt->second)), 0, 5);
        return static_cast<TownMode>(mode);
    }

    std::discrete_distribution<int> distribution({
        std::max(0.0, getParam("townModeWeightGrid", getParam("townModeWeights.grid", 0.25))),
        std::max(0.0, getParam("townModeWeightVillage", getParam("townModeWeights.village", 0.20))),
        std::max(0.0, getParam("townModeWeightRiver", getParam("townModeWeights.river", 0.15))),
        std::max(0.0, getParam("townModeWeightWalled", getParam("townModeWeights.walled", 0.15))),
        std::max(0.0, getParam("townModeWeightMarket", getParam("townModeWeights.market", 0.15))),
        std::max(0.0, getParam("townModeWeightOrganic", getParam("townModeWeights.organic", 0.10))),
    });
    return static_cast<TownMode>(distribution(rng));
}

int ModeEntranceCount(TownMode mode) {
    switch (mode) {
        case TownMode::Village: return 3;
        case TownMode::Walled: return 2;
        case TownMode::Organic: return 3;
        default: return 4;
    }
}


void PaintRect(std::vector<std::uint8_t>& biomes, int width, int height, const Rect& rect, std::uint8_t tile) {
    for (int y = std::max(0, rect.y0); y < std::min(height, rect.y1); ++y) {
        for (int x = std::max(0, rect.x0); x < std::min(width, rect.x1); ++x) {
            biomes[static_cast<std::size_t>(Index(x, y, width))] = tile;
        }
    }
}

std::vector<Point> FindPath(
    const std::vector<std::uint8_t>& footprint,
    const std::vector<std::uint8_t>& structure,
    const std::vector<std::uint8_t>& biomes,
    int width,
    int height,
    Point start,
    Point goal) {
    if (!InBounds(start.x, start.y, width, height) || !InBounds(goal.x, goal.y, width, height)) return {};

    const int total = width * height;
    const int infinity = std::numeric_limits<int>::max() / 4;
    std::vector<int> distance(static_cast<std::size_t>(total), infinity);
    std::vector<int> previous(static_cast<std::size_t>(total), -1);
    using QueueItem = std::pair<int, int>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> pending;
    const int startIndex = Index(start.x, start.y, width);
    const int goalIndex = Index(goal.x, goal.y, width);
    distance[static_cast<std::size_t>(startIndex)] = 0;
    pending.push({0, startIndex});

    const std::array<int, 4> dx{1, -1, 0, 0};
    const std::array<int, 4> dy{0, 0, 1, -1};
    auto passable = [&](int x, int y) {
        const int next = Index(x, y, width);
        if (footprint[static_cast<std::size_t>(next)] == 0) return false;
        if (next == startIndex || next == goalIndex) return true;
        if (structure[static_cast<std::size_t>(next)] != 0) return false;
        const std::uint8_t biome = biomes[static_cast<std::size_t>(next)];
        return biome != MOUNTAIN && biome != EXMOUNTAIN && biome != SEA && !IsNaturalObstacle(biome);
    };

    while (!pending.empty()) {
        const auto [currentCost, current] = pending.top();
        pending.pop();
        if (currentCost != distance[static_cast<std::size_t>(current)]) continue;
        if (current == goalIndex) break;

        const int cx = current % width;
        const int cy = current / width;
        for (int direction = 0; direction < 4; ++direction) {
            const int nx = cx + dx[static_cast<std::size_t>(direction)];
            const int ny = cy + dy[static_cast<std::size_t>(direction)];
            if (!InBounds(nx, ny, width, height) || !passable(nx, ny)) continue;
            const int next = Index(nx, ny, width);
            const std::uint8_t biome = biomes[static_cast<std::size_t>(next)];
            const int stepCost = IsWater(biome) ? 12 : (biome == FOREST ? 2 : 1);
            const int nextCost = currentCost + stepCost;
            if (nextCost >= distance[static_cast<std::size_t>(next)]) continue;
            distance[static_cast<std::size_t>(next)] = nextCost;
            previous[static_cast<std::size_t>(next)] = current;
            pending.push({nextCost, next});
        }
    }

    if (distance[static_cast<std::size_t>(goalIndex)] == infinity) return {};
    std::vector<Point> result;
    for (int current = goalIndex; current >= 0; current = previous[static_cast<std::size_t>(current)]) {
        result.push_back({current % width, current / width});
        if (current == startIndex) break;
    }
    if (result.empty() || !SamePoint(result.back(), start)) return {};
    std::reverse(result.begin(), result.end());
    return result;
}

std::vector<Point> FindPathToRoad(
    const std::vector<std::uint8_t>& footprint,
    const std::vector<std::uint8_t>& structure,
    const std::vector<std::uint8_t>& biomes,
    int width,
    int height,
    Point start,
    const Rect* extraBlocked) {
    if (!InBounds(start.x, start.y, width, height)) return {};
    const int total = width * height;
    std::vector<int> previous(static_cast<std::size_t>(total), -1);
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(total), 0);
    std::queue<int> pending;
    const int startIndex = Index(start.x, start.y, width);
    pending.push(startIndex);
    visited[static_cast<std::size_t>(startIndex)] = 1;
    int goal = IsRoadLike(biomes[static_cast<std::size_t>(startIndex)]) ? startIndex : -1;
    const std::array<int, 4> dx{1, -1, 0, 0};
    const std::array<int, 4> dy{0, 0, 1, -1};
    auto blocked = [&](int x, int y) {
        if (extraBlocked != nullptr && Contains(*extraBlocked, x, y)) return true;
        const int index = Index(x, y, width);
        if (structure[static_cast<std::size_t>(index)] != 0) return true;
        const std::uint8_t biome = biomes[static_cast<std::size_t>(index)];
        return biome == MOUNTAIN || biome == EXMOUNTAIN || biome == SEA || IsNaturalObstacle(biome);
    };

    while (!pending.empty() && goal < 0) {
        const int current = pending.front();
        pending.pop();
        const int cx = current % width;
        const int cy = current / width;
        for (int direction = 0; direction < 4; ++direction) {
            const int nx = cx + dx[static_cast<std::size_t>(direction)];
            const int ny = cy + dy[static_cast<std::size_t>(direction)];
            if (!InBounds(nx, ny, width, height)) continue;
            const int next = Index(nx, ny, width);
            if (visited[static_cast<std::size_t>(next)] != 0 ||
                footprint[static_cast<std::size_t>(next)] == 0 || blocked(nx, ny)) continue;
            visited[static_cast<std::size_t>(next)] = 1;
            previous[static_cast<std::size_t>(next)] = current;
            if (IsRoadLike(biomes[static_cast<std::size_t>(next)])) {
                goal = next;
                break;
            }
            pending.push(next);
        }
    }

    if (goal < 0) return {};
    std::vector<Point> result;
    for (int current = goal; current >= 0; current = previous[static_cast<std::size_t>(current)]) {
        result.push_back({current % width, current / width});
        if (current == startIndex) break;
    }
    if (result.empty() || !SamePoint(result.back(), start)) return {};
    std::reverse(result.begin(), result.end());
    return result;
}

void PaintRoadPath(
    std::vector<std::uint8_t>& biomes,
    const std::vector<std::uint8_t>& footprint,
    const std::vector<std::uint8_t>& structure,
    int width,
    int height,
    const std::vector<Point>& path,
    int roadWidth,
    std::vector<Point>* waterCrossings) {
    const int offsetMin = -(roadWidth / 2);
    const int offsetMax = offsetMin + roadWidth - 1;
    for (const Point& center : path) {
        for (int oy = offsetMin; oy <= offsetMax; ++oy) {
            for (int ox = offsetMin; ox <= offsetMax; ++ox) {
                const int x = center.x + ox;
                const int y = center.y + oy;
                if (!InBounds(x, y, width, height)) continue;
                const int index = Index(x, y, width);
                if (footprint[static_cast<std::size_t>(index)] == 0 ||
                    (structure[static_cast<std::size_t>(index)] != 0 && !SamePoint(center, {x, y}))) continue;
                const std::uint8_t before = biomes[static_cast<std::size_t>(index)];
                if (IsNaturalObstacle(before)) continue;
                if ((before == MOUNTAIN || before == EXMOUNTAIN || before == SEA) && !SamePoint(center, {x, y})) continue;
                if (before == RIVER || before == LAKE) {
                    biomes[static_cast<std::size_t>(index)] = BRIDGE;
                    if (waterCrossings != nullptr) waterCrossings->push_back({x, y});
                } else if (before != BRIDGE) {
                    biomes[static_cast<std::size_t>(index)] = ROAD;
                }
            }
        }
    }
}

bool IsBoundaryCell(const std::vector<std::uint8_t>& footprint, int width, int height, int x, int y) {
    if (!InBounds(x, y, width, height) || footprint[static_cast<std::size_t>(Index(x, y, width))] == 0) return false;
    const std::array<int, 4> dx{1, -1, 0, 0};
    const std::array<int, 4> dy{0, 0, 1, -1};
    for (int direction = 0; direction < 4; ++direction) {
        const int nx = x + dx[static_cast<std::size_t>(direction)];
        const int ny = y + dy[static_cast<std::size_t>(direction)];
        if (!InBounds(nx, ny, width, height) || footprint[static_cast<std::size_t>(Index(nx, ny, width))] == 0) return true;
    }
    return false;
}

std::vector<Point> BoundaryCells(const std::vector<std::uint8_t>& footprint, int width, int height) {
    std::vector<Point> result;
    std::vector<std::uint8_t> exterior(footprint.size(), 0);
    std::queue<int> pending;
    const auto visit = [&](int i) {
        if (footprint[i] || exterior[i]) return;
        exterior[i] = 1; pending.push(i);
    };
    for (int x = 0; x < width; ++x) { visit(x); visit((height - 1) * width + x); }
    for (int y = 0; y < height; ++y) { visit(y * width); visit(y * width + width - 1); }
    while (!pending.empty()) {
        const int i = pending.front(); pending.pop();
        if (i % width > 0) visit(i - 1);
        if (i % width + 1 < width) visit(i + 1);
        if (i >= width) visit(i - width);
        if (i + width < width * height) visit(i + width);
    }
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const int i = Index(x, y, width);
            if (IsBoundaryCell(footprint, width, height, x, y) && (exterior[i - 1] || exterior[i + 1] || exterior[i - width] || exterior[i + width])) result.push_back({x, y});
        }
    }
    return result;
}

Point NearestFootprintPoint(
    const std::vector<std::uint8_t>& footprint,
    const std::vector<std::uint8_t>& biomes,
    int width,
    int height,
    Point requested) {
    int bestDistance = std::numeric_limits<int>::max();
    Point best = requested;
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const int index = Index(x, y, width);
            if (footprint[static_cast<std::size_t>(index)] == 0 ||
                IsWater(biomes[static_cast<std::size_t>(index)]) || IsNaturalObstacle(biomes[static_cast<std::size_t>(index)])) continue;
            const int distance = Manhattan({x, y}, requested);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = {x, y};
            }
        }
    }
    return best;
}

}  // namespace

TownMapImageGenerator::TownMapImageGenerator(MapGenParams params) : MapImageGenerator(std::move(params)) {}

MapGenColor TownMapImageGenerator::BiomeToColor(std::uint8_t biome) const {
    switch (biome) {
        case PLAIN:      return {104, 146, 86};
        case DESERT:     return {196, 168, 103};
        case FOREST:     return {54, 122, 65};
        case MOUNTAIN:   return {124, 90, 67};
        case EXMOUNTAIN: return {77, 54, 49};
        case ROAD:       return {154, 116, 76};
        case BRIDGE:     return {218, 176, 74};
        case RIVER:      return {60, 150, 210};
        case LAKE:       return {40, 90, 180};
        case SEA:        return {22, 28, 38};
        case TOWN_FLOOR:       return {183, 151, 96};
        case TOWN_WOOD_FLOOR:  return {151, 105, 66};
        case TOWN_STONE_FLOOR: return {157, 157, 148};
        case TOWN_FURNITURE:   return {83, 61, 43};
        case TOWN_PAVEMENT:    return {132, 126, 116};
        case TOWN_SEA:         return {38, 103, 155};
        case TOWN_CLIFF:       return {100, 103, 113};
        case TOWN_TREES:       return {25, 69, 42};
        default:         return {255, 0, 255};
    }
}

bool TownMapImageGenerator::Generate(MapPlan& outPlan, std::string& outError) {
    const int width = params_.width;
    const int height = params_.depth;
    if (width < 32 || height < 32) {
        outError = "Town map size must be at least 32x32";
        return false;
    }

    const int area = width * height;
    const double scale = static_cast<double>(std::min(width, height)) / 160.0;
    // Use map area for the default building budget so a long, narrow map is
    // not treated like a tiny square merely because its short side is small.
    // The square-root form keeps the density progression comparable to the
    // existing square-map behavior.
    const double mapAreaScale = std::sqrt(static_cast<double>(area) / (160.0 * 160.0));
    const double areaScale = static_cast<double>(area) / (160.0 * 160.0);
    const bool villageLayout = GetParam("settlementType", 0.0) >= 0.5;
    const TownCityType cityType = villageLayout ? TownCityType::MedievalCity : ChooseCityType(params_);
    const SettlementSite site = static_cast<SettlementSite>(std::clamp(static_cast<int>(std::lround(
        GetParam("settlementSite", CandidateSeed(params_.seed, 809) % 7U))), 0, 6));
    const int siteRotation = std::clamp(static_cast<int>(std::lround(GetParam("siteRotation", CandidateSeed(params_.seed, 811) % 4U))), 0, 3);
    const TownVisualStyle visualStyle = TownVisualStyleFor(cityType, villageLayout);
    const double plazaChance = std::clamp(GetParam("plazaChance", visualStyle.plazaChance), 0.0, 1.0);
    const double pavingTargetRatio = std::clamp(
        GetParam("pavingRatio", GetParam("pavingRate", visualStyle.pavingRatio)), 0.0, 1.0);
    const bool pavedCity = !villageLayout && cityType == TownCityType::PavedCity;
    const bool fortifiedCity = !villageLayout && (cityType == TownCityType::WalledCity || cityType == TownCityType::CastleTown);
    // Keep the default settlement density roughly stable as maps grow. The
    // upper caps are deliberately generous: a 1024x1024 town must gain real
    // neighbourhoods rather than stretching the 160x160 layout into empty
    // grassland. Explicit ranges remain bounded by the same caps.
    const double buildingDensityMin = villageLayout ? 8.0 :
        (cityType == TownCityType::MedievalCity ? 24.0 :
        (cityType == TownCityType::PavedCity ? 24.0 :
        (cityType == TownCityType::WalledCity ? 22.0 : 20.0)));
    const double buildingDensityMax = villageLayout ? 20.0 :
        (cityType == TownCityType::MedievalCity ? 48.0 :
        (cityType == TownCityType::PavedCity ? 46.0 :
        (cityType == TownCityType::WalledCity ? 42.0 : 38.0)));
    const int defaultBuildingMin = std::clamp(static_cast<int>(std::lround(buildingDensityMin * areaScale)), 4, 128);
    const int defaultBuildingMax = std::clamp(static_cast<int>(std::lround(buildingDensityMax * areaScale)), defaultBuildingMin, 256);
    int buildingMin = std::clamp(static_cast<int>(std::lround(GetParam("buildingCountMin", defaultBuildingMin))), 4, 256);
    int buildingMax = std::clamp(static_cast<int>(std::lround(GetParam("buildingCountMax", defaultBuildingMax))), buildingMin, 256);
    if (params_.extra.count("buildingCount") != 0) {
        buildingMax = std::clamp(static_cast<int>(std::lround(GetParam("buildingCount", buildingMax))), 4, 256);
        buildingMin = std::min(buildingMin, buildingMax);
    }
    const int candidateCount = std::clamp(static_cast<int>(std::lround(GetParam("candidateCount", 6.0))), 1, 12);
    const int border = std::clamp(static_cast<int>(std::lround(GetParam("border", 5.0))), 3, std::min(width, height) / 5);
    const bool mainRoadWidthWasExplicit = params_.extra.count("mainRoadWidth") != 0 || params_.extra.count("roadHalfWidth") != 0;
    int mainRoadWidth = std::clamp(static_cast<int>(std::lround(GetParam("mainRoadWidth", 6.0))), 2, 6);
    if (params_.extra.count("roadHalfWidth") != 0) {
        mainRoadWidth = std::clamp(static_cast<int>(std::lround(GetParam("roadHalfWidth", 3.0))) * 2, 2, 6);
    }
    const bool districtRoadWidthWasExplicit = params_.extra.count("districtRoadWidth") != 0;
    int districtRoadWidth = std::clamp(static_cast<int>(std::lround(GetParam("districtRoadWidth", 4.0))), 3, 5);
    int alleyWidth = std::clamp(static_cast<int>(std::lround(GetParam("alleyWidth", 2.0))), 2, 5);
    // Road widths are tile widths, not half-widths. Keep the defaults stable
    // across map sizes so that a 2/4/6 road remains visually intentional.
    const int roadWidthBoost = std::clamp(static_cast<int>(std::lround(scale)) - 1, 0, 1);
    if (!mainRoadWidthWasExplicit) mainRoadWidth = 6;
    if (!districtRoadWidthWasExplicit) districtRoadWidth = std::clamp(districtRoadWidth + roadWidthBoost, 3, 5);
    const double extraEdgeRate = std::clamp(GetParam("roadExtraEdgeRate", 0.0), 0.0, 1.0);
    const double districtDensityMin = villageLayout ? 4.0 :
        ((cityType == TownCityType::MedievalCity || cityType == TownCityType::PavedCity) ? 6.0 : 4.0);
    const double districtDensityMax = villageLayout ? 8.0 :
        ((cityType == TownCityType::MedievalCity || cityType == TownCityType::PavedCity) ? 12.0 : 8.0);
    const int defaultDistrictMin = std::clamp(static_cast<int>(std::lround(districtDensityMin * scale)), 2, 32);
    const int defaultDistrictMax = std::clamp(static_cast<int>(std::lround(districtDensityMax * scale)), defaultDistrictMin, 48);
    const int districtMin = std::clamp(static_cast<int>(std::lround(GetParam("districtCountMin", defaultDistrictMin))), 2, 32);
    const int districtMax = std::clamp(static_cast<int>(std::lround(GetParam("districtCountMax", defaultDistrictMax))), districtMin, 48);
    const int defaultParkMin = std::clamp(static_cast<int>(std::lround(mapAreaScale)), 1, 12);
    const int defaultParkMax = std::clamp(static_cast<int>(std::lround(3.0 * mapAreaScale)), defaultParkMin, 16);
    int parkMin = std::clamp(static_cast<int>(std::lround(GetParam("parkCountMin", defaultParkMin))), 0, 16);
    int parkMax = std::clamp(static_cast<int>(std::lround(GetParam("parkCountMax", defaultParkMax))), parkMin, 20);
    if (params_.extra.count("parkCount") != 0) {
        parkMin = std::clamp(static_cast<int>(std::lround(GetParam("parkCount", 2.0))), 0, 20);
        parkMax = parkMin;
    }
    const double waterChance = std::clamp(GetParam("waterFeatureChance", 0.20), 0.0, 1.0);

    struct CandidateResult {
        MapPlan plan;
        MapQualityMetrics quality;
        double score = -std::numeric_limits<double>::infinity();
    };
    CandidateResult best;
    bool generated = false;
    int mostBuildingsPlaced = 0;
    int smallestBuildingMinimum = buildingMin;
    const bool modeWasExplicit = params_.extra.find("townMode") != params_.extra.end();
    const bool terrainWasExplicit = params_.extra.find("townTerrain") != params_.extra.end();

    for (int candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex) {
        std::mt19937 rng(CandidateSeed(params_.seed, candidateIndex));
        TownMode mode = ChooseMode(rng, params_);
        if (!modeWasExplicit && !villageLayout && candidateCount >= 5 && candidateIndex < 5) {
            // Urban town candidates deliberately exclude the rural village
            // branch. Village is now a standalone map type.
            static const std::array<TownMode, 5> urbanModes{{
                TownMode::Grid, TownMode::River, TownMode::Walled, TownMode::Market, TownMode::Organic,
            }};
            const int offset = static_cast<int>(CandidateSeed(params_.seed, 0) % urbanModes.size());
            mode = urbanModes[static_cast<std::size_t>((offset + candidateIndex) % urbanModes.size())];
        } else if (!modeWasExplicit && !villageLayout && mode == TownMode::Village) {
            mode = TownMode::Market;
        }
        const bool urban = !villageLayout && mode != TownMode::Village;
        MapPlan candidate;
        candidate.Reset(width, height, params_.seed, villageLayout ? "village" : "town");
        candidate.archetype = TownModeName(mode);
        candidate.cityType = villageLayout ? "village" : TownCityTypeName(cityType);
        candidate.plazaEnabled = std::uniform_real_distribution<double>(0.0, 1.0)(rng) < plazaChance;
        candidate.plazaStyle = visualStyle.plazaStyle;
        candidate.pavingTargetRatio = pavingTargetRatio;
        const int terrainIndex = terrainWasExplicit
            ? std::clamp(static_cast<int>(std::lround(GetParam("townTerrain", 0.0))), 0, 2)
            : (site == SettlementSite::Oasis ? 1 : (site == SettlementSite::Forest ? 2 : 0));
        const TownTerrain terrain = static_cast<TownTerrain>(terrainIndex);
        const std::uint8_t groundTile = TownTerrainTile(terrain);
        candidate.environment = TownTerrainName(terrain);
        candidate.biomes.assign(static_cast<std::size_t>(area), static_cast<std::uint8_t>(SEA));
        std::vector<std::uint8_t>& biomes = candidate.biomes;
        std::vector<std::uint8_t> footprint(static_cast<std::size_t>(area), 0);
        std::vector<std::uint8_t> structure(static_cast<std::size_t>(area), 0);
        std::vector<std::uint8_t> plazaMask(static_cast<std::size_t>(area), 0);
        std::vector<std::uint8_t> pavingRoadClass(static_cast<std::size_t>(area), 0);
        int nextRegionId = 1;

        const int centerX = width / 2;
        const int centerY = height / 2;
        std::uniform_real_distribution<double> unit(-1.0, 1.0);
        const int unconstrainedLand = static_cast<int>(std::lround(area * (villageLayout ? 0.36 : 0.74)));
        const bool optionalRiver = site == SettlementSite::Plains &&
            ((modeWasExplicit && GetParam("townMode", 0.0) == 2.0) ||
                static_cast<double>(CandidateSeed(params_.seed, 1201) % 10000U) / 10000.0 < waterChance);
        const bool optionalPond = (site == SettlementSite::Plains || site == SettlementSite::Forest) &&
            static_cast<double>(CandidateSeed(params_.seed, 1203) % 10000U) / 10000.0 < waterChance;
        PrepareSettlementSite(candidate, footprint, structure, site, siteRotation, groundTile, villageLayout, optionalRiver, optionalPond);
        const int footprintCells = std::accumulate(footprint.begin(), footprint.end(), 0);
        if (footprintCells < area / 8) continue;
        int buildableLand = 0;
        for (int i = 0; i < area; ++i) if (footprint[i] && !structure[i] && biomes[i] == groundTile) ++buildableLand;
        // The population budget follows habitable ground, not ocean or cliffs.
        // Preserve explicit counts; only scale the implicit density defaults.
        const double landScale = std::min(1.0, static_cast<double>(buildableLand) / std::max(1, unconstrainedLand));
        const int siteBuildingMin = params_.extra.count("buildingCountMin") || params_.extra.count("buildingCount")
            ? buildingMin : std::max(4, static_cast<int>(std::lround(buildingMin * landScale)));
        const int siteBuildingMax = params_.extra.count("buildingCountMax") || params_.extra.count("buildingCount")
            ? std::max(siteBuildingMin, buildingMax) : std::max(siteBuildingMin, static_cast<int>(std::lround(buildingMax * landScale)));
        smallestBuildingMinimum = std::min(smallestBuildingMinimum, siteBuildingMin);

        const int plazaWidth = std::clamp(static_cast<int>(std::lround(GetParam(
            "plazaWidth", (mode == TownMode::Market ? 0.25 : 0.18) * std::min(width, height)))), 8, std::min(width, height) / 2);
        const int plazaHeight = std::clamp(static_cast<int>(std::lround(GetParam(
            "plazaHeight", (mode == TownMode::Market ? 0.20 : 0.16) * std::min(width, height)))), 8, std::min(width, height) / 2);
        Point plazaCenter{
            std::clamp(centerX + static_cast<int>(std::lround(unit(rng) * width * 0.08)), border + plazaWidth / 2 + 1, width - border - plazaWidth / 2 - 2),
            std::clamp(centerY + static_cast<int>(std::lround(unit(rng) * height * 0.08)), border + plazaHeight / 2 + 1, height - border - plazaHeight / 2 - 2),
        };
        plazaCenter = NearestFootprintPoint(footprint, biomes, width, height, plazaCenter);
        // Reserve a dry clearing before painting: a dry centre pixel alone
        // cannot justify damming a river or levelling a grove for a square.
        const int reservedWidth = candidate.plazaEnabled ? plazaWidth : 3;
        const int reservedHeight = candidate.plazaEnabled ? plazaHeight : 3;
        int plazaDistance = std::numeric_limits<int>::max();
        Point dryCenter = plazaCenter;
        for (int y = reservedHeight / 2 + border; y < height - reservedHeight / 2 - border - 1; ++y) {
            for (int x = reservedWidth / 2 + border; x < width - reservedWidth / 2 - border - 1; ++x) {
                const int distance = Manhattan({x, y}, plazaCenter);
                if (distance >= plazaDistance) continue;
                bool clear = true;
                for (int py = y - reservedHeight / 2; py < y + (reservedHeight + 1) / 2 && clear; ++py) {
                    for (int px = x - reservedWidth / 2; px < x + (reservedWidth + 1) / 2; ++px) {
                        const int i = Index(px, py, width);
                        if (!footprint[i] || structure[i] || biomes[i] != groundTile) { clear = false; break; }
                    }
                }
                if (clear) { dryCenter = {x, y}; plazaDistance = distance; }
            }
        }
        if (plazaDistance == std::numeric_limits<int>::max()) continue;
        plazaCenter = dryCenter;
        const Rect plaza{
            plazaCenter.x - plazaWidth / 2,
            plazaCenter.y - plazaHeight / 2,
            plazaCenter.x + (plazaWidth + 1) / 2,
            plazaCenter.y + (plazaHeight + 1) / 2,
        };
        if (candidate.plazaEnabled) {
            for (int y = plaza.y0; y < plaza.y1; ++y) {
                for (int x = plaza.x0; x < plaza.x1; ++x) {
                    if (!InBounds(x, y, width, height) || footprint[static_cast<std::size_t>(Index(x, y, width))] == 0) continue;
                    biomes[static_cast<std::size_t>(Index(x, y, width))] = visualStyle.plazaTile;
                    plazaMask[static_cast<std::size_t>(Index(x, y, width))] = 1;
                }
            }
            candidate.regions.push_back({"plaza", 0, plaza.x0, plaza.y0, plaza.x1 - 1, plaza.y1 - 1});
            candidate.regions.back().role = "town_center";

            const bool useStoneFeature = cityType == TownCityType::PavedCity;
            const int featureSize = useStoneFeature ? 2 : 3;
            const Rect feature{
                plazaCenter.x - featureSize / 2,
                plazaCenter.y - featureSize / 2,
                plazaCenter.x + (featureSize + 1) / 2,
                plazaCenter.y + (featureSize + 1) / 2,
            };
            for (int y = feature.y0; y < feature.y1; ++y) {
                for (int x = feature.x0; x < feature.x1; ++x) {
                    if (!InBounds(x, y, width, height) || plazaMask[static_cast<std::size_t>(Index(x, y, width))] == 0) continue;
                    biomes[static_cast<std::size_t>(Index(x, y, width))] = static_cast<std::uint8_t>(useStoneFeature ? TOWN_STONE_FLOOR : TOWN_FURNITURE);
                }
            }
            const int featureId = nextRegionId++;
            candidate.regions.push_back({"plaza_feature", featureId, feature.x0, feature.y0, feature.x1 - 1, feature.y1 - 1});
            candidate.regions.back().role = visualStyle.plazaStyle;
            candidate.markers.push_back({"plaza_feature", plazaCenter.x, plazaCenter.y, featureId});
            candidate.markers.back().role = visualStyle.plazaStyle;
        } else {
            const int centerRadius = 1;
            const Rect centerSpace{
                plazaCenter.x - centerRadius,
                plazaCenter.y - centerRadius,
                plazaCenter.x + centerRadius + 1,
                plazaCenter.y + centerRadius + 1,
            };
            for (int y = centerSpace.y0; y < centerSpace.y1; ++y) {
                for (int x = centerSpace.x0; x < centerSpace.x1; ++x) {
                    if (!InBounds(x, y, width, height) || footprint[static_cast<std::size_t>(Index(x, y, width))] == 0) continue;
                    plazaMask[static_cast<std::size_t>(Index(x, y, width))] = 1;
                }
            }
            candidate.regions.push_back({"town_center", 0, centerSpace.x0, centerSpace.y0, centerSpace.x1 - 1, centerSpace.y1 - 1});
            candidate.regions.back().role = "town_center";
        }
        candidate.markers.push_back({"landmark", plazaCenter.x, plazaCenter.y, 0});
        candidate.markers.back().role = "town_center";


        // Find real dry routes from the map edges to the settlement perimeter.
        // Terrain outside development remains visible and is not a SEA sentinel.
        std::vector<int> exteriorDistance(area, -1), exteriorNext(area, -1);
        std::queue<int> exteriorQueue;
        const auto exteriorPassable = [&](int i) {
            return !footprint[i] && !structure[i] && !IsWater(biomes[i]) && !IsNaturalObstacle(biomes[i]);
        };
        const auto seedExterior = [&](int i) {
            if (!exteriorPassable(i) || exteriorDistance[i] >= 0) return;
            exteriorDistance[i] = 0; exteriorQueue.push(i);
        };
        for (int x = 0; x < width; ++x) { seedExterior(x); seedExterior((height - 1) * width + x); }
        for (int y = 0; y < height; ++y) { seedExterior(y * width); seedExterior(y * width + width - 1); }
        while (!exteriorQueue.empty()) {
            const int i = exteriorQueue.front(); exteriorQueue.pop();
            for (const Point offset : {Point{-1,0}, Point{1,0}, Point{0,-1}, Point{0,1}}) {
                const int x = i % width + offset.x, y = i / width + offset.y;
                if (!InBounds(x, y, width, height)) continue;
                const int n = Index(x, y, width);
                if (!exteriorPassable(n) || exteriorDistance[n] >= 0) continue;
                exteriorDistance[n] = exteriorDistance[i] + 1; exteriorNext[n] = i; exteriorQueue.push(n);
            }
        }
        const auto exteriorNeighbor = [&](const Point& point) {
            int best = -1;
            for (const Point offset : {Point{-1,0}, Point{1,0}, Point{0,-1}, Point{0,1}}) {
                const int x = point.x + offset.x, y = point.y + offset.y;
                if (!InBounds(x, y, width, height)) continue;
                const int n = Index(x, y, width);
                if (exteriorDistance[n] >= 0 && (best < 0 || exteriorDistance[n] < exteriorDistance[best])) best = n;
            }
            return best;
        };
        const std::vector<Point> boundary = BoundaryCells(footprint, width, height);
        if (boundary.size() < 2) continue;
        std::vector<Point> entrances;
        std::vector<int> sideOrder{0, 1, 2, 3};
        std::shuffle(sideOrder.begin(), sideOrder.end(), rng);
        const bool outerWall = mode == TownMode::Walled || fortifiedCity;
        const int entranceCount = std::min(outerWall ? 2 : ModeEntranceCount(mode), static_cast<int>(boundary.size()));
        const std::array<Point, 4> sideTargets{{
            {centerX, border}, {centerX, height - border - 1}, {border, centerY}, {width - border - 1, centerY},
        }};
        const auto landEntrance = [&](const Point& point) {
            const int i = Index(point.x, point.y, width);
            if (IsWater(biomes[i]) || structure[i]) return false;
            if (site == SettlementSite::MountainValley) {
                // An undeveloped patch beside the valley is not a route over
                // the mountain. Use the approaches reaching the valley ends.
                const double along = siteRotation % 2 ? static_cast<double>(point.x) / (width - 1)
                    : static_cast<double>(point.y) / (height - 1);
                if (along > .08 && along < .92) return false;
            }
            return exteriorNeighbor(point) >= 0;
        };
        for (int sideIndex : sideOrder) {
            if (static_cast<int>(entrances.size()) >= entranceCount) break;
            std::vector<Point> candidates;
            for (const Point& point : boundary) {
                const bool preferred = sideIndex == 0 ? point.y <= centerY
                    : sideIndex == 1 ? point.y >= centerY
                    : sideIndex == 2 ? point.x <= centerX
                    : point.x >= centerX;
                if (preferred && landEntrance(point)) candidates.push_back(point);
            }
            if (candidates.empty()) continue;
            std::sort(candidates.begin(), candidates.end(), [&](const Point& a, const Point& b) {
                return Manhattan(a, sideTargets[static_cast<std::size_t>(sideIndex)]) < Manhattan(b, sideTargets[static_cast<std::size_t>(sideIndex)]);
            });
            const int choiceCount = std::max(1, static_cast<int>(candidates.size()) / 3);
            const Point selected = candidates[static_cast<std::size_t>(std::uniform_int_distribution<int>(0, choiceCount - 1)(rng))];
            bool tooClose = false;
            for (const Point& existing : entrances) {
                if (Manhattan(existing, selected) < std::min(width, height) / 4) tooClose = true;
            }
            if (!tooClose) entrances.push_back(selected);
        }
        for (const Point& point : boundary) {
            if (static_cast<int>(entrances.size()) >= entranceCount) break;
            if (!landEntrance(point)) continue;
            bool tooClose = false;
            for (const Point& existing : entrances) {
                if (Manhattan(existing, point) < std::min(width, height) / 5) tooClose = true;
            }
            if (!tooClose) entrances.push_back(point);
        }
        if (entrances.size() < 2) continue;

        if (outerWall) {
            for (const Point& point : boundary) {
                const int i = Index(point.x, point.y, width);
                if (!IsWater(biomes[i]) && !IsNaturalObstacle(biomes[i])) biomes[i] = MOUNTAIN;
            }
            candidate.regions.push_back({"wall", 0, border, border, width - border - 1, height - border - 1});
            candidate.regions.back().role = "town_wall";
        }
        for (std::size_t i = 0; i < entrances.size(); ++i) {
            const Point& point = entrances[i];
            biomes[static_cast<std::size_t>(Index(point.x, point.y, width))] = BRIDGE;
            candidate.markers.push_back({"entrance", point.x, point.y, static_cast<int>(i)});
            candidate.markers.back().role = "town_entrance";
        }

        auto canPlaceRect = [&](const Rect& rect, int margin) {
            if (rect.x0 <= border || rect.y0 <= border || rect.x1 >= width - border || rect.y1 >= height - border) return false;
            for (int y = rect.y0 - margin; y < rect.y1 + margin; ++y) {
                for (int x = rect.x0 - margin; x < rect.x1 + margin; ++x) {
                    if (!InBounds(x, y, width, height)) return false;
                    const int index = Index(x, y, width);
                    if (footprint[static_cast<std::size_t>(index)] == 0 || structure[static_cast<std::size_t>(index)] != 0 ||
                        IsWater(biomes[static_cast<std::size_t>(index)]) || IsNaturalObstacle(biomes[static_cast<std::size_t>(index)])) return false;
                    if (Contains(rect, x, y) && biomes[static_cast<std::size_t>(index)] != groundTile) return false;
                }
            }
            for (int y = rect.y0; y < rect.y1; ++y) {
                for (int x = rect.x0; x < rect.x1; ++x) {
                    if (plazaMask[static_cast<std::size_t>(Index(x, y, width))] != 0) return false;
                }
            }
            return true;
        };

        auto paintBuilding = [&](const std::string& kind, const Rect& rect, Point target, Point preferredOutside, bool hasPreferred, bool landmark, const HouseholdProfile& household, Building& outBuilding) {
            struct DoorCandidate {
                Point door;
                Point outside;
                int score = 0;
            };
            std::vector<DoorCandidate> doorCandidates;
            for (int y = rect.y0; y < rect.y1; ++y) {
                for (int x = rect.x0; x < rect.x1; ++x) {
                    if (x != rect.x0 && x != rect.x1 - 1 && y != rect.y0 && y != rect.y1 - 1) continue;
                    if ((x == rect.x0 || x == rect.x1 - 1) && (y == rect.y0 || y == rect.y1 - 1)) continue;
                    Point outside{x, y};
                    if (x == rect.x0) --outside.x;
                    else if (x == rect.x1 - 1) ++outside.x;
                    else if (y == rect.y0) --outside.y;
                    else ++outside.y;
                    if (!InBounds(outside.x, outside.y, width, height) || footprint[static_cast<std::size_t>(Index(outside.x, outside.y, width))] == 0 ||
                        IsWater(biomes[static_cast<std::size_t>(Index(outside.x, outside.y, width))])) continue;
                    int score = Manhattan(outside, target);
                    if (hasPreferred && SamePoint(outside, preferredOutside)) score -= 100000;
                    if (IsRoadLike(biomes[static_cast<std::size_t>(Index(outside.x, outside.y, width))])) score -= 1000;
                    doorCandidates.push_back({{x, y}, outside, score});
                }
            }
            if (doorCandidates.empty()) return false;
            std::sort(doorCandidates.begin(), doorCandidates.end(), [](const DoorCandidate& a, const DoorCandidate& b) { return a.score < b.score; });
            const DoorCandidate& selected = doorCandidates.front();
            const std::uint8_t interiorTile = BuildingFloorTile(kind);
            PaintRect(biomes, width, height, rect, interiorTile);
            for (int y = rect.y0; y < rect.y1; ++y) {
                for (int x = rect.x0; x < rect.x1; ++x) {
                    if (x == rect.x0 || x == rect.x1 - 1 || y == rect.y0 || y == rect.y1 - 1) biomes[static_cast<std::size_t>(Index(x, y, width))] = MOUNTAIN;
                    structure[static_cast<std::size_t>(Index(x, y, width))] = 1;
                }
            }
            biomes[static_cast<std::size_t>(Index(selected.door.x, selected.door.y, width))] = BRIDGE;
            outBuilding = {kind, rect, selected.door, selected.outside, nextRegionId++, landmark};
            outBuilding.household = household;
            const std::string regionKind = IsCommercialBuilding(kind) ? "market" : "building";
            candidate.regions.push_back({regionKind, outBuilding.regionId, rect.x0, rect.y0, rect.x1 - 1, rect.y1 - 1});
            MapPlanRegion& buildingRegion = candidate.regions.back();
            buildingRegion.role = kind;
            buildingRegion.householdId = household.householdId;
            buildingRegion.residentProfile = household.residentProfile;
            buildingRegion.residentCount = household.residentCount;
            buildingRegion.workerCount = household.workerCount;
            buildingRegion.livingArrangement = household.livingArrangement;
            buildingRegion.waterSource = household.waterSource;
            buildingRegion.hearth = household.hearth;
            buildingRegion.linkedRegionId = household.linkedRegionId;
            const int buildingRegionIndex = static_cast<int>(candidate.regions.size()) - 1;
            candidate.markers.push_back({"building_door", selected.door.x, selected.door.y, outBuilding.regionId});
            candidate.markers.back().regionId = outBuilding.regionId;
            candidate.markers.back().role = "public_entry";
            if (landmark) {
                candidate.markers.push_back({"landmark", rect.CenterX(), rect.CenterY(), outBuilding.regionId});
                candidate.markers.back().role = kind;
            }

            const BuildingProfile profile = ProfileForBuilding(kind, urban, std::min(width, height));
            const int innerX0 = rect.x0 + 1;
            const int innerY0 = rect.y0 + 1;
            const int innerX1 = rect.x1 - 1;
            const int innerY1 = rect.y1 - 1;
            auto addFurniture = [&](const char* furnitureKind, Rect furnitureRect) {
                furnitureRect.x0 = std::max(furnitureRect.x0, innerX0);
                furnitureRect.y0 = std::max(furnitureRect.y0, innerY0);
                furnitureRect.x1 = std::min(furnitureRect.x1, innerX1);
                furnitureRect.y1 = std::min(furnitureRect.y1, innerY1);
                if (furnitureRect.Width() <= 0 || furnitureRect.Height() <= 0) return;
                PaintRect(biomes, width, height, furnitureRect, TOWN_FURNITURE);
                const int furnitureId = nextRegionId++;
                candidate.regions.push_back({"furniture", furnitureId, furnitureRect.x0, furnitureRect.y0, furnitureRect.x1 - 1, furnitureRect.y1 - 1,
                    outBuilding.regionId, buildingRegionIndex, std::string("furniture_") + furnitureKind});
                candidate.markers.push_back({"furniture", furnitureRect.CenterX(), furnitureRect.CenterY(), furnitureId,
                    furnitureId, static_cast<int>(candidate.regions.size()) - 1, std::string("furniture_") + furnitureKind});
            };
            if (profile.furnitureCount > 0) {
                if (kind == "house" || kind == "cottage" || kind == "mayor_house" || kind == "manor") {
                    addFurniture("bed", {innerX0, innerY0, innerX0 + 2, innerY0 + 2});
                    addFurniture("table", {rect.CenterX() - 1, rect.CenterY() - 1, rect.CenterX() + 1, rect.CenterY()});
                    if (profile.furnitureCount >= 4) addFurniture("chest", {innerX1 - 2, innerY1 - 1, innerX1, innerY1});
                } else if (kind == "inn" || kind == "caravanserai") {
                    addFurniture("counter", {innerX0, rect.CenterY() - 1, innerX0 + 3, rect.CenterY()});
                    addFurniture("bed", {innerX1 - 3, innerY0, innerX1 - 1, innerY0 + 2});
                    addFurniture("bed", {innerX1 - 3, innerY1 - 2, innerX1 - 1, innerY1});
                    addFurniture("table", {rect.CenterX() - 1, rect.CenterY() - 1, rect.CenterX() + 1, rect.CenterY()});
                } else if (kind == "fishery" || kind == "lumbermill" || kind == "mine_office") {
                    const char* equipment = kind == "fishery" ? "net_rack" : (kind == "lumbermill" ? "sawbench" : "ore_crates");
                    addFurniture(equipment, {innerX0, innerY0, innerX0 + 3, innerY0 + 2});
                    addFurniture("workbench", {rect.CenterX() - 1, rect.CenterY(), rect.CenterX() + 2, rect.CenterY() + 1});
                } else if (IsCommercialBuilding(kind) || kind == "warehouse" || kind == "barn") {
                    addFurniture("counter", {innerX0, innerY0, std::min(innerX0 + 3, innerX1), std::min(innerY0 + 1, innerY1)});
                    addFurniture(kind == "weapon_shop" ? "anvil" : "shelf", {innerX1 - 2, innerY1 - 2, innerX1, innerY1});
                    if (profile.furnitureCount >= 5) addFurniture("table", {rect.CenterX() - 1, rect.CenterY() - 1, rect.CenterX() + 1, rect.CenterY()});
                } else {
                    addFurniture("altar", {rect.CenterX() - 1, innerY0, rect.CenterX() + 1, std::min(innerY0 + 2, innerY1)});
                    addFurniture("bench", {innerX0, rect.CenterY(), innerX1, std::min(rect.CenterY() + 1, innerY1)});
                }
                if (household.hasLivingSpace && kind != "house" && kind != "cottage" &&
                    kind != "inn" && kind != "caravanserai" && kind != "mayor_house" && kind != "manor") {
                    addFurniture("bed", {innerX1 - 2, innerY1 - 1, innerX1, innerY1});
                }
            }
            if (household.waterSource != "none") {
                const char* waterKind = household.hasLivingSpace && !urban ? "water_barrel" : "water_pump";
                addFurniture(waterKind, {innerX0, innerY1 - 1, std::min(innerX0 + 2, innerX1), innerY1});
            }
            if (household.hearth == "hearth") {
                addFurniture("hearth", {innerX1 - 1, innerY0, innerX1, std::min(innerY0 + 2, innerY1)});
            } else if (household.hearth == "forge") {
                addFurniture("forge", {innerX1 - 2, innerY0, innerX1, std::min(innerY0 + 2, innerY1)});
            }
            return true;
        };

        std::vector<Building> buildings;
        std::vector<HouseholdProfile> pendingSeparateResidences;
        int nextHouseholdId = 1;
        auto recordRoadPath = [&](const std::vector<Point>& path, const std::string& reason, int roadWidth, int roadClass) {
            if (path.empty()) return;
            int x0 = path.front().x;
            int y0 = path.front().y;
            int x1 = path.front().x;
            int y1 = path.front().y;
            for (const Point& point : path) {
                x0 = std::min(x0, point.x);
                y0 = std::min(y0, point.y);
                x1 = std::max(x1, point.x);
                y1 = std::max(y1, point.y);
            }
            candidate.regions.push_back({"road", nextRegionId++, x0, y0, x1, y1});
            candidate.regions.back().role = reason;
            candidate.regions.back().width = roadWidth;
            const int offsetMin = -(roadWidth / 2);
            const int offsetMax = offsetMin + roadWidth - 1;
            for (const Point& point : path) {
                for (int oy = offsetMin; oy <= offsetMax; ++oy) {
                    for (int ox = offsetMin; ox <= offsetMax; ++ox) {
                        const int px = point.x + ox;
                        const int py = point.y + oy;
                        if (!InBounds(px, py, width, height)) continue;
                        const std::size_t index = static_cast<std::size_t>(Index(px, py, width));
                        if (structure[index] == 0 && IsRoadLike(biomes[index])) {
                            pavingRoadClass[index] = std::max(pavingRoadClass[index], static_cast<std::uint8_t>(roadClass));
                        }
                    }
                }
            }
        };
        // Extend each gate/entrance through the countryside to the actual image
        // edge. The exterior road mask cannot cut a second hole in city walls,
        // consume protected nature, or expand the urban paving budget.
        std::vector<std::uint8_t> exteriorRoadMask(area, 0);
        for (int i = 0; i < area; ++i) if (exteriorDistance[i] >= 0) exteriorRoadMask[i] = 1;
        for (const Point& entrance : entrances) exteriorRoadMask[Index(entrance.x, entrance.y, width)] = 1;
        const int approachWidth = villageLayout ? districtRoadWidth : mainRoadWidth;
        for (std::size_t index = 0; index < entrances.size(); ++index) {
            std::vector<Point> approach{entrances[index]};
            for (int i = exteriorNeighbor(entrances[index]); i >= 0; i = exteriorNext[i]) approach.push_back({i % width, i / width});
            PaintRoadPath(biomes, exteriorRoadMask, structure, width, height, approach, approachWidth, nullptr);
            recordRoadPath(approach, "settlement_to_map_edge", approachWidth, villageLayout ? 2 : 3);
            const Point exit = approach.back();
            candidate.markers.push_back({"map_exit", exit.x, exit.y, static_cast<int>(index)});
            candidate.markers.back().role = "external_connection";
        }
        const std::vector<std::string> facilityKinds = villageLayout
            ? std::vector<std::string>{"inn", "shrine", "workshop", "barn"}
            : std::vector<std::string>{"inn", "market_hall", "weapon_shop", "mayor_house", "shrine", "warehouse"};
        const int facilityBaseCount = mode == TownMode::Village ? 2 : (mode == TownMode::Market ? 4 : 3);
        const int facilityScaleBonus = std::clamp(static_cast<int>(std::lround((mapAreaScale - 1.0) * 0.75)), 0, 5);
        const int facilityCount = std::clamp(facilityBaseCount + facilityScaleBonus, facilityBaseCount, static_cast<int>(facilityKinds.size()));

        auto placeProfileBuilding = [&](const std::string& kind, Point center, bool landmark, int jitter, bool resourceFacility = false) {
            const BuildingProfile profile = ProfileForBuilding(kind, urban, std::min(width, height));
            const HouseholdProfile household = PlanHousehold(kind, villageLayout, urban, nextHouseholdId, rng);
            for (int attempt = 0; attempt < (resourceFacility ? 192 : 48); ++attempt) {
                const int jitterX = attempt == 0 ? 0 : std::uniform_int_distribution<int>(-jitter, jitter)(rng);
                const int jitterY = attempt == 0 ? 0 : std::uniform_int_distribution<int>(-jitter, jitter)(rng);
                const int buildingWidth = std::uniform_int_distribution<int>(profile.minWidth, profile.maxWidth)(rng);
                const int buildingHeight = std::uniform_int_distribution<int>(profile.minHeight, profile.maxHeight)(rng);
                const Rect rect{
                    center.x - buildingWidth / 2 + jitterX,
                    center.y - buildingHeight / 2 + jitterY,
                    center.x - buildingWidth / 2 + jitterX + buildingWidth,
                    center.y - buildingHeight / 2 + jitterY + buildingHeight,
                };
                if (!canPlaceRect(rect, 1)) continue;
                if (resourceFacility && site != SettlementSite::Plains) {
                    const int resourceTile = site == SettlementSite::Coast ? TOWN_SEA :
                        (site == SettlementSite::MountainValley ? TOWN_CLIFF :
                        (site == SettlementSite::Forest ? TOWN_TREES : (site == SettlementSite::River ? RIVER : LAKE)));
                    const int maxDistance = std::max(6, std::min(width, height) / 6);
                    bool nearResource = false;
                    for (int y = std::max(0, rect.y0 - maxDistance); y < std::min(height, rect.y1 + maxDistance) && !nearResource; ++y) {
                        for (int x = std::max(0, rect.x0 - maxDistance); x < std::min(width, rect.x1 + maxDistance); ++x) {
                            if (biomes[Index(x, y, width)] != resourceTile) continue;
                            const int dx = std::max({rect.x0 - x, 0, x - rect.x1 + 1});
                            const int dy = std::max({rect.y0 - y, 0, y - rect.y1 + 1});
                            if (dx + dy <= maxDistance) { nearResource = true; break; }
                        }
                    }
                    if (!nearResource) continue;
                }
                Building building;
                if (paintBuilding(kind, rect, plazaCenter, {}, false, landmark, household, building)) {
                    buildings.push_back(building);
                    if (building.household.requiresSeparateResidence) {
                        HouseholdProfile residence = building.household;
                        residence.linkedRegionId = building.regionId;
                        pendingSeparateResidences.push_back(residence);
                    }
                    ++nextHouseholdId;
                    return true;
                }
            }
            return false;
        };

        auto placeCastleTownWard = [&]() {
            Rect ward;
            Point keepCenter;
            int wardWidth = 0;
            int wardHeight = 0;
            bool found = false;
            for (int sizeStep = 0; sizeStep < 3 && !found; ++sizeStep) {
                const double sizeScale = 1.0 - static_cast<double>(sizeStep) * 0.20;
                wardWidth = std::clamp(static_cast<int>(std::lround(std::min(width, height) * 0.22 * sizeScale)), 18, 42);
                wardHeight = std::clamp(static_cast<int>(std::lround(std::min(width, height) * 0.18 * sizeScale)), 16, 34);
                const std::array<Point, 5> requestedCenters{{
                    {plazaCenter.x, plazaCenter.y - plazaHeight / 2 - wardHeight / 2 - 5},
                    {plazaCenter.x, plazaCenter.y + plazaHeight / 2 + wardHeight / 2 + 5},
                    {plazaCenter.x - plazaWidth / 2 - wardWidth / 2 - 5, plazaCenter.y},
                    {plazaCenter.x + plazaWidth / 2 + wardWidth / 2 + 5, plazaCenter.y},
                    {plazaCenter.x, plazaCenter.y},
                }};
                for (const Point& requested : requestedCenters) {
                    const int centerXForWard = std::clamp(requested.x, border + wardWidth / 2 + 2, width - border - wardWidth / 2 - 3);
                    const int centerYForWard = std::clamp(requested.y, border + wardHeight / 2 + 2, height - border - wardHeight / 2 - 3);
                    const Rect candidateWard{
                        centerXForWard - wardWidth / 2,
                        centerYForWard - wardHeight / 2,
                        centerXForWard + (wardWidth + 1) / 2,
                        centerYForWard + (wardHeight + 1) / 2,
                    };
                    if (canPlaceRect(candidateWard, 1)) {
                        ward = candidateWard;
                        keepCenter = {centerXForWard, centerYForWard};
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                // The plaza, river, or a compact footprint can invalidate all
                // preferred castle positions. Search the remaining footprint
                // deterministically before giving up on the city profile.
                wardWidth = std::max(18, wardWidth);
                wardHeight = std::max(16, wardHeight);
                for (int y = border + wardHeight / 2 + 2; y < height - border - wardHeight / 2 - 2 && !found; y += 4) {
                    for (int x = border + wardWidth / 2 + 2; x < width - border - wardWidth / 2 - 2; x += 4) {
                        const Rect candidateWard{
                            x - wardWidth / 2,
                            y - wardHeight / 2,
                            x + (wardWidth + 1) / 2,
                            y + (wardHeight + 1) / 2,
                        };
                        if (canPlaceRect(candidateWard, 1)) {
                            ward = candidateWard;
                            keepCenter = {x, y};
                            found = true;
                            break;
                        }
                    }
                }
            }
            if (!found) {
                return false;
            }
            for (int y = ward.y0; y < ward.y1; ++y) {
                for (int x = ward.x0; x < ward.x1; ++x) {
                    const int index = Index(x, y, width);
                    const bool wall = x == ward.x0 || x == ward.x1 - 1 || y == ward.y0 || y == ward.y1 - 1;
                    if (wall) {
                        biomes[static_cast<std::size_t>(index)] = MOUNTAIN;
                        structure[static_cast<std::size_t>(index)] = 1;
                    }
                }
            }
            const std::array<Point, 4> gateCandidates{{
                {std::clamp(plazaCenter.x, ward.x0 + 1, ward.x1 - 2), ward.y0},
                {std::clamp(plazaCenter.x, ward.x0 + 1, ward.x1 - 2), ward.y1 - 1},
                {ward.x0, std::clamp(plazaCenter.y, ward.y0 + 1, ward.y1 - 2)},
                {ward.x1 - 1, std::clamp(plazaCenter.y, ward.y0 + 1, ward.y1 - 2)},
            }};
            const Point gate = *std::min_element(gateCandidates.begin(), gateCandidates.end(), [&](const Point& left, const Point& right) {
                return Manhattan(left, plazaCenter) < Manhattan(right, plazaCenter);
            });
            const int gateIndex = Index(gate.x, gate.y, width);
            biomes[static_cast<std::size_t>(gateIndex)] = groundTile;
            structure[static_cast<std::size_t>(gateIndex)] = 0;
            candidate.markers.push_back({"castle_gate", gate.x, gate.y, 0});
            candidate.markers.back().role = "castle_entrance";
            const int wardId = nextRegionId++;
            candidate.regions.push_back({"castle_ward", wardId, ward.x0, ward.y0, ward.x1 - 1, ward.y1 - 1});
            candidate.regions.back().role = "castle_courtyard";
            if (!placeProfileBuilding("manor", keepCenter, true, 2)) {
                return false;
            }
            for (int y = ward.y0 + 1; y < ward.y1 - 1; ++y) {
                for (int x = ward.x0 + 1; x < ward.x1 - 1; ++x) {
                    const int index = Index(x, y, width);
                    if (structure[static_cast<std::size_t>(index)] == 0 && plazaMask[static_cast<std::size_t>(index)] == 0) {
                        biomes[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(pavedCity ? TOWN_PAVEMENT : TOWN_STONE_FLOOR);
                    }
                }
            }
            return true;
        };

        if (cityType == TownCityType::CastleTown && !placeCastleTownWard()) continue;

        const auto [resourceX, resourceY] = SettlementResourceAnchor(candidate, site);
        const Point resourceAnchor = NearestFootprintPoint(footprint, biomes, width, height, {resourceX, resourceY});
        const std::string siteFacility = SettlementSiteFacility(site);
        if (!placeProfileBuilding(siteFacility, resourceAnchor, true, std::max(8, std::min(width, height) / 10), true) &&
            !placeProfileBuilding(siteFacility, resourceAnchor, true, std::max(12, std::min(width, height) / 5), true)) continue;

        // Cities get a legible commercial avenue before the general road graph
        // is built. Shops are placed on alternating sides of that avenue so
        // the image reads as a town centre rather than a uniform grid.
        if (urban && !entrances.empty()) {
            const Point avenueEntry = entrances.front();
            const std::vector<Point> avenue = FindPath(footprint, structure, biomes, width, height, avenueEntry, plazaCenter);
            if (!avenue.empty()) {
                PaintRoadPath(biomes, footprint, structure, width, height, avenue, mainRoadWidth, nullptr);
                recordRoadPath(avenue, "entrance_to_town_center", mainRoadWidth, 3);
                const std::array<std::string, 4> shopKinds{{"general_store", "weapon_shop", "inn", "market_hall"}};
                for (int shopIndex = 0; shopIndex < static_cast<int>(shopKinds.size()); ++shopIndex) {
                    const std::size_t pathIndex = avenue.size() <= 8
                        ? avenue.size() / 2
                        : std::min(avenue.size() - 2, avenue.size() / 4 + static_cast<std::size_t>(shopIndex) * avenue.size() / 10);
                    const Point pathPoint = avenue[pathIndex];
                    const Point previous = avenue[pathIndex > 0 ? pathIndex - 1 : pathIndex];
                    const bool horizontal = std::abs(pathPoint.x - previous.x) >= std::abs(pathPoint.y - previous.y);
                    const int side = shopIndex % 2 == 0 ? -1 : 1;
                    const Point shopCenter{
                        pathPoint.x + (horizontal ? 0 : side * 8),
                        pathPoint.y + (horizontal ? side * 8 : 0),
                    };
                    placeProfileBuilding(shopKinds[static_cast<std::size_t>(shopIndex)], shopCenter, true, 2);
                }
            }
        }

        for (int facilityIndex = 0; facilityIndex < facilityCount; ++facilityIndex) {
            const double angle = (static_cast<double>(facilityIndex) / std::max(1, facilityCount)) * 6.28318530718 + unit(rng) * 0.25;
            const int distance = std::max(plazaWidth, plazaHeight) / 2 + std::min(width, height) / 8;
            const Point requested{plazaCenter.x + static_cast<int>(std::lround(std::cos(angle) * distance)), plazaCenter.y + static_cast<int>(std::lround(std::sin(angle) * distance))};
            const Point anchor = NearestFootprintPoint(footprint, biomes, width, height, requested);
            placeProfileBuilding(facilityKinds[static_cast<std::size_t>(facilityIndex % facilityKinds.size())], anchor, true, 12);
        }

        std::vector<TownNode> nodes;
        nodes.push_back({plazaCenter, 2, "town_center"});
        for (const Point& entrance : entrances) nodes.push_back({entrance, 2, "town_entry"});
        for (const Building& building : buildings) {
            const bool districtNode = building.landmark || IsCommercialBuilding(building.kind) ||
                building.kind == "warehouse" || building.kind == "manor";
            nodes.push_back({building.approach, districtNode ? 1 : 0, districtNode ? "business_or_landmark" : "local_building"});
        }

        const int requestedDistricts = std::uniform_int_distribution<int>(districtMin, districtMax)(rng);
        std::vector<Point> districtCenters;
        for (int attempt = 0; attempt < requestedDistricts * 80 && static_cast<int>(districtCenters.size()) < requestedDistricts; ++attempt) {
            const Point point{
                std::uniform_int_distribution<int>(border + 2, width - border - 3)(rng),
                std::uniform_int_distribution<int>(border + 2, height - border - 3)(rng),
            };
            const int index = Index(point.x, point.y, width);
            if (footprint[static_cast<std::size_t>(index)] == 0 || IsWater(biomes[static_cast<std::size_t>(index)]) ||
                plazaMask[static_cast<std::size_t>(index)] != 0 || structure[static_cast<std::size_t>(index)] != 0) continue;
            bool tooClose = false;
            for (const Point& existing : districtCenters) if (Manhattan(existing, point) < std::min(width, height) / 8) tooClose = true;
            if (!tooClose) {
                districtCenters.push_back(point);
                nodes.push_back({point, 1, "district_center"});
            }
        }
        if (districtCenters.size() < 2) continue;

        std::vector<Point> waterCrossings;
        std::vector<std::uint8_t> connected(nodes.size(), 0);
        connected[0] = 1;
        int connectedCount = 1;
        auto roadWidthForEdge = [&](int a, int b) {
            const int leftClass = nodes[static_cast<std::size_t>(a)].roadClass;
            const int rightClass = nodes[static_cast<std::size_t>(b)].roadClass;
            if (leftClass >= 2 && rightClass >= 2) return mainRoadWidth;
            if (leftClass >= 1 || rightClass >= 1) return districtRoadWidth;
            return alleyWidth;
        };
        auto roadReasonForEdge = [&](int a, int b) {
            const std::string& leftReason = nodes[static_cast<std::size_t>(a)].roadReason;
            const std::string& rightReason = nodes[static_cast<std::size_t>(b)].roadReason;
            if (leftReason == "town_entry" || rightReason == "town_entry") {
                const int other = leftReason == "town_entry" ? b : a;
                return nodes[static_cast<std::size_t>(other)].roadClass >= 2
                    ? std::string("entrance_to_center")
                    : std::string("entrance_to_district");
            }
            if (leftReason == "district_center" || rightReason == "district_center") {
                if (leftReason == "business_or_landmark" || rightReason == "business_or_landmark") return std::string("district_to_business");
                return std::string("district_to_center");
            }
            if (leftReason == "business_or_landmark" || rightReason == "business_or_landmark") return std::string("business_cluster");
            return std::string("local_connector");
        };
        auto connectNodes = [&](int a, int b, int roadWidth, const std::string& reason) {
            const std::vector<Point> path = FindPath(footprint, structure, biomes, width, height, nodes[static_cast<std::size_t>(a)].point, nodes[static_cast<std::size_t>(b)].point);
            if (path.empty()) return false;
            PaintRoadPath(biomes, footprint, structure, width, height, path, roadWidth, &waterCrossings);
            const int aClass = nodes[static_cast<std::size_t>(a)].roadClass;
            const int bClass = nodes[static_cast<std::size_t>(b)].roadClass;
            recordRoadPath(path, reason, roadWidth, aClass >= 2 && bClass >= 2 ? 3 : (aClass >= 1 || bClass >= 1 ? 2 : 1));
            return true;
        };
        while (connectedCount < static_cast<int>(nodes.size())) {
            int bestA = -1;
            int bestB = -1;
            int bestDistance = std::numeric_limits<int>::max();
            for (int a = 0; a < static_cast<int>(nodes.size()); ++a) {
                if (connected[static_cast<std::size_t>(a)] == 0) continue;
                for (int b = 0; b < static_cast<int>(nodes.size()); ++b) {
                    if (connected[static_cast<std::size_t>(b)] != 0) continue;
                    const int distance = Manhattan(nodes[static_cast<std::size_t>(a)].point, nodes[static_cast<std::size_t>(b)].point);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestA = a;
                        bestB = b;
                    }
                }
            }
            if (bestB < 0) break;
            if (!connectNodes(bestA, bestB, roadWidthForEdge(bestA, bestB), roadReasonForEdge(bestA, bestB))) break;
            connected[static_cast<std::size_t>(bestB)] = 1;
            ++connectedCount;
        }
        if (connectedCount < static_cast<int>(nodes.size())) continue;

        std::vector<std::pair<int, int>> extraEdges;
        for (int a = 0; a < static_cast<int>(nodes.size()); ++a) {
            for (int b = a + 1; b < static_cast<int>(nodes.size()); ++b) {
                const bool districtLoop = nodes[static_cast<std::size_t>(a)].roadReason == "district_center" ||
                    nodes[static_cast<std::size_t>(b)].roadReason == "district_center";
                if (districtLoop) extraEdges.push_back({a, b});
            }
        }
        std::sort(extraEdges.begin(), extraEdges.end(), [&](const std::pair<int, int>& left, const std::pair<int, int>& right) {
            const auto distance = [&](const std::pair<int, int>& edge) {
                return Manhattan(nodes[static_cast<std::size_t>(edge.first)].point, nodes[static_cast<std::size_t>(edge.second)].point);
            };
            return distance(left) < distance(right);
        });
        const int extraCount = static_cast<int>(std::lround(extraEdges.size() * extraEdgeRate));
        for (int i = 0; i < extraCount && i < static_cast<int>(extraEdges.size()); ++i) {
            const auto [a, b] = extraEdges[static_cast<std::size_t>(i)];
            connectNodes(a, b, roadWidthForEdge(a, b), "district_loop");
        }

        std::sort(waterCrossings.begin(), waterCrossings.end(), [](const Point& a, const Point& b) { return a.y == b.y ? a.x < b.x : a.y < b.y; });
        waterCrossings.erase(std::unique(waterCrossings.begin(), waterCrossings.end(), SamePoint), waterCrossings.end());
        for (std::size_t i = 0; i < waterCrossings.size(); i += std::max<std::size_t>(1, waterCrossings.size() / 4)) {
            const Point& crossing = waterCrossings[i];
            candidate.markers.push_back({"water_crossing", crossing.x, crossing.y, static_cast<int>(i)});
            candidate.markers.back().role = "road_bridge";
        }

        auto tryPaintArea = [&](std::uint8_t tile, const Rect& rect, const char* kind, int id) {
            if (rect.Width() < 4 || rect.Height() < 4) return false;
            for (int y = rect.y0; y < rect.y1; ++y) {
                for (int x = rect.x0; x < rect.x1; ++x) {
                    if (!InBounds(x, y, width, height)) return false;
                    const int index = Index(x, y, width);
                    if (footprint[static_cast<std::size_t>(index)] == 0 || structure[static_cast<std::size_t>(index)] != 0 ||
                        IsWater(biomes[static_cast<std::size_t>(index)]) || IsRoadLike(biomes[static_cast<std::size_t>(index)]) ||
                        plazaMask[static_cast<std::size_t>(index)] != 0) return false;
                }
            }
            PaintRect(biomes, width, height, rect, tile);
            candidate.regions.push_back({kind, id, rect.x0, rect.y0, rect.x1 - 1, rect.y1 - 1});
            candidate.regions.back().role = kind;
            return true;
        };

        const int parkCount = std::uniform_int_distribution<int>(parkMin, parkMax)(rng);
        int placedParks = 0;
        const std::uint8_t parkTile = static_cast<std::uint8_t>(terrain == TownTerrain::Woodland ? PLAIN : FOREST);
        for (int attempt = 0; attempt < parkCount * 100 && placedParks < parkCount; ++attempt) {
            const int parkWidth = std::clamp(std::uniform_int_distribution<int>(std::max(5, width / 18), std::max(6, width / 10))(rng), 5, 20);
            const int parkHeight = std::clamp(std::uniform_int_distribution<int>(std::max(5, height / 20), std::max(6, height / 11))(rng), 5, 18);
            const int x = std::uniform_int_distribution<int>(border + 2, std::max(border + 2, width - border - parkWidth - 2))(rng);
            const int y = std::uniform_int_distribution<int>(border + 2, std::max(border + 2, height - border - parkHeight - 2))(rng);
            if (tryPaintArea(parkTile, {x, y, x + parkWidth, y + parkHeight}, "park", placedParks)) ++placedParks;
        }

        const std::vector<std::string> ordinaryKinds = villageLayout
            ? std::vector<std::string>{"cottage", "cottage", "house", "barn", "workshop", "shrine"}
            : (cityType == TownCityType::MedievalCity || cityType == TownCityType::PavedCity
                ? std::vector<std::string>{"house", "house", "house", "house", "house", "warehouse", "workshop", "weapon_shop", "general_store", "manor"}
                : (cityType == TownCityType::CastleTown
                    ? std::vector<std::string>{"house", "house", "house", "house", "cottage", "warehouse", "workshop", "general_store", "manor"}
                    : std::vector<std::string>{"house", "house", "house", "warehouse", "workshop", "weapon_shop", "general_store", "manor"}));
        const int targetBuildingCount = std::uniform_int_distribution<int>(siteBuildingMin, siteBuildingMax)(rng);
        auto placeOrdinaryBuilding = [&](const Rect& rect, const std::string& kind, const HouseholdProfile& household) {
            if (!canPlaceRect(rect, 1)) return false;
            struct DoorRoute {
                Point door;
                Point outside;
                std::vector<Point> path;
            };
            std::vector<DoorRoute> routes;
            for (int y = rect.y0; y < rect.y1; ++y) {
                for (int x = rect.x0; x < rect.x1; ++x) {
                    if (x != rect.x0 && x != rect.x1 - 1 && y != rect.y0 && y != rect.y1 - 1) continue;
                    if ((x == rect.x0 || x == rect.x1 - 1) && (y == rect.y0 || y == rect.y1 - 1)) continue;
                    Point outside{x, y};
                    if (x == rect.x0) --outside.x;
                    else if (x == rect.x1 - 1) ++outside.x;
                    else if (y == rect.y0) --outside.y;
                    else ++outside.y;
                    if (!InBounds(outside.x, outside.y, width, height)) continue;
                    const int outsideIndex = Index(outside.x, outside.y, width);
                    if (footprint[static_cast<std::size_t>(outsideIndex)] == 0 || structure[static_cast<std::size_t>(outsideIndex)] != 0 || IsWater(biomes[static_cast<std::size_t>(outsideIndex)])) continue;
                    std::vector<Point> path = FindPathToRoad(footprint, structure, biomes, width, height, outside, &rect);
                    if (!path.empty()) routes.push_back({{x, y}, outside, std::move(path)});
                }
            }
            if (routes.empty()) return false;
            std::sort(routes.begin(), routes.end(), [](const DoorRoute& a, const DoorRoute& b) {
                const bool aAdjacent = a.path.size() <= 2;
                const bool bAdjacent = b.path.size() <= 2;
                if (aAdjacent != bAdjacent) return aAdjacent;
                return a.path.size() < b.path.size();
            });
            const DoorRoute& route = routes.front();
            PaintRoadPath(biomes, footprint, structure, width, height, route.path, alleyWidth, &waterCrossings);
            recordRoadPath(route.path, "building_to_existing_road", alleyWidth, 1);
            Building building;
            if (!paintBuilding(kind, rect, route.outside, route.outside, true, false, household, building)) return false;
            if (household.linkedRegionId >= 0) {
                for (MapPlanRegion& region : candidate.regions) {
                    if (region.id == household.linkedRegionId) {
                        region.linkedRegionId = building.regionId;
                        break;
                    }
                }
            }
            if (building.household.requiresSeparateResidence) {
                HouseholdProfile residence = building.household;
                residence.linkedRegionId = building.regionId;
                pendingSeparateResidences.push_back(residence);
            }
            buildings.push_back(building);
            return true;
        };

        int placementAttempts = 0;
        while ((static_cast<int>(buildings.size()) < targetBuildingCount || !pendingSeparateResidences.empty()) && placementAttempts < targetBuildingCount * 320) {
            ++placementAttempts;
            const bool compact = placementAttempts > targetBuildingCount * 150;
            std::string kind;
            if (!pendingSeparateResidences.empty()) {
                // A town business that has no living quarters gets a nearby
                // home before unrelated houses are added.
                kind = "house";
            } else {
                kind = ordinaryKinds[static_cast<std::size_t>(std::uniform_int_distribution<int>(0, static_cast<int>(ordinaryKinds.size()) - 1)(rng))];
            }
            const bool linkedResidence = !pendingSeparateResidences.empty();
            HouseholdProfile household = linkedResidence
                ? pendingSeparateResidences.front()
                : PlanHousehold(kind, villageLayout, urban, nextHouseholdId, rng);
            if (linkedResidence) {
                household.hasLivingSpace = true;
                household.isResidence = true;
                household.requiresSeparateResidence = false;
                household.livingArrangement = "separate_residence";
                household.waterSource = household.waterSource == "none"
                    ? (villageLayout ? "well" : "water_pump")
                    : household.waterSource;
                household.hearth = household.hearth == "none" ? "hearth" : household.hearth;
            }
            const BuildingProfile profile = ProfileForBuilding(kind, urban, std::min(width, height));
            const int maxWidth = compact ? std::min(profile.maxWidth, profile.minWidth + 3) : profile.maxWidth;
            const int maxHeight = compact ? std::min(profile.maxHeight, profile.minHeight + 3) : profile.maxHeight;
            const int buildingWidth = std::uniform_int_distribution<int>(profile.minWidth, std::max(profile.minWidth, maxWidth))(rng);
            const int buildingHeight = std::uniform_int_distribution<int>(profile.minHeight, std::max(profile.minHeight, maxHeight))(rng);
            const int x = std::uniform_int_distribution<int>(border + 2, std::max(border + 2, width - border - buildingWidth - 2))(rng);
            const int y = std::uniform_int_distribution<int>(border + 2, std::max(border + 2, height - border - buildingHeight - 2))(rng);
            const Rect rect{x, y, x + buildingWidth, y + buildingHeight};
            if (placeOrdinaryBuilding(rect, kind, household)) {
                if (linkedResidence) {
                    pendingSeparateResidences.erase(pendingSeparateResidences.begin());
                } else {
                    ++nextHouseholdId;
                }
            }
        }
        mostBuildingsPlaced = std::max(mostBuildingsPlaced, static_cast<int>(buildings.size()));
        if (!pendingSeparateResidences.empty()) continue;
        if (static_cast<int>(buildings.size()) < siteBuildingMin) continue;

        ApplyTownPaving(candidate, footprint, structure, plazaMask, pavingRoadClass, groundTile);

        for (std::size_t district = 0; district < districtCenters.size(); ++district) {
            int x0 = width;
            int y0 = height;
            int x1 = 0;
            int y1 = 0;
            for (int y = border; y < height - border; ++y) {
                for (int x = border; x < width - border; ++x) {
                    if (footprint[static_cast<std::size_t>(Index(x, y, width))] == 0) continue;
                    std::size_t nearest = 0;
                    int nearestDistance = std::numeric_limits<int>::max();
                    for (std::size_t other = 0; other < districtCenters.size(); ++other) {
                        const int distance = Manhattan({x, y}, districtCenters[other]);
                        if (distance < nearestDistance) {
                            nearestDistance = distance;
                            nearest = other;
                        }
                    }
                    if (nearest != district) continue;
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x);
                    y1 = std::max(y1, y);
                }
            }
            if (x0 <= x1 && y0 <= y1) {
                candidate.regions.push_back({"district", static_cast<int>(district), x0, y0, x1, y1});
                candidate.regions.back().role = "district";
            }
        }

        int footprintX0 = width;
        int footprintY0 = height;
        int footprintX1 = 0;
        int footprintY1 = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (footprint[static_cast<std::size_t>(Index(x, y, width))] == 0) continue;
                footprintX0 = std::min(footprintX0, x);
                footprintY0 = std::min(footprintY0, y);
                footprintX1 = std::max(footprintX1, x);
                footprintY1 = std::max(footprintY1, y);
            }
        }
        candidate.regions.push_back({"town_footprint", 0, footprintX0, footprintY0, footprintX1, footprintY1});
        candidate.regions.back().role = "settlement_footprint";

        // Entrances are semantic boundary markers, not ordinary road cells.
        // Restore their bridge biome after all road, park, and building passes
        // so the exported marker and tile layer cannot disagree.
        for (const MapPlanMarker& marker : candidate.markers) {
            if ((marker.kind != "entrance" && marker.kind != "water_crossing") ||
                !InBounds(marker.x, marker.y, width, height)) continue;
            biomes[static_cast<std::size_t>(Index(marker.x, marker.y, width))] = BRIDGE;
        }

        const MapQualityMetrics quality = EvaluateMapQuality(candidate);
        const double markerRate = quality.markerCount == 0 ? 0.0 : static_cast<double>(quality.reachableMarkers) / quality.markerCount;
        const double desiredRoadRatio = mode == TownMode::Village ? 0.08 :
            (cityType == TownCityType::PavedCity ? 0.18 :
            (cityType == TownCityType::MedievalCity ? 0.16 :
            (cityType == TownCityType::WalledCity ? 0.14 :
            (mode == TownMode::Market ? 0.15 : (mode == TownMode::Grid ? 0.14 : 0.12)))));
        const double desiredBuildingBase = mode == TownMode::Village ? 12.0 :
            (cityType == TownCityType::MedievalCity ? 36.0 :
            (cityType == TownCityType::PavedCity ? 34.0 :
            (cityType == TownCityType::WalledCity ? 30.0 :
            (cityType == TownCityType::CastleTown ? 27.0 :
            (mode == TownMode::Market ? 28.0 : (mode == TownMode::Grid ? 25.0 : 22.0))))));
        const double desiredBuildingCount = std::clamp(desiredBuildingBase * areaScale,
            static_cast<double>(siteBuildingMin), static_cast<double>(siteBuildingMax));
        const double desiredJunctions = (mode == TownMode::Village ? 700.0 :
            (mode == TownMode::Market ? 1300.0 : (mode == TownMode::Grid ? 1000.0 :
            (mode == TownMode::Walled ? 900.0 : (mode == TownMode::River ? 1100.0 : 1050.0))))) * areaScale;
        double score = quality.townDevelopmentReachableRatio * 500.0 + markerRate * 260.0;
        // Compare density against the selected archetype rather than always
        // rewarding the densest candidate. This keeps village layouts simple
        // while allowing market/grid layouts to remain busier.
        score -= std::abs(static_cast<double>(quality.townBuildingCount) - desiredBuildingCount) * 3.0;
        score += std::min(12, quality.townDistrictCount) * 3.0;
        // Match network complexity to the archetype instead of selecting the
        // busiest graph for every seed.
        score -= std::abs(static_cast<double>(quality.roadJunctionCells) - desiredJunctions) * 0.10;
        score -= std::abs(quality.townRoadRatio - desiredRoadRatio) * 400.0;
        if (mode == TownMode::Village && quality.townParkWaterRatio < 0.02) score += 12.0;
        if (mode == TownMode::Market && quality.townLandmarkCount >= 4) score += 12.0;
        if (!modeWasExplicit) {
            // Keep the candidate search quality-driven while preventing small
            // maps from collapsing onto the same market layout.  The prior
            // is deterministic, modest, and never overrides an actual
            // quality issue; explicit townMode remains authoritative.
            const TownMode preferredMode = static_cast<TownMode>(CandidateSeed(params_.seed, 97) % 6U);
            if (mode == preferredMode) score += 55.0;
        }
        score -= quality.issues.size() * 500.0;
        candidate.qualityScore = score;
        candidate.selectionReason = "highest deterministic town candidate score";
        generated = true;
        if (score > best.score) best = {std::move(candidate), quality, score};
    }

    if (!generated) {
        outError = "Town generation could not place a connected layout with the requested constraints (site=" +
            std::string(SettlementSiteName(site)) + ", mostBuildings=" + std::to_string(mostBuildingsPlaced) +
            ", minimumBuildings=" + std::to_string(smallestBuildingMinimum) + ")";
        return false;
    }
    outPlan = std::move(best.plan);
    return true;
}
