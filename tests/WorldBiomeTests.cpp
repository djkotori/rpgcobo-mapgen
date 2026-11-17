#include "MapImageGenerator.h"
#include "MapQuality.h"
#include "WorldBiomes.h"
#include "WorldMapImageGenerator.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

int Count(const MapPlan& plan, std::uint8_t biome) {
    return static_cast<int>(std::count(plan.biomes.begin(), plan.biomes.end(), biome));
}

void CheckSnowPatches(const MapPlan& plan) {
    const int w = plan.width, h = plan.depth;
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(w * h), 0);
    std::vector<std::uint8_t> core(static_cast<std::size_t>(w * h), 0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const int i = y * w + x;
        if (plan.biomes[i] != SNOWFIELD) continue;
        bool inSquare = false;
        for (int top = y - 2; top <= y && !inSquare; ++top) for (int left = x - 2; left <= x; ++left) {
            if (top < 0 || left < 0 || top + 2 >= h || left + 2 >= w) continue;
            bool full = true;
            for (int oy = 0; oy < 3; ++oy) for (int ox = 0; ox < 3; ++ox) {
                if (plan.biomes[(top + oy) * w + left + ox] != SNOWFIELD) full = false;
            }
            if (full) { inSquare = true; break; }
        }
        core[i] = inSquare;
        if (visited[static_cast<std::size_t>(i)]) continue;
        std::queue<int> pending;
        pending.push(i);
        visited[static_cast<std::size_t>(i)] = 1;
        int area = 0;
        while (!pending.empty()) {
            const int cell = pending.front();
            pending.pop();
            ++area;
            const int neighbors[] = {cell % w ? cell - 1 : -1, cell % w + 1 < w ? cell + 1 : -1,
                cell >= w ? cell - w : -1, cell + w < w * h ? cell + w : -1};
            for (int next : neighbors) {
                if (next < 0 || visited[static_cast<std::size_t>(next)] || plan.biomes[next] != SNOWFIELD) continue;
                visited[static_cast<std::size_t>(next)] = 1;
                pending.push(next);
            }
        }
        Require(area >= 25, "snowfield component is smaller than 25 cells");
    }
    std::fill(visited.begin(), visited.end(), 0);
    for (int i = 0; i < w * h; ++i) {
        if (plan.biomes[i] != SNOWFIELD || core[i] || visited[i]) continue;
        std::queue<int> pending;
        pending.push(i);
        visited[i] = 1;
        int area = 0;
        bool touchesCore = false;
        while (!pending.empty()) {
            const int cell = pending.front();
            pending.pop();
            ++area;
            const int neighbors[] = {cell % w ? cell - 1 : -1, cell % w + 1 < w ? cell + 1 : -1,
                cell >= w ? cell - w : -1, cell + w < w * h ? cell + w : -1};
            for (int next : neighbors) {
                if (next < 0 || plan.biomes[next] != SNOWFIELD) continue;
                if (core[next]) touchesCore = true;
                else if (!visited[next]) { visited[next] = 1; pending.push(next); }
            }
        }
        Require(touchesCore && area <= 24, "snowfield has a thin region larger than merged biome remnants");
    }
}

void TestSnowCleanup() {
    constexpr int w = 96, h = 64;
    MapPlan original;
    original.Reset(w, h, 1, "world");
    original.biomes.assign(w * h, SEA);
    for (int y = 1; y < 15; ++y) {
        original.biomes[y * w + 5] = FOREST;
        for (int x = 12; x < 14; ++x) original.biomes[y * w + x] = DESERT;
    }
    for (int y = 2; y < 6; ++y) for (int x = 22; x < 26; ++x) original.biomes[y * w + x] = FOREST;
    for (int y = 1; y < 17; ++y) for (int x = 50; x < 80; ++x) {
        original.biomes[y * w + x] = (x < 65) ? DESERT : FOREST;
    }
    original.biomes[8 * w + 65] = ROAD;
    original.markers.push_back({"poi", 66, 8, 0});
    const std::vector<float> height(w * h, 0.5f);
    int largeSnow = 0;
    for (std::uint32_t seed = 1; seed <= 32; ++seed) {
        MapPlan output = original;
        ApplyWorldBiomes(output, height, seed, 0.0f, 1.0f);
        for (int y = 1; y < 15; ++y) {
            Require(output.biomes[y * w + 5] == FOREST, "one-cell forest strip was not restored");
            for (int x = 12; x < 14; ++x)
                Require(output.biomes[y * w + x] == DESERT, "two-cell desert strip was not restored");
        }
        for (int y = 2; y < 6; ++y) for (int x = 22; x < 26; ++x)
            Require(output.biomes[y * w + x] == FOREST, "small snow patch was not restored");
        Require(output.biomes[8 * w + 65] == ROAD && output.biomes[8 * w + 66] == FOREST,
            "snow cleanup overwrote road or marker");
        largeSnow += Count(output, SNOWFIELD);
        CheckSnowPatches(output);
    }
    Require(largeSnow > 0, "snow cleanup removed every large patch");
}

