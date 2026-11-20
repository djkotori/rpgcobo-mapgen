#include "CastleLotFacilities.h"
#include "CastleMapImageGenerator.h"
#include "MapQuality.h"
#include "TestScale.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Facilities on the open ground of a castle (docs/references/castle-lot-facilities.md).
//  - Outdoor facilities ("lot_facility") are painted with their own ground; annexes ("lot_annex") are walled buildings
//    (outer wall, inner walls, one 4-wide door, always on the south wall). Their rooms are "room" regions whose
//    role starts with "annex_"; they are not counted as keep rooms.
//  - Every facility is at least its catalog size, inside the castle ground and not in the outer 10% of the image, 4 cells clear of any wall, road, stone floor or
//    other facility, and its door (an annex's) can be walked to from the keep's public entrance.
//  - An annex road ("lot_annex_road") is 2 wide, at most 20 long, ROAD biome.
//  - Nothing is mandatory: no facility appears in nearly every castle.
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

MapPlan Make(int size, int mode, unsigned seed) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"exteriorTerrain", 0}, {"natureSideCount", 0}};
    // The sizes of the registered assets as the editor passes them (the trees are big: 6 across).
    params.extra.insert({{"settlementProp_conifer_width", 6}, {"settlementProp_conifer_depth", 5}, {"settlementProp_shade_tree_width", 6}, {"settlementProp_shade_tree_depth", 6},
        {"settlementProp_palm_width", 5}, {"settlementProp_palm_depth", 4}, {"settlementProp_shrub_width", 4}, {"settlementProp_shrub_depth", 4},
        {"settlementProp_rock_width", 2}, {"settlementProp_rock_depth", 2},
        {"civicObject2_1Width", 3}, {"civicObject2_1Depth", 3}, {"civicObject2_1Height", 6},
        {"civicObject3_14Width", 4}, {"civicObject3_14Depth", 4}, {"civicObject3_14Height", 6},
        {"civicObject3_26Width", 4}, {"civicObject3_26Depth", 4}, {"civicObject3_26Height", 5}});
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    Require(generator.Generate(plan, error), "size=" + std::to_string(size) + " mode=" + std::to_string(mode) +
        " seed=" + std::to_string(seed) + ": " + error);
    return plan;
}

const castle_lot::FacilitySpec* SpecOf(const std::string& role) {
    for (const auto& spec : castle_lot::Catalog()) if (role == spec.role) return &spec;
    return nullptr;
}

struct Tally {
    int castles = 0, courtyard = 0, outdoor = 0, annex = 0, mismatch = 0, roads = 0, grass = 0, forest = 0, jails = 0, twoRoom = 0, rowhouses = 0, castleDoors = 0, fixtures = 0, props = 0, flowers = 0, crops = 0, archery = 0, chairPairs = 0, lamps = 0, stacks = 0, tops = 0, statues = 0, wells = 0, treedGardens = 0, gardens = 0, facingLamps = 0;
    std::map<std::string, int> byRole;
};

bool InRect(const MapPlanRegion& r, int x, int y) { return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1; }

