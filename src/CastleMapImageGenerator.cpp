#include "CastleMapImageGenerator.h"

#include "MapQuality.h"
#include "CastleProgram.h"
#include "CastleInterior.h"
#include "CastleOdds.h"
#include "CastleLotPlacement.h"
#include "TownPaving.h"
#include "SettlementRelief.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <set>
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

// Terrain is independent of the castle type: the ground around the castle is
// plain unless a terrain is requested with the `exteriorTerrain` parameter
// (0 plain, 1 desert, 2 forest, 3 mountain, 4 water). No type places water by itself:
// water is a terrain like the others, and the river castle type only decides the rooms.
// The nature on the edge of the map is forest or cliff: a natural mountain is a cliff
// (TOWN_CLIFF), not the wall's own tile, and the desert's rim is a cliff too (until
// dunes exist). The desert also turns the plain ground into desert ground.
std::uint8_t ExteriorTerrainBiome(int code) {
    switch (code) {
        case 1: return TOWN_CLIFF;
        case 2: return FOREST;
        case 3: return TOWN_CLIFF;
        case 4: return RIVER;
        default: return PLAIN;
    }
}

const char* WealthClass(double wealth) {
    if (wealth < 0.9) return "strained";
    if (wealth < 1.15) return "ordinary";
    if (wealth < 1.65) return "prosperous";
    if (wealth < 2.15) return "wealthy";
    return "opulent";
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
        std::max(0.0, getParam("castleModeWeightConcentric", getParam("castleModeWeights.concentric", castle_odds::kModeWeightConcentric))),
        std::max(0.0, getParam("castleModeWeightHill", getParam("castleModeWeights.hill", castle_odds::kModeWeightHill))),
        std::max(0.0, getParam("castleModeWeightRiver", getParam("castleModeWeights.river", castle_odds::kModeWeightRiver))),
        std::max(0.0, getParam("castleModeWeightPalace", getParam("castleModeWeights.palace", castle_odds::kModeWeightPalace))),
        std::max(0.0, getParam("castleModeWeightFortress", getParam("castleModeWeights.fortress", castle_odds::kModeWeightFortress))),
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

const char* CatalogArchetype(CastleMode mode) {
    switch (mode) {
        case CastleMode::Hill: return "hill";
        case CastleMode::River: return "river";
        case CastleMode::Palace: return "palace";
        case CastleMode::Fortress: return "fortress";
        default: return "cruciform_keep";
    }
}

// Split `rect` into `pieces` rooms of nearly equal area, so that a size limit on
// one room holds for all of them. Cuts are jittered a little for variety.
void SplitIntoRooms(const Rect& rect, int pieces, unsigned variation, unsigned tag, std::vector<Rect>& out,
    int jitterPercent = castle_odds::kSplitJitterPercent, int minPieceLength = castle_odds::kSplitMinPiece) {
    if (pieces <= 1) { out.push_back(rect); return; }
    const int first = pieces / 2;
    const bool alongX = rect.Width() >= rect.Height();
    const int length = alongX ? rect.Width() : rect.Height();
    const int minPiece = minPieceLength;
    const int jitter = static_cast<int>(CatalogHash(variation, tag) % static_cast<unsigned>(2 * jitterPercent + 1)) - jitterPercent;
    // No piece is longer than 3.2 times its width: the cut leaves the pieces on each side room to be that short.
    const int across = alongX ? rect.Height() : rect.Width();
    const int maxLength = std::max(minPiece, across * 32 / 10);
    int lo = std::max(minPiece, length - (pieces - first) * maxLength), hi = std::min(length - minPiece, first * maxLength);
    if (lo > hi) lo = hi = std::clamp(length * first / pieces, minPiece, std::max(minPiece, length - minPiece));
    const int cut = std::clamp(length * (100 * first / pieces + jitter) / 100, lo, std::max(lo, hi));
    Rect a = rect, b = rect;
    if (alongX) { a.x1 = rect.x0 + cut; b.x0 = a.x1; } else { a.y1 = rect.y0 + cut; b.y0 = a.y1; }
    SplitIntoRooms(a, first, variation, tag * 2 + 1, out, jitterPercent, minPieceLength);
    SplitIntoRooms(b, pieces - first, variation, tag * 2 + 2, out, jitterPercent, minPieceLength);
}

// What stands on the castle's axis, drawn per castle. Nothing is certain: the throne room (95%), the
// entrance hall (90%; another hall takes the front when it is left out), the antechamber of the
// throne room (90%) and the private suite (92%, in five patterns) may each be missing.
struct AxisPlan {
    bool throne = true;       // a throne room (and so an antechamber and a suite behind it)
    bool hall = true;         // the entrance hall; otherwise a great hall or guard hall faces the gate
    bool vestibule = false;     // no hall: a small entrance hall (a vestibule) with a room on each side, narrower than the axis
    bool entryCorridor = false; // no hall: the gate opens into a corridor, flanked by small rooms, that leads to the antechamber
    bool ante = true;         // the antechamber in front of the throne room
    int suite = 2;            // 0 none, 1 one room, 2 bedchamber + dressing room + study,
                              // 3 king's and queen's bedchambers apart, 4 the same with a study between,
                              // 5 the two with a shared dressing room between, 6 the king's bedchamber and a study,
                              // 7 the two with the children's chamber between, 8 the two with a dressing room and a study between
    bool suiteVault = false;  // the middle room of the suite (dressing room or study) is a vault (a private treasury)
    bool suiteInWing = false; // the suite is built into a wing instead of behind the throne room
    int throneShape = 0;      // 0 the width of the axis, 1 narrower with a room on each side, 2 narrower with one room beside it
    int backForm = 0;         // without a throne room: 0 one big room, 1 small rooms round a corridor, 2 open ground
    bool deepen = false;      // the throne room grows deep (target 2:1, depth:width)
    bool standard = false;
};

AxisPlan StandardAxisPlan() {
    AxisPlan plan;
    plan.standard = true;
    return plan;
}

AxisPlan RollAxisPlan(unsigned variation) {
    const auto pick = [&](unsigned tag, int range) { return static_cast<int>(CatalogHash(variation, tag) % static_cast<unsigned>(range)); };
    AxisPlan plan;
    // Every probability is in CastleOdds.h.
    namespace odds = castle_odds;
    plan.throne = pick(80, 100) < odds::kThronePercent;
    const int front = pick(81, 100);  // the front: entrance hall, corridor, vestibule with a room on each side, or a stand-in hall
    plan.hall = front < odds::kFrontHallPercent;
    plan.entryCorridor = !plan.hall && front < odds::kFrontHallPercent + odds::kFrontCorridorPercent;
    plan.vestibule = !plan.hall && !plan.entryCorridor && front < odds::kFrontHallPercent + odds::kFrontCorridorPercent + odds::kFrontVestibulePercent;
    plan.ante = pick(82, 100) < odds::kAntechamberPercent;
    plan.suite = odds::PickByWeight(odds::kSuiteWeights, pick(83, 100));
    plan.suiteVault = pick(93, 100) < odds::kSuiteVaultPercent;
    plan.suiteInWing = pick(84, 100) < odds::kSuiteInWingPercent;
    const int shape = pick(86, 100);
    plan.throneShape = shape < odds::kThroneShapeSideRoomsPercent ? 1 : shape < odds::kThroneShapeSideRoomsPercent + odds::kThroneShapeShiftedPercent ? 2 : 0;
    const int back = pick(87, 100);
    plan.backForm = back < odds::kBackBigRoomPercent ? 0 : back < odds::kBackBigRoomPercent + odds::kBackCorridorCellsPercent ? 1 : 2;
    plan.deepen = true;
    return plan;
}

std::vector<std::string> SuiteRoles(int suite, bool vault = false) {
    std::vector<std::string> roles;
    switch (suite) {
        case 1: roles = {"royal_bedchamber"}; break;
        case 2: roles = {"royal_bedchamber", "royal_dressing_room", "royal_study"}; break;
        case 3: roles = {"royal_bedchamber", "queens_bedchamber"}; break;
        case 4: roles = {"royal_bedchamber", "royal_study", "queens_bedchamber"}; break;
        case 5: roles = {"royal_bedchamber", "royal_dressing_room", "queens_bedchamber"}; break;
        case 6: roles = {"royal_bedchamber", "royal_study"}; break;
        case 7: roles = {"royal_bedchamber", "royal_children_chamber", "queens_bedchamber"}; break;
        case 8: roles = {"royal_bedchamber", "royal_dressing_room", "royal_study", "queens_bedchamber"}; break;
        default: break;
    }
    // A vault in place of the middle room (a dressing room or a study).
    if (vault && roles.size() >= 2 && (roles[1] == "royal_dressing_room" || roles[1] == "royal_study")) roles[1] = "royal_treasury";
    return roles;
}

// A room beside the throne room (or in the suite) is sometimes a special one: a treasury, or the room of the type that
// suits the castle.
std::string SpecialRoomRole(CastleMode mode, int draw) {
    if (draw % 2 == 0) return "treasury";
    switch (mode) {
        case CastleMode::Hill: return "map_room";
        case CastleMode::River: return "strongroom";
        case CastleMode::Palace: return "music_room";
        case CastleMode::Fortress: return "armory";
        default: return "chapel";
    }
}

// Big rooms that can take the back of the axis when there is no throne room (or stand beside a
// narrowed throne room), and small rooms that flank it.
std::vector<std::string> BigRoomRoles(CastleMode mode) {
    switch (mode) {
        case CastleMode::Hill: return {"guard_hall", "map_room", "signal_room"};
        case CastleMode::River: return {"great_hall", "chart_archive", "navigation_room"};
        case CastleMode::Palace: return {"great_hall", "royal_chapel", "library", "music_room"};
        case CastleMode::Fortress: return {"guard_hall", "map_room", "war_archive"};
        default: return {"great_hall", "chapel", "council_chamber", "archive"};
    }
}

std::vector<std::string> FlankRoomRoles(CastleMode mode) {
    switch (mode) {
        case CastleMode::Hill: return {"watch_chamber", "reserve_store"};
        case CastleMode::River: return {"toll_office", "chart_archive"};
        case CastleMode::Palace: return {"guard_lodge", "library"};
        case CastleMode::Fortress: return {"armory", "dispatch_office"};
        default: return {"guard_room", "archive"};
    }
}

// The normal castle: rooms are drawn from the type's catalog, the silhouette and
// the proportions vary with `variation`, and a size limit splits oversized wing
// rooms (unless the caller fixed the room count). The throne room (index 0) and the
// entrance hall (index 1) lead the list; the antechamber and the private suite
// follow the wing rooms. Returns an empty list when the rooms do not fit.
// `flatSides` (bit 1 left, 2 right, 4 top) are the sides that face nature (water, forest,
// desert): the outline stays straight there, because the shore follows the building's face.
std::vector<RoomInfo> PlanKeepRoomsVariedImpl(CastleMode mode, const Rect& bounds, int count, unsigned variation, bool autoSplit, int flatSides,
    const AxisPlan& ap, int& entry) {
    std::vector<RoomInfo> result;
    bool valid = true;
    auto add = [&](Rect rect, const std::string& role) {
        if (rect.Width() < 4 || rect.Height() < 4) valid = false;
        result.push_back({rect, role, static_cast<int>(result.size())});
    };
    // The private suite behind the throne room: a strip cut into 1-3 rooms side by side (the pattern
    // steps down where the strip is too narrow for it).
    auto addSuiteRooms = [&](const Rect& strip, int pattern) {
        const int span = strip.x1 - strip.x0;
        if (pattern == 8 && span < 36) pattern = 4;
        if ((pattern == 4 || pattern == 5 || pattern == 7) && span < 24) pattern = 3;
        if ((pattern == 3 || pattern == 6) && span < 16) pattern = 1;
        const std::vector<std::string> roles = SuiteRoles(pattern, ap.suiteVault);
        static const int shares[9][4] = {{0, 0, 0, 0}, {100, 0, 0, 0}, {40, 30, 30, 0}, {50, 50, 0, 0}, {36, 28, 36, 0},
            {40, 20, 40, 0}, {55, 45, 0, 0}, {36, 28, 36, 0}, {26, 20, 22, 32}};
        int x = strip.x0, used = 0;
        for (std::size_t i = 0; i < roles.size(); ++i) {
            used += shares[pattern][i];
            const int end = i + 1 == roles.size() ? strip.x1 : strip.x0 + span * used / 100;
            add({x, strip.y0, end, strip.y1}, roles[i]);
            x = end;
        }
    };
    const int w = bounds.Width(), h = bounds.Height();
    const bool compact = h < 60;
    const int axisCap = std::clamp(h * 52 / 100, 24, 64);          // widest axis room
    const int roomCap = std::max(60, w * h * 7 / 100);             // largest wing room (area)
    const auto catalog = GetCastleRoomCatalog(CatalogArchetype(mode));
    const auto pick = [&](unsigned tag, int range) { return static_cast<int>(CatalogHash(variation, tag) % static_cast<unsigned>(range)); };
    // Number of pieces for a region: at least the requested rooms, more if the
    // size limit demands it. Pieces stay at least 6 cells across.
    // How unequal the rooms of a split are (percent either side of an even split). Hill's band of small rooms keeps them
    // nearly equal: its corridor needs two columns that are both wide enough.
    int splitJitter = castle_odds::kSplitJitterPercent;
    const auto piecesFor = [&](const Rect& rect, int requested) {
        if (rect.Width() < 4 || rect.Height() < 4) return 0;
        int pieces = requested;
        // (The larger room of an uneven split of two is up to (50 + jitter)/50 of an even half: the size limit allows for it.)
        const int capShare = 85 * 50 / (50 + splitJitter);
        if (autoSplit) pieces = std::max(pieces, (rect.Width() * rect.Height() * 100 + roomCap * capShare - 1) / (roomCap * capShare));
        return std::clamp(pieces, 0, std::max(requested, rect.Width() * rect.Height() / 36));
    };
    const auto zoneRank = [](char zone) { return zone == 'B' ? 0 : zone == 'M' ? 1 : 2; };
    // Fill a wing with `roles`, sliced from back to front.
    auto fillWing = [&](const Rect& rect, const std::vector<CastleRoomSpec>& roles, unsigned tag) {
        if (roles.empty()) return;
        std::vector<Rect> plots;
        SplitIntoRooms(rect, static_cast<int>(roles.size()), variation, tag, plots, splitJitter);
        std::sort(plots.begin(), plots.end(), [](const Rect& a, const Rect& b) { return a.y0 == b.y0 ? a.x0 < b.x0 : a.y0 < b.y0; });
        for (std::size_t i = 0; i < plots.size() && i < roles.size(); ++i) add(plots[i], roles[i].role);
    };
    // Share `requested` rooms among stacked blocks by area (at least one each; surplus
    // blocks are dropped) and apply the size limit to each. Returns the pieces per block.
    const auto planBlocks = [&](std::vector<Rect>& blocks, int requested) {
        std::vector<int> pieces;
        if (requested <= 0) { blocks.clear(); return pieces; }
        if (static_cast<int>(blocks.size()) > requested) {
            // Too many blocks for the rooms. The block that reaches farthest on each side stays (so the castle keeps its
            // width) with the largest of the others; at least three blocks stay (a wing on each side and the middle).
            requested = std::max(requested, std::min(3, static_cast<int>(blocks.size())));
            std::size_t leftmost = 0, rightmost = 0;
            for (std::size_t i = 1; i < blocks.size(); ++i) {
                if (blocks[i].x0 < blocks[leftmost].x0) leftmost = i;
                if (blocks[i].x1 > blocks[rightmost].x1) rightmost = i;
            }
            std::vector<std::size_t> order{leftmost};
            if (rightmost != leftmost) order.push_back(rightmost);
            std::vector<std::size_t> others;
            for (std::size_t i = 0; i < blocks.size(); ++i) if (i != leftmost && i != rightmost) others.push_back(i);
            std::stable_sort(others.begin(), others.end(), [&](std::size_t l, std::size_t r) {
                return blocks[l].Width() * blocks[l].Height() > blocks[r].Width() * blocks[r].Height();
            });
            for (const std::size_t i : others) if (static_cast<int>(order.size()) < requested) order.push_back(i);
            std::sort(order.begin(), order.end());
            std::vector<Rect> kept;
            for (const std::size_t i : order) kept.push_back(blocks[i]);
            blocks = std::move(kept);
        }
        long long areaLeft = 0;
        for (const auto& block : blocks) areaLeft += std::max(0, block.Width() * block.Height());
        if (areaLeft <= 0) { blocks.clear(); return pieces; }
        int left = requested;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            const long long area = std::max(0, blocks[i].Width() * blocks[i].Height());
            const int rest = static_cast<int>(blocks.size() - i - 1);
            const int share = rest == 0 ? left : std::clamp(static_cast<int>((left * area + areaLeft / 2) / std::max(1LL, areaLeft)), 1, left - rest);
            left -= share;
            areaLeft -= area;
            pieces.push_back(piecesFor(blocks[i], share));
        }
        return pieces;
    };
    // A wing built from 1-3 stacked blocks of different reach, hanging off the vertical
    // line x = `edge` (to the left or the right), so its outer face steps. With three
    // blocks the middle one is sometimes left out: an open recess in the face.
    const auto stackBlocks = [&](bool leftSide, int edge, int width, int top, int bottom, unsigned tag, int minWidth, bool reachFull = true, int reachBase = castle_odds::kStackReachBase) {
        std::vector<Rect> blocks;
        const int span = bottom - top;
        if (width < minWidth || span < 8) return blocks;
        // A side facing nature stays one flat face: the shore follows it.
        const bool flat = (flatSides & (leftSide ? 1 : 2)) != 0;
        // A big wing is sometimes a ring around an open courtyard on the axis side: three blocks
        // (end, outer strip, end) leave a hole of at least 8x8 cells. The outer face stays flat.
        if (width >= castle_odds::kRingMinWidth && span >= castle_odds::kRingMinSpan && pick(tag + 11, castle_odds::kWingCourtyardRange) < castle_odds::kWingCourtyardHits) {
            const int endA = std::max(8, span * (castle_odds::kRingEndBase + pick(tag + 12, castle_odds::kRingEndSpread)) / 100), endB = std::max(8, span * (castle_odds::kRingEndBase + pick(tag + 13, castle_odds::kRingEndSpread)) / 100);
            const int strip = std::max(8, width * (castle_odds::kRingStripBase + pick(tag + 14, castle_odds::kRingStripSpread)) / 100);
            if (width - strip >= 8 && span - endA - endB >= 8) {
                const int outer = leftSide ? edge - width : edge + width - strip;
                blocks.push_back(leftSide ? Rect{edge - width, top, edge, top + endA} : Rect{edge, top, edge + width, top + endA});
                blocks.push_back({outer, top + endA, outer + strip, bottom - endB});
                blocks.push_back(leftSide ? Rect{edge - width, bottom - endB, edge, bottom} : Rect{edge, bottom - endB, edge + width, bottom});
                return blocks;
            }
        }
        const int count = flat ? 1 : std::clamp(1 + pick(tag + 2, castle_odds::kStackedBlocksMax), 1, std::max(1, span / 8));
        const bool notch = !flat && count == 3 && pick(tag + 9, castle_odds::kNotchRange) < castle_odds::kNotchHits;
        int previous = width;
        // One block of the wing (not the recess) always reaches the wing's full width, so the castle keeps its width.
        const int fullBlock = !reachFull || count == 1 ? 0 : notch ? (pick(tag + 20, 2) == 0 ? 0 : count - 1) : pick(tag + 20, count);
        for (int i = 0; i < count; ++i) {
            if (notch && i == 1) continue;
            int reach = count == 1 || (reachFull && i == fullBlock) ? width : std::max(std::min(8, width), std::min(width, width * (reachBase + pick(tag + 3 + static_cast<unsigned>(i), castle_odds::kStackReachSpread)) / 100));
            // A step of a few cells would leave a jog the wall cannot follow cleanly (and an
            // annex could not meet the face): a face is flush with the edge or steps by 6 or more.
            if (std::abs(reach - previous) < 6) reach = previous;
            if (width - reach < 6) reach = width;
            previous = reach;
            const int y0 = top + span * i / count, y1 = top + span * (i + 1) / count;
            blocks.push_back(leftSide ? Rect{edge - reach, y0, edge, y1} : Rect{edge, y0, edge + reach, y1});
        }
        return blocks;
    };
    // Hand the picked roles out to the blocks in order.
    const auto dispatchRooms = [&](const std::vector<Rect>& blocks, const std::vector<int>& pieces, const std::vector<CastleRoomSpec>& roles, unsigned tag) {
        std::size_t at = 0;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            const std::size_t end = std::min(roles.size(), at + static_cast<std::size_t>(pieces[i]));
            fillWing(blocks[i], std::vector<CastleRoomSpec>(roles.begin() + static_cast<std::ptrdiff_t>(at), roles.begin() + static_cast<std::ptrdiff_t>(end)),
                tag + static_cast<unsigned>(i) * 7u);
            at = end;
        }
    };
    // Axis proportions (percent of the axis height).
    const auto axisHeightOf = [&](int bottomY) { return std::max(8, bottomY - bounds.y0); };
    const int privShare = compact ? castle_odds::kPrivShareCompact : castle_odds::kPrivShare, anteShare = compact ? castle_odds::kAnteShareCompact : castle_odds::kAnteShare;
    std::vector<std::function<void()>> late;  // rooms appended after the wing rooms
    const int wingRooms = count - 2;
    // A suite built into a wing (or, with no throne room, the only place for it): its rooms come first in
    // the wing's back and are on top of the requested rooms.
    const std::vector<std::string> suiteRoles = SuiteRoles(ap.suite, ap.suiteVault);
    // (Hill's suite stays at the top of its tower: its band of small rooms is kept for the corridor.)
    const bool wingSuite = ap.suiteInWing && mode != CastleMode::Hill;
    const bool suiteToWing = ap.suite != 0 && (wingSuite || !ap.throne) && wingRooms >= static_cast<int>(suiteRoles.size()) + 1;
    const int suiteExtra = suiteToWing ? static_cast<int>(suiteRoles.size()) : 0;
    const bool suiteOnAxis = ap.suite != 0 && ap.throne && !wingSuite;
    const bool military = mode == CastleMode::Hill || mode == CastleMode::Fortress;
    const std::string standIn = military ? "guard_hall" : "great_hall";
    const std::vector<std::string> bigRoles = BigRoomRoles(mode), flankRoles = FlankRoomRoles(mode);
    struct AxisGeometry { int privEnd = 0, throneEnd = 0, anteEnd = 0, hallTop = 0; };
    // Lay out the axis column: the entrance hall (it leads the list, `entry`), the throne room, and
    // (appended after the wing rooms) the antechamber, the suite behind the throne room, the rooms beside
    // a narrowed throne room, and the back of the axis where there is no throne room.
    // `hallTopFixed` is the top of the hall where the caller fixes it (Hill); otherwise the hall takes
    // whatever lies in front of the antechamber.
    const auto buildAxis = [&](const Rect& column, int front, int hallTopFixed, int throneShare) {
        AxisGeometry g;
        const int y0 = column.y0, colW = column.x1 - column.x0;
        const int A = axisHeightOf(front);
        // The standard geometry: the suite, the throne room and the antechamber stacked in fixed shares.
        const int privEnd0 = y0 + std::max(compact ? castle_odds::kPrivMinCompact : castle_odds::kPrivMin, A * privShare / 100);
        const int td0 = hallTopFixed >= 0 ? std::max(4, A * throneShare / 100) : std::clamp(A * (mode == CastleMode::Palace ? castle_odds::kThroneSharePalace : throneShare) / 100, 4, std::max(4, A - 6));
        const int throneEnd0 = privEnd0 + td0;
        const int anteEnd0 = throneEnd0 + std::max(compact ? castle_odds::kAnteMinCompact : castle_odds::kAnteMin, A * anteShare / 100);
        g.hallTop = hallTopFixed >= 0 ? hallTopFixed : anteEnd0;
        const bool hallAtAnte = hallTopFixed < 0;
        entry = static_cast<int>(result.size());
        const std::string hallRole = ap.hall ? "entrance_hall" : standIn;
        if (!ap.throne) {
            // No throne room (and no antechamber): the back of the axis, down to where the standard
            // antechamber ended, is a big room, small rooms round a corridor, or open ground.
            g.privEnd = g.throneEnd = y0;
            g.anteEnd = anteEnd0;
            // The hall (stand-in or not) keeps the front of the axis as it always did.
            add({column.x0, g.hallTop, column.x1, front}, hallRole);
            late.push_back([&, column, anteEnd0, y0, colW] {
                const Rect back{column.x0, y0, column.x1, anteEnd0};
                const std::string bigRole = bigRoles[static_cast<std::size_t>(pick(88, static_cast<int>(bigRoles.size())))];
                if (ap.backForm == 2 && back.Height() >= 10) return;  // open ground: a courtyard behind the hall
                if (ap.backForm == 1 && colW >= 30 && back.Height() >= 28) {
                    const int topH = std::clamp(back.Height() * 32 / 100, 8, back.Height() - 18);
                    add({back.x0, back.y0, back.x1, back.y0 + topH}, bigRole);
                    const int sideW = (colW - castle_odds::kCorridorWidth) / 2;
                    const Rect leftCells{back.x0, back.y0 + topH, back.x0 + sideW, back.y1};
                    const Rect spine{back.x0 + sideW, back.y0 + topH, back.x0 + sideW + castle_odds::kCorridorWidth, back.y1};
                    const Rect rightCells{spine.x1, back.y0 + topH, back.x1, back.y1};
                    const int rows = std::clamp(leftCells.Height() / 10, 2, 4);
                    const auto cells = PickCastleRooms(catalog, rows * 2, variation + 5);
                    fillWing(leftCells, std::vector<CastleRoomSpec>(cells.begin(), cells.begin() + rows), 91);
                    add(spine, "corridor");
                    fillWing(rightCells, std::vector<CastleRoomSpec>(cells.begin() + rows, cells.end()), 93);
                    return;
                }
                add(back, bigRole);
            });
            return g;
        }
        // The throne room. Behind it lies the suite (when on the axis); the rows the suite does not take
        // go to the throne room, which is then deepened up to the target (2:1, depth:width) while the
        // hall keeps a minimum.
        // (A suite of its own is at least 11 rows deep, so that a bed fits beside the doors.)
        const int suiteH = suiteOnAxis ? (ap.standard || compact ? privEnd0 - y0 : std::max(castle_odds::kSuiteMinDepth, privEnd0 - y0)) : 0;
        // (Hill always has one: without it the throne room would open into the band of small rooms.)
        const bool hasAnte = ap.ante || hallTopFixed >= 0;
        const int anteH = hasAnte ? anteEnd0 - throneEnd0 : 0;
        g.privEnd = y0 + suiteH;
        int td = throneEnd0 - privEnd0 + (privEnd0 - y0 - suiteH);
        // (Hill's throne room is not deepened: the band of small rooms below it gives the castle its outline.)
        if (ap.deepen && !ap.standard && hallAtAnte) {
            const int hallLimit = front - std::max(castle_odds::kHallMinDepth, A * castle_odds::kHallMinShare / 100);
            const int tdMax = hallLimit - anteH - g.privEnd;
            td = std::max(td, std::min(tdMax, std::max(td, A * castle_odds::kThroneTargetShare / 100)));
        }
        g.throneEnd = g.privEnd + td;
        g.anteEnd = g.throneEnd + anteH;
        g.hallTop = hallAtAnte ? g.anteEnd : hallTopFixed;
        // The gate may open straight into a corridor (8 wide, down the middle of the hall's rows) that leads to the
        // antechamber, with small rooms on both sides; not for Hill, whose hall is a shallow strip below its band.
        const bool corridorEntry = ap.entryCorridor && !ap.hall && hasAnte && hallAtAnte && front - g.hallTop >= castle_odds::kEntryCorridorMinDepth && colW >= castle_odds::kEntryCorridorMinWidth;
        if (corridorEntry) {
            const int sideW = (colW - castle_odds::kCorridorWidth) / 2;
            const Rect spine{column.x0 + sideW, g.hallTop, column.x0 + sideW + castle_odds::kCorridorWidth, front};
            const int hallTop = g.hallTop;
            add(spine, "corridor");
            late.push_back([&, column, spine, hallTop, front] {
                const int rows = std::clamp((front - hallTop) / 9, 1, 3);
                const auto cells = PickCastleRooms(catalog, rows * 2, variation + 7);
                fillWing({column.x0, hallTop, spine.x0, front}, std::vector<CastleRoomSpec>(cells.begin(), cells.begin() + rows), 95);
                fillWing({spine.x1, hallTop, column.x1, front}, std::vector<CastleRoomSpec>(cells.begin() + rows, cells.end()), 97);
            });
        } else if (ap.vestibule && !ap.hall && hasAnte && hallAtAnte && front - g.hallTop >= castle_odds::kVestibuleMinDepth && colW >= castle_odds::kVestibuleMinColumn) {
            // A small entrance hall in the middle of the front rows with a room on each side.
            const int vestibuleWidth = std::clamp(colW * castle_odds::kVestibuleWidthShare / 100, castle_odds::kVestibuleMinWidth, castle_odds::kVestibuleMaxWidth);
            const int vestibuleX0 = column.x0 + (colW - vestibuleWidth) / 2;
            const Rect vestibule{vestibuleX0, g.hallTop, vestibuleX0 + vestibuleWidth, front};
            const int hallTop = g.hallTop;
            add(vestibule, "entrance_hall");
            late.push_back([&, column, vestibule, hallTop, front] {
                const int rows = std::clamp((front - hallTop) / 12, 1, 2);
                const auto cells = PickCastleRooms(catalog, rows * 2, variation + 9);
                fillWing({column.x0, hallTop, vestibule.x0, front}, std::vector<CastleRoomSpec>(cells.begin(), cells.begin() + rows), 99);
                fillWing({vestibule.x1, hallTop, column.x1, front}, std::vector<CastleRoomSpec>(cells.begin() + rows, cells.end()), 101);
            });
        } else {
            add({column.x0, g.hallTop, column.x1, front}, hallRole);
        }
        // The throne room: the width of the axis, or narrower (about half its depth, at least 14).
        Rect throne{column.x0, g.privEnd, column.x1, g.throneEnd};
        std::vector<Rect> beside;
        int shape = ap.standard ? 0 : ap.throneShape;
        // (At least 22 wide where doors of the suite open onto it, so the floor before the throne stays clear of their landings.)
        const int narrow = std::clamp((td + 1) / 2, suiteH > 0 ? castle_odds::kThroneMinWidthWithSuite : castle_odds::kThroneMinWidth, colW);
        const int rest = colW - narrow;
        if (shape == 1 && rest < 12) shape = rest >= 8 ? 2 : 0;
        if (shape == 2 && rest < 8) shape = 0;
        if (shape == 1) {
            const int each = rest / 2;
            throne = {column.x0 + each, g.privEnd, column.x0 + each + narrow, g.throneEnd};
            beside.push_back({column.x0, g.privEnd, column.x0 + each, g.throneEnd});
            beside.push_back({throne.x1, g.privEnd, column.x1, g.throneEnd});
        } else if (shape == 2) {
            const bool onLeft = pick(89, 2) == 0;
            throne = onLeft ? Rect{column.x0, g.privEnd, column.x0 + narrow, g.throneEnd} : Rect{column.x1 - narrow, g.privEnd, column.x1, g.throneEnd};
            beside.push_back(onLeft ? Rect{throne.x1, g.privEnd, column.x1, g.throneEnd} : Rect{column.x0, g.privEnd, throne.x0, g.throneEnd});
        }
        add(throne, "throne_room");
        late.push_back([&, column, g, beside, shape, suiteH, y0, hasAnte] {
            if (hasAnte) add({column.x0, g.throneEnd, column.x1, g.anteEnd}, "antechamber");
            if (suiteH > 0) addSuiteRooms({column.x0, y0, column.x1, g.privEnd}, ap.suite);
            // Each room beside the throne room is one long room, or two or three rooms in a row (drawn per side).
            for (std::size_t i = 0; i < beside.size(); ++i) {
                const Rect& side = beside[i];
                const auto& roles = shape == 1 ? flankRoles : bigRoles;
                const int draw = pick(100 + static_cast<unsigned>(i), 100);
                int pieces = draw < castle_odds::kSidePiecesOnePercent ? 1 : draw < castle_odds::kSidePiecesOnePercent + castle_odds::kSidePiecesTwoPercent ? 2 : 3;
                pieces = std::clamp(pieces, 1, std::max(1, std::min(side.Height() / 9, side.Height() / std::max(1, side.Width() / 2))));
                // A long thin side room is split (up to three rooms), whatever the draw said.
                {
                    const int shortSide = std::max(1, std::min(side.Width(), side.Height())), longSide = std::max(side.Width(), side.Height());
                    if (longSide * 10 > shortSide * castle_odds::kRoomMaxAspectTenths) {
                        const int target = shortSide * castle_odds::kSplitTargetAspectTenths;
                        pieces = std::max(pieces, std::min({4, (longSide * 10 + target - 1) / target, longSide / castle_odds::kSplitMinPiece}));
                    }
                    // A side room is no larger than the size limit of a wing room (a wide keep makes the room beside a narrowed
                    // throne room wide).
                    const int areaPieces = (side.Width() * side.Height() * 100 + roomCap * 85 - 1) / (roomCap * 85);
                    pieces = std::max(pieces, std::min({4, areaPieces, longSide / castle_odds::kSplitMinPiece}));
                }
                // A piece is sometimes a special room (a treasury, the castle's own kind of room), and one of several
                // pieces (at least 8 across) sometimes an open courtyard: no room at all.
                const auto specialDraw = [&](unsigned tag) { return pick(130 + tag, 100); };
                if (pieces == 1) {
                    const int d = specialDraw(static_cast<unsigned>(i));
                    add(side, d < castle_odds::kSideSpecialRoomPercent ? SpecialRoomRole(mode, d) : roles[static_cast<std::size_t>(pick(90 + static_cast<unsigned>(i), static_cast<int>(roles.size())))]);
                    continue;
                }
                std::vector<Rect> plots;
                SplitIntoRooms(side, pieces, variation, 110 + static_cast<unsigned>(i) * 5, plots);
                std::sort(plots.begin(), plots.end(), [](const Rect& l, const Rect& r) { return l.y0 < r.y0; });
                const int first = pick(104 + static_cast<unsigned>(i), static_cast<int>(flankRoles.size()));
                bool courtyard = false;
                for (std::size_t k = 0; k < plots.size(); ++k) {
                    const int d = specialDraw(static_cast<unsigned>(10 + i * 5 + k));
                    if (!courtyard && d >= castle_odds::kSideCourtyardFrom && d < castle_odds::kSideCourtyardFrom + castle_odds::kSideCourtyardPercent && side.Width() >= castle_odds::kCourtyardMinSize && plots[k].Width() >= castle_odds::kCourtyardMinSize && plots[k].Height() >= castle_odds::kCourtyardMinSize) { courtyard = true; continue; }
                    add(plots[k], d < castle_odds::kSideSpecialRoomPercent ? SpecialRoomRole(mode, d) : flankRoles[(static_cast<std::size_t>(first) + k) % flankRoles.size()]);
                }
            }
        });
        return g;
    };

    if (mode == CastleMode::Hill || mode == CastleMode::River) {
        const bool hill = mode == CastleMode::Hill;
        // A single axis column; the wing rooms fill the remaining band.
        // The hill tower is narrower than the keep so the band can reach out on both
        // sides, and it may stand off-centre (30-70% of the free width).
        const int columnWidth = hill ? std::min(w, std::clamp(std::min(axisCap - pick(1, castle_odds::kHillColumnTrimRange) * 4, w * (castle_odds::kHillColumnBase + pick(53, castle_odds::kHillColumnSpread)) / 100), castle_odds::kHillColumnMin, axisCap))
            : std::min(std::min(w - 8, axisCap), std::max(castle_odds::kColumnMin, w * (castle_odds::kColumnBase + pick(1, castle_odds::kColumnSpread)) / 100));
        const int columnX0 = hill ? bounds.x0 + (w - columnWidth) * (castle_odds::kHillColumnOffsetBase + pick(52, castle_odds::kHillColumnOffsetSpread)) / 100 : bounds.x0;
        const Rect column{columnX0, bounds.y0, columnX0 + columnWidth, bounds.y1};
        // The hall shares the axis column's width: the public rooms stay on one axis (the throne room
        // alone may be narrower than the column).
        const int hallTopFixed = hill ? bounds.y1 - std::max(4, h / (compact ? castle_odds::kHillHallDivisorCompact : castle_odds::kHillHallDivisorBase + pick(54, castle_odds::kHillHallDivisorSpread))) : -1;
        const AxisGeometry axis = buildAxis(column, bounds.y1, hallTopFixed, hill ? (compact ? castle_odds::kHillThroneShareCompact : castle_odds::kHillThroneShare) : castle_odds::kThroneShare);
        const int anteEnd = axis.anteEnd, hallTop = axis.hallTop;
        // The band is built from blocks of different reach, so the outline steps
        // instead of staying one straight strip (kept at least 8 cells across).
        std::vector<Rect> blocks;
        const auto addBlock = [&](const Rect& block) { if (block.Width() >= 8 && block.Height() >= 8) blocks.push_back(block); };
        if (hill) {
            splitJitter = castle_odds::kBandSplitJitterPercent;
            const int bandH = hallTop - anteEnd;
            const auto inset = [&](unsigned tag) { const int cells = w * pick(tag, castle_odds::kHillInsetRange) * castle_odds::kHillInsetStep / 100; return cells < 6 ? 0 : cells; };
            const int inL = inset(40), inR = inset(41);
            const auto trim = [&](unsigned tag) { const int cells = bandH * pick(tag, castle_odds::kHillTrimRange) * castle_odds::kHillTrimStep / 100; return cells < 6 ? 0 : cells; };
            // (The two trims of a side leave at least kHillBandMinSpan rows: a side never loses its blocks to them.)
            int trimTL = trim(43), trimBL = trim(44), trimTR = trim(45), trimBR = trim(46);
            const int trimRoom = std::max(0, bandH - castle_odds::kHillBandMinSpan);
            if (trimTL + trimBL > trimRoom) trimBL = std::max(0, trimRoom - trimTL);
            if (trimTR + trimBR > trimRoom) trimBR = std::max(0, trimRoom - trimTR);
            // One side of the band (drawn by the seed) has a block that reaches out to the full width: the castle keeps its width,
            // and the other side is free to step in.
            // Now and then the band has a wing on one side only (the side with more room): the outline is lopsided, and the
            // castle still spans (the tower and the wing) at least two thirds of its width.
            // (Only where the empty side is a small part of the width: the castle keeps at least two thirds of it.)
            const bool oneSided = pick(56, 100) < castle_odds::kHillOneSidedPercent &&
                std::min(column.x0 - bounds.x0, bounds.x1 - column.x1) * 100 <= w * castle_odds::kHillOneSidedMaxEmptyPercent;
            const bool dropLeft = oneSided && column.x0 - bounds.x0 < bounds.x1 - column.x1, dropRight = oneSided && !dropLeft;
            const bool fullLeft = dropRight ? true : dropLeft ? false : pick(55, 2) == 0;
            // (The side that reaches out to the full width has no inset: it reaches the edge.)
            const int leftFree = column.x0 - (bounds.x0 + (fullLeft ? 0 : inL)), rightFree = bounds.x1 - (!fullLeft ? 0 : inR) - column.x1;
            if (!dropLeft)
                for (const auto& block : stackBlocks(true, column.x0, leftFree, anteEnd + trimTL, hallTop - trimBL, 60, 8, fullLeft, fullLeft ? castle_odds::kStackReachBase : castle_odds::kHillStepInReachBase)) addBlock(block);
            addBlock({column.x0, anteEnd, column.x1, hallTop});
            if (!dropRight)
                for (const auto& block : stackBlocks(false, column.x1, rightFree, anteEnd + trimTR, hallTop - trimBR, 66, 8, !fullLeft, !fullLeft ? castle_odds::kStackReachBase : castle_odds::kHillStepInReachBase)) addBlock(block);
        } else {
            const int bandH = std::max(8, h * (castle_odds::kBandBase + pick(2, castle_odds::kBandSpread)) / 100);
            const int blockCount = bandH >= 30 && column.x1 + 16 <= bounds.x1 && (flatSides & 2) == 0 ? 1 + pick(47, castle_odds::kStackedBlocksMax) : 1;
            const int offset = (h - bandH) * pick(48, 3) / 2;
            const int bandW = bounds.x1 - column.x1;
            const bool notch = blockCount == 3 && pick(51, castle_odds::kNotchRange) < castle_odds::kNotchHits;
            for (int i = 0; i < blockCount; ++i) {
                if (notch && i == 1) continue;
                const int top = bounds.y0 + offset + bandH * i / blockCount, bottom = bounds.y0 + offset + bandH * (i + 1) / blockCount;
                int inset = blockCount == 1 ? 0 : bandW * (pick(49 + static_cast<unsigned>(i) * 5, castle_odds::kBandInsetRange) * castle_odds::kBandInsetStep) / 100;
                if (inset < 6) inset = 0;
                addBlock({column.x1, top, bounds.x1 - inset, bottom});
            }
        }
        const std::vector<int> pieces = planBlocks(blocks, wingRooms + suiteExtra);
        int totalPieces = 0;
        for (const int piece : pieces) totalPieces += piece;
        auto picked = PickCastleRooms(catalog, std::max(0, totalPieces - suiteExtra), variation);
        std::stable_sort(picked.begin(), picked.end(), [&](const CastleRoomSpec& a, const CastleRoomSpec& b) { return zoneRank(a.zone) < zoneRank(b.zone); });
        if (suiteToWing) {
            // The suite's rooms lead the list: they take the back of the band.
            std::vector<CastleRoomSpec> suite;
            for (const auto& role : suiteRoles) suite.push_back({role, 95, 'A', 'B'});
            picked.insert(picked.begin(), suite.begin(), suite.end());
        }
        dispatchRooms(blocks, pieces, picked, 11);
    } else {
        struct Silhouette { int leftWidth, leftBack, leftEnd, rightWidth, rightBack, rightEnd; };  // percent
        const auto& row = castle_odds::kSilhouettes[static_cast<std::size_t>(pick(3, castle_odds::kSilhouetteKinds))];
        Silhouette shape = {row[0], row[1], row[2], row[3], row[4], row[5]};
        const bool palace = mode == CastleMode::Palace;
        if (palace) shape = {castle_odds::kPalaceWingBase + pick(4, castle_odds::kPalaceWingSpread), 0, 100, castle_odds::kPalaceWingBase + pick(5, castle_odds::kPalaceWingSpread), 0, 100};  // full-height wings, uneven widths
        int leftWing = w * shape.leftWidth / 100, rightWing = w * shape.rightWidth / 100;
        if (leftWing < 5) leftWing = 0;
        if (rightWing < 5) rightWing = 0;
        const int excess = std::max(0, (w - leftWing - rightWing) - axisCap);
        int margin = 0;
        if (leftWing > 0 && rightWing > 0) { leftWing += excess / 2; rightWing += excess - excess / 2; }
        else if (leftWing > 0) leftWing += excess;    // (a single wing takes the width the axis cannot use: no empty land beside the castle)
        else if (rightWing > 0) rightWing += excess;
        else margin = excess;
        const int axisWidth = w - leftWing - rightWing - margin;
        const int startX = bounds.x0 + (leftWing == 0 ? margin : 0);
        const int left = startX + leftWing, right = left + axisWidth;
        const int front = palace ? bounds.y0 + std::max(8, h * castle_odds::kPalaceFrontShare / 100) : bounds.y1;
        const AxisGeometry axis = buildAxis({left, bounds.y0, right, bounds.y1}, front, -1, castle_odds::kThroneShare);
        const int privEnd = axis.privEnd, throneEnd = axis.throneEnd, anteEnd = axis.anteEnd;
        // Wing rooms: split by wing area, sized by the limit, filled from the catalog.
        // Each wing is 1-3 stacked blocks of different reach, so its outer face steps.
        // Reach and extent also drift a little per seed (not for the palace's full-height wings).
        const auto wingBlocks = [&](bool leftSide, int wingWidth, int backPct, int endPct, unsigned tag) {
            if ((flatSides & 4) == 0) backPct += pick(tag, castle_odds::kWingBackSpread);
            // The palace wings stay long, but each may stop short of the front by its own amount.
            endPct = std::max(backPct + 40, endPct - (palace ? pick(tag + 1, castle_odds::kPalaceWingEndRange) * 10 : pick(tag + 1, castle_odds::kWingEndSpread)));
            endPct = std::min(endPct, 100);
            // A wing end within a few cells of an axis room's edge snaps to it: a small jog
            // between the two would leave a thick or ragged piece of wall.
            const auto snapY = [&](int y) {
                for (const int edge : {bounds.y0, privEnd, throneEnd, anteEnd, front, bounds.y1})
                    if (std::abs(y - edge) < 6) return edge;
                return y;
            };
            const int top = snapY(bounds.y0 + h * backPct / 100);
            const int bottom = snapY(bounds.y0 + h * endPct / 100);
            return stackBlocks(leftSide, leftSide ? left : right, wingWidth, top, bottom, tag, 5);
        };
        std::vector<Rect> leftBlocks = wingBlocks(true, leftWing, shape.leftBack, shape.leftEnd, 30),
            rightBlocks = wingBlocks(false, rightWing, shape.rightBack, shape.rightEnd, 36);
        const auto areaOf = [](const std::vector<Rect>& blocks) {
            int area = 0;
            for (const auto& block : blocks) area += std::max(0, block.Width() * block.Height());
            return area;
        };
        const int leftArea = areaOf(leftBlocks), rightArea = areaOf(rightBlocks);
        const int wingTotal = wingRooms + suiteExtra;
        int leftRequested = 0, rightRequested = 0;
        if (leftArea + rightArea > 0) {
            leftRequested = rightArea == 0 ? wingTotal : leftArea == 0 ? 0 :
                std::clamp((wingTotal * leftArea + (leftArea + rightArea) / 2) / (leftArea + rightArea), 1, wingTotal - 1);
            rightRequested = wingTotal - leftRequested;
        }
        const std::vector<int> leftBlockPieces = planBlocks(leftBlocks, leftRequested);
        const std::vector<int> rightBlockPieces = planBlocks(rightBlocks, rightRequested);
        int leftPieces = 0, rightPieces = 0;
        for (const int piece : leftBlockPieces) leftPieces += piece;
        for (const int piece : rightBlockPieces) rightPieces += piece;
        auto picked = PickCastleRooms(catalog, std::max(0, leftPieces + rightPieces - suiteExtra), variation);
        if (suiteToWing) {
            // The suite's rooms lead the list, all on one side: they take the back of that wing.
            const char suiteSide = pick(85, 2) == 0 ? 'L' : 'R';
            std::vector<CastleRoomSpec> suite;
            for (const auto& role : suiteRoles) suite.push_back({role, 95, suiteSide, 'B'});
            picked.insert(picked.begin(), suite.begin(), suite.end());
        }
        std::vector<CastleRoomSpec> leftRoles, rightRoles;
        for (const auto& spec : picked) {
            const bool toLeft = spec.side == 'L' ? static_cast<int>(leftRoles.size()) < leftPieces :
                spec.side == 'R' ? static_cast<int>(rightRoles.size()) >= rightPieces :
                static_cast<int>(leftRoles.size()) * std::max(1, rightPieces) <= static_cast<int>(rightRoles.size()) * std::max(1, leftPieces);
            if (toLeft && static_cast<int>(leftRoles.size()) < leftPieces) leftRoles.push_back(spec);
            else if (static_cast<int>(rightRoles.size()) < rightPieces) rightRoles.push_back(spec);
            else leftRoles.push_back(spec);
        }
        for (auto* roles : {&leftRoles, &rightRoles})
            std::stable_sort(roles->begin(), roles->end(), [&](const CastleRoomSpec& a, const CastleRoomSpec& b) { return zoneRank(a.zone) < zoneRank(b.zone); });
        dispatchRooms(leftBlocks, leftBlockPieces, leftRoles, 21);
        dispatchRooms(rightBlocks, rightBlockPieces, rightRoles, 22);
    }
    for (auto& step : late) step();
    if (!valid) result.clear();
    return result;
}

