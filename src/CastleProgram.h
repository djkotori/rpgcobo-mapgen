#pragma once

#include <algorithm>
#include <string>
#include <vector>

// An architectural brief, not a palette swap: dimensions, major rooms,
// outdoor uses and service-building proportions all come from one program.
struct CastleFacilitySpec {
    std::string role;
    double width = 1.0;
    double height = 1.0;
    int court = 1; // 0: public/military court, 1: supply court, 2: keep, 3: river flank
    std::string furnishing;
};

struct CastleProgram {
    std::string layout;
    double siteWidth = 0.82;
    double siteDepth = 0.86;
    double keepWidth = 0.46;
    double keepDepth = 0.43;
    int roomsMin = 6;
    int roomsMax = 10;
    int facilitiesMin = 6;
    int facilitiesMax = 10;
    std::string principal;
    std::string hall;
    std::vector<std::string> chambers;
    std::string court0;
    std::string court1;
    std::vector<CastleFacilitySpec> facilities;
};

inline CastleProgram GetCastleProgram(const std::string& archetype) {
    if (archetype == "palace") return {
        "courtyard_palace", 0.98, 0.96, 0.72, 0.54, 6, 12, 5, 8,
        "throne_room", "great_hall", {"great_kitchen", "royal_chambers", "pantry", "royal_chapel", "library", "guest_apartment", "music_room"},
        "formal_garden", "carriage_court", {
            {"guest_house", 1.35, 1.1, 0, "beds"}, {"royal_stable", 1.5, 0.85, 1, "stalls"},
            {"servants_quarters", 1.2, 1.0, 2, "beds"}, {"bakery", 1.05, 1.0, 2, "hearths"},
            {"orangery", 1.8, 0.65, 0, "planters"}, {"laundry", 1.0, 0.8, 2, "basins"},
            {"coach_house", 1.3, 0.9, 1, "carts"}, {"guard_lodge", 0.8, 0.8, 0, "racks"}}};
    if (archetype == "hill") return {
        "ridge_tower", 0.53, 0.86, 0.44, 0.43, 4, 6, 4, 6,
        "command_room", "guard_hall", {"signal_room", "reserve_store", "castellan_chamber", "map_room", "watch_chamber"},
        "signal_terrace", "packhorse_yard", {
            {"mountain_barracks", 0.85, 1.55, 0, "beds"}, {"granary", 0.9, 1.15, 2, "grain_bins"},
            {"watch_post", 0.8, 0.85, 0, "signal_brazier"}, {"packhorse_stable", 0.9, 1.1, 1, "stalls"},
            {"mess_hall", 0.9, 1.0, 0, "tables"}, {"mountain_shrine", 0.75, 0.8, 2, "altar"}}};
    if (archetype == "river") return {
        "riverside_l_house", 0.92, 0.62, 0.62, 0.48, 5, 8, 5, 9,
        "harbor_office", "customs_hall", {"strongroom", "navigation_room", "captains_quarters", "chart_archive", "toll_office"},
        "cargo_yard", "boat_repair_yard", {
            {"customs_warehouse", 1.85, 1.15, 3, "crates"}, {"boathouse", 1.9, 0.85, 3, "boat_cradle"},
            {"dock_barracks", 1.25, 0.9, 0, "beds"}, {"fish_smokehouse", 1.0, 0.8, 1, "hearths"},
            {"ropewalk", 2.4, 0.6, 3, "rope_racks"}, {"net_store", 1.2, 0.75, 1, "racks"},
            {"carter_stable", 1.4, 0.8, 0, "stalls"}, {"dock_kitchen", 1.1, 0.9, 0, "hearths"}}};
    if (archetype == "fortress") return {
        "command_block", 0.78, 0.72, 0.49, 0.36, 4, 7, 6, 10,
        "war_room", "command_hall", {"armory", "officers_quarters", "map_room", "dispatch_office", "war_archive"},
        "drill_square", "siege_yard", {
            {"barracks", 1.8, 1.1, 0, "beds"}, {"siege_workshop", 1.6, 1.3, 1, "workbenches"},
            {"field_hospital", 1.3, 1.2, 0, "hospital_beds"}, {"provision_store", 1.3, 1.0, 1, "crates"},
            {"garrison_kitchen", 1.25, 1.0, 0, "hearths"}, {"cavalry_stable", 1.6, 0.9, 1, "stalls"},
            {"prison", 1.1, 1.0, 2, "cells"}, {"smithy", 1.2, 0.95, 1, "workbenches"}}};
    return {
        "cruciform_keep", 0.83, 0.87, 0.48, 0.45, 5, 10, 6, 10,
        "throne_room", "great_hall", {"treasury", "council_chamber", "chapel", "royal_chambers", "archive", "guard_room"},
        "muster_court", "provision_court", {
            {"barracks", 1.4, 0.95, 0, "beds"}, {"armory", 1.1, 1.2, 2, "racks"},
            {"granary", 1.15, 1.2, 1, "grain_bins"}, {"stable", 1.4, 0.9, 1, "stalls"},
            {"castle_kitchen", 1.1, 1.1, 2, "hearths"}, {"chapel", 0.9, 1.25, 2, "altar"},
            {"smithy", 1.1, 1.0, 1, "workbenches"}, {"prison", 0.9, 1.0, 0, "cells"}}};
}

// ---------------------------------------------------------------------------
// Room catalog: which rooms a castle type may contain, and how likely each is.
// ---------------------------------------------------------------------------
// `presence` is the chance (percent) that the room appears. No room is certain: the most
// likely ones are 95. `side` is the preferred wing (L, R, or A for either) and `zone`
// the preferred depth from the back of the keep (B back, M middle, F front, near
// the gate). The catalog is data: the layout code only reads it.
// The room that faces the main gate is the entrance hall; where the castle has none (one castle in
// ten), another hall (a great hall, or a guard hall in a military castle) takes its place.
inline bool IsCastleEntryRole(const std::string& role) {
    return role == "entrance_hall" || role == "great_hall" || role == "guard_hall";
}