int CoastOnShore(const MapPlan& plan) {
    int result = 0;
    for (int y = 0; y < plan.depth; ++y) for (int x = 0; x < plan.width; ++x) {
        const int i = y * plan.width + x;
        if (plan.biomes[i] != COAST) continue;
        if ((x > 0 && plan.biomes[i - 1] == SEA) ||
            (x + 1 < plan.width && plan.biomes[i + 1] == SEA) ||
            (y > 0 && plan.biomes[i - plan.width] == SEA) ||
            (y + 1 < plan.depth && plan.biomes[i + plan.width] == SEA)) ++result;
    }
    return result;
}

void TestSnowStraightBoundaries() {
    constexpr int w = 256, h = 256;
    MapPlan original;
    original.Reset(w, h, 0, "world");
    original.biomes.assign(w * h, PLAIN);
    const std::vector<float> height(w * h, 0.5f);
    int longest = 0;
    int longEdges = 0;
    for (std::uint32_t seed = 1; seed <= 30; ++seed) {
        MapPlan output = original;
        ApplyWorldBiomes(output, height, seed, 0.0f, 0.5f);
        for (int direction = 0; direction < 2; ++direction) {
            for (int line = 8; line < 68; ++line) {
                int run = 0;
                for (int along = 8; along < w - 8; ++along) {
                    const int x = direction == 0 ? along : line;
                    const int y = direction == 0 ? line : along;
                    if (y >= 68) break;
                    const int i = y * w + x;
                    const int j = direction == 0 ? i - w : i - 1;
                    if ((output.biomes[i] == SNOWFIELD) != (output.biomes[j] == SNOWFIELD)) {
                        ++run;
                    } else {
                        if (run >= 12) ++longEdges;
                        longest = std::max(longest, run);
                        run = 0;
                    }
                }
                if (run >= 12) ++longEdges;
                longest = std::max(longest, run);
            }
        }
    }
    std::cout << "Snow straight edges: " << longEdges << ", longest=" << longest << '\n';
    Require(longEdges <= 12 && longest <= 18, "snowfield has too many long axis-aligned cutoffs");
}

void TestSnowLatitudeFade() {
    constexpr int w = 256, h = 256;
    MapPlan original;
    original.Reset(w, h, 0, "world");
    original.biomes.assign(w * h, PLAIN);
    const std::vector<float> height(w * h, 0.5f);
    int transitionSnow = 0, northernSnow = 0;
    for (std::uint32_t seed = 1; seed <= 30; ++seed) {
        MapPlan output = original;
        ApplyWorldBiomes(output, height, seed, 0.0f, 1.0f);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            if (output.biomes[y * w + x] != SNOWFIELD) continue;
            ++northernSnow;
            if (y >= 67 && y <= 73) ++transitionSnow;
            Require(y < 77, "snow extends beyond the northern zone");
        }
    }
    Require(transitionSnow > 0, "snow latitude transition was removed instead of faded");
    Require(northernSnow > 30 * 77 * w * 0.90, "full rate covers less than 90% of unobstructed northern land");
}