void Check(const MapPlan& plan, int mode, const std::string& context, Tally& tally) {
    ++tally.castles;
    const auto biome = [&](int x, int y) { return plan.biomes[static_cast<std::size_t>(y * plan.width + x)]; };
    const auto inBounds = [&](int x, int y) { return x >= 0 && y >= 0 && x < plan.width && y < plan.depth; };
    const MapPlanRegion* ground = nullptr;
    std::vector<const MapPlanRegion*> outlines, roads, doors, rooms;
    for (const auto& r : plan.regions) {
        if (r.kind == "castle_ground") ground = &r;
        if (r.kind == "lot_facility" || r.kind == "lot_annex") outlines.push_back(&r);
        if (r.kind == "lot_annex_road") roads.push_back(&r);
        if (r.kind == "lot_annex_door") doors.push_back(&r);
        if (r.kind == "room" && r.role.rfind("annex_", 0) == 0) rooms.push_back(&r);
    }
    Require(ground != nullptr, context + ": no castle ground");
    // The keep's room count leaves the annex rooms out (like corridors).
    {
        int keepRooms = 0;
        for (const auto& r : plan.regions) keepRooms += r.kind == "room" && r.role != "corridor" && r.role.rfind("annex_", 0) != 0;
        Require(EvaluateMapQuality(plan).castleRoomCount == keepRooms, context + ": annex rooms are counted as keep rooms");
    }
    // Walkable ground from the keep's public entrance.
    std::vector<std::uint8_t> reach(plan.biomes.size(), 0);
    {
        const auto walkable = [&](int x, int y) {
            const int b = biome(x, y);
            return b != WALL && b != INNERWALL && b != RIVER && b != LAKE && b != SEA && b != TOWN_SEA && b != TOWN_CLIFF;
        };
        std::queue<std::pair<int, int>> open;
        for (const auto& m : plan.markers)
            if (m.role == "keep_public_entry" && inBounds(m.x, m.y) && walkable(m.x, m.y)) { reach[static_cast<std::size_t>(m.y * plan.width + m.x)] = 1; open.push({m.x, m.y}); }
        while (!open.empty()) {
            const auto [x, y] = open.front(); open.pop();
            for (const auto& d : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                const int nx = x + d.first, ny = y + d.second;
                if (!inBounds(nx, ny) || reach[static_cast<std::size_t>(ny * plan.width + nx)] || !walkable(nx, ny)) continue;
                reach[static_cast<std::size_t>(ny * plan.width + nx)] = 1;
                open.push({nx, ny});
            }
        }
    }
    std::set<std::string> seen;  // (the castles a facility appears in, not how many it has)
    for (std::size_t i = 0; i < outlines.size(); ++i) {
        const MapPlanRegion& o = *outlines[i];
        const castle_lot::FacilitySpec* spec = SpecOf(o.role);
        Require(spec != nullptr, context + ": unknown facility role " + o.role);
        Require(spec->annex == (o.kind == "lot_annex"), context + ": " + o.role + " has the wrong region kind");
        const int w = o.x1 - o.x0 + 1, h = o.y1 - o.y0 + 1;
        // (a courtyard facility fills whatever the courtyard is, so it has no catalog minimum)
        if (o.parentId < 0)
            Require((w >= spec->minWidth && h >= spec->minHeight) || (w >= spec->minHeight && h >= spec->minWidth),
                context + ": " + o.role + " is smaller than its catalog size (" + std::to_string(w) + "x" + std::to_string(h) + ")");
        Require(o.x0 >= plan.width / 10 && o.y0 >= plan.depth / 10 && o.x1 <= plan.width - 1 - plan.width / 10 && o.y1 <= plan.depth - 1 - plan.depth / 10, context + ": " + o.role + " lies in the outer 10% of the image");
        Require(o.x0 >= ground->x0 && o.y0 >= ground->y0 && o.x1 <= ground->x1 && o.y1 <= ground->y1, context + ": " + o.role + " lies outside the castle ground");
        ++(spec->annex ? tally.annex : tally.outdoor);
        tally.mismatch += !spec->fits[mode];
        seen.insert(o.role);
        tally.grass += o.role == "yard_garden_grass";
        tally.forest += o.role == "yard_garden_forest";
        // A road of its own may cross the clearance.
        const auto ownRoad = [&](int x, int y) {
            for (const auto* r : roads) if (r->parentId == o.id && InRect(*r, x, y)) return true;
            return false;
        };
        // A courtyard facility (inside the keep) needs no clearance and keeps its square corners.
        const bool courtyard = o.parentId >= 0;
        tally.courtyard += courtyard;
        // 4 cells clear of walls, roads, stone floor, water and other facilities.
        for (int y = o.y0 - (courtyard ? 0 : 4); y <= o.y1 + (courtyard ? 0 : 4); ++y) for (int x = o.x0 - (courtyard ? 0 : 4); x <= o.x1 + (courtyard ? 0 : 4); ++x) {
            if (!inBounds(x, y) || InRect(o, x, y) || ownRoad(x, y)) continue;
            const int b = biome(x, y);
            const bool bad = b == WALL || b == INNERWALL || b == ROAD || b == BRIDGE || b == RIVER || b == LAKE || b == SEA ||
                b == TOWN_STONE_FLOOR || b == TOWN_PAVEMENT || b == TOWN_SEA || b == TOWN_CLIFF;
            Require(!bad, context + ": " + o.role + " is not 4 cells clear (biome " + std::to_string(b) + " at " + std::to_string(x) + "," + std::to_string(y) + ")");
            for (std::size_t j = 0; j < outlines.size(); ++j)
                if (j != i) Require(!InRect(*outlines[j], x, y), context + ": " + o.role + " is within 4 cells of " + outlines[j]->role);
        }
        if (!spec->annex) {
            // Outside the courtyards the four corners are cut by a cell (rounded): they are not the facility's ground.
            if (!courtyard && spec->ground != PLAIN)
                for (const auto& c : std::array<std::pair<int, int>, 4>{{{o.x0, o.y0}, {o.x1, o.y0}, {o.x0, o.y1}, {o.x1, o.y1}}})
                    Require(biome(c.first, c.second) != spec->ground, context + ": " + o.role + " has a square corner");
            // (A courtyard facility fills the whole courtyard, which is not a rectangle: only its bounding box is the region.)
            if (!courtyard)
            for (int y = o.y0; y <= o.y1; ++y) for (int x = o.x0; x <= o.x1; ++x)
                Require(biome(x, y) == spec->ground || biome(x, y) == PLAIN || (o.role == "yard_archery_range" && (biome(x, y) == TOWN_PAVEMENT || biome(x, y) == DESERT)), context + ": " + o.role + " has a foreign ground (biome " + std::to_string(biome(x, y)) + ")");
            continue;
        }
        // An annex: a wall all round (but its door), a stone floor inside, rooms, a door that can be walked to.
        std::vector<const MapPlanRegion*> mine;
        for (const auto* r : rooms) if (r->x0 > o.x0 && r->x1 < o.x1 && r->y0 > o.y0 && r->y1 < o.y1) mine.push_back(r);
        std::vector<const MapPlanRegion*> myDoors;
        for (const auto* d : doors) if (d->parentId == o.id) myDoors.push_back(d);
        Require(!myDoors.empty(), context + ": " + o.role + " has no door");
        for (const auto* d : myDoors) {
            const int dw = d->x1 - d->x0 + 1, dh = d->y1 - d->y0 + 1;
            Require(std::min(dw, dh) == 1 && std::max(dw, dh) == 4, context + ": the door of " + o.role + " is not 4 wide");
            Require(d->y0 == o.y1 && d->y1 == o.y1, context + ": the door of " + o.role + " is not on the south wall");
        }
        const auto inDoor = [&](int x, int y) { for (const auto* d : myDoors) if (InRect(*d, x, y)) return true; return false; };
        // The outer wall (the keep's dark ring) round the outline, then the wall proper just inside it; a door through both.
        for (int y = o.y0; y <= o.y1; ++y) for (int x = o.x0; x <= o.x1; ++x) {
            const bool outer = x == o.x0 || x == o.x1 || y == o.y0 || y == o.y1;
            const bool inner = !outer && (x == o.x0 + 1 || x == o.x1 - 1 || y == o.y0 + 1 || y == o.y1 - 1);
            if (outer) Require(inDoor(x, y) || biome(x, y) == INNERWALL, context + ": " + o.role + " has a gap in its outer wall");
            if (inner) Require(biome(x, y) == WALL || biome(x, y) == BRIDGE, context + ": " + o.role + " has a gap in its wall");
        }
        // Every room is at least 8 x 8 inside; a jail cell 6 x 6, and the jail's hall 6 across the short way.
        for (const auto* r : mine) {
            const bool small = r->role.size() >= 5 && (r->role.compare(r->role.size() - 5, 5, "_cell") == 0 || r->role.compare(r->role.size() - 5, 5, "_hall") == 0);
            const int least = small ? 6 : 8;
            Require(r->x1 - r->x0 + 1 >= least && r->y1 - r->y0 + 1 >= least, context + ": a room of " + o.role + " is smaller than " + std::to_string(least) + " x " + std::to_string(least));
        }
        for (const auto* r : mine) for (int y = r->y0; y <= r->y1; ++y) for (int x = r->x0; x <= r->x1; ++x)
            Require(biome(x, y) == TOWN_STONE_FLOOR, context + ": a room of " + o.role + " is not a stone floor");
        if (spec->rooms == 1) Require(mine.size() == 1 && mine[0]->role == o.role, context + ": " + o.role + " should be one room");
        if (spec->rooms == 2) {
            Require(mine.size() == 2, context + ": " + o.role + " should be two rooms");
            tally.twoRoom += 1;
        }
        if (spec->rooms == -1) {
            // A row house: three rooms or more side by side, each with a door of its own.
            Require(mine.size() >= 3 && myDoors.size() == mine.size(), context + ": the row house has " + std::to_string(mine.size()) + " rooms and " + std::to_string(myDoors.size()) + " doors");
            for (const auto* r : mine) Require(r->role == o.role + "_unit", context + ": a row house room has the role " + r->role);
            for (const auto* r : mine) Require(r->x1 - r->x0 + 1 >= 8, context + ": a row house room is narrower than 8");
            ++tally.rowhouses;
        }
        if (spec->rooms == 0) {
            int cells = 0, halls = 0;
            for (const auto* r : mine) { cells += r->role == o.role + "_cell"; halls += r->role == o.role + "_hall"; }
            Require(cells >= 3 && cells <= 4 && halls == 1, context + ": the jail has " + std::to_string(cells) + " cells and " + std::to_string(halls) + " halls");
            ++tally.jails;
        }
        // The cell just outside the door can be walked to from the keep's public entrance.
        for (const auto* d : myDoors) {
            const int fx = d->x0 == d->x1 ? (d->x0 == o.x0 ? o.x0 - 1 : o.x1 + 1) : (d->x0 + d->x1) / 2;
            const int fy = d->y0 == d->y1 ? (d->y0 == o.y0 ? o.y0 - 1 : o.y1 + 1) : (d->y0 + d->y1) / 2;
            Require(inBounds(fx, fy) && reach[static_cast<std::size_t>(fy * plan.width + fx)], context + ": the door of " + o.role + " cannot be walked to from the keep entrance");
        }
    }
    // One facility to a lot, and a role at most twice in a castle.
    {
        std::map<std::string, int> counts;
        for (const auto* o : outlines) if (o->parentId < 0) Require(++counts[o->role] <= 2, context + ": " + o->role + " appears more than twice");
    }
    for (const std::string& role : seen) ++tally.byRole[role];
    // Layout rules: an archery range has its targets (shields) north of its benches and a firing line; a table's chairs face it
    // (one rotated half a turn); a paddock's gate is on the south.
    {
        for (const auto* o : outlines) {
            if (o->kind != "lot_facility" || o->role != "yard_archery_range") continue;
            int targetsY = 1 << 30, benchesY = -1, stone = 0;
            for (const auto& r : plan.regions) {
                if (r.id < 90000 || r.kind != "castle_fixture" || r.x0 < o->x0 || r.x1 > o->x1 || r.y0 < o->y0 || r.y1 > o->y1) continue;
                if (r.role == "shield") targetsY = std::min(targetsY, r.y0);
                if (r.role == "bench") benchesY = std::max(benchesY, r.y1);
            }
            for (int y = o->y0; y <= o->y1; ++y) for (int x = o->x0; x <= o->x1; ++x) stone += biome(x, y) == TOWN_PAVEMENT;
            Require(benchesY < 0, context + ": an archery range has a bench");
            // (a range too small for its pattern has a single piece in the middle, and no line)
            if (targetsY < (1 << 30)) { Require(stone > 0, context + ": an archery range has no firing line"); ++tally.archery; }
        }
        for (const auto* room : rooms) {
            std::vector<const MapPlanRegion*> chairs;
            for (const auto& r : plan.regions) if (r.id >= 90000 && r.kind == "castle_fixture" && r.role == "chair" && r.parentId == room->id) chairs.push_back(&r);
            if (chairs.size() >= 2) {
                Require(chairs[0]->rotation != chairs[1]->rotation, context + ": the chairs of a table face the same way");
                ++tally.chairPairs;
            }
        }
    }
    // Stacked barrels (castle_detail): a barrel on a barrel, over the seam between two below; a third tier stands on a second.
    for (const auto& r : plan.regions) {
        if (r.kind != "castle_detail" || r.id < 90000 || r.role != "barrel") continue;
        Require(r.parentIndex >= 0 && r.parentIndex < static_cast<int>(plan.regions.size()), context + ": a stacked barrel has no base");
        const MapPlanRegion& base = plan.regions[static_cast<std::size_t>(r.parentIndex)];
        Require(base.role == "barrel" && (base.kind == "castle_fixture" || base.kind == "castle_detail"), context + ": a stacked barrel stands on something else than a barrel");
        Require(!(r.x1 < base.x0 || base.x1 < r.x0 || r.y1 < base.y0 || base.y1 < r.y0), context + ": a stacked barrel is not over its base");
        ++tally.stacks;
        tally.tops += base.kind == "castle_detail";
    }
    // At most one well yard on a map, and no Well asset indoors (a well house is a room with buckets and barrels).
    {
        int wellYards = 0;
        for (const auto* o : outlines) wellYards += o->kind == "lot_facility" && o->role == "yard_well";
        Require(wellYards <= 1, context + ": more than one well yard");
        for (const auto& r : plan.regions) Require(!(r.kind == "settlement_facility" && r.role == "well" && r.parentId >= 0 && plan.regions[static_cast<std::size_t>(r.parentIndex)].kind != "lot_facility"), context + ": a well stands in a room");
    }
    // The outdoor facilities: no smithy (its ground was the castle's floor), no shelf or chest outdoors, the registered Well, lamps in
    // the gardens, statues among the graves; a garden has its trees; a jail cell is always fitted the same way; and only the throne
    // room's front door is a royal door.
    {
        for (const auto* o : outlines) {
            Require(o->role != "yard_smithy", context + ": a smithy yard was generated");
            if (o->kind != "lot_facility") continue;
            int trees = 0;
            for (const auto& r : plan.regions) {
                if (r.id < 90000 || r.x0 < o->x0 || r.x1 > o->x1 || r.y0 < o->y0 || r.y1 > o->y1) continue;
                Require(!(r.kind == "castle_fixture" && (r.role == "chest" || r.role == "shelf")), context + ": a " + r.role + " stands outdoors in " + o->role);
                if (r.kind == "settlement_prop" && (r.role == "shade_tree" || r.role == "conifer" || r.role == "palm" || (o->role == "yard_garden_forest" && (r.role == "cactus" || r.role == "shrub")))) ++trees;
                // A forest garden in the desert is planted with cacti and shrubs, not with a grid of palms.
                Require(!(o->role == "yard_garden_forest" && r.kind == "settlement_prop" && r.role == "palm"), context + ": a forest garden holds a palm");
                if (r.kind == "settlement_facility") {
                    if (r.role == "street_lamp") { Require(o->role == "yard_garden_grass" || o->role == "yard_garden_forest" || o->role == "yard_well", context + ": a lamp outside the gardens and the well yard"); ++tally.lamps; }
                    if (r.role == "statue") { Require(o->role == "yard_graveyard", context + ": a statue outside the graveyard"); ++tally.statues; }
                    if (r.role == "well") { Require(o->role == "yard_well", context + ": a well outside the well yard"); ++tally.wells; }
                }
            }
            // The two lamps of a grass garden stand on either side of the path and face each other: the one on the + side of the main axis
            // turns to the - side, the other the reverse (vertical axis: south 0 / north 2; horizontal axis: east 3 / west 1).
            if (o->role == "yard_garden_grass") {
                std::vector<const MapPlanRegion*> lamps;
                for (const auto& r : plan.regions)
                    if (r.id >= 90000 && r.kind == "settlement_facility" && r.role == "street_lamp" && r.x0 >= o->x0 && r.x1 <= o->x1 && r.y0 >= o->y0 && r.y1 <= o->y1) lamps.push_back(&r);
                if (lamps.size() == 2) {
                    const bool horizontal = lamps[0]->y0 == lamps[1]->y0;  // same row: the axis runs east-west
                    const auto* lo = (horizontal ? lamps[0]->x0 < lamps[1]->x0 : lamps[0]->y0 < lamps[1]->y0) ? lamps[0] : lamps[1];
                    const auto* hi = lo == lamps[0] ? lamps[1] : lamps[0];
                    if (horizontal || lamps[0]->x0 == lamps[1]->x0) {
                        Require(lo->rotation == (horizontal ? 3 : 0) && hi->rotation == (horizontal ? 1 : 2), context + ": the lamps of a grass garden do not face each other");
                        ++tally.facingLamps;
                    }
                }
            }
            // (the grass garden has flower beds and no trees; the forest garden is the one that must have its trees)
            if (o->role == "yard_garden_grass") Require(trees == 0 || (o->x1 - o->x0 + 1 < 14 || o->y1 - o->y0 + 1 < 14), context + ": a grass garden with flower beds has trees");
            if (o->role == "yard_garden_forest") { ++tally.gardens; tally.treedGardens += trees > 0; }
        }
        for (const auto* room : rooms) {
            if (room->role == "annex_jail_cell") {
                const MapPlanRegion* bed = nullptr; const MapPlanRegion* bucket = nullptr;
                for (const auto& r : plan.regions) {
                    if (r.id < 90000 || r.kind != "castle_fixture" || r.parentId != room->id) continue;
                    if (r.role == "bed") bed = &r;
                    if (r.role == "water_bucket") bucket = &r;
                }
                if (bed != nullptr) Require(bed->x0 == room->x0 && bed->y0 == room->y0, context + ": the bed of a jail cell is not at the back left");
                if (bucket != nullptr) Require(bucket->x1 == room->x1 && bucket->y0 == room->y0, context + ": the bucket of a jail cell is not at the back right");
            }
        }
        for (const auto& d : plan.regions) {
            if (d.kind != "castle_door" || d.id >= 80000) continue;
            for (const auto* room : rooms) {
                if (room->role != "throne_room" || (room->id != d.parentId && room->id != d.linkedRegionId)) continue;
                const bool front = d.rotation % 2 == 0 && d.y0 >= room->y1;
                Require((d.role == "ceremonial_door") == front, context + ": a door of the throne room is royal, or not, against the rule (" + d.role + ")");
            }
        }
    }
    // Doors like the keep's: every annex door (exterior and between rooms) is a castle_door 4 wide with two stone jambs.
    {
        std::vector<const MapPlanRegion*> castleDoors;
        for (const auto& r : plan.regions) if (r.kind == "castle_door" && r.id >= 80000) castleDoors.push_back(&r);
        for (const auto* d : castleDoors) {
            const int span = std::max(d->x1 - d->x0, d->y1 - d->y0) + 1, thick = std::min(d->x1 - d->x0, d->y1 - d->y0) + 1;
            Require(span == 4 && thick == 1, context + ": an annex door is not 4 wide");
            int jambs = 0;
            for (const auto& r : plan.regions) jambs += r.kind == "castle_portal" && r.parentId == d->id;
            Require(jambs == 2, context + ": an annex door has " + std::to_string(jambs) + " jambs");
            ++tally.castleDoors;
        }
        // One door region per exterior door and per doorway between rooms of an annex.
        int expected = 0;
        for (const auto& r : plan.regions) expected += r.kind == "lot_annex_door";
        for (const auto* o : outlines) if (o->kind == "lot_annex")
            for (const auto& r : plan.regions) expected += r.kind == "room_connection" && r.x0 > o->x0 && r.x1 < o->x1 && r.y0 > o->y0 && r.y1 < o->y1;
        Require(static_cast<int>(castleDoors.size()) == expected, context + ": annex doors and castle_door regions differ (" + std::to_string(castleDoors.size()) + " vs " + std::to_string(expected) + ")");
        // Furniture of the lots (castle_fixture with an id from 90000): on its own ground, apart from the others and from the doors.
        std::vector<const MapPlanRegion*> fixtures;
        for (const auto& r : plan.regions) if (r.kind == "castle_fixture" && r.id >= 90000) fixtures.push_back(&r);
        for (std::size_t i = 0; i < fixtures.size(); ++i) {
            const MapPlanRegion& f = *fixtures[i];
            Require(f.parentIndex >= 0 && f.parentIndex < static_cast<int>(plan.regions.size()) && plan.regions[static_cast<std::size_t>(f.parentIndex)].id == f.parentId,
                context + ": a fixture of " + f.role + " has no parent");
            const MapPlanRegion& parent = plan.regions[static_cast<std::size_t>(f.parentIndex)];
            Require(f.x0 >= parent.x0 && f.x1 <= parent.x1 && f.y0 >= parent.y0 && f.y1 <= parent.y1, context + ": a " + f.role + " lies outside its " + parent.role);
            if (parent.kind == "room")
                for (int y = f.y0; y <= f.y1; ++y) for (int x = f.x0; x <= f.x1; ++x)
                    Require(biome(x, y) == TOWN_STONE_FLOOR, context + ": a " + f.role + " stands off the floor of " + parent.role);
            for (std::size_t j = i + 1; j < fixtures.size(); ++j) {
                const MapPlanRegion& g = *fixtures[j];
                Require(f.x1 < g.x0 || g.x1 < f.x0 || f.y1 < g.y0 || g.y1 < f.y0, context + ": " + f.role + " and " + g.role + " overlap");
            }
            for (const auto* d : castleDoors)
                Require(f.x1 < d->x0 - 3 || f.x0 > d->x1 + 3 || f.y1 < d->y0 - 3 || f.y0 > d->y1 + 3, context + ": a " + f.role + " blocks a door landing");
            ++tally.fixtures;
        }
    }
    // Planting and crops of the outdoor facilities (settlement_prop / flower / plant / crop with an id from 90000): inside a facility,
    // on its ground, and clear of the solid things (furniture and props), which keep apart from each other.
    {
        std::vector<const MapPlanRegion*> solids, lights;
        for (const auto& r : plan.regions) {
            if (r.id < 90000) continue;
            if (r.kind == "settlement_prop" || r.kind == "castle_fixture" || r.kind == "settlement_facility") solids.push_back(&r);
            if (r.kind == "settlement_flower" || r.kind == "settlement_plant" || r.kind == "settlement_crop") lights.push_back(&r);
        }
        const auto overlap = [](const MapPlanRegion& a, const MapPlanRegion& b) { return !(a.x1 < b.x0 || b.x1 < a.x0 || a.y1 < b.y0 || b.y1 < a.y0); };
        for (const auto* r : solids) {
            if (r->kind != "settlement_prop") continue;
            ++tally.props;
            // The editor places a prop unturned and skips one whose size differs from the registered asset's: the sizes must match exactly.
            {
                const std::map<std::string, std::pair<int, int>> sizes = {{"conifer", {6, 5}}, {"shade_tree", {6, 6}}, {"palm", {5, 4}}, {"shrub", {4, 4}}, {"rock", {2, 2}}};
                const auto it = sizes.find(r->role);
                if (it != sizes.end()) Require(r->x1 - r->x0 + 1 == it->second.first && r->y1 - r->y0 + 1 == it->second.second, context + ": a " + r->role + " has a size the editor would not place");
            }
            const MapPlanRegion* home = nullptr;
            for (const auto* o : outlines) if (o->kind == "lot_facility" && r->x0 >= o->x0 && r->x1 <= o->x1 && r->y0 >= o->y0 && r->y1 <= o->y1) home = o;
            Require(home != nullptr, context + ": a " + r->role + " lies outside every facility");
        }
        for (std::size_t i = 0; i < solids.size(); ++i) for (std::size_t j = i + 1; j < solids.size(); ++j)
            Require(!overlap(*solids[i], *solids[j]), context + ": " + solids[i]->role + " and " + solids[j]->role + " overlap");
        for (const auto* l : lights) {
            const MapPlanRegion* home = nullptr;
            for (const auto* o : outlines) if (o->kind == "lot_facility" && l->x0 >= o->x0 && l->x1 <= o->x1 && l->y0 >= o->y0 && l->y1 <= o->y1) home = o;
            Require(home != nullptr, context + ": a " + l->role + " lies outside every facility");
            for (const auto* sd : solids) Require(!overlap(*l, *sd), context + ": a " + l->role + " lies under a " + sd->role);
            ++(l->kind == "settlement_crop" ? tally.crops : tally.flowers);
        }
    }
    for (const auto* r : roads) {
        ++tally.roads;
        const int w = r->x1 - r->x0 + 1, h = r->y1 - r->y0 + 1;
        // (a straight road is 2 wide; an L is two such pieces, so the region is its bounding box: only the length is bounded)
        Require(std::min(w, h) >= 2 && w + h <= 2 + 20 + 2, context + ": an annex road has the wrong size (" + std::to_string(w) + "x" + std::to_string(h) + ")");
    }
}
}  // namespace

