#pragma once

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