void TestShoreAndRates() {
    constexpr int w = 128;
    MapPlan original;
    original.Reset(w, w, 1, "world");
    original.biomes.assign(w * w, SEA);
    for (int y = 12; y < 112; ++y) for (int x = 8; x < 82; ++x) original.biomes[y * w + x] = PLAIN;
    for (int y = 50; y < 58; ++y) for (int x = 100; x < 108; ++x) original.biomes[y * w + x] = PLAIN;
    original.biomes[12 * w + 20] = ROAD;
    original.markers.push_back({"poi", 21, 12, 0});
    const std::vector<float> height(w * w, 0.5f);
    int totalFull = 0, totalHalf = 0, smallFull = 0, smallShore = 0, largeFull = 0, largeShore = 0;
    int maxWidth = 0;
    bool tapered = false, shortBeach = false, longBeach = false;
    for (std::uint32_t seed = 1; seed <= 256; ++seed) {
        MapPlan full = original;
        MapPlan half = original;
        MapPlan disabled = original;
        ApplyWorldBiomes(full, height, seed, 1.0f, 0.0f);
        ApplyWorldBiomes(half, height, seed, 0.5f, 0.0f);
        ApplyWorldBiomes(disabled, height, seed, 0.0f, 0.0f);
        Require(disabled.biomes == original.biomes, "zero rates changed terrain");
        Require(full.biomes[12 * w + 20] == ROAD && full.biomes[12 * w + 21] == PLAIN,
            "road or marker overwritten");
        int shore = 0, coastShore = 0, halfShore = 0;
        std::vector<int> distance(w * w, -1);
        std::queue<int> pending;
        for (int y = 1; y < w - 1; ++y) for (int x = 1; x < w - 1; ++x) {
            const int i = y * w + x;
            if (original.biomes[i] == SEA) continue;
            const bool adjacent = original.biomes[i - 1] == SEA || original.biomes[i + 1] == SEA ||
                original.biomes[i - w] == SEA || original.biomes[i + w] == SEA;
            if (adjacent) {
                ++shore;
                if (full.biomes[i] == COAST) {
                    ++coastShore;
                    distance[i] = 0;
                    pending.push(i);
                }
                if (half.biomes[i] == COAST) ++halfShore;
                if (x >= 100) {
                    ++smallShore;
                    if (full.biomes[i] == COAST) ++smallFull;
                } else {
                    ++largeShore;
                    if (full.biomes[i] == COAST) ++largeFull;
                }
            }
        }
        while (!pending.empty()) {
            const int i = pending.front();
            pending.pop();
            const int neighbors[] = {i % w ? i - 1 : -1, i % w + 1 < w ? i + 1 : -1,
                i >= w ? i - w : -1, i + w < w * w ? i + w : -1};
            for (int next : neighbors) {
                if (next < 0 || full.biomes[next] != COAST || distance[next] >= 0) continue;
                distance[next] = distance[i] + 1;
                pending.push(next);
            }
        }
        for (int i = 0; i < w * w; ++i) if (full.biomes[i] == COAST) {
            Require(distance[i] >= 0 && distance[i] <= 4, "coast disconnected from shore or wider than five cells");
            Require(original.biomes[i] == PLAIN || original.biomes[i] == DESERT || original.biomes[i] == FOREST,
                "coast overwrote protected terrain");
            maxWidth = std::max(maxWidth, distance[i] + 1);
        }
        for (int i = 0; i < w * w; ++i) {
            if (half.biomes[i] == COAST) Require(full.biomes[i] == COAST, "lower coastRate added coast");
        }
        // On the straight north shore, check that a long segment widens inward
        // and returns to a single-cell tip at each end.
        for (int x = 9; x < 81;) {
            if (full.biomes[12 * w + x] != COAST) { ++x; continue; }
            const int begin = x;
            while (x < 81 && full.biomes[12 * w + x] == COAST) ++x;
            const int end = x - 1;
            if (begin < 10 || end >= 80 || begin <= 22 && end >= 19) continue;
            int widest = 0;
            for (int px = begin; px <= end; ++px) {
                int width = 0;
                while (width < 6 && full.biomes[(12 + width) * w + px] == COAST) ++width;
                widest = std::max(widest, width);
            }
            const int length = end - begin + 1;
            if (length >= 4 && length <= 7 && widest >= 2 && widest <= 3) shortBeach = true;
            if (length >= 12 && widest == 5) longBeach = true;
            tapered = tapered || (length >= 12 && widest >= 3 && full.biomes[13 * w + begin] != COAST &&
                full.biomes[13 * w + end] != COAST);
        }
        Require(coastShore >= shore * 0.40 && coastShore <= shore * 0.50, "full-rate coastal shoreline ratio outside 40%-50%");
        totalFull += coastShore;
        totalHalf += halfShore;
    }
    Require(maxWidth == 5, "beaches did not reach five cells, max=" + std::to_string(maxWidth));
    Require(tapered, "beaches did not taper at both ends");
    Require(shortBeach && longBeach, "beach width did not increase with shoreline length");
    Require(totalHalf > totalFull * 0.30 && totalHalf < totalFull * 0.70, "coastRate did not affect generation probability");
    Require(static_cast<double>(smallFull) / smallShore > static_cast<double>(largeFull) / largeShore,
        "small island shore was not favored");
}