int main() {
    try {
        // Same seed, same facilities.
        {
            const MapPlan a = Make(128, 0, 3), b = Make(128, 0, 3);
            std::string da, db;
            for (const auto& r : a.regions) if (r.kind.rfind("lot_", 0) == 0) da += r.kind + r.role + std::to_string(r.x0) + "," + std::to_string(r.y0) + ";";
            for (const auto& r : b.regions) if (r.kind.rfind("lot_", 0) == 0) db += r.kind + r.role + std::to_string(r.x0) + "," + std::to_string(r.y0) + ";";
            Require(da == db, "the facilities are not deterministic");
        }
        Tally all;
        const unsigned seeds = QuickSeeds(40);
        for (const int size : {128, 160}) for (const int mode : {0, 1, 2, 3, 4}) for (unsigned seed = 1; seed <= seeds; ++seed) {
            const std::string context = "size=" + std::to_string(size) + " mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
            Check(Make(size, mode, seed), mode, context, all);
        }
        std::cout << "  castles=" << all.castles << " outdoor=" << all.outdoor << " annex=" << all.annex << " mismatch=" << all.mismatch
                  << " courtyard=" << all.courtyard << " roads=" << all.roads << " grass gardens=" << all.grass << " forest gardens=" << all.forest << " jails=" << all.jails << " row houses=" << all.rowhouses << " doors=" << all.castleDoors << " fixtures=" << all.fixtures << " props=" << all.props << " flowers=" << all.flowers << " crops=" << all.crops << " archery=" << all.archery << " chair pairs=" << all.chairPairs << " stacks=" << all.stacks << " tops=" << all.tops << " lamps=" << all.lamps << " statues=" << all.statues << " wells=" << all.wells << " treed gardens=" << all.treedGardens << "/" << all.gardens << '\n';
        for (const auto& [role, count] : all.byRole) std::cout << "    " << role << " " << count << '\n';
        if (!QuickMode()) {
            Require(all.outdoor > 0 && all.annex > 0, "no outdoor facility or no annex was generated");
            Require(all.grass > 0 && all.forest > 0, "one of the two gardens never appears");
            Require(all.jails > 0 && all.twoRoom > 0, "no jail, or no two-room annex");
            Require(all.rowhouses > 0, "no row house was generated");
            Require(all.castleDoors > 0 && all.fixtures > 0, "no annex door or no lot furniture was generated");
            Require(all.chairPairs > 0, "no table with a pair of chairs was generated");
            Require(all.props > 0 && all.flowers > 0 && all.crops > 0, "no tree or shrub, no flower, or no crop was planted");
            Require(all.courtyard > 0, "no courtyard facility was generated");
            Require(all.lamps > 0 && all.statues > 0 && all.wells > 0, "no lamp, no statue or no well was placed");
            Require(all.facingLamps > 0, "no grass garden had its two lamps checked for facing each other");
            Require(all.stacks > 0, "no barrel was ever stacked");
            Require(all.treedGardens * 100 >= all.gardens * 85, "too many gardens have no tree");
            Require(all.roads > 0, "no annex road was ever drawn");
            // A facility that does not suit the castle type is a quarter as likely: they are a minority.
            Require(all.mismatch * 100 <= (all.outdoor + all.annex) * 30, "too many facilities do not suit their castle type");
            // Nothing is mandatory.
            for (const auto& [role, count] : all.byRole) Require(count * 100 <= all.castles * 90, role + " appears in nearly every castle");
        }
        std::cout << "CASTLE_LOT_FACILITY: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_LOT_FACILITY: FAIL " << e.what() << '\n';
        return 1;
    }
}
