#include "CastleMapImageGenerator.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// The editor places a castle's doors and furniture from the Plan, so a castle with bare
// rooms and bare partitions is a defect. Every generated castle must carry:
//  - one room_connection per partition door, each 4 cells wide and 1 deep, set into the
//    partition between the two rooms it joins, with a castle_door asset on it;
//  - castle_fixture regions inside the rooms (a throne in the throne room) that do not
//    overlap each other or any door;
//  - no raised "furniture" floor tiles (those stay unused).
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool Overlap(const MapPlanRegion& a, const MapPlanRegion& b) {
    return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1;
}

int g_doorsteps = 0, g_pavedSteps = 0, g_chapels = 0, g_threeWide = 0, g_pews = 0, g_fourWide = 0;  // (g_threeWide counts the 2-wide chapel strips)

// A chapel's altar stands on the wall opposite its entrance: the back wall, unless a door is there. A door in the top wall turns the
// chapel a quarter turn right (the altar on the right wall); doors in the top and right walls turn it left (the altar on the left wall);
// doors in the top, left and right walls leave it as it is. The pews are aligned in rows, all facing the altar, and the carpet strips
// are 2 cells wide (an antechamber's are 4 wide).
void CheckChapels(const MapPlan& plan, const std::string& context) {
    for (const auto& room : plan.regions) {
        if (room.kind == "room" && room.role == "antechamber")
            for (const auto& f : plan.regions)
                if (f.parentId == room.id && f.kind == "castle_surface" && f.role == "royal_carpet") {
                    Require(std::min(f.x1 - f.x0, f.y1 - f.y0) + 1 <= 4, context + ": an antechamber carpet strip is wider than 4 cells");
                    g_fourWide += std::min(f.x1 - f.x0, f.y1 - f.y0) + 1 == 4;
                }
        if (room.kind != "room" || (room.role.find("chapel") == std::string::npos && room.role.find("shrine") == std::string::npos)) continue;
        if (room.role.rfind("annex_", 0) == 0) continue;
        bool top = false, right = false, left = false;
        for (const auto& d : plan.regions) {
            if (d.kind != "castle_door" || (d.parentId != room.id && d.linkedRegionId != room.id)) continue;
            if (d.rotation % 2 == 0) top |= d.y0 <= room.y0 + 1;
            else (d.x0 <= room.x0 + 1 ? left : right) = true;
        }
        const MapPlanRegion* altar = nullptr;
        std::vector<const MapPlanRegion*> pews;
        for (const auto& f : plan.regions) {
            if (f.parentId != room.id) continue;
            if (f.kind == "castle_fixture" && f.role == "chapel_altar") altar = &f;
            if (f.kind == "castle_fixture" && f.role == "chapel_pew") pews.push_back(&f);
            if (f.kind == "castle_surface" && f.role == "royal_carpet") {
                Require(std::min(f.x1 - f.x0, f.y1 - f.y0) + 1 <= 2, context + ": a chapel carpet strip is wider than 2 cells");
                g_threeWide += std::min(f.x1 - f.x0, f.y1 - f.y0) + 1 == 2;
            }
        }
        if (altar == nullptr) continue;
        const int dist[4] = {altar->y0 - room.y0, room.x1 - altar->x1, room.y1 - altar->y1, altar->x0 - room.x0};  // top, right, bottom, left
        int side = 0, ties = 0;
        for (int i = 1; i < 4; ++i) if (dist[i] < dist[side]) side = i;
        for (int i = 0; i < 4; ++i) ties += dist[i] == dist[side];
        if (ties > 1) continue;
        const int expected = !top ? 0 : right ? (left ? 0 : 3) : 1;  // 0 top, 1 right, 3 left
        Require(side == expected, context + ": the chapel altar is on the wrong wall (side " + std::to_string(side) + ", expected " + std::to_string(expected) + ")");
        ++g_chapels;
        const int faces = side == 0 ? 2 : side == 1 ? 3 : 1;  // the pews face the altar
        for (const auto* pew : pews) { Require(pew->rotation == faces, context + ": a chapel pew does not face the altar"); ++g_pews; }
    }
}

