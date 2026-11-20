#pragma once

#include <algorithm>
#include <string>

// 主館の部屋の「種類」と、裕福度との連動の重み。部屋の役割名（role）を完全一致または語で分類し、
// 部分一致の連鎖で別の部屋に落ちる（兵舎に本棚が並ぶ、など）ことを防ぐ。
namespace castle_interior {

enum class RoomKind {
    Throne, Corridor, Antechamber, Kitchen, Scullery,
    Bedroom,       // 王・王妃・王子の寝室、王の居室
    PrivateRoom,   // 城主・船長・将校・客人の個室（ベッド 1、机、チェスト）
    Dressing, Study,
    Barracks,      // 兵舎・使用人の宿舎・野戦病院（ベッドの列）
    Ballroom, Hall, Council, Chapel, Armory, GuardRoom, Signal, Workshop,
    Treasury, Storage, Library, Office, Music, Plain
};

inline bool Has(const std::string& role, const char* word) { return role.find(word) != std::string::npos; }

inline RoomKind ClassifyRoom(const std::string& role, const std::string& archetype) {
    if (role == "throne_room") return RoomKind::Throne;
    if (role == "corridor") return RoomKind::Corridor;
    if (role == "antechamber") return RoomKind::Antechamber;
    if (role == "scullery" || role == "laundry") return RoomKind::Scullery;
    if (Has(role, "kitchen")) return RoomKind::Kitchen;
    if (role == "royal_dressing_room") return RoomKind::Dressing;
    if (role == "royal_study") return RoomKind::Study;
    if (role == "royal_chambers" || role == "royal_bedchamber" || role == "queens_bedchamber" || role == "royal_children_chamber") return RoomKind::Bedroom;
    if (role == "castellan_chamber" || role == "watch_chamber" || role == "captains_quarters" || role == "officers_quarters" || role == "guest_apartment" || role == "guest_house")
        return RoomKind::PrivateRoom;
    if (Has(role, "barracks") || role == "servants_quarters" || Has(role, "hospital")) return RoomKind::Barracks;
    if (role == "entrance_hall") return archetype == "palace" ? RoomKind::Ballroom : RoomKind::Hall;
    if (role == "great_hall" || role == "guard_hall" || role == "mess_hall" || role == "servants_hall" || role == "customs_hall") return RoomKind::Hall;
    if (Has(role, "council") || Has(role, "command") || role == "war_room" || role == "map_room" || role == "navigation_room" || role == "chart_archive") return RoomKind::Council;
    if (Has(role, "chapel") || Has(role, "shrine")) return RoomKind::Chapel;
    if (role == "armory") return RoomKind::Armory;
    if (role == "signal_room") return RoomKind::Signal;
    if (Has(role, "guard") || role == "watch_post") return RoomKind::GuardRoom;
    if (role == "smithy" || role == "siege_workshop") return RoomKind::Workshop;
    if (role == "treasury" || role == "royal_treasury" || role == "strongroom") return RoomKind::Treasury;
    if (Has(role, "store") || role == "pantry" || role == "granary" || Has(role, "warehouse")) return RoomKind::Storage;
    if (role == "library" || role == "archive" || role == "war_archive") return RoomKind::Library;
    if (Has(role, "office")) return RoomKind::Office;
    if (role == "music_room") return RoomKind::Music;
    return RoomKind::Plain;
}

// 倉庫・宝物庫・武器庫は、家具で詰まった部屋にするため、占有率の上限を高くする（ほかは 23%、玉座の間は 12%）。
inline bool StoresFiller(const std::string& role, const std::string& archetype) {
    const RoomKind kind = ClassifyRoom(role, archetype);
    return kind == RoomKind::Storage || kind == RoomKind::Treasury || kind == RoomKind::Armory;
}

// 裕福度 tw（0〜1）に、部屋の種類の重みと城のタイプの補正を掛けた、その部屋の豊かさ r（0〜1）。
// 品目はこの r のしきい値で足されるので、豊かな城の部屋は、貧しい城の同じ部屋に品目を足したものになる。
inline double RoomRichness(RoomKind kind, double tw, const std::string& archetype) {
    double weight = 1.0, bonus = 0.0;
    bool military = false, ceremonial = false, trade = false;
    switch (kind) {
        case RoomKind::Bedroom: case RoomKind::Dressing: case RoomKind::Study: weight = 1.0; bonus = 0.15; ceremonial = true; break;
        case RoomKind::PrivateRoom: weight = 0.8; break;
        case RoomKind::Treasury: weight = 1.2; trade = true; break;
        case RoomKind::Throne: case RoomKind::Ballroom: case RoomKind::Antechamber: case RoomKind::Chapel: case RoomKind::Music: case RoomKind::Hall: weight = 1.0; ceremonial = true; break;
        case RoomKind::Library: case RoomKind::Council: case RoomKind::Office: weight = 0.8; trade = kind == RoomKind::Office; break;
        case RoomKind::Kitchen: case RoomKind::Scullery: case RoomKind::Storage: weight = 0.6; bonus = 0.1; trade = true; break;
        case RoomKind::Armory: case RoomKind::GuardRoom: case RoomKind::Barracks: case RoomKind::Workshop: case RoomKind::Signal: weight = 0.4; bonus = 0.2; military = true; break;
        default: weight = 0.6; break;
    }
    double r = tw * weight + bonus;
    if (archetype == "hill") r += military ? 0.0 : -0.1;
    else if (archetype == "river") r += trade ? 0.2 : 0.0;
    else if (archetype == "palace") r += ceremonial ? 0.2 : military ? -0.1 : 0.0;
    else if (archetype == "fortress") r += military ? 0.25 : ceremonial ? -0.2 : 0.0;
    return std::clamp(r, 0.0, 1.0);
}

}  // namespace castle_interior
