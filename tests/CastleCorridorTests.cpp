#include "CastleMapImageGenerator.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Corridors (docs/dig/2026-11-17-castle-corridors.md):
//  - A corridor is a room with role "corridor", not counted in the requested room count.
//    Every corridor is 8 across (7 floor cells), whether vertical, horizontal or the Hill band
//    corridor (their end doors are 4 wide), and at least 12 long.
//  - Hill's band of small rooms between the entrance hall and the antechamber gets a corridor
//    through its middle, joined to both of them.
//  - A wing (rooms stacked flush against the central block) gets a vertical corridor along the
//    central block when it holds three rooms or more and reaches the entrance hall.
//  - A horizontal corridor runs out of the central block along a boundary between two rows of
//    rooms (rooms above and below it open onto it). It opens into the antechamber or the entrance
//    hall or the throne room (never the private suite); where a wing has a vertical corridor too,
//    the horizontal one joins it through an opened wall (a "corridor_junction", no door).
//  - Rooms beside a corridor open onto it and have no door to each other.
//  - No dead end: the corridor does not run on for more than 6 cells past its last door (the end
//    room takes the space back), unless that would leave it serving fewer than two rooms.
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
    return role.rfind("royal_", 0) == 0 || role == "queens_bedchamber";  // (the throne room is not private to a corridor: it may open onto one)
}

struct Stats { int castles = 0, withCorridor = 0, bands = 0, vertical = 0, horizontal = 0, junctions = 0, unfurnished = 0, longCorridors = 0, deadEnds = 0; };