// Rooms of the private suite behind the throne room, and the like: never given a side door.
inline bool IsCastlePrivateRole(const std::string& role) {
    return role.rfind("royal_", 0) == 0 || role == "queens_bedchamber";
}

struct CastleRoomSpec {
    std::string role;
    int presence = 100;
    char side = 'A';
    char zone = 'M';
};

inline std::vector<CastleRoomSpec> GetCastleRoomCatalog(const std::string& archetype) {
    if (archetype == "palace") return {
        {"great_kitchen", 95, 'L', 'B'}, {"royal_chambers", 95, 'R', 'B'},
        {"pantry", 80, 'L', 'M'}, {"scullery", 60, 'L', 'M'}, {"cold_store", 55, 'L', 'F'},
        {"servants_hall", 70, 'L', 'F'}, {"royal_chapel", 85, 'R', 'M'}, {"guest_apartment", 75, 'R', 'F'},
        {"library", 65, 'R', 'B'}, {"music_room", 55, 'R', 'M'}, {"orangery", 35, 'R', 'F'},
        {"guard_lodge", 80, 'A', 'F'}};
    if (archetype == "hill") return {
        {"signal_room", 95, 'A', 'B'}, {"reserve_store", 95, 'L', 'B'},
        {"castellan_chamber", 80, 'R', 'B'}, {"map_room", 60, 'R', 'M'}, {"watch_chamber", 75, 'A', 'F'},
        {"mountain_barracks", 70, 'L', 'F'}, {"mess_hall", 55, 'R', 'F'}, {"mountain_shrine", 25, 'A', 'B'},
        {"packhorse_stable", 30, 'A', 'F'}};
    if (archetype == "river") return {
        {"strongroom", 95, 'A', 'B'}, {"navigation_room", 95, 'A', 'M'},
        {"captains_quarters", 70, 'A', 'B'}, {"chart_archive", 55, 'A', 'M'}, {"toll_office", 90, 'A', 'F'},
        {"customs_warehouse", 80, 'A', 'F'}, {"boathouse", 50, 'A', 'F'}, {"dock_barracks", 55, 'A', 'F'},
        {"dock_kitchen", 70, 'A', 'M'}};
    if (archetype == "fortress") return {
        {"armory", 95, 'L', 'F'}, {"officers_quarters", 95, 'R', 'B'},
        {"map_room", 75, 'R', 'M'}, {"dispatch_office", 70, 'R', 'F'}, {"war_archive", 50, 'R', 'B'},
        {"barracks", 95, 'L', 'F'}, {"field_hospital", 60, 'L', 'B'}, {"provision_store", 80, 'L', 'M'},
        {"siege_workshop", 45, 'A', 'F'}, {"smithy", 50, 'A', 'F'}, {"prison", 65, 'A', 'F'},
        {"cavalry_stable", 40, 'A', 'F'}};
    return {
        {"treasury", 95, 'L', 'B'}, {"council_chamber", 95, 'R', 'B'},
        {"chapel", 85, 'R', 'M'}, {"archive", 60, 'L', 'M'}, {"guard_room", 95, 'A', 'F'},
        {"castle_kitchen", 90, 'L', 'F'}, {"barracks", 70, 'L', 'F'}, {"armory", 55, 'L', 'F'},
        {"granary", 60, 'L', 'B'}, {"stable", 35, 'A', 'F'}, {"prison", 30, 'A', 'F'},
        {"guest_apartment", 40, 'R', 'F'}};
}

inline unsigned CatalogHash(unsigned seed, unsigned index) {
    unsigned value = seed * 2654435761u + index * 40503u + 0x9e3779b9u;
    value ^= value >> 15; value *= 2246822519u; value ^= value >> 13; value *= 3266489917u; value ^= value >> 16;
    return value;
}

// Pick exactly `count` rooms. Mandatory rooms (presence 100) come first; each
// other room is drawn by its presence chance. Too many hits drop the least likely
// ones; too few add the most likely misses; a keep larger than the catalog repeats
// rooms from the top. The result is deterministic in `seed`.
inline std::vector<CastleRoomSpec> PickCastleRooms(const std::vector<CastleRoomSpec>& catalog, int count, unsigned seed) {
    std::vector<CastleRoomSpec> picked, optional, missed;
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        if (catalog[i].presence >= 100) picked.push_back(catalog[i]);
        else if (static_cast<int>(CatalogHash(seed, static_cast<unsigned>(i)) % 100u) < catalog[i].presence) optional.push_back(catalog[i]);
        else missed.push_back(catalog[i]);
    }
    const auto byChance = [](const CastleRoomSpec& a, const CastleRoomSpec& b) { return a.presence > b.presence; };
    std::stable_sort(optional.begin(), optional.end(), byChance);
    std::stable_sort(missed.begin(), missed.end(), byChance);
    for (const auto& spec : optional) picked.push_back(spec);
    for (const auto& spec : missed) picked.push_back(spec);
    // `picked` now lists mandatory, drawn, then missed rooms: take the first `count`.
    std::vector<CastleRoomSpec> result;
    for (int i = 0; i < count; ++i) {
        if (picked.empty()) break;
        result.push_back(picked[static_cast<std::size_t>(i) % picked.size()]);
    }
    if (static_cast<int>(picked.size()) < count) {
        for (int i = static_cast<int>(picked.size()); i < count; ++i) result[static_cast<std::size_t>(i)].presence = 1;
    }
    return result;
}
