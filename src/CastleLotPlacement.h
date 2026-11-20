#pragma once

#include "CastleInterior.h"
#include "CastleLotDecoration.h"
#include "CastleLotFacilities.h"
#include "CastleLotLayout.h"
#include "MapPlan.h"

#include <algorithm>
#include <array>
#include <functional>
#include <cstdlib>
#include <queue>
#include <random>
#include <string>
#include <utility>
#include <vector>

// 城の空き地に施設（屋外の区画とはなれ）を置く。完成した Plan に対して行う後処理で、城の部屋や家具には触れない。
// 仕様の正本は docs/references/castle-lot-facilities.md。
//  - 置く範囲は城の敷地（castle_ground）の中で、画像の外周 10% は使わず、素の地面（PLAIN。砂漠の城では DESERT）の上。
//  - 壁・道・石床・水・ほかの施設から 4 マス離す。鍵となる通用口の前（3 マス）には置かない。
//  - 屋外施設は kind "lot_facility"、はなれの外形は "lot_annex"、はなれの外扉は "lot_annex_door"、道は "lot_annex_road"。
//    はなれの入口は必ず南（下）の壁。屋外施設は、確保できた空き地の矩形いっぱいに置く。
//    はなれの部屋は kind "room" で、役割名は "annex_" で始まる（品質チェックの部屋数には数えない）。
namespace castle_lot {

namespace detail {
struct Cell { int x, y; };
}  // namespace detail

// 置いた施設の数を返す。mode は 0=同心円 1=丘 2=川 3=宮殿 4=要塞。
inline int PlaceFacilities(MapPlan& plan, int mode, std::mt19937& rng, bool desert, const MapGenParams& params) {
    using detail::Cell;
    const int W = plan.width, H = plan.depth;
    if (W <= 0 || H <= 0 || plan.biomes.size() != static_cast<std::size_t>(W) * static_cast<std::size_t>(H)) return 0;
    const std::uint8_t openBiome = static_cast<std::uint8_t>(desert ? DESERT : PLAIN);
    const MapPlanRegion* ground = nullptr;
    for (const MapPlanRegion& r : plan.regions) if (r.kind == "castle_ground") ground = &r;
    if (ground == nullptr) return 0;
    // The outer 10% of the image on every side is not used as a lot.
    const int edgeX = W / 10, edgeY = H / 10;
    const Cell groundMin{std::max(ground->x0, edgeX), std::max(ground->y0, edgeY)}, groundMax{std::min(ground->x1, W - 1 - edgeX), std::min(ground->y1, H - 1 - edgeY)};
    const auto in = [&](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H; };
    const auto idx = [&](int x, int y) { return static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x); };
    const auto biomeAt = [&](int x, int y) -> std::uint8_t& { return plan.biomes[idx(x, y)]; };
    const auto randomBelow = [&](int n) { return n <= 1 ? 0 : static_cast<int>(rng() % static_cast<unsigned>(n)); };

    // Ground that can be walked from the keep's public entrance (before anything is placed).
    std::vector<std::uint8_t> reach(plan.biomes.size(), 0);
    {
        const auto walkable = [&](int x, int y) {
            const int b = biomeAt(x, y);
            return b != WALL && b != INNERWALL && b != RIVER && b != LAKE && b != SEA && b != TOWN_SEA && b != TOWN_CLIFF;
        };
        std::queue<Cell> open;
        for (const MapPlanMarker& m : plan.markers)
            if (m.role == "keep_public_entry" && in(m.x, m.y) && walkable(m.x, m.y) && !reach[idx(m.x, m.y)]) { reach[idx(m.x, m.y)] = 1; open.push({m.x, m.y}); }
        while (!open.empty()) {
            const Cell c = open.front(); open.pop();
            for (const Cell d : {Cell{1, 0}, Cell{-1, 0}, Cell{0, 1}, Cell{0, -1}}) {
                const int nx = c.x + d.x, ny = c.y + d.y;
                if (!in(nx, ny) || reach[idx(nx, ny)] || !walkable(nx, ny)) continue;
                reach[idx(nx, ny)] = 1;
                open.push({nx, ny});
            }
        }
    }
    // The keep's side doors: their fronts stay free, and an annex road leads to the nearest.
    std::vector<std::uint8_t> reserved(plan.biomes.size(), 0), taken(plan.biomes.size(), 0);
    std::vector<Cell> targets;
    for (const MapPlanRegion& r : plan.regions) {
        if (r.kind != "castle_side_door") continue;
        for (int y = r.y0 - 3; y <= r.y1 + 3; ++y) for (int x = r.x0 - 3; x <= r.x1 + 3; ++x) if (in(x, y)) reserved[idx(x, y)] = 1;
        const bool vertical = r.x0 == r.x1;
        for (const int sign : {-1, 1}) {
            const Cell p = vertical ? Cell{r.x0 + sign * 3, (r.y0 + r.y1) / 2} : Cell{(r.x0 + r.x1) / 2, r.y0 + sign * 3};
            if (in(p.x, p.y) && biomeAt(p.x, p.y) == openBiome) targets.push_back(p);
        }
    }

    const auto clear = [&](int x0, int y0, int x1, int y1) {
        if (x0 < groundMin.x || y0 < groundMin.y || x1 > groundMax.x || y1 > groundMax.y) return false;
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
            if (biomeAt(x, y) != openBiome || reserved[idx(x, y)] || taken[idx(x, y)]) return false;
        for (int y = y0 - 4; y <= y1 + 4; ++y) for (int x = x0 - 4; x <= x1 + 4; ++x) {
            if (!in(x, y) || (x >= x0 && x <= x1 && y >= y0 && y <= y1)) continue;
            const std::uint8_t b = biomeAt(x, y);
            if (!(b == PLAIN || b == DESERT || b == FOREST) || taken[idx(x, y)]) return false;
        }
        return true;
    };

