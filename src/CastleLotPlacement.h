#pragma once

#include "CastleLotFacilities.h"
#include "MapPlan.h"

#include <algorithm>
#include <array>
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
inline int PlaceFacilities(MapPlan& plan, int mode, std::mt19937& rng, bool desert) {
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
                biomeAt(x, y) == openBiome && !reserved[idx(x, y)] && !taken[idx(x, y)];
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
                if (f.annex || perRole[static_cast<std::size_t>(i)] >= 2) continue;
                if ((f.minWidth <= cw && f.minHeight <= ch) || (f.minHeight <= cw && f.minWidth <= ch)) pool.push_back(i);
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
            for (const Cell c : cells) { biomeAt(c.x, c.y) = groundBiome; taken[idx(c.x, c.y)] = 1; }
            MapPlanRegion yard{"lot_facility", nextId++, cx0, cy0, cx1, cy1};
            yard.role = spec.role;
            yard.parentId = keepId;  // (a courtyard facility: it lies inside the keep)
            plan.regions.push_back(yard);
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
        if (randomBelow(100) < kNothingPercent) continue;
        std::vector<int> pool;
        for (int i = 0; i < static_cast<int>(catalog.size()); ++i) {
            const FacilitySpec& f = catalog[static_cast<std::size_t>(i)];
            if (perRole[static_cast<std::size_t>(i)] >= 2) continue;
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
            const int x0 = lx0 + randomBelow(lw - w + 1);
            const int y0 = ly0 + randomBelow(lh - h + 1);
            const int x1 = x0 + w - 1, y1 = y0 + h - 1;
            if (!clear(x0, y0, x1, y1)) continue;
            if (!spec.annex) {
                const std::uint8_t groundBiome = spec.ground == PLAIN ? openBiome : spec.ground;
                for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) { biomeAt(x, y) = groundBiome; taken[idx(x, y)] = 1; }
                // The four corners are cut by a cell, for a rounded shape.
                for (const Cell c : {Cell{x0, y0}, Cell{x1, y0}, Cell{x0, y1}, Cell{x1, y1}}) biomeAt(c.x, c.y) = openBiome;
                MapPlanRegion yard{"lot_facility", nextId++, x0, y0, x1, y1};
                yard.role = spec.role;
                plan.regions.push_back(yard);
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
                    paint(u, 7, BRIDGE); paint(u + 1, 7, BRIDGE);
                    inner.push_back({u, u + 1, 7, 7, c + 1, 0});
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
