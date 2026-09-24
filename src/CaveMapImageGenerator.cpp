#include "CaveMapImageGenerator.h"
#include "utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <random>
#include <tuple>
#include <utility>
#include <vector>

namespace {

inline int Index(int x, int y, int w) {
    return y * w + x;
}

inline bool InBounds(int x, int y, int w, int h) {
    return x >= 0 && y >= 0 && x < w && y < h;
}

}  // namespace

CaveMapImageGenerator::CaveMapImageGenerator(MapGenParams params) : MapImageGenerator(std::move(params)) {}

MapGenColor CaveMapImageGenerator::BiomeToColor(std::uint8_t biome) const {
    switch (biome) {
        case FLOOR_LV1:
            return {74, 125, 74};
        case FLOOR_LV2:
            return {111, 162, 111};
        case WALL:
            return {96, 82, 68};
        case INNERWALL:
            return {8, 10, 14};
        case LADDER:
            return {182, 122, 77};
        case RIVER:
            return {38, 184, 224};
        case LAKE:
            return {34, 66, 168};
        default:
            return {255, 0, 255};
    }
}

bool CaveMapImageGenerator::Generate(MapPlan& outPlan, std::string& outError) {
    const int w = params_.width;
    const int h = params_.depth;
    if (w < 32 || h < 32) {
        outError = "Cave map size must be at least 32x32";
        return false;
    }

    const int n = w * h;
    outPlan.Reset(w, h, params_.seed, "cave");
    std::vector<std::uint8_t>& outBiomes = outPlan.biomes;
    auto parameterSeed = [&](const char* key) {
        std::uint32_t hash = 2166136261U;
        for (const unsigned char* c = reinterpret_cast<const unsigned char*>(key); *c != 0; ++c) {
            hash = (hash ^ *c) * 16777619U;
        }
        return params_.seed ^ hash ^ 0x9e3779b9U;
    };
    auto rangedDefault = [&](const char* key, double minimum, double maximum) {
        const auto explicitValue = params_.extra.find(key);
        if (explicitValue != params_.extra.end()) {
            return explicitValue->second;
        }
        std::mt19937 parameterRng(parameterSeed(key));
        return std::uniform_real_distribution<double>(minimum, maximum)(parameterRng);
    };
    auto rangedIntDefault = [&](const char* key, int minimum, int maximum) {
        const auto explicitValue = params_.extra.find(key);
        if (explicitValue != params_.extra.end()) {
            return static_cast<int>(explicitValue->second);
        }
        std::mt19937 parameterRng(parameterSeed(key));
        return std::uniform_int_distribution<int>(minimum, maximum)(parameterRng);
    };

    const float largeCellSize = static_cast<float>(std::clamp(rangedDefault("largeCellSize", 22.0, 38.0), 12.0, 96.0));
    const float mediumCellSize = static_cast<float>(std::clamp(rangedDefault("mediumCellSize", 9.0, 19.0), 6.0, 48.0));
    const float largeWeight = static_cast<float>(std::clamp(rangedDefault("largeWeight", 0.35, 0.60), 0.0, 1.0));
    const float mediumWeight = static_cast<float>(std::clamp(rangedDefault("mediumWeight", 0.15, 0.35), 0.0, 1.0));
    const float noiseWeight = static_cast<float>(std::clamp(rangedDefault("noiseWeight", 0.20, 0.40), 0.0, 1.0));
    const float noiseScale = static_cast<float>(std::clamp(rangedDefault("noiseScale", 0.015, 0.040), 0.005, 0.10));
    const int noiseOctaves = std::clamp(rangedIntDefault("noiseOctaves", 2, 4), 1, 5);
    float threshold = static_cast<float>(std::clamp(rangedDefault("wallThreshold", 0.50, 0.62), 0.25, 0.85));
    const int borderThickness = std::clamp(rangedIntDefault("borderThickness", 4, 6), 2, 16);
    const float edgeFalloffWidth = static_cast<float>(std::clamp(rangedDefault("edgeFalloffWidth", 22.0, 38.0), 8.0, 64.0));
    const float edgePressure = static_cast<float>(std::clamp(rangedDefault("edgePressure", 0.62, 0.84), 0.10, 1.00));
    const float edgeVariation = static_cast<float>(std::clamp(rangedDefault("edgeVariation", 0.75, 1.00), 0.0, 1.0));
    const float edgeNoiseScale = static_cast<float>(std::clamp(rangedDefault("edgeNoiseScale", 0.022, 0.040), 0.005, 0.05));
    const int wallOpenRadius = std::clamp(rangedIntDefault("wallOpenRadius", 1, 2), 0, 4);
    const int floorOpenRadius = std::clamp(rangedIntDefault("floorOpenRadius", 1, 2), 0, 3);
    const int minWallThickness = std::clamp(rangedIntDefault("minWallThickness", 3, 4), 1, 9);
    const int minPassageWidth = std::clamp(rangedIntDefault("minPassageWidth", 2, 3), 2, 8);
    const int effectiveWallOpenRadius = std::max(wallOpenRadius, minWallThickness / 2);
    const int effectiveFloorOpenRadius = std::max(floorOpenRadius, (minPassageWidth - 1) / 2);
    const int closingRadius = std::clamp(rangedIntDefault("closingRadius", 1, 2), 0, 3);
    const int minRegionArea = std::clamp(rangedIntDefault("minRegionArea", 32, 96), 8, 1024);
    const float targetFloorRatioMin = static_cast<float>(std::clamp(rangedDefault("targetFloorRatioMin", 0.30, 0.34), 0.10, 0.80));
    const float targetFloorRatioMax = static_cast<float>(std::clamp(rangedDefault("targetFloorRatioMax", 0.50, 0.55), static_cast<double>(targetFloorRatioMin), 0.90));
    const int generationRetries = std::clamp(rangedIntDefault("generationRetries", 8, 12), 1, 20);
    const float floorLv2CellSize = static_cast<float>(std::clamp(rangedDefault("floorLv2CellSize", 24.0, 42.0), 12.0, 96.0));
    const float floorLv2TopRatio = static_cast<float>(std::clamp(rangedDefault("floorLv2TopRatio", 0.34, 0.50), 0.10, 0.75));
    const float floorLv2Variation = static_cast<float>(std::clamp(rangedDefault("floorLv2Variation", 0.18, 0.36), 0.0, 0.40));
    const int minFloorLevelIslandArea = std::clamp(rangedIntDefault("minFloorLevelIslandArea", 24, 64), 0, 1024);
    const auto cliffStrengthIt = params_.extra.find("floorLv2CliffStrength");
    const auto legacyCliffRoughnessIt = params_.extra.find("floorLv2CliffRoughness");
    const int floorLv2CliffStrength = cliffStrengthIt != params_.extra.end()
        ? std::clamp(static_cast<int>(std::lround(cliffStrengthIt->second)), 0, 8)
        : (legacyCliffRoughnessIt != params_.extra.end()
            ? std::clamp(static_cast<int>(std::lround(legacyCliffRoughnessIt->second)), 0, 8)
            : std::clamp(rangedIntDefault("floorLv2CliffStrength", 2, 4), 0, 8));
    const float floorLv2CliffFrequency = static_cast<float>(std::clamp(rangedDefault("floorLv2CliffFrequency", 0.035, 0.065), 0.01, 0.20));
    const int floorLv2GenerationRetries = std::clamp(rangedIntDefault("floorLv2GenerationRetries", 6, 6), 1, 16);
    const float floorLv2MaxVerticalRunRatio = static_cast<float>(std::clamp(rangedDefault("floorLv2MaxVerticalRunRatio", 0.10, 0.10), 0.03, 0.50));
    const float lakeChance = static_cast<float>(std::clamp(rangedDefault("lakeChance", 0.05, 0.35), 0.0, 1.0));
    const int lakeMaxCount = std::clamp(rangedIntDefault("lakeMaxCount", 1, 4), 0, 6);
    const int lakeMinWidth = std::clamp(rangedIntDefault("lakeMinWidth", 7, 10), 5, 20);
    const int lakeMaxWidth = std::clamp(rangedIntDefault("lakeMaxWidth", 14, 22), lakeMinWidth, 36);
    const int lakeMinDepth = std::clamp(rangedIntDefault("lakeMinDepth", 3, 5), 3, 12);
    const int lakeMaxDepth = std::clamp(rangedIntDefault("lakeMaxDepth", 7, 11), lakeMinDepth, 18);

    auto smoothRange = [](float edge0, float edge1, float value) {
        const float t = std::clamp((value - edge0) / std::max(0.0001f, edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    auto erode = [&](const std::vector<std::uint8_t>& mask, int radius) {
        std::vector<std::uint8_t> result(mask.size(), 0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                bool keep = mask[static_cast<std::size_t>(Index(x, y, w))] != 0;
                for (int oy = -radius; oy <= radius && keep; ++oy) {
                    for (int ox = -radius; ox <= radius; ++ox) {
                        if (ox * ox + oy * oy > radius * radius) continue;
                        const int nx = x + ox;
                        const int ny = y + oy;
                        if (!InBounds(nx, ny, w, h) || mask[static_cast<std::size_t>(Index(nx, ny, w))] == 0) { keep = false; break; }
                    }
                }
                result[static_cast<std::size_t>(Index(x, y, w))] = keep ? 1 : 0;
            }
        }
        return result;
    };
    auto dilate = [&](const std::vector<std::uint8_t>& mask, int radius) {
        std::vector<std::uint8_t> result(mask.size(), 0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                bool fill = false;
                for (int oy = -radius; oy <= radius && !fill; ++oy) {
                    for (int ox = -radius; ox <= radius; ++ox) {
                        if (ox * ox + oy * oy > radius * radius) continue;
                        const int nx = x + ox;
                        const int ny = y + oy;
                        if (InBounds(nx, ny, w, h) && mask[static_cast<std::size_t>(Index(nx, ny, w))] != 0) { fill = true; break; }
                    }
                }
                result[static_cast<std::size_t>(Index(x, y, w))] = fill ? 1 : 0;
            }
        }
        return result;
    };
    auto enforceBorder = [&](std::vector<std::uint8_t>& floor) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (x < borderThickness || y < borderThickness || x >= w - borderThickness || y >= h - borderThickness) {
                    floor[static_cast<std::size_t>(Index(x, y, w))] = 0;
                }
            }
        }
    };
    auto keepLargestRegion = [&](std::vector<std::uint8_t>& floor) {
        std::vector<int> labels(static_cast<std::size_t>(n), -1);
        std::vector<std::vector<int>> regions;
        constexpr int dx[4] = {1, -1, 0, 0};
        constexpr int dy[4] = {0, 0, 1, -1};
        for (int start = 0; start < n; ++start) {
            if (floor[static_cast<std::size_t>(start)] == 0 || labels[static_cast<std::size_t>(start)] >= 0) continue;
            const int label = static_cast<int>(regions.size());
            regions.emplace_back();
            std::queue<int> pending;
            pending.push(start);
            labels[static_cast<std::size_t>(start)] = label;
            while (!pending.empty()) {
                const int current = pending.front(); pending.pop();
                regions.back().push_back(current);
                const int x = current % w; const int y = current / w;
                for (int direction = 0; direction < 4; ++direction) {
                    const int nx = x + dx[direction]; const int ny = y + dy[direction];
                    if (!InBounds(nx, ny, w, h)) continue;
                    const int next = Index(nx, ny, w);
                    if (floor[static_cast<std::size_t>(next)] != 0 && labels[static_cast<std::size_t>(next)] < 0) { labels[static_cast<std::size_t>(next)] = label; pending.push(next); }
                }
            }
        }
        if (regions.empty()) return;
        int largest = 0;
        for (int i = 1; i < static_cast<int>(regions.size()); ++i) if (regions[static_cast<std::size_t>(i)].size() > regions[static_cast<std::size_t>(largest)].size()) largest = i;
        for (int i = 0; i < n; ++i) if (labels[static_cast<std::size_t>(i)] != largest || (labels[static_cast<std::size_t>(i)] >= 0 && static_cast<int>(regions[static_cast<std::size_t>(labels[static_cast<std::size_t>(i)])].size()) < minRegionArea)) floor[static_cast<std::size_t>(i)] = 0;
    };

    const float weightSum = std::max(0.0001f, largeWeight + mediumWeight + noiseWeight);
    std::vector<float> wallField(static_cast<std::size_t>(n), 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float warpX = (mapgen::Fbm2D(x * noiseScale * 0.55f, y * noiseScale * 0.55f, 2, 2.0f, 0.5f, params_.seed ^ 0xa511e9b3U) - 0.5f) * largeCellSize * 0.45f;
            const float warpY = (mapgen::Fbm2D(x * noiseScale * 0.55f, y * noiseScale * 0.55f, 2, 2.0f, 0.5f, params_.seed ^ 0x63d83595U) - 0.5f) * largeCellSize * 0.45f;
            const float wx = (static_cast<float>(x) + warpX) * 0.90f;
            const float wy = (static_cast<float>(y) + warpY) * 1.10f;
            const float large = smoothRange(0.42f, 0.88f, 1.0f - mapgen::Worley2D(wx, wy, largeCellSize, params_.seed ^ 0x7f4a7c15U));
            const float medium = smoothRange(0.48f, 0.90f, 1.0f - mapgen::Worley2D(wx, wy, mediumCellSize, params_.seed ^ 0x94d049bbU));
            const float noise = mapgen::Fbm2D(x * noiseScale, y * noiseScale, noiseOctaves, 1.9f, 0.48f, params_.seed ^ 0x2c1b3c6dU);
            const float leftNoise = mapgen::Fbm2D(0.0f, y * edgeNoiseScale, 3, 1.9f, 0.52f, params_.seed ^ 0xb7e15163U);
            const float rightNoise = mapgen::Fbm2D(5.0f, y * edgeNoiseScale, 3, 1.9f, 0.52f, params_.seed ^ 0x8aed2a6bU);
            const float topNoise = mapgen::Fbm2D(x * edgeNoiseScale, 0.0f, 3, 1.9f, 0.52f, params_.seed ^ 0x4f1bbcdcU);
            const float bottomNoise = mapgen::Fbm2D(x * edgeNoiseScale, 5.0f, 3, 1.9f, 0.52f, params_.seed ^ 0xc3a5c85cU);
            const float leftWidth = edgeFalloffWidth * (1.0f + (leftNoise - 0.5f) * edgeVariation);
            const float rightWidth = edgeFalloffWidth * (1.0f + (rightNoise - 0.5f) * edgeVariation);
            const float topWidth = edgeFalloffWidth * (1.0f + (topNoise - 0.5f) * edgeVariation);
            const float bottomWidth = edgeFalloffWidth * (1.0f + (bottomNoise - 0.5f) * edgeVariation);
            const float leftFactor = 1.0f - smoothRange(static_cast<float>(borderThickness), leftWidth, static_cast<float>(x));
            const float rightFactor = 1.0f - smoothRange(static_cast<float>(borderThickness), rightWidth, static_cast<float>(w - 1 - x));
            const float topFactor = 1.0f - smoothRange(static_cast<float>(borderThickness), topWidth, static_cast<float>(y));
            const float bottomFactor = 1.0f - smoothRange(static_cast<float>(borderThickness), bottomWidth, static_cast<float>(h - 1 - y));
            const float edgeFactor = std::max({leftFactor, rightFactor, topFactor, bottomFactor});
            const float edgeIntrusions = smoothRange(0.30f, 0.82f, 1.0f - mapgen::Worley2D(wx, wy, 18.0f, params_.seed ^ 0xd2511f53U));
            const float baseField = (large * largeWeight + medium * mediumWeight + noise * noiseWeight) / weightSum;
            const float structuredPressure = edgeFactor * edgeFactor * edgePressure * (0.35f + edgeIntrusions * 0.90f);
            const float safetyPressure = std::pow(edgeFactor, 6.0f) * 0.45f;
            wallField[static_cast<std::size_t>(Index(x, y, w))] = baseField + structuredPressure + safetyPressure;
        }
    }

    std::vector<std::uint8_t> bestFloor(static_cast<std::size_t>(n), 0);
    float bestScore = std::numeric_limits<float>::max();
    for (int attempt = 0; attempt < generationRetries; ++attempt) {
        std::vector<std::uint8_t> walls(static_cast<std::size_t>(n), 0);
        for (int i = 0; i < n; ++i) walls[static_cast<std::size_t>(i)] = wallField[static_cast<std::size_t>(i)] >= threshold ? 1 : 0;
        if (effectiveWallOpenRadius > 0) walls = dilate(erode(walls, effectiveWallOpenRadius), effectiveWallOpenRadius);
        std::vector<std::uint8_t> floor(static_cast<std::size_t>(n), 0);
        for (int i = 0; i < n; ++i) floor[static_cast<std::size_t>(i)] = walls[static_cast<std::size_t>(i)] == 0 ? 1 : 0;
        if (effectiveFloorOpenRadius > 0) floor = dilate(erode(floor, effectiveFloorOpenRadius), effectiveFloorOpenRadius);
        if (closingRadius > 0) floor = erode(dilate(floor, closingRadius), closingRadius);
        enforceBorder(floor);
        keepLargestRegion(floor);
        const float ratio = static_cast<float>(std::count(floor.begin(), floor.end(), static_cast<std::uint8_t>(1))) / static_cast<float>(n);
        const float score = ratio < targetFloorRatioMin ? targetFloorRatioMin - ratio : (ratio > targetFloorRatioMax ? ratio - targetFloorRatioMax : 0.0f);
        if (score < bestScore) { bestScore = score; bestFloor = floor; }
        if (score <= 0.0001f) break;
        threshold = std::clamp(threshold + (ratio < targetFloorRatioMin ? 0.025f : -0.025f), 0.25f, 0.85f);
    }
    if (std::count(bestFloor.begin(), bestFloor.end(), static_cast<std::uint8_t>(1)) == 0) {
        outError = "Failed to generate a connected cave floor";
        return false;
    }

    outBiomes.assign(static_cast<std::size_t>(n), static_cast<std::uint8_t>(WALL));
    for (int i = 0; i < n; ++i) if (bestFloor[static_cast<std::size_t>(i)] != 0) outBiomes[static_cast<std::size_t>(i)] = FLOOR_LV1;

    constexpr int floorDx[4] = {1, -1, 0, 0};
    constexpr int floorDy[4] = {0, 0, 1, -1};
    auto generateFloorLevels = [&](std::uint32_t elevationSeed) {
        std::vector<std::uint8_t> candidateBiomes = outBiomes;
        auto isUpperFloorCell = [&](int x, int y) {
        const float gridX = static_cast<float>(x) / floorLv2CellSize;
        const float gridY = static_cast<float>(y) / floorLv2CellSize;
        const int cellX = static_cast<int>(std::floor(gridX));
        const int cellY = static_cast<int>(std::floor(gridY));
        float weightedDepth = 0.0f;
        float totalWeight = 0.0f;
        const float verticalBias = std::clamp(1.0f - floorLv2Variation * 2.0f, 0.40f, 0.80f);
        for (int offsetY = -2; offsetY <= 2; ++offsetY) {
            for (int offsetX = -2; offsetX <= 2; ++offsetX) {
                const int candidateX = cellX + offsetX;
                const int candidateY = cellY + offsetY;
                const float featureX = static_cast<float>(candidateX) + mapgen::HashNoise2D(candidateX, candidateY, elevationSeed);
                const float featureY = static_cast<float>(candidateY) + mapgen::HashNoise2D(candidateX, candidateY, elevationSeed ^ 0x9e3779b9U);
                const float deltaX = featureX - gridX;
                const float deltaY = featureY - gridY;
                const float distance = deltaX * deltaX + deltaY * deltaY;
                const float weight = std::exp(-distance * 2.75f);
                const float cellNoise = mapgen::HashNoise2D(candidateX, candidateY, elevationSeed ^ 0x3c6ef372U);
                const float macroNoise = mapgen::ValueNoise2D(static_cast<float>(candidateX) * 0.42f, static_cast<float>(candidateY) * 0.42f, elevationSeed ^ 0xa54ff53aU);
                const float noiseDepth = 1.0f - (cellNoise * 0.35f + macroNoise * 0.65f);
                const float normalizedY = std::clamp(featureY * floorLv2CellSize / static_cast<float>(h), 0.0f, 1.0f);
                weightedDepth += (normalizedY * verticalBias + noiseDepth * (1.0f - verticalBias)) * weight;
                totalWeight += weight;
            }
        }
        const float effectiveDepth = totalWeight > 0.0f ? weightedDepth / totalWeight : 1.0f;
            return effectiveDepth <= floorLv2TopRatio;
        };
        std::vector<std::uint8_t> upperFloorMask(static_cast<std::size_t>(n), 0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int index = Index(x, y, w);
                if (bestFloor[static_cast<std::size_t>(index)] != 0 && isUpperFloorCell(x, y)) {
                    upperFloorMask[static_cast<std::size_t>(index)] = 1;
                }
            }
        }
        for (int y = h - 1; y > 0; --y) {
            for (int x = 0; x < w; ++x) {
                if (upperFloorMask[static_cast<std::size_t>(Index(x, y, w))] == 0) {
                    continue;
                }
                const int ny = y - 1;
                if (bestFloor[static_cast<std::size_t>(Index(x, ny, w))] != 0) {
                    upperFloorMask[static_cast<std::size_t>(Index(x, ny, w))] = 1;
                }
            }
        }
        for (int index = 0; index < n; ++index) {
            if (upperFloorMask[static_cast<std::size_t>(index)] != 0) {
                candidateBiomes[static_cast<std::size_t>(index)] = FLOOR_LV2;
            }
        }

        for (int cleanupPass = 0; cleanupPass < n && minFloorLevelIslandArea > 0; ++cleanupPass) {
            std::vector<std::uint8_t> visited(static_cast<std::size_t>(n), 0);
            std::vector<int> smallestIsland;
            std::uint8_t replacementLevel = FLOOR_LV1;
            for (int start = 0; start < n; ++start) {
                const std::uint8_t level = candidateBiomes[static_cast<std::size_t>(start)];
                if ((level != FLOOR_LV1 && level != FLOOR_LV2) || visited[static_cast<std::size_t>(start)] != 0) {
                    continue;
                }
                std::vector<int> island;
                bool touchesOtherLevel = false;
                std::queue<int> floorPending;
                floorPending.push(start);
                visited[static_cast<std::size_t>(start)] = 1;
                while (!floorPending.empty()) {
                    const int current = floorPending.front();
                    floorPending.pop();
                    island.push_back(current);
                    const int x = current % w;
                    const int y = current / w;
                    for (int direction = 0; direction < 4; ++direction) {
                        const int nx = x + floorDx[direction];
                        const int ny = y + floorDy[direction];
                        if (!InBounds(nx, ny, w, h)) {
                            continue;
                        }
                        const int next = Index(nx, ny, w);
                        const std::uint8_t neighborLevel = candidateBiomes[static_cast<std::size_t>(next)];
                        if (neighborLevel == level && visited[static_cast<std::size_t>(next)] == 0) {
                            visited[static_cast<std::size_t>(next)] = 1;
                            floorPending.push(next);
                        } else if ((neighborLevel == FLOOR_LV1 || neighborLevel == FLOOR_LV2) && neighborLevel != level) {
                            touchesOtherLevel = true;
                        }
                    }
                }
                if (touchesOtherLevel && static_cast<int>(island.size()) < minFloorLevelIslandArea &&
                    (smallestIsland.empty() || island.size() < smallestIsland.size())) {
                    smallestIsland = std::move(island);
                    replacementLevel = static_cast<std::uint8_t>(level == FLOOR_LV1 ? FLOOR_LV2 : FLOOR_LV1);
                }
            }
            if (smallestIsland.empty()) {
                break;
            }
            for (int index : smallestIsland) {
                candidateBiomes[static_cast<std::size_t>(index)] = replacementLevel;
            }
        }
        return candidateBiomes;
    };
    auto longestVerticalFloorLevelBoundary = [&](const std::vector<std::uint8_t>& biomes) {
        int longestRun = 0;
        for (int x = 0; x + 1 < w; ++x) {
            int currentRun = 0;
            for (int y = 0; y < h; ++y) {
                const std::uint8_t left = biomes[static_cast<std::size_t>(Index(x, y, w))];
                const std::uint8_t right = biomes[static_cast<std::size_t>(Index(x + 1, y, w))];
                const bool isBoundary = (left == FLOOR_LV1 && right == FLOOR_LV2) ||
                                        (left == FLOOR_LV2 && right == FLOOR_LV1);
                currentRun = isBoundary ? currentRun + 1 : 0;
                longestRun = std::max(longestRun, currentRun);
            }
        }
        return longestRun;
    };

    const int maxVerticalRun = std::max(4, static_cast<int>(std::lround(static_cast<float>(h) * floorLv2MaxVerticalRunRatio)));
    int bestVerticalRun = std::numeric_limits<int>::max();
    std::vector<std::uint8_t> bestFloorLevels;
    std::uint32_t bestElevationSeed = 0;
    const std::uint32_t baseElevationSeed = params_.seed ^ 0x6a09e667U;
    for (int attempt = 0; attempt < floorLv2GenerationRetries; ++attempt) {
        const std::uint32_t elevationSeed = baseElevationSeed + static_cast<std::uint32_t>(attempt) * 0x9e3779b9U;
        std::vector<std::uint8_t> candidateBiomes = generateFloorLevels(elevationSeed);
        const int verticalRun = longestVerticalFloorLevelBoundary(candidateBiomes);
        if (verticalRun < bestVerticalRun) {
            bestVerticalRun = verticalRun;
            bestFloorLevels = std::move(candidateBiomes);
            bestElevationSeed = elevationSeed;
        }
        if (verticalRun <= maxVerticalRun) {
            break;
        }
    }
    outBiomes = std::move(bestFloorLevels);
    if (floorLv2CliffStrength > 0) {
        const std::uint32_t cliffSeed = bestElevationSeed ^ 0x510e527fU;
        for (int x = 0; x < w; ++x) {
            int y = 0;
            while (y < h) {
                while (y < h && bestFloor[static_cast<std::size_t>(Index(x, y, w))] == 0) {
                    ++y;
                }
                const int segmentStart = y;
                int deepestUpperCell = -1;
                while (y < h && bestFloor[static_cast<std::size_t>(Index(x, y, w))] != 0) {
                    if (outBiomes[static_cast<std::size_t>(Index(x, y, w))] == FLOOR_LV2) {
                        deepestUpperCell = y;
                    }
                    ++y;
                }
                const int segmentEnd = y - 1;
                if (deepestUpperCell < segmentStart) {
                    continue;
                }
                const float broadNoise = mapgen::Fbm2D(
                    static_cast<float>(x) * floorLv2CliffFrequency,
                    static_cast<float>(deepestUpperCell) * floorLv2CliffFrequency,
                    3, 1.9f, 0.52f, cliffSeed);
                const float detailNoise = mapgen::Fbm2D(
                    static_cast<float>(x) * floorLv2CliffFrequency * 2.1f,
                    static_cast<float>(deepestUpperCell) * floorLv2CliffFrequency * 2.1f,
                    2, 2.0f, 0.50f, cliffSeed ^ 0x1f83d9abU);
                const float displacementNoise = broadNoise * 0.65f + detailNoise * 0.35f;
                const int displacement = static_cast<int>(std::lround(
                    std::clamp((displacementNoise - 0.5f) * 4.0f, -1.0f, 1.0f) *
                    static_cast<float>(floorLv2CliffStrength)));
                int roughBoundary = std::clamp(deepestUpperCell + displacement, segmentStart, segmentEnd);
                bool touchesRock = false;
                const int affectedStart = std::max(segmentStart, std::min(deepestUpperCell, roughBoundary) - 1);
                const int affectedEnd = std::min(segmentEnd, std::max(deepestUpperCell, roughBoundary) + 1);
                for (int affectedY = affectedStart; affectedY <= affectedEnd && !touchesRock; ++affectedY) {
                    for (int offsetY = -1; offsetY <= 1 && !touchesRock; ++offsetY) {
                        for (int offsetX = -1; offsetX <= 1; ++offsetX) {
                            if (offsetX == 0 && offsetY == 0) {
                                continue;
                            }
                            const int nx = x + offsetX;
                            const int ny = affectedY + offsetY;
                            if (InBounds(nx, ny, w, h) && bestFloor[static_cast<std::size_t>(Index(nx, ny, w))] == 0) {
                                touchesRock = true;
                                break;
                            }
                        }
                    }
                }
                if (touchesRock && roughBoundary < deepestUpperCell) {
                    roughBoundary = deepestUpperCell;
                }
                for (int segmentY = segmentStart; segmentY <= segmentEnd; ++segmentY) {
                    outBiomes[static_cast<std::size_t>(Index(x, segmentY, w))] =
                        static_cast<std::uint8_t>(segmentY <= roughBoundary ? FLOOR_LV2 : FLOOR_LV1);
                }
            }
        }

        const std::vector<std::uint8_t> verticalBoundarySource = outBiomes;
        for (int boundaryX = 0; boundaryX + 1 < w; ++boundaryX) {
            int runStart = -1;
            for (int y = 0; y <= h; ++y) {
                bool isVerticalBoundary = false;
                if (y < h) {
                    const std::uint8_t left = verticalBoundarySource[static_cast<std::size_t>(Index(boundaryX, y, w))];
                    const std::uint8_t right = verticalBoundarySource[static_cast<std::size_t>(Index(boundaryX + 1, y, w))];
                    isVerticalBoundary = (left == FLOOR_LV1 && right == FLOOR_LV2) ||
                                         (left == FLOOR_LV2 && right == FLOOR_LV1);
                }
                if (isVerticalBoundary && runStart < 0) {
                    runStart = y;
                }
                if (isVerticalBoundary || runStart < 0) {
                    continue;
                }
                const int runEnd = y - 1;
                if (runEnd - runStart + 1 >= 4) {
                    const bool lv2OnLeft = verticalBoundarySource[static_cast<std::size_t>(Index(boundaryX, runStart, w))] == FLOOR_LV2;
                    std::vector<float> runNoise(static_cast<std::size_t>(runEnd - runStart + 1), 0.5f);
                    float minRunNoise = 1.0f;
                    float maxRunNoise = 0.0f;
                    for (int runY = runStart; runY <= runEnd; ++runY) {
                        const float broadNoise = mapgen::Fbm2D(
                            static_cast<float>(boundaryX) * floorLv2CliffFrequency,
                            static_cast<float>(runY) * floorLv2CliffFrequency,
                            3, 1.9f, 0.52f, cliffSeed ^ 0x5be0cd19U);
                        const float detailNoise = mapgen::Fbm2D(
                            static_cast<float>(boundaryX) * floorLv2CliffFrequency * 2.1f,
                            static_cast<float>(runY) * floorLv2CliffFrequency * 2.1f,
                            2, 2.0f, 0.50f, cliffSeed ^ 0xcbbb9d5dU);
                        const float displacementNoise = broadNoise * 0.75f + detailNoise * 0.25f;
                        runNoise[static_cast<std::size_t>(runY - runStart)] = displacementNoise;
                        minRunNoise = std::min(minRunNoise, displacementNoise);
                        maxRunNoise = std::max(maxRunNoise, displacementNoise);
                    }
                    const float runNoiseRange = maxRunNoise - minRunNoise;
                    for (int runY = runStart; runY <= runEnd; ++runY) {
                        const float displacementNoise = runNoise[static_cast<std::size_t>(runY - runStart)];
                        const float normalizedNoise = runNoiseRange > 0.0001f
                            ? ((displacementNoise - minRunNoise) / runNoiseRange) * 2.0f - 1.0f
                            : 0.0f;
                        const int displacement = static_cast<int>(std::lround(
                            normalizedNoise * static_cast<float>(floorLv2CliffStrength)));
                        const int targetBoundary = std::clamp(boundaryX + displacement, 0, w - 2);
                        const int affectedStart = std::max(0, std::min(boundaryX, targetBoundary) - 1);
                        const int affectedEnd = std::min(w - 1, std::max(boundaryX + 1, targetBoundary + 1) + 1);
                        bool touchesRock = false;
                        for (int affectedX = affectedStart; affectedX <= affectedEnd && !touchesRock; ++affectedX) {
                            for (int offsetY = -1; offsetY <= 1 && !touchesRock; ++offsetY) {
                                for (int offsetX = -1; offsetX <= 1; ++offsetX) {
                                    const int nx = affectedX + offsetX;
                                    const int ny = runY + offsetY;
                                    if (InBounds(nx, ny, w, h) && bestFloor[static_cast<std::size_t>(Index(nx, ny, w))] == 0) {
                                        touchesRock = true;
                                        break;
                                    }
                                }
                            }
                        }
                        const bool contractsLv2 = lv2OnLeft ? displacement < 0 : displacement > 0;
                        if (touchesRock && contractsLv2) {
                            continue;
                        }
                        for (int affectedX = affectedStart; affectedX <= affectedEnd; ++affectedX) {
                            const int index = Index(affectedX, runY, w);
                            if (bestFloor[static_cast<std::size_t>(index)] == 0) {
                                continue;
                            }
                            const bool becomesLv2 = lv2OnLeft ? affectedX <= targetBoundary : affectedX > targetBoundary;
                            outBiomes[static_cast<std::size_t>(index)] =
                                static_cast<std::uint8_t>(becomesLv2 ? FLOOR_LV2 : FLOOR_LV1);
                        }
                    }
                }
                runStart = -1;
            }
        }

        for (int x = 0; x < w; ++x) {
            int y = 0;
            while (y < h) {
                while (y < h && bestFloor[static_cast<std::size_t>(Index(x, y, w))] == 0) {
                    ++y;
                }
                const int segmentStart = y;
                int deepestUpperCell = -1;
                while (y < h && bestFloor[static_cast<std::size_t>(Index(x, y, w))] != 0) {
                    if (outBiomes[static_cast<std::size_t>(Index(x, y, w))] == FLOOR_LV2) {
                        deepestUpperCell = y;
                    }
                    ++y;
                }
                for (int segmentY = segmentStart; segmentY <= deepestUpperCell; ++segmentY) {
                    outBiomes[static_cast<std::size_t>(Index(x, segmentY, w))] = FLOOR_LV2;
                }
            }
        }
        if (longestVerticalFloorLevelBoundary(outBiomes) >
            longestVerticalFloorLevelBoundary(verticalBoundarySource)) {
            outBiomes = verticalBoundarySource;
        }
    }

    std::vector<int> wallDepth(static_cast<std::size_t>(n), std::numeric_limits<int>::max());
    std::queue<int> pending;
    for (int i = 0; i < n; ++i) if (bestFloor[static_cast<std::size_t>(i)] != 0) { wallDepth[static_cast<std::size_t>(i)] = 0; pending.push(i); }
    constexpr int dx[4] = {1, -1, 0, 0};
    constexpr int dy[4] = {0, 0, 1, -1};
    while (!pending.empty()) {
        const int current = pending.front(); pending.pop();
        const int x = current % w; const int y = current / w;
        for (int direction = 0; direction < 4; ++direction) {
            const int nx = x + dx[direction]; const int ny = y + dy[direction];
            if (!InBounds(nx, ny, w, h)) continue;
            const int next = Index(nx, ny, w);
            if (wallDepth[static_cast<std::size_t>(next)] > wallDepth[static_cast<std::size_t>(current)] + 1) { wallDepth[static_cast<std::size_t>(next)] = wallDepth[static_cast<std::size_t>(current)] + 1; pending.push(next); }
        }
    }

    for (int index = 0; index < n; ++index) {
        if (bestFloor[static_cast<std::size_t>(index)] == 0 && wallDepth[static_cast<std::size_t>(index)] >= 4) {
            outBiomes[static_cast<std::size_t>(index)] = INNERWALL;
        }
    }

    std::mt19937 rng(params_.seed ^ 0xd1b54a35U);
    const int requestedLakes = std::min(lakeMaxCount, static_cast<int>(std::round(lakeChance * static_cast<float>(lakeMaxCount + 2))));
    std::vector<int> lakeCenters;
    std::vector<std::vector<int>> wallIslands;
    std::vector<std::uint8_t> wallVisited(static_cast<std::size_t>(n), 0);
    const int islandMinArea = std::max(18, lakeMinWidth * lakeMinDepth / 2);
    const int islandMaxArea = std::max(islandMinArea, lakeMaxWidth * lakeMaxDepth * 2);
    for (int start = 0; start < n; ++start) {
        if (bestFloor[static_cast<std::size_t>(start)] != 0 || wallVisited[static_cast<std::size_t>(start)] != 0) {
            continue;
        }
        bool touchesBorder = false;
        std::vector<int> component;
        std::queue<int> wallPending;
        wallPending.push(start);
        wallVisited[static_cast<std::size_t>(start)] = 1;
        while (!wallPending.empty()) {
            const int current = wallPending.front();
            wallPending.pop();
            component.push_back(current);
            const int x = current % w;
            const int y = current / w;
            if (x <= borderThickness || y <= borderThickness || x >= w - 1 - borderThickness || y >= h - 1 - borderThickness) {
                touchesBorder = true;
            }
            for (int direction = 0; direction < 4; ++direction) {
                const int nx = x + dx[direction];
                const int ny = y + dy[direction];
                if (!InBounds(nx, ny, w, h)) {
                    continue;
                }
                const int next = Index(nx, ny, w);
                if (bestFloor[static_cast<std::size_t>(next)] == 0 && wallVisited[static_cast<std::size_t>(next)] == 0) {
                    wallVisited[static_cast<std::size_t>(next)] = 1;
                    wallPending.push(next);
                }
            }
        }
        if (!touchesBorder && static_cast<int>(component.size()) >= islandMinArea && static_cast<int>(component.size()) <= islandMaxArea) {
            wallIslands.push_back(std::move(component));
        }
    }
    std::shuffle(wallIslands.begin(), wallIslands.end(), rng);
    for (const std::vector<int>& island : wallIslands) {
        if (static_cast<int>(lakeCenters.size()) >= requestedLakes || lakeCenters.size() >= static_cast<std::size_t>((requestedLakes + 1) / 2)) {
            break;
        }
        long long sumX = 0;
        long long sumY = 0;
        int shoreContacts = 0;
        for (int index : island) {
            const int x = index % w;
            const int y = index / w;
            sumX += x;
            sumY += y;
            for (int direction = 0; direction < 4; ++direction) {
                const int nx = x + dx[direction];
                const int ny = y + dy[direction];
                if (InBounds(nx, ny, w, h) && bestFloor[static_cast<std::size_t>(Index(nx, ny, w))] != 0) {
                    ++shoreContacts;
                    break;
                }
            }
        }
        if (shoreContacts < 8) {
            continue;
        }
        const int center = Index(static_cast<int>(sumX / static_cast<long long>(island.size())), static_cast<int>(sumY / static_cast<long long>(island.size())), w);
        for (int index : island) {
            outBiomes[static_cast<std::size_t>(index)] = LAKE;
        }
        lakeCenters.push_back(center);
    }

    std::vector<int> shorelineSeeds;
    for (int y = borderThickness + lakeMaxDepth; y < h - borderThickness - 2; ++y) {
        for (int x = borderThickness + 2; x < w - borderThickness - 2; ++x) {
            const int index = Index(x, y, w);
            if (bestFloor[static_cast<std::size_t>(index)] != 0 || bestFloor[static_cast<std::size_t>(Index(x, y + 1, w))] == 0) {
                continue;
            }
            int upperRock = 0;
            for (int offset = 1; offset <= lakeMinDepth; ++offset) {
                if (bestFloor[static_cast<std::size_t>(Index(x, y - offset, w))] == 0) {
                    ++upperRock;
                }
            }
            if (upperRock == lakeMinDepth) {
                shorelineSeeds.push_back(index);
            }
        }
    }
    std::shuffle(shorelineSeeds.begin(), shorelineSeeds.end(), rng);
    for (int seed : shorelineSeeds) {
        if (static_cast<int>(lakeCenters.size()) >= requestedLakes) {
            break;
        }
        const int seedX = seed % w;
        const int seedY = seed / w;
        bool separated = true;
        for (int center : lakeCenters) {
            if (std::abs(seedX - center % w) + std::abs(seedY - center / w) < lakeMaxWidth + lakeMaxDepth) {
                separated = false;
                break;
            }
        }
        if (!separated) {
            continue;
        }

        const int targetArea = std::uniform_int_distribution<int>(lakeMinWidth * lakeMinDepth, lakeMaxWidth * lakeMaxDepth)(rng);
        const std::uint32_t lakeSeed = params_.seed ^ static_cast<std::uint32_t>(seed * 977 + static_cast<int>(lakeCenters.size()) * 131);
        using GrowthCell = std::pair<float, int>;
        std::priority_queue<GrowthCell, std::vector<GrowthCell>, std::greater<GrowthCell>> frontier;
        std::vector<std::uint8_t> queued(static_cast<std::size_t>(n), 0);
        std::vector<int> basin;
        frontier.emplace(0.0f, seed);
        queued[static_cast<std::size_t>(seed)] = 1;
        while (!frontier.empty() && static_cast<int>(basin.size()) < targetArea) {
            const auto [cost, current] = frontier.top();
            frontier.pop();
            (void)cost;
            const int x = current % w;
            const int y = current / w;
            if (bestFloor[static_cast<std::size_t>(current)] != 0 || outBiomes[static_cast<std::size_t>(current)] == LAKE) {
                continue;
            }
            if (std::abs(x - seedX) > lakeMaxWidth || y > seedY + 1 || seedY - y > lakeMaxDepth * 2) {
                continue;
            }
            basin.push_back(current);
            for (int direction = 0; direction < 4; ++direction) {
                const int nx = x + dx[direction];
                const int ny = y + dy[direction];
                if (!InBounds(nx, ny, w, h)) {
                    continue;
                }
                const int next = Index(nx, ny, w);
                if (queued[static_cast<std::size_t>(next)] != 0 || bestFloor[static_cast<std::size_t>(next)] != 0) {
                    continue;
                }
                queued[static_cast<std::size_t>(next)] = 1;
                const float lowNoise = mapgen::Fbm2D(nx * 0.055f, ny * 0.055f, 3, 1.9f, 0.52f, lakeSeed);
                const float horizontalDrift = static_cast<float>(std::abs(nx - seedX)) / static_cast<float>(std::max(1, lakeMaxWidth));
                const float downwardPenalty = ny > seedY ? 2.0f : 0.0f;
                const float depthPreference = std::abs(static_cast<float>(wallDepth[static_cast<std::size_t>(next)] - 3)) * 0.12f;
                const float nextCost = (1.0f - lowNoise) * 1.8f + horizontalDrift * 0.45f + downwardPenalty + depthPreference;
                frontier.emplace(nextCost, next);
            }
        }
        int lowerShore = 0;
        int upperBacked = 0;
        for (int index : basin) {
            const int x = index % w;
            const int y = index / w;
            if (y + 1 < h && bestFloor[static_cast<std::size_t>(Index(x, y + 1, w))] != 0) {
                ++lowerShore;
            }
            if (y - 2 >= 0 && bestFloor[static_cast<std::size_t>(Index(x, y - 2, w))] == 0) {
                ++upperBacked;
            }
        }
        if (static_cast<int>(basin.size()) < lakeMinWidth * lakeMinDepth || lowerShore < 3 || upperBacked * 4 < static_cast<int>(basin.size()) * 3) {
            continue;
        }
        for (int index : basin) {
            outBiomes[static_cast<std::size_t>(index)] = LAKE;
        }
        lakeCenters.push_back(seed);
    }

    const int shorelineWallLimit = std::clamp(lakeMinDepth, 2, 4);
    std::vector<int> lakeDistance(static_cast<std::size_t>(n), std::numeric_limits<int>::max());
    std::queue<int> lakePending;
    for (int index = 0; index < n; ++index) {
        if (outBiomes[static_cast<std::size_t>(index)] == LAKE) {
            lakeDistance[static_cast<std::size_t>(index)] = 0;
            lakePending.push(index);
        }
    }
    while (!lakePending.empty()) {
        const int current = lakePending.front();
        lakePending.pop();
        if (lakeDistance[static_cast<std::size_t>(current)] >= shorelineWallLimit) {
            continue;
        }
        const int x = current % w;
        const int y = current / w;
        for (int direction = 0; direction < 4; ++direction) {
            const int nx = x + dx[direction];
            const int ny = y + dy[direction];
            if (!InBounds(nx, ny, w, h)) {
                continue;
            }
            const int next = Index(nx, ny, w);
            const int nextDistance = lakeDistance[static_cast<std::size_t>(current)] + 1;
            if (bestFloor[static_cast<std::size_t>(next)] == 0 && lakeDistance[static_cast<std::size_t>(next)] > nextDistance) {
                lakeDistance[static_cast<std::size_t>(next)] = nextDistance;
                lakePending.push(next);
            }
        }
    }
    for (int index = 0; index < n; ++index) {
        if (outBiomes[static_cast<std::size_t>(index)] != LAKE &&
            lakeDistance[static_cast<std::size_t>(index)] <= shorelineWallLimit &&
            lakeDistance[static_cast<std::size_t>(index)] + wallDepth[static_cast<std::size_t>(index)] <= shorelineWallLimit + 1) {
            outBiomes[static_cast<std::size_t>(index)] = LAKE;
        }
    }

    constexpr int lakeInnerWallBuffer = 3;
    std::vector<int> finalLakeDistance(static_cast<std::size_t>(n), std::numeric_limits<int>::max());
    std::queue<int> finalLakePending;
    for (int index = 0; index < n; ++index) {
        if (outBiomes[static_cast<std::size_t>(index)] == LAKE) {
            finalLakeDistance[static_cast<std::size_t>(index)] = 0;
            finalLakePending.push(index);
        }
    }
    while (!finalLakePending.empty()) {
        const int current = finalLakePending.front();
        finalLakePending.pop();
        const int currentDistance = finalLakeDistance[static_cast<std::size_t>(current)];
        if (currentDistance >= lakeInnerWallBuffer) {
            continue;
        }
        const int x = current % w;
        const int y = current / w;
        for (int direction = 0; direction < 4; ++direction) {
            const int nx = x + dx[direction];
            const int ny = y + dy[direction];
            if (!InBounds(nx, ny, w, h)) {
                continue;
            }
            const int next = Index(nx, ny, w);
            const std::uint8_t neighbor = outBiomes[static_cast<std::size_t>(next)];
            if ((neighbor == WALL || neighbor == INNERWALL) &&
                finalLakeDistance[static_cast<std::size_t>(next)] > currentDistance + 1) {
                finalLakeDistance[static_cast<std::size_t>(next)] = currentDistance + 1;
                finalLakePending.push(next);
            }
        }
    }
    for (int index = 0; index < n; ++index) {
        if (outBiomes[static_cast<std::size_t>(index)] == INNERWALL &&
            finalLakeDistance[static_cast<std::size_t>(index)] <= lakeInnerWallBuffer) {
            outBiomes[static_cast<std::size_t>(index)] = WALL;
        }
    }

    std::vector<std::uint8_t> waterVisited(static_cast<std::size_t>(n), 0);
    for (int start = 0; start < n; ++start) {
        if (outBiomes[static_cast<std::size_t>(start)] != LAKE || waterVisited[static_cast<std::size_t>(start)] != 0) {
            continue;
        }
        bool touchesFloorLv1 = false;
        std::vector<int> component;
        std::queue<int> waterPending;
        waterPending.push(start);
        waterVisited[static_cast<std::size_t>(start)] = 1;
        while (!waterPending.empty()) {
            const int current = waterPending.front();
            waterPending.pop();
            component.push_back(current);
            const int x = current % w;
            const int y = current / w;
            for (int direction = 0; direction < 4; ++direction) {
                const int nx = x + dx[direction];
                const int ny = y + dy[direction];
                if (!InBounds(nx, ny, w, h)) {
                    continue;
                }
                const int next = Index(nx, ny, w);
                const std::uint8_t neighbor = outBiomes[static_cast<std::size_t>(next)];
                if (neighbor == FLOOR_LV1) {
                    touchesFloorLv1 = true;
                } else if (neighbor == LAKE && waterVisited[static_cast<std::size_t>(next)] == 0) {
                    waterVisited[static_cast<std::size_t>(next)] = 1;
                    waterPending.push(next);
                }
            }
        }
        if (touchesFloorLv1) {
            for (int index : component) {
                outBiomes[static_cast<std::size_t>(index)] = RIVER;
            }
        }
    }

    // Explicitly materialize level transitions.  Previously LADDER existed in
    // the palette but was never emitted, leaving multi-level caves without a
    // usable transition marker.
    const int requestedLadders = std::clamp(static_cast<int>(std::lround(GetParam("ladderCount", 1.0))), 0, 8);
    std::vector<int> ladderCandidates;
    ladderCandidates.reserve(static_cast<std::size_t>(n / 8));
    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            const int cell = Index(x, y, w);
            if (outBiomes[static_cast<std::size_t>(cell)] != FLOOR_LV1 &&
                outBiomes[static_cast<std::size_t>(cell)] != FLOOR_LV2) {
                continue;
            }
            bool touchesOtherLevel = false;
            for (const auto& direction : std::array<std::array<int, 2>, 4>{{{{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}}}}) {
                const int nx = x + direction[0];
                const int ny = y + direction[1];
                if (InBounds(nx, ny, w, h)) {
                    const std::uint8_t neighbor = outBiomes[static_cast<std::size_t>(Index(nx, ny, w))];
                    if ((outBiomes[static_cast<std::size_t>(cell)] == FLOOR_LV1 && neighbor == FLOOR_LV2) ||
                        (outBiomes[static_cast<std::size_t>(cell)] == FLOOR_LV2 && neighbor == FLOOR_LV1)) {
                        touchesOtherLevel = true;
                        break;
                    }
                }
            }
            if (touchesOtherLevel) {
                ladderCandidates.push_back(cell);
            }
        }
    }
    std::mt19937 ladderRng(params_.seed ^ 0x6d61706cU);
    std::shuffle(ladderCandidates.begin(), ladderCandidates.end(), ladderRng);
    const int ladderCount = std::min(requestedLadders, static_cast<int>(ladderCandidates.size()));
    for (int i = 0; i < ladderCount; ++i) {
        const int cell = ladderCandidates[static_cast<std::size_t>(i)];
        outBiomes[static_cast<std::size_t>(cell)] = LADDER;
        outPlan.markers.push_back({"ladder", cell % w, cell / w, i});
    }

    return true;
}