void TestSnow() {
    constexpr int w = 128;
    MapPlan original;
    original.Reset(w, w, 1, "world");
    original.biomes.assign(w * w, PLAIN);
    for (int x = 54; x <= 74; ++x) {
        original.biomes[79 * w + x] = MOUNTAIN;
        original.biomes[99 * w + x] = MOUNTAIN;
    }
    for (int y = 79; y <= 99; ++y) {
        original.biomes[y * w + 54] = MOUNTAIN;
        original.biomes[y * w + 74] = MOUNTAIN;
    }
    original.biomes[10 * w + 10] = ROAD;
    original.markers.push_back({"poi", 11, 10, 0});
    const std::vector<float> height(w * w, 0.5f);
    int northTop = 0, northBottom = 0, basin = 0, fullCount = 0, halfCount = 0;
    int northOnlyCount = 0, middleBasin = 0;
    for (std::uint32_t seed = 1; seed <= 128; ++seed) {
        MapPlan full = original, half = original, northOnly = original, midMountain = original, explicitFull = original;
        ApplyWorldBiomes(full, height, seed, 0.0f, 1.0f);
        ApplyWorldBiomes(half, height, seed, 0.0f, 0.5f);
        ApplyWorldBiomes(northOnly, height, seed, 0.0f, 1.0f, 0.0f);
        ApplyWorldBiomes(midMountain, height, seed, 0.0f, 1.0f, 0.5f);
        ApplyWorldBiomes(explicitFull, height, seed, 0.0f, 1.0f, 1.0f);
        Require(full.biomes == explicitFull.biomes, "mountain snow default changed");
        CheckSnowPatches(full);
        CheckSnowPatches(half);
        CheckSnowPatches(northOnly);
        fullCount += Count(full, SNOWFIELD);
        halfCount += Count(half, SNOWFIELD);
        Require(full.biomes[10 * w + 10] == ROAD && full.biomes[10 * w + 11] == PLAIN,
            "snow overwrote road or marker");
        for (int y = 0; y < w; ++y) for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            if (y < 39) {
                Require(full.biomes[i] == northOnly.biomes[i] && full.biomes[i] == midMountain.biomes[i],
                    "mountain rate changed isolated northern snow");
                if (northOnly.biomes[i] == SNOWFIELD) ++northOnlyCount;
            } else {
                Require(northOnly.biomes[i] != SNOWFIELD, "mountain rate zero left southern snow");
                if (midMountain.biomes[i] == SNOWFIELD) ++middleBasin;
            }
            if (full.biomes[y * w + x] != SNOWFIELD) continue;
            if (y < 13) ++northTop;
            else if (y >= 26 && y < 39) ++northBottom;
            if (y >= 79 && y <= 99 && x >= 54 && x <= 74) ++basin;
            if (y >= 39) Require(x > 54 && x < 74 && y > 79 && y < 99,
                "southern snowfield outside mountain basin");
        }
    }
    Require(northTop > northBottom, "snow probability does not increase northward");
    Require(basin > 0, "mountain basin snowfield missing");
    Require(northOnlyCount > 0, "mountain rate zero removed northern snow");
    Require(middleBasin > 0 && middleBasin < basin, "mountain snow rate does not affect basin coverage");
    Require(halfCount > fullCount * 0.30 && halfCount < fullCount * 0.70, "snowfieldRate did not affect generation probability");
}