void CheckCorridors(const MapPlan& plan, const std::string& context, Stats& stats) {
    std::vector<const MapPlanRegion*> rooms, corridors;
    for (const auto& r : plan.regions) {
        if (r.kind != "room") continue;
        rooms.push_back(&r);
        if (r.role == "corridor") corridors.push_back(&r);
    }
    ++stats.castles;
    if (corridors.empty()) return;
    ++stats.withCorridor;
    // Every corridor has at least three doors: onto its rooms (room_connection) or, where it has no room to open into, out
    // of the castle (castle_side_door). A corridor that serves fewer rooms is not worth its width.
    for (const auto* c : corridors) {
        int doors = 0;
        for (const auto& d : plan.regions)
            doors += (d.kind == "room_connection" && (d.parentId == c->id || d.linkedRegionId == c->id)) || (d.kind == "castle_side_door" && d.parentId == c->id);
        Require(doors >= 3, context + ": a corridor has only " + std::to_string(doors) + " doors (" + std::to_string(c->x0) + "," + std::to_string(c->y0) + ".." + std::to_string(c->x1) + "," + std::to_string(c->y1) + ")");
    }
    bool unfurnished = false;
    for (const auto& h : plan.castleHistory) unfurnished |= h.change.rfind("unfurnished", 0) == 0;
    stats.unfurnished += unfurnished;
    const auto hasDoor = [&](int a, int b) {
        for (const auto& d : plan.regions)
            if (d.kind == "room_connection" && ((d.parentId == a && d.linkedRegionId == b) || (d.parentId == b && d.linkedRegionId == a))) return true;
        return false;
    };
    const auto joined = [&](const MapPlanRegion& c) {
        for (const auto& j : plan.regions)
            if (j.kind == "corridor_junction" && (j.parentId == c.id || j.linkedRegionId == c.id)) return true;
        return false;
    };
    const auto opensToThrone = [&](const MapPlanRegion& c) {
        // (the throne room, or a room of the private suite for a corridor on the wing's window side)
        for (const auto* r : rooms) if ((r->role == "throne_room" || Private(r->role)) && hasDoor(c.id, r->id)) return true;
        return false;
    };
    const MapPlanRegion* hall = nullptr;
    const MapPlanRegion* ante = nullptr;
    for (const auto* r : rooms) { if (r->role == "entrance_hall") hall = r; if (r->role == "antechamber") ante = r; }
    if (hall == nullptr || ante == nullptr) {
        // A castle with no throne room (so no antechamber) may have the spine the planner draws at the back of its axis: the
        // small rooms beside it open onto it, and it is not one of the corridors counted below.
        for (const auto* c : corridors) {
            const bool vertical = c->y1 - c->y0 > c->x1 - c->x0;
            int beside = 0;
            for (const auto* r : rooms) {
                // (The suite behind the throne room only: the catalog's royal_chambers and royal_chapel are ordinary rooms here.)
                const bool suiteRoom = r->role == "throne_room" || r->role == "royal_bedchamber" || r->role == "royal_dressing_room" || r->role == "royal_study" ||
                    r->role == "royal_children_chamber" || r->role == "royal_treasury" || r->role == "queens_bedchamber";
                if (r == c || r->role == "corridor" || suiteRoom) continue;
                const int rows = std::min(c->y1, r->y1) - std::max(c->y0, r->y0) + 1;
                const int columns = std::min(c->x1, r->x1) - std::max(c->x0, r->x0) + 1;
                if (!(vertical ? ((c->x1 + 1 == r->x0 || r->x1 + 1 == c->x0) && rows >= 4) : ((c->y1 + 1 == r->y0 || r->y1 + 1 == c->y0) && columns >= 4))) continue;
                if (hall != nullptr && r == hall) continue;
                ++beside;
                Require(hasDoor(c->id, r->id), context + ": " + r->role + " beside a corridor has no door onto it");
            }
            if (beside < 2) {
                std::string dump;
                for (const auto* r : rooms) dump += " [" + r->role + " " + std::to_string(r->x0) + "," + std::to_string(r->y0) + ".." + std::to_string(r->x1) + "," + std::to_string(r->y1) + "]";
                Require(false, context + ": a corridor serves fewer than two rooms (" + std::to_string(c->x0) + "," + std::to_string(c->y0) + ".." + std::to_string(c->x1) + "," + std::to_string(c->y1) + ")" + dump);
            }
        }
        return;
    }
    for (const auto* c : corridors) {
        const int w = c->x1 - c->x0 + 1, h = c->y1 - c->y0 + 1;
        const bool vertical = h > w;
        const bool band = vertical && c->y0 == ante->y1 + 1 && c->y1 + 1 == hall->y0;
        const int thickness = std::min(w, h);
        Require(thickness == 8 && std::max(w, h) >= 12, context + ": a corridor has the wrong size (" +
            std::to_string(w) + "x" + std::to_string(h) + ")");
        // Floor cells across, in the middle of its length (the first line is the shared wall).
        const int mid = vertical ? (c->y0 + c->y1) / 2 : (c->x0 + c->x1) / 2;
        for (int k = 1; k < thickness; ++k) {
            const int x = vertical ? c->x0 + k : mid, y = vertical ? mid : c->y0 + k;
            const std::uint8_t b = plan.biomes[static_cast<std::size_t>(y * plan.width + x)];
            Require(b == TOWN_STONE_FLOOR || b == BRIDGE, context + ": the corridor floor is not stone (biome " + std::to_string(b) + ")");
        }
        stats.bands += band;
        stats.vertical += vertical && !band;
        stats.horizontal += !vertical;
        // Where it opens: a vertical wing corridor into the hall; the band into the hall and the
        // antechamber; a horizontal one into the antechamber or the hall, unless it joins a
        // vertical corridor of the same wing.
        if (band) {
            Require(hasDoor(c->id, hall->id) && hasDoor(c->id, ante->id), context + ": the band corridor is not joined to the hall and the antechamber");
        } else if (vertical) {
            Require(hasDoor(c->id, hall->id), context + ": the corridor does not open into the entrance hall");
        } else {
            Require(joined(*c) || hasDoor(c->id, hall->id) || hasDoor(c->id, ante->id) || opensToThrone(*c),
                context + ": the horizontal corridor opens nowhere");
        }
        // The rooms along its long sides (not the ones at its ends).
        std::vector<const MapPlanRegion*> beside;
        for (const auto* r : rooms) {
            if (r == c) continue;
            const int rows = std::min(c->y1, r->y1) - std::max(c->y0, r->y0) + 1;
            const int columns = std::min(c->x1, r->x1) - std::max(c->x0, r->x0) + 1;
            const bool alongSide = vertical ? ((c->x1 + 1 == r->x0 || r->x1 + 1 == c->x0) && rows >= 4)
                                            : ((c->y1 + 1 == r->y0 || r->y1 + 1 == c->y0) && columns >= 4);
            // (Only a horizontal corridor along the wing's outer edge, the window side, may open into the private suite.)
            if (Private(r->role) && vertical) Require(!hasDoor(c->id, r->id), context + ": a corridor opens into " + r->role);
            if (!alongSide || r == hall || r == ante || Private(r->role) || r->role == "corridor") continue;
            beside.push_back(r);
            Require(hasDoor(c->id, r->id), context + ": " + r->role + " beside a corridor has no door onto it");
        }
        Require(beside.size() >= 2, context + ": a corridor serves fewer than two rooms");
        for (std::size_t i = 0; i < beside.size(); ++i) for (std::size_t j = i + 1; j < beside.size(); ++j)
            Require(!hasDoor(beside[i]->id, beside[j]->id), context + ": " + beside[i]->role + " and " + beside[j]->role +
                " beside a corridor have a door to each other");
        // No long dead end past the last door (the band's ends are the hall and the antechamber).
        // A corridor keeps its length when taking the end room back would cut it off from the hall,
        // so a few remain: they are counted and bounded below.
        if (!band && beside.size() >= 3) {
            int first = 1 << 30, last = -(1 << 30);
            for (const auto& d : plan.regions) {
                if (d.kind != "room_connection" || (d.parentId != c->id && d.linkedRegionId != c->id)) continue;
                first = std::min(first, vertical ? d.y0 : d.x0);
                last = std::max(last, vertical ? d.y1 : d.x1);
            }
            const int start = vertical ? c->y0 : c->x0, end = vertical ? c->y1 : c->x1;
            ++stats.longCorridors;
            if (first - (start + 1) > 6 || end - last > 6) { ++stats.deadEnds; if (std::getenv("CORRIDOR_DEBUG")) std::cout << "  dead end: " << context << (vertical ? " vertical" : " horizontal") << " lead=" << first - (start + 1) << " tail=" << end - last << " beside=" << beside.size() << " c=" << c->x0 << "," << c->y0 << ".." << c->x1 << "," << c->y1 << '\n'; }
        }
    }
    for (const auto& j : plan.regions) {
        if (j.kind != "corridor_junction") continue;
        ++stats.junctions;
        const int length = std::max(j.x1 - j.x0, j.y1 - j.y0) + 1;
        Require(length >= 5, context + ": a corridor junction is narrower than 5 cells");
        for (int y = j.y0; y <= j.y1; ++y) for (int x = j.x0; x <= j.x1; ++x)
            Require(plan.biomes[static_cast<std::size_t>(y * plan.width + x)] == TOWN_STONE_FLOOR,
                context + ": a corridor junction is not an open stone floor");
    }
    for (const auto* c : corridors) {
        int lights = 0;
        for (const auto& f : plan.regions) lights += f.kind == "castle_fixture" && f.parentId == c->id && f.role == "corridor_candelabrum";
        // Doors and their clearance take most of a short corridor; a long one has room for lights.
        const int length = std::max(c->x1 - c->x0, c->y1 - c->y0) + 1;
        if (length >= 36 && !unfurnished) Require(lights > 0, context + ": the long corridor has no candelabra");
    }
}
}  // namespace