// The planner: draws what stands on the axis; if that does not fit the keep, falls back to the standard
// axis (throne room across the axis, antechamber, bedchamber/dressing room/study). `entry` is the index of the
// room that faces the gate.
std::vector<RoomInfo> PlanKeepRoomsVaried(CastleMode mode, const Rect& bounds, int count, unsigned variation, bool autoSplit, int flatSides, int& entry) {
    std::vector<RoomInfo> rooms = PlanKeepRoomsVariedImpl(mode, bounds, count, variation, autoSplit, flatSides, RollAxisPlan(variation), entry);
    if (rooms.empty()) rooms = PlanKeepRoomsVariedImpl(mode, bounds, count, variation, autoSplit, flatSides, StandardAxisPlan(), entry);
    return rooms;
}

// A keep shorter than 30 cells (an explicit tiny `keepHeight`) cannot hold the full
// throne axis, so it keeps a two-room axis (throne room, entrance hall) and fills
// the sides with the program's rooms. Taller keeps use PlanKeepRoomsVaried.
// Only secondary apartments are subdivided; the major halls keep their area.
std::vector<RoomInfo> PlanKeepRooms(CastleMode mode, const CastleProgram& program, const Rect& bounds, int count, int& entry,
    unsigned variation = 0, bool autoSplit = false, int flatSides = 0) {
    if (bounds.Height() >= 30) return PlanKeepRoomsVaried(mode, bounds, count, variation, autoSplit, flatSides, entry);
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
        return biome != MOUNTAIN && biome != EXMOUNTAIN && biome != TOWN_CLIFF && !IsWater(biome);
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
                if (before == MOUNTAIN || before == EXMOUNTAIN || before == TOWN_CLIFF || before == SEA) continue;
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

// Corridors (docs/dig/2026-11-17-castle-corridors.md). A corridor is a room with the role
// "corridor", cut out of the rooms beside it and not counted in the requested room count.
//  - vertical wing corridor: 8 wide (7 floor cells), along the central block;
//  - horizontal wing corridor: 8 high (7 floor cells), out of the central block along a boundary
//    between two rows of rooms (or out of a vertical corridor, which it joins through an opened
//    wall); its end door is 4 wide, which needs the extra width;
//  - the Hill band corridor: 8 wide, between the entrance hall and the antechamber.
// The rooms beside a corridor ("served") open onto it and have no door to each other or to the
// central rooms; a corridor opens only into the central rooms it is allowed to (never the throne
// room or the private suite). A corridor does not run on for more than a few cells past its last
// door: the end room takes the space back (it is then no longer served).
struct CorridorLink {
    int id = -1;
    std::vector<int> served;   // rooms that open onto the corridor
    std::vector<int> allowed;  // central rooms and corridors the corridor may open into
    bool horizontal = false;
};

constexpr int kCorridorWidth = castle_odds::kCorridorWidth;      // vertical wing corridor (all corridors are as wide)
constexpr int kBandCorridorWidth = castle_odds::kCorridorWidth;  // band and horizontal corridors; their 4-wide end doors need at least 6
constexpr int kCorridorMinRoomWidth = 12;
constexpr int kCorridorMinLength = 12;
constexpr int kCorridorMaxDeadEnd = 6;

// Where the door between a corridor and a room goes along a shared wall of [a0, a1) cells: the
// same rule as the room doors below (middle of the overlap, 4 wide where the wall allows).
int CorridorDoorStart(int a0, int a1, int& width) {
    const int span = a1 - a0 - 2;
    width = std::max(1, std::min(4, span));
    const int lo = a0 + 1, hi = a1 - 2;
    return std::clamp((lo + hi) / 2 - (width - 1) / 2, lo, std::max(lo, lo + span - width));
}

// A long thin room (its long side more than castle_odds::kRoomMaxAspectTenths/10 times its short side) is split across
// its long side into rooms of different sizes. The largest piece keeps the role; every other piece takes a role of its
// own (not the same as another piece's): a dressing room, a study... for a private room, the castle's own kinds of room
// for the rest. The throne room, the antechamber, the front room and the corridors are long on purpose.
void SplitThinRooms(std::vector<RoomInfo>& rooms, CastleMode mode, unsigned variation, int hallIndex) {
    // The roles the other pieces can take: rooms of the kind the room belongs to.
    const auto rolePool = [&](bool isPrivate) -> std::vector<std::string> {
        if (isPrivate) return {"royal_dressing_room", "royal_study", "royal_children_chamber", "royal_treasury"};
        switch (mode) {
            case CastleMode::Hill: return {"reserve_store", "map_room", "signal_room", "castellan_chamber"};
            case CastleMode::River: return {"chart_archive", "toll_office", "customs_warehouse", "strongroom"};
            case CastleMode::Palace: return {"library", "music_room", "pantry", "guard_lodge"};
            case CastleMode::Fortress: return {"dispatch_office", "war_archive", "armory", "provision_store"};
            default: return {"archive", "guard_room", "council_chamber", "granary"};
        }
    };
    const std::size_t original = rooms.size();
    // Hill's band of small rooms between the hall and the antechamber: its corridor needs whole columns, so a tall room
    // of the band is split into rooms one above the other (the column stays whole), and a wide one is left as it is.
    const Rect hall = rooms[static_cast<std::size_t>(hallIndex)].rect;
    Rect ante{0, 0, 0, 0};
    for (const RoomInfo& room : rooms) if (room.role == "antechamber") ante = room.rect;
    for (std::size_t i = 0; i < original; ++i) {
        const std::string role = rooms[i].role;
        if (role == "corridor" || role == "throne_room" || role == "antechamber" || IsCastleEntryRole(role)) continue;
        // Rooms furnished to a size (kitchens, bedrooms and the like) are not split into rooms shorter than 12: the furniture
        // needs the room.
        bool furnishedToSize = false;
        for (const char* word : {"kitchen", "quarters", "barracks", "apartment", "chamber", "hospital", "mess"})
            furnishedToSize = furnishedToSize || role.find(word) != std::string::npos;
        const Rect rect = rooms[i].rect;
        const bool inBand = ante.Width() > 0 && rect.y0 >= ante.y1 && rect.y1 <= hall.y0 && rect.x0 >= hall.x0 && rect.x1 <= hall.x1;
        if (inBand && rect.Width() >= rect.Height()) continue;
        const int shortSide = std::min(rect.Width(), rect.Height()), longSide = std::max(rect.Width(), rect.Height());
        if (shortSide < 1 || longSide * 10 <= shortSide * castle_odds::kRoomMaxAspectTenths) continue;
        // A room beside the throne room that is long along the throne room's wall is left to the side-room code (below): its
        // pieces would each have a door onto that wall, and the door landings would have no way through. A room that is long
        // away from the wall splits into pieces of which only the first touches it.
        bool besideThrone = false;
        for (const RoomInfo& other : rooms)
            if (other.role == "throne_room" && (rect.x1 == other.rect.x0 || rect.x0 == other.rect.x1) && std::min(rect.y1, other.rect.y1) - std::max(rect.y0, other.rect.y0) >= 4) besideThrone = true;
        if (besideThrone && rect.Height() > rect.Width()) continue;
        const int target = shortSide * castle_odds::kSplitTargetAspectTenths;
        int pieces = (longSide * 10 + target - 1) / target;
        // (A kitchen, a bedroom and the like are not split into rooms shorter than 12: the furniture needs the room.)
        const int minLength = furnishedToSize ? castle_odds::kSplitMinPieceFurnished : castle_odds::kSplitMinPiece;
        pieces = std::clamp(pieces, 2, std::max(2, longSide / minLength));
        std::vector<Rect> parts;
        SplitIntoRooms(rect, pieces, variation, 200u + static_cast<unsigned>(i) * 7u, parts, castle_odds::kSplitJitterPercent, minLength);
        if (parts.size() < 2) continue;
        std::sort(parts.begin(), parts.end(), [](const Rect& l, const Rect& r) { return l.y0 == r.y0 ? l.x0 < r.x0 : l.y0 < r.y0; });
        // The largest piece keeps the role (so that a bedroom or a kitchen keeps room for its furniture).
        std::size_t largest = 0;
        for (std::size_t k = 1; k < parts.size(); ++k) if (parts[k].Width() * parts[k].Height() > parts[largest].Width() * parts[largest].Height()) largest = k;
        std::swap(parts[0], parts[largest]);
        rooms[i].rect = parts[0];
        const std::vector<std::string> options = rolePool(IsCastlePrivateRole(role));
        std::vector<std::string> used{role};
        const std::size_t start = static_cast<std::size_t>(CatalogHash(variation, 300u + static_cast<unsigned>(i) * 5u)) % options.size();
        for (std::size_t k = 1; k < parts.size(); ++k) {
            RoomInfo piece = rooms[i];
            piece.rect = parts[k];
            piece.regionId = static_cast<int>(rooms.size());  // (a room's id is its place in the list)
            // The next role of the pool that no piece of this room has yet (the pool is longer than the number of pieces).
            piece.role = options[(start + k) % options.size()];
            for (std::size_t step = 0; step < options.size(); ++step) {
                const std::string& candidate = options[(start + k + step) % options.size()];
                if (std::find(used.begin(), used.end(), candidate) == used.end()) { piece.role = candidate; break; }
            }
            used.push_back(piece.role);
            rooms.push_back(std::move(piece));
        }
    }
}

std::vector<CorridorLink> AddKeepCorridors(std::vector<RoomInfo>& rooms, CastleMode mode, double wealth, std::mt19937& rng, int hallIndex) {
    std::vector<CorridorLink> links;
    if (rooms.size() < 4) return links;
    // A corridor the planner drew (the spine of the small rooms at the back of a castle with no throne
    // room): the rooms beside it open onto it, and it opens into the hall in front.
    for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
        if (rooms[static_cast<std::size_t>(i)].role != "corridor") continue;
        const Rect c = rooms[static_cast<std::size_t>(i)].rect;
        CorridorLink link;
        link.id = i;
        for (int j = 0; j < static_cast<int>(rooms.size()); ++j) {
            if (j == i) continue;
            const Rect r = rooms[static_cast<std::size_t>(j)].rect;
            const bool beside = (r.x1 == c.x0 || r.x0 == c.x1) && r.y0 < c.y1 && r.y1 > c.y0;
            if (j == hallIndex || (r.y0 == c.y1 && r.x0 < c.x1 && r.x1 > c.x0)) link.allowed.push_back(j);
            else if (beside) link.served.push_back(j);
            else if (r.y1 == c.y0 && r.x0 < c.x1 && r.x1 > c.x0) link.allowed.push_back(j);  // the big room across the back
        }
        links.push_back(std::move(link));
    }
    if (!links.empty()) return links;
    int anteIndex = -1;
    for (int i = 0; i < static_cast<int>(rooms.size()); ++i) if (rooms[static_cast<std::size_t>(i)].role == "antechamber") anteIndex = i;
    if (anteIndex < 0 || rooms[static_cast<std::size_t>(hallIndex)].role != "entrance_hall") return links;
    const Rect hall = rooms[static_cast<std::size_t>(hallIndex)].rect;
    const Rect ante = rooms[static_cast<std::size_t>(anteIndex)].rect;
    int throneIndex = -1;
    for (int i = 0; i < static_cast<int>(rooms.size()); ++i) if (rooms[static_cast<std::size_t>(i)].role == "throne_room") throneIndex = i;
    const Rect throne = throneIndex >= 0 ? rooms[static_cast<std::size_t>(throneIndex)].rect : Rect{0, 0, 0, 0};
    const auto isPrivate =[](const std::string& role) { return role == "throne_room" || IsCastlePrivateRole(role); };
    const auto eligible = [&](int i) {
        const RoomInfo& room = rooms[static_cast<std::size_t>(i)];
        return i != hallIndex && i != anteIndex && !isPrivate(room.role) && room.role != "corridor" && room.role != "entrance_hall";
    };
    const auto rectOf = [&](int i) -> Rect& { return rooms[static_cast<std::size_t>(i)].rect; };
    const auto addCorridor = [&](const Rect& rect, std::vector<int> served, std::vector<int> allowed, bool horizontal) {
        CorridorLink link;
        link.id = static_cast<int>(rooms.size());
        link.served = std::move(served);
        link.allowed = std::move(allowed);
        link.horizontal = horizontal;
        // A room of the central block that lies along the corridor (beside a narrowed throne room, in the axis column) opens
        // onto it too, like the hall.
        for (int j = 0; j < static_cast<int>(rooms.size()); ++j) {
            const RoomInfo& other = rooms[static_cast<std::size_t>(j)];
            if (j == hallIndex || j == anteIndex || other.role == "corridor" || isPrivate(other.role)) continue;
            if (std::find(link.served.begin(), link.served.end(), j) != link.served.end() ||
                std::find(link.allowed.begin(), link.allowed.end(), j) != link.allowed.end()) continue;
            const Rect& r = other.rect;
            const int rows = std::min(rect.y1, r.y1) - std::max(rect.y0, r.y0), columns = std::min(rect.x1, r.x1) - std::max(rect.x0, r.x0);
            const bool alongSide = horizontal ? ((rect.y1 == r.y0 || r.y1 == rect.y0) && columns >= 4)
                                              : ((rect.x1 == r.x0 || r.x1 == rect.x0) && rows >= 4);
            const bool central = r.x0 >= hall.x0 && r.x1 <= hall.x1;  // inside the axis column
            if (alongSide && central) link.allowed.push_back(j);
        }
        rooms.push_back({rect, "corridor", link.id});
        links.push_back(std::move(link));
        return static_cast<int>(links.size()) - 1;
    };
    // The band between the entrance hall and the antechamber (Hill): columns of small rooms
    // filling the rows between them. A corridor along a boundary between two columns (the one
    // nearest the middle of the hall) joins the two, so the way to the antechamber no longer
    // runs through a service room.
    {
        std::vector<int> band;
        for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
            const Rect& r = rectOf(i);
            if (eligible(i) && r.y0 >= ante.y1 && r.y1 <= hall.y0 && r.x0 >= hall.x0 && r.x1 <= hall.x1) band.push_back(i);
        }
        std::set<int> columns;
        for (const int i : band) columns.insert(rectOf(i).x0);
        const auto bySpan = [&](int a, int b) { return rectOf(a).y0 < rectOf(b).y0; };
        const auto fills = [&](std::vector<int> column) {
            std::sort(column.begin(), column.end(), bySpan);
            int y = ante.y1;
            for (const int i : column) {
                if (rectOf(i).y0 != y) return false;
                y = rectOf(i).y1;
            }
            return y == hall.y0;
        };
        int bestBoundary = -1;
        std::vector<int> bestLeft, bestRight;
        if (band.size() >= 2 && columns.size() >= 2 && ante.y1 + kCorridorMinLength <= hall.y0) {
            for (const int boundary : columns) {
                std::vector<int> left, right;
                for (const int i : band) {
                    const Rect& r = rectOf(i);
                    if (r.x1 == boundary) left.push_back(i);
                    if (r.x0 == boundary) right.push_back(i);
                }
                if (left.empty() || right.empty() || !fills(left) || !fills(right)) continue;
                bool ok = true;
                for (const int i : left) ok = ok && boundary - kBandCorridorWidth / 2 - rectOf(i).x0 >= castle_odds::kBandCorridorMinRoomWidth;
                for (const int i : right) ok = ok && rectOf(i).x1 - (boundary + kBandCorridorWidth / 2) >= castle_odds::kBandCorridorMinRoomWidth;
                if (!ok) continue;
                if (bestBoundary < 0 || std::abs(2 * boundary - (hall.x0 + hall.x1)) < std::abs(2 * bestBoundary - (hall.x0 + hall.x1))) {
                    bestBoundary = boundary; bestLeft = left; bestRight = right;
                }
            }
        }
        if (bestBoundary >= 0) {
            for (const int i : bestLeft) rectOf(i).x1 = bestBoundary - kBandCorridorWidth / 2;
            for (const int i : bestRight) rectOf(i).x0 = bestBoundary + kBandCorridorWidth / 2;
            std::vector<int> served = bestLeft;
            served.insert(served.end(), bestRight.begin(), bestRight.end());
            addCorridor({bestBoundary - kBandCorridorWidth / 2, ante.y1, bestBoundary + kBandCorridorWidth / 2, hall.y0}, served, {hallIndex, anteIndex}, false);
        }
    }
    // Wings: rooms stacked flush against the side of the central block. The chance of a corridor
    // depends on the castle type (a fortress is plain and functional) and on its wealth, and is
    // drawn separately for the vertical and the horizontal corridor of each wing.
    double chance = castle_odds::kCorridorChanceConcentric;
    switch (mode) {
        case CastleMode::Palace: chance = castle_odds::kCorridorChancePalace; break;
        case CastleMode::Concentric: chance = castle_odds::kCorridorChanceConcentric; break;
        case CastleMode::Hill: chance = castle_odds::kCorridorChanceHill; break;
        case CastleMode::Fortress: chance = castle_odds::kCorridorChanceFortress; break;
        default: chance = castle_odds::kCorridorChanceRiver; break;
    }
    chance = std::clamp(chance + (wealth - 1.0) * castle_odds::kCorridorChancePerWealth, 0.0, 1.0);
    const auto draw = [&]() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng) < chance; };
    for (int side = 0; side < 2; ++side) {  // 0: left of the central block, 1: right
        // A vertical corridor along the central block: three or more rooms in a stack that reaches the hall.
        int verticalLink = -1;
        {
            std::vector<int> flush;
            for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
                if (!eligible(i)) continue;
                const Rect& r = rectOf(i);
                if ((side == 0 && r.x1 == hall.x0) || (side == 1 && r.x0 == hall.x1)) flush.push_back(i);
            }
            std::sort(flush.begin(), flush.end(), [&](int a, int b) { return rectOf(a).y0 < rectOf(b).y0; });
            std::vector<std::vector<int>> runs;
            for (const int i : flush) {
                if (!runs.empty() && rectOf(runs.back().back()).y1 == rectOf(i).y0) runs.back().push_back(i);
                else runs.push_back({i});
            }
            for (auto& run : runs) {
                if (run.size() < 3) continue;
                bool wide = true;
                for (const int i : run) {
                    const Rect& r = rectOf(i);
                    wide = wide && (side == 0 ? r.x1 - kCorridorWidth - r.x0 : r.x1 - (r.x0 + kCorridorWidth)) >= kCorridorMinRoomWidth;
                }
                if (!wide) continue;
                // Take back the dead ends: an end room whose last door would leave more than a few
                // cells of corridor beyond it is not served, so it keeps its full width.
                int y0 = rectOf(run.front()).y0, y1 = rectOf(run.back()).y1;
                std::vector<int> served = run;
                for (bool again = true; again && served.size() > 2;) {
                    again = false;
                    int width = 4;
                    const auto reachesHall = [&](int from, int to) { return std::min(to, hall.y1) - std::max(from, hall.y0) >= 6 && to - from >= kCorridorMinLength; };
                    const int topStart = CorridorDoorStart(rectOf(served.front()).y0, rectOf(served.front()).y1, width);
                    if (topStart - (y0 + 1) > kCorridorMaxDeadEnd && reachesHall(rectOf(served[1]).y0, y1)) {
                        served.erase(served.begin());
                        y0 = rectOf(served.front()).y0;
                        again = true;
                    }
                    if (served.size() <= 2) break;
                    const int bottomStart = CorridorDoorStart(rectOf(served.back()).y0, rectOf(served.back()).y1, width);
                    if ((y1 - 1) - (bottomStart + width - 1) > kCorridorMaxDeadEnd && reachesHall(y0, rectOf(served[served.size() - 2]).y1)) {
                        served.pop_back();
                        y1 = rectOf(served.back()).y1;
                        again = true;
                    }
                }
                // The corridor has to reach the entrance hall's side wall, with room for its door.
                if (std::min(y1, hall.y1) - std::max(y0, hall.y0) < 6 || y1 - y0 < kCorridorMinLength) continue;
                if (!draw()) continue;
                for (const int i : served) {
                    Rect& r = rectOf(i);
                    if (side == 0) r.x1 -= kCorridorWidth; else r.x0 += kCorridorWidth;
                }
                const Rect strip = side == 0 ? Rect{hall.x0 - kCorridorWidth, y0, hall.x0, y1} : Rect{hall.x1, y0, hall.x1 + kCorridorWidth, y1};
                verticalLink = addCorridor(strip, served, {hallIndex}, false);
                break;
            }
        }
        // A horizontal corridor out of the central block (or out of the vertical corridor), along a
        // boundary between two rows of rooms. The central room it opens into is the antechamber, the
        // hall or the throne room (when the wing lies flush against its side), and the corridor lies
        // wholly within that room's rows. A tall wing can have a second one (castle_odds::kCorridorSecondPercent).
        bool madeHorizontal = false;
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 1 && (!madeHorizontal || std::uniform_int_distribution<int>(0, 99)(rng) >= castle_odds::kCorridorSecondPercent)) break;
            const bool joinsVertical = verticalLink >= 0;
            const Rect inner = joinsVertical ? rectOf(links[static_cast<std::size_t>(verticalLink)].id) : hall;
            const int innerX = joinsVertical ? (side == 0 ? inner.x0 : inner.x1) : (side == 0 ? hall.x0 : hall.x1);
            const auto flushRoom = [&](int i, int atX) { return side == 0 ? rectOf(i).x1 == atX : rectOf(i).x0 == atX; };
            // Rooms are chained outward from the inner edge, all above (or all below) the boundary.
            const auto chain = [&](int boundary, bool above) {
                std::vector<int> result;
                int edge = innerX;
                for (bool found = true; found;) {
                    found = false;
                    for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
                        if (!eligible(i) || !flushRoom(i, edge) || (above ? rectOf(i).y1 : rectOf(i).y0) != boundary) continue;
                        result.push_back(i);
                        edge = side == 0 ? rectOf(i).x0 : rectOf(i).x1;
                        found = true;
                        break;
                    }
                }
                return result;
            };
            // The rooms of the private suite that lie against the end of a corridor spanning the rows [top, bottom] (6 rows or more shared).
            const auto suiteBeside = [&](int top, int bottom) {
                std::vector<int> result;
                for (int j = 0; j < static_cast<int>(rooms.size()); ++j) {
                    const Rect& r = rectOf(j);
                    if (!IsCastlePrivateRole(rooms[static_cast<std::size_t>(j)].role) || (side == 0 ? r.x0 != innerX : r.x1 != innerX)) continue;
                    if (std::min(bottom, r.y1) - std::max(top, r.y0) >= 6) result.push_back(j);
                }
                return result;
            };
            struct Option { int boundary; std::vector<int> above, below; int outer; bool edge; };
            std::vector<Option> options;
            // Where a corridor may run: along a boundary between two rows (kind 0), or along the outer edge of the wing, the
            // window side, with rooms on one side only (kind 1: the top edge, rooms below it; kind 2: the bottom edge).
            std::set<std::pair<int, int>> candidates;
            for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
                if (!eligible(i) || !flushRoom(i, innerX)) continue;
                candidates.insert({rectOf(i).y1, 0});
                candidates.insert({rectOf(i).y0, 1});
                candidates.insert({rectOf(i).y1, 2});
            }
            for (const auto& [edgeY, kind] : candidates) {
                std::vector<int> above = kind != 1 ? chain(edgeY, true) : std::vector<int>{};
                std::vector<int> below = kind != 2 ? chain(edgeY, false) : std::vector<int>{};
                if (kind == 0 ? (above.empty() || below.empty()) : (above.empty() && below.empty())) continue;
                if (kind != 0) {
                    // Nothing may lie beyond the corridor: its outer side is the wing's outside wall.
                    const std::vector<int>& line = kind == 1 ? below : above;
                    int x0 = innerX, x1 = innerX;
                    for (const int i : line) { x0 = std::min(x0, rectOf(i).x0); x1 = std::max(x1, rectOf(i).x1); }
                    bool free = true;
                    for (int j = 0; j < static_cast<int>(rooms.size()); ++j) {
                        const Rect& r = rectOf(j);
                        if ((kind == 1 ? r.y1 == edgeY : r.y0 == edgeY) && std::min(r.x1, x1) - std::max(r.x0, x0) > 0) free = false;
                    }
                    if (!free) continue;
                }
                const int boundary = kind == 0 ? edgeY : kind == 1 ? edgeY + kBandCorridorWidth / 2 : edgeY - kBandCorridorWidth / 2;
                // The corridor ends where both chains have a room boundary: the farthest common one.
                const auto outerEdges = [&](const std::vector<int>& chainRooms) {
                    std::vector<int> edges;
                    for (const int i : chainRooms) edges.push_back(side == 0 ? rectOf(i).x0 : rectOf(i).x1);
                    return edges;
                };
                const std::vector<int> a = outerEdges(above), b = outerEdges(below);
                int outer = -1;
                if (kind == 0) {
                    for (const int edge : a) if (std::find(b.begin(), b.end(), edge) != b.end())
                        if (outer < 0 || (side == 0 ? edge < outer : edge > outer)) outer = edge;
                } else {
                    for (const int edge : kind == 1 ? b : a) if (outer < 0 || (side == 0 ? edge < outer : edge > outer)) outer = edge;
                }
                if (outer < 0) continue;
                const auto within = [&](const std::vector<int>& chainRooms) {
                    std::vector<int> kept;
                    for (const int i : chainRooms) if (side == 0 ? rectOf(i).x0 >= outer : rectOf(i).x1 <= outer) kept.push_back(i);
                    return kept;
                };
                std::vector<int> servedAbove = within(above), servedBelow = within(below);
                // Take back the dead end at the outer end: the two end rooms keep their rows.
                const auto deadEnd = [&](int room, int outerEdge) {
                    int width = 4;
                    const Rect& r = rectOf(room);
                    const int start = CorridorDoorStart(r.x0, r.x1, width);
                    return side == 0 ? start - (outerEdge + 1) : (outerEdge - 1) - (start + width - 1);
                };
                if (kind != 0) {
                    // One row of rooms: drop the end room while a dead end of more than a few cells would remain.
                    std::vector<int>& row = kind == 1 ? servedBelow : servedAbove;
                    while (row.size() > 2 && deadEnd(row.back(), outer) > kCorridorMaxDeadEnd) {
                        row.pop_back();
                        outer = side == 0 ? rectOf(row.back()).x0 : rectOf(row.back()).x1;
                    }
                } else if (servedAbove.size() + servedBelow.size() > 2 && std::min(deadEnd(servedAbove.back(), outer), deadEnd(servedBelow.back(), outer)) > kCorridorMaxDeadEnd) {
                    std::vector<int> a2 = servedAbove, b2 = servedBelow;
                    a2.pop_back(); b2.pop_back();
                    if (!a2.empty() && !b2.empty()) {
                        const int newOuter = side == 0 ? rectOf(a2.back()).x0 : rectOf(a2.back()).x1;
                        const int newOuterBelow = side == 0 ? rectOf(b2.back()).x0 : rectOf(b2.back()).x1;
                        if (newOuter == newOuterBelow) { servedAbove = a2; servedBelow = b2; outer = newOuter; }
                    }
                }
                const int length = side == 0 ? innerX - outer : outer - innerX;
                if (length < kCorridorMinLength) continue;
                const int top = boundary - kBandCorridorWidth / 2, bottom = boundary + kBandCorridorWidth / 2;
                bool ok = servedAbove.size() + servedBelow.size() >= 2;
                // (A window-side corridor takes its 8 rows from one row of rooms only, so those rooms may end up a little shallower.)
                const int minDepth = kind == 0 ? kCorridorMinRoomWidth : castle_odds::kWindowCorridorMinRoomDepth;
                for (const int i : servedAbove) ok = ok && (top - rectOf(i).y0) >= minDepth;
                for (const int i : servedBelow) ok = ok && (rectOf(i).y1 - bottom) >= minDepth;
                if (!ok) continue;
                // It shares at least 6 rows with the antechamber or the hall (their door is 4 wide, in the shared rows),
                // or, where it joins a vertical corridor, lies within that corridor's floor rows.
                const auto sharesRows = [&](const Rect& room) { return std::min(bottom, room.y1) - std::max(top, room.y0) >= 6; };
                const bool opensToThrone = throneIndex >= 0 && !joinsVertical && (side == 0 ? throne.x0 == innerX : throne.x1 == innerX) && sharesRows(throne);
                // A window-side corridor may also open into a room of the private suite that lies along its end (the wing's
                // top rows lie beside the suite, which the other rooms it could open into do not reach).
                const bool opensToSuite = kind != 0 && !joinsVertical && !suiteBeside(top, bottom).empty();
                if (joinsVertical) {
                    if (!(top >= inner.y0 && bottom <= inner.y1)) continue;
                } else if (!(sharesRows(ante) || sharesRows(hall) || opensToThrone || opensToSuite)) {
                    continue;
                }
                options.push_back({boundary, servedAbove, servedBelow, outer, kind != 0});
            }
            if (!options.empty() && (pass == 1 || draw())) {
                // A corridor on the window side is the rarer kind: castle_odds::kCorridorWindowSidePercent when the wing offers both.
                std::vector<const Option*> windowSide, between;
                for (const auto& option : options) (option.edge ? windowSide : between).push_back(&option);
                const bool useWindowSide = !windowSide.empty() && (between.empty() || std::uniform_int_distribution<int>(0, 99)(rng) < castle_odds::kCorridorWindowSidePercent);
                const std::vector<const Option*>& pool = useWindowSide ? windowSide : between;
                const Option& pick = *pool[static_cast<std::size_t>(std::uniform_int_distribution<int>(0, static_cast<int>(pool.size()) - 1)(rng))];
                const int top = pick.boundary - kBandCorridorWidth / 2, bottom = pick.boundary + kBandCorridorWidth / 2;
                for (const int i : pick.above) rectOf(i).y1 = top;
                for (const int i : pick.below) rectOf(i).y0 = bottom;
                const Rect strip = side == 0 ? Rect{pick.outer, top, innerX, bottom} : Rect{innerX, top, pick.outer, bottom};
                std::vector<int> served = pick.above;
                served.insert(served.end(), pick.below.begin(), pick.below.end());
                std::vector<int> allowed;
                if (joinsVertical) allowed.push_back(links[static_cast<std::size_t>(verticalLink)].id);
                else {
                    const auto sharesRows = [&](const Rect& room) { return std::min(bottom, room.y1) - std::max(top, room.y0) >= 6; };
                    if (sharesRows(ante)) allowed.push_back(anteIndex);
                    if (sharesRows(hall)) allowed.push_back(hallIndex);
                    if (throneIndex >= 0 && (side == 0 ? throne.x0 == innerX : throne.x1 == innerX) && sharesRows(throne)) allowed.push_back(throneIndex);
                    if (pick.edge) for (const int j : suiteBeside(top, bottom)) allowed.push_back(j);
                }
                madeHorizontal = true;
                const int horizontalLink = addCorridor(strip, served, allowed, true);
                if (joinsVertical) links[static_cast<std::size_t>(verticalLink)].allowed.push_back(links[static_cast<std::size_t>(horizontalLink)].id);
            }
        }
    }
    return links;
}

}  // namespace