void TestMountainEnclosureAndIndependentRates() {
    constexpr int w = 160, h = 128;
    MapPlan closed;
    closed.Reset(w, h, 1, "world");
    closed.biomes.assign(w * h, PLAIN);
    for (int x = 60; x <= 84; ++x) {
        closed.biomes[72 * w + x] = MOUNTAIN;
        closed.biomes[96 * w + x] = EXMOUNTAIN;
    }
    for (int y = 72; y <= 96; ++y) {
        closed.biomes[y * w + 60] = MOUNTAIN;
        closed.biomes[y * w + 84] = EXMOUNTAIN;
    }
    closed.biomes[80 * w + 70] = ROAD;
    closed.markers.push_back({"poi", 73, 85, 0});
    MapPlan gap = closed, threeSides = closed, oneSide = closed, waterGap = closed;
    MapPlan overlap;
    overlap.Reset(w, h, 1, "world");
    overlap.biomes.assign(w * h, PLAIN);
    for (int x = 60; x <= 84; ++x) {
        overlap.biomes[5 * w + x] = MOUNTAIN;
        overlap.biomes[29 * w + x] = MOUNTAIN;
    }
    for (int y = 5; y <= 29; ++y) {
        overlap.biomes[y * w + 60] = MOUNTAIN;
        overlap.biomes[y * w + 84] = MOUNTAIN;
    }
    for (int y = 72; y <= 96; ++y) {
        threeSides.biomes[y * w + 84] = PLAIN;
        waterGap.biomes[y * w + 84] = SEA;
        oneSide.biomes[y * w + 60] = PLAIN;
        oneSide.biomes[y * w + 84] = PLAIN;
    }
    for (int x = 60; x <= 84; ++x) oneSide.biomes[96 * w + x] = PLAIN;
    for (int y = 82; y <= 84; ++y) gap.biomes[y * w + 84] = PLAIN;
    const std::vector<float> height(w * h, 0.5f);
    int totalHalf = 0, totalFull = 0, overlapCells = 0;
    for (std::uint32_t seed = 1; seed <= 32; ++seed) {
        MapPlan north = closed, mountain = closed, combined = closed, half = closed, empty = closed;
        MapPlan partial = gap, open = threeSides, slope = oneSide, water = waterGap;
        MapPlan overlapNorth = overlap, overlapMountain = overlap, overlapBoth = overlap;
        ApplyWorldBiomes(north, height, seed, 0.0f, 1.0f, 0.0f);
        ApplyWorldBiomes(mountain, height, seed, 0.0f, 0.0f, 1.0f);
        ApplyWorldBiomes(combined, height, seed, 0.0f, 1.0f, 1.0f);
        ApplyWorldBiomes(half, height, seed, 0.0f, 0.0f, 0.5f);
        ApplyWorldBiomes(empty, height, seed, 0.0f, 0.0f, 0.0f);
        ApplyWorldBiomes(partial, height, seed, 0.0f, 0.0f, 1.0f);
        ApplyWorldBiomes(open, height, seed, 0.0f, 0.0f, 1.0f);
        ApplyWorldBiomes(slope, height, seed, 0.0f, 0.0f, 1.0f);
        ApplyWorldBiomes(water, height, seed, 0.0f, 0.0f, 1.0f);
        ApplyWorldBiomes(overlapNorth, height, seed, 0.0f, 1.0f, 0.0f);
        ApplyWorldBiomes(overlapMountain, height, seed, 0.0f, 0.0f, 1.0f);
        ApplyWorldBiomes(overlapBoth, height, seed, 0.0f, 1.0f, 1.0f);
        Require(empty.biomes == closed.biomes, "both snow rates zero changed terrain");
        Require(Count(mountain, SNOWFIELD) > 0, "enclosed mountain basin has no snow");
        Require(Count(partial, SNOWFIELD) > 0, "small opening eliminated nearly enclosed basin");
        Require(Count(open, SNOWFIELD) == 0 && Count(slope, SNOWFIELD) == 0 &&
            Count(water, SNOWFIELD) == 0, "open mountain terrain was treated as enclosed");
        Require(mountain.biomes[80 * w + 70] == ROAD && mountain.biomes[85 * w + 73] == PLAIN,
            "mountain snow overwrote a protected cell");
        CheckSnowPatches(mountain);
        totalFull += Count(mountain, SNOWFIELD);
        totalHalf += Count(half, SNOWFIELD);
        for (int i = 0; i < w * h; ++i) {
            const bool expectedSnow = north.biomes[i] == SNOWFIELD || mountain.biomes[i] == SNOWFIELD;
            Require((combined.biomes[i] == SNOWFIELD) == expectedSnow,
                "northern and mountain snow rates are not independent");
            const bool northSnow = overlapNorth.biomes[i] == SNOWFIELD;
            const bool mountainSnow = overlapMountain.biomes[i] == SNOWFIELD;
            if (northSnow && mountainSnow) ++overlapCells;
            Require((overlapBoth.biomes[i] == SNOWFIELD) == (northSnow || mountainSnow),
                "overlapping snow sources are not independent");
            if (mountain.biomes[i] == SNOWFIELD) {
                Require(i % w > 60 && i % w < 84 && i / w > 72 && i / w < 96,
                    "mountain snow escaped the enclosed basin");
            }
        }
    }
    Require(totalHalf > 0 && totalHalf < totalFull, "mountain rate does not affect enclosed basin snow");
    Require(overlapCells > 0, "test did not exercise overlapping northern and mountain snow");
}