int main() {
    try {
        std::array<Stats, 5> stats{};
        for (const int size : {128, 160}) for (const int mode : {0, 1, 2, 3, 4}) for (unsigned seed = 1; seed <= 24; ++seed) {
            const std::string context = "size=" + std::to_string(size) + " mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
            CheckCorridors(Make(size, mode, seed), context, stats[static_cast<std::size_t>(mode)]);
        }
        Stats all;
        for (const int mode : {0, 1, 2, 3, 4}) {
            const Stats& s = stats[static_cast<std::size_t>(mode)];
            std::cout << "  mode=" << mode << " castles=" << s.castles << " with corridor=" << s.withCorridor << " bands=" << s.bands
                      << " vertical=" << s.vertical << " horizontal=" << s.horizontal << " junctions=" << s.junctions
                      << " unfurnished=" << s.unfurnished << '\n';
            all.castles += s.castles; all.withCorridor += s.withCorridor; all.horizontal += s.horizontal;
            all.junctions += s.junctions; all.unfurnished += s.unfurnished; all.longCorridors += s.longCorridors; all.deadEnds += s.deadEnds;
        }
        // Hill's band of small rooms gets its corridor in most castles; wing corridors appear
        // in the other types, a fortress less often than a palace.
        Require(stats[1].bands * 2 >= stats[1].castles, "Hill's band corridor is missing in most castles");
        // (A vertical wing corridor lies along the entrance hall's side: only a castle whose front room is a full-width entrance hall can have
        // one, which is under half of them.)
        Require(stats[0].vertical + stats[3].vertical > 0, "no vertical wing corridor was generated for concentric or palace castles");
        // (Only a castle with a full-width entrance hall is counted, and a palace has few of them now that the front can also be a
        // vestibule or a corridor; a fortress, plain as it is, may still have a few more than that.)
        Require(stats[4].vertical + stats[4].horizontal <= stats[3].vertical + stats[3].horizontal + 8, "fortress castles have far more wing corridors than palaces");
        Require(stats[2].horizontal > 0 && stats[3].horizontal > 0, "no horizontal corridor was generated for river or palace castles");
        Require(all.horizontal >= all.castles / 20, "horizontal corridors are too rare");
        Require(all.junctions > 0, "no vertical and horizontal corridor ever joined");
        std::cout << "  corridors with 3+ rooms=" << all.longCorridors << " with a long dead end=" << all.deadEnds << '\n';
        // About a third keep one: taking the end room back would cut a vertical corridor off from the hall, or leave a
        // horizontal corridor with rooms on one side only (see the dig log).
        Require(all.deadEnds * 2 <= all.longCorridors, "too many corridors keep a long dead end");
        // Furnishing is not guaranteed for every castle with corridors (the generator keeps a
        // furnished candidate when it can), but it must not be the usual outcome.
        Require(all.unfurnished * 4 <= all.withCorridor, "too many castles with corridors could not be furnished");
        std::cout << "CASTLE_CORRIDOR: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_CORRIDOR: FAIL " << e.what() << '\n';
        return 1;
    }
}
