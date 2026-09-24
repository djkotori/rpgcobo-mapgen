#include "CastleMapImageGenerator.h"

#include "MapQuality.h"
#include "CastleProgram.h"
#include "TownPaving.h"

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

struct Gate {
    int side = 0;
    bool major = false;
    int halfWidth = 0;
    Point outer;
    Point inner;
    std::vector<Point> bridge;
};

struct RouteNode {
    Point point;
    bool major = false;
};

struct RoomInfo {
    Rect rect;
    std::string role;
    int regionId = -1;
    int column = 1;
};

struct FacilityInfo {
    Rect rect;
    Point door;
    Point approach;
    int regionId = -1;
    int regionIndex = -1;
    std::string furnishing;
};

enum class CastleMode {
    Concentric,
    Hill,
    River,
    Palace,
    Fortress,
};

const char* CastleModeName(CastleMode mode) {
    switch (mode) {
        case CastleMode::Concentric: return "concentric";
        case CastleMode::Hill: return "hill";
        case CastleMode::River: return "river";
        case CastleMode::Palace: return "palace";
        case CastleMode::Fortress: return "fortress";
    }
    return "unknown";
}

std::uint8_t CastleExteriorBiome(CastleMode mode) {
    switch (mode) {
        case CastleMode::Concentric: return PLAIN;
        case CastleMode::Hill: return MOUNTAIN;
        case CastleMode::River: return RIVER;
        case CastleMode::Palace: return FOREST;
        case CastleMode::Fortress: return DESERT;
    }
    return PLAIN;
}

const char* WealthClass(double wealth) {
    if (wealth < 0.9) return "strained";
    if (wealth < 1.15) return "ordinary";
    if (wealth < 1.65) return "prosperous";
    if (wealth < 2.15) return "wealthy";
    return "opulent";
}

int BaseDefenseLevel(CastleMode mode) {
    switch (mode) {
        case CastleMode::Concentric: return 4;
        case CastleMode::Hill: return 4;
        case CastleMode::River: return 3;
        case CastleMode::Palace: return 2;
        case CastleMode::Fortress: return 5;
    }
    return 3;
}

const char* WallFormName(CastleMode mode, int variant) {
    static const std::array<std::array<const char*, 3>, 5> forms{{
        {{"balanced_octagon", "offset_concentric", "deep_corner_enceinte"}},
        {{"ridge_wedge", "cliff_flank", "saddle_rampart"}},
        {{"river_bulwark", "harbor_notch", "asymmetric_quay_wall"}},
        {{"formal_rectangle", "garden_octagon", "ceremonial_forecourt"}},
        {{"front_hornwork", "heavy_bastions", "staggered_kill_zone"}},
    }};
    return forms[static_cast<std::size_t>(mode)][static_cast<std::size_t>(std::clamp(variant, 0, 2))];
}

CastleFacilitySpec ExpansionFacility(CastleMode mode, int index) {
    switch (index % 6) {
    case 0:
        switch (mode) {
            case CastleMode::Hill: return {"mountain_trading_post", 1.0, 0.85, 1, "crates"};
            case CastleMode::River: return {"merchant_warehouse", 1.45, 0.95, 3, "crates"};
            case CastleMode::Palace: return {"merchant_gallery", 1.25, 0.85, 0, "desks_and_shelves"};
            case CastleMode::Fortress: return {"quartermaster_depot", 1.2, 0.95, 1, "grain_bins"};
            case CastleMode::Concentric: return {"merchant_storehouse", 1.2, 0.9, 1, "crates"};
        }
        break;
    case 1:
        switch (mode) {
            case CastleMode::Hill: return {"ridge_guard_annex", 0.9, 1.0, 0, "weapon_racks"};
            case CastleMode::River: return {"harbor_guard_annex", 1.0, 0.9, 0, "beds"};
            case CastleMode::Palace: return {"household_guard_annex", 1.05, 0.9, 0, "weapon_racks"};
            case CastleMode::Fortress: return {"reserve_barracks", 1.4, 1.0, 0, "beds"};
            case CastleMode::Concentric: return {"garrison_annex", 1.2, 0.95, 0, "beds"};
        }
        break;
    case 2:
        switch (mode) {
            case CastleMode::Hill: return {"ridge_tavern", 1.0, 0.9, 0, "tables"};
            case CastleMode::River: return {"dockside_tavern", 1.15, 0.9, 3, "tables"};
            case CastleMode::Palace: return {"court_wine_tavern", 1.05, 0.9, 0, "tables"};
            case CastleMode::Fortress: return {"garrison_tavern", 1.1, 0.9, 0, "tables"};
            case CastleMode::Concentric: return {"castle_tavern", 1.05, 0.9, 0, "tables"};
        }
        break;
    case 3:
        switch (mode) {
            case CastleMode::Hill: return {"mountain_guides_guild", 1.1, 1.0, 1, "desks_and_shelves"};
            case CastleMode::River: return {"boatmens_guild", 1.2, 1.0, 3, "desks_and_shelves"};
            case CastleMode::Palace: return {"court_artisans_guild", 1.2, 1.0, 0, "desks_and_shelves"};
            case CastleMode::Fortress: return {"armorers_guild", 1.2, 1.0, 1, "workbenches"};
            case CastleMode::Concentric: return {"royal_crafts_guild", 1.15, 1.0, 1, "workbenches"};
        }
        break;
    case 4:
        switch (mode) {
            case CastleMode::Hill: return {"climbers_inn", 1.15, 1.0, 1, "beds"};
            case CastleMode::River: return {"harbor_inn", 1.25, 1.0, 3, "beds"};
            case CastleMode::Palace: return {"diplomats_inn", 1.25, 1.0, 0, "beds"};
            case CastleMode::Fortress: return {"officers_inn", 1.2, 1.0, 0, "beds"};
            case CastleMode::Concentric: return {"pilgrims_inn", 1.2, 1.0, 1, "beds"};
        }
        break;
    default:
        switch (mode) {
            case CastleMode::Hill: return {"signal_corps_lodge", 1.0, 0.9, 0, "signal_brazier"};
            case CastleMode::River: return {"navigation_exchange", 1.15, 0.9, 3, "map_tables"};
            case CastleMode::Palace: return {"royal_academy", 1.2, 1.0, 0, "desks_and_shelves"};
            case CastleMode::Fortress: return {"siege_engine_guild", 1.25, 1.0, 1, "workbenches"};
            case CastleMode::Concentric: return {"scribes_college", 1.1, 1.0, 1, "desks_and_shelves"};
        }
        break;
    }
    return {"garrison_annex", 1.0, 1.0, 0, "beds"};
}

CastleFacilitySpec KeepExpansionWing(CastleMode mode, int index) {
    const bool administrative = index == 0;
    switch (mode) {
        case CastleMode::Hill: return administrative
            ? CastleFacilitySpec{"command_annex", 1.0, 1.0, 2, "map_tables"}
            : CastleFacilitySpec{"ridge_garrison_wing", 1.0, 1.0, 2, "weapon_racks"};
        case CastleMode::River: return administrative
            ? CastleFacilitySpec{"customs_wing", 1.0, 1.0, 2, "customs_counters"}
            : CastleFacilitySpec{"harbor_garrison_wing", 1.0, 1.0, 2, "beds"};
        case CastleMode::Palace: return administrative
            ? CastleFacilitySpec{"state_apartment_wing", 1.0, 1.0, 2, "desks_and_shelves"}
            : CastleFacilitySpec{"household_guard_wing", 1.0, 1.0, 2, "weapon_racks"};
        case CastleMode::Fortress: return administrative
            ? CastleFacilitySpec{"staff_headquarters_wing", 1.0, 1.0, 2, "map_tables"}
            : CastleFacilitySpec{"arsenal_wing", 1.0, 1.0, 2, "weapon_racks"};
        case CastleMode::Concentric: return administrative
            ? CastleFacilitySpec{"royal_household_wing", 1.0, 1.0, 2, "desks_and_shelves"}
            : CastleFacilitySpec{"garrison_wing", 1.0, 1.0, 2, "beds"};
    }
    return {"keep_annex", 1.0, 1.0, 2, "desks_and_shelves"};
}

CastleFacilitySpec UpperWardExpansion(CastleMode mode, int index) {
    const bool civilWing = index == 0;
    switch (mode) {
        case CastleMode::Hill: return civilWing
            ? CastleFacilitySpec{"ridge_command_block", 1.0, 1.0, 2, "map_tables"}
            : CastleFacilitySpec{"mountain_garrison_block", 1.0, 1.0, 2, "beds"};
        case CastleMode::River: return civilWing
            ? CastleFacilitySpec{"customs_administration_block", 1.0, 1.0, 2, "customs_counters"}
            : CastleFacilitySpec{"harbor_guard_block", 1.0, 1.0, 2, "weapon_racks"};
        case CastleMode::Palace: return civilWing
            ? CastleFacilitySpec{"court_administration_block", 1.0, 1.0, 2, "desks_and_shelves"}
            : CastleFacilitySpec{"royal_guest_block", 1.0, 1.0, 2, "beds"};
        case CastleMode::Fortress: return civilWing
            ? CastleFacilitySpec{"headquarters_block", 1.0, 1.0, 2, "map_tables"}
            : CastleFacilitySpec{"arsenal_block", 1.0, 1.0, 2, "weapon_racks"};
        case CastleMode::Concentric: return civilWing
            ? CastleFacilitySpec{"royal_offices_block", 1.0, 1.0, 2, "desks_and_shelves"}
            : CastleFacilitySpec{"knights_quarters_block", 1.0, 1.0, 2, "beds"};
    }
    return {"upper_ward_annex", 1.0, 1.0, 2, "desks_and_shelves"};
}

const char* WealthParkRole(CastleMode mode) {
    switch (mode) {
        case CastleMode::Hill: return "lookout_garden";
        case CastleMode::River: return "riverside_pocket_park";
        case CastleMode::Palace: return "formal_court_garden";
        case CastleMode::Fortress: return "veterans_green";
        case CastleMode::Concentric: return "guild_green";
    }
    return "pocket_park";
}

