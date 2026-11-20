#include "CastleMapImageGenerator.h"
#include "TestScale.h"
#include "MapQuality.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Walls of a castle:
//  - the wall between two rooms is exactly one cell thick;
//  - the castle's outside wall is exactly two cells thick: the inner wall of the rooms
//    and an outer wall laid around the finished castle (its notches and wings too);
//  - walls meet without gaps: no corner where two walls touch only diagonally.
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Case { int size; int mode; unsigned seed; double wealth; int rooms; };

bool IsWall(std::uint8_t b) { return b == MOUNTAIN || b == EXMOUNTAIN; }
bool IsFloor(std::uint8_t b) { return b == TOWN_FLOOR || b == TOWN_WOOD_FLOOR || b == TOWN_STONE_FLOOR || b == TOWN_FURNITURE; }

MapPlan Make(const Case& c, std::string& context) {
    MapGenParams params;
    params.width = params.depth = c.size;
    params.seed = c.seed;
    params.extra = {{"castleMode", c.mode}, {"assetFoundation", 1}, {"lotFacilities", 0}, {"wealth", c.wealth}};
    if (c.rooms > 0) { params.extra["keepRoomCountMin"] = c.rooms; params.extra["keepRoomCountMax"] = c.rooms; }
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    context = "size=" + std::to_string(c.size) + " mode=" + std::to_string(c.mode) + " seed=" + std::to_string(c.seed) +
        " wealth=" + std::to_string(c.wealth) + " rooms=" + std::to_string(c.rooms);
    Require(generator.Generate(plan, error), context + ": " + error);
    return plan;
}

void Check(const Case& c) {
    std::string context;
    const MapPlan plan = Make(c, context);
    const int size = c.size;
    const auto at = [&](int x, int y) { return plan.biomes[static_cast<std::size_t>(y * size + x)]; };
    // Cells inside a room or a keep wing: where a wall run starts.
    std::vector<std::uint8_t> inRoom(static_cast<std::size_t>(size * size), 0);
    for (const auto& region : plan.regions) {
        if (region.kind != "room" && region.kind != "keep_expansion") continue;
        for (int y = std::max(0, region.y0); y <= std::min(size - 1, region.y1); ++y)
            for (int x = std::max(0, region.x0); x <= std::min(size - 1, region.x1); ++x)
                inRoom[static_cast<std::size_t>(y * size + x)] = 1;
    }
    // Walk out of every room cell in the four directions: the wall run must end in another
    // room after one cell, or in open ground after two cells (inner wall + outer wall).
    // Doors, roads and map edges end a run without a verdict.
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        if (!inRoom[static_cast<std::size_t>(y * size + x)] || !IsFloor(at(x, y))) continue;
        for (int d = 0; d < 4; ++d) {
            const int sx = x + dx[d], sy = y + dy[d];
            if (sx < 0 || sy < 0 || sx >= size || sy >= size || !IsWall(at(sx, sy))) continue;  // not at a wall
            // Only judge a wall face: both neighbours along the wall are wall too (not a wall end or corner).
            const int px = dy[d] != 0 ? 1 : 0, py = dx[d] != 0 ? 1 : 0;
            if (sx - px < 0 || sy - py < 0 || sx + px >= size || sy + py >= size ||
                !IsWall(at(sx - px, sy - py)) || !IsWall(at(sx + px, sy + py))) continue;
            int length = 0, ex = sx, ey = sy;
            bool straight = true;  // every wall cell of the run has wall on both sides (no corner)
            while (ex >= 0 && ey >= 0 && ex < size && ey < size && IsWall(at(ex, ey))) {
                if (ex - px < 0 || ey - py < 0 || ex + px >= size || ey + py >= size ||
                    !IsWall(at(ex - px, ey - py)) || !IsWall(at(ex + px, ey + py))) straight = false;
                ++length; ex += dx[d]; ey += dy[d];
            }
            if (!straight) continue;  // a corner of the outline: not a wall face
            if (ex < 0 || ey < 0 || ex >= size || ey >= size) continue;  // reached the map edge
            const std::uint8_t end = at(ex, ey);
            const std::string where = " at " + std::to_string(x) + "," + std::to_string(y) + " going " + std::to_string(d);
            if (inRoom[static_cast<std::size_t>(ey * size + ex)] && IsFloor(end))
                Require(length == 1, context + ": a wall between rooms is " + std::to_string(length) + " cells thick" + where);
            else if (end == PLAIN || end == DESERT || end == FOREST || end == RIVER || end == LAKE || end == SEA)
                Require(length == 2, context + ": the outside wall is " + std::to_string(length) + " cells thick (want 2)" + where);
                // The inner wall is the same wall as the partitions between rooms; the outer wall is the darker one.
                if (length == 2)
                    Require(at(sx, sy) == MOUNTAIN && at(sx + dx[d], sy + dy[d]) == EXMOUNTAIN,
                        context + ": the inner wall must be the room-partition wall and the outer wall the dark one" + where);
        }
    }
    // No corner where two walls touch only diagonally while room floor shows through.
    for (int y = 0; y + 1 < size; ++y) for (int x = 0; x + 1 < size; ++x) {
        const bool a = IsWall(at(x, y)), b = IsWall(at(x + 1, y)), cc = IsWall(at(x, y + 1)), d = IsWall(at(x + 1, y + 1));
        const bool diagonal = (a && d && !b && !cc) || (b && cc && !a && !d);
        if (!diagonal) continue;
        const bool floorShows = (!a && IsFloor(at(x, y))) || (!b && IsFloor(at(x + 1, y))) ||
            (!cc && IsFloor(at(x, y + 1))) || (!d && IsFloor(at(x + 1, y + 1)));
        Require(!floorShows, context + ": a wall corner is open (diagonal gap) at " + std::to_string(x) + "," + std::to_string(y));
    }
}

