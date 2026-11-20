#include "CastleMapImageGenerator.h"
#include "TestScale.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Horizontal corridors on a wing's window side, second corridors, and openings to the throne room or the private suite
// (docs/dig/2026-11-17-castle-corridors.md).
//  - A "window-side" corridor runs along the wing's outer edge, with rooms on one side only (the other side is the castle's
//    outside wall). Every room beside it opens onto it, and the rooms are at least kWindowCorridorMinRoomDepth deep.
//  - A corridor between two rows of rooms has rooms on both sides.
//  - A wing may hold a second horizontal corridor. Corridors never overlap.
//  - A horizontal corridor may open into the throne room, and a window-side one into a private-suite room. Any other
//    corridor never opens into a private-suite room.
// The sweep checks per-castle invariants for every sample; how often each form appears is checked in the full run only.
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

MapPlan Make(int size, int mode, unsigned seed) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"exteriorTerrain", 0}, {"natureSideCount", 0}};
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    Require(generator.Generate(plan, error), "size=" + std::to_string(size) + " mode=" + std::to_string(mode) +
        " seed=" + std::to_string(seed) + ": " + error);
    return plan;
}

bool Private(const std::string& role) {
    // (The suite behind the throne room only: the catalog's royal_chambers and royal_chapel are ordinary rooms here.)
    return role == "royal_bedchamber" || role == "royal_dressing_room" || role == "royal_study" ||
        role == "royal_children_chamber" || role == "royal_treasury" || role == "queens_bedchamber";
}

struct Tally { int castles = 0, top = 0, bottom = 0, between = 0, twoHorizontal = 0, throneDoors = 0, suiteDoors = 0; };

constexpr int kMinRoomDepth = 8;  // kWindowCorridorMinRoomDepth (CastleOdds.h)

void Check(const MapPlan& plan, const std::string& context, Tally& tally) {
    std::vector<const MapPlanRegion*> rooms, corridors;
    for (const auto& r : plan.regions) {
        if (r.kind != "room") continue;
        rooms.push_back(&r);
        if (r.role == "corridor") corridors.push_back(&r);
    }
    ++tally.castles;
    const auto hasDoor = [&](int a, int b) {
        for (const auto& d : plan.regions)
            if (d.kind == "room_connection" && ((d.parentId == a && d.linkedRegionId == b) || (d.parentId == b && d.linkedRegionId == a))) return true;
        return false;
    };
    // No two corridors overlap.
    for (std::size_t i = 0; i < corridors.size(); ++i) for (std::size_t j = i + 1; j < corridors.size(); ++j) {
        const auto* a = corridors[i]; const auto* b = corridors[j];
        Require(a->x1 < b->x0 || b->x1 < a->x0 || a->y1 < b->y0 || b->y1 < a->y0, context + ": two corridors overlap");
    }
    int horizontal = 0;
    for (const auto* c : corridors) {
        const bool vertical = c->y1 - c->y0 > c->x1 - c->x0;
        // A vertical corridor (and the Hill band) never opens into the private suite.
        for (const auto* r : rooms)
            if (vertical && Private(r->role)) Require(!hasDoor(c->id, r->id), context + ": a vertical corridor opens into " + r->role);
        if (vertical) continue;
        ++horizontal;
        std::vector<const MapPlanRegion*> above, below;  // rooms along its top and bottom walls
        for (const auto* r : rooms) {
            if (r == c || r->role == "corridor") continue;
            const int columns = std::min(c->x1, r->x1) - std::max(c->x0, r->x0) + 1;
            if (columns < 4) continue;
            if (r->y1 + 1 == c->y0) above.push_back(r);
            else if (c->y1 + 1 == r->y0) below.push_back(r);
        }
        for (const auto* r : rooms) {
            if (!hasDoor(c->id, r->id)) continue;
            tally.throneDoors += r->role == "throne_room";
        }
        const bool windowSide = above.empty() != below.empty();
        if (!windowSide) {
            tally.between += !above.empty() && !below.empty();
            // Between two rows: no private-suite room is entered from the side.
            for (const auto* r : rooms)
                if (Private(r->role)) Require(!hasDoor(c->id, r->id), context + ": a corridor between two rows opens into " + r->role);
            continue;
        }
        (above.empty() ? tally.top : tally.bottom) += 1;
        // (A private-suite room lies against the corridor's inner end, not beside it.)
        for (const auto* r : rooms) if (Private(r->role) && hasDoor(c->id, r->id)) ++tally.suiteDoors;
        const auto& side = above.empty() ? below : above;
        for (const auto* r : side) {
            const int depth = r->y1 - r->y0 + 1;
            if (r->role != "entrance_hall" && r->role != "antechamber" && !Private(r->role))
                Require(hasDoor(c->id, r->id), context + ": " + r->role + " beside a window-side corridor has no door onto it");
            if (r->role != "entrance_hall" && r->role != "antechamber" && r->role != "throne_room")
                Require(depth >= kMinRoomDepth, context + ": a room " + std::to_string(depth) + " deep lies beside a window-side corridor");
        }
    }
    tally.twoHorizontal += horizontal >= 2;
}
}  // namespace

int main() {
    try {
        Tally all;
        const unsigned seeds = QuickSeeds(60);
        for (const int mode : {0, 2, 3, 4}) for (unsigned seed = 1; seed <= seeds; ++seed) {
            const std::string context = "mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
            Tally one;
            Check(Make(128, mode, seed), context, one);
            all.castles += one.castles; all.top += one.top; all.bottom += one.bottom; all.between += one.between;
            all.twoHorizontal += one.twoHorizontal; all.throneDoors += one.throneDoors; all.suiteDoors += one.suiteDoors;
        }
        std::cout << "  castles=" << all.castles << " top=" << all.top << " bottom=" << all.bottom << " between=" << all.between
                  << " two horizontal=" << all.twoHorizontal << " throne doors=" << all.throneDoors << " suite doors=" << all.suiteDoors << '\n';
        if (!QuickMode()) {
            Require(all.top > 0 && all.bottom > 0, "no window-side corridor on one of the wing's two sides");
            Require(all.between > 0, "no corridor between two rows of rooms");
            Require(all.twoHorizontal > 0, "no wing with a second horizontal corridor");
            Require(all.throneDoors > 0, "no corridor opens into the throne room");
            Require(all.suiteDoors > 0, "no window-side corridor opens into a private-suite room");
        }
        std::cout << "CASTLE_WINDOW_CORRIDOR: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_WINDOW_CORRIDOR: FAIL " << e.what() << '\n';
        return 1;
    }
}