void TestDesertBasinsAndWholeRegionDraws() {
    constexpr int w = 128;
    MapPlan closed;
    closed.Reset(w, w, 1, "world");
    closed.biomes.assign(w * w, DESERT);
    for (int x = 54; x <= 74; ++x) {
        closed.biomes[79 * w + x] = MOUNTAIN;
        closed.biomes[99 * w + x] = MOUNTAIN;
    }
    for (int y = 79; y <= 99; ++y) {
        closed.biomes[y * w + 54] = MOUNTAIN;
        closed.biomes[y * w + 74] = MOUNTAIN;
    }
    MapPlan open = closed;
    for (int y = 85; y <= 91; ++y) open.biomes[y * w + 74] = DESERT;
    const std::vector<float> height(w * w, 0.5f);
    int selected = 0;
    for (std::uint32_t seed = 1; seed <= 64; ++seed) {
        MapPlan full = closed, half = closed, exposed = open;
        ApplyWorldBiomes(full, height, seed, 0.0f, 0.0f, 1.0f);
        ApplyWorldBiomes(half, height, seed, 0.0f, 0.0f, 0.5f);
        ApplyWorldBiomes(exposed, height, seed, 0.0f, 0.0f, 1.0f);
        Require(Count(exposed, SNOWFIELD) == 0, "open desert was misclassified as a mountain basin");
        Require(Count(full, SNOWFIELD) == 19 * 19, "full-rate snow did not fill the whole enclosed desert");
        const int halfArea = Count(half, SNOWFIELD);
        Require(halfArea == 0 || halfArea == 19 * 19, "rate draw sliced a snowfield into partial regions");
        if (halfArea) ++selected;
    }
    Require(selected > 16 && selected < 48, "whole-basin rate draw is not approximately calibrated");
}

void TestSnowRemnantCleanup() {
    constexpr int w = 96;
    MapPlan original;
    original.Reset(w, w, 1, "world");
    original.biomes.assign(w * w, MOUNTAIN);
    for (int y = 50; y <= 69; ++y) for (int x = 20; x <= 39; ++x)
        original.biomes[y * w + x] = SNOWFIELD;
    const std::vector<int> remnants = {55 * w + 19, 60 * w + 40, 61 * w + 40,
        49 * w + 30, 49 * w + 31, 55 * w + 25, 56 * w + 25, 60 * w + 28};
    original.biomes[remnants[0]] = PLAIN;
    original.biomes[remnants[1]] = original.biomes[remnants[2]] = DESERT;
    original.biomes[remnants[3]] = original.biomes[remnants[4]] = FOREST;
    original.biomes[remnants[5]] = original.biomes[remnants[6]] = PLAIN;
    original.biomes[remnants[7]] = FOREST;
    for (int y = 65; y <= 67; ++y) for (int x = 40; x <= 42; ++x)
        original.biomes[y * w + x] = DESERT;
    original.biomes[60 * w + 32] = PLAIN;
    original.markers.push_back({"poi", 32, 60, 0});
    const std::uint8_t protectedBiomes[] = {ROAD, RIVER, LAKE, BRIDGE, COAST, EXMOUNTAIN};
    for (int k = 0; k < 6; ++k) original.biomes[64 * w + 22 + k] = protectedBiomes[k];
    MapPlan output = original;
    ApplyWorldBiomes(output, std::vector<float>(w * w, 0.5f), 42, 0.0f, 1.0f, 0.0f);
    for (int i = 0; i < w * w; ++i) {
        const bool merge = std::find(remnants.begin(), remnants.end(), i) != remnants.end();
        Require(output.biomes[i] == (merge ? SNOWFIELD : original.biomes[i]),
            "snow cleanup missed a small remnant or changed protected/large terrain");
    }
}