void Check(int size, int mode, unsigned seed) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"lotFacilities", 0}};
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    const std::string context = "size=" + std::to_string(size) + " mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
    Require(generator.Generate(plan, error), context + ": " + error);

    int rooms = 0, connections = 0, sideDoors = 0, doors = 0, fixtures = 0;
    bool throne = false, thronePlaced = false;
    for (const auto& r : plan.regions) {
        if (r.kind == "room") { ++rooms; throne |= r.role == "throne_room"; }
        if (r.kind == "room_connection") {
            ++connections;
            const int w = r.x1 - r.x0 + 1, d = r.y1 - r.y0 + 1;
            Require(r.rotation % 2 ? (w == 1 && d == 4) : (w == 4 && d == 1),
                context + ": a partition door must be 4 wide and 1 deep");
        }
        if (r.kind == "castle_side_door") ++sideDoors;
        if (r.kind == "castle_door") { ++doors; Require(r.objectType == 1, context + ": castle_door must use the door asset"); }
        if (r.kind == "castle_fixture") {
            ++fixtures;
            thronePlaced |= r.role == "royal_throne";
            Require(r.objectType >= 0, context + ": a fixture needs an asset type");
        }
        Require(r.kind != "furniture", context + ": raised furniture floor tiles must not be output");
    }
    Require(rooms >= 2, context + ": expected a keep with rooms");
    Require(connections >= rooms - 1, context + ": every room must be joined by a partition door (" +
        std::to_string(connections) + " doors for " + std::to_string(rooms) + " rooms)");
    Require(doors == connections + sideDoors, context + ": every partition and side door must carry a door asset");
    Require(fixtures > 0, context + ": the rooms must be furnished");
    Require(!throne || thronePlaced, context + ": the throne room must hold a throne");
    CheckChapels(plan, context);
    {
        int chapels = 0;
        for (const auto& r : plan.regions) chapels += r.kind == "room" && (r.role.find("chapel") != std::string::npos || r.role.find("shrine") != std::string::npos);
        Require(chapels <= 1, context + ": " + std::to_string(chapels) + " chapels on one map");
    }
    // Outside a service door the ground is a patch of sand (a paved one on sand), never plain or forest, so the step is marked.
    {
        const bool desert = std::any_of(plan.regions.begin(), plan.regions.end(), [](const MapPlanRegion& r) { return r.kind == "natural_terrain" && r.role == "desert_flank"; });
        for (const auto& door : plan.regions) {
            if (door.kind != "castle_side_door") continue;
            const MapPlanRegion* parent = nullptr;
            for (const auto& r : plan.regions) if (r.kind == "room" && r.id == door.parentId) parent = &r;
            if (parent == nullptr) continue;
            const bool vertical = door.rotation % 2 == 1;
            const int out = vertical ? (door.x0 < (parent->x0 + parent->x1) / 2 ? -1 : 1) : (door.y0 < (parent->y0 + parent->y1) / 2 ? -1 : 1);
            const int lateral = (vertical ? door.y0 : door.x0) + 1;
            const int x = vertical ? door.x0 + out * 2 : lateral, y = vertical ? lateral : door.y0 + out * 2;
            if (x < 0 || y < 0 || x >= plan.width || y >= plan.depth) continue;
            // (where a road leads on from the door there is no patch)
            bool road = false;
            for (int depth = 2; depth <= 6 && !road; ++depth) for (int k = -1; k <= 4; ++k) {
                const int px = vertical ? door.x0 + out * depth : (door.x0) + k, py = vertical ? door.y0 + k : door.y0 + out * depth;
                if (px < 0 || py < 0 || px >= plan.width || py >= plan.depth) continue;
                const int b = plan.biomes[static_cast<std::size_t>(py * plan.width + px)];
                road |= b == ROAD || b == BRIDGE || b == TOWN_PAVEMENT;
            }
            if (road) continue;
            const int tile = plan.biomes[static_cast<std::size_t>(y * plan.width + x)];
            if (tile == PLAIN || tile == FOREST) throw std::runtime_error(context + ": the ground outside a service door was left plain or forest");
            ++g_doorsteps;
            g_pavedSteps += desert && tile == TOWN_PAVEMENT;
        }
    }

    std::vector<const MapPlanRegion*> solid;
    for (const auto& r : plan.regions)
        if (r.kind == "castle_fixture" || r.kind == "castle_door") solid.push_back(&r);
    for (std::size_t i = 0; i < solid.size(); ++i) for (std::size_t j = i + 1; j < solid.size(); ++j)
        Require(!Overlap(*solid[i], *solid[j]), context + ": " + solid[i]->role + " overlaps " + solid[j]->role);
}
}  // namespace

int main() {
    try {
        for (const int mode : {0, 1, 2, 3, 4}) for (unsigned seed = 1; seed <= 6; ++seed) Check(128, mode, seed);
        Check(128, 1, 4516451);
        Check(128, 0, 3804656);
        // A door in the throne room's back wall puts its landing over the throne's spot: the throne steps forward instead of staying bare.
        for (const int mode : {0, 1, 2, 3, 4}) for (const unsigned seed : {4u, 5u, 30u, 45u, 47u, 606u, 101u, 202u, 303u, 404u}) Check(160, mode, seed);
        Require(g_doorsteps > 0, "no service door was checked");
        Require(g_chapels > 0, "no chapel with an altar was checked");
        Require(g_threeWide > 0, "no 2-wide chapel carpet was checked");
        Require(g_fourWide > 0, "no 4-wide antechamber carpet was checked");
        std::cout << "castle interior furnishing tests passed (chapels=" << g_chapels << " pews=" << g_pews << " 2-wide=" << g_threeWide << " 4-wide=" << g_fourWide << ")\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