    int nextId = 80000;
    int placed = 0;
    const auto& catalog = Catalog();
    std::vector<int> perRole(catalog.size(), 0);
    std::vector<std::uint8_t> used(plan.biomes.size(), 0);  // lots already carved (with a facility or declined)

    // Decoration with the assets already registered in the editor's blocks (castle_fixture regions, as the keep's rooms have).
    std::vector<std::uint8_t> fixOcc(plan.biomes.size(), 0), doorZone(plan.biomes.size(), 0);
    const auto zone = [&](int x0, int y0, int x1, int y1, int margin) {
        for (int y = y0 - margin; y <= y1 + margin; ++y) for (int x = x0 - margin; x <= x1 + margin; ++x) if (in(x, y)) doorZone[idx(x, y)] = 1;
    };
    const auto decorate = [&](int parentIndex, const std::string& role, int ax0, int ay0, int ax1, int ay1, const std::function<bool(int, int)>& inside) {
        const std::vector<DecorItem> items = DecorationFor(role);
        if (items.empty()) return;
        const int parentId = plan.regions[static_cast<std::size_t>(parentIndex)].id;
        if (role == "annex_jail_cell") {
            // A cell is fitted the same way every time: the bed at the back left, the bucket at the back right (the door is on the south side).
            const castle_interior::Object bed = castle_interior::Native(params, 4, 14, 2, 4, 2), bucket = castle_interior::Native(params, 4, 41, 2, 2, 2);
            const auto put = [&](const char* name, const castle_interior::Object& m, int x, int y) {
                if (x + m.w - 1 > ax1 || y + m.d - 1 > ay1) return;
                MapPlanRegion f{"castle_fixture", 90000 + static_cast<int>(plan.regions.size()), x, y, x + m.w - 1, y + m.d - 1};
                f.role = name; f.rotation = 0;
                f.objectType = m.type; f.objectVariant = m.variant; f.objectHeight = m.h;
                f.parentIndex = parentIndex; f.parentId = parentId;
                plan.regions.push_back(f);
                for (int yy = y; yy < y + m.d; ++yy) for (int xx = x; xx < x + m.w; ++xx) fixOcc[idx(xx, yy)] = 1;
            };
            put("bed", bed, ax0, ay0);
            put("water_bucket", bucket, ax1 - bucket.w + 1, ay0);
            return;
        }
        int cellCount = 0;
        for (int y = ay0; y <= ay1; ++y) for (int x = ax0; x <= ax1; ++x) cellCount += inside(x, y) ? 1 : 0;
        const int budget = std::max(4, cellCount * 25 / 100);
        int usedArea = 0;
        for (const DecorItem& item : items) {
            const castle_interior::Object model = castle_interior::Native(params, item.type, item.variant, item.w, item.d, item.h);
            if (std::string(item.role) == "chair") continue;  // (the chairs stand by the table, below)
            for (int n = 0; n < item.count; ++n) {
                std::vector<Cell> candidates;
                for (int y = ay0; y + model.d - 1 <= ay1; ++y) for (int x = ax0; x + model.w - 1 <= ax1; ++x) {
                    if (item.wall && !(x == ax0 || y == ay0 || x + model.w - 1 == ax1 || y + model.d - 1 == ay1)) continue;
                    bool ok = true;
                    for (int yy = y - 1; yy <= y + model.d && ok; ++yy) for (int xx = x - 1; xx <= x + model.w; ++xx) {
                        if (!in(xx, yy)) continue;
                        const bool footprint = xx >= x && xx < x + model.w && yy >= y && yy < y + model.d;
                        if (footprint ? (!inside(xx, yy) || fixOcc[idx(xx, yy)] || doorZone[idx(xx, yy)]) : fixOcc[idx(xx, yy)] != 0) { ok = false; break; }
                    }
                    if (ok) candidates.push_back({x, y});
                }
                if (candidates.empty() || usedArea + model.w * model.d > budget) break;
                const Cell at = candidates[static_cast<std::size_t>(randomBelow(static_cast<int>(candidates.size())))];
                MapPlanRegion f{"castle_fixture", 90000 + static_cast<int>(plan.regions.size()), at.x, at.y, at.x + model.w - 1, at.y + model.d - 1};
                if (item.civic) f.kind = "settlement_facility";
                f.role = item.role;
                f.rotation = 0;
                f.objectType = model.type; f.objectVariant = model.variant; f.objectHeight = model.h;
                f.parentIndex = parentIndex; f.parentId = parentId;
                const int fixtureIndex = static_cast<int>(plan.regions.size());
                const int fixtureId = f.id;
                plan.regions.push_back(f);
                for (int y = at.y; y < at.y + model.d; ++y) for (int x = at.x; x < at.x + model.w; ++x) fixOcc[idx(x, y)] = 1;
                usedArea += model.w * model.d;
                if (std::string(item.role) == "table") {
                    // Chairs on either side of the table, each facing it (the back to the outside): rotation 2 on the left, 0 on the right.
                    const castle_interior::Object chair = castle_interior::Native(params, 4, 4, 2, 2, 3);
                    for (const int side : {-1, 1}) {
                        const int cx = side < 0 ? f.x0 - 1 - chair.w : f.x1 + 2, cy = f.y0 + (model.d - chair.d) / 2;
                        bool ok = cx >= ax0 && cy >= ay0 && cx + chair.w - 1 <= ax1 && cy + chair.d - 1 <= ay1;
                        for (int yy = cy - 1; yy <= cy + chair.d && ok; ++yy) for (int xx = cx - 1; xx <= cx + chair.w; ++xx) {
                            if (!in(xx, yy)) continue;
                            const bool fp = xx >= cx && xx < cx + chair.w && yy >= cy && yy < cy + chair.d;
                            const bool tableCell = xx >= f.x0 && xx <= f.x1 && yy >= f.y0 && yy <= f.y1;
                            if (tableCell) continue;
                            if (fp ? (!inside(xx, yy) || fixOcc[idx(xx, yy)] || doorZone[idx(xx, yy)]) : fixOcc[idx(xx, yy)] != 0) { ok = false; break; }
                        }
                        if (!ok) continue;
                        MapPlanRegion c{"castle_fixture", 90000 + static_cast<int>(plan.regions.size()), cx, cy, cx + chair.w - 1, cy + chair.d - 1};
                        c.role = "chair"; c.rotation = side < 0 ? 2 : 0;
                        c.objectType = chair.type; c.objectVariant = chair.variant; c.objectHeight = chair.h;
                        c.parentIndex = parentIndex; c.parentId = parentId;
                        plan.regions.push_back(c);
                        for (int yy = cy; yy < cy + chair.d; ++yy) for (int xx = cx; xx < cx + chair.w; ++xx) fixOcc[idx(xx, yy)] = 1;
                    }

                    // Papers on the table (a detail: it stands on the table's top).
                    const castle_interior::Object paper = castle_interior::Native(params, 4, 34, 2, 2, 1);
                    const int px = (f.x0 + f.x1 + 1 - paper.w) / 2, py = (f.y0 + f.y1 + 1 - paper.d) / 2;
                    MapPlanRegion detail{"castle_detail", 90000 + static_cast<int>(plan.regions.size()), px, py, px + paper.w - 1, py + paper.d - 1};
                    detail.role = "table_documents";
                    detail.objectType = paper.type; detail.objectVariant = paper.variant; detail.objectHeight = paper.h;
                    detail.parentId = fixtureId; detail.parentIndex = fixtureIndex;
                    plan.regions.push_back(detail);
                }
            }
        }
    };
    // A door like the keep's: the door itself (castle_door), two stone jambs (castle_portal) and its landing (castle_clearance).
    const auto emitDoor = [&](const MapPlanRegion& cells, int rotation) {
        const castle_interior::Object model = castle_interior::Native(params, 1, 8, 4, 1, 4);
        MapPlanRegion door = cells;
        door.kind = "castle_door"; door.role = "timber_door"; door.rotation = rotation;
        door.objectType = 1; door.objectVariant = model.variant; door.objectHeight = model.h;
        const int doorIndex = static_cast<int>(plan.regions.size());
        plan.regions.push_back(door);
        for (const int side : {-1, 1}) {
            MapPlanRegion post = cells;
            post.kind = "castle_portal"; post.role = "stone_door_jamb";
            post.parentIndex = doorIndex; post.parentId = door.id; post.objectHeight = model.h + 1;
            if (rotation % 2) { post.x0--; post.x1++; post.y0 = post.y1 = side < 0 ? cells.y0 - 1 : cells.y1 + 1; }
            else { post.y0--; post.y1++; post.x0 = post.x1 = side < 0 ? cells.x0 - 1 : cells.x1 + 1; }
            plan.regions.push_back(post);
        }
        MapPlanRegion landing = cells;
        landing.kind = "castle_clearance"; landing.role = "door_landing";
        landing.x0 = std::max(0, cells.x0 - 3); landing.y0 = std::max(0, cells.y0 - 3);
        landing.x1 = std::min(W - 1, cells.x1 + 3); landing.y1 = std::min(H - 1, cells.y1 + 3);
        plan.regions.push_back(landing);
        zone(cells.x0, cells.y0, cells.x1, cells.y1, 3);
    };