void TestWorldRateCalibration() {
    long long defaultCoast = 0, fullCoast = 0, defaultSnow = 0, fullSnow = 0;
    long long eligibleNorth = 0, coveredNorth = 0;
    long long defaultShore = 0, fullShore = 0;
    for (int mode = 0; mode < 3; ++mode) {
        for (std::uint32_t seed : {11U, 42U, 101U, 202U, 303U, 404U, 505U, 606U, 707U, 909U}) {
            MapGenParams params;
            params.width = 256;
            params.depth = 256;
            params.seed = seed;
            params.extra["macroArchipelagoWeight"] = mode == 0 ? 1 : 0;
            params.extra["macroContinentWeight"] = mode == 1 ? 1 : 0;
            params.extra["macroBasinWeight"] = mode == 2 ? 1 : 0;
            MapPlan defaults, explicitDefaults, coastMax, snowMax;
            std::string error;
            Require(WorldMapImageGenerator(params).Generate(defaults, error), error);
            params.extra["coastRate"] = 0.3;
            params.extra["snowfieldRate"] = 0.5;
            params.extra["mountainSnowfieldRate"] = 1.0;
            Require(WorldMapImageGenerator(params).Generate(explicitDefaults, error), error);
            Require(defaults.biomes == explicitDefaults.biomes, "world biome defaults do not match explicit rates");
            params.extra["coastRate"] = 1.0;
            Require(WorldMapImageGenerator(params).Generate(coastMax, error), error);
            params.extra["coastRate"] = 0.3;
            params.extra["snowfieldRate"] = 1.0;
            Require(WorldMapImageGenerator(params).Generate(snowMax, error), error);
            defaultCoast += Count(defaults, COAST);
            fullCoast += Count(coastMax, COAST);
            defaultShore += CoastOnShore(defaults);
            fullShore += CoastOnShore(coastMax);
            defaultSnow += Count(defaults, SNOWFIELD);
            fullSnow += Count(snowMax, SNOWFIELD);
            for (int y = 0; y < 0.30 * (snowMax.depth - 1); ++y) for (int x = 0; x < snowMax.width; ++x) {
                const auto biome = snowMax.biomes[y * snowMax.width + x];
                if (biome == PLAIN || biome == DESERT || biome == FOREST || biome == SNOWFIELD) {
                    ++eligibleNorth;
                    if (biome == SNOWFIELD) ++coveredNorth;
                }
            }
        }
    }
    Require(fullShore > defaultShore * 2.5 && fullShore < defaultShore * 4.0,
        "full rate shoreline is not roughly three times the default");
    Require(fullCoast > defaultCoast * 2.5 && fullCoast < defaultCoast * 4.2,
        "full coast rate is not roughly three times the default area");
    Require(fullSnow > defaultSnow * 1.5 && fullSnow < defaultSnow * 2.5,
        "full snow rate is not roughly twice the default area");
    constexpr long long previousDefaultSnow = 30708;
    Require(defaultSnow > previousDefaultSnow * 1.7 && defaultSnow < previousDefaultSnow * 2.5,
        "default snow area is not roughly twice the previous 30-map baseline");
    Require(coveredNorth > eligibleNorth * 0.85, "full rate covers less than 85% of eligible northern land: " +
        std::to_string(coveredNorth) + "/" + std::to_string(eligibleNorth));
    std::cout << "Coast area default/full: " << defaultCoast << "/" << fullCoast
              << ", shoreline: " << defaultShore << "/" << fullShore
              << ", snow area: " << defaultSnow << "/" << fullSnow
              << ", northern coverage: " << coveredNorth << "/" << eligibleNorth << '\n';
}

void TestGeneratedWorlds() {
    for (int size : {64, 128, 256}) for (int mode = 0; mode < 3; ++mode) for (std::uint32_t seed : {11U, 42U, 101U}) {
        MapGenParams params;
        params.width = size; params.depth = size; params.seed = seed;
        params.extra["macroArchipelagoWeight"] = mode == 0 ? 1 : 0;
        params.extra["macroContinentWeight"] = mode == 1 ? 1 : 0;
        params.extra["macroBasinWeight"] = mode == 2 ? 1 : 0;
        params.extra["coastRate"] = 0;
        params.extra["snowfieldRate"] = 0;
        params.extra["mountainSnowfieldRate"] = 0;
        MapPlan baseline, output, repeat;
        std::string error;
        Require(WorldMapImageGenerator(params).Generate(baseline, error), error);
        params.extra["coastRate"] = 1;
        params.extra["snowfieldRate"] = 1;
        params.extra["mountainSnowfieldRate"] = 1;
        WorldMapImageGenerator generator(params);
        Require(generator.Generate(output, error) && generator.Generate(repeat, error), error);
        Require(output.biomes == repeat.biomes, "world is not deterministic");
        CheckSnowPatches(output);
        Require(Count(output, COAST) > 0, "coast missing from generated world");
        int shoreline = 0, coastalShoreline = 0;
        for (int y = 0; y < output.depth; ++y) for (int x = 0; x < output.width; ++x) {
            const int i = y * output.width + x;
            const auto biome = output.biomes[static_cast<std::size_t>(i)];
            if (biome == SEA || biome == LAKE || biome == RIVER || biome == BRIDGE) continue;
            if ((x > 0 && output.biomes[i - 1] == SEA) ||
                (x + 1 < output.width && output.biomes[i + 1] == SEA) ||
                (y > 0 && output.biomes[i - output.width] == SEA) ||
                (y + 1 < output.depth && output.biomes[i + output.width] == SEA)) {
                ++shoreline;
                if (biome == COAST) ++coastalShoreline;
            }
        }
        Require(coastalShoreline <= shoreline * 0.46 && coastalShoreline > 0,
            "generated world coast exceeds full-rate shoreline target");
        for (std::size_t i = 0; i < output.biomes.size(); ++i) {
            if (output.biomes[i] == COAST || output.biomes[i] == SNOWFIELD) {
                Require(baseline.biomes[i] == PLAIN || baseline.biomes[i] == DESERT || baseline.biomes[i] == FOREST,
                    "new biome replaced protected terrain");
            } else {
                Require(output.biomes[i] == baseline.biomes[i], "new biome pass changed other terrain: mode=" +
                    std::to_string(mode) + " seed=" + std::to_string(seed) + " cell=" + std::to_string(i) +
                    " before=" + std::to_string(baseline.biomes[i]) + " after=" + std::to_string(output.biomes[i]));
            }
        }
        const auto before = EvaluateMapQuality(baseline);
        const auto after = EvaluateMapQuality(output);
        Require(before.walkableCells == after.walkableCells &&
            before.largestWalkableComponent == after.largestWalkableComponent &&
            before.landRatio == after.landRatio, "new biome changed world passability or land accounting");
        const auto coastColor = generator.BiomeToColor(COAST);
        const auto snowColor = generator.BiomeToColor(SNOWFIELD);
        Require(coastColor.r != 255 && snowColor.r != 255, "missing biome color");
    }
}