const char* StreetTreeRole(CastleMode mode) {
    switch (mode) {
        case CastleMode::Hill: return "windbreak_tree";
        case CastleMode::River: return "quay_tree";
        case CastleMode::Palace: return "avenue_tree";
        case CastleMode::Fortress: return "shade_tree";
        case CastleMode::Concentric: return "ward_tree";
    }
    return "street_tree";
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

inline bool Contains(const Rect& rect, int x, int y) {
    return x >= rect.x0 && x < rect.x1 && y >= rect.y0 && y < rect.y1;
}

inline int Manhattan(const Point& a, const Point& b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

bool IsWater(std::uint8_t biome) {
    return biome == RIVER || biome == LAKE || biome == SEA;
}

bool IsRoadLike(std::uint8_t biome) {
    return biome == ROAD || biome == BRIDGE;
}

// Chebyshev distance gives continuous masonry and moat bands even at the
// oblique corners of the enceinte. No per-cell contour noise is used.
std::vector<int> MaskDistance(const std::vector<std::uint8_t>& mask, int width, int height, bool toInside) {
    std::vector<int> distance(mask.size(), width + height);
    std::queue<int> pending;
    for (int i = 0; i < width * height; ++i) {
        if ((mask[i] != 0) == toInside) { distance[i] = 0; pending.push(i); }
    }
    while (!pending.empty()) {
        const int i = pending.front(); pending.pop();
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int x = i % width + dx, y = i / width + dy;
            if (!InBounds(x, y, width, height)) continue;
            const int next = Index(x, y, width);
            if (distance[next] <= distance[i] + 1) continue;
            distance[next] = distance[i] + 1;
            pending.push(next);
        }
    }
    return distance;
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

CastleMode ChooseMode(std::mt19937& rng, const MapGenParams& params) {
    const auto getParam = [&params](const char* key, double fallback) {
        const auto it = params.extra.find(key);
        return it == params.extra.end() ? fallback : it->second;
    };
    const auto requested = params.extra.find("castleMode");
    if (requested != params.extra.end() && requested->second >= 0.0) {
        return static_cast<CastleMode>(std::clamp(static_cast<int>(std::lround(requested->second)), 0, 4));
    }
    std::discrete_distribution<int> distribution({
        std::max(0.0, getParam("castleModeWeightConcentric", getParam("castleModeWeights.concentric", 0.30))),
        std::max(0.0, getParam("castleModeWeightHill", getParam("castleModeWeights.hill", 0.15))),
        std::max(0.0, getParam("castleModeWeightRiver", getParam("castleModeWeights.river", 0.15))),
        std::max(0.0, getParam("castleModeWeightPalace", getParam("castleModeWeights.palace", 0.15))),
        std::max(0.0, getParam("castleModeWeightFortress", getParam("castleModeWeights.fortress", 0.25))),
    });
    return static_cast<CastleMode>(distribution(rng));
}

void PaintRect(std::vector<std::uint8_t>& biomes, int width, int height, const Rect& rect, std::uint8_t tile) {
    for (int y = std::max(0, rect.y0); y < std::min(height, rect.y1); ++y) {
        for (int x = std::max(0, rect.x0); x < std::min(width, rect.x1); ++x) {
            biomes[static_cast<std::size_t>(Index(x, y, width))] = tile;
        }
    }
}

bool IsRectBoundary(const Rect& rect, int x, int y, int thickness) {
    return x < rect.x0 + thickness || x >= rect.x1 - thickness ||
        y < rect.y0 + thickness || y >= rect.y1 - thickness;
}

void PaintRing(
    std::vector<std::uint8_t>& biomes,
    std::vector<std::uint8_t>& structure,
    int width,
    int height,
    const Rect& rect,
    int thickness,
    std::uint8_t tile) {
    for (int y = rect.y0; y < rect.y1; ++y) {
        for (int x = rect.x0; x < rect.x1; ++x) {
            if (!InBounds(x, y, width, height) || !IsRectBoundary(rect, x, y, thickness)) continue;
            const int index = Index(x, y, width);
            biomes[static_cast<std::size_t>(index)] = tile;
            structure[static_cast<std::size_t>(index)] = 1;
        }
    }
}

// Only secondary apartments are subdivided. Major halls/kitchens retain
// their reserved area even when the requested room count increases.
std::vector<RoomInfo> PlanKeepRooms(CastleMode mode, const CastleProgram& program, const Rect& bounds, int count, int& entry) {
    std::vector<RoomInfo> result;
    bool valid = true;
    auto add = [&](Rect rect, const std::string& role) {
        if (rect.Width() < 4 || rect.Height() < 4) valid = false;
        result.push_back({rect, role, static_cast<int>(result.size())});
    };
    auto bank = [&](Rect rect, int pieces, const std::vector<std::string>& roles, bool largeFirst) {
        if (pieces <= 0) return;
        std::vector<Rect> plots;
        if (largeFirst && pieces > 1 && rect.Height() >= 12) {
            const int split = rect.y0 + std::clamp(static_cast<int>(rect.Height() * 0.58), 4, rect.Height() - 4);
            add({rect.x0, rect.y0, rect.x1, split}, roles.front());
            rect.y0 = split;
            --pieces;
        } else largeFirst = false;
        plots.push_back(rect);
        while (static_cast<int>(plots.size()) < pieces) {
            int selected = -1, bestArea = 0;
            for (int i = 0; i < static_cast<int>(plots.size()); ++i) {
                const auto& p = plots[i];
                if (p.Width() < 8 && p.Height() < 8) continue;
                if (p.Width() * p.Height() > bestArea) { selected = i; bestArea = p.Width() * p.Height(); }
            }
            if (selected < 0) { valid = false; break; }
            Rect a = plots[selected], b = a;
            if (a.Width() >= a.Height() && a.Width() >= 8) { a.x1 = a.CenterX(); b.x0 = a.x1; }
            else { a.y1 = a.CenterY(); b.y0 = a.y1; }
            plots[selected] = a; plots.push_back(b);
        }
        std::sort(plots.begin(), plots.end(), [](const Rect& a, const Rect& b) {
            return a.y0 == b.y0 ? a.x0 < b.x0 : a.y0 < b.y0;
        });
        for (std::size_t i = 0; i < plots.size(); ++i) add(plots[i], roles[(i + (largeFirst ? 1 : 0)) % roles.size()]);
    };
    const int w = bounds.Width(), h = bounds.Height();
    entry = 1;
    if (mode == CastleMode::Hill) {
        const int top = bounds.y0 + std::max(4, h / 4);
        const int bottom = bounds.y1 - std::max(4, h / 3);
        add({bounds.x0, bounds.y0, bounds.x1, top}, "throne_room");
        add({bounds.x0, bottom, bounds.x1, bounds.y1}, "entrance_hall");
        bank({bounds.x0, top, bounds.x1, bottom}, count - 2, program.chambers, false);
    } else if (mode == CastleMode::River) {
        const int elbow = bounds.x0 + std::clamp(w * 36 / 100, 4, w - 4);
        const int crossbar = bounds.y0 + std::max(4, h * 42 / 100);
        const int officeEnd = bounds.y0 + std::clamp(h / 3, 4, h - 4);
        add({bounds.x0, bounds.y0, elbow, officeEnd}, "throne_room");
        add({bounds.x0, officeEnd, elbow, bounds.y1}, "entrance_hall");
        bank({elbow, bounds.y0, bounds.x1, crossbar}, count - 2, program.chambers, false);
    } else {
        const int wing = std::clamp(w * (mode == CastleMode::Palace ? 26 : 28) / 100, 4, (w - 4) / 2);
        const int left = bounds.x0 + wing, right = bounds.x1 - wing;
        const int front = mode == CastleMode::Palace ? bounds.y0 + std::max(8, h * 62 / 100) : bounds.y1;
        const int principalEnd = bounds.y0 + std::clamp((front - bounds.y0) / 4, 4, front - bounds.y0 - 4);
        add({left, bounds.y0, right, principalEnd}, "throne_room");
        add({left, principalEnd, right, front}, "entrance_hall");
        const int leftCount = (count - 1) / 2, rightCount = count - 2 - leftCount;
        if (mode == CastleMode::Palace) {
            bank({bounds.x0, bounds.y0, left, bounds.y1}, leftCount,
                {"great_kitchen", "pantry", "scullery", "cold_store", "servants_hall"}, true);
            bank({right, bounds.y0, bounds.x1, bounds.y1}, rightCount,
                {"royal_chambers", "royal_chapel", "guest_apartment", "library", "music_room"}, false);
        } else {
            const int back = bounds.y0 + (mode == CastleMode::Fortress ? h / 3 : h / 5);
            const int end = mode == CastleMode::Fortress ? bounds.y1 : bounds.y1 - h / 5;
            bank({bounds.x0, back, left, end}, leftCount, {program.chambers[0], program.chambers[2], program.chambers[4]}, false);
            bank({right, back, bounds.x1, end}, rightCount, {program.chambers[1], program.chambers[3], program.chambers[5 % program.chambers.size()]}, false);
        }
    }
    if (!valid || static_cast<int>(result.size()) != count) result.clear();
    return result;
}

std::string RoomFurnishing(const std::string& role) {
    if (role.find("kitchen") != std::string::npos || role == "scullery") return "hearths";
    if (role == "great_hall" || role == "guard_hall") return "banquet_tables";
    if (role == "customs_hall") return "customs_counters";
    if (role == "command_hall" || role == "command_room" || role == "war_room" || role == "map_room") return "map_tables";
    if (role == "throne_room") return "throne_dais";
    if (role == "treasury" || role == "strongroom") return "strongboxes";
    if (role == "armory") return "weapon_racks";
    if (role.find("chapel") != std::string::npos) return "altar";
    if (role.find("chamber") != std::string::npos || role.find("quarters") != std::string::npos || role == "guest_apartment") return "beds";
    if (role.find("store") != std::string::npos || role == "pantry") return "grain_bins";
    if (role == "signal_room") return "signal_brazier";
    return "desks_and_shelves";
}

void FurnishCastleRoom(MapPlan& plan, int parentIndex, const std::string& pattern, double density = 1.0) {
    const MapPlanRegion parent = plan.regions[parentIndex];
    // Keep wall-side circulation and a cross aisle open. Equipment is an
    // additive semantic tile, just as in town; never stamp over doors.
    const Rect inside{parent.x0 + 2, parent.y0 + 2, parent.x1 - 1, parent.y1 - 1};
    if (inside.Width() < 2 || inside.Height() < 2) return;
    const int centerX = (parent.x0 + parent.x1 + 1) / 2;
    const int centerY = (parent.y0 + parent.y1 + 1) / 2;
    density = std::clamp(density, 0.35, 1.65);
    auto stamp = [&](Rect shape, const std::string& role, bool essential = false) {
        shape.x1 = std::min(shape.x1, inside.x1); shape.y1 = std::min(shape.y1, inside.y1);
        if (shape.Width() < 1 || shape.Height() < 1) return;
        const unsigned selector = static_cast<unsigned>(shape.x0 * 73856093) ^
            static_cast<unsigned>(shape.y0 * 19349663) ^ static_cast<unsigned>((parent.id + 31) * 83492791);
        if (!essential && density < 1.0 && selector % 100U >= static_cast<unsigned>(density * 100.0)) return;
        for (int y = shape.y0; y < shape.y1; ++y) for (int x = shape.x0; x < shape.x1; ++x) {
            if (x == centerX || y == centerY) return;
            const auto tile = plan.biomes[Index(x,y,plan.width)];
            if (tile != TOWN_FLOOR && tile != TOWN_WOOD_FLOOR && tile != TOWN_STONE_FLOOR) return;
        }
        PaintRect(plan.biomes, plan.width, plan.depth, shape, TOWN_FURNITURE);
        plan.regions.push_back({"furniture", 10000 + static_cast<int>(plan.regions.size()),
            shape.x0, shape.y0, shape.x1 - 1, shape.y1 - 1});
        plan.regions.back().role = role;
        plan.regions.back().parentId = parent.id;
        plan.regions.back().parentIndex = parentIndex;
    };
    if (pattern == "throne_dais" || pattern == "altar" || pattern == "signal_brazier") {
        stamp({inside.x0, inside.y0, std::min(centerX, inside.x0 + 3), inside.y0 + 2}, pattern, true);
        if (density > 1.2) stamp({inside.x1 - 2, inside.y0, inside.x1, inside.y0 + 2}, "ceremonial_furnishing");
    } else if (pattern == "hearths") {
        const int rowStep = std::max(3, static_cast<int>(std::lround(4.0 / std::max(1.0, density))));
        for (int y = inside.y0; y < inside.y1; y += rowStep) {
            stamp({inside.x0,y,inside.x0+2,y+2}, "cooking_hearth");
            stamp({inside.x1-2,y,inside.x1,y+2}, "kitchen_storage");
        }
        const int tableStep = std::max(3, static_cast<int>(std::lround(5.0 / std::max(1.0, density))));
        for (int x = inside.x0 + 4; x < inside.x1 - 3; x += tableStep) for (int y = inside.y0 + 2; y < inside.y1; y += tableStep)
            stamp({x,y,x+2,y+3}, "preparation_table");
    } else if (pattern == "banquet_tables" || pattern == "tables" || pattern == "map_tables" || pattern == "customs_counters") {
        const std::string role = pattern == "banquet_tables" ? "banquet_table" : pattern;
        const int columnStep = std::max(3, static_cast<int>(std::lround(5.0 / std::max(1.0, density))));
        for (int x = inside.x0; x < inside.x1; x += columnStep) {
            stamp({x,inside.y0,x+2,centerY-1}, role);
            stamp({x,centerY+2,x+2,inside.y1}, role);
        }
    } else {
        const int featureWidth = pattern == "boat_cradle" || pattern == "carts" ? 3 : 2;
        const int featureHeight = pattern == "boat_cradle" ? 6 :
            (pattern == "beds" || pattern == "hospital_beds" || pattern == "stalls" ? 2 : 1);
        const int xStep = std::max(featureWidth + 1, static_cast<int>(std::lround((featureWidth + 2) / std::max(1.0, density))));
        const int yStep = std::max(featureHeight + 1, static_cast<int>(std::lround((featureHeight + 2) / std::max(1.0, density))));
        for (int y = inside.y0; y < inside.y1; y += yStep) for (int x = inside.x0; x < inside.x1; x += xStep)
            stamp({x,y,x+featureWidth,y+featureHeight}, pattern);
    }
}

std::vector<Point> FindPath(
    const std::vector<std::uint8_t>& domain,
    const std::vector<std::uint8_t>& structure,
    const std::vector<std::uint8_t>& biomes,
    int width,
    int height,
    Point start,
    Point goal) {
    if (!InBounds(start.x, start.y, width, height) || !InBounds(goal.x, goal.y, width, height)) return {};
    const int total = width * height;
    const int infinity = std::numeric_limits<int>::max() / 4;
    const int startIndex = Index(start.x, start.y, width);
    const int goalIndex = Index(goal.x, goal.y, width);
    std::vector<int> distance(static_cast<std::size_t>(total), infinity);
    std::vector<int> previous(static_cast<std::size_t>(total), -1);
    using QueueItem = std::pair<int, int>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> pending;
    distance[static_cast<std::size_t>(startIndex)] = 0;
    pending.push({0, startIndex});
    const std::array<int, 4> dx{1, -1, 0, 0};
    const std::array<int, 4> dy{0, 0, 1, -1};

    auto passable = [&](int x, int y) {
        const int next = Index(x, y, width);
        if (domain[static_cast<std::size_t>(next)] == 0) return false;
        if (next == startIndex || next == goalIndex) return true;
        if (structure[static_cast<std::size_t>(next)] != 0) return false;
        const std::uint8_t biome = biomes[static_cast<std::size_t>(next)];
        return biome != MOUNTAIN && biome != EXMOUNTAIN && !IsWater(biome);
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
            // Existing lanes are cheaper than paving another parallel shortcut.
            const auto tile = biomes[static_cast<std::size_t>(next)];
            const int nextCost = currentCost + (IsRoadLike(tile) ? 2 : (tile == FOREST ? 9 : 6));
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

void PaintRoadPath(
    std::vector<std::uint8_t>& biomes,
    const std::vector<std::uint8_t>& domain,
    const std::vector<std::uint8_t>& structure,
    int width,
    int height,
    const std::vector<Point>& path,
    int halfWidth,
    std::vector<Point>* waterCrossings) {
    for (const Point& center : path) {
        for (int oy = -halfWidth; oy <= halfWidth; ++oy) {
            for (int ox = -halfWidth; ox <= halfWidth; ++ox) {
                const int x = center.x + ox;
                const int y = center.y + oy;
                if (!InBounds(x, y, width, height)) continue;
                const int index = Index(x, y, width);
                if (domain[static_cast<std::size_t>(index)] == 0 ||
                    (structure[static_cast<std::size_t>(index)] != 0 && !SamePoint(center, {x, y}))) continue;
                const std::uint8_t before = biomes[static_cast<std::size_t>(index)];
                if (before == MOUNTAIN || before == EXMOUNTAIN || before == SEA) continue;
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

}  // namespace

CastleMapImageGenerator::CastleMapImageGenerator(MapGenParams params) : MapImageGenerator(std::move(params)) {}

MapGenColor CastleMapImageGenerator::BiomeToColor(std::uint8_t biome) const {
    switch (biome) {
        case PLAIN:      return {128, 137, 116};
        case DESERT:     return {190, 170, 112};
        case FOREST:     return {76, 116, 72};
        case MOUNTAIN:   return {126, 129, 136};
        case EXMOUNTAIN: return {76, 78, 87};
        case ROAD:       return {150, 128, 94};
        case BRIDGE:     return {211, 175, 72};
        case RIVER:      return {46, 126, 198};
        case LAKE:       return {36, 78, 155};
        case SEA:        return {18, 22, 30};
        case TOWN_FLOOR: return {172, 153, 113};
        case TOWN_WOOD_FLOOR: return {158, 119, 80};
        case TOWN_STONE_FLOOR: return {177, 177, 172};
        case TOWN_PAVEMENT: return {160, 156, 139};
        case TOWN_FURNITURE: return {89, 61, 45};
        case CASTLE_WALL_WALK: return {139, 142, 145};
        default:         return {255, 0, 255};
    }
}

bool CastleMapImageGenerator::Generate(MapPlan& outPlan, std::string& outError) {
    const int width = params_.width;
    const int height = params_.depth;
    if (width < 64 || height < 64) {
        outError = "Castle map size must be at least 64x64";
        return false;
    }

    const int area = width * height;
    const int minDimension = std::min(width, height);
    const double scale = static_cast<double>(minDimension) / 192.0;
    // Type and size belong to the brief. Candidate search must not replace a
    // small mountain castle with a palace merely because it has more rooms.
    std::mt19937 programRng(CandidateSeed(params_.seed, 91));
    const CastleMode mode = ChooseMode(programRng, params_);
    const CastleProgram program = GetCastleProgram(CastleModeName(mode));
    std::mt19937 wealthRng(CandidateSeed(params_.seed, 701));
    double wealth = std::uniform_real_distribution<double>(0.7, 2.5)(wealthRng);
    if (const auto it = params_.extra.find("countryWealth"); it != params_.extra.end()) wealth = it->second;
    if (const auto it = params_.extra.find("wealth"); it != params_.extra.end()) wealth = it->second;
    wealth = std::clamp(wealth, 0.7, 2.5);
    std::mt19937 defenseRng(CandidateSeed(params_.seed, 809));
    double outerWallDefenseStrength = std::uniform_real_distribution<double>(0.0, 2.0)(defenseRng);
    if (const auto it = params_.extra.find("outerWallDefenseStrength"); it != params_.extra.end()) {
        outerWallDefenseStrength = it->second;
    }
    if (const auto it = params_.extra.find("outerDefense"); it != params_.extra.end()) {
        outerWallDefenseStrength = it->second;
    }
    outerWallDefenseStrength = std::clamp(outerWallDefenseStrength, 0.0, 2.0);
    const bool hasOuterWall = outerWallDefenseStrength >= 0.5;
    const bool hasMoat = outerWallDefenseStrength >= 1.5;
    const bool hasOuterDefenseEquipment = outerWallDefenseStrength > 1.1;
    const int wallVariant = std::uniform_int_distribution<int>(0, 2)(programRng);
    const int defenseLevel = std::clamp(BaseDefenseLevel(mode) +
        (outerWallDefenseStrength < 0.5 ? -2 : (outerWallDefenseStrength <= 1.1 ? -1 : 0)) +
        (outerWallDefenseStrength > 1.6 ? 1 : 0), 1, 5);
    const int requestedCandidateCount = std::clamp(static_cast<int>(std::lround(GetParam("candidateCount", 6.0))), 1, 12);
    // At the minimum supported size, a few random facility/tower placements
    // can block every route even though another deterministic candidate would
    // fit. Spend the available search budget automatically for that case;
    // explicit candidateCount remains an escape hatch for callers that need a
    // fixed generation budget.
    const bool candidateCountWasExplicit = params_.extra.find("candidateCount") != params_.extra.end();
    const int candidateCount = !candidateCountWasExplicit && scale < 0.40 ? 12 : requestedCandidateCount;
    int gateMin = std::clamp(static_cast<int>(std::lround(GetParam("gateCountMin", 1.0))), 1, 3);
    int gateMax = std::clamp(static_cast<int>(std::lround(GetParam("gateCountMax", 3.0))), gateMin, 3);
    const bool gateRangeWasExplicit = params_.extra.find("gateCountMin") != params_.extra.end() ||
        params_.extra.find("gateCountMax") != params_.extra.end();
    int towerMin = std::clamp(static_cast<int>(std::lround(GetParam("towerCountMin", 4.0))), 4, 12);
    int towerMax = std::clamp(static_cast<int>(std::lround(GetParam("towerCountMax", 8.0))), towerMin, 12);
    const bool towerRangeWasExplicit = params_.extra.find("towerCountMin") != params_.extra.end() ||
        params_.extra.find("towerCountMax") != params_.extra.end();
    int roomMin = std::clamp(static_cast<int>(std::lround(GetParam("keepRoomCountMin", 4.0))), 4, 32);
    int roomMax = std::clamp(static_cast<int>(std::lround(GetParam("keepRoomCountMax", 12.0))), roomMin, 32);
    const bool roomRangeWasExplicit = params_.extra.find("keepRoomCountMin") != params_.extra.end() ||
        params_.extra.find("keepRoomCountMax") != params_.extra.end();
    int facilityMin = std::clamp(static_cast<int>(std::lround(GetParam("outerFacilityCountMin", 4.0))), 4, 32);
    int facilityMax = std::clamp(static_cast<int>(std::lround(GetParam("outerFacilityCountMax", 12.0))), facilityMin, 32);
    const bool facilityRangeWasExplicit = params_.extra.find("outerFacilityCountMin") != params_.extra.end() ||
        params_.extra.find("outerFacilityCountMax") != params_.extra.end();
    const double outerFacilityDensity = std::clamp(GetParam("outerFacilityDensity", 2.0), 0.5, 3.0);
    const int wallThickness = std::clamp(static_cast<int>(std::lround(GetParam("wallThickness", 2.0))), 1, 3);
    const int moatMin = std::clamp(static_cast<int>(std::lround(GetParam("moatWidthMin", 2.0))), 2, 5);
    const int moatMax = std::clamp(static_cast<int>(std::lround(GetParam("moatWidthMax", 5.0))), moatMin, 5);
    const double extraEdgeRate = std::clamp(GetParam("roadExtraEdgeRate", 0.15), 0.0, 1.0);
    int mainRoadWidth = std::clamp(static_cast<int>(std::lround(GetParam("mainRoadWidth", 3.0))), 1, 5);
    const bool mainRoadWidthWasExplicit = params_.extra.find("mainRoadWidth") != params_.extra.end();

    // Small maps cannot support the full large-castle density. Keep the
    // structural minimums, but scale optional facilities, rooms, and towers
    // with the map while leaving explicit ranges authoritative.
    if (scale < 0.60) {
        if (!facilityRangeWasExplicit) facilityMax = std::min(facilityMax, 6);
        if (!roomRangeWasExplicit) roomMax = std::min(roomMax, 8);
    } else {
        if (!facilityRangeWasExplicit) {
            facilityMin = std::clamp(static_cast<int>(std::lround(4.0 * scale)), 4, 16);
            facilityMax = std::clamp(std::max(12, static_cast<int>(std::lround(8.0 * scale))), facilityMin, 32);
        }
        if (!roomRangeWasExplicit) {
            roomMin = std::clamp(static_cast<int>(std::lround(4.0 * scale)), 4, 16);
            roomMax = std::clamp(std::max(12, static_cast<int>(std::lround(8.0 * scale))), roomMin, 32);
        }
    }
    if (!towerRangeWasExplicit) {
        if (scale >= 4.0) {
            towerMin = 8;
            towerMax = 12;
        } else if (scale >= 3.0) {
            towerMin = 6;
            towerMax = 10;
        } else if (scale >= 2.0) {
            towerMin = 6;
            towerMax = 8;
        }
    }

    struct CandidateResult {
        MapPlan plan;
        MapQualityMetrics quality;
        double score = -std::numeric_limits<double>::infinity();
    };
    CandidateResult best;
    bool generated = false;
    if (!roomRangeWasExplicit) {
        roomMin = minDimension < 112 ? 4 : program.roomsMin;
        roomMax = minDimension < 112 ? 6 : std::min(32, program.roomsMax + std::max(0, minDimension / 160 - 1) * 2);
    }
    if (!facilityRangeWasExplicit) {
        facilityMin = minDimension < 112 ? (hasOuterDefenseEquipment ? 4 : 6) :
            std::clamp(static_cast<int>(std::lround(program.facilitiesMin * outerFacilityDensity)), 4, 32);
        facilityMax = minDimension < 112 ? (hasOuterDefenseEquipment ? 5 : 9) :
            std::clamp(static_cast<int>(std::lround((program.facilitiesMax +
                std::max(0, minDimension / 192 - 1) * 2) * outerFacilityDensity)), facilityMin, 32);
    }
    if (!roomRangeWasExplicit) {
        if (wealth < 1.0) {
            roomMin = std::max(4, static_cast<int>(std::floor(roomMin * wealth)));
            roomMax = std::max(roomMin, static_cast<int>(std::floor(roomMax * wealth)));
        } else {
            const int additions = static_cast<int>(std::ceil((wealth - 1.0) * 3.0));
            roomMin = std::min(32, roomMin + additions);
            roomMax = std::min(32, std::max(roomMin, roomMax + additions));
        }
    }
    if (!facilityRangeWasExplicit) {
        if (wealth < 1.0) {
            facilityMin = std::max(4, static_cast<int>(std::floor(facilityMin * wealth)));
            facilityMax = std::max(facilityMin, static_cast<int>(std::floor(facilityMax * wealth)));
        } else {
            const int additions = static_cast<int>(std::ceil((wealth - 1.0) * 8.0));
            facilityMin = std::min(32, facilityMin + additions);
            facilityMax = std::min(32, std::max(facilityMin, facilityMax + additions));
        }
    }
    if (!towerRangeWasExplicit) {
        if (!hasOuterDefenseEquipment) {
            towerMin = 0;
            towerMax = 0;
        } else {
            const int additions = static_cast<int>(std::ceil((outerWallDefenseStrength - 1.1) * 5.0));
            towerMin = std::clamp(towerMin + additions, 4, 12);
            towerMax = std::clamp(std::max(towerMin, towerMax + additions), towerMin, 12);
        }
    }
    if (!hasOuterDefenseEquipment) {
        towerMin = 0;
        towerMax = 0;
    }
    if (!gateRangeWasExplicit && minDimension >= 96) {
        gateMin = hasOuterDefenseEquipment ? 2 : 1;
        gateMax = outerWallDefenseStrength >= 1.6 ? 3 : gateMin;
    }
    if (!mainRoadWidthWasExplicit) {
        mainRoadWidth = wealth < 0.9 ? 2 : (wealth < 1.5 ? 3 : (wealth < 2.2 ? 4 : 5));
    }
    if (scale < 0.60) {
        if (!roomRangeWasExplicit) {
            roomMax = std::min(roomMax, 6);
            roomMin = 4;
        }
        if (!facilityRangeWasExplicit) {
            facilityMax = std::min(facilityMax, hasOuterDefenseEquipment ? 6 : 9);
            facilityMin = hasOuterDefenseEquipment ? 4 : std::min(6, facilityMax);
        }
        if (!towerRangeWasExplicit) {
            if (hasOuterDefenseEquipment) {
                towerMax = std::min(towerMax, 6);
                towerMin = 4;
            } else {
                towerMin = 0;
                towerMax = 0;
            }
        }
        if (!mainRoadWidthWasExplicit) mainRoadWidth = std::min(mainRoadWidth, 3);
    }

    for (int candidateIndex = 0;
        candidateIndex < candidateCount || (!candidateCountWasExplicit && !generated && candidateIndex < 12);
        ++candidateIndex) {
        std::mt19937 rng(CandidateSeed(params_.seed, candidateIndex));
        const int moatWidth = hasMoat ? std::uniform_int_distribution<int>(moatMin, moatMax)(rng) : 0;
        const int outerWallThickness = hasOuterWall ? wallThickness : 0;
        const int wallInset = hasMoat ? moatWidth : (hasOuterWall ? std::max(2, wallThickness) : 0);
        const Rect moatRect{0, 0, width, height};
        const Rect wallRect{wallInset, wallInset, width - wallInset, height - wallInset};
        const Rect innerRect{wallRect.x0 + outerWallThickness, wallRect.y0 + outerWallThickness,
            wallRect.x1 - outerWallThickness, wallRect.y1 - outerWallThickness};
        if (innerRect.Width() < 24 || innerRect.Height() < 24) continue;
        const int castleDimension = std::min(innerRect.Width(), innerRect.Height());

        MapPlan candidate;
        candidate.Reset(width, height, params_.seed, "castle");
        candidate.archetype = CastleModeName(mode);
        candidate.countryWealth = wealth;
        candidate.wealthClass = WealthClass(wealth);
        candidate.outerWallDefenseStrength = outerWallDefenseStrength;
        candidate.castleWallForm = !hasOuterWall ? "open_estate" :
            (!hasMoat ? std::string("dry_") + WallFormName(mode, wallVariant) : WallFormName(mode, wallVariant));
        candidate.castleDefenseLevel = defenseLevel;
        candidate.castleDevelopmentModel = "wealth_and_construction_history_v1";
        candidate.biomes.assign(static_cast<std::size_t>(area), CastleExteriorBiome(mode));
        std::vector<std::uint8_t>& biomes = candidate.biomes;
        std::vector<std::uint8_t> domain(static_cast<std::size_t>(area), 0);
        std::vector<std::uint8_t> structure(static_cast<std::size_t>(area), 0);
        candidate.castleDefenseMask.assign(area, 0);
        auto& defense = candidate.castleDefenseMask;
        // Straight wall faces meet gently irregular cut-back corners. The
        // ridge constrains the upper ward; a river constrains one flank;
        // formal palaces retain broad, almost rectangular courts.
        const int contourScale = minDimension < 96 ? 0 : std::min(wallRect.Width(), wallRect.Height());
        const int baseCut = static_cast<int>(contourScale * std::uniform_real_distribution<double>(0.08, 0.16)(rng));
        std::array<int, 4> cuts{baseCut, baseCut, baseCut, baseCut}; // NW, NE, SW, SE
        if (mode == CastleMode::Hill) cuts = {contourScale / 3, contourScale / 3, baseCut / 2, baseCut / 2};
        if (mode == CastleMode::River) cuts = {contourScale / 4, baseCut / 2, contourScale / 5, baseCut / 2};
        if (mode == CastleMode::Palace) cuts.fill(baseCut / 3);
        if (mode == CastleMode::Fortress) cuts = {baseCut / 2, baseCut / 2, contourScale / 5, contourScale / 5};
        const int variantCut = std::max(1, contourScale / 48);
        if (wallVariant == 1) {
            cuts[0] = std::min(contourScale / 3, cuts[0] + variantCut);
            cuts[3] = std::min(contourScale / 3, cuts[3] + variantCut);
        } else if (wallVariant == 2) {
            cuts[1] = std::min(contourScale / 3, cuts[1] + variantCut);
            cuts[2] = std::min(contourScale / 3, cuts[2] + variantCut);
        }
        const int contourRoughness = contourScale < 96 ? 0 : std::clamp(contourScale / 36, 2, 7);
        const int contourStride = std::clamp(contourScale / 24, 4, 12);
        const int contourProfileSize = width + height + 1;
        std::array<std::vector<int>, 4> contourOffsets;
        std::mt19937 contourRng(CandidateSeed(params_.seed, 1201 + candidateIndex));
        for (auto& profile : contourOffsets) {
            profile.resize(static_cast<std::size_t>(contourProfileSize), 0);
            int offset = contourRoughness == 0 ? 0 :
                std::uniform_int_distribution<int>(-contourRoughness, contourRoughness)(contourRng);
            int target = offset;
            for (int position = 0; position < contourProfileSize; ++position) {
                if (contourRoughness > 0 && position % contourStride == 0) {
                    target = std::uniform_int_distribution<int>(-contourRoughness, contourRoughness)(contourRng);
                }
                offset += (target > offset) - (target < offset);
                profile[static_cast<std::size_t>(position)] = offset;
            }
        }
        const auto contourOffset = [&](int corner, int tangent) {
            return contourOffsets[static_cast<std::size_t>(corner)][static_cast<std::size_t>(
                std::clamp(tangent + height, 0, contourProfileSize - 1))];
        };
        for (int y = wallRect.y0; y < wallRect.y1; ++y) {
            for (int x = wallRect.x0; x < wallRect.x1; ++x) {
                const int west = x - wallRect.x0, east = wallRect.x1 - 1 - x;
                const int north = y - wallRect.y0, south = wallRect.y1 - 1 - y;
                if (west + north >= std::max(0, cuts[0] + contourOffset(0, west - north)) &&
                    east + north >= std::max(0, cuts[1] + contourOffset(1, east - north)) &&
                    west + south >= std::max(0, cuts[2] + contourOffset(2, west - south)) &&
                    east + south >= std::max(0, cuts[3] + contourOffset(3, east - south))) {
                    domain[static_cast<std::size_t>(Index(x, y, width))] = 1;
                    biomes[static_cast<std::size_t>(Index(x, y, width))] = PLAIN;
                }
            }
        }
        const auto insideDistance = MaskDistance(domain, width, height, false);
        const auto outsideDistance = MaskDistance(domain, width, height, true);
        for (int i = 0; i < area; ++i) {
            if (hasMoat && !domain[i] && outsideDistance[i] <= moatWidth) { biomes[i] = RIVER; defense[i] = 1; }
            if (hasOuterWall && domain[i] && insideDistance[i] <= outerWallThickness) {
                biomes[i] = MOUNTAIN; structure[i] = 1; defense[i] = 2;
            }
        }
        candidate.regions.push_back({"fortress", 0, wallRect.x0, wallRect.y0, wallRect.x1 - 1, wallRect.y1 - 1});
        candidate.regions.back().role = "fortress_footprint";
        if (hasMoat) {
            candidate.regions.push_back({"moat", 0, moatRect.x0, moatRect.y0, moatRect.x1 - 1, moatRect.y1 - 1});
            candidate.regions.back().role = "moat";
        }
        if (hasOuterWall) {
            candidate.regions.push_back({"curtain_wall", 0, wallRect.x0, wallRect.y0, wallRect.x1 - 1, wallRect.y1 - 1});
            candidate.regions.back().role = "curtain_wall";
        }
        candidate.regions.push_back({"outer_bailey", 0, innerRect.x0, innerRect.y0, innerRect.x1 - 1, innerRect.y1 - 1});
        candidate.regions.back().role = "outer_bailey";
        const int wallWalkWidth = hasOuterDefenseEquipment ?
            std::min(outerWallThickness, outerWallDefenseStrength >= 1.6 ? 2 : 1) : 0;
        int wallWalkCells = 0;
        for (int i = 0; i < area; ++i) {
            if (wallWalkWidth > 0 && defense[i] == 2 && insideDistance[i] > outerWallThickness - wallWalkWidth) {
                biomes[i] = CASTLE_WALL_WALK;
                ++wallWalkCells;
            }
        }
        if (wallWalkCells > 0) {
            candidate.regions.push_back({"wall_walk", 0, innerRect.x0, innerRect.y0, innerRect.x1 - 1, innerRect.y1 - 1});
            candidate.regions.back().role = outerWallDefenseStrength >= 1.6 ? "armed_wall_walk" : "wall_walk";
        }

        if (mode == CastleMode::River) {
            const int riverWidth = std::clamp(minDimension / 24, 2, 5);
            // The water protects the constrained western flank; it does not
            // cut through the castle only to be erased by the building pass.
            for (int y = 0; y < height; ++y) for (int x = 0; x < moatRect.x0 + riverWidth; ++x) {
                const int i = Index(x, y, width);
                if (!domain[i]) biomes[i] = RIVER;
            }
            candidate.regions.push_back({"waterway", 0, 0, 0, moatRect.x0 + riverWidth - 1, height - 1});
            candidate.regions.back().role = "river_flank";
        }

        const int centerX = width / 2;
        const int centerY = height / 2;
        int gateCount = hasOuterWall ? std::uniform_int_distribution<int>(gateMin, gateMax)(rng) : 1;
        gateCount = std::clamp(gateCount, gateMin, gateMax);
        std::vector<Gate> gates;
        std::vector<int> sideOrder{0, 1, 2, 3}; // south first: the ceremonial axis
        std::shuffle(sideOrder.begin() + 1, sideOrder.end(), rng);
        const int gateOffset = std::max(5, minDimension / 10);
        for (int gateIndex = 0; gateIndex < gateCount; ++gateIndex) {
            const int side = sideOrder[static_cast<std::size_t>(gateIndex % sideOrder.size())];
            bool placed = false;
            for (int attempt = 0; attempt < 80 && !placed; ++attempt) {
                const int midpoint = side < 2 ? wallRect.CenterX() : wallRect.CenterY();
                const int spread = std::max(0, std::min(gateOffset, (side < 2 ? wallRect.Width() : wallRect.Height()) / 8));
                const int coordinate = gateIndex == 0 && mode == CastleMode::Palace ? midpoint :
                    midpoint + std::uniform_int_distribution<int>(-spread, spread)(rng);
                Gate gate;
                gate.side = side;
                gate.major = gateIndex == 0;
                gate.halfWidth = gate.major ? (mainRoadWidth - 1) / 2 : 0;
                if (side == 0) {
                    gate.outer = {coordinate, moatRect.y1 - 1};
                    gate.inner = {coordinate, wallRect.y1 - outerWallThickness - 1};
                    for (int y = gate.outer.y; y >= gate.inner.y; --y) gate.bridge.push_back({coordinate, y});
                } else if (side == 1) {
                    gate.outer = {coordinate, moatRect.y0};
                    gate.inner = {coordinate, wallRect.y0 + outerWallThickness};
                    for (int y = gate.outer.y; y <= gate.inner.y; ++y) gate.bridge.push_back({coordinate, y});
                } else if (side == 2) {
                    gate.outer = {moatRect.x0, coordinate};
                    gate.inner = {wallRect.x0 + outerWallThickness, coordinate};
                    for (int x = gate.outer.x; x <= gate.inner.x; ++x) gate.bridge.push_back({x, coordinate});
                } else {
                    gate.outer = {moatRect.x1 - 1, coordinate};
                    gate.inner = {wallRect.x1 - outerWallThickness - 1, coordinate};
                    for (int x = gate.outer.x; x >= gate.inner.x; --x) gate.bridge.push_back({x, coordinate});
                }
                if (gate.halfWidth > 0) {
                    const std::vector<Point> centerLine = gate.bridge;
                    gate.bridge.clear();
                    for (const Point& point : centerLine) {
                        for (int offset = -gate.halfWidth; offset <= gate.halfWidth; ++offset) {
                            gate.bridge.push_back(gate.side < 2 ? Point{point.x + offset, point.y} : Point{point.x, point.y + offset});
                        }
                    }
                }
                bool tooClose = false;
                for (const Gate& existing : gates) {
                    if (Manhattan(existing.inner, gate.inner) < minDimension / 4) tooClose = true;
                }
                if (tooClose) continue;
                gates.push_back(gate);
                placed = true;
            }
        }
        if (static_cast<int>(gates.size()) < (hasOuterWall ? gateMin : 1)) continue;
        for (std::size_t gateIndex = 0; gateIndex < gates.size(); ++gateIndex) {
            Gate& gate = gates[gateIndex];
            for (const Point& point : gate.bridge) {
                if (!InBounds(point.x, point.y, width, height)) continue;
                const int index = Index(point.x, point.y, width);
                biomes[static_cast<std::size_t>(index)] = BRIDGE;
                structure[static_cast<std::size_t>(index)] = 0;
                if (defense[index] == 1) defense[index] = 4;
                else if (defense[index] == 2) defense[index] = 5;
            }
            if (!hasOuterWall) {
                candidate.markers.push_back({"map_exit", gate.inner.x, gate.inner.y, static_cast<int>(gateIndex)});
                candidate.markers.back().role = "open_castle_approach";
                continue;
            }
            candidate.markers.push_back({"gate", gate.inner.x, gate.inner.y, static_cast<int>(gateIndex)});
            candidate.markers.back().role = gate.major ? "main_gate" : "postern_gate";
            candidate.markers.push_back({"entrance", gate.outer.x, gate.outer.y, static_cast<int>(gateIndex)});
            candidate.markers.back().role = gate.major ? "main_entrance" : "postern_entrance";
            if (hasOuterDefenseEquipment) {
                const int gateMargin = 2 + gate.halfWidth;
                const int minX = std::min(gate.outer.x, gate.inner.x) - gateMargin;
                const int maxX = std::max(gate.outer.x, gate.inner.x) + gateMargin + 1;
                const int minY = std::min(gate.outer.y, gate.inner.y) - gateMargin;
                const int maxY = std::max(gate.outer.y, gate.inner.y) + gateMargin + 1;
                candidate.regions.push_back({"gatehouse", static_cast<int>(gateIndex), std::max(0, minX), std::max(0, minY), std::min(width - 1, maxX), std::min(height - 1, maxY)});
                candidate.regions.back().role = gate.major ? "great_gatehouse" : "postern_gatehouse";
            }
            if (hasMoat) {
                const Point middle = gate.bridge[gate.bridge.size() / 2];
                Point crossing = middle;
                int crossingDistance = std::numeric_limits<int>::max();
                for (const Point& bridgePoint : gate.bridge) {
                    bool adjacentWater = false;
                    for (const Point& offset : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                        const int nx = bridgePoint.x + offset.x;
                        const int ny = bridgePoint.y + offset.y;
                        if (InBounds(nx, ny, width, height) && IsWater(biomes[static_cast<std::size_t>(Index(nx, ny, width))])) {
                            adjacentWater = true;
                            break;
                        }
                    }
                    const int distance = Manhattan(bridgePoint, middle);
                    if (adjacentWater && distance < crossingDistance) {
                        crossing = bridgePoint;
                        crossingDistance = distance;
                    }
                }
                candidate.markers.push_back({"water_crossing", crossing.x, crossing.y, static_cast<int>(gateIndex)});
                candidate.markers.back().role = gate.major ? "main_gate_bridge" : "postern_bridge";
            }
            Point wallAccess{-1, -1};
            int accessDistance = std::numeric_limits<int>::max();
            const int searchRadius = gate.halfWidth + outerWallThickness + 2;
            for (int y = gate.inner.y - searchRadius; y <= gate.inner.y + searchRadius; ++y) {
                for (int x = gate.inner.x - searchRadius; x <= gate.inner.x + searchRadius; ++x) {
                    if (!InBounds(x, y, width, height) || biomes[Index(x, y, width)] != CASTLE_WALL_WALK) continue;
                    const int distance = Manhattan({x, y}, gate.inner);
                    if (distance < accessDistance) {
                        accessDistance = distance;
                        wallAccess = {x, y};
                    }
                }
            }
            if (wallAccess.x >= 0) {
                candidate.markers.push_back({"wall_access", wallAccess.x, wallAccess.y, static_cast<int>(gateIndex)});
                candidate.markers.back().role = gate.major ? "great_gatehouse_wall_stairs" : "postern_wall_stairs";
            }
        }

        // Give each gate a readable gatehouse instead of leaving the bridge
        // as a single isolated opening in the curtain wall.  The central
        // lane stays open so the subsequent route graph can still enter the
        // bailey; the two wings are protected from later road painting.
        const int gatehouseHalfWidth = std::clamp(minDimension / 48, 2, 4);
        const int gatehouseDepth = std::clamp(minDimension / 64, 2, 4);
        for (std::size_t gateIndex = 0; hasOuterDefenseEquipment && gateIndex < gates.size(); ++gateIndex) {
            const Gate& gate = gates[gateIndex];
            const int currentHalfWidth = gatehouseHalfWidth + (gate.major && defenseLevel >= 4 ? 1 : 0);
            const int currentDepth = gatehouseDepth + (gate.major && wealth >= 1.5 ? 1 : 0);
            int x0 = gate.inner.x - currentHalfWidth;
            int y0 = gate.inner.y - currentDepth;
            int x1 = gate.inner.x + currentHalfWidth + 1;
            int y1 = gate.inner.y + currentDepth + 1;
            if (gate.side == 0) {
                y0 = std::max(innerRect.y0 + 1, gate.inner.y - currentDepth);
                y1 = std::min(innerRect.y1 - 1, gate.inner.y + 1);
            } else if (gate.side == 1) {
                y0 = std::max(innerRect.y0 + 1, gate.inner.y);
                y1 = std::min(innerRect.y1 - 1, gate.inner.y + currentDepth + 1);
            } else if (gate.side == 2) {
                x0 = std::max(innerRect.x0 + 1, gate.inner.x);
                x1 = std::min(innerRect.x1 - 1, gate.inner.x + currentDepth + 1);
            } else {
                x0 = std::max(innerRect.x0 + 1, gate.inner.x - currentDepth);
                x1 = std::min(innerRect.x1 - 1, gate.inner.x + 1);
            }
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const bool laneOpen = gate.side < 2
                        ? std::abs(x - gate.inner.x) <= gate.halfWidth
                        : std::abs(y - gate.inner.y) <= gate.halfWidth;
                    if (laneOpen || !InBounds(x, y, width, height)) continue;
                    const int index = Index(x, y, width);
                    if (biomes[static_cast<std::size_t>(index)] == BRIDGE) continue;
                    biomes[static_cast<std::size_t>(index)] = EXMOUNTAIN;
                    structure[static_cast<std::size_t>(index)] = 1;
                }
            }
            for (MapPlanRegion& region : candidate.regions) {
                if (region.kind != "gatehouse" || region.id != static_cast<int>(gateIndex)) continue;
                region.x0 = std::min(region.x0, x0);
                region.y0 = std::min(region.y0, y0);
                region.x1 = std::max(region.x1, x1 - 1);
                region.y1 = std::max(region.y1, y1 - 1);
            }
        }

        const double compactRatio = minDimension < 112 ? 0.42 : program.keepWidth;
        const int keepWidth = std::clamp(static_cast<int>(std::lround(GetParam("keepWidth", innerRect.Width() * compactRatio))),
            16, std::max(16, innerRect.Width() - 22));
        const int keepHeight = std::clamp(static_cast<int>(std::lround(GetParam("keepHeight", innerRect.Height() *
            (minDimension < 112 ? 0.40 : program.keepDepth)))), 16, std::max(16, innerRect.Height() - 22));
        const int keepCenterX = centerX + (mode == CastleMode::River ? std::max(4, innerRect.Width() / 32) : 0);
        const int keepY = innerRect.y0 + std::max(10, innerRect.Height() / (mode == CastleMode::Hill ? 6 : 8)) + keepHeight / 2;
        const Rect keep{
            std::clamp(keepCenterX - keepWidth / 2, innerRect.x0 + 2, innerRect.x1 - keepWidth - 2),
            std::clamp(keepY - keepHeight / 2, innerRect.y0 + 2, innerRect.y1 - keepHeight - 2),
            0,
            0,
        };
        Rect keepFixed = keep;
        keepFixed.x1 = keepFixed.x0 + keepWidth;
        keepFixed.y1 = keepFixed.y0 + keepHeight;
        int wardGap = std::clamp(castleDimension / 20, 4, 20);
        Rect innerWard = keepFixed;
        bool hasInnerWard = false;
        for (int candidateGap = wardGap; candidateGap >= 4 && minDimension >= 112; --candidateGap) {
            const Rect option{keepFixed.x0 - candidateGap, keepFixed.y0 - candidateGap,
                keepFixed.x1 + candidateGap, keepFixed.y1 + candidateGap};
            bool clear = option.x0 > innerRect.x0 + 1 && option.y0 > innerRect.y0 + 1 &&
                option.x1 < innerRect.x1 - 1 && option.y1 < innerRect.y1 - 1;
            for (int y = option.y0; y < option.y1 && clear; ++y) {
                for (int x = option.x0; x < option.x1; ++x) {
                    if (!domain[Index(x, y, width)] || structure[Index(x, y, width)]) {
                        clear = false;
                        break;
                    }
                }
            }
            if (!clear) continue;
            wardGap = candidateGap;
            innerWard = option;
            hasInnerWard = true;
            break;
        }
        const Rect towerExclusion = hasInnerWard ? innerWard : keepFixed;
        const int towerCount = std::uniform_int_distribution<int>(towerMin, towerMax)(rng);
        const int towerBaseSize = std::clamp(castleDimension / 16, 5, 11);
        std::vector<Point> towerApproaches;
        const std::array<Point, 12> towerCenters{{
            {wallRect.x0 + towerBaseSize / 2 + 1, wallRect.y0 + towerBaseSize / 2 + 1},
            {wallRect.x1 - towerBaseSize / 2 - 2, wallRect.y0 + towerBaseSize / 2 + 1},
            {wallRect.x0 + towerBaseSize / 2 + 1, wallRect.y1 - towerBaseSize / 2 - 2},
            {wallRect.x1 - towerBaseSize / 2 - 2, wallRect.y1 - towerBaseSize / 2 - 2},
            {centerX, wallRect.y0 + towerBaseSize / 2 + 1},
            {centerX, wallRect.y1 - towerBaseSize / 2 - 2},
            {wallRect.x0 + towerBaseSize / 2 + 1, centerY},
            {wallRect.x1 - towerBaseSize / 2 - 2, centerY},
            {wallRect.x0 + wallRect.Width() / 3, wallRect.y0},
            {wallRect.x0 + wallRect.Width() * 2 / 3, wallRect.y0},
            {wallRect.x0 + wallRect.Width() / 3, wallRect.y1 - 1},
            {wallRect.x0 + wallRect.Width() * 2 / 3, wallRect.y1 - 1},
        }};
        for (int towerIndex = 0; towerIndex < towerCount; ++towerIndex) {
            const Point center = towerCenters[static_cast<std::size_t>(towerIndex)];
            const bool cornerTower = towerIndex < 4;
            const int sizeVariation = std::uniform_int_distribution<int>(-1, 1)(rng);
            const int fortressBoost = mode == CastleMode::Fortress && cornerTower && minDimension >= 96 ? 2 : 0;
            const int towerWidth = std::clamp(towerBaseSize + sizeVariation + fortressBoost, 5, 11);
            const int towerHeight = std::clamp(towerBaseSize - sizeVariation + fortressBoost, 5, 11);
            // Move each bastion to the nearest buildable point on its actual
            // wall face. Never index a fixed eight-tower array for 9..12 towers.
            Rect tower;
            int towerDistance = std::numeric_limits<int>::max();
            for (int ty = innerRect.y0; ty + towerHeight <= innerRect.y1; ++ty) {
                for (int tx = innerRect.x0; tx + towerWidth <= innerRect.x1; ++tx) {
                    Rect option{tx, ty, tx + towerWidth, ty + towerHeight};
                    if (option.x0 < towerExclusion.x1 + 2 && option.x1 > towerExclusion.x0 - 2 &&
                        option.y0 < towerExclusion.y1 + 2 && option.y1 > towerExclusion.y0 - 2) continue;
                    const int distance = Manhattan({option.CenterX(), option.CenterY()}, center);
                    if (distance >= towerDistance) continue;
                    bool clear = true;
                    for (int y = ty - 1; y <= option.y1 && clear; ++y) for (int x = tx - 1; x <= option.x1; ++x) {
                        const int i = Index(x, y, width);
                        if (!domain[i] || (structure[i] && defense[i] != 2) || biomes[i] == BRIDGE ||
                            (Contains(option, x, y) && defense[i] == 2)) { clear = false; break; }
                    }
                    if (!clear) continue;
                    tower = option;
                    towerDistance = distance;
                }
            }
            if (towerDistance == std::numeric_limits<int>::max()) continue;
            PaintRect(biomes, width, height, tower, EXMOUNTAIN);
            for (int y = tower.y0 + 1; y < tower.y1 - 1; ++y) {
                for (int x = tower.x0 + 1; x < tower.x1 - 1; ++x) {
                    biomes[static_cast<std::size_t>(Index(x, y, width))] = TOWN_STONE_FLOOR;
                }
            }
            for (int y = tower.y0; y < tower.y1; ++y) for (int x = tower.x0; x < tower.x1; ++x) structure[static_cast<std::size_t>(Index(x, y, width))] = 1;
            Point door{tower.CenterX(), tower.CenterY()};
            if (std::abs(center.x - centerX) >= std::abs(center.y - centerY)) {
                door.x = center.x < centerX ? tower.x1 - 1 : tower.x0;
            } else {
                door.y = center.y < centerY ? tower.y1 - 1 : tower.y0;
            }
            Point approach = door;
            if (door.x == tower.x0) --approach.x;
            else if (door.x == tower.x1 - 1) ++approach.x;
            else if (door.y == tower.y0) --approach.y;
            else ++approach.y;
            const int approachIndex = Index(approach.x, approach.y, width);
            biomes[static_cast<std::size_t>(approachIndex)] = CASTLE_WALL_WALK;
            structure[static_cast<std::size_t>(approachIndex)] = 0;
            biomes[static_cast<std::size_t>(Index(door.x, door.y, width))] = BRIDGE;
            candidate.regions.push_back({"tower", towerIndex, tower.x0, tower.y0, tower.x1 - 1, tower.y1 - 1});
            if (cornerTower) {
                candidate.regions.back().role = mode == CastleMode::Fortress ? "corner_bastion" : "corner_tower";
            } else {
                switch (mode) {
                    case CastleMode::Hill: candidate.regions.back().role = "signal_watch_tower"; break;
                    case CastleMode::River: candidate.regions.back().role = "river_watch_tower"; break;
                    case CastleMode::Palace: candidate.regions.back().role = "garden_watch_tower"; break;
                    case CastleMode::Fortress: candidate.regions.back().role = "artillery_watch_tower"; break;
                    default: candidate.regions.back().role = "watch_tower"; break;
                }
            }
            candidate.markers.push_back({"landmark", tower.CenterX(), tower.CenterY(), towerIndex});
            candidate.markers.back().role = candidate.regions.back().role;
            candidate.markers.push_back({"stairs", door.x, door.y, towerIndex});
            candidate.markers.back().role = "tower_stairs";
            towerApproaches.push_back(approach);
        }
        if (static_cast<int>(towerApproaches.size()) < towerMin) continue;

        std::vector<std::pair<int, std::string>> keepAnnexFurnishings;
        const Rect keepInner{keepFixed.x0 + 2, keepFixed.y0 + 2, keepFixed.x1 - 2, keepFixed.y1 - 2};
        std::vector<std::vector<RoomInfo>> layouts;
        for (int count = roomMin; count <= roomMax; ++count) {
            int ignoredEntry = 0;
            auto layout = PlanKeepRooms(mode, program, keepInner, count, ignoredEntry);
            if (!layout.empty()) layouts.push_back(std::move(layout));
        }
        if (layouts.empty()) continue;
        std::vector<RoomInfo> rooms = layouts[std::uniform_int_distribution<int>(0, static_cast<int>(layouts.size()) - 1)(rng)];
        const int entryRoomIndex = 1;
        candidate.regions.push_back({"keep", 0, keepFixed.x0, keepFixed.y0, keepFixed.x1 - 1, keepFixed.y1 - 1});
        candidate.regions.back().role = "keep";
        candidate.regions.push_back({"keep_layout", 0, keepFixed.x0, keepFixed.y0, keepFixed.x1 - 1, keepFixed.y1 - 1});
        candidate.regions.back().role = program.layout;
        // Build masonry around the union of rooms. Recesses/courts are real
        // open space, so U/L/cross/T plans do not become rectangular boxes.
        for (const auto& room : rooms) {
            const Rect shell{room.rect.x0 - 2, room.rect.y0 - 2, room.rect.x1 + 2, room.rect.y1 + 2};
            PaintRect(biomes, width, height, shell, EXMOUNTAIN);
            for (int y = shell.y0; y < shell.y1; ++y) for (int x = shell.x0; x < shell.x1; ++x) structure[Index(x, y, width)] = 1;
        }
        for (auto& room : rooms) {
            const bool wood = room.role.find("chamber") != std::string::npos || room.role.find("quarters") != std::string::npos ||
                room.role == "guard_hall" || room.role == "guest_apartment";
            PaintRect(biomes, width, height, room.rect, static_cast<std::uint8_t>(wood ? TOWN_WOOD_FLOOR : TOWN_STONE_FLOOR));
            PaintRing(biomes, structure, width, height, room.rect, 1, MOUNTAIN);
            candidate.regions.push_back({"room", room.regionId, room.rect.x0, room.rect.y0, room.rect.x1 - 1, room.rect.y1 - 1});
            candidate.regions.back().role = room.role;
            candidate.markers.push_back({"landmark", room.rect.CenterX(), room.rect.CenterY(), room.regionId});
            candidate.markers.back().role = room.role;
        }
        auto openRoomCell = [&](Point point) {
            if (!InBounds(point.x, point.y, width, height)) return;
            const int index = Index(point.x, point.y, width);
            biomes[index] = BRIDGE; structure[index] = 0;
        };
        auto openRoomDoor = [&](Point point, int id) {
            openRoomCell(point);
            candidate.markers.push_back({"room_door", point.x, point.y, id});
            candidate.markers.back().role = rooms[id].role + "_door";
        };
        struct RoomEdge { int a; int b; Point doorA; Point doorB; int priority; };
        std::vector<RoomEdge> roomEdges;
        for (int a = 0; a < static_cast<int>(rooms.size()); ++a) for (int b = a + 1; b < static_cast<int>(rooms.size()); ++b) {
            const auto& ar = rooms[a].rect; const auto& br = rooms[b].rect;
            const int y0 = std::max(ar.y0, br.y0) + 1, y1 = std::min(ar.y1, br.y1) - 2;
            const int x0 = std::max(ar.x0, br.x0) + 1, x1 = std::min(ar.x1, br.x1) - 2;
            const int priority = a < 2 || b < 2 ? 0 : 1;
            if (y0 <= y1 && ar.x1 == br.x0) roomEdges.push_back({a,b,{ar.x1-1,(y0+y1)/2},{br.x0,(y0+y1)/2},priority});
            if (y0 <= y1 && br.x1 == ar.x0) roomEdges.push_back({a,b,{ar.x0,(y0+y1)/2},{br.x1-1,(y0+y1)/2},priority});
            if (x0 <= x1 && ar.y1 == br.y0) roomEdges.push_back({a,b,{(x0+x1)/2,ar.y1-1},{(x0+x1)/2,br.y0},priority});
            if (x0 <= x1 && br.y1 == ar.y0) roomEdges.push_back({a,b,{(x0+x1)/2,ar.y0},{(x0+x1)/2,br.y1-1},priority});
        }
        std::stable_sort(roomEdges.begin(), roomEdges.end(), [](const RoomEdge& a, const RoomEdge& b) { return a.priority < b.priority; });
        std::vector<bool> roomConnected(rooms.size(), false);
        roomConnected[entryRoomIndex] = true;
        int roomConnections = 1;
        while (roomConnections < static_cast<int>(rooms.size())) {
            bool found = false;
            for (const auto& edge : roomEdges) {
                if (roomConnected[edge.a] == roomConnected[edge.b]) continue;
                if (roomConnected[edge.a]) { openRoomCell(edge.doorA); openRoomDoor(edge.doorB, edge.b); }
                else { openRoomDoor(edge.doorA, edge.a); openRoomCell(edge.doorB); }
                roomConnected[edge.a] = roomConnected[edge.b] = true;
                ++roomConnections; found = true; break;
            }
            if (!found) break;
        }
        if (roomConnections != static_cast<int>(rooms.size())) continue;
        const RoomInfo& entryRoom = rooms[entryRoomIndex];
        const Point keepDoor{entryRoom.rect.CenterX(), entryRoom.rect.y1 + 1};
        openRoomDoor({entryRoom.rect.CenterX(), entryRoom.rect.y1 - 1}, entryRoom.regionId);
        openRoomCell({keepDoor.x, keepDoor.y - 1});
        openRoomCell(keepDoor);
        candidate.markers.push_back({"service_entry", keepDoor.x, keepDoor.y, 0});
        candidate.markers.back().role = "keep_public_entry";
        // A palace's entrance opens into the court between its long wings;
        // the river house opens beside a working recess in its L-shaped body.
        if (mode == CastleMode::Palace || mode == CastleMode::River) {
            Rect court = mode == CastleMode::Palace ?
                Rect{entryRoom.rect.x0 + 2, keepDoor.y + 1, entryRoom.rect.x1 - 2, keepFixed.y1} :
                Rect{entryRoom.rect.x1 + 2, rooms[2].rect.y1 + 2, keepFixed.x1, keepFixed.y1};
            if (court.Width() >= 3 && court.Height() >= 3) {
                for (int y = court.y0; y < court.y1; ++y) for (int x = court.x0; x < court.x1; ++x) {
                    const int i = Index(x,y,width);
                    if (!structure[i] && !IsRoadLike(biomes[i])) {
                        biomes[i] = static_cast<std::uint8_t>(mode == CastleMode::Palace ? TOWN_STONE_FLOOR : TOWN_FLOOR);
                    }
                }
                candidate.regions.push_back({"keep_court",0,court.x0,court.y0,court.x1-1,court.y1-1});
                candidate.regions.back().role = mode == CastleMode::Palace ? "court_of_honor" : "customs_loading_court";
            }
        }
        const int keepAnnexCount = hasInnerWard ? (wealth >= 2.15 ? 2 : (wealth >= 1.5 ? 1 : 0)) : 0;
        for (int annexIndex = 0; annexIndex < keepAnnexCount; ++annexIndex) {
            const int side = annexIndex == 0 ? 1 : -1;
            const int annexDepth = std::min(std::max(4, castleDimension / 26), wardGap - 2);
            if (annexDepth < 4) continue;
            const int probeX = side > 0 ? keepFixed.x1 - 4 : keepFixed.x0 + 3;
            int connectionY = -1;
            for (int offset = 0; offset < keepFixed.Height() / 2 && connectionY < 0; ++offset) {
                for (const int y : {keepFixed.CenterY() - offset, keepFixed.CenterY() + offset}) {
                    if (y <= keepFixed.y0 + 2 || y >= keepFixed.y1 - 3) continue;
                    const std::uint8_t tile = biomes[static_cast<std::size_t>(Index(probeX, y, width))];
                    if (tile == TOWN_FLOOR || tile == TOWN_WOOD_FLOOR || tile == TOWN_STONE_FLOOR) {
                        connectionY = y;
                        break;
                    }
                }
            }
            if (connectionY < 0) continue;
            const int annexHeight = std::clamp(keepFixed.Height() / 3, 10, keepFixed.Height() - 6);
            const int annexY = std::clamp(connectionY - annexHeight / 2,
                keepFixed.y0 + 2, keepFixed.y1 - annexHeight - 2);
            const Rect annex = side > 0
                ? Rect{keepFixed.x1 - 1, annexY, keepFixed.x1 + annexDepth, annexY + annexHeight}
                : Rect{keepFixed.x0 - annexDepth, annexY, keepFixed.x0 + 1, annexY + annexHeight};
            bool clear = annex.x0 > innerWard.x0 + 1 && annex.y0 > innerWard.y0 + 1 &&
                annex.x1 < innerWard.x1 - 1 && annex.y1 < innerWard.y1 - 1;
            for (int y = annex.y0; y < annex.y1 && clear; ++y) {
                for (int x = annex.x0; x < annex.x1; ++x) {
                    if (Contains(keepFixed, x, y)) continue;
                    const int index = Index(x, y, width);
                    if (!domain[static_cast<std::size_t>(index)] || structure[static_cast<std::size_t>(index)] ||
                        biomes[static_cast<std::size_t>(index)] != PLAIN) {
                        clear = false;
                        break;
                    }
                }
            }
            if (!clear) continue;
            const CastleFacilitySpec spec = KeepExpansionWing(mode, annexIndex);
            PaintRect(biomes, width, height, annex, TOWN_STONE_FLOOR);
            PaintRing(biomes, structure, width, height, annex, 1, MOUNTAIN);
            for (int y = annex.y0; y < annex.y1; ++y) {
                for (int x = annex.x0; x < annex.x1; ++x) structure[static_cast<std::size_t>(Index(x, y, width))] = 1;
            }
            const int annexInteriorX = side > 0 ? annex.x0 + 1 : annex.x1 - 2;
            for (int x = std::min(probeX, annexInteriorX); x <= std::max(probeX, annexInteriorX); ++x) {
                biomes[static_cast<std::size_t>(Index(x, connectionY, width))] = BRIDGE;
                structure[static_cast<std::size_t>(Index(x, connectionY, width))] = 0;
            }
            const int regionId = 80 + annexIndex;
            const int regionIndex = static_cast<int>(candidate.regions.size());
            candidate.regions.push_back({"keep_expansion", regionId, annex.x0, annex.y0, annex.x1 - 1, annex.y1 - 1});
            candidate.regions.back().role = spec.role;
            candidate.regions.back().linkedRegionId = 0;
            candidate.markers.push_back({"landmark", annex.CenterX(), annex.CenterY(), regionId});
            candidate.markers.back().role = spec.role;
            keepAnnexFurnishings.push_back({regionIndex, spec.furnishing});
        }
        if (hasInnerWard) {
            PaintRing(biomes, structure, width, height, innerWard, 1, MOUNTAIN);
            for (int y = innerWard.y0; y < innerWard.y1; ++y) for (int x = innerWard.x0; x < innerWard.x1; ++x) {
                if (IsRectBoundary(innerWard, x, y, 1)) defense[Index(x, y, width)] = 3;
            }
            const int gateX = mode == CastleMode::Palace ? keepDoor.x :
                std::clamp(keepDoor.x - wardGap, innerWard.x0 + 2, innerWard.x1 - 3);
            const Point southGate{gateX, innerWard.y1 - 1};
            for (const Point& point : std::array<Point, 1>{{southGate}}) {
                biomes[static_cast<std::size_t>(Index(point.x, point.y, width))] = BRIDGE;
                structure[static_cast<std::size_t>(Index(point.x, point.y, width))] = 0;
                defense[Index(point.x, point.y, width)] = 6;
                candidate.markers.push_back({"service_entry", point.x, point.y, 1});
                candidate.markers.back().role = "inner_ward_entry";
            }
            candidate.regions.push_back({"inner_ward", 0, innerWard.x0, innerWard.y0, innerWard.x1 - 1, innerWard.y1 - 1});
            candidate.regions.back().role = "inner_ward";
        }

        // Reserve the procession/defence route in every archetype before
        // placing service buildings. Its only inner-wall opening is the
        // controlled gate; military layouts offset it from the keep door.
        auto paintModeAxis = [&](Point start, Point goal, int halfWidth, const char* kind, int id) {
            const std::vector<Point> path = FindPath(domain, structure, biomes, width, height, start, goal);
            if (path.empty()) return false;
            PaintRoadPath(biomes, domain, structure, width, height, path, halfWidth, nullptr);
            int x0 = width;
            int y0 = height;
            int x1 = 0;
            int y1 = 0;
            for (const Point& point : path) {
                x0 = std::min(x0, point.x);
                y0 = std::min(y0, point.y);
                x1 = std::max(x1, point.x);
                y1 = std::max(y1, point.y);
            }
            candidate.regions.push_back({kind, id, x0, y0, x1, y1});
            candidate.regions.back().role = kind;
            return true;
        };
        if (!paintModeAxis(gates.front().inner, keepDoor, gates.front().halfWidth,
            mode == CastleMode::Palace ? "ceremonial_avenue" : "defense_axis", 0)) continue;

        std::vector<std::uint8_t> reservedAccess(static_cast<std::size_t>(area), 0);
        auto canPlaceRect = [&](const Rect& rect, int marginSize) {
            if (rect.x0 <= innerRect.x0 || rect.y0 <= innerRect.y0 || rect.x1 >= innerRect.x1 || rect.y1 >= innerRect.y1) return false;
            for (int y = rect.y0 - marginSize; y < rect.y1 + marginSize; ++y) {
                for (int x = rect.x0 - marginSize; x < rect.x1 + marginSize; ++x) {
                    if (!InBounds(x, y, width, height)) return false;
                    const int index = Index(x, y, width);
                    if (domain[static_cast<std::size_t>(index)] == 0 || structure[static_cast<std::size_t>(index)] != 0 ||
                        (Contains(rect, x, y) && reservedAccess[static_cast<std::size_t>(index)] != 0)) return false;
                    if (Contains(rect, x, y) && biomes[static_cast<std::size_t>(index)] != PLAIN) return false;
                }
            }
            return true;
        };

        std::vector<FacilityInfo> facilities;
        const int upperWardExpansionCount = hasInnerWard ? (wealth >= 2.15 ? 2 : (wealth >= 1.5 ? 1 : 0)) : 0;
        for (int expansionIndex = 0; expansionIndex < upperWardExpansionCount; ++expansionIndex) {
            const bool rightSide = expansionIndex == 0;
            const Rect plot = rightSide
                ? Rect{innerWard.x1 + 2, innerWard.y0 + 2, innerRect.x1 - 2, innerWard.y1 - 2}
                : Rect{innerRect.x0 + 2, innerWard.y0 + 2, innerWard.x0 - 2, innerWard.y1 - 2};
            Rect selected;
            int selectedArea = 0;
            const int maximumWidth = std::min(plot.Width(), std::max(10, castleDimension / 7));
            const int maximumHeight = std::min(plot.Height(), std::max(14, keepFixed.Height() / 2));
            for (int facilityHeight = maximumHeight; facilityHeight >= 8 && selectedArea == 0; --facilityHeight) {
                for (int facilityWidth = maximumWidth; facilityWidth >= 7 && selectedArea == 0; --facilityWidth) {
                    const int targetX = rightSide ? plot.x0 : plot.x1 - facilityWidth;
                    const int targetY = std::clamp(keepFixed.CenterY() - facilityHeight / 2,
                        plot.y0, std::max(plot.y0, plot.y1 - facilityHeight));
                    for (int offset = 0; offset <= std::max(0, plot.Height() - facilityHeight); ++offset) {
                        for (const int y : {targetY - offset, targetY + offset}) {
                            if (y < plot.y0 || y + facilityHeight > plot.y1) continue;
                            const Rect option{targetX, y, targetX + facilityWidth, y + facilityHeight};
                            if (!canPlaceRect(option, 0)) continue;
                            selected = option;
                            selectedArea = option.Width() * option.Height();
                            break;
                        }
                        if (selectedArea > 0) break;
                    }
                }
            }
            if (selectedArea == 0) continue;
            const Point anchor = gates.front().inner;
            const std::array<Point, 4> doorOptions{{{selected.CenterX(), selected.y0}, {selected.CenterX(), selected.y1 - 1},
                {selected.x0, selected.CenterY()}, {selected.x1 - 1, selected.CenterY()}}};
            Point door{-1, -1};
            Point approach;
            int doorCost = std::numeric_limits<int>::max();
            for (const Point& option : doorOptions) {
                Point outside = option;
                if (option.x == selected.x0) --outside.x;
                else if (option.x == selected.x1 - 1) ++outside.x;
                else if (option.y == selected.y0) --outside.y;
                else ++outside.y;
                if (!Contains(innerRect, outside.x, outside.y) ||
                    structure[static_cast<std::size_t>(Index(outside.x, outside.y, width))]) continue;
                const int cost = Manhattan(outside, anchor);
                if (cost < doorCost) {
                    door = option;
                    approach = outside;
                    doorCost = cost;
                }
            }
            if (door.x < 0) continue;
            const CastleFacilitySpec spec = UpperWardExpansion(mode, expansionIndex);
            PaintRect(biomes, width, height, selected, TOWN_STONE_FLOOR);
            PaintRing(biomes, structure, width, height, selected, 1, MOUNTAIN);
            for (int y = selected.y0; y < selected.y1; ++y) {
                for (int x = selected.x0; x < selected.x1; ++x) structure[static_cast<std::size_t>(Index(x, y, width))] = 1;
            }
            biomes[static_cast<std::size_t>(Index(door.x, door.y, width))] = BRIDGE;
            structure[static_cast<std::size_t>(Index(door.x, door.y, width))] = 0;
            const int regionId = 500 + expansionIndex;
            const int regionIndex = static_cast<int>(candidate.regions.size());
            facilities.push_back({selected, door, approach, regionId, regionIndex, spec.furnishing});
            candidate.regions.push_back({"facility", regionId, selected.x0, selected.y0, selected.x1 - 1, selected.y1 - 1});
            candidate.regions.back().role = spec.role;
            candidate.regions.back().linkedRegionId = 0;
            candidate.markers.push_back({"landmark", selected.CenterX(), selected.CenterY(), regionId});
            candidate.markers.back().role = spec.role;
            candidate.markers.push_back({"service_entry", door.x, door.y, regionId});
            candidate.markers.back().role = spec.role + "_entry";
            reservedAccess[static_cast<std::size_t>(Index(approach.x, approach.y, width))] = 1;
        }

        // Reserve usable outdoor rooms before service buildings. Facilities
        // will face these courts, not arbitrary nearest empty coordinates.
        struct Court { Rect rect; Point anchor; std::string role; };
        std::vector<Court> courts;
        const int courtY0 = (hasInnerWard ? innerWard.y1 : keepFixed.y1) + 3;
        const double courtDensityScale = wealth >= 2.15 ? 0.58 : (wealth >= 1.5 ? 0.76 : 1.0);
        const int courtHeight = std::max(4, static_cast<int>(std::lround(
            (innerRect.y1 - courtY0 - 5) / static_cast<double>(mode == CastleMode::Fortress ? 2 : 3) * courtDensityScale)));
        const int courtWidth = std::max(5, static_cast<int>(std::lround(
            innerRect.Width() * (mode == CastleMode::Fortress ? 0.28 : 0.20) * courtDensityScale)));
        for (int courtIndex = 0; courtIndex < 2 && minDimension >= 96; ++courtIndex) {
            const Point target{centerX + (courtIndex == 0 ? -1 : 1) * innerRect.Width() / 4,
                courtY0 + (innerRect.y1 - courtY0) / 2};
            Rect selected;
            int bestDistance = std::numeric_limits<int>::max();
            for (int y = courtY0; y + courtHeight < innerRect.y1 - 2; ++y) {
                for (int x = innerRect.x0 + 2; x + courtWidth < innerRect.x1 - 2; ++x) {
                    Rect option{x, y, x + courtWidth, y + courtHeight};
                    const int distance = Manhattan({option.CenterX(), option.CenterY()}, target);
                    if (distance >= bestDistance || !canPlaceRect(option, 1)) continue;
                    bestDistance = distance; selected = option;
                }
            }
            if (bestDistance == std::numeric_limits<int>::max()) continue;
            const char* kind = courtIndex == 0 && mode == CastleMode::Palace ? "garden" : (courtIndex == 0 ? "training_ground" : "service_court");
            PaintRect(biomes, width, height, selected, static_cast<std::uint8_t>(courtIndex == 0 ?
                (mode == CastleMode::Palace ? FOREST : DESERT) : TOWN_FLOOR));
            courts.push_back({selected, {selected.CenterX(), selected.CenterY()}, kind});
            candidate.regions.push_back({kind, courtIndex, selected.x0, selected.y0, selected.x1 - 1, selected.y1 - 1});
            candidate.regions.back().role = courtIndex == 0 ? program.court0 : program.court1;
            paintModeAxis(courts.back().anchor, gates.front().inner, 0, "court_access", courtIndex);
        }

        const int targetFacilities = std::uniform_int_distribution<int>(facilityMin, facilityMax)(rng);
        const int requiredFacilities = facilityRangeWasExplicit ? facilityMin :
            std::max(4, static_cast<int>(std::lround(facilityMin / outerFacilityDensity)));
        int nextFacilityId = 0;
        const int remainingFacilityTarget = std::max(4, targetFacilities - static_cast<int>(facilities.size()));
        for (int attempt = 0; attempt < targetFacilities * 180 && nextFacilityId < remainingFacilityTarget; ++attempt) {
            const CastleFacilitySpec spec = wealth > 1.0 && nextFacilityId >= 4
                ? ExpansionFacility(mode, nextFacilityId - 4)
                : program.facilities[static_cast<std::size_t>(nextFacilityId % program.facilities.size())];
            const double buildingVariation = std::uniform_real_distribution<double>(0.9, 1.12)(rng);
            const double densityCompactFactor = std::clamp(1.18 / std::sqrt(std::max(1.0, outerFacilityDensity)), 0.72, 1.0);
            const double compactFactor = minDimension < 112 ? 0.7 : densityCompactFactor;
            const int facilityWidth = std::clamp(static_cast<int>(std::lround(std::max(6, castleDimension / 11) * spec.width * buildingVariation * compactFactor)),
                6, std::max(6, innerRect.Width() / 3));
            const int facilityHeight = std::clamp(static_cast<int>(std::lround(std::max(5, castleDimension / 15) * spec.height * buildingVariation * compactFactor)),
                5, std::max(5, innerRect.Height() / 3));
            const std::string& facilityKind = spec.role;
            const bool sacred = spec.furnishing == "altar";
            const bool kitchen = spec.furnishing == "hearths";
            Point anchor = gates.front().inner;
            if (!courts.empty()) anchor = courts[std::min<std::size_t>(spec.court == 0 ? 0 : 1, courts.size() - 1)].anchor;
            if (spec.court == 2) anchor = {keepFixed.x0 - wardGap - 3, keepFixed.y1};
            if (spec.court == 3) anchor = {innerRect.x0 + 3, innerRect.CenterY()};
            Rect rect;
            int placementCost = std::numeric_limits<int>::max();
            for (int optionIndex = 0; optionIndex < (wealth >= 1.5 ? 72 : 28); ++optionIndex) {
                const int x = std::uniform_int_distribution<int>(innerRect.x0 + 2, std::max(innerRect.x0 + 2, innerRect.x1 - facilityWidth - 2))(rng);
                const int y = std::uniform_int_distribution<int>(innerRect.y0 + 2, std::max(innerRect.y0 + 2, innerRect.y1 - facilityHeight - 2))(rng);
                const Rect option{x, y, x + facilityWidth, y + facilityHeight};
                if (hasInnerWard && option.x0 < innerWard.x1 && option.x1 > innerWard.x0 &&
                    option.y0 < innerWard.y1 && option.y1 > innerWard.y0) continue;
                if (!canPlaceRect(option, wealth >= 1.5 ? 0 : 1)) continue;
                const int cost = Manhattan({option.CenterX(), option.CenterY()}, anchor);
                if (cost < placementCost) { rect = option; placementCost = cost; }
            }
            if (placementCost == std::numeric_limits<int>::max()) continue;
            const std::array<Point, 4> doorOptions{{{rect.CenterX(), rect.y0}, {rect.CenterX(), rect.y1 - 1},
                {rect.x0, rect.CenterY()}, {rect.x1 - 1, rect.CenterY()}}};
            const Point door = *std::min_element(doorOptions.begin(), doorOptions.end(), [&](Point a, Point b) {
                return Manhattan(a, anchor) < Manhattan(b, anchor);
            });
            Point approach = door;
            if (door.x == rect.x0) --approach.x;
            else if (door.x == rect.x1 - 1) ++approach.x;
            else if (door.y == rect.y0) --approach.y;
            else ++approach.y;
            if (!Contains(innerRect, approach.x, approach.y) || structure[static_cast<std::size_t>(Index(approach.x, approach.y, width))] != 0) continue;
            const int facilityIndex = nextFacilityId++;
            const std::uint8_t facilityFloor = static_cast<std::uint8_t>(spec.furnishing == "stalls" ? TOWN_FLOOR :
                (sacred || kitchen || facilityKind == "workshop" ? TOWN_STONE_FLOOR : TOWN_WOOD_FLOOR));
            PaintRect(biomes, width, height, rect, facilityFloor);
            PaintRing(biomes, structure, width, height, rect, 1, MOUNTAIN);
            for (int y = rect.y0; y < rect.y1; ++y) for (int x = rect.x0; x < rect.x1; ++x) structure[Index(x, y, width)] = 1;
            biomes[static_cast<std::size_t>(Index(door.x, door.y, width))] = BRIDGE;
            const int regionId = 100 + facilityIndex;
            const int regionIndex = static_cast<int>(candidate.regions.size());
            facilities.push_back({rect, door, approach, regionId, regionIndex, spec.furnishing});
            candidate.regions.push_back({"facility", regionId, rect.x0, rect.y0, rect.x1 - 1, rect.y1 - 1});
            candidate.regions.back().role = facilityKind;
            if (spec.court == 2) candidate.regions.back().linkedRegionId = mode == CastleMode::Palace ? 2 : 1;
            candidate.markers.push_back({"landmark", rect.CenterX(), rect.CenterY(), regionId});
            candidate.markers.back().role = candidate.regions.back().role;
            candidate.markers.push_back({"service_entry", door.x, door.y, regionId});
            candidate.markers.back().role = candidate.regions.back().role + "_entry";
            reservedAccess[static_cast<std::size_t>(Index(approach.x, approach.y, width))] = 1;
        }
        if (nextFacilityId < std::min(requiredFacilities, remainingFacilityTarget)) continue;

        std::vector<RouteNode> nodes;
        nodes.push_back({gates.front().inner, true});
        for (std::size_t gateIndex = 1; gateIndex < gates.size(); ++gateIndex) nodes.push_back({gates[gateIndex].inner, true});
        nodes.push_back({keepDoor, true});
        for (const Court& court : courts) nodes.push_back({court.anchor, false});
        for (const Point& approach : towerApproaches) nodes.push_back({approach, false});
        for (const FacilityInfo& facility : facilities) nodes.push_back({facility.approach, false});
        std::vector<Point> waterCrossings;
        std::vector<std::uint8_t> connected(nodes.size(), 0);
        connected[0] = 1;
        int connectedCount = 1;
        auto connectNodes = [&](int a, int b, int halfWidth) {
            const std::vector<Point> path = FindPath(domain, structure, biomes, width, height, nodes[static_cast<std::size_t>(a)].point, nodes[static_cast<std::size_t>(b)].point);
            if (path.empty()) return false;
            PaintRoadPath(biomes, domain, structure, width, height, path, halfWidth, &waterCrossings);
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
            if (!connectNodes(bestA, bestB, bestA == 0 || bestB == 0 ? std::max(0, (mainRoadWidth - 1) / 2) : 0)) {
                break;
            }
            connected[static_cast<std::size_t>(bestB)] = 1;
            ++connectedCount;
        }
        if (connectedCount < static_cast<int>(nodes.size())) continue;
        std::vector<std::pair<int, int>> extraEdges;
        for (int a = 0; a < static_cast<int>(nodes.size()); ++a) for (int b = a + 1; b < static_cast<int>(nodes.size()); ++b) extraEdges.push_back({a, b});
        std::shuffle(extraEdges.begin(), extraEdges.end(), rng);
        const int extraCount = static_cast<int>(std::lround(nodes.size() * extraEdgeRate));
        for (int i = 0; i < extraCount && i < static_cast<int>(extraEdges.size()); ++i) {
            const auto [a, b] = extraEdges[static_cast<std::size_t>(i)];
            connectNodes(a, b, 0);
        }
        for (const Gate& gate : gates) for (const Point& point : gate.bridge) biomes[static_cast<std::size_t>(Index(point.x, point.y, width))] = BRIDGE;
        biomes[static_cast<std::size_t>(Index(keepDoor.x, keepDoor.y, width))] = BRIDGE;
        biomes[static_cast<std::size_t>(Index(keepDoor.x, keepDoor.y - 1, width))] = BRIDGE;
        for (const RoomInfo& room : rooms) {
            if (room.role == "entrance_hall") {
                biomes[static_cast<std::size_t>(Index(room.rect.CenterX(), room.rect.y1 - 1, width))] = BRIDGE;
            }
        }
        for (const MapPlanMarker& marker : candidate.markers) {
            if (marker.kind != "service_entry" || marker.role != "inner_ward_entry") continue;
            if (!InBounds(marker.x, marker.y, width, height)) continue;
            biomes[static_cast<std::size_t>(Index(marker.x, marker.y, width))] = BRIDGE;
            structure[static_cast<std::size_t>(Index(marker.x, marker.y, width))] = 0;
        }

        const int pocketParkTarget = wealth >= 2.15 ? 3 : (wealth >= 1.5 ? 2 : (wealth >= 1.15 ? 1 : 0));
        const int pocketParkSize = std::clamp(minDimension / 32, 4, 7);
        int pocketParks = 0;
        for (int attempt = 0; attempt < pocketParkTarget * 160 && pocketParks < pocketParkTarget; ++attempt) {
            const int x = std::uniform_int_distribution<int>(innerRect.x0 + 2,
                std::max(innerRect.x0 + 2, innerRect.x1 - pocketParkSize - 2))(rng);
            const int y = std::uniform_int_distribution<int>(innerRect.y0 + 2,
                std::max(innerRect.y0 + 2, innerRect.y1 - pocketParkSize - 2))(rng);
            const Rect park{x, y, x + pocketParkSize, y + pocketParkSize};
            bool clear = true;
            for (int py = park.y0; py < park.y1 && clear; ++py) {
                for (int px = park.x0; px < park.x1; ++px) {
                    const int index = Index(px, py, width);
                    if (!domain[static_cast<std::size_t>(index)] || structure[static_cast<std::size_t>(index)] ||
                        biomes[static_cast<std::size_t>(index)] != PLAIN) {
                        clear = false;
                        break;
                    }
                }
            }
            if (!clear) continue;
            PaintRect(biomes, width, height, park, FOREST);
            candidate.regions.push_back({"garden", 200 + pocketParks, park.x0, park.y0, park.x1 - 1, park.y1 - 1});
            candidate.regions.back().role = WealthParkRole(mode);
            ++pocketParks;
        }

        const int streetTreeTarget = wealth >= 1.15 ?
            static_cast<int>(std::lround((wealth - 1.0) * minDimension / 6.0)) : 0;
        std::vector<Point> streetTreeCandidates;
        for (int y = innerRect.y0 + 1; y < innerRect.y1 - 1; ++y) {
            for (int x = innerRect.x0 + 1; x < innerRect.x1 - 1; ++x) {
                const int index = Index(x, y, width);
                if (structure[static_cast<std::size_t>(index)] || biomes[static_cast<std::size_t>(index)] != PLAIN) continue;
                bool besideRoad = false;
                for (const Point& offset : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                    besideRoad = besideRoad || IsRoadLike(biomes[static_cast<std::size_t>(Index(x + offset.x, y + offset.y, width))]);
                }
                if (besideRoad) streetTreeCandidates.push_back({x, y});
            }
        }
        std::shuffle(streetTreeCandidates.begin(), streetTreeCandidates.end(), rng);
        int streetTrees = 0;
        for (const Point& point : streetTreeCandidates) {
            if (streetTrees >= streetTreeTarget) break;
            bool separated = true;
            for (int oy = -1; oy <= 1 && separated; ++oy) for (int ox = -1; ox <= 1; ++ox) {
                if (InBounds(point.x + ox, point.y + oy, width, height) &&
                    biomes[static_cast<std::size_t>(Index(point.x + ox, point.y + oy, width))] == FOREST) {
                    separated = false;
                    break;
                }
            }
            if (!separated) continue;
            biomes[static_cast<std::size_t>(Index(point.x, point.y, width))] = FOREST;
            candidate.regions.push_back({"street_tree", 300 + streetTrees, point.x, point.y, point.x, point.y});
            candidate.regions.back().role = StreetTreeRole(mode);
            ++streetTrees;
        }

        auto tryPaintArea = [&](std::uint8_t tile, const Rect& rect, const char* kind, int id) {
            if (rect.Width() < 4 || rect.Height() < 4) return false;
            for (int y = rect.y0; y < rect.y1; ++y) {
                for (int x = rect.x0; x < rect.x1; ++x) {
                    if (!InBounds(x, y, width, height)) return false;
                    const int index = Index(x, y, width);
                    if (domain[static_cast<std::size_t>(index)] == 0 || structure[static_cast<std::size_t>(index)] != 0 || IsRoadLike(biomes[static_cast<std::size_t>(index)]) || IsWater(biomes[static_cast<std::size_t>(index)])) return false;
                }
            }
            PaintRect(biomes, width, height, rect, tile);
            candidate.regions.push_back({kind, id, rect.x0, rect.y0, rect.x1 - 1, rect.y1 - 1});
            candidate.regions.back().role = kind;
            return true;
        };
        // The lord's garden occupies the quiet sides of the inner ward.
        // A cistern is protected behind the keep rather than dropped into a
        // random route. These spaces have a use, size and access relationship.
        if (hasInnerWard) {
            tryPaintArea(FOREST, {innerWard.x0 + 2, keepFixed.y0,
                keepFixed.x0 - 2, keepFixed.y1}, "garden", 0);
            tryPaintArea(FOREST, {keepFixed.x1 + 2, keepFixed.y0,
                innerWard.x1 - 2, keepFixed.y1}, "garden", 1);
            const int cisternSize = std::clamp(wardGap - 4, 4, 8);
            tryPaintArea(LAKE, {keepFixed.CenterX() - cisternSize / 2, innerWard.y0 + 2,
                keepFixed.CenterX() - cisternSize / 2 + cisternSize, innerWard.y0 + 2 + cisternSize}, "cistern", 0);
        }

        // Reuse the town's connected construction projects: approaches and
        // working frontages receive paving before unused ground. Supply it
        // with a semantic view of castle buildings; leave exported roles intact.
        MapPlan pavingPlan = candidate;
        const double basePavingRatio = mode == CastleMode::Palace ? 0.88 : 0.68;
        const double wealthPavingRatio = std::clamp(basePavingRatio + (wealth - 1.0) * 0.18, 0.35, 0.96);
        pavingPlan.pavingTargetRatio = std::clamp(GetParam("pavingRatio", wealthPavingRatio), 0.0, 1.0);
        for (auto& region : pavingPlan.regions) {
            if (region.kind == "facility" || region.kind == "keep" || region.kind == "keep_expansion") region.kind = "building";
            if (region.kind == "garden") region.kind = "park";
        }
        for (auto& marker : pavingPlan.markers) {
            if (marker.kind == "service_entry" && (marker.id >= 100 || marker.role == "keep_public_entry")) {
                marker.kind = "building_door";
                marker.regionId = marker.id;
            }
        }
        std::vector<std::uint8_t> plazaMask(area, 0), roadClass(area, 0);
        for (int i = 0; i < area; ++i) {
            if (biomes[i] == ROAD || biomes[i] == BRIDGE) roadClass[i] = 2;
            if (biomes[i] == TOWN_FLOOR || biomes[i] == DESERT) plazaMask[i] = !structure[i];
        }
        // Main approach classification follows the actual route, not its
        // rectangular bounding box (which can include buildings and gardens).
        const auto mainApproach = FindPath(domain, structure, biomes, width, height, gates.front().inner, keepDoor);
        for (const auto& point : mainApproach) roadClass[Index(point.x, point.y, width)] = 3;
        ApplyTownPaving(pavingPlan, domain, structure, plazaMask, roadClass, PLAIN);
        biomes = std::move(pavingPlan.biomes);
        candidate.pavingTargetRatio = pavingPlan.pavingTargetRatio;
        candidate.pavingActualRatio = pavingPlan.pavingActualRatio;
        candidate.pavingModel = pavingPlan.pavingModel;
        candidate.pavingStages = std::move(pavingPlan.pavingStages);
        // A wide road brush can leave a handful of one-cell stubs beside a
        // protected tower or facility. Remove only those non-semantic stubs;
        // gates, bridges, and doors remain untouched.
        std::vector<std::uint8_t> roadVisited(static_cast<std::size_t>(area), 0);
        const std::array<int, 4> roadDx{1, -1, 0, 0};
        const std::array<int, 4> roadDy{0, 0, 1, -1};
        for (int start = 0; start < area; ++start) {
            if (roadVisited[static_cast<std::size_t>(start)] != 0 || !IsRoadLike(biomes[static_cast<std::size_t>(start)])) continue;
            std::vector<int> component;
            std::queue<int> pending;
            pending.push(start);
            roadVisited[static_cast<std::size_t>(start)] = 1;
            while (!pending.empty()) {
                const int current = pending.front();
                pending.pop();
                component.push_back(current);
                const int cx = current % width;
                const int cy = current / width;
                for (int direction = 0; direction < 4; ++direction) {
                    const int nx = cx + roadDx[static_cast<std::size_t>(direction)];
                    const int ny = cy + roadDy[static_cast<std::size_t>(direction)];
                    if (!InBounds(nx, ny, width, height)) continue;
                    const int next = Index(nx, ny, width);
                    if (roadVisited[static_cast<std::size_t>(next)] == 0 && IsRoadLike(biomes[static_cast<std::size_t>(next)])) {
                        roadVisited[static_cast<std::size_t>(next)] = 1;
                        pending.push(next);
                    }
                }
            }
            bool hasBridge = false;
            for (const int cell : component) if (biomes[static_cast<std::size_t>(cell)] == BRIDGE) hasBridge = true;
            if (component.size() < 12 && !hasBridge) {
                for (const int cell : component) biomes[static_cast<std::size_t>(cell)] = PLAIN;
            }
        }

        const int architectureRegionCount = static_cast<int>(candidate.regions.size());
        for (int regionIndex = 0; regionIndex < architectureRegionCount; ++regionIndex) {
            const auto region = candidate.regions[regionIndex];
            if (region.kind == "room") FurnishCastleRoom(candidate, regionIndex, RoomFurnishing(region.role), wealth);
        }
        for (const auto& [regionIndex, furnishing] : keepAnnexFurnishings) {
            FurnishCastleRoom(candidate, regionIndex, furnishing, wealth);
        }
        for (const FacilityInfo& facility : facilities) {
            FurnishCastleRoom(candidate, facility.regionIndex, facility.furnishing, wealth);
        }
        const int equipmentCount = static_cast<int>(std::count_if(candidate.regions.begin(), candidate.regions.end(), [](const auto& region) {
            return region.kind == "furniture";
        }));
        wallWalkCells = static_cast<int>(std::count_if(candidate.biomes.begin(), candidate.biomes.end(), [](std::uint8_t tile) {
            return tile == CASTLE_WALL_WALK;
        }));
        const double condition = std::clamp(0.64 + wealth * 0.14, 0.70, 1.0);
        const char* foundation = !hasOuterWall ? "open_estate" : (!hasMoat ?
            (hasOuterDefenseEquipment ? "fortified_dry_enceinte" : "dry_curtain_wall") : "fortified_moated_enceinte");
        candidate.castleHistory.push_back({"foundation", foundation,
            hasOuterWall ? std::max(1, static_cast<int>(towerApproaches.size())) : 0, condition});
        candidate.castleHistory.push_back({"keep_construction", program.layout, static_cast<int>(rooms.size()), condition});
        candidate.castleHistory.push_back({"bailey_development", "working_courts_and_service_buildings", static_cast<int>(facilities.size()), condition});
        if (wealth > 1.0) {
            candidate.castleHistory.push_back({"economic_expansion", "commercial_and_garrison_annexes",
                std::max(1, static_cast<int>(facilities.size()) - 4), condition});
        }
        if (hasOuterDefenseEquipment) {
            candidate.castleHistory.push_back({"defensive_modernization", "wall_walk_and_watch_towers",
                wallWalkCells + std::max(0, static_cast<int>(towerApproaches.size()) - 4), condition});
        }
        if (wealth >= 1.5) {
            candidate.castleHistory.push_back({"interior_refit", "dense_and_luxurious_furnishing", equipmentCount, condition});
        }
        candidate.localEconomy = candidate.wealthClass;
        const MapQualityMetrics quality = EvaluateMapQuality(candidate);
        const double markerRate = quality.markerCount == 0 ? 0.0 : static_cast<double>(quality.reachableMarkers) / quality.markerCount;
        double score = quality.largestWalkableComponentRatio * 500.0 + markerRate * 260.0;
        score += std::min(12, quality.castleTowerCount) * 10.0;
        score += std::min(32, quality.castleRoomCount) * 7.0;
        score += std::min(32, quality.castleFacilityCount) * 5.0;
        score -= quality.castleCourtyardEmptyRatio * 120.0;
        if (mode == CastleMode::River) {
            int externalRiverCells = 0;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    if (biomes[static_cast<std::size_t>(Index(x, y, width))] == RIVER && !Contains(moatRect, x, y)) {
                        ++externalRiverCells;
                    }
                }
            }
            // The river archetype must be visibly different from a normal
            // sea-backed castle; reward its external waterway explicitly.
            score += std::min(120.0, static_cast<double>(externalRiverCells) * 0.05);
        }
        score -= quality.issues.size() * 700.0;
        candidate.qualityScore = score;
        candidate.selectionReason = "highest deterministic castle score with wealth-led growth, defended wards and working courts";
        generated = true;
        if (score > best.score) best = {std::move(candidate), quality, score};
    }

    if (!generated) {
        outError = "Castle generation could not place a connected fortified layout with the requested constraints";
        return false;
    }
    outPlan = std::move(best.plan);
    return true;
}
