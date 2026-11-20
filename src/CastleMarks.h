#pragma once
#include "MapPlan.h"
#include "utils.h"

#include <algorithm>
#include <vector>

// Marks in the map image, for people: a pale yellow on the floor of the throne room where the throne stands, and
// of the antechamber where its benches stand, so that the two rooms can be told apart at a glance.
// The editor reads this image as its biome map: each pixel becomes the biome of the nearest colour of its palette
// (castleColors in MapGenerator.sk). The mark is a colour that the editor reads as the stone floor (biome 12), the
// floor it is painted on, so nothing changes for the editor; only stone-floor cells are marked. A brighter yellow
// such as (255, 220, 0) would be read as a bridge (the nearest colour is the bridge's), so keep to this colour:
// CastleMarksTests decodes every pixel with the editor's palette.
namespace castle_marks {
inline constexpr mapgen::Rgb kMark{255, 255, 200};
// A corridor's floor is a little darker than the other stone floors, and still read by the editor as the stone floor.
inline constexpr mapgen::Rgb kCorridorFloor{164, 166, 172};

// Marks `rgb` (one entry per cell of `plan`). A room without the furniture (a castle left unfurnished) gets a small
// mark in its middle instead, so the room can still be told. Only cells that are stone floor in the plan are marked.
inline void Paint(const MapPlan& plan, std::vector<mapgen::Rgb>& rgb) {
    if (plan.type != "castle" || rgb.size() != static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.depth)) return;
    const auto fill = [&](int x0, int y0, int x1, int y1) {
        for (int y = std::max(0, y0); y <= std::min(plan.depth - 1, y1); ++y)
            for (int x = std::max(0, x0); x <= std::min(plan.width - 1, x1); ++x)
            {
                const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(plan.width) + static_cast<std::size_t>(x);
                if (plan.biomes[i] == TOWN_STONE_FLOOR) rgb[i] = kMark;
            }
    };
    for (const MapPlanRegion& room : plan.regions) {
        if (room.kind != "room" || room.role != "corridor") continue;
        for (int y = std::max(0, room.y0); y <= std::min(plan.depth - 1, room.y1); ++y)
            for (int x = std::max(0, room.x0); x <= std::min(plan.width - 1, room.x1); ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(plan.width) + static_cast<std::size_t>(x);
                if (plan.biomes[i] == TOWN_STONE_FLOOR) rgb[i] = kCorridorFloor;
            }
    }
    for (const char* roomRole : {"throne_room", "antechamber"}) {
        const std::string furniture = std::string(roomRole) == "throne_room" ? "royal_throne" : "waiting_bench";
        for (const MapPlanRegion& room : plan.regions) {
            if (room.kind != "room" || room.role != roomRole) continue;
            bool marked = false;
            for (const MapPlanRegion& item : plan.regions) {
                if (item.kind != "castle_fixture" || item.role != furniture || item.parentId != room.id) continue;
                fill(item.x0, item.y0, item.x1, item.y1);
                marked = true;
            }
            if (!marked) fill((room.x0 + room.x1) / 2 - 1, (room.y0 + room.y1) / 2 - 1, (room.x0 + room.x1) / 2 + 1, (room.y0 + room.y1) / 2 + 1);
        }
    }
}
}  // namespace castle_marks
