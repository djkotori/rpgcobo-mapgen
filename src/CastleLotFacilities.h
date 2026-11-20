#pragma once

#include "MapImageGenerator.h"

#include <array>
#include <cstddef>
#include <cstdint>

// 城の空き地に置く施設の一覧と、城タイプとの相性。
// 仕様の正本は docs/references/castle-lot-facilities.md の「施設の一覧」と「相性表」。
// ここでは数値と ○× の表だけを持ち、配置の処理は別のファイルで行う。
namespace castle_lot {

// 施設の仕様 1 件。
// role: 役割名（英語）。屋外は yard_*、はなれは annex_*。
// annex: はなれなら true、屋外なら false。
// minWidth / minHeight: 外寸の下限（幅×奥行き。壁を含む）。
// ground: 床に使う MapGenBiomeID の値。
// rooms: 部屋数。-1 は長屋（部屋が横に並ぶ）、0 は「通路と独房 3〜4 室を実装側で決める」（牢屋のみ）の意味。
// fits[5]: 城タイプの順に 0=同心円 1=丘 2=川 3=宮殿 4=要塞。true は ○（通常）、false は ×（発生率 1/4）。
struct FacilitySpec {
    const char* role;
    bool annex;
    int minWidth;
    int minHeight;
    std::uint8_t ground;
    int rooms;
    bool fits[5];
};

// 何も置かない割合（パーセント）。
inline constexpr int kNothingPercent = 20;

// 中庭（主館に囲まれた空き地）で何も置かない割合（パーセント）。ほかの空き地より当選しやすい。
inline constexpr int kCourtyardNothingPercent = 5;

// 相性が × の施設の発生率の倍率（○ は 1.0）。
inline constexpr double kMismatchWeight = 0.25;

// 施設の一覧（屋外 9 件 + はなれ 9 件）。順序は仕様の表のとおり。
// 寸法は人間の大きさ（2×2 マス）に合わせ、当初の案の 2 倍にしてある。
inline const std::array<FacilitySpec, 18>& Catalog() {
    static const std::array<FacilitySpec, 18> kCatalog = {{
        // 屋外（annex = false, rooms = 0）
        // role,                 annex, minW, minH, ground,                                  rooms, fits{同心円, 丘, 川, 宮殿, 要塞}
        {"yard_cargo",           false,   16,    16, static_cast<std::uint8_t>(TOWN_PAVEMENT),     0, {true,  true,  true,  false, true}},
        {"yard_garden_grass",    false,  20,   20, static_cast<std::uint8_t>(PLAIN),              0, {true,  true,  false, true,  false}},
        {"yard_garden_forest",   false,  20,   20, static_cast<std::uint8_t>(FOREST),             0, {true,  true,  false, true,  false}},
        {"yard_well",            false,   12,    12, static_cast<std::uint8_t>(TOWN_PAVEMENT),     0, {true,  true,  true,  true,  true}},
        {"yard_graveyard",       false,   16,    16, static_cast<std::uint8_t>(PLAIN),             0, {true,  true,  false, false, true}},
        {"yard_herb_garden",     false,   16,    16, static_cast<std::uint8_t>(PLAIN),             0, {true,  true,  false, true,  false}},
        {"yard_drill_ground",    false,  40,   32, static_cast<std::uint8_t>(DESERT),             0, {true,  false, false, false, true}},
        {"yard_paddock",         false,  48,   24, static_cast<std::uint8_t>(DESERT),             0, {true,  false, true,  true,  false}},
        {"yard_archery_range",   false,  40,    16, static_cast<std::uint8_t>(PLAIN),             0, {true,  true,  false, false, true}},
        // はなれ（annex = true）。床はすべて城と同じ石床（TOWN_STONE_FLOOR）
        {"annex_jail",           true,  24,   20, static_cast<std::uint8_t>(TOWN_STONE_FLOOR),    0, {true,  true,  false, false, true}},
        {"annex_laboratory",     true,  22,    16, static_cast<std::uint8_t>(TOWN_STONE_FLOOR),    2, {true,  true,  false, true,  false}},
        {"annex_barracks",       true,  24,    16, static_cast<std::uint8_t>(TOWN_STONE_FLOOR),    2, {true,  true,  false, false, true}},
        {"annex_armory",         true,   16,    16, static_cast<std::uint8_t>(TOWN_STONE_FLOOR),    1, {true,  true,  false, false, true}},
        {"annex_chapel",         true,  20,    16, static_cast<std::uint8_t>(TOWN_STONE_FLOOR),    1, {true,  true,  false, true,  false}},
        {"annex_stable",         true,  24,    16, static_cast<std::uint8_t>(TOWN_STONE_FLOOR),    1, {true,  false, true,  true,  false}},
        {"annex_well_house",     true,   12,    12, static_cast<std::uint8_t>(TOWN_STONE_FLOOR),    1, {true,  true,  true,  true,  true}},
        {"annex_storehouse",     true,   16,    12, static_cast<std::uint8_t>(TOWN_STONE_FLOOR),    1, {true,  true,  true,  false, true}},
        // 長屋（住み込みで働く人の住まい）。rooms = -1 は「8 マス幅の部屋が横に並び、各部屋が南に自分の扉を持つ」の意味。
        {"annex_rowhouse",       true,   34,   16, static_cast<std::uint8_t>(TOWN_STONE_FLOOR),    -1, {true,  true,  true,  true,  false}},
    }};
    return kCatalog;
}

// 城タイプ mode（0〜4）に対する施設の重み。相性が ○ なら 1.0、× なら kMismatchWeight。
inline double Weight(const FacilitySpec& spec, int mode) {
    return spec.fits[mode] ? 1.0 : kMismatchWeight;
}

}  // namespace castle_lot
