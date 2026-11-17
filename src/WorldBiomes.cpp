#include "WorldBiomes.h"

#include "MapImageGenerator.h"
#include "utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace {

bool IsSoftLand(std::uint8_t biome) {
    return biome == PLAIN || biome == DESERT || biome == FOREST;
}

bool IsWater(std::uint8_t biome) {
    return biome == SEA || biome == RIVER || biome == LAKE || biome == BRIDGE;
}

bool IsMountain(std::uint8_t biome) {
    return biome == MOUNTAIN || biome == EXMOUNTAIN;
}

// Each irregular region shares one random draw, avoiding isolated noisy pixels.
float RegionDraw(float x, float y, int spacing, std::uint32_t seed) {
    const int gx = static_cast<int>(std::floor(x / spacing));
    const int gy = static_cast<int>(std::floor(y / spacing));
    float best = std::numeric_limits<float>::max();
    int bestX = gx;
    int bestY = gy;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int cx = gx + dx;
            const int cy = gy + dy;
            const float px = static_cast<float>(cx) + mapgen::HashNoise2D(cx, cy, seed);
            const float py = static_cast<float>(cy) + mapgen::HashNoise2D(cx, cy, seed ^ 0x9e3779b9U);
            const float fx = x / spacing - px;
            const float fy = y / spacing - py;
            const float d = fx * fx + fy * fy;
            if (d < best) {
                best = d;
                bestX = cx;
                bestY = cy;
            }
        }
    }
    return mapgen::HashNoise2D(bestX, bestY, seed ^ 0x65c5d3a1U);
}

}  // namespace

