#include "CastleMapImageGenerator.h"
#include "TestScale.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Two things about how a castle reads from the outside and the inside:
//  1. The antechamber (the waiting room before the throne room) is not a bedroom: it must
//     never be filled with beds, even though its role name contains "chamber".
//  2. The keep has side (service) doors onto its open ground: a castle_side_door region in
//     the outer wall of a working room (never a ceremonial one), facing open ground (not a
//     nature side, not the gate) and set with a door asset. It has no road of its own.
//  4. The way from the main gate to the throne runs straight: where the entrance hall and the antechamber
//     share a wall, a door joins them in front of the gate (on the hall's centre line), whatever other
//     doors the rooms have.
//  3. Empty land in the keep's outline (a notch, a courtyard, a corner behind the wings - anything
//     that holds an 8x8 free square, other than the court in front of the gate) has a side door
//     opening onto it, so it is not a dead pocket.
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

MapPlan Make(int size, int mode, unsigned seed, int terrain, int sides, const std::string& context) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"lotFacilities", 0}, {"exteriorTerrain", terrain}, {"natureSideCount", sides}};
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    Require(generator.Generate(plan, error), context + ": " + error);
    return plan;
}

const MapPlanRegion* RoomById(const MapPlan& plan, int id) {
    for (const auto& r : plan.regions) if (r.kind == "room" && r.id == id) return &r;
    return nullptr;
}

int TestAntechamber() {
    int antechambers = 0;
    for (const int mode : {0, 1, 2, 3, 4}) for (unsigned seed = 1; seed <= QuickSeeds(8); ++seed) {
        const std::string context = "antechamber mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
        const MapPlan plan = Make(128, mode, seed, 0, 0, context);
        for (const auto& room : plan.regions) {
            if (room.kind != "room" || room.role != "antechamber") continue;
            ++antechambers;
            int fixtures = 0;
            for (const auto& f : plan.regions) {
                if (f.kind != "castle_fixture" || f.parentId != room.id) continue;
                ++fixtures;
                Require(f.role.find("bed") == std::string::npos && f.role != "wardrobe" && f.role != "bedroom_bookcase",
                    context + ": the antechamber holds " + f.role);
            }
            bool unfurnished = false;
            for (const auto& h : plan.castleHistory) unfurnished |= h.change.rfind("unfurnished", 0) == 0;
            if (!unfurnished) Require(fixtures > 0, context + ": the antechamber is left empty");
        }
    }
    Require(antechambers > 0, "no antechamber was generated to check");
    return antechambers;
}