    // An outdoor facility is laid out by its pattern (CastleLotLayout.h): hedges, paths, rows and axes, not a scatter.
    const auto layoutYard = [&](int yardIndex, const std::string& role, int bx0, int by0, int bx1, int by1,
                                int kx0, int ky0, int kx1, int ky1, const std::function<bool(int, int)>& inside) {
        LayoutInput input;
        input.role = role;
        input.x0 = bx0; input.y0 = by0; input.x1 = bx1; input.y1 = by1;
        input.cx0 = kx0; input.cy0 = ky0; input.cx1 = kx1; input.cy1 = ky1;
        input.inside = inside;
        input.blocked = [&](int x, int y) { return !in(x, y) || reserved[idx(x, y)] || doorZone[idx(x, y)]; };
        input.desert = desert;
        input.wealth = plan.countryWealth;
        input.mode = mode;
        input.rng = &rng;
        input.params = &params;
        const int parentId = plan.regions[static_cast<std::size_t>(yardIndex)].id;
        const std::vector<Piece> pieces = LayoutYardPieces(input);
        std::vector<int> planIndex(pieces.size(), -1);
        for (std::size_t pieceNumber = 0; pieceNumber < pieces.size(); ++pieceNumber) {
            const Piece& piece = pieces[pieceNumber];
            if (piece.kind == "paint") {
                for (int y = piece.y; y < piece.y + piece.d; ++y) for (int x = piece.x; x < piece.x + piece.w; ++x)
                    if (in(x, y) && inside(x, y)) biomeAt(x, y) = static_cast<std::uint8_t>(piece.biome);
                continue;
            }
            MapPlanRegion r{piece.kind, 90000 + static_cast<int>(plan.regions.size()), piece.x, piece.y, piece.x + piece.w - 1, piece.y + piece.d - 1};
            r.role = piece.role;
            r.rotation = piece.rotation;
            if (piece.kind == "castle_fixture" || piece.kind == "settlement_facility") {
                r.objectType = piece.type; r.objectVariant = piece.variant; r.objectHeight = piece.height;
                r.parentIndex = yardIndex; r.parentId = parentId;
            }
            if (piece.kind == "castle_detail" && piece.parent >= 0 && planIndex[static_cast<std::size_t>(piece.parent)] >= 0) {
                // (a second tier or more: it stands on its base, raised by the base's height)
                r.objectType = piece.type; r.objectVariant = piece.variant; r.objectHeight = piece.height;
                r.parentIndex = planIndex[static_cast<std::size_t>(piece.parent)];
                r.parentId = plan.regions[static_cast<std::size_t>(r.parentIndex)].id;
            }
            planIndex[pieceNumber] = static_cast<int>(plan.regions.size());
            plan.regions.push_back(r);
        }
    };

