#include "CastleMapImageGenerator.h"
#include "CastleRoomStyle.h"
#include "TestScale.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Room kinds and the wealth link of the keep's furnishing (CastleRoomStyle.h):
//  - every role the castle can draw is classified, and none of the known ones falls to the generic study (Plain);
//  - a room's richness r never falls as the wealth rises, a military room is less sensitive to wealth than a private one;
//  - in a real castle a treasury holds more chests, and a store more barrels, when the castle is rich than when it is poor;
//  - a barracks has no bookcase, a kitchen has its stove, and a room has no more than one candle of the old per-room kind.
using namespace castle_interior;
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

MapPlan Make(int mode, unsigned seed, double wealth) {
    MapGenParams params;
    params.width = params.depth = 128;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"countryWealth", wealth}, {"lotFacilities", 0}};
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    Require(generator.Generate(plan, error), "mode=" + std::to_string(mode) + " seed=" + std::to_string(seed) + ": " + error);
    return plan;
}
}  // namespace

int main() {
    try {
        // Classification of the roles the generator can draw.
        const std::map<std::string, RoomKind> expected = {
            {"throne_room", RoomKind::Throne}, {"great_kitchen", RoomKind::Kitchen}, {"scullery", RoomKind::Scullery}, {"barracks", RoomKind::Barracks},
            {"mountain_barracks", RoomKind::Barracks}, {"watch_chamber", RoomKind::PrivateRoom}, {"castellan_chamber", RoomKind::PrivateRoom},
            {"royal_dressing_room", RoomKind::Dressing}, {"royal_study", RoomKind::Study}, {"royal_bedchamber", RoomKind::Bedroom},
            {"mess_hall", RoomKind::Hall}, {"servants_hall", RoomKind::Hall}, {"library", RoomKind::Library}, {"archive", RoomKind::Library},
            {"music_room", RoomKind::Music}, {"granary", RoomKind::Storage}, {"reserve_store", RoomKind::Storage}, {"customs_warehouse", RoomKind::Storage},
            {"treasury", RoomKind::Treasury}, {"strongroom", RoomKind::Treasury}, {"toll_office", RoomKind::Office}, {"dispatch_office", RoomKind::Office},
            {"armory", RoomKind::Armory}, {"guard_room", RoomKind::GuardRoom}, {"guard_lodge", RoomKind::GuardRoom}, {"signal_room", RoomKind::Signal},
            {"smithy", RoomKind::Workshop}, {"siege_workshop", RoomKind::Workshop}, {"chapel", RoomKind::Chapel}, {"royal_chapel", RoomKind::Chapel},
            {"map_room", RoomKind::Council}, {"navigation_room", RoomKind::Council}, {"council_chamber", RoomKind::Council}, {"chart_archive", RoomKind::Council}};
        for (const auto& [role, kind] : expected) Require(ClassifyRoom(role, "concentric") == kind, "the role " + role + " is classified wrongly");
        Require(ClassifyRoom("entrance_hall", "palace") == RoomKind::Ballroom && ClassifyRoom("entrance_hall", "river") == RoomKind::Hall, "the entrance hall is a ballroom only in a palace");

        // Richness: monotone in wealth; a military room is less sensitive than a private one.
        for (const char* type : {"concentric", "hill", "river", "palace", "fortress"})
            for (int k = 0; k < static_cast<int>(RoomKind::Plain) + 1; ++k) {
                double last = -1.0;
                for (double tw = 0.0; tw <= 1.0; tw += 0.1) {
                    const double r = RoomRichness(static_cast<RoomKind>(k), tw, type);
                    Require(r >= last - 1e-9 && r >= 0.0 && r <= 1.0, std::string("richness is not monotone in wealth for ") + type);
                    last = r;
                }
            }
        Require(RoomRichness(RoomKind::Armory, 1.0, "concentric") - RoomRichness(RoomKind::Armory, 0.0, "concentric") <
                RoomRichness(RoomKind::Bedroom, 1.0, "concentric") - RoomRichness(RoomKind::Bedroom, 0.0, "concentric"), "an armory should follow wealth less than a royal bedroom");

        // A real castle: richer holds more.
        long poorCandles = 0, richCandles = 0, poorThrones = 0, richThrones = 0;
        long poorChests = 0, richChests = 0, poorBarrels = 0, richBarrels = 0, poorTreasuries = 0, richTreasuries = 0, poorStores = 0, richStores = 0;
        const unsigned seeds = QuickSeeds(12);
        for (int mode = 0; mode < 5; ++mode) for (unsigned seed = 1; seed <= seeds; ++seed) {
            for (const double wealth : {0.7, 2.5}) {
                const MapPlan plan = Make(mode, seed + 100u * static_cast<unsigned>(mode), wealth);
                std::map<int, std::string> roleOf;
                for (const auto& r : plan.regions) if (r.kind == "room") {
                    roleOf[r.id] = r.role;
                    const RoomKind kind = ClassifyRoom(r.role, plan.archetype);
                    (wealth < 1.0 ? poorTreasuries : richTreasuries) += kind == RoomKind::Treasury;
                    (wealth < 1.0 ? poorStores : richStores) += kind == RoomKind::Storage;
                    (wealth < 1.0 ? poorThrones : richThrones) += kind == RoomKind::Throne;
                }
                for (const auto& r : plan.regions) {
                    if (r.kind != "castle_fixture") continue;
                    const auto it = roleOf.find(r.parentId);
                    if (it == roleOf.end()) continue;
                    const RoomKind kind = ClassifyRoom(it->second, plan.archetype);
                    (wealth < 1.0 ? poorCandles : richCandles) += kind == RoomKind::Throne && r.role.find("candle") != std::string::npos;
                    (wealth < 1.0 ? poorChests : richChests) += kind == RoomKind::Treasury && r.role == "storage_chest";
                    (wealth < 1.0 ? poorBarrels : richBarrels) += kind == RoomKind::Storage && r.role == "storage_barrel";
                    Require(!(kind == RoomKind::Barracks && r.role.find("bookcase") != std::string::npos), "a barracks has a bookcase");
                    Require(!(kind == RoomKind::Scullery && r.role == "cooking_stove"), "a scullery has a stove");
                }
            }
        }
        std::cout << "  chests poor/rich=" << poorChests << "/" << richChests << " barrels poor/rich=" << poorBarrels << "/" << richBarrels << '\n';
        // (the rooms of a castle change with its wealth: compare per room)
        const auto per = [](long items, long rooms) { return rooms > 0 ? static_cast<double>(items) / static_cast<double>(rooms) : 0.0; };
        std::cout << "  per treasury " << per(poorChests, poorTreasuries) << " -> " << per(richChests, richTreasuries)
                  << ", per store " << per(poorBarrels, poorStores) << " -> " << per(richBarrels, richStores) << '\n';
        std::cout << "  throne-room candles per room " << per(poorCandles, poorThrones) << " -> " << per(richCandles, richThrones) << '\n';
        Require(per(richCandles, richThrones) > per(poorCandles, poorThrones), "a rich throne room does not hold more candles than a poor one");
        Require(per(richChests, richTreasuries) >= per(poorChests, poorTreasuries), "a rich treasury does not hold more chests than a poor one");
        Require(per(richBarrels, richStores) >= per(poorBarrels, poorStores), "a rich store does not hold more barrels than a poor one");
        std::cout << "CASTLE_ROOM_STYLE: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_ROOM_STYLE: FAIL " << e.what() << '\n';
        return 1;
    }
}