CastleMapImageGenerator::CastleMapImageGenerator(MapGenParams params) : MapImageGenerator(std::move(params)) {
    // The editor builds castles of 128 x 128 or larger (and does not handle smaller ones): a smaller request is raised to that.
    params_.width = std::max(params_.width, castle_odds::kMinCastleSize);
    params_.depth = std::max(params_.depth, castle_odds::kMinCastleSize);
}

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
        case TOWN_CLIFF: return {100, 103, 113};  // the editor's castleColors[16]
        default:         return {255, 0, 255};
    }
}

// The castle keeps 5% of the map clear on every side. A small map may not hold every castle type in what
// is left: then the margin shrinks (2%, then none) instead of the generation failing.
bool CastleMapImageGenerator::Generate(MapPlan& outPlan, std::string& outError) {
    for (const int percent : {5, 2, 0}) {
        edgeMarginPercent_ = percent;
        moreCandidates_ = false;
        if (!GenerateOnce(outPlan, outError)) continue;
        // A castle that could not be furnished is tried again with twelve layouts instead of six (the first six are among them):
        // the furnished one, if any, is taken. (Unless the caller chose the number of layouts.)
        const bool unfurnished = std::any_of(outPlan.castleHistory.begin(), outPlan.castleHistory.end(), [](const auto& step) {
            return step.phase == "interior_design" && step.change.rfind("unfurnished", 0) == 0;
        });
        if (unfurnished && params_.extra.find("candidateCount") == params_.extra.end()) {
            moreCandidates_ = true;
            MapPlan retry;
            std::string retryError;
            if (GenerateOnce(retry, retryError)) {
                const bool stillBare = std::any_of(retry.castleHistory.begin(), retry.castleHistory.end(), [](const auto& step) {
                    return step.phase == "interior_design" && step.change.rfind("unfurnished", 0) == 0;
                });
                if (!stillBare) outPlan = std::move(retry);
            }
            moreCandidates_ = false;
        }
        for (const auto& step : outPlan.castleHistory)
            if (step.phase == "interior_design" && step.change.rfind("unfurnished: ", 0) == 0)
                std::cerr << "WARNING: castle interior not furnished (" << step.change.substr(13) << ")\n";
        return true;
    }
    return false;
}

