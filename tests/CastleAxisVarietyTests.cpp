#include "CastleMapImageGenerator.h"
#include "CastleProgram.h"
#include "MapQuality.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// No facility is certain to appear, and the throne room is long rather than wide.
//  1. The throne room (95%), the entrance hall (90%) and the antechamber, the private suite and the
//     catalog's mandatory rooms (Concentric: treasury, council chamber) are all left out of some castles.
//     When the entrance hall is left out, another hall takes the front, and the gate still opens into it.
//  2. The private suite varies: none, one room, the bedchamber/dressing room/study trio, a king's and
//     a queen's bedchamber apart, and the same with a study between.
//  3. The throne room is deeper than it is wide: the target is 2:1 (depth:width), and few castles
//     (only where the keep is too small) fall short of 1.4:1.
//  4. Every room stays reachable and every castle has a door into its front room.
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Tally {
    int castles = 0, throne = 0, hall = 0, ante = 0, suite = 0, concentric = 0, treasury = 0, councilChamber = 0;
    int bigHall = 0;                      // castles whose front room is a stand-in for the entrance hall
    std::set<std::string> suiteSets;       // the rooms of the private suite, as one string per castle
    int vaults = 0, children = 0, treasuryElsewhere = 0;
    int corridorEntry = 0;                // castles whose gate opens into a corridor, which leads to the antechamber
    int vestibule = 0;                    // castles whose gate opens into a small vestibule with a room on each side
    int wideFront = 0;                    // castles whose front room is a big hall (as wide as the antechamber)
    std::map<std::string, int> suites;    // pattern -> count
    int thrones = 0, tall = 0, veryTall = 0;
    double ratioSum = 0;
};

bool IsEntryRole(const std::string& role) { return role == "entrance_hall" || role == "great_hall" || role == "guard_hall" || role == "corridor"; }

void Check(int size, int mode, unsigned seed, Tally& tally) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}};
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    const std::string context = "size=" + std::to_string(size) + " mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
    Require(generator.Generate(plan, error), context + ": " + error);
    const auto quality = EvaluateMapQuality(plan);
    Require(quality.castleReachableRooms == quality.castleRoomCount, context + ": a room is unreachable");
    ++tally.castles;
    std::set<std::string> roles;
    const MapPlanRegion* throne = nullptr;
    for (const auto& r : plan.regions) {
        if (r.kind != "room") continue;
        roles.insert(r.role);
        if (r.role == "throne_room") throne = &r;
    }
    const bool hasThrone = roles.count("throne_room") > 0;
    tally.throne += hasThrone;
    tally.hall += roles.count("entrance_hall") > 0;
    tally.ante += roles.count("antechamber") > 0;
    if (mode == 0) {
        ++tally.concentric;
        tally.treasury += roles.count("treasury") > 0;
        tally.councilChamber += roles.count("council_chamber") > 0;
    }
    // The antechamber is the waiting room of the throne room: it never stands alone.
    Require(roles.count("antechamber") == 0 || hasThrone, context + ": an antechamber without a throne room");
    const bool king = roles.count("royal_bedchamber") > 0, queen = roles.count("queens_bedchamber") > 0;
    const bool dressing = roles.count("royal_dressing_room") > 0, study = roles.count("royal_study") > 0;
    std::string pattern = !king && !queen ? "none" : queen ? (study ? "split+study" : "split") : (dressing ? "trio" : "one");
    ++tally.suites[pattern];
    tally.suite += king || queen;
    {
        std::string key;
        for (const auto& role : roles) if (role.rfind("royal_", 0) == 0 || role == "queens_bedchamber") key += role + ",";
        tally.suiteSets.insert(key);
        tally.vaults += roles.count("royal_treasury") > 0;
        tally.children += roles.count("royal_children_chamber") > 0;
        if (mode != 0) tally.treasuryElsewhere += roles.count("treasury") > 0;  // only Concentric lists a treasury: the others get one by chance
    }
    // A front door opens into a hall (the entrance hall, or the stand-in that takes its place).
    bool frontDoor = false;
    for (const auto& r : plan.regions) {
        if (r.kind != "castle_front_door") continue;
        for (const auto& room : plan.regions)
            if (room.kind == "room" && IsEntryRole(room.role) && (r.x0 + r.x1) / 2 >= room.x0 && (r.x0 + r.x1) / 2 <= room.x1 && r.y0 >= room.y0 - 1 && r.y1 <= room.y1 + 1) frontDoor = true;
    }
    Require(frontDoor, context + ": the gate does not open into a hall");
    if (roles.count("entrance_hall") == 0) ++tally.bigHall;
    // The front room: a vestibule is a narrow entrance hall (at most 60% of the antechamber's width) with rooms beside it.
    {
        const MapPlanRegion* ante = nullptr;
        const MapPlanRegion* front = nullptr;
        for (const auto& r : plan.regions) {
            if (r.kind != "room") continue;
            if (r.role == "antechamber") ante = &r;
        }
        for (const auto& d : plan.regions) {
            if (d.kind != "castle_front_door") continue;
            for (const auto& room : plan.regions)
                if (room.kind == "room" && IsEntryRole(room.role) && (d.x0 + d.x1) / 2 >= room.x0 && (d.x0 + d.x1) / 2 <= room.x1 && d.y0 >= room.y0 - 1 && d.y1 <= room.y1 + 1) front = &room;
        }
        if (front != nullptr && front->role != "corridor") {
            const double frontWidth = front->x1 - front->x0 + 1;
            const double anteWidth = ante != nullptr ? ante->x1 - ante->x0 + 1 : frontWidth;
            if (ante != nullptr && frontWidth <= anteWidth * 0.6) ++tally.vestibule; else ++tally.wideFront;
        }
    }
    // The gate opens into a corridor (the front room is a corridor) that leads on to the antechamber.
    for (const auto& r : plan.regions) {
        if (r.kind != "castle_front_door") continue;
        for (const auto& room : plan.regions)
            if (room.kind == "room" && room.role == "corridor" && (r.x0 + r.x1) / 2 >= room.x0 && (r.x0 + r.x1) / 2 <= room.x1 && r.y0 >= room.y0 - 1 && r.y1 <= room.y1 + 1) {
                ++tally.corridorEntry;
                Require(roles.count("antechamber") > 0, context + ": the entrance corridor leads nowhere (no antechamber)");
            }
    }
    if (throne != nullptr) {
        const double width = throne->x1 - throne->x0 + 1, depth = throne->y1 - throne->y0 + 1;
        ++tally.thrones;
        tally.ratioSum += depth / width;
        tally.tall += depth >= width * 1.4;
        tally.veryTall += depth >= width * 1.9;
    }
}
}  // namespace