    // The biggest free rectangle (at most 100 on a side) that keeps 4 cells clear all round: a lot.
    constexpr int kLotCap = 100, kClear = 4, kMinSide = 12;
    const auto findLot = [&](int& lx0, int& ly0, int& lx1, int& ly1) {
        const std::size_t stride = static_cast<std::size_t>(W) + 1;
        std::vector<int> insideBad((static_cast<std::size_t>(H) + 1) * stride, 0), ringBad(insideBad.size(), 0);
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            const std::uint8_t b = biomeAt(x, y);
            const bool blocked = taken[idx(x, y)] || used[idx(x, y)] || reserved[idx(x, y)];
            const int ib = (b != openBiome || blocked) ? 1 : 0, rb = (!(b == PLAIN || b == DESERT || b == FOREST) || blocked) ? 1 : 0;
            const std::size_t at = static_cast<std::size_t>(y + 1) * stride + static_cast<std::size_t>(x + 1);
            insideBad[at] = ib + insideBad[at - 1] + insideBad[at - stride] - insideBad[at - stride - 1];
            ringBad[at] = rb + ringBad[at - 1] + ringBad[at - stride] - ringBad[at - stride - 1];
        }
        const auto sum = [&](const std::vector<int>& t, int x0, int y0, int x1, int y1) {
            x0 = std::max(x0, 0); y0 = std::max(y0, 0); x1 = std::min(x1, W - 1); y1 = std::min(y1, H - 1);
            if (x0 > x1 || y0 > y1) return 0;
            return t[static_cast<std::size_t>(y1 + 1) * stride + static_cast<std::size_t>(x1 + 1)] - t[static_cast<std::size_t>(y0) * stride + static_cast<std::size_t>(x1 + 1)] -
                t[static_cast<std::size_t>(y1 + 1) * stride + static_cast<std::size_t>(x0)] + t[static_cast<std::size_t>(y0) * stride + static_cast<std::size_t>(x0)];
        };
        const auto valid = [&](int x0, int y0, int w, int h) {
            return sum(insideBad, x0, y0, x0 + w - 1, y0 + h - 1) == 0 && sum(ringBad, x0 - kClear, y0 - kClear, x0 + w - 1 + kClear, y0 + h - 1 + kClear) == 0;
        };
        int bestArea = 0;
        for (int y0 = groundMin.y; y0 + kMinSide - 1 <= groundMax.y; y0 += 2) for (int x0 = groundMin.x; x0 + kMinSide - 1 <= groundMax.x; x0 += 2) {
            for (int w = kMinSide; w <= std::min(kLotCap, groundMax.x - x0 + 1); w += 2) {
                if (!valid(x0, y0, w, kMinSide)) break;
                int lo = kMinSide, hi = std::min(kLotCap, groundMax.y - y0 + 1);
                while (lo < hi) { const int mid = (lo + hi + 1) / 2; if (valid(x0, y0, w, mid)) lo = mid; else hi = mid - 1; }
                const int area = w * lo + randomBelow(4);
                if (area > bestArea) { bestArea = area; lx0 = x0; ly0 = y0; lx1 = x0 + w - 1; ly1 = y0 + lo - 1; }
            }
        }
        return bestArea > 0;
    };

    // Courtyards: open ground that no way leads out of (walls and room floors all round): it cannot be reached from the map's edge.
    // Likelier to get a facility than other lots (kCourtyardNothingPercent); outdoor only, the biggest rectangle that fits, no clearance.
    {
        std::vector<std::uint8_t> outside(plan.biomes.size(), 0);
        std::queue<Cell> open;
        const auto passable = [&](int x, int y) {
            const int b = biomeAt(x, y);
            return b != WALL && b != INNERWALL && b != TOWN_STONE_FLOOR;
        };
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
            if ((x == 0 || y == 0 || x == W - 1 || y == H - 1) && passable(x, y)) { outside[idx(x, y)] = 1; open.push({x, y}); }
        while (!open.empty()) {
            const Cell c = open.front(); open.pop();
            for (const Cell d : {Cell{1, 0}, Cell{-1, 0}, Cell{0, 1}, Cell{0, -1}}) {
                const int nx = c.x + d.x, ny = c.y + d.y;
                if (!in(nx, ny) || outside[idx(nx, ny)] || !passable(nx, ny)) continue;
                outside[idx(nx, ny)] = 1;
                open.push({nx, ny});
            }
        }
        const int keepId = [&] { for (const MapPlanRegion& r : plan.regions) if (r.kind == "keep") return r.id; return 0; }();
        std::vector<int> comp(plan.biomes.size(), 0);
        int components = 0;
        const auto usable = [&](int x, int y) {
            return in(x, y) && x >= groundMin.x && y >= groundMin.y && x <= groundMax.x && y <= groundMax.y && !outside[idx(x, y)] &&
                biomeAt(x, y) == openBiome && !taken[idx(x, y)];
        };
        for (int sy = groundMin.y; sy <= groundMax.y; ++sy) for (int sx = groundMin.x; sx <= groundMax.x; ++sx) {
            if (comp[idx(sx, sy)] != 0 || !usable(sx, sy)) continue;
            ++components;
            std::vector<Cell> cells{{sx, sy}};
            comp[idx(sx, sy)] = components;
            int cx0 = sx, cy0 = sy, cx1 = sx, cy1 = sy;
            for (std::size_t k = 0; k < cells.size(); ++k) {
                const Cell c = cells[k];
                cx0 = std::min(cx0, c.x); cy0 = std::min(cy0, c.y); cx1 = std::max(cx1, c.x); cy1 = std::max(cy1, c.y);
                for (const Cell d : {Cell{1, 0}, Cell{-1, 0}, Cell{0, 1}, Cell{0, -1}}) {
                    const int nx = c.x + d.x, ny = c.y + d.y;
                    if (!usable(nx, ny) || comp[idx(nx, ny)] != 0) continue;
                    comp[idx(nx, ny)] = components;
                    cells.push_back({nx, ny});
                }
            }
            // The biggest rectangle inside the piece (histogram method).
            int bx0 = 0, by0 = 0, bx1 = -1, by1 = -1, bestArea = 0;
            std::vector<int> hist(static_cast<std::size_t>(W), 0);
            for (int y = cy1; y >= cy0; --y) {
                for (int x = cx0; x <= cx1; ++x) hist[static_cast<std::size_t>(x)] = comp[idx(x, y)] == components ? hist[static_cast<std::size_t>(x)] + 1 : 0;
                for (int x0 = cx0; x0 <= cx1; ++x0) {
                    int minH = 1 << 30;
                    for (int x1 = x0; x1 <= cx1 && hist[static_cast<std::size_t>(x1)] > 0; ++x1) {
                        minH = std::min(minH, hist[static_cast<std::size_t>(x1)]);
                        const int area = (x1 - x0 + 1) * minH;
                        if (area > bestArea) { bestArea = area; bx0 = x0; bx1 = x1; by0 = y; by1 = y + minH - 1; }
                    }
                }
            }
            if (bestArea == 0) continue;
            const int cw = bx1 - bx0 + 1, ch = by1 - by0 + 1;
            if (randomBelow(100) < kCourtyardNothingPercent) continue;
            std::vector<int> pool;
            for (int i = 0; i < static_cast<int>(catalog.size()); ++i) {
                const FacilitySpec& f = catalog[static_cast<std::size_t>(i)];
                if (f.annex || (std::string(f.role) == "yard_well" && perRole[static_cast<std::size_t>(i)] >= 1)) continue;
                if ((f.minWidth <= cw && f.minHeight <= ch) || (f.minHeight <= cw && f.minWidth <= ch)) pool.push_back(i);
            }
            // A small courtyard is never left empty for want of size: any facility but the big ones (it fills whatever the courtyard is).
            if (pool.empty() && std::min(cw, ch) >= 4)
                for (int i = 0; i < static_cast<int>(catalog.size()); ++i) {
                    const FacilitySpec& f = catalog[static_cast<std::size_t>(i)];
                    if (!f.annex && f.minWidth * f.minHeight <= 400 && !(std::string(f.role) == "yard_well" && perRole[static_cast<std::size_t>(i)] >= 1)) pool.push_back(i);
                }
            if (pool.empty()) continue;
            double total = 0.0;
            for (const int i : pool) total += Weight(catalog[static_cast<std::size_t>(i)], mode);
            double roll = std::uniform_real_distribution<double>(0.0, total)(rng);
            int specIndex = pool.back();
            for (const int i : pool) { roll -= Weight(catalog[static_cast<std::size_t>(i)], mode); if (roll <= 0.0) { specIndex = i; break; } }
            const FacilitySpec& spec = catalog[static_cast<std::size_t>(specIndex)];
            const std::uint8_t groundBiome = spec.ground == PLAIN ? openBiome : spec.ground;
            // The whole courtyard is the facility (every cell of the piece, not a rectangle in it); the region is its bounding box.
            // (the door fronts stay as they are under a forest ground)
            for (const Cell c : cells) { if (!(groundBiome == FOREST && reserved[idx(c.x, c.y)])) biomeAt(c.x, c.y) = groundBiome; taken[idx(c.x, c.y)] = 1; }
            MapPlanRegion yard{"lot_facility", nextId++, cx0, cy0, cx1, cy1};
            yard.role = spec.role;
            yard.parentId = keepId;  // (a courtyard facility: it lies inside the keep)
            const int yardIndex = static_cast<int>(plan.regions.size());
            plan.regions.push_back(yard);
            const int piece = components;
            // (laid out in the biggest rectangle of the courtyard; the rest of its shape stays bare)
            layoutYard(yardIndex, spec.role, cx0, cy0, cx1, cy1, bx0, by0, bx1, by1, [&](int x, int y) { return in(x, y) && comp[idx(x, y)] == piece; });
            ++perRole[static_cast<std::size_t>(specIndex)];
            ++placed;
        }
    }

    // One facility to a lot, chosen to suit the lot's size (a lot may stay empty: kNothingPercent).
    for (int lotIndex = 0; lotIndex < 24; ++lotIndex) {
        int lx0 = 0, ly0 = 0, lx1 = 0, ly1 = 0;
        if (!findLot(lx0, ly0, lx1, ly1)) break;
        const int lw = lx1 - lx0 + 1, lh = ly1 - ly0 + 1;
        for (int y = ly0; y <= ly1; ++y) for (int x = lx0; x <= lx1; ++x) used[idx(x, y)] = 1;
        // A big lot is half as likely to stay empty; the other half of that share goes to an outdoor facility.
        const bool large = lw * lh >= 600;
        const int lotRoll = randomBelow(100);
        bool outdoorOnly = false;
        if (large) { if (lotRoll < kNothingPercent / 2) continue; outdoorOnly = lotRoll < kNothingPercent; }
        else if (lotRoll < kNothingPercent) continue;
        std::vector<int> pool;
        for (int i = 0; i < static_cast<int>(catalog.size()); ++i) {
            const FacilitySpec& f = catalog[static_cast<std::size_t>(i)];
            if (perRole[static_cast<std::size_t>(i)] >= (std::string(f.role) == "yard_well" ? 1 : 2) || (outdoorOnly && f.annex)) continue;
            if ((f.minWidth <= lw && f.minHeight <= lh) || (f.minHeight <= lw && f.minWidth <= lh)) pool.push_back(i);
        }
        bool lotDone = false;
        for (int pick = 0; pick < 3 && !pool.empty() && !lotDone; ++pick) {
        // (a facility whose size is close to the lot's is likelier than a small one in a big lot)
        const auto weightOf = [&](int i) {
            const FacilitySpec& f = catalog[static_cast<std::size_t>(i)];
            return Weight(f, mode) * (1.0 + 3.0 * std::min(1.0, static_cast<double>(f.minWidth * f.minHeight) / static_cast<double>(lw * lh)));
        };
        double total = 0.0;
        for (const int i : pool) total += weightOf(i);
        double roll = std::uniform_real_distribution<double>(0.0, total)(rng);
        std::size_t chosen = 0;
        for (std::size_t k = 0; k < pool.size(); ++k) {
            roll -= weightOf(pool[k]);
            if (roll <= 0.0 || k + 1 == pool.size()) { chosen = k; break; }
        }
        const int specIndex = pool[chosen];
        const FacilitySpec& spec = catalog[static_cast<std::size_t>(specIndex)];
        // An annex is half as likely: the other half of its share leaves the lot empty.
        if (spec.annex && randomBelow(2) == 0) { lotDone = true; break; }
        bool done = false;
        for (int attemptAt = 0; attemptAt < 40 && !done; ++attemptAt) {
            // The size follows the lot: from the catalog size up to twice it, as far as the lot allows.
            int w = spec.minWidth + randomBelow(std::min(lw - spec.minWidth, spec.minWidth) + 1), h = spec.minHeight + randomBelow(std::min(lh - spec.minHeight, spec.minHeight) + 1);
            if (randomBelow(2) == 1 && h <= lw && w <= lh) std::swap(w, h);
            if (w > lw || h > lh) { std::swap(w, h); if (w > lw || h > lh) continue; }
            // An outdoor facility takes the whole lot (the biggest rectangle that could be had); only an annex varies in size.
            if (!spec.annex) { w = lw; h = lh; }
            int x0 = lx0 + randomBelow(lw - w + 1);
            const int y0 = ly0 + randomBelow(lh - h + 1);
            const int y1 = y0 + h - 1;
            if (spec.annex) {
                // The door is on the south wall. Where a side door of the keep lies south of the annex, within reach of the road,
                // slide the annex so that its door stands right in front of that side door (a straight road).
                const int doorColumn = spec.rooms == -1 ? 1 : spec.rooms == 2 ? 1 + (((w - 2) - 2) / 2 - 4) / 2 : 1 + ((w - 2) - 2 - 4) / 2;
                const Cell* nearestTarget = nullptr;
                int nearestDistance = 1 << 30;
                for (const Cell& t : targets) {
                    if (t.y <= y1 + 2 || t.y - y1 > 22) continue;
                    const int dx = std::abs(t.x - (x0 + doorColumn + 2));
                    if (dx < nearestDistance) { nearestDistance = dx; nearestTarget = &t; }
                }
                if (nearestTarget != nullptr) x0 = std::clamp(nearestTarget->x - doorColumn - 2, lx0, lx0 + lw - w);
            }
            const int x1 = x0 + w - 1;
            if (!clear(x0, y0, x1, y1)) continue;
            if (!spec.annex) {
                const std::uint8_t groundBiome = spec.ground == PLAIN ? openBiome : spec.ground;
                for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) { biomeAt(x, y) = groundBiome; taken[idx(x, y)] = 1; }
                // The four corners are cut by a cell, for a rounded shape.
                for (const Cell c : {Cell{x0, y0}, Cell{x1, y0}, Cell{x0, y1}, Cell{x1, y1}}) biomeAt(c.x, c.y) = openBiome;
                MapPlanRegion yard{"lot_facility", nextId++, x0, y0, x1, y1};
                yard.role = spec.role;
                const int yardIndex = static_cast<int>(plan.regions.size());
                plan.regions.push_back(yard);
                layoutYard(yardIndex, spec.role, x0, y0, x1, y1, x0 + 1, y0 + 1, x1 - 1, y1 - 1, [&](int x, int y) {
                    return !((x == x0 || x == x1) && (y == y0 || y == y1));  // (the corners are cut)
                });
                done = true;
                continue;
            }
            // The outer wall (the dark ring, as the keep has) is the outline's border; the wall proper lies just inside it.
            const int ix0 = x0 + 1, iy0 = y0 + 1, ix1 = x1 - 1, iy1 = y1 - 1, iw = w - 2, ih = h - 2;
            // An annex: pick the wall that faces the nearest side door, as long as its door can be walked to.
            // (side 0 left, 1 right, 2 top, 3 bottom; the local frame has u along the wall and v inward from it)
            const auto worldFor = [&](int side, int u, int v) {
                switch (side) {
                    case 3: return Cell{ix0 + u, iy1 - v};
                    case 2: return Cell{ix0 + u, iy0 + v};
                    case 1: return Cell{ix1 - v, iy0 + u};
                    default: return Cell{ix0 + v, iy0 + u};
                }
            };
            const auto doorUOf = [&](int lw) { return spec.rooms == -1 ? 1 : spec.rooms == 2 ? 1 + ((lw - 2) / 2 - 4) / 2 : 1 + (lw - 2 - 4) / 2; };
            int bestSide = -1, bestScore = 1 << 30;
            // (the door of an annex is always on the south wall: side 3)
            for (int side = 3; side < 4; ++side) {
                const bool across = side >= 2;
                const int lw = across ? iw : ih, lv = across ? ih : iw;
                // (every room is at least 8 x 8 inside, a jail cell 6 x 6 and the jail's hall 6 deep)
                if (spec.rooms == 1 && (lw < 10 || lv < 10)) continue;
                if (spec.rooms == 2 && (lw < 19 || lv < 10)) continue;
                if (spec.rooms == 0 && (lw < 22 || lv < 17)) continue;
                if (spec.rooms == -1 && (lw < 28 || lv < 10)) continue;
                const int du = doorUOf(lw);
                bool walkable = true;
                Cell front{0, 0};
                for (int k = 0; k < (spec.rooms == -1 ? lw - 2 : 4) && walkable; ++k) {
                    const Cell c = worldFor(side, du + k, -2);
                    walkable = in(c.x, c.y) && reach[idx(c.x, c.y)] && biomeAt(c.x, c.y) == openBiome;
                    if (k == 1) front = c;
                }
                if (!walkable) continue;
                int score = 1000 + randomBelow(8);
                for (const Cell& t : targets) score = std::min(score, std::abs(t.x - front.x) + std::abs(t.y - front.y) + randomBelow(8));
                if (score < bestScore) { bestScore = score; bestSide = side; }
            }
            if (bestSide < 0) continue;
            const int side = bestSide;
            const bool across = side >= 2;
            const int lw = across ? iw : ih, lv = across ? ih : iw;
            const auto world = [&](int u, int v) { return worldFor(side, u, v); };
            const auto paint = [&](int u, int v, std::uint8_t b) { const Cell c = world(u, v); biomeAt(c.x, c.y) = b; };
            const int outlineId = nextId++;
            struct LocalRoom { int ua, ub, va, vb; std::string role; int id; };
            std::vector<LocalRoom> locals;
            struct LocalDoor { int ua, ub, va, vb; int a, b; };
            std::vector<LocalDoor> inner;
            for (int v = 0; v < lv; ++v) for (int u = 0; u < lw; ++u)
                paint(u, v, (u == 0 || v == 0 || u == lw - 1 || v == lv - 1) ? static_cast<std::uint8_t>(WALL) : static_cast<std::uint8_t>(TOWN_STONE_FLOOR));
            int doorU = doorUOf(lw);
            std::vector<int> doorStarts;  // (the first cell of each exterior door along the south wall)
            if (spec.rooms == -1) {
                // A row house: rooms side by side, each 8 across with a wall between, each with a door of its own to the south.
                const int units = std::max(3, (lw - 1) / 9);
                int u = 1;
                for (int c = 0; c < units; ++c) {
                    const int uw = c + 1 == units ? (lw - 2) - (u - 1) : 8;
                    locals.push_back({u, u + uw - 1, 1, lv - 2, std::string(spec.role) + "_unit", nextId++});
                    doorStarts.push_back(u + (uw - 4) / 2);
                    if (c + 1 < units) for (int v = 1; v <= lv - 2; ++v) paint(u + uw, v, WALL);
                    u += uw + 1;
                }
            } else if (spec.rooms == 1) {
                locals.push_back({1, lw - 2, 1, lv - 2, spec.role, nextId++});
            } else if (spec.rooms == 2) {
                const int wall = 1 + (lw - 2) / 2;
                for (int v = 1; v <= lv - 2; ++v) paint(wall, v, WALL);
                for (int v = 1; v <= 4; ++v) paint(wall, v, BRIDGE);
                locals.push_back({1, wall - 1, 1, lv - 2, spec.role, nextId++});
                locals.push_back({wall + 1, lw - 2, 1, lv - 2, std::string(spec.role) + "_back", nextId++});
                inner.push_back({wall, wall, 1, 4, 0, 1});
            } else {
                const int interior = lw - 2;
                const int cells = interior >= 27 ? 3 + randomBelow(2) : 3;
                // The hall is 6 deep (rows 1..6), then a wall (row 7), then the cells (rows 8 and on), each at least 6 x 6.
                for (int u = 1; u <= lw - 2; ++u) paint(u, 7, WALL);
                locals.push_back({1, lw - 2, 1, 6, std::string(spec.role) + "_hall", nextId++});
                const int base = (interior - (cells - 1)) / cells, extra = (interior - (cells - 1)) % cells;
                int u = 1;
                for (int c = 0; c < cells; ++c) {
                    const int cw = base + (c < extra ? 1 : 0);
                    locals.push_back({u, u + cw - 1, 8, lv - 2, std::string(spec.role) + "_cell", nextId++});
                    for (int k = 1; k <= 4; ++k) paint(u + k, 7, BRIDGE);  // (a doorway 4 wide, in the middle of the cell)
                    inner.push_back({u + 1, u + 4, 7, 7, c + 1, 0});
                    if (c + 1 < cells) for (int v = 8; v <= lv - 2; ++v) paint(u + cw, v, WALL);
                    u += cw + 1;
                }
            }
            if (doorStarts.empty()) doorStarts.push_back(doorU);
            doorU = doorStarts.front();
            for (const int ds : doorStarts) for (int u = ds; u < ds + 4; ++u) paint(u, 0, BRIDGE);
            // The outer wall all round, with the door through it too.
            for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) if (x == x0 || x == x1 || y == y0 || y == y1) biomeAt(x, y) = INNERWALL;
            for (const int ds : doorStarts) for (int u = ds; u < ds + 4; ++u) paint(u, -1, BRIDGE);
            const auto box = [&](int ua, int ub, int va, int vb) {
                const Cell a = world(ua, va), b = world(ub, vb);
                return std::array<int, 4>{std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
            };
            MapPlanRegion outline{"lot_annex", outlineId, x0, y0, x1, y1};
            outline.role = spec.role;
            plan.regions.push_back(outline);
            for (const int ds : doorStarts) {
                const auto d = box(ds, ds + 3, -1, -1);
                MapPlanRegion door{"lot_annex_door", nextId++, d[0], d[1], d[2], d[3]};
                door.role = spec.role;
                door.parentId = outlineId;
                plan.regions.push_back(door);
            }
            for (const LocalRoom& r : locals) {
                const auto b = box(r.ua, r.ub, r.va, r.vb);
                MapPlanRegion room{"room", r.id, b[0], b[1], b[2], b[3]};
                room.role = r.role;
                plan.regions.push_back(room);
            }
            for (const LocalDoor& l : inner) {
                const auto b = box(l.ua, l.ub, l.va, l.vb);
                MapPlanRegion link{"room_connection", nextId++, b[0], b[1], b[2], b[3]};
                link.parentId = locals[static_cast<std::size_t>(l.a)].id;
                link.linkedRegionId = locals[static_cast<std::size_t>(l.b)].id;
                plan.regions.push_back(link);
            }
            // Doors like the keep's (the door, its two stone jambs and its landing): each exterior door on the south wall, and each
            // doorway between two rooms. Then the furniture, kept off the doors' landings.
            for (const int ds : doorStarts) {
                const auto b = box(ds, ds + 3, 0, 0);
                emitDoor(MapPlanRegion{"", nextId++, b[0], b[1], b[2], b[3]}, 0);
            }
            for (const LocalDoor& l : inner) {
                const auto b = box(l.ua, l.ub, l.va, l.vb);
                emitDoor(MapPlanRegion{"", nextId++, b[0], b[1], b[2], b[3]}, l.ua == l.ub ? 1 : 0);
            }
            for (const LocalRoom& r : locals) {
                const auto b = box(r.ua, r.ub, r.va, r.vb);
                int roomIndex = -1;
                for (int i = 0; i < static_cast<int>(plan.regions.size()); ++i) if (plan.regions[static_cast<std::size_t>(i)].kind == "room" && plan.regions[static_cast<std::size_t>(i)].id == r.id) roomIndex = i;
                if (roomIndex >= 0) decorate(roomIndex, r.role, b[0], b[1], b[2], b[3], [](int, int) { return true; });
            }
            // A road, 2 wide, straight or an L, at most 20 long, from the door to the nearest side door of the keep.
            const Cell front = world(doorU + 1, -2);
            if (!targets.empty() && in(front.x, front.y)) {
                const Cell* best = nullptr;
                int bestDistance = 1 << 30;
                for (const Cell& t : targets) {
                    const int dist = std::abs(t.x - front.x) + std::abs(t.y - front.y);
                    if (dist < bestDistance) { bestDistance = dist; best = &t; }
                }
                if (best != nullptr && bestDistance <= 20) {
                    for (const bool xFirst : {true, false}) {
                        std::vector<Cell> cells;
                        const Cell corner = xFirst ? Cell{best->x, front.y} : Cell{front.x, best->y};
                        const auto segment = [&](Cell a, Cell b) {
                            if (a.y == b.y) for (int x = std::min(a.x, b.x); x <= std::max(a.x, b.x); ++x) { cells.push_back({x, a.y}); cells.push_back({x, a.y + 1}); }
                            else for (int y = std::min(a.y, b.y); y <= std::max(a.y, b.y); ++y) { cells.push_back({a.x, y}); cells.push_back({a.x + 1, y}); }
                        };
                        segment(front, corner);
                        segment(corner, *best);
                        bool free = true;
                        for (const Cell& c : cells) {
                            if (!in(c.x, c.y) || biomeAt(c.x, c.y) != openBiome || taken[idx(c.x, c.y)] || c.x < groundMin.x || c.y < groundMin.y || c.x > groundMax.x || c.y > groundMax.y) { free = false; break; }
                            // (4 cells clear of the other facilities, whose clearance a road must not enter)
                            for (int dy = -4; dy <= 4 && free; ++dy) for (int dx = -4; dx <= 4; ++dx)
                                if (in(c.x + dx, c.y + dy) && taken[idx(c.x + dx, c.y + dy)]) { free = false; break; }
                            if (!free) break;
                        }
                        if (!free) continue;
                        int rx0 = W, ry0 = H, rx1 = 0, ry1 = 0;
                        for (const Cell& c : cells) {
                            biomeAt(c.x, c.y) = ROAD;
                            rx0 = std::min(rx0, c.x); ry0 = std::min(ry0, c.y); rx1 = std::max(rx1, c.x); ry1 = std::max(ry1, c.y);
                        }
                        MapPlanRegion road{"lot_annex_road", nextId++, rx0, ry0, rx1, ry1};
                        road.role = spec.role;
                        road.parentId = outlineId;
                        plan.regions.push_back(road);
                        break;
                    }
                }
            }
            for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) taken[idx(x, y)] = 1;
            done = true;
        }
        if (done) { ++placed; ++perRole[static_cast<std::size_t>(specIndex)]; lotDone = true; }
        else pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(chosen));
        }
    }
    return placed;
}

}  // namespace castle_lot