bool CastleMapImageGenerator::GenerateOnce(MapPlan& outPlan, std::string& outError) {
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
    double wealth = std::uniform_real_distribution<double>(castle_odds::kWealthMin, castle_odds::kWealthMax)(wealthRng);
    if (const auto it = params_.extra.find("countryWealth"); it != params_.extra.end()) wealth = it->second;
    if (const auto it = params_.extra.find("wealth"); it != params_.extra.end()) wealth = it->second;
    wealth = std::clamp(wealth, 0.7, 2.5);
    // This generator now represents the castle interior and its working
    // grounds. Legacy callers may still send outer-defense settings, but
    // those settings cannot turn this layout back into a walled settlement.
    const double outerWallDefenseStrength = 0.0;
    const bool hasOuterWall = outerWallDefenseStrength >= 0.5;
    // The straight main avenue is laid after the castle is finished (no walls, no gates).
    const bool straightAvenue = !hasOuterWall;
    const bool hasMoat = outerWallDefenseStrength >= 1.5;
    const bool hasOuterDefenseEquipment = outerWallDefenseStrength > 1.1;
    const int wallVariant = std::uniform_int_distribution<int>(0, 2)(programRng);
    const int defenseLevel = 0;
    const int requestedCandidateCount = std::clamp(static_cast<int>(std::lround(GetParam("candidateCount", 6.0))), 1, 12);
    // At the minimum supported size, a few random facility/tower placements
    // can block every route even though another deterministic candidate would
    // fit. Spend the available search budget automatically for that case;
    // explicit candidateCount remains an escape hatch for callers that need a
    // fixed generation budget.
    const bool candidateCountWasExplicit = params_.extra.find("candidateCount") != params_.extra.end();
    const int candidateCount = !candidateCountWasExplicit && (scale < 0.40 || moreCandidates_) ? 12 : requestedCandidateCount;
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
    std::vector<CandidateResult> ranked;
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

    // Nothing is built outside the castle body: no detached service buildings, houses or
    // yards. The editor always sends a facility count range (default 4-12); a castle ignores it.
    facilityMin = 0;
    facilityMax = 0;

    const char* failureStage = "outer fortifications";
    const int candidateOffset=std::clamp(static_cast<int>(GetParam("castleCandidateOffset",0)),0,1000000);
    for (int candidateIndex = 0;
        candidateIndex < candidateCount || (!candidateCountWasExplicit && !generated && candidateIndex < 12);
        ++candidateIndex) {
        std::mt19937 rng(CandidateSeed(params_.seed, candidateOffset+candidateIndex));
        const int moatWidth = hasMoat ? std::uniform_int_distribution<int>(moatMin, moatMax)(rng) : 0;
        const int outerWallThickness = hasOuterWall ? wallThickness : 0;
        const int wallInset = hasMoat ? moatWidth : (hasOuterWall ? std::max(2, wallThickness) : 0);
        const Rect moatRect{0, 0, width, height};
        const Rect wallRect{wallInset, wallInset, width - wallInset, height - wallInset};
        const Rect innerRect{wallRect.x0 + outerWallThickness, wallRect.y0 + outerWallThickness,
            wallRect.x1 - outerWallThickness, wallRect.y1 - outerWallThickness};
        // The castle stays clear of the map edges: its outer wall (the ring around the keep) never comes
        // within the outer 5% of the map on any side. The ring lies one cell outside the keep's own wall,
        // which lies one cell outside the ground the layout may use, so that ground starts two cells in.
        // (The proportions of the keep still follow the whole canvas; only its size and place are limited.)
        const int edgeMarginX = edgeMarginPercent_ == 0 ? 0 : (width * edgeMarginPercent_ + 99) / 100 + 1;
        const int edgeMarginY = edgeMarginPercent_ == 0 ? 0 : (height * edgeMarginPercent_ + 99) / 100 + 1;
        const Rect safeRect{std::max(innerRect.x0, edgeMarginX), std::max(innerRect.y0, edgeMarginY),
            std::min(innerRect.x1, width - edgeMarginX), std::min(innerRect.y1, height - edgeMarginY)};
        if (innerRect.Width() < 24 || innerRect.Height() < 24) continue;
        const int castleDimension = std::min(innerRect.Width(), innerRect.Height());

        MapPlan candidate;
        candidate.Reset(width, height, params_.seed, "castle");
        candidate.archetype = CastleModeName(mode);
        candidate.countryWealth = wealth;
        candidate.wealthClass = WealthClass(wealth);
        candidate.outerWallDefenseStrength = outerWallDefenseStrength;
        candidate.castleWallForm = "castle_ground";
        candidate.castleDefenseLevel = defenseLevel;
        candidate.castleDevelopmentModel = "wealth_and_construction_history_v1";
        candidate.biomes.assign(static_cast<std::size_t>(area), PLAIN);
        std::vector<std::uint8_t>& biomes = candidate.biomes;
        std::vector<std::uint8_t> domain(static_cast<std::size_t>(area), 0);
        std::vector<std::uint8_t> structure(static_cast<std::size_t>(area), 0);
        candidate.castleDefenseMask.assign(area, 0);
        auto& defense = candidate.castleDefenseMask;
        // Straight wall faces meet gently irregular cut-back corners. The
        // ridge constrains the upper ward; a river constrains one flank;
        // formal palaces retain broad, almost rectangular courts.
        const int contourScale = minDimension < 96 && GetParam("castleHumanScale",1)<2 ? 0 : std::min(wallRect.Width(), wallRect.Height());
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
        candidate.castleGroundMask = domain;
        const auto insideDistance = MaskDistance(domain, width, height, false);
        const auto outsideDistance = MaskDistance(domain, width, height, true);
        for (int i = 0; i < area; ++i) {
            if (hasMoat && !domain[i] && outsideDistance[i] <= moatWidth) { biomes[i] = RIVER; defense[i] = 1; }
            if (hasOuterWall && domain[i] && insideDistance[i] <= outerWallThickness) {
                biomes[i] = MOUNTAIN; structure[i] = 1; defense[i] = 2;
            }
        }
        candidate.regions.push_back({"castle_ground", 0, innerRect.x0, innerRect.y0, innerRect.x1 - 1, innerRect.y1 - 1});
        candidate.regions.back().role = "interior_castle_ground";
        if (hasMoat) {
            candidate.regions.push_back({"moat", 0, moatRect.x0, moatRect.y0, moatRect.x1 - 1, moatRect.y1 - 1});
            candidate.regions.back().role = "moat";
        }
        if (hasOuterWall) {
            candidate.regions.push_back({"curtain_wall", 0, wallRect.x0, wallRect.y0, wallRect.x1 - 1, wallRect.y1 - 1});
            candidate.regions.back().role = "curtain_wall";
        }
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

        // A river castle's water is laid after the buildings (see below), so that
        // it follows their outline instead of a fixed strip.

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
            // Without an outer wall there is no gate to build here: the main
            // avenue is laid straight from the finished keep's door later.
            if (straightAvenue) continue;
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
        const int baseKeepWidth = std::clamp(static_cast<int>(std::lround(GetParam("keepWidth", innerRect.Width() * compactRatio))),
            16, std::max(16, innerRect.Width() - 22));
        const int baseKeepHeight = std::clamp(static_cast<int>(std::lround(GetParam("keepHeight", innerRect.Height() *
            (minDimension < 112 ? 0.40 : program.keepDepth)))), 16, std::max(16, innerRect.Height() - 22));
        // Enlarge the rooms, not the masonry. Leave an outdoor access strip
        // inside the curtain even when the requested keep exceeds the canvas.
        const bool keepScaleExplicit = params_.extra.find("keepScale") != params_.extra.end();
        const double keepScale = std::clamp(GetParam("keepScale", 1.5),
            1.0, 1.5);
        int keepWidth = std::min(static_cast<int>(std::lround(baseKeepWidth * keepScale)),
            std::max(16, innerRect.Width() - 10));
        int keepHeight = std::min(static_cast<int>(std::lround(baseKeepHeight * keepScale)),
            std::max(16, innerRect.Height() - 10));
        const bool fullCanvasKeep = !keepScaleExplicit && minDimension >= 112;
        // Terrain is independent of the castle type. Water is one of the terrains: when it is
        // drawn, one or more sides (0 left, 1 right, 2 top) become water from the map edge
        // to the castle's outer wall, and the castle is moved away from them to leave room.
        // One to three of the left, right and top sides get nature (never the gate side).
        int natureSides = 0;
        std::uint8_t natureBiome = PLAIN;
        bool desertGround = false;  // the desert's ground is desert (painted at the end)
        {
            // exteriorTerrain: -1 (default) draws forest, mountain, desert or water; 0 plain (no
            // nature); 1 desert; 2 forest; 3 mountain; 4 water. Independent of the castle type.
            int requested = static_cast<int>(std::lround(GetParam("exteriorTerrain", -1)));
            if (requested < 0) {
                static const int drawn[4] = {2, 3, 1, 4};
                requested = drawn[std::uniform_int_distribution<int>(0, 3)(rng)];
            }
            natureBiome = ExteriorTerrainBiome(requested);
            desertGround = requested == 1;
        }
        if (natureBiome != PLAIN) {
            std::array<int, 3> order{{0, 1, 2}};
            std::shuffle(order.begin(), order.end(), rng);
            // natureSideCount: how many of the left / right / top sides get nature, decided
            // before the terrain fills them. -1 (default) draws 0..3; 0 leaves every side
            // plain, so the terrain has no effect. Water always keeps at least one side.
            int count = static_cast<int>(std::lround(GetParam("natureSideCount", -1)));
            const int drawnCount = std::uniform_int_distribution<int>(0, 3)(rng);
            if (count < 0) count = drawnCount;
            count = std::min(count, 3);
            if (natureBiome == RIVER) count = std::max(count, 1);
            for (int i = 0; i < count; ++i) natureSides |= 1 << order[static_cast<std::size_t>(i)];
        }
        const bool natureLeft = (natureSides & 1) != 0, natureRight = (natureSides & 2) != 0, natureTop = (natureSides & 4) != 0;
        if (fullCanvasKeep) {
            // Default castle: the keep takes the whole ground the edge margin leaves (the clamp below holds it
            // inside the margin). A nature side at the top leaves room for it: the keep stays shorter there.
            keepWidth = std::max(keepWidth, innerRect.Width());
            keepHeight = std::max(keepHeight, natureTop ? static_cast<int>(std::lround(innerRect.Height() * 0.74)) : innerRect.Height());
            // Nature on the left or right needs a wide flank: the keep is narrower.
            if (natureLeft && natureRight) keepWidth = static_cast<int>(std::lround(innerRect.Width() * 0.72));
            else if (natureLeft || natureRight) keepWidth = static_cast<int>(std::lround(innerRect.Width() * 0.84));
        }
        const int riverShift = std::max(4, innerRect.Width() * 8 / 100);
        const int keepCenterX = centerX + (natureLeft && !natureRight ? riverShift : natureRight && !natureLeft ? -riverShift : 0);
        const int keepNorthMargin = (natureTop ? std::max(fullCanvasKeep ? 4 : 10, innerRect.Height() * 14 / 100) :
            fullCanvasKeep ? 4 : std::max(10, innerRect.Height() / (mode == CastleMode::Hill ? 6 : 8)));
        // The keep fits inside the margin-clear ground (the water or the nature on a side may come out
        // narrower than before: the castle is not squeezed to keep it wide).
        keepWidth = std::min(keepWidth, std::max(16, safeRect.Width() - 4));
        keepHeight = std::min(keepHeight, std::max(16, safeRect.Height() - 4));
        const int keepY = innerRect.y0 + keepNorthMargin + keepHeight / 2;
        const Rect keep{
            std::clamp(keepCenterX - keepWidth / 2, safeRect.x0 + 2, std::max(safeRect.x0 + 2, safeRect.x1 - keepWidth - 2)),
            std::clamp(keepY - keepHeight / 2, safeRect.y0 + 2, std::max(safeRect.y0 + 2, safeRect.y1 - keepHeight - 2)),
            0,
            0,
        };
        Rect keepFixed = keep;
        keepFixed.x1 = keepFixed.x0 + keepWidth;
        keepFixed.y1 = keepFixed.y0 + keepHeight;
        int wardGap = std::clamp(castleDimension / 20, 4, 20);
        Rect innerWard = keepFixed;
        bool hasInnerWard = false;
        // The castle itself fills the map; no enclosing inner-ward wall is drawn.
        constexpr bool kAllowInnerWard = false;
        for (int candidateGap = wardGap; kAllowInnerWard && candidateGap >= 4 && minDimension >= 112; --candidateGap) {
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
        failureStage = "fitting the requested towers beside the keep";
        if (static_cast<int>(towerApproaches.size()) < towerMin) continue;

        // Every wall in the castle is exactly one cell thick: neighbouring rooms share
        // a single partition and the outline is one cell as well, so the rooms reach
        // one cell closer to the edge. (There is no option for double wall rings.)
        constexpr bool sharedKeepWalls = true;
        const int keepInset = sharedKeepWalls ? 1 : 2;
        const Rect keepInner{keepFixed.x0 + keepInset, keepFixed.y0 + keepInset,
            keepFixed.x1 - keepInset, keepFixed.y1 - keepInset};
        std::vector<std::vector<RoomInfo>> layouts;
        std::vector<int> layoutEntries;  // the room that faces the gate, per layout
        // One draw per candidate drives the silhouette and the room draw, so every
        // room count of this candidate shares one castle shape. An explicit room
        // count is honoured exactly: the size limit only splits rooms when the
        // count was left to the generator.
        const unsigned layoutVariation = static_cast<unsigned>(rng());
        for (int count = roomMin; count <= roomMax; ++count) {
            int layoutEntry = 1;
            auto layout = PlanKeepRooms(mode, program, keepInner, count, layoutEntry,
                layoutVariation, !roomRangeWasExplicit, natureSides);
            if (!layout.empty()) { layouts.push_back(std::move(layout)); layoutEntries.push_back(layoutEntry); }
        }
        failureStage = "fitting the requested keep rooms";
        if (layouts.empty()) continue;
        const int layoutChoice = std::uniform_int_distribution<int>(0, static_cast<int>(layouts.size()) - 1)(rng);
        std::vector<RoomInfo> rooms = layouts[static_cast<std::size_t>(layoutChoice)];
        // The room that faces the gate: the entrance hall, or the hall that stands in for it.
        const int entryRoomIndex = layoutEntries[static_cast<std::size_t>(layoutChoice)];
        SplitThinRooms(rooms, mode, static_cast<unsigned>(rng()), entryRoomIndex);
        const std::vector<CorridorLink> corridorLinks = AddKeepCorridors(rooms, mode, wealth, rng, entryRoomIndex);
        candidate.regions.push_back({"keep", 0, keepFixed.x0, keepFixed.y0, keepFixed.x1 - 1, keepFixed.y1 - 1});
        candidate.regions.back().role = "keep";
        candidate.regions.push_back({"keep_layout", 0, keepFixed.x0, keepFixed.y0, keepFixed.x1 - 1, keepFixed.y1 - 1});
        candidate.regions.back().role = program.layout;
        std::vector<int> roomOwner(sharedKeepWalls ? area : 0, -1);
        if (sharedKeepWalls) for (const auto& room : rooms)
            for (int y = room.rect.y0; y < room.rect.y1; ++y)
                for (int x = room.rect.x0; x < room.rect.x1; ++x) roomOwner[Index(x,y,width)] = room.regionId;
        // Preserve the outline and open courts. Adjacent rooms share a single
        // partition on the lower/right room's edge, instead of two wall rings.
        for (const auto& room : rooms) {
            const int shellWidth = sharedKeepWalls ? 1 : 2;
            const Rect shell{room.rect.x0 - shellWidth, room.rect.y0 - shellWidth,
                room.rect.x1 + shellWidth, room.rect.y1 + shellWidth};
            PaintRect(biomes, width, height, shell, EXMOUNTAIN);
            for (int y = shell.y0; y < shell.y1; ++y) for (int x = shell.x0; x < shell.x1; ++x) structure[Index(x, y, width)] = 1;
        }
        for (auto& room : rooms) {
            const bool wood = (room.role.find("chamber") != std::string::npos && room.role != "antechamber") || room.role.find("quarters") != std::string::npos ||
                room.role == "guard_hall" || room.role == "guest_apartment";
            PaintRect(biomes, width, height, room.rect, static_cast<std::uint8_t>(wood ? TOWN_WOOD_FLOOR : TOWN_STONE_FLOOR));
            candidate.regions.push_back({"room", room.regionId, room.rect.x0, room.rect.y0, room.rect.x1 - 1, room.rect.y1 - 1});
            candidate.regions.back().role = room.role;
            candidate.markers.push_back({"landmark", room.rect.CenterX(), room.rect.CenterY(), room.regionId});
            candidate.markers.back().role = room.role;
        }
        if (sharedKeepWalls) for (const auto& room : rooms) {
            for (int y = room.rect.y0; y < room.rect.y1; ++y) for (int x = room.rect.x0; x < room.rect.x1; ++x) {
                if ((x == room.rect.x0 && roomOwner[Index(x-1,y,width)] >= 0) ||
                    (y == room.rect.y0 && roomOwner[Index(x,y-1,width)] >= 0))
                    biomes[Index(x,y,width)] = MOUNTAIN;
            }
        }
        // Where two walls meet only diagonally (a wall line that steps by one cell where a
        // room ends), the corner is open: close it with one cell of wall. The cell comes
        // from the room floor beside the step, so the wall line stays continuous.
        auto closeWallCorners = [&]() {
            const auto wallAt = [&](int x, int y) {
                if (!InBounds(x, y, width, height)) return false;
                const std::uint8_t b = biomes[static_cast<std::size_t>(Index(x, y, width))];
                return b == MOUNTAIN || b == EXMOUNTAIN;
            };
            const auto floorAt = [&](int x, int y) {
                if (!InBounds(x, y, width, height)) return false;
                const std::uint8_t b = biomes[static_cast<std::size_t>(Index(x, y, width))];
                return b == TOWN_FLOOR || b == TOWN_WOOD_FLOOR || b == TOWN_STONE_FLOOR;
            };
            for (int pass = 0, changed = 1; pass < 6 && changed; ++pass) {
                changed = 0;
                for (int y = 0; y + 1 < height; ++y) for (int x = 0; x + 1 < width; ++x) {
                    const bool a = wallAt(x, y), b = wallAt(x + 1, y), c = wallAt(x, y + 1), d = wallAt(x + 1, y + 1);
                    Point first{0, 0}, second{0, 0};
                    if (a && d && !b && !c) { first = {x + 1, y}; second = {x, y + 1}; }
                    else if (b && c && !a && !d) { first = {x, y}; second = {x + 1, y + 1}; }
                    else continue;
                    for (const Point cell : {first, second}) {
                        if (!floorAt(cell.x, cell.y)) continue;
                        const int index = Index(cell.x, cell.y, width);
                        biomes[static_cast<std::size_t>(index)] = MOUNTAIN;
                        structure[static_cast<std::size_t>(index)] = 1;
                        changed = 1;
                        break;
                    }
                }
            }
        };
        closeWallCorners();
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
        // A room door is the editor's native door asset (4 wide, 1 deep by default), set into
        // the single-cell partition. Each opening is therefore as wide as that asset.
        const int doorWidth = std::max({4, castle_interior::Native(params_, 1, 8, 4, 1, 4).w,
            castle_interior::Native(params_, 1, 13, 4, 1, 4).w});
        struct RoomEdge { int a; int b; Point doorA; Point doorB; int priority; bool vertical; int plane; int lo; int hi; int width; };
        std::vector<RoomEdge> roomEdges;
        for (int a = 0; a < static_cast<int>(rooms.size()); ++a) for (int b = a + 1; b < static_cast<int>(rooms.size()); ++b) {
            const auto& ar = rooms[a].rect; const auto& br = rooms[b].rect;
            const int y0 = std::max(ar.y0, br.y0) + 1, y1 = std::min(ar.y1, br.y1) - 2;
            const int x0 = std::max(ar.x0, br.x0) + 1, x1 = std::min(ar.x1, br.x1) - 2;
            const int priority = a < 2 || b < 2 ? 0 : 1;
            // The opening is as wide as the door asset where the shared partition allows, and
            // as wide as it can be otherwise (that room is then connected but cannot be
            // furnished with the native door, so such an edge is only a fallback).
            const int spanY = std::min(ar.y1, br.y1) - std::max(ar.y0, br.y0) - 2;  // usable rows
            const int spanX = std::min(ar.x1, br.x1) - std::max(ar.x0, br.x0) - 2;
            const int widthY = std::min(doorWidth, spanY), widthX = std::min(doorWidth, spanX);
            const int loY = y0, hiY = y0 + spanY - widthY, loX = x0, hiX = x0 + spanX - widthX;
            const int pY = priority + (widthY < doorWidth ? 2 : 0), pX = priority + (widthX < doorWidth ? 2 : 0);
            if (y0 <= y1 && ar.x1 == br.x0) roomEdges.push_back({a,b,{ar.x1-1,(y0+y1)/2},{br.x0,(y0+y1)/2},pY,true,br.x0,loY,hiY,widthY});
            if (y0 <= y1 && br.x1 == ar.x0) roomEdges.push_back({a,b,{ar.x0,(y0+y1)/2},{br.x1-1,(y0+y1)/2},pY,true,ar.x0,loY,hiY,widthY});
            if (x0 <= x1 && ar.y1 == br.y0) roomEdges.push_back({a,b,{(x0+x1)/2,ar.y1-1},{(x0+x1)/2,br.y0},pX,false,br.y0,loX,hiX,widthX});
            if (x0 <= x1 && br.y1 == ar.y0) roomEdges.push_back({a,b,{(x0+x1)/2,ar.y0},{(x0+x1)/2,br.y1-1},pX,false,ar.y0,loX,hiX,widthX});
        }
        const std::vector<RoomEdge> allRoomEdges = roomEdges;  // every shared wall, before the corridor rules below take some away
        // Corridors: a corridor opens only into the rooms it serves and its allowed central rooms
        // (never the throne room or the private suite); served rooms open onto the corridor
        // only, not to each other or to the central rooms.
        if (!corridorLinks.empty()) {
            const auto isCorridor = [&](int index) { return rooms[static_cast<std::size_t>(index)].role == "corridor"; };
            const auto linkOf = [&](int index) -> const CorridorLink* {
                for (const CorridorLink& link : corridorLinks) if (link.id == index) return &link;
                return nullptr;
            };
            const auto servedBy = [&](int index) -> const CorridorLink* {
                for (const CorridorLink& link : corridorLinks)
                    if (std::find(link.served.begin(), link.served.end(), index) != link.served.end()) return &link;
                return nullptr;
            };
            roomEdges.erase(std::remove_if(roomEdges.begin(), roomEdges.end(), [&](const RoomEdge& edge) {
                for (const int corridor : {edge.a, edge.b}) {
                    if (!isCorridor(corridor)) continue;
                    const int other = corridor == edge.a ? edge.b : edge.a;
                    const CorridorLink* link = linkOf(corridor);
                    const bool open = std::find(link->served.begin(), link->served.end(), other) != link->served.end() ||
                        std::find(link->allowed.begin(), link->allowed.end(), other) != link->allowed.end();
                    return !open;
                }
                const CorridorLink* linkA = servedBy(edge.a);
                const CorridorLink* linkB = servedBy(edge.b);
                if (linkA != nullptr && linkB != nullptr && linkA == linkB) return true;  // two rooms beside one corridor
                // A served room does not open straight into the central block.
                const auto central = [&](int index) { return index == entryRoomIndex || rooms[static_cast<std::size_t>(index)].role == "antechamber" ||
                    rooms[static_cast<std::size_t>(index)].role == "throne_room" || IsCastlePrivateRole(rooms[static_cast<std::size_t>(index)].role); };
                return (linkA != nullptr && central(edge.b)) || (linkB != nullptr && central(edge.a));
            }), roomEdges.end());
            for (RoomEdge& edge : roomEdges) if (isCorridor(edge.a) || isCorridor(edge.b)) edge.priority = edge.priority >= 2 ? 2 : 0;
        }
        // The rooms that stand beside the throne room (its side rooms, one to three on a side) do not all open onto its wall:
        // the largest of them on each side does, and the others open onto it (their neighbours along the wall). Door
        // landings on the throne room's side walls would otherwise leave no way through the room.
        {
            int throneIndex = -1;
            for (int i = 0; i < static_cast<int>(rooms.size()); ++i) if (rooms[static_cast<std::size_t>(i)].role == "throne_room") throneIndex = i;
            if (throneIndex >= 0) {
                const Rect t = rooms[static_cast<std::size_t>(throneIndex)].rect;
                const auto sideRoom = [&](int i) {
                    const RoomInfo& room = rooms[static_cast<std::size_t>(i)];
                    return i != throneIndex && room.role != "antechamber" && room.role != "corridor" && !IsCastlePrivateRole(room.role) &&
                        !IsCastleEntryRole(room.role) && room.rect.y0 >= t.y0 - 1 && room.rect.y1 <= t.y1 + 1 && (room.rect.x1 == t.x0 || room.rect.x0 == t.x1);
                };
                for (const bool left : {true, false}) {
                    int keep = -1;
                    int count = 0;
                    for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
                        if (!sideRoom(i) || (rooms[static_cast<std::size_t>(i)].rect.x1 == t.x0) != left) continue;
                        ++count;
                        const Rect& r = rooms[static_cast<std::size_t>(i)].rect;
                        if (keep < 0 || r.Width() * r.Height() > rooms[static_cast<std::size_t>(keep)].rect.Width() * rooms[static_cast<std::size_t>(keep)].rect.Height()) keep = i;
                    }
                    // (A narrow side room, under 10 across, keeps its own door onto the throne room: a chain of doors through such
                    // a room leaves its door landings no way through.)
                    if (count < 2 || rooms[static_cast<std::size_t>(keep)].rect.Width() < 10) continue;
                    roomEdges.erase(std::remove_if(roomEdges.begin(), roomEdges.end(), [&](const RoomEdge& edge) {
                        const int other = edge.a == throneIndex ? edge.b : edge.b == throneIndex ? edge.a : -1;
                        return other >= 0 && other != keep && sideRoom(other) && (rooms[static_cast<std::size_t>(other)].rect.x1 == t.x0) == left;
                    }), roomEdges.end());
                }
            }
        }
        int roomDoorId = 0;
        // Open the partition over the door's full width and record it for the interior pass.
        std::set<std::pair<int, int>> doorPairs;  // room pairs already joined by a door or an opened wall
        auto openPartition = [&](const RoomEdge& edge) {
            doorPairs.insert({std::min(edge.a, edge.b), std::max(edge.a, edge.b)});
            const int center = edge.vertical ? edge.doorA.y : edge.doorA.x;
            const int start = std::clamp(center - (edge.width - 1) / 2, edge.lo, std::max(edge.lo, edge.hi));
            for (int k = 0; k < edge.width; ++k) openRoomCell(edge.vertical ? Point{edge.plane, start + k} : Point{start + k, edge.plane});
            MapPlanRegion door{"room_connection", roomDoorId++, edge.vertical ? edge.plane : start, edge.vertical ? start : edge.plane,
                edge.vertical ? edge.plane : start + edge.width - 1, edge.vertical ? start + edge.width - 1 : edge.plane};
            door.role = "shared_wall_door";
            door.rotation = edge.vertical ? 1 : 0;
            door.parentId = edge.a;
            door.linkedRegionId = edge.b;
            for (int i = 0; i < static_cast<int>(candidate.regions.size()); ++i)
                if (candidate.regions[i].kind == "room" && candidate.regions[i].id == edge.a) door.parentIndex = i;
            candidate.regions.push_back(door);
        };
        // Two corridors that meet (a vertical and a horizontal one in the same wing) merge: the wall
        // between them is opened over the floor width of the one that ends there.
        auto openJunction = [&](const RoomEdge& edge) {
            doorPairs.insert({std::min(edge.a, edge.b), std::max(edge.a, edge.b)});
            const Rect& ra = rooms[static_cast<std::size_t>(edge.a)].rect;
            const Rect& rb = rooms[static_cast<std::size_t>(edge.b)].rect;
            const int lo = (edge.vertical ? std::max(ra.y0, rb.y0) : std::max(ra.x0, rb.x0)) + 1;
            const int hi = (edge.vertical ? std::min(ra.y1, rb.y1) : std::min(ra.x1, rb.x1)) - 1;
            for (int k = lo; k <= hi; ++k) {
                const Point p = edge.vertical ? Point{edge.plane, k} : Point{k, edge.plane};
                biomes[static_cast<std::size_t>(Index(p.x, p.y, width))] = TOWN_STONE_FLOOR;
            }
            MapPlanRegion junction{"corridor_junction", roomDoorId++, edge.vertical ? edge.plane : lo, edge.vertical ? lo : edge.plane,
                edge.vertical ? edge.plane : hi, edge.vertical ? hi : edge.plane};
            junction.role = "open_wall";
            junction.rotation = edge.vertical ? 1 : 0;
            junction.parentId = edge.a;
            junction.linkedRegionId = edge.b;
            candidate.regions.push_back(junction);
        };
        // The private suite is entered beside the dais, never through the wall
        // directly behind the throne: the bedchamber on the left and the study on
        // the right open onto the throne room; the dressing room behind the throne
        // is reached only through its neighbours.
        // The x of the door between the throne room and a suite room behind it (across a horizontal wall): near the
        // corner on the side the suite room lies on (so that a narrow throne room keeps the floor in front of its
        // throne clear of the doors' landings), and clear of the middle of the throne room. -1: no such door, or no
        // place for it (a suite room that lies behind the middle of the throne room is reached through its neighbours).
        const auto suiteDoorX = [&](const RoomEdge& edge) {
            const bool aThrone = rooms[edge.a].role == "throne_room", bThrone = rooms[edge.b].role == "throne_room";
            if (aThrone == bThrone || edge.vertical) return -1;
            const std::string& other = aThrone ? rooms[edge.b].role : rooms[edge.a].role;
            if (other != "royal_bedchamber" && other != "royal_study" && other != "queens_bedchamber") return -1;
            const auto& tr = aThrone ? rooms[edge.a].rect : rooms[edge.b].rect;
            const auto& pr = aThrone ? rooms[edge.b].rect : rooms[edge.a].rect;
            const int overlapLo = std::max(tr.x0, pr.x0), overlapHi = std::min(tr.x1, pr.x1);
            const int centre = (tr.x0 + tr.x1) / 2;
            const bool leftOfThrone = (pr.x0 + pr.x1) < (tr.x0 + tr.x1);
            const int leftX = overlapLo + 1, rightX = std::max(leftX, overlapHi - 5);
            const auto clear = [&](int x) { return x + 3 <= centre - 3 || x >= centre + 3; };
            const int first = leftOfThrone ? leftX : rightX, second = leftOfThrone ? rightX : leftX;
            return clear(first) ? first : clear(second) ? second : -1;
        };
        // The dressing room behind the throne is reached only through its neighbours.
        roomEdges.erase(std::remove_if(roomEdges.begin(), roomEdges.end(), [&](const RoomEdge& edge) {
            const bool aThrone = rooms[edge.a].role == "throne_room", bThrone = rooms[edge.b].role == "throne_room";
            const std::string& other = aThrone ? rooms[edge.b].role : rooms[edge.a].role;
            if ((aThrone != bThrone) && (other == "royal_dressing_room" || other == "royal_children_chamber" || other == "royal_treasury")) return true;
            if ((aThrone != bThrone) && !edge.vertical && (other == "royal_bedchamber" || other == "royal_study" || other == "queens_bedchamber"))
                return suiteDoorX(edge) < 0;
            return false;
        }), roomEdges.end());
        for (auto& edge : roomEdges) {
            const int sideX = suiteDoorX(edge);
            if (sideX >= 0) edge.doorA.x = edge.doorB.x = sideX;
        }
        // The way from the main gate to the throne runs straight: the door between the entrance hall and the
        // antechamber is made first, so the throne room is reached through it and not round the side
        // (through a chapel or a council room beside the throne room).
        for (RoomEdge& edge : roomEdges) {
            const bool hallAnte = (edge.a == entryRoomIndex && rooms[static_cast<std::size_t>(edge.b)].role == "antechamber") ||
                (edge.b == entryRoomIndex && rooms[static_cast<std::size_t>(edge.a)].role == "antechamber");
            // ... and the antechamber opens into the throne room across its width, not round the side.
            const bool anteThrone = (rooms[static_cast<std::size_t>(edge.a)].role == "antechamber" && rooms[static_cast<std::size_t>(edge.b)].role == "throne_room") ||
                (rooms[static_cast<std::size_t>(edge.b)].role == "antechamber" && rooms[static_cast<std::size_t>(edge.a)].role == "throne_room");
            if ((hallAnte || anteThrone) && edge.priority < 2) edge.priority = -1;
        }
        std::stable_sort(roomEdges.begin(), roomEdges.end(), [](const RoomEdge& a, const RoomEdge& b) { return a.priority < b.priority; });
        std::vector<bool> roomConnected(rooms.size(), false);
        roomConnected[entryRoomIndex] = true;
        int roomConnections = 1;
        while (roomConnections < static_cast<int>(rooms.size())) {
            bool found = false;
            for (const auto& edge : roomEdges) {
                if (roomConnected[edge.a] == roomConnected[edge.b]) continue;
                if (rooms[static_cast<std::size_t>(edge.a)].role == "corridor" && rooms[static_cast<std::size_t>(edge.b)].role == "corridor") {
                    openJunction(edge);  // two corridors of one wing merge: an opened wall, no door
                } else {
                    openPartition(edge);
                    if (roomConnected[edge.a]) openRoomDoor(edge.doorB, edge.b);
                    else openRoomDoor(edge.doorA, edge.a);
                }
                roomConnected[edge.a] = roomConnected[edge.b] = true;
                ++roomConnections; found = true; break;
            }
            if (!found) break;
        }
        failureStage = "connecting keep rooms";
        if (roomConnections != static_cast<int>(rooms.size())) continue;
        // The tree above joins every room once. A corridor is more than that: every room beside it
        // opens onto it, it opens into the central rooms it is allowed to, and two corridors that
        // meet merge. Add the doors (and the opened walls) the tree did not need.
        for (const CorridorLink& link : corridorLinks) {
            std::vector<int> partners = link.served;
            partners.insert(partners.end(), link.allowed.begin(), link.allowed.end());
            for (const int other : partners) {
                const std::pair<int, int> pair{std::min(link.id, other), std::max(link.id, other)};
                if (doorPairs.count(pair) != 0) continue;
                for (const RoomEdge& edge : roomEdges) {
                    if (std::min(edge.a, edge.b) != pair.first || std::max(edge.a, edge.b) != pair.second) continue;
                    if (rooms[static_cast<std::size_t>(other)].role == "corridor") {
                        openJunction(edge);
                    } else {
                        openPartition(edge);
                        // The door marker goes on the room's side, not the corridor's.
                        if (edge.a == other) openRoomDoor(edge.doorA, edge.a); else openRoomDoor(edge.doorB, edge.b);
                    }
                    break;
                }
            }
        }
        // Every corridor has at least three doors. Where the rooms it serves and the central rooms it opens into give fewer,
        // the other rooms that touch it (along its sides or at its ends) open onto it too: the widest shared wall first.
        // Never the throne room, the private suite or another corridor.
        for (const CorridorLink& link : corridorLinks) {
            const auto doorsOf = [&]() {  // doors (not rooms): a long wall may carry two
                int count = 0;
                for (const MapPlanRegion& region : candidate.regions)
                    count += region.kind == "room_connection" && (region.parentId == link.id || region.linkedRegionId == link.id);
                return count;
            };
            std::vector<const RoomEdge*> extra;
            for (const RoomEdge& edge : allRoomEdges) {
                if (edge.a != link.id && edge.b != link.id) continue;
                const int other = edge.a == link.id ? edge.b : edge.a;
                const std::string& role = rooms[static_cast<std::size_t>(other)].role;
                if (role == "corridor" || role == "throne_room" || IsCastlePrivateRole(role) || edge.width < 4) continue;
                if (doorPairs.count({std::min(link.id, other), std::max(link.id, other)}) != 0) continue;
                extra.push_back(&edge);
            }
            std::stable_sort(extra.begin(), extra.end(), [](const RoomEdge* l, const RoomEdge* r) {
                const bool lFull = l->priority < 2, rFull = r->priority < 2;
                return lFull != rFull ? lFull : l->width > r->width;
            });
            for (const RoomEdge* edge : extra) {
                if (doorsOf() >= castle_odds::kCorridorMinDoors) break;
                const int other = edge->a == link.id ? edge->b : edge->a;
                if (doorPairs.count({std::min(link.id, other), std::max(link.id, other)}) != 0) continue;
                openPartition(*edge);
                if (edge->a == other) openRoomDoor(edge->doorA, edge->a); else openRoomDoor(edge->doorB, edge->b);
            }
            // Still short (a corridor with few neighbours): a long shared wall takes a second door, as far from the first as it goes.
            for (const RoomEdge& edge : allRoomEdges) {
                if (doorsOf() >= castle_odds::kCorridorMinDoors) break;
                if (edge.a != link.id && edge.b != link.id) continue;
                const int other = edge.a == link.id ? edge.b : edge.a;
                const std::string& role = rooms[static_cast<std::size_t>(other)].role;
                if (role == "corridor" || role == "throne_room" || IsCastlePrivateRole(role) || edge.width < 4) continue;
                int firstStart = -1;
                for (const MapPlanRegion& region : candidate.regions) {
                    if (region.kind != "room_connection") continue;
                    if (!((region.parentId == edge.a && region.linkedRegionId == edge.b) || (region.parentId == edge.b && region.linkedRegionId == edge.a))) continue;
                    firstStart = edge.vertical ? region.y0 : region.x0;
                    break;
                }
                if (firstStart < 0) continue;
                int best = -1;
                for (const int candidateStart : {edge.lo, edge.hi})
                    if (std::abs(candidateStart - firstStart) >= edge.width + 2 && (best < 0 || std::abs(candidateStart - firstStart) > std::abs(best - firstStart))) best = candidateStart;
                if (best < 0) continue;
                RoomEdge second = edge;
                const int center = best + (edge.width - 1) / 2;
                if (second.vertical) second.doorA.y = second.doorB.y = center; else second.doorA.x = second.doorB.x = center;
                openPartition(second);
                if (edge.a == other) openRoomDoor(second.doorA, second.a); else openRoomDoor(second.doorB, second.b);
            }
        }
        const RoomInfo& entryRoom = rooms[entryRoomIndex];
        const Point keepDoor{entryRoom.rect.CenterX(), entryRoom.rect.y1 + 1};
        openRoomDoor({entryRoom.rect.CenterX(), entryRoom.rect.y1 - 1}, entryRoom.regionId);
        openRoomCell({keepDoor.x, keepDoor.y - 1});
        openRoomCell(keepDoor);
        if (sharedKeepWalls) {
            // The main gate is a grand seven cells wide.
            const int gateHalf = 3;
            for (int y = entryRoom.rect.y1 - 1; y <= keepDoor.y; ++y)
                for (int x = keepDoor.x - gateHalf; x <= keepDoor.x + gateHalf; ++x) openRoomCell({x,y});
            candidate.regions.push_back({"castle_front_door",0,keepDoor.x-gateHalf,entryRoom.rect.y1,keepDoor.x+gateHalf,entryRoom.rect.y1});
            candidate.regions.back().role = "great_castle_gate";
        }
        candidate.markers.push_back({"service_entry", keepDoor.x, keepDoor.y, 0});
        candidate.markers.back().role = "keep_public_entry";
        // A palace's entrance opens into the court between its long wings;
        // the river house opens beside a working recess in its L-shaped body.
        if (mode == CastleMode::Palace) {  // (a river castle has water beside it, not a loading court)
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
        // Keep wings are part of the castle building, so they do not need an
        // inner ward. They only have to stay inside the castle ground.
        const Rect wingBounds = hasInnerWard ? innerWard : safeRect;
        const int keepAnnexCount = wealth >= castle_odds::kKeepAnnexWealthTwo ? 2 : (wealth >= castle_odds::kKeepAnnexWealthOne ? 1 : 0);
        for (int annexIndex = 0; annexIndex < keepAnnexCount; ++annexIndex) {
            const int side = annexIndex == 0 ? 1 : -1;
            // No wing on the water side: it would squeeze the water.
            if ((natureRight && side > 0) || (natureLeft && side < 0)) continue;
            const int annexDepth = std::min(std::max(4, castleDimension / 26), wardGap - 2);
            if (annexDepth < 4) continue;
            const int probeX = side > 0 ? keepFixed.x1 - 4 : keepFixed.x0 + 3;
            const auto floorAtProbe = [&](int y) {
                const std::uint8_t tile = biomes[static_cast<std::size_t>(Index(probeX, y, width))];
                return tile == TOWN_FLOOR || tile == TOWN_WOOD_FLOOR || tile == TOWN_STONE_FLOOR;
            };
            // The wing is centred on the wall it is attached to: the middle of the rows
            // where this side of the keep has rooms (not on the door, which may sit off-centre).
            // Only a run of rows that stays flush against the edge counts: where the face steps
            // back (a stepped wing) the wing would float with a thick gap behind it. The longest
            // such run is used.
            int wallFirst = keepFixed.y1, wallLast = keepFixed.y0, bestLength = 0;
            for (int y = keepFixed.y0; y < keepFixed.y1;) {
                if (!floorAtProbe(y)) { ++y; continue; }
                int end = y;
                while (end + 1 < keepFixed.y1 && floorAtProbe(end + 1)) ++end;
                if (end - y + 1 > bestLength) { bestLength = end - y + 1; wallFirst = y; wallLast = end; }
                y = end + 1;
            }
            if (wallLast < wallFirst || bestLength < 10) continue;
            const int wallMiddle = (wallFirst + wallLast) / 2;
            // The wing stays inside the flush run, one row in from each end.
            const int annexHeight = std::min(std::clamp(keepFixed.Height() / 3, 10, keepFixed.Height() - 6), bestLength - 2);
            if (annexHeight < 10) continue;
            const int annexY = std::clamp(wallMiddle - annexHeight / 2, wallFirst + 1, wallLast - annexHeight);
            if (annexY < keepFixed.y0 + 2 || annexY + annexHeight > keepFixed.y1 - 2) continue;
            // The door joins the wing to the room nearest the middle of the wall, inside the wing's rows.
            int connectionY = -1;
            for (int offset = 0; offset < annexHeight && connectionY < 0; ++offset) {
                for (const int y : {wallMiddle - offset, wallMiddle + offset}) {
                    if (y <= annexY + 1 || y >= annexY + annexHeight - 2 || !floorAtProbe(y)) continue;
                    connectionY = y;
                    break;
                }
            }
            if (connectionY < 0) continue;
            const Rect annex = side > 0
                ? Rect{keepFixed.x1 - 1, annexY, keepFixed.x1 + annexDepth, annexY + annexHeight}
                : Rect{keepFixed.x0 - annexDepth, annexY, keepFixed.x0 + 1, annexY + annexHeight};
            bool clear = annex.x0 > wingBounds.x0 + 1 && annex.y0 > wingBounds.y0 + 1 &&
                annex.x1 < wingBounds.x1 - 1 && annex.y1 < wingBounds.y1 - 1;
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
            candidate.regions.push_back({"keep_expansion", regionId, annex.x0, annex.y0, annex.x1 - 1, annex.y1 - 1});
            candidate.regions.back().role = spec.role;
            candidate.regions.back().linkedRegionId = 0;
            candidate.markers.push_back({"landmark", annex.CenterX(), annex.CenterY(), regionId});
            candidate.markers.back().role = spec.role;
        }
        // Side (service) doors: a working room whose outer wall faces open ground gets a door
        // there, so the castle can be entered and served without the main gate. The door goes in
        // the room's own wall (never a ceremonial room, never a side that faces nature, never in
        // front of the gate), opens the outer ring as well, and has no road of its own. One door, or two on a wide keep.
        std::vector<std::uint8_t> doorFront(static_cast<std::size_t>(area), 0);  // keep street trees off the doorsteps
        {
            struct SideOption { int rank; int room; int side; int start; int distance; };
            const auto rankOf = [](const std::string& role) {
                // Never the throne room, the antechamber, the private suite behind the throne, or a corridor.
                if (role == "throne_room" || role == "antechamber" || IsCastlePrivateRole(role) || role == "corridor") return -1;
                // The entrance hall is the very last choice: a side door into it only where the lot has
                // no other room facing it (a bare strip along the central block).
                if (role == "entrance_hall") return 4;
                // Bedrooms, chapels and council rooms need their wall space: they get a side door only
                // when nothing else faces an empty lot (the last choice).
                if (role.find("council") != std::string::npos || role.find("chapel") != std::string::npos ||
                    role.find("chamber") != std::string::npos || role.find("quarters") != std::string::npos ||
                    role.find("apartment") != std::string::npos) return 3;
                for (const char* key : {"kitchen", "scullery", "pantry", "store", "cellar", "servants", "laundry",
                         "stable", "armory", "granary", "workshop"})
                    if (role.find(key) != std::string::npos) return 0;
                for (const char* key : {"barracks", "guard", "garrison"})
                    if (role.find(key) != std::string::npos) return 1;
                return 2;
            };
            const auto openGround = [&](int x, int y) {
                if (!InBounds(x, y, width, height)) return false;
                const std::size_t i = static_cast<std::size_t>(Index(x, y, width));
                return domain[i] && !structure[i] && biomes[i] == PLAIN;
            };
            // Forest, cliff and desert reach in at most 10% of the map from their edge: a door's ground must lie
            // outside that band, and outside the rounded corner where two sides meet.
            const int natureReach = natureBiome == RIVER ? 0 : std::max(3, std::min(width, height) / 10);
            // The water of a river castle runs from the map edge up to the castle's outermost point, so the
            // land in the notches of the outline stays dry: the water zone ends at the outermost wall.
            int leftmost = width, rightmost = -1, topmost = height;
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
                if (structure[static_cast<std::size_t>(Index(x, y, width))] != 0) {
                    leftmost = std::min(leftmost, x); rightmost = std::max(rightmost, x); topmost = std::min(topmost, y);
                }
            const auto inNatureZone = [&](int x, int y) {
                if (natureSides == 0 || natureBiome == PLAIN) return false;
                if (natureBiome == RIVER) {
                    return (natureLeft && x <= leftmost) || (natureRight && x >= rightmost) || (natureTop && y <= topmost);
                }
                const int margin = natureReach + 4, corner = 2 * natureReach + 4;
                if (natureLeft && x < margin) return true;
                if (natureRight && x > width - 1 - margin) return true;
                if (natureTop && y < margin) return true;
                if (natureTop && natureLeft && x < corner && y < corner) return true;
                if (natureTop && natureRight && x > width - 1 - corner && y < corner) return true;
                return false;
            };
            // The land a nature side itself covers (no margin and no rounded corner): a lot that nothing faces
            // through the wider zone above is served through this one.
            const auto inNatureReach = [&](int x, int y) {
                if (natureSides == 0 || natureBiome == PLAIN) return false;
                if (natureBiome == RIVER) return inNatureZone(x, y);
                return (natureLeft && x < natureReach) || (natureRight && x > width - 1 - natureReach) || (natureTop && y < natureReach);
            };
            const int span = doorWidth;
            // A corridor with fewer than three doors onto rooms gets a door out of the castle: the last resort, so it is
            // offered only to such a corridor (every other corridor takes no side door).
            const auto corridorNeedsExit = [&](int index) {
                if (rooms[static_cast<std::size_t>(index)].role != "corridor") return false;
                int doors = 0;
                for (const MapPlanRegion& region : candidate.regions)
                    doors += region.kind == "room_connection" && (region.parentId == index || region.linkedRegionId == index);
                return doors < castle_odds::kCorridorMinDoors;
            };
            std::vector<SideOption> options, relaxedOptions;
            for (const bool relaxed : {false, true}) {
            std::vector<SideOption>& found = relaxed ? relaxedOptions : options;
            for (int r = 0; r < static_cast<int>(rooms.size()); ++r) {
                int rank = r == entryRoomIndex ? (rankOf(rooms[r].role) < 0 ? -1 : 4) : rankOf(rooms[r].role);
                // A corridor is the very last choice: for a corridor short of doors (below), or for a lot no room faces (a corridor
                // along the wing's outer edge, the window side, may be the only thing that does).
                if (rooms[r].role == "corridor") rank = 5;
                // The rooms of a suite built into a wing may take a door onto a lot nothing else faces (the dressing room and
                // the study, then the bedchambers: the last choices); a suite room behind the throne room never.
                if (IsCastlePrivateRole(rooms[r].role)) {
                    bool behind = false;
                    for (const RoomInfo& other : rooms)
                        if (other.role == "throne_room" && rooms[r].rect.y1 <= other.rect.y0 + 1 && rooms[r].rect.x0 < other.rect.x1 && rooms[r].rect.x1 > other.rect.x0) behind = true;
                    rank = behind ? -1 : rooms[r].role == "royal_dressing_room" || rooms[r].role == "royal_study" ? 3 : 4;
                }
                if (rank < 0 || rooms[r].rect.Width() < 7 || rooms[r].rect.Height() < 7) continue;
                const Rect& rc = rooms[r].rect;
                for (int side = 0; side < 4; ++side) {  // 0 left, 1 right, 2 top, 3 bottom
                    const bool vertical = side < 2;
                    const int lo = (vertical ? rc.y0 : rc.x0) + 1, hi = (vertical ? rc.y1 : rc.x1) - 1;
                    if (hi - lo < span + 2) continue;
                    const int plane = side == 0 ? rc.x0 - 1 : side == 1 ? rc.x1 : side == 2 ? rc.y0 - 1 : rc.y1;
                    const int out = (side == 0 || side == 2) ? -1 : 1;
                    const int mid = lo + (hi - lo - span) / 2;
                    for (int shift = 0; shift <= hi - lo - span; ++shift) {
                        bool placed = false;
                        for (const int start : {mid - shift, mid + shift}) {
                            if (start < lo || start + span > hi) continue;
                            bool free = true;
                            for (int k = 0; k < span && free; ++k) {
                                const int along = start + k;
                                const Point wall = vertical ? Point{plane, along} : Point{along, plane};
                                if (!InBounds(wall.x, wall.y, width, height) ||
                                    roomOwner[static_cast<std::size_t>(Index(wall.x, wall.y, width))] >= 0 ||
                                    !structure[static_cast<std::size_t>(Index(wall.x, wall.y, width))]) { free = false; break; }
                                for (int depth = 1; depth <= 3; ++depth) {
                                    const Point p = vertical ? Point{plane + out * depth, along} : Point{along, plane + out * depth};
                                    if (!openGround(p.x, p.y) || (relaxed ? inNatureReach(p.x, p.y) : inNatureZone(p.x, p.y))) { free = false; break; }
                                }
                            }
                            if (!free) continue;
                            const Point centre = vertical ? Point{plane, start + span / 2} : Point{start + span / 2, plane};
                            const int distance = std::abs(centre.x - keepDoor.x) + std::abs(centre.y - keepDoor.y);
                            if (distance < 12) continue;
                            found.push_back({rank, r, side, start, distance});
                            placed = true;
                            break;
                        }
                        if (placed) break;
                    }
                }
            }
            std::stable_sort(found.begin(), found.end(), [](const SideOption& a, const SideOption& b) {
                return a.rank != b.rank ? a.rank < b.rank : a.distance > b.distance;
            });
            }  // relaxed
            // Empty lots inside the keep outline (a notch, a courtyard, a corner behind the wings: free
            // land holding an 8x8 square, other than the court in front of the gate), as connected pieces.
            std::vector<int> lotOf(static_cast<std::size_t>(area), 0);
            {
                std::vector<int> label(static_cast<std::size_t>(area), 0);
                std::vector<std::vector<int>> pieces(1);
                // The outer wall ring (one cell around the walls, drawn later) is not free land.
                const auto inKeep = [&](int x, int y) {
                    if (x < keepFixed.x0 || x >= keepFixed.x1 || y < keepFixed.y0 || y >= keepFixed.y1 || !openGround(x, y)) return false;
                    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                        if (InBounds(x + dx, y + dy, width, height) && structure[static_cast<std::size_t>(Index(x + dx, y + dy, width))]) return false;
                    return true;
                };
                for (int y = keepFixed.y0; y < keepFixed.y1; ++y) for (int x = keepFixed.x0; x < keepFixed.x1; ++x) {
                    if (!inKeep(x, y) || label[static_cast<std::size_t>(Index(x, y, width))] != 0) continue;
                    const int id = static_cast<int>(pieces.size());
                    pieces.emplace_back();
                    std::vector<int> stack{Index(x, y, width)};
                    label[static_cast<std::size_t>(stack.back())] = id;
                    while (!stack.empty()) {
                        const int cell = stack.back();
                        stack.pop_back();
                        pieces.back().push_back(cell);
                        for (const Point& d : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                            const int nx = cell % width + d.x, ny = cell / width + d.y;
                            if (!inKeep(nx, ny) || label[static_cast<std::size_t>(Index(nx, ny, width))] != 0) continue;
                            label[static_cast<std::size_t>(Index(nx, ny, width))] = id;
                            stack.push_back(Index(nx, ny, width));
                        }
                    }
                }
                for (int id = 1; id < static_cast<int>(pieces.size()); ++id) {
                    bool front = false, square = false;
                    for (const int cell : pieces[static_cast<std::size_t>(id)]) {
                        front = front || (std::abs(cell % width - keepDoor.x) <= 4 && std::abs(cell / width - keepDoor.y) <= 4);
                        if (square) continue;
                        bool all = true;
                        for (int dy = 0; dy < 8 && all; ++dy) for (int dx = 0; dx < 8; ++dx) {
                            const int x = cell % width + dx, y = cell / width + dy;
                            if (!InBounds(x, y, width, height) || label[static_cast<std::size_t>(Index(x, y, width))] != id) { all = false; break; }
                        }
                        square = all;
                    }
                    if (square && !front) for (const int cell : pieces[static_cast<std::size_t>(id)]) lotOf[static_cast<std::size_t>(cell)] = id;
                }
            }
            // Where a door's ground lies: three cells out from the wall, in the middle of the opening.
            const auto approachOf = [&](const SideOption& option) {
                const Rect& rc = rooms[option.room].rect;
                const bool vertical = option.side < 2;
                const int plane = option.side == 0 ? rc.x0 - 1 : option.side == 1 ? rc.x1 : option.side == 2 ? rc.y0 - 1 : rc.y1;
                const int out = (option.side == 0 || option.side == 2) ? -1 : 1;
                const int along = option.start + span / 2;
                return vertical ? Point{plane + out * 3, along} : Point{along, plane + out * 3};
            };
            std::vector<SideOption> chosen;
            for (const SideOption& option : options) {
                if (option.rank > 2) continue;
                if (static_cast<int>(chosen.size()) >= (keepFixed.Width() >= 56 ? 2 : 1)) break;
                bool apart = true;
                for (const SideOption& other : chosen)
                    if (other.room == option.room) apart = false;
                if (apart) chosen.push_back(option);
            }
            // Every empty lot gets a door onto it (from the best-ranked room that faces it), up to ten doors.
            std::set<int> served;
            for (const SideOption& option : chosen) {
                const Point p = approachOf(option);
                if (InBounds(p.x, p.y, width, height)) served.insert(lotOf[static_cast<std::size_t>(Index(p.x, p.y, width))]);
            }
            for (const std::vector<SideOption>* list : {&options, &relaxedOptions}) for (const SideOption& option : *list) {
                if (static_cast<int>(chosen.size()) >= 10) break;
                const Point p = approachOf(option);
                if (!InBounds(p.x, p.y, width, height)) continue;
                const int lot = lotOf[static_cast<std::size_t>(Index(p.x, p.y, width))];
                if (lot == 0 || served.count(lot) != 0) continue;
                served.insert(lot);
                chosen.push_back(option);
            }
            // Each corridor short of doors takes the best door out of the castle it has (the farthest from the gate).
            for (int r = 0; r < static_cast<int>(rooms.size()); ++r) {
                if (!corridorNeedsExit(r)) continue;
                bool has = false;
                for (const SideOption& option : chosen) has = has || option.room == r;
                if (has) continue;
                for (const std::vector<SideOption>* list : {&options, &relaxedOptions}) {
                    const auto it = std::find_if(list->begin(), list->end(), [&](const SideOption& option) { return option.room == r; });
                    if (it != list->end()) { chosen.push_back(*it); break; }
                }
            }
            for (const SideOption& option : chosen) {
                const Rect& rc = rooms[option.room].rect;
                const bool vertical = option.side < 2;
                const int plane = option.side == 0 ? rc.x0 - 1 : option.side == 1 ? rc.x1 : option.side == 2 ? rc.y0 - 1 : rc.y1;
                const int out = (option.side == 0 || option.side == 2) ? -1 : 1;
                const auto at = [&](int depth, int k) {
                    return vertical ? Point{plane + out * depth, option.start + k} : Point{option.start + k, plane + out * depth};
                };
                for (int k = 0; k < span; ++k) { openRoomCell(at(0, k)); openRoomCell(at(1, k)); }
                for (int k = -1; k <= span; ++k) for (int depth = 2; depth <= 4; ++depth) {
                    const Point p = at(depth, k);
                    if (InBounds(p.x, p.y, width, height)) doorFront[static_cast<std::size_t>(Index(p.x, p.y, width))] = 1;
                }
                MapPlanRegion door{"castle_side_door", roomDoorId++, at(0, 0).x, at(0, 0).y, at(0, span - 1).x, at(0, span - 1).y};
                door.role = "keep_side_door";
                door.rotation = vertical ? 1 : 0;
                door.parentId = rooms[option.room].regionId;
                for (int i = 0; i < static_cast<int>(candidate.regions.size()); ++i)
                    if (candidate.regions[i].kind == "room" && candidate.regions[i].id == door.parentId) door.parentIndex = i;
                candidate.regions.push_back(door);
                candidate.markers.push_back({"service_entry", at(1, span / 2).x, at(1, span / 2).y, door.parentId});
                candidate.markers.back().role = "keep_side_door";
            }
        }
        if (hasInnerWard) {
            PaintRing(biomes, structure, width, height, innerWard, 1, MOUNTAIN);
            for (int y = innerWard.y0; y < innerWard.y1; ++y) for (int x = innerWard.x0; x < innerWard.x1; ++x) {
            }
            const int gateX = mode == CastleMode::Palace ? keepDoor.x :
                std::clamp(keepDoor.x - wardGap, innerWard.x0 + 2, innerWard.x1 - 3);
            const Point southGate{gateX, innerWard.y1 - 1};
            for (const Point& point : std::array<Point, 1>{{southGate}}) {
                biomes[static_cast<std::size_t>(Index(point.x, point.y, width))] = BRIDGE;
                structure[static_cast<std::size_t>(Index(point.x, point.y, width))] = 0;
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
        failureStage = "connecting the main gate to the keep";
        if (!straightAvenue && !paintModeAxis(gates.front().inner, keepDoor, gates.front().halfWidth,
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
        for (int courtIndex = 0; courtIndex < 2 && minDimension >= 96 && facilityMax > 0; ++courtIndex) {
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
        const int requiredFacilities = facilityMax == 0 ? 0 : (facilityRangeWasExplicit ? facilityMin :
            std::max(4, static_cast<int>(std::lround(facilityMin / outerFacilityDensity))));
        int nextFacilityId = 0;
        const int remainingFacilityTarget = facilityMax == 0 ? 0 :
            std::max(4, targetFacilities - static_cast<int>(facilities.size()));
        for (int attempt = 0; attempt < targetFacilities * 180 && nextFacilityId < remainingFacilityTarget; ++attempt) {
            // Only the castle's own program facilities are built. Commercial
            // annexes would read as a castle town around the keep.
            const CastleFacilitySpec spec =
                program.facilities[static_cast<std::size_t>(nextFacilityId % program.facilities.size())];
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
        // An open castle has no gate to route from; its avenue is laid
        // straight after the castle is finished (see below).
        if (!straightAvenue) {
            nodes.push_back({gates.front().inner, true});
            for (std::size_t gateIndex = 1; gateIndex < gates.size(); ++gateIndex) nodes.push_back({gates[gateIndex].inner, true});
        }
        nodes.push_back({keepDoor, true});
        for (const Court& court : courts) nodes.push_back({court.anchor, false});
        for (const Point& approach : towerApproaches) nodes.push_back({approach, false});
        for (const FacilityInfo& facility : facilities) nodes.push_back({facility.approach, false});
        std::vector<Point> waterCrossings;
        std::vector<std::uint8_t> connected(nodes.size(), 0);
        std::vector<std::uint8_t> blockedEdges(nodes.size()*nodes.size(), 0);
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
                    if (blockedEdges[static_cast<std::size_t>(a)*nodes.size()+b]) continue;
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
                // The enlarged keep can separate the nearest pair. Try a
                // route from another connected node before rejecting it.
                blockedEdges[static_cast<std::size_t>(bestA)*nodes.size()+bestB] = 1;
                continue;
            }
            connected[static_cast<std::size_t>(bestB)] = 1;
            ++connectedCount;
        }
        failureStage = "connecting castle facilities and towers";
        if (connectedCount < static_cast<int>(nodes.size())) continue;
        std::vector<std::pair<int, int>> extraEdges;
        for (int a = 0; a < static_cast<int>(nodes.size()); ++a) for (int b = a + 1; b < static_cast<int>(nodes.size()); ++b) extraEdges.push_back({a, b});
        std::shuffle(extraEdges.begin(), extraEdges.end(), rng);
        const int extraCount = static_cast<int>(std::lround(nodes.size() * extraEdgeRate));
        for (int i = 0; i < extraCount && i < static_cast<int>(extraEdges.size()); ++i) {
            const auto [a, b] = extraEdges[static_cast<std::size_t>(i)];
            connectNodes(a, b, 0);
        }
        if (!straightAvenue) {
            for (const Gate& gate : gates) for (const Point& point : gate.bridge) biomes[static_cast<std::size_t>(Index(point.x, point.y, width))] = BRIDGE;
        }
        biomes[static_cast<std::size_t>(Index(keepDoor.x, keepDoor.y, width))] = BRIDGE;
        biomes[static_cast<std::size_t>(Index(keepDoor.x, keepDoor.y - 1, width))] = BRIDGE;
        {
            const RoomInfo& room = rooms[static_cast<std::size_t>(entryRoomIndex)];
            biomes[static_cast<std::size_t>(Index(room.rect.CenterX(), room.rect.y1 - 1, width))] = BRIDGE;
        }
        // Outer wall: the castle is complete, so wrap its whole outline (notches and wings
        // included) in a second wall. The castle's outside wall is therefore two cells
        // thick: the inner wall of the rooms plus this one. Doors, the main gate, roads
        // and water are left open.
        {
            std::vector<std::uint8_t> outerWall(area, 0);
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                if (!structure[static_cast<std::size_t>(Index(x, y, width))]) continue;
                for (int ny = y - 1; ny <= y + 1; ++ny) for (int nx = x - 1; nx <= x + 1; ++nx) {
                    if (!InBounds(nx, ny, width, height)) continue;
                    const std::size_t index = static_cast<std::size_t>(Index(nx, ny, width));
                    const std::uint8_t b = biomes[index];
                    if (structure[index] || b == BRIDGE || b == ROAD || b == RIVER || b == LAKE || b == SEA || b == TOWN_PAVEMENT) continue;
                    outerWall[index] = 1;
                }
            }
            // The inner wall is drawn like the partitions between rooms; the outer wall
            // around the castle is the darker one.
            for (std::size_t i = 0; i < biomes.size(); ++i) if (biomes[i] == EXMOUNTAIN) biomes[i] = MOUNTAIN;
            for (std::size_t i = 0; i < outerWall.size(); ++i) {
                if (!outerWall[i]) continue;
                biomes[i] = EXMOUNTAIN;
                structure[i] = 1;
            }
            closeWallCorners();
        }
        std::array<std::vector<int>, 3> sideShore;  // per side: nature cells from the map edge, line by line
        for (int natureSide = 0; natureSide < 3; ++natureSide) {
            if (((natureSides >> natureSide) & 1) == 0) continue;
            const int riverSide = natureSide;  // 0 left, 1 right, 2 top
            // The castle is complete: turn the chosen side of the map into water, from
            // the map edge up to the castle's outer wall. The shore follows the wall,
            // including its notches. Roads and buildings are never covered.
            const auto isBuilt = [&](int x, int y) { return structure[static_cast<std::size_t>(Index(x, y, width))] != 0; };
            const bool alongRows = riverSide != 2;                 // left/right: one edge per row
            const int lines = alongRows ? height : width;          // rows, or columns for the top side
            const int length = alongRows ? width : height;
            std::vector<int> known(static_cast<std::size_t>(lines), -1);  // water cells before the first wall
            for (int line = 0; line < lines; ++line) {
                for (int k = 0; k < length; ++k) {
                    const int pos = riverSide == 1 ? length - 1 - k : k;  // scan from the water's side
                    const int x = alongRows ? pos : line, y = alongRows ? line : pos;
                    if (isBuilt(x, y)) { known[static_cast<std::size_t>(line)] = k; break; }
                }
            }
            // The water is widest where it meets the building's outermost point
            // (`widest` cells from the edge) and never enters a notch of the building.
            // The rest of the shore wobbles with smooth noise, like a natural river,
            // and settles to the full width near the points where it meets the wall.
            int widest = length;
            for (const int k : known) if (k >= 0) widest = std::min(widest, k);
            std::vector<int> shore(static_cast<std::size_t>(lines), 0);
            if (widest > 0 && widest < length) {
                // The baseline of the shore is two cells short of the outermost point of the
                // building; the wobble moves it both ways, so the water can come right up to
                // the wall in places and leave bare ground between it and the wall elsewhere.
                // Too narrow to leave a gap: keep the shore at the outermost point.
                // A side of forest, cliff or desert reaches in at most 10% of the map (12 cells on a 128
                // map): a small castle is not squeezed by nature pressing in from the sides. The water
                // of a river castle is not limited.
                const int limit = natureBiome == RIVER ? length : std::max(3, length / 10);
                double baseline = widest > 3 ? widest - 2 : widest;
                double amplitude = widest > 3 ? std::clamp(widest / 4.0, 2.0, 5.0) : 0.0;
                if (baseline + amplitude > limit) {  // keep the wobble below the limit
                    amplitude = std::min(amplitude, limit / 4.0);
                    baseline = std::min(baseline, limit - amplitude);
                }
                const int ceiling = std::min(widest, limit);
                const int knotSpacing = 28;
                std::mt19937 shoreRng(static_cast<unsigned>(rng()));
                std::vector<double> knots(static_cast<std::size_t>(lines / knotSpacing + 3));
                for (double& knot : knots) knot = std::uniform_real_distribution<double>(0.0, 1.0)(shoreRng);
                std::vector<double> reach(static_cast<std::size_t>(lines), static_cast<double>(widest));
                for (int line = 0; line < lines; ++line) {
                    const double position = static_cast<double>(line) / knotSpacing;
                    const int k0 = static_cast<int>(position);
                    const double t = (1.0 - std::cos((position - k0) * 3.14159265358979)) * 0.5;
                    const double noise = knots[static_cast<std::size_t>(k0)] * (1.0 - t) + knots[static_cast<std::size_t>(k0 + 1)] * t;
                    // Signed wobble in [-amplitude, +amplitude], kept as a real number for now.
                    reach[static_cast<std::size_t>(line)] = baseline + amplitude * (noise * 2.0 - 1.0);
                }
                // Smooth the shore: average neighbouring lines, then let it change by at
                // most one cell from one line to the next, so it reads as a flowing bank
                // and never as steps. It never goes past the outermost point of the building.
                for (int pass = 0; pass < 2; ++pass) {
                    std::vector<double> blurred(reach.size());
                    for (int line = 0; line < lines; ++line) {
                        double sum = 0.0;
                        int count = 0;
                        for (int d = -8; d <= 8; ++d) {
                            const int other = std::clamp(line + d, 0, lines - 1);
                            sum += reach[static_cast<std::size_t>(other)];
                            ++count;
                        }
                        blurred[static_cast<std::size_t>(line)] = sum / count;
                    }
                    reach.swap(blurred);
                }
                for (int line = 0; line < lines; ++line)
                    shore[static_cast<std::size_t>(line)] = std::clamp(static_cast<int>(std::lround(reach[static_cast<std::size_t>(line)])), 0, ceiling);
                for (int line = 1; line < lines; ++line)
                    shore[static_cast<std::size_t>(line)] = std::clamp(shore[static_cast<std::size_t>(line)], shore[static_cast<std::size_t>(line - 1)] - 1, shore[static_cast<std::size_t>(line - 1)] + 1);
                for (int line = lines - 2; line >= 0; --line)
                    shore[static_cast<std::size_t>(line)] = std::clamp(shore[static_cast<std::size_t>(line)], shore[static_cast<std::size_t>(line + 1)] - 1, shore[static_cast<std::size_t>(line + 1)] + 1);
                for (int line = 0; line < lines; ++line)
                    shore[static_cast<std::size_t>(line)] = std::clamp(shore[static_cast<std::size_t>(line)], 0, ceiling);
            }
            sideShore[static_cast<std::size_t>(natureSide)] = shore;
            Rect water{width, height, -1, -1};
            for (int line = 0; line < lines; ++line) {
                const int cells = shore[static_cast<std::size_t>(line)];
                for (int k = 0; k < cells; ++k) {
                    const int pos = riverSide == 1 ? length - 1 - k : k;
                    const int x = alongRows ? pos : line, y = alongRows ? line : pos;
                    const std::size_t index = static_cast<std::size_t>(Index(x, y, width));
                    if (structure[index] != 0 || IsRoadLike(biomes[index])) continue;
                    biomes[index] = natureBiome;
                    water.x0 = std::min(water.x0, x); water.x1 = std::max(water.x1, x + 1);
                    water.y0 = std::min(water.y0, y); water.y1 = std::max(water.y1, y + 1);
                }
            }
            if (water.x1 > water.x0) {
                if (natureBiome == RIVER) {
                    candidate.regions.push_back({"waterway", 0, water.x0, water.y0, water.x1 - 1, water.y1 - 1});
                    candidate.regions.back().role = "river_flank";
                } else {
                    candidate.regions.push_back({"natural_terrain", 0, water.x0, water.y0, water.x1 - 1, water.y1 - 1});
                    candidate.regions.back().role = natureBiome == FOREST ? "forest_flank" :
                        desertGround ? "desert_flank" : "cliff_flank";
                }
            }
        }
        // Where the top side meets the left or the right side, the corner of the land is
        // rounded by an arc. Its radius follows the free space around the corner (large when
        // the land is open, small when the castle is near), and its edge wobbles gently.
        for (const int horizontalSide : {0, 1}) {
            if (((natureSides >> horizontalSide) & 1) == 0 || ((natureSides >> 2) & 1) == 0) continue;
            const std::vector<int>& sideCells = sideShore[static_cast<std::size_t>(horizontalSide)];
            const std::vector<int>& topCells = sideShore[2];
            if (sideCells.empty() || topCells.empty()) continue;
            const bool right = horizontalSide == 1;
            const auto landEdgeColumn = [&](int row) {
                const int cells = sideCells[static_cast<std::size_t>(std::clamp(row, 0, height - 1))];
                return right ? width - 1 - cells : cells;
            };
            // Where the two shores meet: the land's square corner (column of the first land
            // cell beside the side nature, row of the first land cell under the top nature).
            int cornerRow = 0, cornerColumn = right ? width - 1 : 0;
            for (int pass = 0; pass < 4; ++pass) {
                cornerColumn = landEdgeColumn(cornerRow);
                cornerRow = topCells[static_cast<std::size_t>(std::clamp(cornerColumn, 0, width - 1))];
            }
            // Largest square of free land (two cells clear of the castle) at the corner.
            const auto cellAt = [&](int u, int v) {  // u: into the land, v: down from the top shore
                const int x = right ? cornerColumn - u : cornerColumn + u, y = cornerRow + v;
                return Point{x, y};
            };
            const auto squareIsFree = [&](int radius) {
                for (int v = -2; v < radius + 2; ++v) for (int u = -2; u < radius + 2; ++u) {
                    const Point p = cellAt(u, v);
                    if (!InBounds(p.x, p.y, width, height)) return false;
                    if (structure[static_cast<std::size_t>(Index(p.x, p.y, width))]) return false;
                }
                return true;
            };
            int radius = 0;
            // (The arc is no bigger than the nature is thick, so a thin side is not bulged by it.)
            const int radiusLimit = natureBiome == RIVER ? 24 : std::min(24, std::max(3, std::min(width, height) / 10));
            while (radius < radiusLimit && squareIsFree(radius + 1)) ++radius;
            if (radius < 3) continue;
            std::mt19937 cornerRng(static_cast<unsigned>(rng()));
            double wobble[4];
            for (double& value : wobble) value = std::uniform_real_distribution<double>(0.0, 1.0)(cornerRng);
            std::vector<int> rowReach(static_cast<std::size_t>(radius), -1), columnReach(static_cast<std::size_t>(radius), -1);
            for (int v = 0; v < radius; ++v) for (int u = 0; u < radius; ++u) {
                // Distance from the arc's centre (radius, radius); the angle picks the wobble.
                const double du = static_cast<double>(radius - u), dv = static_cast<double>(radius - v);
                const double angle = std::atan2(dv, du) / 1.5707963267948966;  // 0..1 along the quarter circle
                const double position = std::clamp(angle, 0.0, 1.0) * 3.0;
                const int k0 = std::min(2, static_cast<int>(position));
                const double t = (1.0 - std::cos((position - k0) * 3.14159265358979)) * 0.5;
                const double noise = wobble[k0] * (1.0 - t) + wobble[k0 + 1] * t;
                const double arc = radius * (0.92 + 0.16 * noise);
                if (du * du + dv * dv <= arc * arc) continue;  // inside the rounded land
                const Point p = cellAt(u, v);
                const std::size_t index = static_cast<std::size_t>(Index(p.x, p.y, width));
                if (structure[index] != 0 || IsRoadLike(biomes[index]) || biomes[index] == natureBiome) continue;
                biomes[index] = natureBiome;
                rowReach[static_cast<std::size_t>(v)] = std::max(rowReach[static_cast<std::size_t>(v)], u);
                columnReach[static_cast<std::size_t>(u)] = std::max(columnReach[static_cast<std::size_t>(u)], v);
            }
            // Join the arc to both shores: the shores wobble, so land can be left between them
            // and the arc. Everything between the map edge and the arc's cells becomes nature.
            const auto joinCell = [&](int x, int y) {
                if (!InBounds(x, y, width, height)) return;
                const std::size_t index = static_cast<std::size_t>(Index(x, y, width));
                if (structure[index] != 0 || IsRoadLike(biomes[index])) return;
                biomes[index] = natureBiome;
            };
            for (int v = 0; v < radius; ++v) {
                const int reach = rowReach[static_cast<std::size_t>(v)];
                if (reach < 0) continue;
                const int edgeX = right ? width - 1 : 0, farX = cellAt(reach, v).x, y = cornerRow + v;
                for (int x = std::min(edgeX, farX); x <= std::max(edgeX, farX); ++x) joinCell(x, y);
            }
            for (int u = 0; u < radius; ++u) {
                const int reach = columnReach[static_cast<std::size_t>(u)];
                if (reach < 0) continue;
                const int x = cellAt(u, 0).x;
                for (int y = 0; y <= cornerRow + reach; ++y) joinCell(x, y);
            }
            // Smooth away the last nubs: a land cell pinched between nature (on both sides, or
            // with nature on three sides) around the corner joins the nature.
            for (int pass = 0; pass < 3; ++pass) {
                for (int v = -1; v <= radius; ++v) for (int u = -1; u <= radius; ++u) {
                    const Point p = cellAt(u, v);
                    if (!InBounds(p.x, p.y, width, height)) continue;
                    const std::size_t index = static_cast<std::size_t>(Index(p.x, p.y, width));
                    if (biomes[index] == natureBiome) continue;
                    const auto isNature = [&](int cx, int cy) {
                        return InBounds(cx, cy, width, height) && biomes[static_cast<std::size_t>(Index(cx, cy, width))] == natureBiome;
                    };
                    const int around = static_cast<int>(isNature(p.x - 1, p.y)) + static_cast<int>(isNature(p.x + 1, p.y)) +
                        static_cast<int>(isNature(p.x, p.y - 1)) + static_cast<int>(isNature(p.x, p.y + 1));
                    const bool pinched = (isNature(p.x - 1, p.y) && isNature(p.x + 1, p.y)) ||
                        (isNature(p.x, p.y - 1) && isNature(p.x, p.y + 1)) || around >= 3;
                    if (pinched) joinCell(p.x, p.y);
                }
            }
        }
        // Where the top side meets the left or the right side, smooth the boundary between nature and
        // land: a few passes of a 7x7 majority filter over the corner window (the shores wobble, and a
        // castle close to the corner leaves jagged nubs and steps). Walls, floors and roads are left
        // alone and do not vote, so the nature never covers a building and never closes a road. The
        // ground within three cells of the castle or a road is not touched either: the strip of land
        // that runs along the wall keeps its shape (smoothing it would cut it into blocks).
        if ((natureSides & 4) != 0 && (natureSides & 3) != 0 && natureBiome != PLAIN && natureBiome != RIVER) {
            const auto widest = [&](int side) { int most = 0; for (const int v : sideShore[static_cast<std::size_t>(side)]) most = std::max(most, v); return most; };
            const int yEnd = std::min(height, widest(2) + 26);
            std::vector<std::pair<int, int>> windows;
            if ((natureSides & 1) != 0) windows.push_back({0, std::min(width, widest(0) + 26)});
            if ((natureSides & 2) != 0) windows.push_back({std::max(0, width - widest(1) - 26), width});
            const auto kindAt = [&](int x, int y) {  // 1 nature, 0 open land, -1 anything else
                if (!InBounds(x, y, width, height)) return -1;
                const std::size_t index = static_cast<std::size_t>(Index(x, y, width));
                if (biomes[index] == natureBiome) return 1;
                return (biomes[index] == PLAIN && structure[index] == 0) ? 0 : -1;
            };
            std::vector<std::uint8_t> frozen(area, 0);
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                const std::size_t index = static_cast<std::size_t>(Index(x, y, width));
                if (structure[index] == 0 && !IsRoadLike(biomes[index])) continue;
                for (int dy = -3; dy <= 3; ++dy) for (int dx = -3; dx <= 3; ++dx)
                    if (InBounds(x + dx, y + dy, width, height)) frozen[static_cast<std::size_t>(Index(x + dx, y + dy, width))] = 1;
            }
            for (const auto& window : windows) {
                for (int pass = 0; pass < 30; ++pass) {
                    int changed = 0;  // cells are updated in place, in scan order, so the passes converge
                    for (int y = 0; y < yEnd; ++y) for (int x = window.first; x < window.second; ++x) {
                        const int here = kindAt(x, y);
                        if (here < 0 || frozen[static_cast<std::size_t>(Index(x, y, width))]) continue;
                        int natureCount = 0, total = 0;
                        for (int dy = -3; dy <= 3; ++dy) for (int dx = -3; dx <= 3; ++dx) {
                            const int k = kindAt(x + dx, y + dy);
                            if (k < 0) continue;
                            ++total;
                            natureCount += k;
                        }
                        const int next = natureCount * 2 > total ? 1 : natureCount * 2 < total ? 0 : here;
                        if (next == here) continue;
                        biomes[static_cast<std::size_t>(Index(x, y, width))] = next == 1 ? natureBiome : static_cast<std::uint8_t>(PLAIN);
                        ++changed;
                    }
                    if (changed == 0) break;
                }
            }
        }
        // A land cell pinched between two nature cells (a slit one cell wide) joins the nature, wherever
        // it is: the corner rounding above only reaches the corners that have room for an arc.
        if (natureSides != 0 && natureBiome != PLAIN && natureBiome != RIVER) {
            for (int pass = 0; pass < 2; ++pass) {
                for (int y = 1; y + 1 < height; ++y) for (int x = 1; x + 1 < width; ++x) {
                    const std::size_t index = static_cast<std::size_t>(Index(x, y, width));
                    if (biomes[index] == natureBiome || structure[index] != 0 || IsRoadLike(biomes[index])) continue;
                    const auto isNature = [&](int cx, int cy) { return biomes[static_cast<std::size_t>(Index(cx, cy, width))] == natureBiome; };
                    if ((isNature(x - 1, y) && isNature(x + 1, y)) || (isNature(x, y - 1) && isNature(x, y + 1))) biomes[index] = natureBiome;
                }
            }
        }
        if (straightAvenue) {
            // The castle is complete, so its door position is final. Lay the
            // main avenue now: one straight line from the keep's door to the
            // map edge, never bending. A candidate whose door axis is blocked
            // by a building or water is rejected in favour of another layout.
            failureStage = "laying the straight main avenue";
            if (keepDoor.y + 1 >= height) continue;
            bool avenueClear = true;
            for (int y = keepDoor.y + 1; y < height && avenueClear; ++y) {
                const std::size_t index = static_cast<std::size_t>(Index(keepDoor.x, y, width));
                // Water is bridged; only a building in the way rejects the layout.
                avenueClear = structure[index] == 0;
            }
            if (!avenueClear) continue;
            const int avenueHalfWidth = (mainRoadWidth - 1) / 2;
            for (int y = keepDoor.y + 1; y < height; ++y) {
                for (int offset = -avenueHalfWidth; offset <= avenueHalfWidth; ++offset) {
                    const int x = keepDoor.x + offset;
                    if (!InBounds(x, y, width, height)) continue;
                    const std::size_t index = static_cast<std::size_t>(Index(x, y, width));
                    if (structure[index] != 0) continue;
                    biomes[index] = IsWater(biomes[index]) ? BRIDGE : ROAD;
                }
            }
            candidate.markers.push_back({"map_exit", keepDoor.x, height - 1, 0});
            candidate.markers.back().role = "open_castle_approach";
        }
        for (const MapPlanMarker& marker : candidate.markers) {
            if (marker.kind != "service_entry" || marker.role != "inner_ward_entry") continue;
            if (!InBounds(marker.x, marker.y, width, height)) continue;
            biomes[static_cast<std::size_t>(Index(marker.x, marker.y, width))] = BRIDGE;
            structure[static_cast<std::size_t>(Index(marker.x, marker.y, width))] = 0;
        }

        // Pocket parks are unrelated to the castle and are not built. Street trees stay.
        const int pocketParkTarget = 0;
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
                if (structure[static_cast<std::size_t>(index)] || biomes[static_cast<std::size_t>(index)] != PLAIN || doorFront[static_cast<std::size_t>(index)]) continue;
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
        candidate.castleStreetMask.assign(area,0);
        for(int i=0;i<area;++i)if(biomes[i]==ROAD||biomes[i]==BRIDGE)candidate.castleStreetMask[i]=1;
        MapPlan pavingPlan = candidate;
        // Open ground outside the castle stays natural: only the main avenue is built.
        // The editor always sends a paving ratio; a castle ignores it (no castle town).
        pavingPlan.pavingTargetRatio = 0.0;
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
        const Point approachStart = !straightAvenue ? gates.front().inner : Point{keepDoor.x, height - 1};
        const auto mainApproach = FindPath(domain, structure, biomes, width, height, approachStart, keepDoor);
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

        wallWalkCells = static_cast<int>(std::count_if(candidate.biomes.begin(), candidate.biomes.end(), [](std::uint8_t tile) {
            return tile == CASTLE_WALL_WALK;
        }));
        const double condition = std::clamp(0.64 + wealth * 0.14, 0.70, 1.0);
        candidate.castleHistory.push_back({"foundation", "castle_ground", 0, condition});
        candidate.castleHistory.push_back({"keep_construction", program.layout, static_cast<int>(rooms.size()), condition});
        candidate.castleHistory.push_back({"castle_ground_development", "working_courts_and_service_buildings", static_cast<int>(facilities.size()), condition});
        if (hasOuterDefenseEquipment) {
            candidate.castleHistory.push_back({"defensive_modernization", "wall_walk_and_watch_towers",
                wallWalkCells + std::max(0, static_cast<int>(towerApproaches.size()) - 4), condition});
        }
        candidate.localEconomy = candidate.wealthClass;
        const MapQualityMetrics quality = EvaluateMapQuality(candidate);
        const double markerRate = quality.markerCount == 0 ? 0.0 : static_cast<double>(quality.reachableMarkers) / quality.markerCount;
        double score = quality.largestWalkableComponentRatio * 500.0 + markerRate * 260.0;
        score += std::min(12, quality.castleTowerCount) * 10.0;
        // Rooms and facilities only matter up to a modest count: past it, a layout with more of them (a corridor entrance
        // with a room on each side, say) must not win over one with fewer just for that.
        score += std::min(16, quality.castleRoomCount) * 7.0;
        score += std::min(8, quality.castleFacilityCount) * 5.0;
        score -= quality.castleCourtyardEmptyRatio * 120.0;
        // A long, thin room (more than 3.5:1) that could not be split loses to a layout without one.
        for (const auto& region : candidate.regions) {
            if (region.kind != "room" || region.role == "corridor" || region.role == "throne_room" || region.role == "antechamber" || IsCastleEntryRole(region.role)) continue;
            const double rw = region.x1 - region.x0 + 1, rh = region.y1 - region.y0 + 1;
            if (std::max(rw, rh) > 3.5 * std::min(rw, rh)) score -= 250.0;
        }
        score -= quality.issues.size() * 700.0;
        candidate.qualityScore = score;
        candidate.selectionReason = "highest deterministic castle score with wealth-led growth, keep rooms and working courts";
        generated = true;
        ranked.push_back({std::move(candidate), quality, score});
    }

    if (!generated) {
        outError = std::string("Castle generation could not place a connected fortified layout: ") + failureStage;
        return false;
    }
    // Furnish the best layout with the editor's native assets: doors in the partitions, then
    // furniture and floor surfaces. (The raised "furniture" floor tiles stay unused.) Furnishing
    // is the costly step, so it runs on the ranked layouts in turn until one succeeds. If none
    // can be furnished the best layout is kept bare and its reason is reported as a warning.
    std::stable_sort(ranked.begin(), ranked.end(), [](const CandidateResult& a, const CandidateResult& b) { return a.score > b.score; });
    std::string interiorError;
    bool furnished = false;
    for (auto& entry : ranked) {
        std::string error;
        MapPlan trial = entry.plan;  // Decorate edits its plan even when it fails
        if (castle_interior::Decorate(trial, params_, error)) {
            entry.plan = std::move(trial);
            best = std::move(entry);
            furnished = true;
            break;
        }
        if (interiorError.empty()) interiorError = error;
    }
    if (!furnished) {
        best = std::move(ranked.front());
        // The castle keeps its doors: a layout that cannot be furnished is decorated with the doors only.
        {
            MapPlan doors = best.plan;
            std::string doorError;
            if (castle_interior::Decorate(doors, params_, doorError, true)) best.plan = std::move(doors);
        }
        best.plan.castleHistory.push_back({"interior_design", "unfurnished: " + interiorError, 0, 0.0});
    }
    outPlan = std::move(best.plan);
    // The desert's ground is desert. It is painted on the finished plan only, after the
    // candidates were built and ranked on plain ground, so it cannot change which wins.
    const bool desert = std::any_of(outPlan.regions.begin(), outPlan.regions.end(), [](const MapPlanRegion& region) {
        return region.kind == "natural_terrain" && region.role == "desert_flank";
    });
    if (desert) {
        for (auto& tile : outPlan.biomes) if (tile == PLAIN) tile = DESERT;
    }
    // Facilities on the open ground (outdoor yards and walled annexes): after the desert, so the ground is final.
    {
        std::mt19937 lotRng(CandidateSeed(params_.seed, 9100));
        // (lotFacilities=0 leaves the open ground bare: the tests of the keep and the walls check them without the facilities.)
        const auto lotSwitch = params_.extra.find("lotFacilities");
        if (lotSwitch == params_.extra.end() || lotSwitch->second != 0) castle_lot::PlaceFacilities(outPlan, static_cast<int>(mode), lotRng, desert);
    }
    // Grade the dry banks beside the water the way a village does: low, gentle
    // banks, and nothing is cut near a building or road.
    ApplySettlementShore(outPlan);
    return true;
}