int TestSideDoors(int& cases, int& withDoor) {
    int doors = 0;
    for (const int size : {128, 160}) for (const int mode : {0, 1, 3, 4}) for (unsigned seed = 1; seed <= QuickSeeds(4); ++seed)
        for (const std::array<int, 2> option : {std::array<int, 2>{0, 0}, std::array<int, 2>{2, 2}, std::array<int, 2>{3, 1}}) {
            const std::string context = "side door size=" + std::to_string(size) + " mode=" + std::to_string(mode) +
                " seed=" + std::to_string(seed) + " terrain=" + std::to_string(option[0]) + " sides=" + std::to_string(option[1]);
            const MapPlan plan = Make(size, mode, seed, option[0], option[1], context);
            ++cases;
            int found = 0;
            const MapPlanMarker* gate = nullptr;
            for (const auto& m : plan.markers) if (m.role == "keep_public_entry") gate = &m;
            Require(gate != nullptr, context + ": no main entry");
            for (const auto& door : plan.regions) {
                if (door.kind != "castle_side_door") continue;
                ++found;
                const MapPlanRegion* room = RoomById(plan, door.parentId);
                Require(room != nullptr, context + ": side door without a room");
                Require(room->role != "throne_room" && room->role != "antechamber", context + ": side door on a ceremonial room (" + room->role + ")");
                // The rooms of a suite built into a wing may have one, but not those behind the throne room.
                if (room->role.rfind("royal_", 0) == 0 || room->role == "queens_bedchamber")
                    for (const auto& other : plan.regions)
                        if (other.kind == "room" && other.role == "throne_room")
                            Require(!(room->y1 <= other.y0 + 1 && room->x0 <= other.x1 && room->x1 >= other.x0), context + ": side door behind the throne room (" + room->role + ")");
                // Which way the door faces: out of the room through its wall.
                int dx = 0, dy = 0;
                if (door.rotation % 2) dx = door.x0 > room->x1 ? 1 : -1; else dy = door.y0 > room->y1 ? 1 : -1;
                const int cx = (door.x0 + door.x1) / 2, cy = (door.y0 + door.y1) / 2;
                // The wall is open through its whole thickness and the ground beyond is walkable
                // open land, not a nature side.
                for (int step = 0; step <= 4; ++step) {
                    const bool near = step <= 3;  // a stray tree may stand farther out
                    const int x = cx + dx * step, y = cy + dy * step;
                    Require(x >= 0 && y >= 0 && x < size && y < size, context + ": side door leaves the map");
                    const std::uint8_t b = plan.biomes[static_cast<std::size_t>(y * size + x)];
                    Require(b != MOUNTAIN && b != EXMOUNTAIN && b != TOWN_CLIFF && (b != FOREST || !near) && b != RIVER,
                        context + ": side door is blocked or faces nature at step " + std::to_string(step) + " (biome " + std::to_string(b) + ", door " + std::to_string(door.x0) + "," + std::to_string(door.y0) + " rot " + std::to_string(door.rotation) + " dir " + std::to_string(dx) + "," + std::to_string(dy) + ")");
                }
                Require(std::abs(cx - gate->x) + std::abs(cy - gate->y) >= 10, context + ": side door sits on the main gate");
                bool hasObject = false;
                for (const auto& d : plan.regions)
                    hasObject |= d.kind == "castle_door" && d.x0 == door.x0 && d.y0 == door.y0 && d.x1 == door.x1 && d.y1 == door.y1;
                bool unfurnished = false;
                for (const auto& h : plan.castleHistory) unfurnished |= h.change.rfind("unfurnished", 0) == 0;
                if (!unfurnished) Require(hasObject, context + ": the side door has no door asset");
            }
            doors += found;
            withDoor += found > 0;
        }
    return doors;
}
// Free land (plain ground) inside the keep's bounding box, in connected pieces; a piece that holds an
// 8x8 free square is a lot. Returns {lots, lots with a side door}.
std::pair<int, int> TestLots() {
    int lots = 0, served = 0;
    // With no nature, and with forest or mountain on two or three sides: nature stays within 10% of the
    // map, so the land beside the keep is still open and its empty lots still get their doors.
    for (const std::array<int, 2> terrain : {std::array<int, 2>{0, 0}, std::array<int, 2>{2, 3}, std::array<int, 2>{3, 2}})
    for (const int size : {128, 160}) for (const int mode : {0, 1, 2, 3, 4}) for (unsigned seed = 1; seed <= 12; ++seed) {
        const std::string context = "lot size=" + std::to_string(size) + " mode=" + std::to_string(mode) + " seed=" + std::to_string(seed) +
            " terrain=" + std::to_string(terrain[0]) + " sides=" + std::to_string(terrain[1]);
        const MapPlan plan = Make(size, mode, seed, terrain[0], terrain[1], context);
        const MapPlanRegion* keep = nullptr;
        for (const auto& r : plan.regions) if (r.kind == "keep") keep = &r;
        Require(keep != nullptr, context + ": no keep");
        const MapPlanMarker* gate = nullptr;
        for (const auto& m : plan.markers) if (m.role == "keep_public_entry") gate = &m;
        Require(gate != nullptr, context + ": no main entry");
        const auto free = [&](int x, int y) {
            return x >= keep->x0 && x <= keep->x1 && y >= keep->y0 && y <= keep->y1 &&
                plan.biomes[static_cast<std::size_t>(y * size + x)] == PLAIN;
        };
        std::vector<int> label(static_cast<std::size_t>(size * size), 0);
        int labels = 0;
        std::vector<std::vector<int>> pieces(1);
        for (int y = keep->y0; y <= keep->y1; ++y) for (int x = keep->x0; x <= keep->x1; ++x) {
            if (!free(x, y) || label[static_cast<std::size_t>(y * size + x)]) continue;
            ++labels;
            pieces.emplace_back();
            std::vector<int> stack{y * size + x};
            label[static_cast<std::size_t>(y * size + x)] = labels;
            while (!stack.empty()) {
                const int cell = stack.back(); stack.pop_back();
                pieces.back().push_back(cell);
                for (const auto& d : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                    const int nx = cell % size + d.first, ny = cell / size + d.second;
                    if (!free(nx, ny) || label[static_cast<std::size_t>(ny * size + nx)]) continue;
                    label[static_cast<std::size_t>(ny * size + nx)] = labels;
                    stack.push_back(ny * size + nx);
                }
            }
        }
        for (int piece = 1; piece <= labels; ++piece) {
            // The court in front of the gate is not a lot.
            bool front = false;
            for (const int cell : pieces[static_cast<std::size_t>(piece)])
                front |= std::abs(cell % size - gate->x) <= 4 && std::abs(cell / size - gate->y) <= 4;
            if (front) continue;
            bool square = false;
            for (const int cell : pieces[static_cast<std::size_t>(piece)]) {
                bool all = true;
                for (int dy = 0; dy < 8 && all; ++dy) for (int dx = 0; dx < 8; ++dx) {
                    const int x = cell % size + dx, y = cell / size + dy;
                    if (x >= size || y >= size || label[static_cast<std::size_t>(y * size + x)] != piece) { all = false; break; }
                }
                if (all) { square = true; break; }
            }
            if (!square) continue;
            ++lots;
            // A side door opens onto the lot: the ground out from the door, past the doorstep patch (up to 5 deep), is in it.
            bool door = false;
            for (const auto& d : plan.regions) {
                if (d.kind != "castle_side_door") continue;
                const MapPlanRegion* room = RoomById(plan, d.parentId);
                if (room == nullptr) continue;
                int dx = 0, dy = 0;
                if (d.rotation % 2) dx = d.x0 > room->x1 ? 1 : -1; else dy = d.y0 > room->y1 ? 1 : -1;
                for (int depth = 3; depth <= 8; ++depth) {
                    const int ax = (d.x0 + d.x1) / 2 + dx * depth, ay = (d.y0 + d.y1) / 2 + dy * depth;
                    if (ax >= 0 && ay >= 0 && ax < size && ay < size && label[static_cast<std::size_t>(ay * size + ax)] == piece) door = true;
                }
            }
            served += door;
            int bx0 = size, by0 = size, bx1 = 0, by1 = 0;
            for (const int cell : pieces[static_cast<std::size_t>(piece)]) {
                bx0 = std::min(bx0, cell % size); bx1 = std::max(bx1, cell % size);
                by0 = std::min(by0, cell / size); by1 = std::max(by1, cell / size);
            }
            Require(door, context + ": an empty lot of " + std::to_string(pieces[static_cast<std::size_t>(piece)].size()) +
                " cells (" + std::to_string(bx0) + "," + std::to_string(by0) + ".." + std::to_string(bx1) + "," + std::to_string(by1) +
                ") has no side door onto it");
        }
    }
    return {lots, served};
}
// The hall and the antechamber are joined by a door in front of the gate. (Hill's band of small rooms
// between them has its own corridor, tested with the corridors.)
std::pair<int, int> TestAxisDoor() {
    int adjacent = 0, joined = 0;
    for (const int size : {128, 160}) for (const int mode : {0, 2, 3, 4}) for (unsigned seed = 1; seed <= QuickSeeds(40); ++seed) {
        const std::string context = "axis size=" + std::to_string(size) + " mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
        const MapPlan plan = Make(size, mode, seed, 0, 0, context);
        const MapPlanRegion* hall = nullptr;
        const MapPlanRegion* ante = nullptr;
        for (const auto& r : plan.regions) {
            if (r.kind != "room") continue;
            if (r.role == "entrance_hall") hall = &r;
            if (r.role == "antechamber") ante = &r;
        }
        if (hall == nullptr || ante == nullptr || ante->y1 + 1 != hall->y0) continue;  // not side by side
        ++adjacent;
        bool door = false;
        for (const auto& d : plan.regions) {
            if (d.kind != "room_connection" || !((d.parentId == hall->id && d.linkedRegionId == ante->id) || (d.parentId == ante->id && d.linkedRegionId == hall->id))) continue;
            const int centre = (d.x0 + d.x1) / 2, hallCentre = (hall->x0 + hall->x1) / 2;
            door = std::abs(centre - hallCentre) <= 2;
        }
        joined += door;
        Require(door, context + ": no door joins the entrance hall and the antechamber in front of the gate");
    }
    return {adjacent, joined};
}
}  // namespace

int main() {
    try {
        const int antechambers = TestAntechamber();
        int cases = 0, withDoor = 0;
        const int doors = TestSideDoors(cases, withDoor);
        // Most castles have open ground beside the keep, so most of them get a side door.
        Require(withDoor * 10 >= cases * 7, "only " + std::to_string(withDoor) + " of " + std::to_string(cases) + " castles have a side door");
        const auto axis = TestAxisDoor();
        Require(axis.first > 0, "no castle had its hall and antechamber side by side");
        const auto lots = TestLots();
        Require(lots.first > 0, "no empty lot was found to check");
        std::cout << "CASTLE_SERVICE_DOOR: OK lots=" << lots.second << "/" << lots.first << " antechambers=" << antechambers << " doors=" << doors << " castles=" << withDoor << "/" << cases << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_SERVICE_DOOR: FAIL " << e.what() << '\n';
        return 1;
    }
}