int main() {
    try {
        Tally tally;
        for (const int size : {128, 160}) for (const int mode : {0, 1, 2, 3, 4}) for (unsigned seed = 1; seed <= 36; ++seed)
            Check(size, mode, seed, tally);
        const auto percent = [&](int n, int of) { return of == 0 ? 0 : n * 100 / of; };
        std::cout << "  castles " << tally.castles << ", throne " << percent(tally.throne, tally.castles) << "%, hall " << percent(tally.hall, tally.castles)
                  << "%, antechamber " << percent(tally.ante, tally.castles) << "%, suite " << percent(tally.suite, tally.castles) << "%\n";
        for (const auto& [name, count] : tally.suites) std::cout << "  suite " << name << ": " << count << '\n';
        std::cout << "  throne depth:width mean " << (tally.thrones ? tally.ratioSum / tally.thrones : 0) << ", >=1.4 " << percent(tally.tall, tally.thrones)
                  << "%, >=1.9 " << percent(tally.veryTall, tally.thrones) << "%\n";
        // 1. Nothing is certain, but the usual rooms still appear in most castles.
        Require(tally.throne < tally.castles && percent(tally.throne, tally.castles) >= 85, "the throne room is not left out of some castles (or too rarely present)");
        Require(tally.hall < tally.castles && percent(tally.hall, tally.castles) >= 40, "the entrance hall is not left out of some castles (or too rarely present)");
        std::cout << "  front: vestibule " << tally.vestibule << ", big hall " << tally.wideFront << ", gate into a corridor " << tally.corridorEntry << '\n';
        Require(tally.vestibule * 100 >= tally.castles * 15, "too few castles have a vestibule with a room on each side (" + std::to_string(tally.vestibule) + ")");
        // The ranking of candidates must not favour one front form: a corridor entrance is rolled for 20% of the layouts.
        Require(tally.corridorEntry * 100 <= tally.castles * 23, "the gate opens into a corridor in too many castles (" + std::to_string(tally.corridorEntry) + " of " + std::to_string(tally.castles) + ")");
        Require(tally.wideFront * 100 <= tally.castles * 65, "the front room is a big hall in too many castles (" + std::to_string(tally.wideFront) + " of " + std::to_string(tally.castles) + ")");
        Require(tally.ante < tally.throne && percent(tally.ante, tally.throne) >= 75, "the antechamber is not left out of some castles with a throne room");
        // The catalog's principal rooms (once certain) are left out of some castles too: picked in most, not in all.
        {
            const auto catalog = GetCastleRoomCatalog("cruciform_keep");
            int picks = 0, treasury = 0, council = 0;
            for (unsigned seed = 1; seed <= 600; ++seed) {
                std::set<std::string> names;
                for (const auto& spec : PickCastleRooms(catalog, 4, seed)) names.insert(spec.role);
                ++picks; treasury += names.count("treasury") > 0; council += names.count("council_chamber") > 0;
            }
            Require(treasury < picks && council < picks, "the treasury and the council chamber (once mandatory) are never left out");
            Require(treasury * 100 >= picks * 85 && council * 100 >= picks * 85, "the treasury and the council chamber are left out too often");
        }
        Require(tally.suite < tally.castles && percent(tally.suite, tally.castles) >= 80, "the private suite is always present (or too rarely)");
        std::cout << "  gate into a corridor: " << tally.corridorEntry << '\n';
        Require(tally.corridorEntry > 0, "no castle has the gate open into a corridor leading to the antechamber");
        // 2. The suite patterns.
        for (const char* name : {"none", "one", "trio", "split", "split+study"})
            Require(tally.suites[name] > 0, std::string("the private suite pattern never appears: ") + name);
        std::cout << "  suite room sets: " << tally.suiteSets.size() << ", vaults " << tally.vaults << ", children's chambers " << tally.children
                  << ", treasuries beside the throne (not concentric) " << tally.treasuryElsewhere << '\n';
        Require(tally.suiteSets.size() >= 9, "the private suite has too few different room sets");
        Require(tally.vaults > 0 && tally.children > 0, "no suite has a vault or a children's chamber");
        Require(tally.treasuryElsewhere > 0, "no side room is a treasury in the other castle types");
        // 3. A long throne room.
        Require(tally.thrones > 0 && tally.ratioSum / tally.thrones >= 1.4, "the throne rooms are not deeper than wide on average");
        Require(percent(tally.tall, tally.thrones) >= 60, "too few throne rooms are at least 1.4:1");
        Require(percent(tally.veryTall, tally.thrones) >= 25, "too few throne rooms are close to 2:1");
        std::cout << "CASTLE_AXIS_VARIETY: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_AXIS_VARIETY: FAIL " << e.what() << '\n';
        return 1;
    }
}
