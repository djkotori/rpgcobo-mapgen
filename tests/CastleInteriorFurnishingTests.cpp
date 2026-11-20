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
        std::cout << "castle interior furnishing tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
