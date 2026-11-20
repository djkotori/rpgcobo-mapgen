#pragma once

#include <string>
#include <vector>

// 空き地施設（屋外の区画とはなれ）の装飾に使う、既存の設備（エディターのブロックに登録済みのアセット）の一覧。
// 型と番号は CastleInterior.h の Native() と同じ（キー "castleObject<type>_<variant>"）。寸法は幅・奥行き・高さ。
// 部屋の役割名（はなれの部屋は annex_*、屋外は yard_*）ごとに、置くものと個数を決める。
namespace castle_lot {

struct DecorItem {
    int type, variant, w, d, h;
    int count;
    bool wall;          // true: 部屋の壁に接して置く。false: 区画の中ならどこでもよい。
    const char* role;   // fixture の役割名
    bool civic = false; // true: 街の設備（settlement_facility。井戸など）
};

namespace decor {
inline constexpr DecorItem kBed{4, 14, 2, 4, 2, 1, true, "bed"};
inline constexpr DecorItem kTable{4, 7, 4, 4, 2, 1, false, "table"};
inline constexpr DecorItem kChair{4, 4, 2, 2, 3, 2, false, "chair"};
inline constexpr DecorItem kBench{4, 5, 4, 2, 3, 1, false, "bench"};
inline constexpr DecorItem kShelf{4, 9, 4, 2, 4, 1, true, "shelf"};
inline constexpr DecorItem kChest{4, 11, 2, 2, 4, 1, true, "chest"};
inline constexpr DecorItem kBarrel{4, 21, 3, 3, 3, 1, false, "barrel"};
inline constexpr DecorItem kBucket{4, 41, 2, 2, 2, 1, false, "water_bucket"};
inline constexpr DecorItem kCounter{4, 10, 4, 2, 2, 1, true, "counter"};
inline constexpr DecorItem kStove{2, 7, 4, 2, 3, 1, true, "stove"};
inline constexpr DecorItem kCandle{2, 4, 2, 1, 2, 1, false, "candle"};
inline constexpr DecorItem kSword{4, 30, 4, 1, 3, 1, true, "sword"};
inline constexpr DecorItem kShield{4, 28, 3, 1, 3, 1, true, "shield"};
inline constexpr DecorItem kWell{3, 26, 4, 4, 5, 1, false, "well", true};
}  // namespace decor

// 個数と置き方だけを変えた写し。
inline DecorItem With(DecorItem item, int count, int wall = -1) {
    item.count = count;
    if (wall >= 0) item.wall = wall != 0;
    return item;
}

// 役割名ごとの装飾の一覧。
inline std::vector<DecorItem> DecorationFor(const std::string& role) {
    using namespace decor;
    // 屋外
    if (role == "yard_cargo") return {With(kBarrel, 6, 0)};
    if (role == "yard_garden_grass") return {With(kBench, 3, 0)};
    if (role == "yard_garden_forest") return {With(kBench, 2, 0)};
    if (role == "yard_well") return {With(kBucket, 2), With(kBarrel, 2)};
    if (role == "yard_graveyard") return {With(kCandle, 4)};
    if (role == "yard_herb_garden") return {With(kBench, 1, 0), With(kBarrel, 3, 0), With(kBucket, 2)};
    if (role == "yard_drill_ground") return {With(kBarrel, 4, 0), With(kBench, 3, 0)};
    if (role == "yard_paddock") return {With(kBucket, 4), With(kBarrel, 3, 0)};
    if (role == "yard_archery_range") return {With(kShield, 4, 0), With(kBench, 2, 0)};
    // はなれ
    if (role == "annex_jail_hall") return {With(kBench, 2, 1), With(kBarrel, 1, 1)};
    if (role == "annex_jail_cell") return {kBed, With(kBucket, 1, 1)};
    if (role == "annex_laboratory") return {With(kShelf, 3), kTable, With(kChair, 2)};
    if (role == "annex_laboratory_back") return {With(kShelf, 4), kChest};
    if (role == "annex_barracks") return {With(kBed, 4)};
    if (role == "annex_barracks_back") return {With(kBed, 4), With(kChest, 2), kShelf};
    if (role == "annex_armory") return {With(kSword, 3), With(kShield, 3), With(kChest, 3), With(kBarrel, 2, 1)};
    if (role == "annex_chapel") return {With(kBench, 4, 0), With(kCandle, 4, 1), kShelf};
    if (role == "annex_stable") return {With(kBarrel, 4, 1), kChest, With(kBench, 1, 1)};
    if (role == "annex_well_house") return {With(kBucket, 2), With(kBarrel, 2, 1)};
    if (role == "annex_storehouse") return {With(kBarrel, 6, 1), With(kChest, 5), With(kShelf, 2)};
    if (role == "annex_rowhouse_unit") return {kBed, kTable, With(kChair, 2), kChest, kStove};
    return {};
}

}  // namespace castle_lot