void ApplyWorldBiomes(MapPlan& plan, const std::vector<float>& height,
    std::uint32_t seed, float coastRate, float snowfieldRate, float mountainSnowfieldRate) {
    if (coastRate <= 0.0f && snowfieldRate <= 0.0f && mountainSnowfieldRate <= 0.0f) return;
    const int w = plan.width;
    const int h = plan.depth;
    const int n = w * h;
    const auto index = [w](int x, int y) { return y * w + x; };
    const auto inBounds = [w, h](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h; };
    const std::array<int, 4> dx = {1, -1, 0, 0};
    const std::array<int, 4> dy = {0, 0, 1, -1};
    std::vector<std::uint8_t> protectedCells(static_cast<std::size_t>(n), 0);
    for (const auto& marker : plan.markers) {
        if (inBounds(marker.x, marker.y)) {
            protectedCells[static_cast<std::size_t>(index(marker.x, marker.y))] = 1;
        }
    }

    // Components exclude bridges, so small islands retain their own shoreline preference.
    std::vector<int> component(static_cast<std::size_t>(n), -1);
    std::vector<int> areas;
    for (int i = 0; i < n; ++i) {
        if (component[static_cast<std::size_t>(i)] >= 0 || IsWater(plan.biomes[static_cast<std::size_t>(i)])) continue;
        const int id = static_cast<int>(areas.size());
        int area = 0;
        std::queue<int> pending;
        pending.push(i);
        component[static_cast<std::size_t>(i)] = id;
        while (!pending.empty()) {
            const int cell = pending.front();
            pending.pop();
            ++area;
            for (int k = 0; k < 4; ++k) {
                const int x = cell % w + dx[static_cast<std::size_t>(k)];
                const int y = cell / w + dy[static_cast<std::size_t>(k)];
                if (!inBounds(x, y)) continue;
                const int next = index(x, y);
                if (component[static_cast<std::size_t>(next)] >= 0 || IsWater(plan.biomes[static_cast<std::size_t>(next)])) continue;
                component[static_cast<std::size_t>(next)] = id;
                pending.push(next);
            }
        }
        areas.push_back(area);
    }

    std::vector<std::pair<float, int>> shores;
    std::vector<std::uint8_t> shoreline(static_cast<std::size_t>(n), 0);
    int shorelineCells = 0;
    const int spacing = std::max(4, std::min(w, h) / 24);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int cell = index(x, y);
            const std::uint8_t biome = plan.biomes[static_cast<std::size_t>(cell)];
            if (IsWater(biome)) continue;
            int seaNeighbors = 0;
            float steepness = 0.0f;
            for (int k = 0; k < 4; ++k) {
                const int nx = x + dx[static_cast<std::size_t>(k)];
                const int ny = y + dy[static_cast<std::size_t>(k)];
                if (!inBounds(nx, ny)) continue;
                const int next = index(nx, ny);
                if (plan.biomes[static_cast<std::size_t>(next)] == SEA) ++seaNeighbors;
                steepness = std::max(steepness, std::abs(height[static_cast<std::size_t>(cell)] - height[static_cast<std::size_t>(next)]));
            }
            if (seaNeighbors == 0) continue;
            ++shorelineCells;
            shoreline[static_cast<std::size_t>(cell)] = 1;
            if (!IsSoftLand(biome) || protectedCells[static_cast<std::size_t>(cell)]) continue;

            const int area = areas[static_cast<std::size_t>(component[static_cast<std::size_t>(cell)])];
            const float smallIsland = 1.0f - std::min(1.0f, static_cast<float>(area) / std::max(1.0f, n * 0.01f));
            const float patch = mapgen::Fbm2D(static_cast<float>(x) / spacing, static_cast<float>(y) / spacing,
                3, 2.0f, 0.5f, seed ^ 0xd63a28e5U);
            // Beaches favor gently sloping, exposed shores; rocky mountains remain untouched.
            const float score = patch + smallIsland * 0.30f + seaNeighbors * 0.025f - steepness * 0.35f;
            shores.push_back({score, cell});
        }
    }
    std::sort(shores.begin(), shores.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    const int coastCandidates = std::min(static_cast<int>(shores.size()),
        static_cast<int>(std::lround(shorelineCells * 0.42)));
    for (int i = 0; i < coastCandidates; ++i) {
        const int cell = shores[static_cast<std::size_t>(i)].second;
        if (coastRate >= 1.0f || RegionDraw(static_cast<float>(cell % w), static_cast<float>(cell / w), spacing * 2, seed ^ 0x12c341d7U) < coastRate) {
            plan.biomes[static_cast<std::size_t>(cell)] = COAST;
        }
    }

    // Measure each continuous shoreline segment, then taper its inland width
    // toward its ends. Propagating the widths produces rounded beach corners.
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(n), 0);
    std::vector<int> endDistance(static_cast<std::size_t>(n), -1);
    std::priority_queue<std::pair<int, int>> expansion;
    for (int start = 0; start < n; ++start) {
        if (plan.biomes[static_cast<std::size_t>(start)] != COAST || visited[static_cast<std::size_t>(start)]) continue;
        std::vector<int> segment;
        std::queue<int> pending;
        pending.push(start);
        visited[static_cast<std::size_t>(start)] = 1;
        while (!pending.empty()) {
            const int cell = pending.front();
            pending.pop();
            segment.push_back(cell);
            for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
                if (ox == 0 && oy == 0) continue;
                const int x = cell % w + ox;
                const int y = cell / w + oy;
                if (!inBounds(x, y)) continue;
                const int next = index(x, y);
                if (plan.biomes[static_cast<std::size_t>(next)] != COAST || visited[static_cast<std::size_t>(next)]) continue;
                visited[static_cast<std::size_t>(next)] = 1;
                pending.push(next);
            }
        }

        const int maxWidth = std::clamp(2 + static_cast<int>(segment.size()) / 4, 2, 5);
        for (int cell : segment) {
            bool end = false;
            int neighbors = 0;
            for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
                if (ox == 0 && oy == 0) continue;
                const int x = cell % w + ox;
                const int y = cell / w + oy;
                if (!inBounds(x, y)) continue;
                const int next = index(x, y);
                if (plan.biomes[static_cast<std::size_t>(next)] == COAST) {
                    ++neighbors;
                } else if (shoreline[static_cast<std::size_t>(next)]) {
                    end = true;
                }
            }
            if (end || neighbors <= 1) {
                endDistance[static_cast<std::size_t>(cell)] = 0;
                pending.push(cell);
            }
        }
        while (!pending.empty()) {
            const int cell = pending.front();
            pending.pop();
            for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
                if (ox == 0 && oy == 0) continue;
                const int x = cell % w + ox;
                const int y = cell / w + oy;
                if (!inBounds(x, y)) continue;
                const int next = index(x, y);
                if (plan.biomes[static_cast<std::size_t>(next)] != COAST || endDistance[static_cast<std::size_t>(next)] >= 0) continue;
                endDistance[static_cast<std::size_t>(next)] = endDistance[static_cast<std::size_t>(cell)] + 1;
                pending.push(next);
            }
        }
        for (int cell : segment) {
            const int d = endDistance[static_cast<std::size_t>(cell)];
            const int width = d < 0 ? maxWidth : std::min(maxWidth, d + 1);
            expansion.push({width - 1, cell});
        }
    }

    std::vector<int> remaining(static_cast<std::size_t>(n), -1);
    while (!expansion.empty()) {
        const auto [radius, cell] = expansion.top();
        expansion.pop();
        if (radius <= remaining[static_cast<std::size_t>(cell)]) continue;
        remaining[static_cast<std::size_t>(cell)] = radius;
        if (radius == 0) continue;
        for (int k = 0; k < 4; ++k) {
            const int x = cell % w + dx[static_cast<std::size_t>(k)];
            const int y = cell / w + dy[static_cast<std::size_t>(k)];
            if (!inBounds(x, y)) continue;
            const int next = index(x, y);
            const auto biome = plan.biomes[static_cast<std::size_t>(next)];
            if (shoreline[static_cast<std::size_t>(next)] || protectedCells[static_cast<std::size_t>(next)] ||
                (!IsSoftLand(biome) && biome != COAST)) continue;
            plan.biomes[static_cast<std::size_t>(next)] = COAST;
            expansion.push({radius - 1, next});
        }
    }

    if (snowfieldRate <= 0.0f && mountainSnowfieldRate <= 0.0f) return;
    const int mountainRange = std::max(8, std::min(w, h) / 8);
    const int snowSpacing = spacing * 2;
    std::vector<std::uint8_t> northernCandidates(static_cast<std::size_t>(n), 0);
    std::vector<std::uint8_t> mountainCandidates(static_cast<std::size_t>(n), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int cell = index(x, y);
            if (!IsSoftLand(plan.biomes[static_cast<std::size_t>(cell)]) ||
                protectedCells[static_cast<std::size_t>(cell)]) continue;

            const float latitude = static_cast<float>(y) / std::max(1, h - 1);
            const float north = std::max(0.0f, 1.0f - latitude / 0.30f);
            const float snowNoise = mapgen::Fbm2D(static_cast<float>(x) / snowSpacing,
                static_cast<float>(y) / snowSpacing, 3, 2.0f, 0.5f, seed ^ 0xc481f93bU);
            const float edgeFade = std::clamp((latitude - 0.28f) / 0.02f, 0.0f, 1.0f);
            const bool northern = north > 0.0f && snowNoise > 0.28f - north * 0.10f + edgeFade * 0.65f;
            if (northern && snowfieldRate > 0.0f) {
                northernCandidates[static_cast<std::size_t>(cell)] = 1;
            }
        }
    }

    if (mountainSnowfieldRate > 0.0f) {
        // Close only narrow mountain passes, then inspect whole lowland basins,
        // rather than clipping individual cells along cardinal sight lines.
        std::vector<std::uint8_t> dilated(n, 0), barrier(n, 0), seen(n, 0);
        constexpr int radius = 2;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            if (!IsMountain(plan.biomes[index(x, y)])) continue;
            for (int oy = -radius; oy <= radius; ++oy) for (int ox = -radius; ox <= radius; ++ox) {
                if (inBounds(x + ox, y + oy))
                    dilated[index(x + ox, y + oy)] = 1;
            }
        }
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            bool full = true;
            for (int oy = -radius; oy <= radius && full; ++oy) for (int ox = -radius; ox <= radius; ++ox) {
                if (!inBounds(x + ox, y + oy) || !dilated[index(x + ox, y + oy)]) {
                    full = false;
                    break;
                }
            }
            const auto biome = plan.biomes[index(x, y)];
            barrier[index(x, y)] = IsMountain(biome) || (full && !IsWater(biome));
        }
        for (int start = 0; start < n; ++start) {
            if (barrier[start] || seen[start]) continue;
            std::vector<int> basin;
            std::queue<int> pending;
            pending.push(start);
            seen[start] = 1;
            bool open = false;
            int perimeter = 0, mountainEdges = 0;
            int minX = w, maxX = 0, minY = h, maxY = 0;
            while (!pending.empty()) {
                const int cell = pending.front();
                pending.pop();
                basin.push_back(cell);
                const int x = cell % w, y = cell / w;
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
                if (IsWater(plan.biomes[cell]) || plan.biomes[cell] == COAST) open = true;
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dx[k], ny = y + dy[k];
                    if (!inBounds(nx, ny)) { open = true; continue; }
                    const int next = index(nx, ny);
                    if (barrier[next]) {
                        ++perimeter;
                        bool nearMountain = false;
                        for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
                            if (inBounds(nx + ox, ny + oy) &&
                                IsMountain(plan.biomes[index(nx + ox, ny + oy)])) nearMountain = true;
                        }
                        if (nearMountain) ++mountainEdges;
                    } else if (!seen[next]) {
                        seen[next] = 1;
                        pending.push(next);
                    }
                }
            }
            if (open || perimeter == 0 || mountainEdges < perimeter * 0.80f ||
                maxX - minX > mountainRange * 2 || maxY - minY > mountainRange * 2) continue;
            for (int cell : basin) {
                if (IsSoftLand(plan.biomes[cell]) && !protectedCells[cell]) mountainCandidates[cell] = 1;
            }
        }
    }

    // Clean each source independently so neither rate can create or remove the other's patches.
    const auto applySnow = [&](const std::vector<std::uint8_t>& snowCandidates, float rate, std::uint32_t salt) {
        // Opening keeps only candidates belonging to a full 3x3 snow patch.
        std::vector<std::uint8_t> opened(static_cast<std::size_t>(n), 0);
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                bool full = true;
                for (int oy = -1; oy <= 1 && full; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (!snowCandidates[static_cast<std::size_t>(index(x + ox, y + oy))]) {
                            full = false;
                            break;
                        }
                    }
                }
                if (!full) continue;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        opened[static_cast<std::size_t>(index(x + ox, y + oy))] = 1;
                    }
                }
            }
        }

        constexpr int minSnowArea = 25;
        std::vector<std::uint8_t> snowVisited(static_cast<std::size_t>(n), 0);
        for (int start = 0; start < n; ++start) {
            if (!opened[static_cast<std::size_t>(start)] || snowVisited[static_cast<std::size_t>(start)]) continue;
            std::vector<int> patch;
            std::queue<int> pending;
            pending.push(start);
            snowVisited[static_cast<std::size_t>(start)] = 1;
            while (!pending.empty()) {
                const int cell = pending.front();
                pending.pop();
                patch.push_back(cell);
                for (int k = 0; k < 4; ++k) {
                    const int x = cell % w + dx[static_cast<std::size_t>(k)];
                    const int y = cell / w + dy[static_cast<std::size_t>(k)];
                    if (!inBounds(x, y)) continue;
                    const int next = index(x, y);
                    if (!opened[static_cast<std::size_t>(next)] || snowVisited[static_cast<std::size_t>(next)]) continue;
                    snowVisited[static_cast<std::size_t>(next)] = 1;
                    pending.push(next);
                }
            }
            if (static_cast<int>(patch.size()) < minSnowArea) continue;
            const int anchor = *std::min_element(patch.begin(), patch.end());
            if (rate <= 0.0f || (rate < 1.0f &&
                mapgen::HashNoise2D(anchor % w, anchor / w, seed ^ salt) >= rate)) continue;
            for (int cell : patch) {
                plan.biomes[static_cast<std::size_t>(cell)] = SNOWFIELD;
            }
        }
    };
    applySnow(northernCandidates, snowfieldRate, 0xb6327e91U);
    applySnow(mountainCandidates, mountainSnowfieldRate, 0x791ad4c3U);

    // Merge tiny biome remnants touching snow, without iteratively expanding its edge.
    const auto beforeCleanup = plan.biomes;
    std::vector<std::uint8_t> remnantVisited(n, 0);
    for (int start = 0; start < n; ++start) {
        if (!IsSoftLand(beforeCleanup[start]) || remnantVisited[start]) continue;
        std::vector<int> remnant;
        std::queue<int> pending;
        pending.push(start);
        remnantVisited[start] = 1;
        bool protectedRemnant = false;
        int snowEdges = 0, otherLandEdges = 0;
        while (!pending.empty()) {
            const int cell = pending.front();
            pending.pop();
            remnant.push_back(cell);
            if (protectedCells[cell]) protectedRemnant = true;
            for (int k = 0; k < 4; ++k) {
                const int x = cell % w + dx[k], y = cell / w + dy[k];
                if (!inBounds(x, y)) continue;
                const int next = index(x, y);
                if (beforeCleanup[next] == beforeCleanup[start]) {
                    if (!remnantVisited[next]) {
                        remnantVisited[next] = 1;
                        pending.push(next);
                    }
                } else if (beforeCleanup[next] == SNOWFIELD) {
                    ++snowEdges;
                } else if (IsSoftLand(beforeCleanup[next])) {
                    ++otherLandEdges;
                }
            }
        }
        if (!protectedRemnant && remnant.size() <= 8 && snowEdges > 0 && snowEdges >= otherLandEdges) {
            for (int cell : remnant) plan.biomes[cell] = SNOWFIELD;
        }
    }
}