// A wing is placed on the middle of the wall it is attached to.
void CheckWingsAreCentred(const Case& c) {
    std::string context;
    const MapPlan plan = Make(c, context);
    const int size = c.size;
    const MapPlanRegion* keep = nullptr;
    for (const auto& region : plan.regions) if (region.kind == "keep") keep = &region;
    Require(keep != nullptr, context + ": no keep");
    int wings = 0;
    for (const auto& region : plan.regions) {
        if (region.kind != "keep_expansion") continue;
        ++wings;
        const bool right = region.x0 >= keep->x1 - 1;
        const int probeX = right ? keep->x1 - 3 : keep->x0 + 3;
        // The wall is the run of rows (along the keep's edge) that stays flush against it and
        // holds the wing: a stepped keep face has several runs, and a wing sits on one of them.
        const auto flush = [&](int y) {
            const std::uint8_t b = plan.biomes[static_cast<std::size_t>(y * size + probeX)];
            return IsFloor(b) || b == BRIDGE;
        };
        const int wingMiddle = (region.y0 + region.y1) / 2;
        Require(wingMiddle >= keep->y0 && wingMiddle <= keep->y1 && flush(wingMiddle), context + ": no wall to attach the wing to");
        int lo = wingMiddle, hi = wingMiddle;
        while (lo - 1 >= keep->y0 && flush(lo - 1)) --lo;
        while (hi + 1 <= keep->y1 && flush(hi + 1)) ++hi;
        Require(region.y0 >= lo && region.y1 - 1 <= hi, context + ": the wing reaches past the flush run of the wall (" +
            std::to_string(lo) + ".." + std::to_string(hi) + ")");
        const int wallMiddle = (lo + hi) / 2;
        Require(std::abs(wallMiddle - wingMiddle) <= 1, context + ": the wing is not on the middle of the wall (wall " +
            std::to_string(wallMiddle) + ", wing " + std::to_string(wingMiddle) + ")");
    }
    (void)wings;
}
}

int main() {
    try {
        int cases = 0;
        for (const int size : {128, 160, 192}) for (int mode = 0; mode < 5; ++mode)
            for (unsigned seed : {1U, 2U, 3U, 4516451U}) for (const double wealth : {0.7, 1.0, 2.5}) {
                Check({size, mode, seed, wealth, 0});
                ++cases;
            }
        for (const int size : {128, 160}) for (int mode = 0; mode < 5; ++mode) for (const int rooms : {4, 12, 32}) {
            Check({size, mode, 7, 1.0, rooms});
            ++cases;
        }
        int wingCases = 0;
        for (const int size : {128, 160}) for (const int mode : {0, 1, 4}) for (unsigned seed = 1; seed <= QuickSeeds(8); ++seed) {
            CheckWingsAreCentred({size, mode, seed, 2.5, 0});
            ++wingCases;
        }
        std::cout << "CASTLE_WALL_THICKNESS: PASS " << cases << " cases, " << wingCases << " wing cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CASTLE_WALL_THICKNESS: FAIL " << error.what() << '\n';
        return 1;
    }
}