void TestParameterValidation() {
    MapGenParams params;
    params.width = 64;
    params.depth = 64;
    params.extra["coastRate"] = std::numeric_limits<double>::quiet_NaN();
    MapPlan plan;
    std::string error;
    Require(!WorldMapImageGenerator(params).Generate(plan, error) && !error.empty(), "invalid coastRate was accepted");
    params.extra["coastRate"] = 1.0;
    params.extra["snowfieldRate"] = std::numeric_limits<double>::infinity();
    Require(!WorldMapImageGenerator(params).Generate(plan, error) && !error.empty(), "invalid snowfieldRate was accepted");
    params.extra["snowfieldRate"] = 0.5;
    params.extra["mountainSnowfieldRate"] = std::numeric_limits<double>::quiet_NaN();
    Require(!WorldMapImageGenerator(params).Generate(plan, error) && !error.empty(), "invalid mountainSnowfieldRate was accepted");
    params.extra["mountainSnowfieldRate"] = std::numeric_limits<double>::infinity();
    Require(!WorldMapImageGenerator(params).Generate(plan, error) && !error.empty(), "infinite mountainSnowfieldRate was accepted");
}

void TestMountainRateInWorldGenerator() {
    MapGenParams params;
    params.width = 256;
    params.depth = 256;
    params.seed = 606;
    params.extra["snowfieldRate"] = 1;
    params.extra["mountainSnowfieldRate"] = 0;
    MapPlan disabled, clamped, enabled, repeat, mountainOnly;
    std::string error;
    Require(WorldMapImageGenerator(params).Generate(disabled, error), error);
    params.extra["mountainSnowfieldRate"] = -1;
    Require(WorldMapImageGenerator(params).Generate(clamped, error), error);
    Require(disabled.biomes == clamped.biomes, "mountain snow rate was not clamped to zero");
    params.extra["mountainSnowfieldRate"] = 1;
    WorldMapImageGenerator generator(params);
    Require(generator.Generate(enabled, error) && generator.Generate(repeat, error), error);
    params.extra["snowfieldRate"] = 0;
    Require(WorldMapImageGenerator(params).Generate(mountainOnly, error), error);
    Require(enabled.biomes == repeat.biomes, "mountain snow generation is not deterministic");
    int northSnow = 0, southernEnabled = 0;
    for (int y = 0; y < params.depth; ++y) for (int x = 0; x < params.width; ++x) {
        const int i = y * params.width + x;
        Require((enabled.biomes[i] == SNOWFIELD) ==
            (disabled.biomes[i] == SNOWFIELD || mountainOnly.biomes[i] == SNOWFIELD),
            "world generator couples northern and mountain snow rates");
        if (y < 0.30 * params.depth) {
            if (disabled.biomes[i] == SNOWFIELD) ++northSnow;
        } else {
            Require(disabled.biomes[i] != SNOWFIELD, "disabled mountain snow appeared outside northern zone");
            if (mountainOnly.biomes[i] == SNOWFIELD) ++southernEnabled;
        }
    }
    Require(northSnow > 0 && southernEnabled > 0, "generator did not apply mountain snow rate");
}

}  // namespace

int main() {
    try {
        TestShoreAndRates();
        TestSnowCleanup();
        TestSnow();
        TestMountainEnclosureAndIndependentRates();
        TestDesertBasinsAndWholeRegionDraws();
        TestSnowRemnantCleanup();
        TestSnowStraightBoundaries();
        TestSnowLatitudeFade();
        TestWorldRateCalibration();
        TestGeneratedWorlds();
        TestParameterValidation();
        TestMountainRateInWorldGenerator();
        std::cout << "World biome tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
