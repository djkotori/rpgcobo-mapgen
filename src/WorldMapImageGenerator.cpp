#include "WorldMapImageGenerator.h"

#include "utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <unordered_set>
#include <tuple>
#include <utility>
#include <vector>

namespace {

struct Point {
    int x = 0;
    int y = 0;
};

struct Edge {
    int a = 0;
    int b = 0;
    float d = 0.0f;
};

class DisjointSet {
public:
    explicit DisjointSet(int n) : parent_(static_cast<std::size_t>(n)), rank_(static_cast<std::size_t>(n), 0) {
        for (int i = 0; i < n; ++i) {
            parent_[static_cast<std::size_t>(i)] = i;
        }
    }

    int Find(int x) {
        if (parent_[static_cast<std::size_t>(x)] == x) {
            return x;
        }
        parent_[static_cast<std::size_t>(x)] = Find(parent_[static_cast<std::size_t>(x)]);
        return parent_[static_cast<std::size_t>(x)];
    }

    bool Unite(int a, int b) {
        a = Find(a);
        b = Find(b);
        if (a == b) {
            return false;
        }
        if (rank_[static_cast<std::size_t>(a)] < rank_[static_cast<std::size_t>(b)]) {
            std::swap(a, b);
        }
        parent_[static_cast<std::size_t>(b)] = a;
        if (rank_[static_cast<std::size_t>(a)] == rank_[static_cast<std::size_t>(b)]) {
            ++rank_[static_cast<std::size_t>(a)];
        }
        return true;
    }

private:
    std::vector<int> parent_;
    std::vector<int> rank_;
};

inline int Index(int x, int y, int w) {
    return y * w + x;
}

inline bool InBounds(int x, int y, int w, int h) {
    return (x >= 0 && y >= 0 && x < w && y < h);
}

bool IsWater(std::uint8_t biome) {
    return biome == SEA || biome == RIVER || biome == LAKE;
}

bool IsMountainLike(std::uint8_t biome) {
    return biome == MOUNTAIN || biome == EXMOUNTAIN;
}

bool IsPassableForPoi(std::uint8_t biome) {
    return biome == PLAIN || biome == DESERT;
}

bool IsRoadLikeBiome(std::uint8_t biome) {
    return biome == ROAD || biome == BRIDGE;
}

float Clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

enum class MacroMode {
    Archipelago,
    Continent,
    Basin,
};

MacroMode ChooseMode(std::mt19937& rng, double archipelagoWeight, double continentWeight, double basinWeight) {
    std::discrete_distribution<int> dist({
        std::max(0.0, archipelagoWeight),
        std::max(0.0, continentWeight),
        std::max(0.0, basinWeight),
    });
    const int mode = dist(rng);
    if (mode == 0) {
        return MacroMode::Archipelago;
    }
    if (mode == 1) {
        return MacroMode::Continent;
    }
    return MacroMode::Basin;
}

std::vector<int> MultiSourceDistance(const std::vector<std::uint8_t>& biomes, int w, int h, const std::vector<std::uint8_t>& sourceKinds) {
    const int n = w * h;
    std::vector<int> dist(static_cast<std::size_t>(n), std::numeric_limits<int>::max());
    std::queue<int> q;

    auto isSource = [&sourceKinds](std::uint8_t b) {
        for (std::uint8_t s : sourceKinds) {
            if (b == s) {
                return true;
            }
        }
        return false;
    };

    for (int i = 0; i < n; ++i) {
        if (isSource(biomes[static_cast<std::size_t>(i)])) {
            dist[static_cast<std::size_t>(i)] = 0;
            q.push(i);
        }
    }

    const std::array<int, 4> dx = {1, -1, 0, 0};
    const std::array<int, 4> dy = {0, 0, 1, -1};

    while (!q.empty()) {
        const int cur = q.front();
        q.pop();

        const int x = cur % w;
        const int y = cur / w;
        const int nd = dist[static_cast<std::size_t>(cur)] + 1;

        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[static_cast<std::size_t>(k)];
            const int ny = y + dy[static_cast<std::size_t>(k)];
            if (!InBounds(nx, ny, w, h)) {
                continue;
            }
            const int ni = Index(nx, ny, w);
            if (dist[static_cast<std::size_t>(ni)] <= nd) {
                continue;
            }
            dist[static_cast<std::size_t>(ni)] = nd;
            q.push(ni);
        }
    }

    return dist;
}

std::vector<Point> ReconstructPath(const std::vector<int>& parent, int goal, int w) {
    std::vector<Point> path;
    int cur = goal;
    while (cur >= 0) {
        const int x = cur % w;
        const int y = cur / w;
        path.push_back({x, y});
        cur = parent[static_cast<std::size_t>(cur)];
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<Point> FindPathAStar(
    const std::vector<std::uint8_t>& biomes,
    int w,
    int h,
    Point start,
    Point goal,
    float waterCost,
    bool allowMountain) {
    const int n = w * h;
    const int s = Index(start.x, start.y, w);
    const int g = Index(goal.x, goal.y, w);

    auto heuristic = [goal](int x, int y) {
        const int dx = std::abs(goal.x - x);
        const int dy = std::abs(goal.y - y);
        return static_cast<float>(dx + dy);
    };

    auto cellCost = [waterCost, allowMountain](std::uint8_t b) {
        if (b == ROAD || b == BRIDGE) {
            // Encourage route reuse to avoid dense parallel roads.
            return 0.35f;
        }
        if (!allowMountain && (b == MOUNTAIN || b == EXMOUNTAIN)) {
            return 1000000.0f;
        }
        if (allowMountain && (b == MOUNTAIN || b == EXMOUNTAIN)) {
            return 26.0f;
        }
        if (b == SEA || b == RIVER || b == LAKE) {
            return waterCost;
        }
        if (b == FOREST) {
            return 3.0f;
        }
        return 1.0f;
    };

    std::vector<float> gScore(static_cast<std::size_t>(n), std::numeric_limits<float>::infinity());
    std::vector<int> parent(static_cast<std::size_t>(n), -1);
    std::vector<std::uint8_t> closed(static_cast<std::size_t>(n), 0);

    struct Node {
        int idx;
        float f;
    };
    struct Cmp {
        bool operator()(const Node& a, const Node& b) const {
            return a.f > b.f;
        }
    };

    std::priority_queue<Node, std::vector<Node>, Cmp> open;
    gScore[static_cast<std::size_t>(s)] = 0.0f;
    open.push({s, heuristic(start.x, start.y)});

    const std::array<int, 8> dx = {1, -1, 0, 0, 1, 1, -1, -1};
    const std::array<int, 8> dy = {0, 0, 1, -1, 1, -1, 1, -1};

    while (!open.empty()) {
        const Node curNode = open.top();
        open.pop();

        const int cur = curNode.idx;
        if (closed[static_cast<std::size_t>(cur)] != 0) {
            continue;
        }
        closed[static_cast<std::size_t>(cur)] = 1;

        if (cur == g) {
            return ReconstructPath(parent, g, w);
        }

        const int x = cur % w;
        const int y = cur / w;

        for (int k = 0; k < 8; ++k) {
            const int nx = x + dx[static_cast<std::size_t>(k)];
            const int ny = y + dy[static_cast<std::size_t>(k)];
            if (!InBounds(nx, ny, w, h)) {
                continue;
            }

            const int ni = Index(nx, ny, w);
            const float cc = cellCost(biomes[static_cast<std::size_t>(ni)]);
            if (cc > 100000.0f) {
                continue;
            }

            float lanePenalty = 0.0f;
            if (!IsRoadLikeBiome(biomes[static_cast<std::size_t>(ni)])) {
                int roadAdj = 0;
                const std::array<int, 4> adx = {1, -1, 0, 0};
                const std::array<int, 4> ady = {0, 0, 1, -1};
                for (int ai = 0; ai < 4; ++ai) {
                    const int ax = nx + adx[static_cast<std::size_t>(ai)];
                    const int ay = ny + ady[static_cast<std::size_t>(ai)];
                    if (!InBounds(ax, ay, w, h)) {
                        continue;
                    }
                    if (IsRoadLikeBiome(biomes[static_cast<std::size_t>(Index(ax, ay, w))])) {
                        ++roadAdj;
                    }
                }

                // Penalize carving side-by-side lanes near existing roads.
                if (roadAdj >= 2) {
                    lanePenalty = 1.35f;
                } else if (roadAdj == 1) {
                    lanePenalty = 0.25f;
                }
            }

            const float step = (k < 4) ? 1.0f : 1.4f;
            const float tentative = gScore[static_cast<std::size_t>(cur)] + step * (cc + lanePenalty);
            if (tentative < gScore[static_cast<std::size_t>(ni)]) {
                gScore[static_cast<std::size_t>(ni)] = tentative;
                parent[static_cast<std::size_t>(ni)] = cur;
                const float f = tentative + heuristic(nx, ny);
                open.push({ni, f});
            }
        }
    }

    return {};
}

}  // namespace

WorldMapImageGenerator::WorldMapImageGenerator(MapGenParams params) : MapImageGenerator(std::move(params)) {}

MapGenColor WorldMapImageGenerator::BiomeToColor(std::uint8_t biome) const {
    switch (biome) {
        case PLAIN:
            return {114, 187, 78};
        case DESERT:
            return {231, 209, 136};
        case FOREST:
            return {47, 129, 53};
        case MOUNTAIN:
            return {146, 146, 146};
        case EXMOUNTAIN:
            return {105, 105, 105};
        case ROAD:
            return {216, 189, 116};
        case BRIDGE:
            return {154, 108, 62};
        case RIVER:
            return {47, 124, 234};
        case LAKE:
            return {70, 145, 240};
        case SEA:
            return {28, 90, 202};
        default:
            return {255, 0, 255};
    }
}

bool WorldMapImageGenerator::Generate(MapPlan& outPlan, std::string& outError) {
    const int w = params_.width;
    const int h = params_.depth;
    if (w <= 0 || h <= 0) {
        outError = "Invalid map size";
        return false;
    }

    const int n = w * h;
    outPlan.Reset(w, h, params_.seed, "world");
    std::vector<std::uint8_t>& outBiomes = outPlan.biomes;
    outBiomes.assign(static_cast<std::size_t>(n), static_cast<std::uint8_t>(PLAIN));

    const float seaLevel = static_cast<float>(std::clamp(GetParam("seaLevel", 0.48), 0.2, 0.8));
    const float scale = static_cast<float>(std::clamp(GetParam("scale", 1.0), 0.2, 4.0));
    const float ridgeStrength = static_cast<float>(std::clamp(GetParam("ridgeStrength", 0.55), 0.1, 1.2));
    const float riverDensity = static_cast<float>(std::clamp(GetParam("riverDensity", 1.0), 0.25, 3.0));
    const float roadDensity = static_cast<float>(std::clamp(GetParam("roadDensity", 1.0), 0.25, 3.0));
    const float basinCornerRound = static_cast<float>(std::clamp(GetParam("basinCornerRound", 0.82), 0.0, 1.0));
    const int riverMinBase = static_cast<int>(std::clamp(GetParam("riverCountMin", 2.0), 1.0, 8.0));
    const int riverMaxBase = static_cast<int>(std::clamp(GetParam("riverCountMax", 5.0), static_cast<double>(riverMinBase), 12.0));
    const int riverMin = std::max(1, static_cast<int>(std::round(static_cast<float>(riverMinBase) * riverDensity)));
    const int riverMax = std::max(riverMin, static_cast<int>(std::round(static_cast<float>(riverMaxBase) * riverDensity)));
    const float roadExtraEdgeRate = static_cast<float>(std::clamp(GetParam("roadExtraEdgeRate", 0.45), 0.0, 1.2)) * roadDensity;
    const int bridgeMinInterval = static_cast<int>(std::clamp(GetParam("bridgeMinInterval", 18.0), 3.0, 128.0));

    std::mt19937 rng(params_.seed);
    std::uniform_real_distribution<float> ur(0.0f, 1.0f);

    auto BasinRoundMetric = [basinCornerRound](float nx, float ny) {
        const float fx = std::abs(nx * 2.0f - 1.0f);
        const float fy = std::abs(ny * 2.0f - 1.0f);
        const float rSquare = std::max(fx, fy);
        const float rCircle = std::sqrt(fx * fx + fy * fy);
        return rSquare * (1.0f - basinCornerRound) + rCircle * basinCornerRound;
    };

    const MacroMode mode = ChooseMode(
        rng,
        GetParam("macroArchipelagoWeight", 0.35),
        GetParam("macroContinentWeight", 0.40),
        GetParam("macroBasinWeight", 0.25));

    std::vector<float> height(static_cast<std::size_t>(n), 0.0f);
    std::vector<float> ridge(static_cast<std::size_t>(n), 0.0f);

    float hmin = std::numeric_limits<float>::max();
    float hmax = std::numeric_limits<float>::lowest();

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float nx = static_cast<float>(x) / static_cast<float>(std::max(1, w - 1));
            const float ny = static_cast<float>(y) / static_cast<float>(std::max(1, h - 1));

            float base = mapgen::Fbm2D(nx * 3.6f * scale, ny * 3.6f * scale, 5, 2.0f, 0.5f, params_.seed ^ 0x1234abcdU);
            const float detail = mapgen::Fbm2D(nx * 9.0f * scale, ny * 9.0f * scale, 3, 2.2f, 0.55f, params_.seed ^ 0x8f3a2c17U);
            const float worley = 1.0f - mapgen::Worley2D(nx * static_cast<float>(w), ny * static_cast<float>(h), 18.0f / scale, params_.seed ^ 0x77aab003U);
            float radial = 1.0f;

            if (mode == MacroMode::Archipelago) {
                const float cx = nx - 0.5f;
                const float cy = ny - 0.5f;
                const float d = std::sqrt(cx * cx + cy * cy);
                radial = 1.0f - Clamp01((d - 0.15f) / 0.55f);
            } else if (mode == MacroMode::Continent) {
                const float cx = nx - 0.48f;
                const float cy = ny - 0.52f;
                const float d = std::sqrt(cx * cx + cy * cy);
                radial = 1.0f - 0.6f * Clamp01((d - 0.05f) / 0.8f);
            } else {
                const float cx = nx - 0.5f;
                const float cy = ny - 0.5f;
                const float d = std::sqrt(cx * cx + cy * cy);
                radial = 0.6f + 0.4f * Clamp01((0.9f - d) / 0.9f);
            }

            base = base * 0.62f + detail * 0.18f + worley * 0.20f;
            base *= radial;

            const int i = Index(x, y, w);
            height[static_cast<std::size_t>(i)] = base;
            ridge[static_cast<std::size_t>(i)] = mapgen::Worley2D(nx * static_cast<float>(w), ny * static_cast<float>(h), 11.0f / scale, params_.seed ^ 0x6b5d3e1fU);
            hmin = std::min(hmin, base);
            hmax = std::max(hmax, base);
        }
    }

    const float invRange = (hmax > hmin) ? (1.0f / (hmax - hmin)) : 1.0f;
    for (int i = 0; i < n; ++i) {
        height[static_cast<std::size_t>(i)] = Clamp01((height[static_cast<std::size_t>(i)] - hmin) * invRange);
    }

    {
        const int edgeBand = std::max(8, std::min(w, h) / 14);
        const float basinInner = static_cast<float>(std::clamp(GetParam("basinEdgeInner", 0.74), 0.55, 0.90));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int i = Index(x, y, w);
                const int edgeDist = std::min({x, y, w - 1 - x, h - 1 - y});
                const float nx = static_cast<float>(x) / static_cast<float>(std::max(1, w - 1));
                const float ny = static_cast<float>(y) / static_cast<float>(std::max(1, h - 1));

                if (mode == MacroMode::Basin) {
                    const float r = BasinRoundMetric(nx, ny);
                    const float edgeT = Clamp01((r - basinInner) / std::max(0.02f, 1.0f - basinInner));
                    const float k = edgeT * edgeT;
                    // Mountain/valley boundary mode: keep edge land-dense with organic noise variation.
                    const float edgeNoise = mapgen::Fbm2D(nx * 5.1f * scale, ny * 5.1f * scale, 3, 2.0f, 0.52f, params_.seed ^ 0x2ac84b31U);
                    const float gain = 0.12f + 0.30f * edgeNoise;
                    height[static_cast<std::size_t>(i)] = Clamp01(height[static_cast<std::size_t>(i)] + k * gain);
                } else {
                    const float t = Clamp01(static_cast<float>(edgeDist) / static_cast<float>(std::max(1, edgeBand)));
                    const float k = (1.0f - t) * (1.0f - t);
                    // Ocean boundary mode: pull shoreline inward to avoid clipped edge islands.
                    height[static_cast<std::size_t>(i)] = Clamp01(height[static_cast<std::size_t>(i)] - k * 0.24f);
                }
            }
        }
    }

    {
        // Smooth only around the sea-level band to make coastlines less pixel-speckled.
        const int coastHeightFilterPasses = static_cast<int>(std::clamp(
            GetParam("coastHeightFilterPasses", (scale >= 1.6f) ? 2.0 : 1.0),
            0.0,
            5.0));
        const float coastHeightFilterBand = static_cast<float>(std::clamp(GetParam("coastHeightFilterBand", 0.11), 0.02, 0.30));

        for (int pass = 0; pass < coastHeightFilterPasses; ++pass) {
            std::vector<float> next = height;
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    const int i = Index(x, y, w);
                    const float h0 = height[static_cast<std::size_t>(i)];
                    const float bandT = 1.0f - Clamp01(std::abs(h0 - seaLevel) / coastHeightFilterBand);
                    if (bandT <= 0.0f) {
                        continue;
                    }

                    float sum = 0.0f;
                    for (int oy = -1; oy <= 1; ++oy) {
                        for (int ox = -1; ox <= 1; ++ox) {
                            const int ni = Index(x + ox, y + oy, w);
                            sum += height[static_cast<std::size_t>(ni)];
                        }
                    }

                    const float avg = sum / 9.0f;
                    const float k = 0.62f * bandT;
                    next[static_cast<std::size_t>(i)] = Clamp01(h0 * (1.0f - k) + avg * k);
                }
            }
            height.swap(next);
        }
    }

    for (int i = 0; i < n; ++i) {
        outBiomes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((height[static_cast<std::size_t>(i)] < seaLevel) ? SEA : PLAIN);
    }

    {
        if (mode == MacroMode::Basin) {
            // Basin edge guarantee: only a thin rim, avoiding thick rectangular border bands.
            const float rimThreshold = static_cast<float>(std::clamp(GetParam("basinLandRimThreshold", 0.96), 0.86, 0.995));
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const int i = Index(x, y, w);
                    if (outBiomes[static_cast<std::size_t>(i)] != SEA) {
                        continue;
                    }
                    const float nx = static_cast<float>(x) / static_cast<float>(std::max(1, w - 1));
                    const float ny = static_cast<float>(y) / static_cast<float>(std::max(1, h - 1));
                    const float r = BasinRoundMetric(nx, ny);
                    if (r >= rimThreshold) {
                        outBiomes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(PLAIN);
                    }
                }
            }
        } else {
            // Guarantee a thin ocean ring so islands are fully contained in-map.
            const int seaBand = std::max(2, std::min(w, h) / 80);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const int edgeDist = std::min({x, y, w - 1 - x, h - 1 - y});
                    if (edgeDist <= seaBand) {
                        outBiomes[static_cast<std::size_t>(Index(x, y, w))] = static_cast<std::uint8_t>(SEA);
                    }
                }
            }
        }
    }

    {
        // Coastline cleanup: reduce speckled coast pixels at larger map scales while preserving macro shape.
        const int coastSmoothPasses = static_cast<int>(std::clamp(
            GetParam("coastSmoothPasses", (scale >= 1.6f) ? 3.0 : 2.0),
            0.0,
            6.0));
        const float coastHeightSlack = static_cast<float>(std::clamp(GetParam("coastHeightSlack", 0.055), 0.0, 0.2));
        const int coastMinIsletArea = static_cast<int>(std::clamp(
            GetParam("coastMinIsletArea", static_cast<double>((w * h) / ((scale >= 1.6f) ? 7000 : 11000))),
            0.0,
            600.0));
        const int coastMinSeaPocketArea = static_cast<int>(std::clamp(
            GetParam("coastMinSeaPocketArea", static_cast<double>(std::max(1, coastMinIsletArea / 2))),
            0.0,
            600.0));
        const int coastOuterIsletMaxArea = static_cast<int>(std::clamp(
            GetParam("coastOuterIsletMaxArea", static_cast<double>((w * h) / ((scale >= 1.6f) ? 2800 : 4200))),
            0.0,
            1200.0));
        const int coastOuterIsletDepth = static_cast<int>(std::clamp(
            GetParam("coastOuterIsletDepth", 2.0),
            0.0,
            8.0));
        const int coastTrimPasses = static_cast<int>(std::clamp(
            GetParam("coastTrimPasses", (scale >= 1.6f) ? 2.0 : 1.0),
            0.0,
            5.0));

        for (int pass = 0; pass < coastSmoothPasses; ++pass) {
            std::vector<std::uint8_t> next = outBiomes;
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    const int i = Index(x, y, w);
                    const std::uint8_t cur = outBiomes[static_cast<std::size_t>(i)];
                    const bool isSea = (cur == SEA);
                    int seaN = 0;

                    for (int oy = -1; oy <= 1; ++oy) {
                        for (int ox = -1; ox <= 1; ++ox) {
                            if (ox == 0 && oy == 0) {
                                continue;
                            }
                            const int ni = Index(x + ox, y + oy, w);
                            if (outBiomes[static_cast<std::size_t>(ni)] == SEA) {
                                ++seaN;
                            }
                        }
                    }

                    const float hDelta = std::abs(height[static_cast<std::size_t>(i)] - seaLevel);
                    if (isSea) {
                        if (seaN <= 2 && hDelta <= coastHeightSlack) {
                            next[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(PLAIN);
                        }
                    } else {
                        if (seaN >= 6 && hDelta <= coastHeightSlack) {
                            next[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(SEA);
                        }
                    }
                }
            }
            outBiomes.swap(next);
        }

        for (int pass = 0; pass < coastTrimPasses; ++pass) {
            const std::vector<int> seaDistMask = MultiSourceDistance(outBiomes, w, h, {SEA});
            std::vector<std::uint8_t> next = outBiomes;
            const int trimBand = std::max(2, std::min(w, h) / 96);

            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    const int i = Index(x, y, w);
                    if (outBiomes[static_cast<std::size_t>(i)] == SEA || seaDistMask[static_cast<std::size_t>(i)] > trimBand) {
                        continue;
                    }

                    int seaN = 0;
                    int landCardinal = 0;
                    for (int oy = -1; oy <= 1; ++oy) {
                        for (int ox = -1; ox <= 1; ++ox) {
                            if (ox == 0 && oy == 0) {
                                continue;
                            }
                            const int ni = Index(x + ox, y + oy, w);
                            if (outBiomes[static_cast<std::size_t>(ni)] == SEA) {
                                ++seaN;
                            }
                        }
                    }

                    static const std::array<int, 4> cdx = {1, -1, 0, 0};
                    static const std::array<int, 4> cdy = {0, 0, 1, -1};
                    for (int k = 0; k < 4; ++k) {
                        const int ni = Index(x + cdx[static_cast<std::size_t>(k)], y + cdy[static_cast<std::size_t>(k)], w);
                        if (outBiomes[static_cast<std::size_t>(ni)] != SEA) {
                            ++landCardinal;
                        }
                    }

                    const float hDelta = std::abs(height[static_cast<std::size_t>(i)] - seaLevel);
                    if ((seaN >= 5 || (seaN >= 4 && landCardinal <= 1)) && hDelta <= coastHeightSlack * 1.45f) {
                        next[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(SEA);
                    }
                }
            }

            outBiomes.swap(next);
        }

        if (coastMinIsletArea > 0 || coastMinSeaPocketArea > 0) {
            const std::vector<int> seaDistForComp = MultiSourceDistance(outBiomes, w, h, {SEA});
            std::vector<std::uint8_t> visited(static_cast<std::size_t>(n), 0);
            const std::array<int, 4> dx = {1, -1, 0, 0};
            const std::array<int, 4> dy = {0, 0, 1, -1};

            for (int i = 0; i < n; ++i) {
                if (visited[static_cast<std::size_t>(i)] != 0) {
                    continue;
                }

                const bool seaComp = (outBiomes[static_cast<std::size_t>(i)] == SEA);
                std::queue<int> q;
                std::vector<int> comp;
                q.push(i);
                visited[static_cast<std::size_t>(i)] = 1;
                bool touchesEdge = false;
                int maxCompSeaDist = 0;

                while (!q.empty()) {
                    const int cur = q.front();
                    q.pop();
                    comp.push_back(cur);

                    const int x = cur % w;
                    const int y = cur / w;
                    if (x == 0 || y == 0 || x == w - 1 || y == h - 1) {
                        touchesEdge = true;
                    }
                    if (!seaComp) {
                        maxCompSeaDist = std::max(maxCompSeaDist, seaDistForComp[static_cast<std::size_t>(cur)]);
                    }

                    for (int k = 0; k < 4; ++k) {
                        const int nx = x + dx[static_cast<std::size_t>(k)];
                        const int ny = y + dy[static_cast<std::size_t>(k)];
                        if (!InBounds(nx, ny, w, h)) {
                            continue;
                        }
                        const int ni = Index(nx, ny, w);
                        if (visited[static_cast<std::size_t>(ni)] != 0) {
                            continue;
                        }
                        if ((outBiomes[static_cast<std::size_t>(ni)] == SEA) != seaComp) {
                            continue;
                        }
                        visited[static_cast<std::size_t>(ni)] = 1;
                        q.push(ni);
                    }
                }

                if (touchesEdge) {
                    continue;
                }

                if (seaComp && static_cast<int>(comp.size()) < coastMinSeaPocketArea) {
                    for (int ci : comp) {
                        outBiomes[static_cast<std::size_t>(ci)] = static_cast<std::uint8_t>(PLAIN);
                    }
                }
                if (!seaComp && (static_cast<int>(comp.size()) < coastMinIsletArea ||
                        (static_cast<int>(comp.size()) < coastOuterIsletMaxArea && maxCompSeaDist <= coastOuterIsletDepth))) {
                    for (int ci : comp) {
                        outBiomes[static_cast<std::size_t>(ci)] = static_cast<std::uint8_t>(SEA);
                    }
                }
            }
        }
    }

    if (mode == MacroMode::Basin) {
        const float wallInner = static_cast<float>(std::clamp(GetParam("basinWallInner", 0.84), 0.70, 0.95));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int i = Index(x, y, w);
                const float nx = static_cast<float>(x) / static_cast<float>(std::max(1, w - 1));
                const float ny = static_cast<float>(y) / static_cast<float>(std::max(1, h - 1));
                const float r = BasinRoundMetric(nx, ny);
                const float t = Clamp01((r - wallInner) / std::max(0.02f, 1.0f - wallInner));
                if (t <= 0.0f) {
                    continue;
                }
                const float n0 = mapgen::Fbm2D(nx * 6.3f * scale, ny * 6.3f * scale, 3, 2.0f, 0.5f, params_.seed ^ 0x7e5190a3U);
                const float wallScore = t * 0.74f + n0 * 0.26f;
                if (wallScore > 0.66f) {
                    outBiomes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((wallScore > 0.78f) ? EXMOUNTAIN : MOUNTAIN);
                }
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        if (outBiomes[static_cast<std::size_t>(i)] == SEA) {
            continue;
        }
        const float hv = height[static_cast<std::size_t>(i)];
        const float rv = 1.0f - ridge[static_cast<std::size_t>(i)];
        if (hv > 0.91f || (hv > 0.84f && rv > ridgeStrength * 0.74f)) {
            outBiomes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(EXMOUNTAIN);
        } else if (hv > 0.80f || (hv > 0.72f && rv > ridgeStrength * 0.84f)) {
            outBiomes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(MOUNTAIN);
        }
    }

    {
        // Connect nearby mountain speckles into short ridge links to reduce dotted appearance.
        const int chainAttempts = static_cast<int>(std::clamp(GetParam("mountainChainAttempts", static_cast<double>(std::max(6, std::min(w, h) / 24))), 0.0, 128.0));
        const int chainConnectDist = static_cast<int>(std::clamp(GetParam("mountainChainConnectDist", static_cast<double>(std::max(8, std::min(w, h) / 14))), 4.0, 96.0));
        const int chainMinCompArea = static_cast<int>(std::clamp(GetParam("mountainChainMinCompArea", 2.0), 1.0, 64.0));
        const int chainMaxSeaCross = static_cast<int>(std::clamp(GetParam("mountainChainMaxSeaCross", 0.0), 0.0, 6.0));

        if (chainAttempts > 0) {
            std::vector<int> compId(static_cast<std::size_t>(n), -1);
            std::vector<std::vector<int>> comps;
            comps.reserve(static_cast<std::size_t>(n / 32));
            const std::array<int, 4> dx = {1, -1, 0, 0};
            const std::array<int, 4> dy = {0, 0, 1, -1};

            for (int i = 0; i < n; ++i) {
                if (compId[static_cast<std::size_t>(i)] >= 0 || !IsMountainLike(outBiomes[static_cast<std::size_t>(i)])) {
                    continue;
                }

                const int cid = static_cast<int>(comps.size());
                comps.emplace_back();
                std::queue<int> q;
                q.push(i);
                compId[static_cast<std::size_t>(i)] = cid;

                while (!q.empty()) {
                    const int cur = q.front();
                    q.pop();
                    comps[static_cast<std::size_t>(cid)].push_back(cur);

                    const int x = cur % w;
                    const int y = cur / w;
                    for (int k = 0; k < 4; ++k) {
                        const int nx = x + dx[static_cast<std::size_t>(k)];
                        const int ny = y + dy[static_cast<std::size_t>(k)];
                        if (!InBounds(nx, ny, w, h)) {
                            continue;
                        }
                        const int ni = Index(nx, ny, w);
                        if (compId[static_cast<std::size_t>(ni)] >= 0 || !IsMountainLike(outBiomes[static_cast<std::size_t>(ni)])) {
                            continue;
                        }
                        compId[static_cast<std::size_t>(ni)] = cid;
                        q.push(ni);
                    }
                }
            }

            struct CompInfo {
                int id = -1;
                float cx = 0.0f;
                float cy = 0.0f;
                std::vector<int> edge;
            };

            std::vector<CompInfo> info;
            info.reserve(comps.size());
            for (int cid = 0; cid < static_cast<int>(comps.size()); ++cid) {
                const std::vector<int>& cells = comps[static_cast<std::size_t>(cid)];
                if (static_cast<int>(cells.size()) < chainMinCompArea) {
                    continue;
                }

                CompInfo ci;
                ci.id = cid;
                float sx = 0.0f;
                float sy = 0.0f;
                for (int idx : cells) {
                    const int x = idx % w;
                    const int y = idx / w;
                    sx += static_cast<float>(x);
                    sy += static_cast<float>(y);

                    bool isEdge = false;
                    for (int k = 0; k < 4; ++k) {
                        const int nx = x + dx[static_cast<std::size_t>(k)];
                        const int ny = y + dy[static_cast<std::size_t>(k)];
                        if (!InBounds(nx, ny, w, h)) {
                            isEdge = true;
                            break;
                        }
                        if (!IsMountainLike(outBiomes[static_cast<std::size_t>(Index(nx, ny, w))])) {
                            isEdge = true;
                            break;
                        }
                    }
                    if (isEdge) {
                        ci.edge.push_back(idx);
                    }
                }

                if (ci.edge.empty()) {
                    continue;
                }

                const float inv = 1.0f / static_cast<float>(std::max(1, static_cast<int>(cells.size())));
                ci.cx = sx * inv;
                ci.cy = sy * inv;
                info.push_back(std::move(ci));
            }

            auto makePairKey = [](int a, int b) {
                if (a > b) {
                    std::swap(a, b);
                }
                return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 32) |
                    static_cast<std::uint64_t>(static_cast<std::uint32_t>(b));
            };

            auto drawLine = [&](Point a, Point b) {
                std::vector<int> line;
                int x0 = a.x;
                int y0 = a.y;
                const int x1 = b.x;
                const int y1 = b.y;
                const int sx = (x0 < x1) ? 1 : -1;
                const int sy = (y0 < y1) ? 1 : -1;
                const int dxl = std::abs(x1 - x0);
                const int dyl = std::abs(y1 - y0);
                int err = dxl - dyl;

                while (true) {
                    if (!InBounds(x0, y0, w, h)) {
                        break;
                    }
                    line.push_back(Index(x0, y0, w));
                    if (x0 == x1 && y0 == y1) {
                        break;
                    }
                    const int e2 = err * 2;
                    if (e2 > -dyl) {
                        err -= dyl;
                        x0 += sx;
                    }
                    if (e2 < dxl) {
                        err += dxl;
                        y0 += sy;
                    }
                }
                return line;
            };

            std::shuffle(info.begin(), info.end(), rng);
            std::unordered_set<std::uint64_t> usedPairs;
            int connected = 0;

            for (const CompInfo& from : info) {
                if (connected >= chainAttempts) {
                    break;
                }

                int bestTo = -1;
                float bestD2 = std::numeric_limits<float>::max();
                for (const CompInfo& to : info) {
                    if (from.id == to.id) {
                        continue;
                    }
                    const std::uint64_t key = makePairKey(from.id, to.id);
                    if (usedPairs.find(key) != usedPairs.end()) {
                        continue;
                    }

                    const float dx0 = from.cx - to.cx;
                    const float dy0 = from.cy - to.cy;
                    const float d2 = dx0 * dx0 + dy0 * dy0;
                    if (d2 > static_cast<float>(chainConnectDist * chainConnectDist)) {
                        continue;
                    }
                    if (d2 < bestD2) {
                        bestD2 = d2;
                        bestTo = to.id;
                    }
                }

                if (bestTo < 0) {
                    continue;
                }

                const CompInfo* toInfo = nullptr;
                for (const CompInfo& t : info) {
                    if (t.id == bestTo) {
                        toInfo = &t;
                        break;
                    }
                }
                if (toInfo == nullptr) {
                    continue;
                }

                const int strideA = std::max(1, static_cast<int>(from.edge.size()) / 48);
                const int strideB = std::max(1, static_cast<int>(toInfo->edge.size()) / 48);
                float bestEdgeD2 = std::numeric_limits<float>::max();
                Point pa{};
                Point pb{};
                bool foundPair = false;

                for (int ai = 0; ai < static_cast<int>(from.edge.size()); ai += strideA) {
                    const int aidx = from.edge[static_cast<std::size_t>(ai)];
                    const int ax = aidx % w;
                    const int ay = aidx / w;
                    for (int bi = 0; bi < static_cast<int>(toInfo->edge.size()); bi += strideB) {
                        const int bidx = toInfo->edge[static_cast<std::size_t>(bi)];
                        const int bx = bidx % w;
                        const int by = bidx / w;
                        const float dx0 = static_cast<float>(ax - bx);
                        const float dy0 = static_cast<float>(ay - by);
                        const float d2 = dx0 * dx0 + dy0 * dy0;
                        if (d2 < bestEdgeD2) {
                            bestEdgeD2 = d2;
                            pa = {ax, ay};
                            pb = {bx, by};
                            foundPair = true;
                        }
                    }
                }

                if (!foundPair || bestEdgeD2 > static_cast<float>(chainConnectDist * chainConnectDist)) {
                    usedPairs.insert(makePairKey(from.id, bestTo));
                    continue;
                }

                std::vector<int> line = drawLine(pa, pb);
                if (line.size() < 2 || line.size() > static_cast<std::size_t>(chainConnectDist * 2)) {
                    usedPairs.insert(makePairKey(from.id, bestTo));
                    continue;
                }

                int seaCross = 0;
                bool blocked = false;
                for (int li : line) {
                    const std::uint8_t b = outBiomes[static_cast<std::size_t>(li)];
                    if (b == SEA) {
                        ++seaCross;
                        if (seaCross > chainMaxSeaCross) {
                            blocked = true;
                            break;
                        }
                    }
                    if (b == RIVER || b == LAKE || b == BRIDGE) {
                        blocked = true;
                        break;
                    }
                }
                if (blocked) {
                    usedPairs.insert(makePairKey(from.id, bestTo));
                    continue;
                }

                for (int li : line) {
                    std::uint8_t& b = outBiomes[static_cast<std::size_t>(li)];
                    if (b == SEA || b == RIVER || b == LAKE || b == BRIDGE) {
                        continue;
                    }
                    const float hv = height[static_cast<std::size_t>(li)];
                    if (hv > 0.85f) {
                        b = static_cast<std::uint8_t>(EXMOUNTAIN);
                    } else {
                        b = static_cast<std::uint8_t>(MOUNTAIN);
                    }
                }

                usedPairs.insert(makePairKey(from.id, bestTo));
                ++connected;
            }
        }

        const int ridgeGapFillPasses = static_cast<int>(std::clamp(GetParam("mountainRidgeGapFillPasses", 2.0), 0.0, 6.0));
        const int ridgeGap = static_cast<int>(std::clamp(GetParam("mountainRidgeGap", 2.0), 1.0, 6.0));
        if (ridgeGapFillPasses > 0) {
            const std::array<Point, 4> dirs = {{{1, 0}, {0, 1}, {1, 1}, {1, -1}}};

            for (int pass = 0; pass < ridgeGapFillPasses; ++pass) {
                std::vector<int> fill;
                fill.reserve(static_cast<std::size_t>(n / 64));

                for (int y = 1; y < h - 1; ++y) {
                    for (int x = 1; x < w - 1; ++x) {
                        const int i = Index(x, y, w);
                        const std::uint8_t cur = outBiomes[static_cast<std::size_t>(i)];
                        if (IsMountainLike(cur) || IsWater(cur) || cur == ROAD || cur == BRIDGE) {
                            continue;
                        }

                        bool join = false;
                        for (const Point& d : dirs) {
                            bool hasA = false;
                            bool hasB = false;
                            bool blocked = false;

                            for (int step = 1; step <= ridgeGap + 1; ++step) {
                                const int ax = x + d.x * step;
                                const int ay = y + d.y * step;
                                if (!InBounds(ax, ay, w, h)) {
                                    break;
                                }
                                const std::uint8_t b = outBiomes[static_cast<std::size_t>(Index(ax, ay, w))];
                                if (IsWater(b) || b == ROAD || b == BRIDGE) {
                                    blocked = true;
                                    break;
                                }
                                if (IsMountainLike(b)) {
                                    hasA = true;
                                    break;
                                }
                            }
                            if (blocked || !hasA) {
                                continue;
                            }

                            for (int step = 1; step <= ridgeGap + 1; ++step) {
                                const int bx = x - d.x * step;
                                const int by = y - d.y * step;
                                if (!InBounds(bx, by, w, h)) {
                                    break;
                                }
                                const std::uint8_t b = outBiomes[static_cast<std::size_t>(Index(bx, by, w))];
                                if (IsWater(b) || b == ROAD || b == BRIDGE) {
                                    blocked = true;
                                    break;
                                }
                                if (IsMountainLike(b)) {
                                    hasB = true;
                                    break;
                                }
                            }

                            if (!blocked && hasA && hasB) {
                                join = true;
                                break;
                            }
                        }

                        if (join) {
                            fill.push_back(i);
                        }
                    }
                }

                for (int i : fill) {
                    outBiomes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((height[static_cast<std::size_t>(i)] > 0.86f) ? EXMOUNTAIN : MOUNTAIN);
                }
            }
        }
    }

    const std::vector<int> mountainDist = MultiSourceDistance(outBiomes, w, h, {MOUNTAIN, EXMOUNTAIN});

    if (mode == MacroMode::Basin) {
        // Increase land probability around the mountain/valley edge belt while keeping natural contours.
        const float surroundInner = static_cast<float>(std::clamp(GetParam("basinSurroundInner", 0.76), 0.58, 0.92));
        const int nearMountain = std::max(6, std::min(w, h) / 28);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int i = Index(x, y, w);
                if (outBiomes[static_cast<std::size_t>(i)] != SEA) {
                    continue;
                }

                const float nx = static_cast<float>(x) / static_cast<float>(std::max(1, w - 1));
                const float ny = static_cast<float>(y) / static_cast<float>(std::max(1, h - 1));
                const float r = BasinRoundMetric(nx, ny);
                const float edgeN = Clamp01((r - surroundInner) / std::max(0.02f, 1.0f - surroundInner));
                if (edgeN <= 0.0f) {
                    continue;
                }

                const int md = mountainDist[static_cast<std::size_t>(i)];
                if (md > nearMountain) {
                    continue;
                }
                const float n0 = mapgen::Fbm2D(nx * 4.9f * scale, ny * 4.9f * scale, 3, 2.0f, 0.5f, params_.seed ^ 0x4b90ce17U);
                const float proximity = 1.0f - Clamp01(static_cast<float>(md) / static_cast<float>(std::max(1, nearMountain)));
                const float landScore = 0.52f * proximity + 0.24f * edgeN + 0.24f * n0;

                if (landScore > 0.60f) {
                    outBiomes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((landScore > 0.82f) ? MOUNTAIN : PLAIN);
                }
            }
        }
    }

    std::vector<int> highCells;
    highCells.reserve(static_cast<std::size_t>(n / 4));
    for (int i = 0; i < n; ++i) {
        if (height[static_cast<std::size_t>(i)] > 0.68f && !IsWater(outBiomes[static_cast<std::size_t>(i)])) {
            highCells.push_back(i);
        }
    }
    std::sort(highCells.begin(), highCells.end(), [&](int a, int b) {
        return height[static_cast<std::size_t>(a)] > height[static_cast<std::size_t>(b)];
    });

    std::uniform_int_distribution<int> riverCountDist(riverMin, riverMax);
    const int riverCount = std::min(static_cast<int>(highCells.size()), riverCountDist(rng));
    std::vector<Point> riverSources;
    const int minSourceDist = std::max(8, std::min(w, h) / 10);

    for (int idx : highCells) {
        const Point p{idx % w, idx / w};
        bool farEnough = true;
        for (const Point& s : riverSources) {
            if (std::abs(s.x - p.x) + std::abs(s.y - p.y) < minSourceDist) {
                farEnough = false;
                break;
            }
        }
        if (!farEnough) {
            continue;
        }
        riverSources.push_back(p);
        if (static_cast<int>(riverSources.size()) >= riverCount) {
            break;
        }
    }

    const std::array<int, 8> rdx = {1, -1, 0, 0, 1, 1, -1, -1};
    const std::array<int, 8> rdy = {0, 0, 1, -1, 1, -1, 1, -1};

    for (const Point& src : riverSources) {
        Point cur = src;
        std::vector<std::uint8_t> visited(static_cast<std::size_t>(n), 0);
        const int maxSteps = (w + h) * 2;

        for (int step = 0; step < maxSteps; ++step) {
            const int ci = Index(cur.x, cur.y, w);
            visited[static_cast<std::size_t>(ci)] = 1;

            if (!IsWater(outBiomes[static_cast<std::size_t>(ci)])) {
                outBiomes[static_cast<std::size_t>(ci)] = RIVER;
            }

            if (outBiomes[static_cast<std::size_t>(ci)] == SEA || height[static_cast<std::size_t>(ci)] < seaLevel) {
                outBiomes[static_cast<std::size_t>(ci)] = SEA;
                break;
            }

            float bestScore = std::numeric_limits<float>::max();
            Point best = cur;

            for (int k = 0; k < 8; ++k) {
                const int nx = cur.x + rdx[static_cast<std::size_t>(k)];
                const int ny = cur.y + rdy[static_cast<std::size_t>(k)];
                if (!InBounds(nx, ny, w, h)) {
                    continue;
                }
                const int ni = Index(nx, ny, w);
                if (visited[static_cast<std::size_t>(ni)] != 0) {
                    continue;
                }

                float s = height[static_cast<std::size_t>(ni)];
                const float uphillPenalty = std::max(0.0f, height[static_cast<std::size_t>(ni)] - height[static_cast<std::size_t>(ci)]);
                s += uphillPenalty * 0.8f;
                s += ur(rng) * 0.04f;

                if (s < bestScore) {
                    bestScore = s;
                    best = {nx, ny};
                }
            }

            if (best.x == cur.x && best.y == cur.y) {
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int lx = cur.x + ox;
                        const int ly = cur.y + oy;
                        if (!InBounds(lx, ly, w, h)) {
                            continue;
                        }
                        const int li = Index(lx, ly, w);
                        if (!IsMountainLike(outBiomes[static_cast<std::size_t>(li)]) && outBiomes[static_cast<std::size_t>(li)] != SEA) {
                            outBiomes[static_cast<std::size_t>(li)] = LAKE;
                        }
                    }
                }
                break;
            }

            cur = best;
        }
    }


    {
        const int lakeTargetBase = static_cast<int>(std::clamp(GetParam("lakeCount", static_cast<double>(std::max(1, riverCount))), 0.0, 8.0));
        const int lakeTarget = static_cast<int>(std::clamp(
            std::round(static_cast<double>(lakeTargetBase) * static_cast<double>(riverDensity)),
            0.0,
            20.0));
        const int lakeMaxArea = static_cast<int>(std::clamp(GetParam("lakeMaxArea", static_cast<double>(std::max(24, (w * h) / 1800))), 8.0, 240.0));

        if (lakeTarget > 0) {
            const std::vector<int> preLakeWaterDist = MultiSourceDistance(outBiomes, w, h, {SEA, RIVER, LAKE});

            std::vector<int> basinCandidates;
            basinCandidates.reserve(static_cast<std::size_t>(n / 8));
            for (int i = 0; i < n; ++i) {
                if (IsWater(outBiomes[static_cast<std::size_t>(i)]) || IsMountainLike(outBiomes[static_cast<std::size_t>(i)])) {
                    continue;
                }
                const float h0 = height[static_cast<std::size_t>(i)];
                if (h0 <= seaLevel + 0.015f || h0 >= seaLevel + 0.16f) {
                    continue;
                }

                const int wd = preLakeWaterDist[static_cast<std::size_t>(i)];
                const int md = mountainDist[static_cast<std::size_t>(i)];
                if (wd < 5 || wd > std::max(26, std::min(w, h) / 4)) {
                    continue;
                }
                if (md < 2 || md > std::max(14, std::min(w, h) / 5)) {
                    continue;
                }

                const int x = i % w;
                const int y = i / w;
                int lowerN = 0;
                int higherN = 0;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (ox == 0 && oy == 0) {
                            continue;
                        }
                        const int nx = x + ox;
                        const int ny = y + oy;
                        if (!InBounds(nx, ny, w, h)) {
                            continue;
                        }
                        const float hn = height[static_cast<std::size_t>(Index(nx, ny, w))];
                        if (hn <= h0 + 0.006f) {
                            ++lowerN;
                        }
                        if (hn >= h0 + 0.018f) {
                            ++higherN;
                        }
                    }
                }
                if (lowerN <= 2 && higherN >= 4) {
                    basinCandidates.push_back(i);
                }
            }

            std::sort(basinCandidates.begin(), basinCandidates.end(), [&](int a, int b) {
                return height[static_cast<std::size_t>(a)] < height[static_cast<std::size_t>(b)];
            });

            std::vector<int> chosenSeeds;
            const int minLakeSeedDist = std::max(12, std::min(w, h) / 9);
            for (int idx : basinCandidates) {
                const Point p{idx % w, idx / w};
                bool farEnough = true;
                for (int c : chosenSeeds) {
                    const Point q{c % w, c / w};
                    if (std::abs(p.x - q.x) + std::abs(p.y - q.y) < minLakeSeedDist) {
                        farEnough = false;
                        break;
                    }
                }
                if (!farEnough) {
                    continue;
                }
                chosenSeeds.push_back(idx);
                if (static_cast<int>(chosenSeeds.size()) >= lakeTarget) {
                    break;
                }
            }

            for (int seed : chosenSeeds) {
                const float seedH = height[static_cast<std::size_t>(seed)];
                const float lakeCutoff = seedH + 0.030f + ur(rng) * 0.020f;

                std::queue<int> q;
                std::vector<std::uint8_t> mark(static_cast<std::size_t>(n), 0);
                std::vector<int> cells;
                q.push(seed);
                mark[static_cast<std::size_t>(seed)] = 1;

                while (!q.empty() && static_cast<int>(cells.size()) < lakeMaxArea) {
                    const int cur = q.front();
                    q.pop();

                    if (IsMountainLike(outBiomes[static_cast<std::size_t>(cur)]) || outBiomes[static_cast<std::size_t>(cur)] == SEA) {
                        continue;
                    }
                    if (height[static_cast<std::size_t>(cur)] > lakeCutoff) {
                        continue;
                    }

                    cells.push_back(cur);
                    const int x = cur % w;
                    const int y = cur / w;
                    const std::array<int, 4> ldx = {1, -1, 0, 0};
                    const std::array<int, 4> ldy = {0, 0, 1, -1};
                    for (int k = 0; k < 4; ++k) {
                        const int nx = x + ldx[static_cast<std::size_t>(k)];
                        const int ny = y + ldy[static_cast<std::size_t>(k)];
                        if (!InBounds(nx, ny, w, h)) {
                            continue;
                        }
                        const int ni = Index(nx, ny, w);
                        if (mark[static_cast<std::size_t>(ni)] != 0) {
                            continue;
                        }
                        mark[static_cast<std::size_t>(ni)] = 1;
                        if (height[static_cast<std::size_t>(ni)] <= lakeCutoff + 0.010f) {
                            q.push(ni);
                        }
                    }
                }

                if (static_cast<int>(cells.size()) < 6) {
                    continue;
                }

                for (int c : cells) {
                    outBiomes[static_cast<std::size_t>(c)] = LAKE;
                }

                const std::vector<int> afterLakeDist = MultiSourceDistance(outBiomes, w, h, {SEA, RIVER, LAKE});
                int outletStart = -1;
                int outletScore = std::numeric_limits<int>::max();
                for (int c : cells) {
                    const int x = c % w;
                    const int y = c / w;
                    const std::array<int, 4> ldx = {1, -1, 0, 0};
                    const std::array<int, 4> ldy = {0, 0, 1, -1};
                    for (int k = 0; k < 4; ++k) {
                        const int nx = x + ldx[static_cast<std::size_t>(k)];
                        const int ny = y + ldy[static_cast<std::size_t>(k)];
                        if (!InBounds(nx, ny, w, h)) {
                            continue;
                        }
                        const int ni = Index(nx, ny, w);
                        if (outBiomes[static_cast<std::size_t>(ni)] == LAKE) {
                            continue;
                        }
                        const int score = afterLakeDist[static_cast<std::size_t>(ni)] + static_cast<int>(height[static_cast<std::size_t>(ni)] * 100.0f);
                        if (score < outletScore) {
                            outletScore = score;
                            outletStart = ni;
                        }
                    }
                }

                if (outletStart >= 0) {
                    Point cur{outletStart % w, outletStart / w};
                    std::vector<std::uint8_t> visited(static_cast<std::size_t>(n), 0);
                    const int outletMax = std::max(12, std::min(w, h) / 3);
                    for (int step = 0; step < outletMax; ++step) {
                        const int ci = Index(cur.x, cur.y, w);
                        if (visited[static_cast<std::size_t>(ci)] != 0) {
                            break;
                        }
                        visited[static_cast<std::size_t>(ci)] = 1;

                        if (outBiomes[static_cast<std::size_t>(ci)] == SEA || outBiomes[static_cast<std::size_t>(ci)] == RIVER) {
                            break;
                        }
                        if (!IsMountainLike(outBiomes[static_cast<std::size_t>(ci)])) {
                            outBiomes[static_cast<std::size_t>(ci)] = RIVER;
                        }

                        float bestScore = std::numeric_limits<float>::max();
                        Point best = cur;
                        for (int k = 0; k < 8; ++k) {
                            const int nx = cur.x + rdx[static_cast<std::size_t>(k)];
                            const int ny = cur.y + rdy[static_cast<std::size_t>(k)];
                            if (!InBounds(nx, ny, w, h)) {
                                continue;
                            }
                            const int ni = Index(nx, ny, w);
                            if (visited[static_cast<std::size_t>(ni)] != 0 || IsMountainLike(outBiomes[static_cast<std::size_t>(ni)])) {
                                continue;
                            }

                            const float s = height[static_cast<std::size_t>(ni)] * 0.65f +
                                static_cast<float>(afterLakeDist[static_cast<std::size_t>(ni)]) / static_cast<float>(std::max(1, std::max(w, h))) * 0.30f +
                                ur(rng) * 0.03f;
                            if (s < bestScore) {
                                bestScore = s;
                                best = {nx, ny};
                            }
                        }

                        if (best.x == cur.x && best.y == cur.y) {
                            break;
                        }
                        cur = best;
                    }
                }
            }
        }
    }
    {
        const int tributaryTargetBase = static_cast<int>(std::clamp(GetParam("tributaryCount", static_cast<double>(std::max(4, riverCount * 3))), 0.0, 30.0));
        const int tributaryTarget = static_cast<int>(std::clamp(
            std::round(static_cast<double>(tributaryTargetBase) * static_cast<double>(riverDensity)),
            0.0,
            60.0));
        if (tributaryTarget > 0) {
            std::vector<int> sourceCandidates;
            sourceCandidates.reserve(static_cast<std::size_t>(n / 8));

            const std::vector<int> baseWaterDist = MultiSourceDistance(outBiomes, w, h, {SEA, RIVER, LAKE});
            for (int i = 0; i < n; ++i) {
                if (IsWater(outBiomes[static_cast<std::size_t>(i)]) || IsMountainLike(outBiomes[static_cast<std::size_t>(i)])) {
                    continue;
                }
                const int wd = baseWaterDist[static_cast<std::size_t>(i)];
                const int md = mountainDist[static_cast<std::size_t>(i)];
                if (wd >= 8 && wd <= std::max(24, std::min(w, h) / 6) && md >= 2 && md <= 9 && height[static_cast<std::size_t>(i)] >= 0.58f) {
                    sourceCandidates.push_back(i);
                }
            }

            std::shuffle(sourceCandidates.begin(), sourceCandidates.end(), rng);

            std::vector<Point> tributarySources;
            const int tributaryMinSourceDist = std::max(7, std::min(w, h) / 14);
            for (int idx : sourceCandidates) {
                const Point p{idx % w, idx / w};
                bool farEnough = true;
                for (const Point& s : tributarySources) {
                    if (std::abs(s.x - p.x) + std::abs(s.y - p.y) < tributaryMinSourceDist) {
                        farEnough = false;
                        break;
                    }
                }
                if (!farEnough) {
                    continue;
                }
                tributarySources.push_back(p);
                if (static_cast<int>(tributarySources.size()) >= tributaryTarget) {
                    break;
                }
            }

            for (const Point& src : tributarySources) {
                Point cur = src;
                std::vector<std::uint8_t> visited(static_cast<std::size_t>(n), 0);
                const int maxSteps = w + h;
                std::vector<int> waterDistDyn = MultiSourceDistance(outBiomes, w, h, {SEA, RIVER, LAKE});

                for (int step = 0; step < maxSteps; ++step) {
                    const int ci = Index(cur.x, cur.y, w);
                    visited[static_cast<std::size_t>(ci)] = 1;

                    if (!IsWater(outBiomes[static_cast<std::size_t>(ci)])) {
                        outBiomes[static_cast<std::size_t>(ci)] = RIVER;
                    }

                    if (waterDistDyn[static_cast<std::size_t>(ci)] <= 1 && step >= 2) {
                        break;
                    }

                    float bestScore = std::numeric_limits<float>::max();
                    Point best = cur;
                    bool foundLower = false;

                    for (int k = 0; k < 8; ++k) {
                        const int nx = cur.x + rdx[static_cast<std::size_t>(k)];
                        const int ny = cur.y + rdy[static_cast<std::size_t>(k)];
                        if (!InBounds(nx, ny, w, h)) {
                            continue;
                        }
                        const int ni = Index(nx, ny, w);
                        if (visited[static_cast<std::size_t>(ni)] != 0 || IsMountainLike(outBiomes[static_cast<std::size_t>(ni)])) {
                            continue;
                        }

                        const float hCur = height[static_cast<std::size_t>(ci)];
                        const float hNext = height[static_cast<std::size_t>(ni)];
                        const float uphillPenalty = std::max(0.0f, hNext - hCur);
                        const float towardWater = static_cast<float>(waterDistDyn[static_cast<std::size_t>(ni)]) / static_cast<float>(std::max(1, std::max(w, h)));
                        const float score = hNext * 0.58f + towardWater * 0.34f + uphillPenalty * 0.75f + ur(rng) * 0.02f;

                        if (hNext <= hCur + 0.01f) {
                            foundLower = true;
                        }
                        if (score < bestScore) {
                            bestScore = score;
                            best = {nx, ny};
                        }
                    }

                    if (best.x == cur.x && best.y == cur.y) {
                        break;
                    }

                    if (!foundLower && step > 6) {
                        break;
                    }

                    cur = best;
                }
            }
        }
    }

    const std::vector<int> waterDist = MultiSourceDistance(outBiomes, w, h, {SEA, RIVER, LAKE});
    const std::vector<int> seaDist = MultiSourceDistance(outBiomes, w, h, {SEA});

    std::vector<float> moisture(static_cast<std::size_t>(n), 0.0f);
    const float maxD = static_cast<float>(std::max(w, h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int i = Index(x, y, w);
            const float dWater = std::min(maxD, static_cast<float>(waterDist[static_cast<std::size_t>(i)]));
            const float dSea = std::min(maxD, static_cast<float>(seaDist[static_cast<std::size_t>(i)]));
            const float wetByRiverOrSea = 1.0f - Clamp01(dWater / (maxD * 0.32f));
            const float inlandDryness = Clamp01(dSea / (maxD * 0.28f));

            const float nx = static_cast<float>(x) / static_cast<float>(std::max(1, w - 1));
            const float ny = static_cast<float>(y) / static_cast<float>(std::max(1, h - 1));
            const float humidNoise = mapgen::Fbm2D(nx * 5.2f * scale, ny * 5.2f * scale, 4, 2.1f, 0.55f, params_.seed ^ 0x55cc21a9U);
            const float m = wetByRiverOrSea * 0.62f + humidNoise * 0.18f + (1.0f - inlandDryness) * 0.20f;
            moisture[static_cast<std::size_t>(i)] = Clamp01(m);
        }
    }

    const float desertCov = static_cast<float>(std::clamp(GetParam("desertCoverage", 0.12), 0.02, 0.4));
    const float forestCov = static_cast<float>(std::clamp(GetParam("forestCoverage", 0.25), 0.05, 0.6));

    for (int i = 0; i < n; ++i) {
        const std::uint8_t b = outBiomes[static_cast<std::size_t>(i)];
        if (IsWater(b) || IsMountainLike(b)) {
            continue;
        }

        const float hv = height[static_cast<std::size_t>(i)];
        const float m = moisture[static_cast<std::size_t>(i)];
        const float dryness = 1.0f - m;

        const float inland = Clamp01(static_cast<float>(seaDist[static_cast<std::size_t>(i)]) / (maxD * 0.24f));
        const float coastProximity = 1.0f - Clamp01(static_cast<float>(seaDist[static_cast<std::size_t>(i)]) / (maxD * 0.22f));
        const float mountainProximity = 1.0f - Clamp01(static_cast<float>(mountainDist[static_cast<std::size_t>(i)]) / (maxD * 0.18f));
        const float adjustedDryness = Clamp01(dryness + inland * 0.20f);

        if (adjustedDryness > (0.57f - desertCov * 0.18f) && hv < 0.74f) {
            outBiomes[static_cast<std::size_t>(i)] = DESERT;
        } else {
            const float forestScore = mountainProximity * 0.52f + inland * 0.20f + m * 0.18f + hv * 0.10f;
            const float plainScore = coastProximity * 0.45f + (1.0f - mountainProximity) * 0.30f + m * 0.15f + (1.0f - hv) * 0.10f;

            if (forestScore > plainScore + (0.06f - forestCov * 0.05f)) {
                outBiomes[static_cast<std::size_t>(i)] = FOREST;
            } else {
                outBiomes[static_cast<std::size_t>(i)] = PLAIN;
            }
        }
    }

    for (int pass = 0; pass < 2; ++pass) {
        std::vector<std::uint8_t> next = outBiomes;
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                const int i = Index(x, y, w);
                if (IsWater(outBiomes[static_cast<std::size_t>(i)]) || IsMountainLike(outBiomes[static_cast<std::size_t>(i)])) {
                    continue;
                }
                int forestN = 0;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (ox == 0 && oy == 0) {
                            continue;
                        }
                        const int ni = Index(x + ox, y + oy, w);
                        if (outBiomes[static_cast<std::size_t>(ni)] == FOREST) {
                            ++forestN;
                        }
                    }
                }

                if (outBiomes[static_cast<std::size_t>(i)] == FOREST && forestN <= 2 && seaDist[static_cast<std::size_t>(i)] <= std::max(12, std::min(w, h) / 6)) {
                    next[static_cast<std::size_t>(i)] = PLAIN;
                } else if (outBiomes[static_cast<std::size_t>(i)] == PLAIN && forestN >= 7 && moisture[static_cast<std::size_t>(i)] > 0.62f && mountainDist[static_cast<std::size_t>(i)] <= std::max(14, std::min(w, h) / 6)) {
                    next[static_cast<std::size_t>(i)] = FOREST;
                }
            }
        }
        outBiomes.swap(next);
    }

    {
        const float plainCoverageMin = static_cast<float>(std::clamp(GetParam("plainCoverageMin", 0.20), 0.05, 0.6));
        const float plainCoverageMax = static_cast<float>(std::clamp(GetParam("plainCoverageMax", 0.45), plainCoverageMin + 0.02, 0.9));

        auto isSoftLand = [](std::uint8_t b) {
            return b == PLAIN || b == DESERT || b == FOREST;
        };

        auto softLandCount = [&]() {
            int c = 0;
            for (std::uint8_t b : outBiomes) {
                if (isSoftLand(b)) {
                    ++c;
                }
            }
            return c;
        };

        const int softCount = std::max(1, softLandCount());

        for (int pass = 0; pass < 3; ++pass) {
            int plainCount = 0;
            for (std::uint8_t b : outBiomes) {
                if (b == PLAIN) {
                    ++plainCount;
                }
            }
            const float plainRatio = static_cast<float>(plainCount) / static_cast<float>(softCount);

            if (plainRatio < plainCoverageMin) {
                std::vector<int> candidates;
                candidates.reserve(static_cast<std::size_t>(n / 5));
                for (int i = 0; i < n; ++i) {
                    const std::uint8_t b = outBiomes[static_cast<std::size_t>(i)];
                    if (b != FOREST && b != DESERT) {
                        continue;
                    }
                    if (waterDist[static_cast<std::size_t>(i)] <= 1) {
                        continue;
                    }
                    if (b == FOREST && moisture[static_cast<std::size_t>(i)] > 0.82f) {
                        continue;
                    }
                    candidates.push_back(i);
                }
                std::shuffle(candidates.begin(), candidates.end(), rng);

                const int need = static_cast<int>(std::ceil((plainCoverageMin - plainRatio) * static_cast<float>(softCount)));
                int changed = 0;
                for (int i : candidates) {
                    outBiomes[static_cast<std::size_t>(i)] = PLAIN;
                    ++changed;
                    if (changed >= need) {
                        break;
                    }
                }
            } else if (plainRatio > plainCoverageMax) {
                std::vector<int> candidates;
                candidates.reserve(static_cast<std::size_t>(n / 5));
                for (int i = 0; i < n; ++i) {
                    if (outBiomes[static_cast<std::size_t>(i)] == PLAIN && waterDist[static_cast<std::size_t>(i)] >= 2) {
                        candidates.push_back(i);
                    }
                }
                std::shuffle(candidates.begin(), candidates.end(), rng);

                const int need = static_cast<int>(std::ceil((plainRatio - plainCoverageMax) * static_cast<float>(softCount)));
                int changed = 0;
                for (int i : candidates) {
                    const float m = moisture[static_cast<std::size_t>(i)];
                    const float d = 1.0f - m;
                    outBiomes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((d > 0.56f) ? DESERT : FOREST);
                    ++changed;
                    if (changed >= need) {
                        break;
                    }
                }
            } else {
                break;
            }
        }
    }

    {
        const int smoothPasses = static_cast<int>(std::clamp(GetParam("biomeSmoothPasses", 2.0), 0.0, 6.0));
        const int minPatchSize = static_cast<int>(std::clamp(GetParam("biomeMinPatchSize", 14.0), 0.0, 256.0));

        auto isLandBiome = [](std::uint8_t b) {
            return b == PLAIN || b == DESERT || b == FOREST || b == MOUNTAIN || b == EXMOUNTAIN;
        };

        auto pickDominantLand = [&](const std::array<int, 10>& cnt, std::uint8_t cur) {
            const std::array<std::uint8_t, 5> kinds = {PLAIN, DESERT, FOREST, MOUNTAIN, EXMOUNTAIN};
            int bestCount = -1;
            std::uint8_t best = cur;
            for (std::uint8_t k : kinds) {
                const int c = cnt[static_cast<std::size_t>(k)];
                if (c > bestCount) {
                    bestCount = c;
                    best = k;
                }
            }
            return best;
        };

        for (int pass = 0; pass < smoothPasses; ++pass) {
            std::vector<std::uint8_t> next = outBiomes;
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    const int i = Index(x, y, w);
                    const std::uint8_t cur = outBiomes[static_cast<std::size_t>(i)];
                    if (!isLandBiome(cur)) {
                        continue;
                    }

                    std::array<int, 10> cnt{};
                    for (int oy = -1; oy <= 1; ++oy) {
                        for (int ox = -1; ox <= 1; ++ox) {
                            const int ni = Index(x + ox, y + oy, w);
                            const std::uint8_t nb = outBiomes[static_cast<std::size_t>(ni)];
                            if (isLandBiome(nb)) {
                                ++cnt[static_cast<std::size_t>(nb)];
                            }
                        }
                    }

                    const std::uint8_t dominant = pickDominantLand(cnt, cur);
                    const int curCnt = cnt[static_cast<std::size_t>(cur)];
                    const int domCnt = cnt[static_cast<std::size_t>(dominant)];

                    if (dominant != cur && domCnt >= 5 && domCnt >= curCnt + 2) {
                        if (cur == MOUNTAIN || cur == EXMOUNTAIN || dominant == MOUNTAIN || dominant == EXMOUNTAIN) {
                            if (domCnt >= 6) {
                                next[static_cast<std::size_t>(i)] = dominant;
                            }
                        } else {
                            next[static_cast<std::size_t>(i)] = dominant;
                        }
                    }
                }
            }
            outBiomes.swap(next);
        }

        if (minPatchSize > 0) {
            std::vector<std::uint8_t> visited(static_cast<std::size_t>(n), 0);
            const std::array<int, 4> dx = {1, -1, 0, 0};
            const std::array<int, 4> dy = {0, 0, 1, -1};

            auto remapSmallPatch = [&](const std::vector<int>& comp, std::uint8_t biome) {
                std::array<int, 10> neighborCnt{};
                for (int ci : comp) {
                    const int x = ci % w;
                    const int y = ci / w;
                    for (int k = 0; k < 4; ++k) {
                        const int nx = x + dx[static_cast<std::size_t>(k)];
                        const int ny = y + dy[static_cast<std::size_t>(k)];
                        if (!InBounds(nx, ny, w, h)) {
                            continue;
                        }
                        const int ni = Index(nx, ny, w);
                        const std::uint8_t nb = outBiomes[static_cast<std::size_t>(ni)];
                        if (nb != biome && isLandBiome(nb)) {
                            ++neighborCnt[static_cast<std::size_t>(nb)];
                        }
                    }
                }

                std::uint8_t to = PLAIN;
                int best = -1;
                const std::array<std::uint8_t, 5> kinds = {PLAIN, DESERT, FOREST, MOUNTAIN, EXMOUNTAIN};
                for (std::uint8_t k : kinds) {
                    if (neighborCnt[static_cast<std::size_t>(k)] > best) {
                        best = neighborCnt[static_cast<std::size_t>(k)];
                        to = k;
                    }
                }

                if (best <= 0) {
                    to = static_cast<std::uint8_t>(PLAIN);
                }

                for (int ci : comp) {
                    outBiomes[static_cast<std::size_t>(ci)] = to;
                }
            };

            for (int i = 0; i < n; ++i) {
                if (visited[static_cast<std::size_t>(i)] != 0) {
                    continue;
                }
                const std::uint8_t biome = outBiomes[static_cast<std::size_t>(i)];
                if (!isLandBiome(biome)) {
                    visited[static_cast<std::size_t>(i)] = 1;
                    continue;
                }

                std::queue<int> q;
                std::vector<int> comp;
                q.push(i);
                visited[static_cast<std::size_t>(i)] = 1;

                while (!q.empty()) {
                    const int cur = q.front();
                    q.pop();
                    comp.push_back(cur);

                    const int x = cur % w;
                    const int y = cur / w;
                    for (int k = 0; k < 4; ++k) {
                        const int nx = x + dx[static_cast<std::size_t>(k)];
                        const int ny = y + dy[static_cast<std::size_t>(k)];
                        if (!InBounds(nx, ny, w, h)) {
                            continue;
                        }
                        const int ni = Index(nx, ny, w);
                        if (visited[static_cast<std::size_t>(ni)] != 0) {
                            continue;
                        }
                        if (outBiomes[static_cast<std::size_t>(ni)] != biome) {
                            continue;
                        }
                        visited[static_cast<std::size_t>(ni)] = 1;
                        q.push(ni);
                    }
                }

                int threshold = minPatchSize;
                if (biome == MOUNTAIN || biome == EXMOUNTAIN) {
                    threshold = std::max(3, minPatchSize / 2);
                }
                if (biome == PLAIN) {
                    threshold = std::max(4, minPatchSize / 3);
                }

                if (static_cast<int>(comp.size()) < threshold) {
                    remapSmallPatch(comp, biome);
                }
            }
        }
    }

    {
        const float forestSpeckleRate = static_cast<float>(std::clamp(GetParam("forestSpeckleRate", 0.008), 0.0, 0.08));
        const float forestSpeckleClusterRate = static_cast<float>(std::clamp(GetParam("forestSpeckleClusterRate", 0.22), 0.0, 1.0));

        if (forestSpeckleRate > 0.0f) {
            const float minSeaForSpeckle = static_cast<float>(std::max(4, std::min(w, h) / 18));

            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    const int i = Index(x, y, w);
                    if (outBiomes[static_cast<std::size_t>(i)] != PLAIN) {
                        continue;
                    }
                    if (static_cast<float>(seaDist[static_cast<std::size_t>(i)]) < minSeaForSpeckle) {
                        continue;
                    }

                    const float n0 = mapgen::HashNoise2D(x, y, params_.seed ^ 0x4f13a91bU);
                    if (n0 > forestSpeckleRate) {
                        continue;
                    }

                    outBiomes[static_cast<std::size_t>(i)] = FOREST;

                    if (ur(rng) < forestSpeckleClusterRate) {
                        const int dir = static_cast<int>(ur(rng) * 4.0f) % 4;
                        static const std::array<int, 4> dx = {1, -1, 0, 0};
                        static const std::array<int, 4> dy = {0, 0, 1, -1};
                        const int nx = x + dx[static_cast<std::size_t>(dir)];
                        const int ny = y + dy[static_cast<std::size_t>(dir)];
                        if (InBounds(nx, ny, w, h)) {
                            const int ni = Index(nx, ny, w);
                            if (outBiomes[static_cast<std::size_t>(ni)] == PLAIN && seaDist[static_cast<std::size_t>(ni)] >= static_cast<int>(minSeaForSpeckle)) {
                                outBiomes[static_cast<std::size_t>(ni)] = FOREST;
                            }
                        }
                    }
                }
            }
        }
    }

    std::vector<Point> coastCandidates;
    std::vector<Point> inlandCandidates;
    std::vector<Point> frontierCandidates;
    std::vector<Point> allPassableCandidates;

    coastCandidates.reserve(static_cast<std::size_t>(n / 12));
    inlandCandidates.reserve(static_cast<std::size_t>(n / 12));
    frontierCandidates.reserve(static_cast<std::size_t>(n / 12));
    allPassableCandidates.reserve(static_cast<std::size_t>(n / 6));

    const int inlandMinSeaDist = std::max(6, std::min(w, h) / 12);
    const int inlandMaxSeaDist = std::max(12, std::min(w, h) / 3);

    for (int y = 3; y < h - 3; ++y) {
        for (int x = 3; x < w - 3; ++x) {
            const int i = Index(x, y, w);
            if (!IsPassableForPoi(outBiomes[static_cast<std::size_t>(i)])) {
                continue;
            }

            const int edgeDist = std::min({x, y, w - 1 - x, h - 1 - y});
            if (mode == MacroMode::Basin && edgeDist < std::max(12, std::min(w, h) / 9)) {
                continue;
            }

            const int coastD = seaDist[static_cast<std::size_t>(i)];
            const int mtnD = mountainDist[static_cast<std::size_t>(i)];
            const int riverD = waterDist[static_cast<std::size_t>(i)];

            const Point c{x, y};
            allPassableCandidates.push_back(c);

            if (coastD >= 2 && coastD <= 9) {
                coastCandidates.push_back(c);
            }
            if (coastD >= inlandMinSeaDist && coastD <= inlandMaxSeaDist && riverD <= std::max(10, std::min(w, h) / 8)) {
                inlandCandidates.push_back(c);
            }
            if (mtnD >= 2 && mtnD <= 7 && coastD > 4) {
                frontierCandidates.push_back(c);
            }
        }
    }

    const int poiTargetBase = std::clamp((w * h) / 8000, 8, 16);
    const int poiTarget = std::clamp(static_cast<int>(std::round(static_cast<float>(poiTargetBase) * roadDensity)), 6, 24);
    const int poiMinDist = std::max(10, std::min(w, h) / 8);
    const int coastTarget = std::max(2, poiTarget * 35 / 100);
    const int inlandTarget = std::max(2, poiTarget * 40 / 100);
    const int frontierTarget = std::max(1, poiTarget - coastTarget - inlandTarget);

    std::vector<Point> pois;
    auto tryPickFrom = [&](std::vector<Point>& pool, int target) {
        std::shuffle(pool.begin(), pool.end(), rng);
        int picked = 0;
        for (const Point& c : pool) {
            bool ok = true;
            for (const Point& p : pois) {
                if (std::abs(c.x - p.x) + std::abs(c.y - p.y) < poiMinDist) {
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                continue;
            }
            pois.push_back(c);
            ++picked;
            if (picked >= target || static_cast<int>(pois.size()) >= poiTarget) {
                break;
            }
        }
    };

    tryPickFrom(coastCandidates, coastTarget);
    tryPickFrom(inlandCandidates, inlandTarget);
    tryPickFrom(frontierCandidates, frontierTarget);

    if (static_cast<int>(pois.size()) < poiTarget) {
        tryPickFrom(allPassableCandidates, poiTarget - static_cast<int>(pois.size()));
    }

    if (pois.size() < 6) {
        tryPickFrom(allPassableCandidates, 6 - static_cast<int>(pois.size()));
    }
    for (int i = 0; i < static_cast<int>(pois.size()); ++i) {
        const Point& poi = pois[static_cast<std::size_t>(i)];
        outPlan.markers.push_back({i == 0 ? "capital" : "poi", poi.x, poi.y, i});
    }

    if (pois.size() >= 2) {
        std::vector<Edge> edges;
        for (int i = 0; i < static_cast<int>(pois.size()); ++i) {
            for (int j = i + 1; j < static_cast<int>(pois.size()); ++j) {
                const float dx = static_cast<float>(pois[static_cast<std::size_t>(i)].x - pois[static_cast<std::size_t>(j)].x);
                const float dy = static_cast<float>(pois[static_cast<std::size_t>(i)].y - pois[static_cast<std::size_t>(j)].y);
                const float d = std::sqrt(dx * dx + dy * dy);
                edges.push_back({i, j, d});
            }
        }

        std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
            return a.d < b.d;
        });

        std::vector<Edge> roadEdges;
        DisjointSet ds(static_cast<int>(pois.size()));
        for (const Edge& e : edges) {
            if (ds.Unite(e.a, e.b)) {
                roadEdges.push_back(e);
            }
        }

        const int extraCount = static_cast<int>(std::round(static_cast<float>(roadEdges.size()) * roadExtraEdgeRate));
        int added = 0;
        for (const Edge& e : edges) {
            bool already = false;
            for (const Edge& r : roadEdges) {
                if ((r.a == e.a && r.b == e.b) || (r.a == e.b && r.b == e.a)) {
                    already = true;
                    break;
                }
            }
            if (already) {
                continue;
            }
            roadEdges.push_back(e);
            ++added;
            if (added >= extraCount) {
                break;
            }
        }

        std::vector<Point> bridgeCenters;
        const int maxBridgeSpan = 5;
        const float roadDiagonalRate = static_cast<float>(std::clamp(GetParam("roadDiagonalRate", 0.08), 0.0, 0.5));

        auto collectWaterSegments = [&](const std::vector<Point>& path) {
            std::vector<std::pair<int, int>> segments;
            int segStart = -1;
            for (int pi = 0; pi < static_cast<int>(path.size()); ++pi) {
                const int idx = Index(path[static_cast<std::size_t>(pi)].x, path[static_cast<std::size_t>(pi)].y, w);
                const bool onWater = IsWater(outBiomes[static_cast<std::size_t>(idx)]);
                if (onWater && segStart < 0) {
                    segStart = pi;
                }
                if (!onWater && segStart >= 0) {
                    segments.push_back({segStart, pi - 1});
                    segStart = -1;
                }
            }
            if (segStart >= 0) {
                segments.push_back({segStart, static_cast<int>(path.size()) - 1});
            }
            return segments;
        };

        auto isValidBridgePattern = [&](const std::vector<Point>& path, const std::vector<std::pair<int, int>>& waterSegments) {
            if (waterSegments.empty() || waterSegments.size() > 3) {
                return false;
            }
            for (const auto& seg : waterSegments) {
                const int segLen = seg.second - seg.first + 1;
                if (seg.first <= 0 || seg.second + 1 >= static_cast<int>(path.size()) || segLen > maxBridgeSpan) {
                    return false;
                }

                const Point center = path[static_cast<std::size_t>((seg.first + seg.second) / 2)];
                for (const Point& bp : bridgeCenters) {
                    const int dist = std::abs(bp.x - center.x) + std::abs(bp.y - center.y);
                    if (dist < bridgeMinInterval) {
                        return false;
                    }
                }
            }
            return true;
        };

        auto makeRetroPath = [&](const std::vector<Point>& rawPath) {
            if (rawPath.size() <= 1) {
                return rawPath;
            }

            std::vector<Point> retro;
            retro.reserve(rawPath.size() * 2);
            retro.push_back(rawPath.front());

            auto bendScore = [&](const Point& p) {
                if (!InBounds(p.x, p.y, w, h)) {
                    return 1000000.0f;
                }
                const int idx = Index(p.x, p.y, w);
                const std::uint8_t b = outBiomes[static_cast<std::size_t>(idx)];
                if (IsMountainLike(b)) {
                    return 1000000.0f;
                }
                float score = 0.0f;
                if (b == SEA || b == RIVER || b == LAKE) {
                    score += 3.5f;
                } else if (b == FOREST) {
                    score += 0.7f;
                } else if (b == DESERT) {
                    score += 0.2f;
                }
                score += height[static_cast<std::size_t>(idx)] * 0.12f;
                return score;
            };

            for (std::size_t i = 1; i < rawPath.size(); ++i) {
                const Point prev = retro.back();
                const Point cur = rawPath[i];
                const int dx = cur.x - prev.x;
                const int dy = cur.y - prev.y;

                if (std::abs(dx) == 1 && std::abs(dy) == 1 && ur(rng) > roadDiagonalRate) {
                    const Point p1{cur.x, prev.y};
                    const Point p2{prev.x, cur.y};
                    const float s1 = bendScore(p1);
                    const float s2 = bendScore(p2);

                    if (s1 < 999999.0f || s2 < 999999.0f) {
                        const Point bend = (s1 <= s2) ? p1 : p2;
                        if (!(bend.x == prev.x && bend.y == prev.y) && !(bend.x == cur.x && bend.y == cur.y)) {
                            retro.push_back(bend);
                        }
                    }
                }

                retro.push_back(cur);
            }

            return retro;
        };

        auto shouldSkipOverParallelPath = [&](const std::vector<Point>& path, float minFreshRatio, int minFreshCells) {
            if (path.empty()) {
                return true;
            }

            int fresh = 0;
            int parallelFresh = 0;
            const std::array<int, 4> dx = {1, -1, 0, 0};
            const std::array<int, 4> dy = {0, 0, 1, -1};
            for (const Point& p : path) {
                const int i = Index(p.x, p.y, w);
                const std::uint8_t b = outBiomes[static_cast<std::size_t>(i)];
                if (IsRoadLikeBiome(b)) {
                    continue;
                }
                ++fresh;

                int adjRoad = 0;
                for (int k = 0; k < 4; ++k) {
                    const int nx = p.x + dx[static_cast<std::size_t>(k)];
                    const int ny = p.y + dy[static_cast<std::size_t>(k)];
                    if (!InBounds(nx, ny, w, h)) {
                        continue;
                    }
                    if (IsRoadLikeBiome(outBiomes[static_cast<std::size_t>(Index(nx, ny, w))])) {
                        ++adjRoad;
                    }
                }
                if (adjRoad >= 1) {
                    ++parallelFresh;
                }
            }

            if (fresh < minFreshCells) {
                return true;
            }
            const float freshRatio = static_cast<float>(fresh) / static_cast<float>(std::max(1, static_cast<int>(path.size())));
            if (freshRatio < minFreshRatio) {
                return true;
            }
            const float parallelRatio = static_cast<float>(parallelFresh) / static_cast<float>(std::max(1, fresh));
            return parallelRatio > 0.82f;
        };

        for (const Edge& e : roadEdges) {
            const Point s = pois[static_cast<std::size_t>(e.a)];
            const Point g = pois[static_cast<std::size_t>(e.b)];
            const std::vector<Point> dryPath = FindPathAStar(outBiomes, w, h, s, g, 1000000.0f, false);
            const std::vector<Point> wetPath = FindPathAStar(outBiomes, w, h, s, g, 8.5f, false);

            std::vector<Point> path = dryPath;
            bool usedWetFallback = false;

            if (!wetPath.empty()) {
                const std::vector<std::pair<int, int>> wetSegments = collectWaterSegments(wetPath);
                const bool wetValid = isValidBridgePattern(wetPath, wetSegments);
                if (path.empty()) {
                    if (wetValid) {
                        path = wetPath;
                        usedWetFallback = true;
                    }
                } else if (wetValid && wetPath.size() + 8 < path.size()) {
                    path = wetPath;
                    usedWetFallback = true;
                }
            }

            if (path.empty()) {
                continue;
            }

            const std::vector<Point> paintPath = makeRetroPath(path);
            const std::vector<std::pair<int, int>> waterSegments = collectWaterSegments(paintPath);
            if (usedWetFallback && !isValidBridgePattern(paintPath, waterSegments)) {
                continue;
            }
            if (shouldSkipOverParallelPath(paintPath, 0.18f, 4)) {
                continue;
            }

            for (const Point& p : paintPath) {
                const int i = Index(p.x, p.y, w);
                const std::uint8_t b = outBiomes[static_cast<std::size_t>(i)];

                if (b == RIVER || b == LAKE || b == SEA) {
                    outBiomes[static_cast<std::size_t>(i)] = BRIDGE;
                } else if (!IsWater(b) && !IsMountainLike(b)) {
                    outBiomes[static_cast<std::size_t>(i)] = ROAD;
                }
            }

            if (usedWetFallback) {
                for (const auto& seg : waterSegments) {
                    bridgeCenters.push_back(paintPath[static_cast<std::size_t>((seg.first + seg.second) / 2)]);
                }
            }
        }

        auto isRoadNetworkCell = [&](std::uint8_t b) {
            return b == ROAD || b == BRIDGE;
        };

        auto isPoiConnectedByRoad = [&](Point a, Point b) {
            const int start = Index(a.x, a.y, w);
            const int goal = Index(b.x, b.y, w);
            if (start == goal) {
                return true;
            }

            std::queue<int> q;
            std::vector<std::uint8_t> vis(static_cast<std::size_t>(n), 0);
            q.push(start);
            vis[static_cast<std::size_t>(start)] = 1;

            const std::array<int, 4> dx = {1, -1, 0, 0};
            const std::array<int, 4> dy = {0, 0, 1, -1};
            while (!q.empty()) {
                const int cur = q.front();
                q.pop();
                const int x = cur % w;
                const int y = cur / w;

                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dx[static_cast<std::size_t>(k)];
                    const int ny = y + dy[static_cast<std::size_t>(k)];
                    if (!InBounds(nx, ny, w, h)) {
                        continue;
                    }
                    const int ni = Index(nx, ny, w);
                    if (vis[static_cast<std::size_t>(ni)] != 0) {
                        continue;
                    }
                    if (ni == goal) {
                        return true;
                    }
                    const std::uint8_t bcell = outBiomes[static_cast<std::size_t>(ni)];
                    if (!isRoadNetworkCell(bcell)) {
                        continue;
                    }
                    vis[static_cast<std::size_t>(ni)] = 1;
                    q.push(ni);
                }
            }
            return false;
        };

        std::vector<int> connectedPoiIdx;
        if (!pois.empty()) {
            connectedPoiIdx.push_back(0);
        }

        for (int pass = 0; pass < static_cast<int>(pois.size()); ++pass) {
            bool expanded = false;
            for (int i = 0; i < static_cast<int>(pois.size()); ++i) {
                bool already = false;
                for (int c : connectedPoiIdx) {
                    if (c == i) {
                        already = true;
                        break;
                    }
                }
                if (already) {
                    continue;
                }

                for (int c : connectedPoiIdx) {
                    if (isPoiConnectedByRoad(pois[static_cast<std::size_t>(i)], pois[static_cast<std::size_t>(c)])) {
                        connectedPoiIdx.push_back(i);
                        expanded = true;
                        break;
                    }
                }
            }
            if (!expanded) {
                break;
            }
        }

        for (int i = 0; i < static_cast<int>(pois.size()); ++i) {
            bool already = false;
            for (int c : connectedPoiIdx) {
                if (c == i) {
                    already = true;
                    break;
                }
            }
            if (already) {
                continue;
            }

            int anchor = -1;
            float bestD = std::numeric_limits<float>::max();
            for (int c : connectedPoiIdx) {
                const float dx = static_cast<float>(pois[static_cast<std::size_t>(i)].x - pois[static_cast<std::size_t>(c)].x);
                const float dy = static_cast<float>(pois[static_cast<std::size_t>(i)].y - pois[static_cast<std::size_t>(c)].y);
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d < bestD) {
                    bestD = d;
                    anchor = c;
                }
            }
            if (anchor < 0) {
                continue;
            }

            const Point s = pois[static_cast<std::size_t>(i)];
            const Point g = pois[static_cast<std::size_t>(anchor)];
            std::vector<Point> repair = FindPathAStar(outBiomes, w, h, s, g, 7.8f, true);
            if (repair.empty()) {
                continue;
            }

            std::vector<Point> paintRepair = makeRetroPath(repair);
            const std::vector<std::pair<int, int>> segs = collectWaterSegments(paintRepair);
            if (!segs.empty() && !isValidBridgePattern(paintRepair, segs)) {
                continue;
            }
            if (shouldSkipOverParallelPath(paintRepair, 0.12f, 3)) {
                continue;
            }

            for (const Point& p : paintRepair) {
                const int pi = Index(p.x, p.y, w);
                const std::uint8_t bb = outBiomes[static_cast<std::size_t>(pi)];
                if (bb == SEA || bb == RIVER || bb == LAKE) {
                    outBiomes[static_cast<std::size_t>(pi)] = BRIDGE;
                } else if (bb == MOUNTAIN || bb == EXMOUNTAIN) {
                    if (ur(rng) < 0.08f) {
                        outBiomes[static_cast<std::size_t>(pi)] = ROAD;
                    }
                } else {
                    outBiomes[static_cast<std::size_t>(pi)] = ROAD;
                }
            }

            connectedPoiIdx.push_back(i);
            for (const auto& seg : segs) {
                bridgeCenters.push_back(paintRepair[static_cast<std::size_t>((seg.first + seg.second) / 2)]);
            }
        }
    }

    {
        const int branchAttemptsBase = static_cast<int>(std::clamp(GetParam("roadBranchAttempts", 10.0), 0.0, 64.0));
        const int branchAttempts = static_cast<int>(std::clamp(
            std::round(static_cast<double>(branchAttemptsBase) * static_cast<double>(roadDensity)),
            0.0,
            128.0));
        const int branchMinTargetDist = static_cast<int>(std::clamp(GetParam("roadBranchMinDist", 22.0), 8.0, 96.0));

        auto isRoadLike = [](std::uint8_t b) {
            return b == ROAD || b == BRIDGE;
        };

        std::vector<int> roadDist(static_cast<std::size_t>(n), std::numeric_limits<int>::max());
        std::queue<int> q;
        for (int i = 0; i < n; ++i) {
            if (isRoadLike(outBiomes[static_cast<std::size_t>(i)])) {
                roadDist[static_cast<std::size_t>(i)] = 0;
                q.push(i);
            }
        }

        const std::array<int, 4> dx = {1, -1, 0, 0};
        const std::array<int, 4> dy = {0, 0, 1, -1};
        auto isTraversableForBranch = [&](std::uint8_t b) {
            return b == PLAIN || b == DESERT || b == FOREST || b == ROAD || b == BRIDGE;
        };
        auto branchTooParallel = [&](const std::vector<Point>& path) {
            int fresh = 0;
            int parallelFresh = 0;
            for (const Point& p : path) {
                const int i = Index(p.x, p.y, w);
                const std::uint8_t b = outBiomes[static_cast<std::size_t>(i)];
                if (isRoadLike(b)) {
                    continue;
                }
                ++fresh;
                int adj = 0;
                for (int k = 0; k < 4; ++k) {
                    const int nx = p.x + dx[static_cast<std::size_t>(k)];
                    const int ny = p.y + dy[static_cast<std::size_t>(k)];
                    if (!InBounds(nx, ny, w, h)) {
                        continue;
                    }
                    if (isRoadLike(outBiomes[static_cast<std::size_t>(Index(nx, ny, w))])) {
                        ++adj;
                    }
                }
                if (adj >= 1) {
                    ++parallelFresh;
                }
            }
            if (fresh < 4) {
                return true;
            }
            return (static_cast<float>(parallelFresh) / static_cast<float>(std::max(1, fresh))) > 0.86f;
        };

        while (!q.empty()) {
            const int cur = q.front();
            q.pop();
            const int x = cur % w;
            const int y = cur / w;
            const int nd = roadDist[static_cast<std::size_t>(cur)] + 1;

            for (int k = 0; k < 4; ++k) {
                const int nx = x + dx[static_cast<std::size_t>(k)];
                const int ny = y + dy[static_cast<std::size_t>(k)];
                if (!InBounds(nx, ny, w, h)) {
                    continue;
                }
                const int ni = Index(nx, ny, w);
                if (roadDist[static_cast<std::size_t>(ni)] <= nd) {
                    continue;
                }
                if (!isTraversableForBranch(outBiomes[static_cast<std::size_t>(ni)])) {
                    continue;
                }
                roadDist[static_cast<std::size_t>(ni)] = nd;
                q.push(ni);
            }
        }

        std::vector<Point> hubs;
        hubs.reserve(static_cast<std::size_t>(n / 24));
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                const int i = Index(x, y, w);
                if (!isRoadLike(outBiomes[static_cast<std::size_t>(i)])) {
                    continue;
                }

                int deg = 0;
                for (int k = 0; k < 4; ++k) {
                    const int ni = Index(x + dx[static_cast<std::size_t>(k)], y + dy[static_cast<std::size_t>(k)], w);
                    if (isRoadLike(outBiomes[static_cast<std::size_t>(ni)])) {
                        ++deg;
                    }
                }
                if (deg >= 2) {
                    hubs.push_back({x, y});
                }
            }
        }

        std::vector<int> frontier;
        frontier.reserve(static_cast<std::size_t>(n / 6));
        for (int i = 0; i < n; ++i) {
            const std::uint8_t b = outBiomes[static_cast<std::size_t>(i)];
            if (!(b == PLAIN || b == DESERT || b == FOREST)) {
                continue;
            }
            const int d = roadDist[static_cast<std::size_t>(i)];
            if (d >= branchMinTargetDist && d < std::numeric_limits<int>::max() / 2) {
                frontier.push_back(i);
            }
        }

        std::shuffle(hubs.begin(), hubs.end(), rng);
        std::shuffle(frontier.begin(), frontier.end(), rng);

        int done = 0;
        for (int fi : frontier) {
            if (done >= branchAttempts || hubs.empty()) {
                break;
            }

            const Point target{fi % w, fi / w};
            Point anchor = hubs[static_cast<std::size_t>(done % hubs.size())];

            std::vector<Point> branchPath = FindPathAStar(outBiomes, w, h, anchor, target, 10.0f, false);
            if (branchPath.empty()) {
                branchPath = FindPathAStar(outBiomes, w, h, anchor, target, 8.0f, true);
            }
            if (branchPath.empty()) {
                continue;
            }
            if (branchTooParallel(branchPath)) {
                continue;
            }

            int painted = 0;
            for (const Point& p : branchPath) {
                const int i = Index(p.x, p.y, w);
                std::uint8_t b = outBiomes[static_cast<std::size_t>(i)];
                if (b == ROAD || b == BRIDGE) {
                    continue;
                }
                if (b == SEA || b == RIVER || b == LAKE) {
                    outBiomes[static_cast<std::size_t>(i)] = BRIDGE;
                    ++painted;
                } else if (b == MOUNTAIN || b == EXMOUNTAIN) {
                    if (ur(rng) < 0.08f) {
                        outBiomes[static_cast<std::size_t>(i)] = ROAD;
                        ++painted;
                    }
                } else {
                    outBiomes[static_cast<std::size_t>(i)] = ROAD;
                    ++painted;
                }
            }

            if (painted > 0) {
                ++done;
            }
        }
    }

    {
        const std::array<int, 4> bdx = {1, -1, 0, 0};
        const std::array<int, 4> bdy = {0, 0, 1, -1};
        const int maxAutoBridges = std::clamp(static_cast<int>(pois.size()) / 2, 1, 8);
        const int maxSpan = 3;
        int bridgesAdded = 0;

        auto hasRoadNearby = [&](int x, int y, int r) {
            for (int oy = -r; oy <= r; ++oy) {
                for (int ox = -r; ox <= r; ++ox) {
                    const int nx = x + ox;
                    const int ny = y + oy;
                    if (!InBounds(nx, ny, w, h)) {
                        continue;
                    }
                    const std::uint8_t b = outBiomes[static_cast<std::size_t>(Index(nx, ny, w))];
                    if (b == ROAD || b == BRIDGE) {
                        return true;
                    }
                }
            }
            return false;
        };

        auto hasBridgeTooClose = [&](int x, int y, int minDist) {
            for (int ny = std::max(0, y - minDist); ny <= std::min(h - 1, y + minDist); ++ny) {
                for (int nx = std::max(0, x - minDist); nx <= std::min(w - 1, x + minDist); ++nx) {
                    const int md = std::abs(nx - x) + std::abs(ny - y);
                    if (md > minDist) {
                        continue;
                    }
                    if (outBiomes[static_cast<std::size_t>(Index(nx, ny, w))] == BRIDGE) {
                        return true;
                    }
                }
            }
            return false;
        };

        for (int y = 2; y < h - 2 && bridgesAdded < maxAutoBridges; ++y) {
            for (int x = 2; x < w - 2 && bridgesAdded < maxAutoBridges; ++x) {
                const int i = Index(x, y, w);
                if (outBiomes[static_cast<std::size_t>(i)] != ROAD) {
                    continue;
                }

                for (int d = 0; d < 4 && bridgesAdded < maxAutoBridges; ++d) {
                    int cx = x;
                    int cy = y;
                    std::vector<int> waterCells;

                    for (int step = 1; step <= maxSpan; ++step) {
                        cx += bdx[static_cast<std::size_t>(d)];
                        cy += bdy[static_cast<std::size_t>(d)];
                        if (!InBounds(cx, cy, w, h)) {
                            break;
                        }

                        const int ci = Index(cx, cy, w);
                        const std::uint8_t b = outBiomes[static_cast<std::size_t>(ci)];
                        if (b == RIVER || b == LAKE || b == SEA) {
                            waterCells.push_back(ci);
                            continue;
                        }

                        if (waterCells.empty()) {
                            break;
                        }

                        if (IsMountainLike(b) || IsWater(b)) {
                            break;
                        }

                        if (!hasRoadNearby(cx, cy, 2)) {
                            break;
                        }

                        const Point center{(x + cx) / 2, (y + cy) / 2};
                        if (hasBridgeTooClose(center.x, center.y, std::max(4, bridgeMinInterval / 2))) {
                            break;
                        }

                        for (int wi : waterCells) {
                            outBiomes[static_cast<std::size_t>(wi)] = BRIDGE;
                        }
                        if (outBiomes[static_cast<std::size_t>(ci)] == PLAIN || outBiomes[static_cast<std::size_t>(ci)] == DESERT || outBiomes[static_cast<std::size_t>(ci)] == FOREST) {
                            outBiomes[static_cast<std::size_t>(ci)] = ROAD;
                        }
                        ++bridgesAdded;
                        break;
                    }
                }
            }
        }
    }

    {
        const float roadMinCoverage = static_cast<float>(std::clamp(GetParam("roadMinCoverage", 0.005), 0.001, 0.03)) * roadDensity;
        const int roadTarget = static_cast<int>(std::ceil(static_cast<float>(n) * roadMinCoverage));

        auto countRoadLike = [&]() {
            int c = 0;
            for (std::uint8_t b : outBiomes) {
                if (b == ROAD || b == BRIDGE) {
                    ++c;
                }
            }
            return c;
        };

        int roadCount = countRoadLike();
        if (roadCount < roadTarget && pois.size() >= 2) {
            auto isRoadLike = [](std::uint8_t b) {
                return b == ROAD || b == BRIDGE;
            };
            const std::array<int, 4> dx = {1, -1, 0, 0};
            const std::array<int, 4> dy = {0, 0, 1, -1};

            std::vector<Edge> pairs;
            pairs.reserve((pois.size() * (pois.size() - 1)) / 2);
            for (int i = 0; i < static_cast<int>(pois.size()); ++i) {
                for (int j = i + 1; j < static_cast<int>(pois.size()); ++j) {
                    const float pdx = static_cast<float>(pois[static_cast<std::size_t>(i)].x - pois[static_cast<std::size_t>(j)].x);
                    const float pdy = static_cast<float>(pois[static_cast<std::size_t>(i)].y - pois[static_cast<std::size_t>(j)].y);
                    pairs.push_back({i, j, std::sqrt(pdx * pdx + pdy * pdy)});
                }
            }
            std::sort(pairs.begin(), pairs.end(), [](const Edge& a, const Edge& b) {
                return a.d > b.d;
            });

            int attempts = 0;
            for (const Edge& e : pairs) {
                if (roadCount >= roadTarget) {
                    break;
                }
                if (attempts >= static_cast<int>(pairs.size())) {
                    break;
                }
                ++attempts;

                const Point s = pois[static_cast<std::size_t>(e.a)];
                const Point g = pois[static_cast<std::size_t>(e.b)];
                std::vector<Point> path = FindPathAStar(outBiomes, w, h, s, g, 7.5f, true);
                if (path.empty()) {
                    continue;
                }

                int fresh = 0;
                int parallelFresh = 0;
                for (const Point& p : path) {
                    const int i = Index(p.x, p.y, w);
                    if (isRoadLike(outBiomes[static_cast<std::size_t>(i)])) {
                        continue;
                    }
                    ++fresh;
                    int adj = 0;
                    for (int k = 0; k < 4; ++k) {
                        const int nx = p.x + dx[static_cast<std::size_t>(k)];
                        const int ny = p.y + dy[static_cast<std::size_t>(k)];
                        if (!InBounds(nx, ny, w, h)) {
                            continue;
                        }
                        if (isRoadLike(outBiomes[static_cast<std::size_t>(Index(nx, ny, w))])) {
                            ++adj;
                        }
                    }
                    if (adj >= 1) {
                        ++parallelFresh;
                    }
                }
                if (fresh < 6 || (static_cast<float>(parallelFresh) / static_cast<float>(std::max(1, fresh))) > 0.85f) {
                    continue;
                }

                for (const Point& p : path) {
                    const int i = Index(p.x, p.y, w);
                    const std::uint8_t b = outBiomes[static_cast<std::size_t>(i)];
                    if (b == ROAD || b == BRIDGE) {
                        continue;
                    }
                    if (b == SEA || b == RIVER || b == LAKE) {
                        outBiomes[static_cast<std::size_t>(i)] = BRIDGE;
                        ++roadCount;
                    } else if (b == MOUNTAIN || b == EXMOUNTAIN) {
                        if (ur(rng) < 0.12f) {
                            outBiomes[static_cast<std::size_t>(i)] = ROAD;
                            ++roadCount;
                        }
                    } else {
                        outBiomes[static_cast<std::size_t>(i)] = ROAD;
                        ++roadCount;
                    }
                }
            }
        }
    }

    {
        if (mode == MacroMode::Basin) {
            const float rimThreshold = static_cast<float>(std::clamp(GetParam("basinLandRimThreshold", 0.96), 0.86, 0.995));
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const int i = Index(x, y, w);
                    const std::uint8_t b = outBiomes[static_cast<std::size_t>(i)];
                    const float nx = static_cast<float>(x) / static_cast<float>(std::max(1, w - 1));
                    const float ny = static_cast<float>(y) / static_cast<float>(std::max(1, h - 1));
                    const float r = BasinRoundMetric(nx, ny);
                    if (r < rimThreshold) {
                        continue;
                    }
                    if (b == SEA || b == LAKE || b == RIVER) {
                        outBiomes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(PLAIN);
                    } else if (b == BRIDGE) {
                        outBiomes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(ROAD);
                    }
                }
            }
        } else {
            const int seaBand = std::max(2, std::min(w, h) / 80);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const int edgeDist = std::min({x, y, w - 1 - x, h - 1 - y});
                    if (edgeDist <= seaBand) {
                        outBiomes[static_cast<std::size_t>(Index(x, y, w))] = static_cast<std::uint8_t>(SEA);
                    }
                }
            }
        }
    }

    // Keep the macro land/sea balance inside the documented range.  Only
    // ordinary land is removed, while expansion uses interior sea cells near
    // existing land, so rivers, roads, mountains, and POI markers survive.
    {
        const int minLandCells = static_cast<int>(std::lround(static_cast<double>(n) * 0.35));
        const int maxLandCells = static_cast<int>(std::lround(static_cast<double>(n) * 0.70));
        int landCells = 0;
        std::unordered_set<int> protectedCells;
        protectedCells.reserve(outPlan.markers.size());
        for (const MapPlanMarker& marker : outPlan.markers) {
            if (InBounds(marker.x, marker.y, w, h)) {
                protectedCells.insert(Index(marker.x, marker.y, w));
            }
        }
        for (std::uint8_t biome : outBiomes) {
            if (!IsWater(biome)) ++landCells;
        }

        if (landCells < minLandCells) {
            std::vector<std::pair<int, int>> candidates;
            candidates.reserve(static_cast<std::size_t>(n - landCells));
            for (int y = 2; y < h - 2; ++y) {
                for (int x = 2; x < w - 2; ++x) {
                    const int cell = Index(x, y, w);
                    if (outBiomes[static_cast<std::size_t>(cell)] != SEA || protectedCells.find(cell) != protectedCells.end()) continue;
                    int adjacentLand = 0;
                    for (int oy = -1; oy <= 1; ++oy) {
                        for (int ox = -1; ox <= 1; ++ox) {
                            if (ox == 0 && oy == 0) continue;
                            const std::uint8_t adjacentBiome = outBiomes[static_cast<std::size_t>(Index(x + ox, y + oy, w))];
                            if (!IsWater(adjacentBiome)) ++adjacentLand;
                        }
                    }
                    candidates.push_back({adjacentLand, cell});
                }
            }
            std::sort(candidates.begin(), candidates.end(), [](const std::pair<int, int>& lhs, const std::pair<int, int>& rhs) {
                if (lhs.first != rhs.first) return lhs.first > rhs.first;
                return lhs.second < rhs.second;
            });
            for (const auto& candidate : candidates) {
                if (landCells >= minLandCells) break;
                outBiomes[static_cast<std::size_t>(candidate.second)] = PLAIN;
                ++landCells;
            }
            if (landCells < minLandCells) {
                for (int y = 2; y < h - 2 && landCells < minLandCells; ++y) {
                    for (int x = 2; x < w - 2 && landCells < minLandCells; ++x) {
                        const int cell = Index(x, y, w);
                        if (outBiomes[static_cast<std::size_t>(cell)] == SEA && protectedCells.find(cell) == protectedCells.end()) {
                            outBiomes[static_cast<std::size_t>(cell)] = PLAIN;
                            ++landCells;
                        }
                    }
                }
            }
        } else if (landCells > maxLandCells) {
            std::vector<std::pair<int, int>> candidates;
            for (int cell = 0; cell < n; ++cell) {
                const std::uint8_t biome = outBiomes[static_cast<std::size_t>(cell)];
                if ((biome != PLAIN && biome != DESERT && biome != FOREST) || protectedCells.find(cell) != protectedCells.end()) continue;
                const int x = cell % w;
                const int y = cell / w;
                int adjacentSea = 0;
                int adjacentRoad = 0;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if ((ox == 0 && oy == 0) || !InBounds(x + ox, y + oy, w, h)) continue;
                        const std::uint8_t neighbor = outBiomes[static_cast<std::size_t>(Index(x + ox, y + oy, w))];
                        if (neighbor == SEA) ++adjacentSea;
                        if (neighbor == ROAD || neighbor == BRIDGE) ++adjacentRoad;
                    }
                }
                candidates.push_back({adjacentSea * 4 - adjacentRoad * 10, cell});
            }
            std::sort(candidates.begin(), candidates.end(), [](const std::pair<int, int>& lhs, const std::pair<int, int>& rhs) {
                if (lhs.first != rhs.first) return lhs.first > rhs.first;
                return lhs.second < rhs.second;
            });
            for (const auto& candidate : candidates) {
                if (landCells <= maxLandCells) break;
                if (outBiomes[static_cast<std::size_t>(candidate.second)] == PLAIN ||
                    outBiomes[static_cast<std::size_t>(candidate.second)] == DESERT ||
                    outBiomes[static_cast<std::size_t>(candidate.second)] == FOREST) {
                    outBiomes[static_cast<std::size_t>(candidate.second)] = SEA;
                    --landCells;
                }
            }
        }
    }

    // Coverage parameters are quality targets, not only threshold hints.  The
    // earlier biome pass could collapse desert/forest into tiny speckles after
    // water and road repair.  Rebalance only ordinary land cells at the end so
    // roads, rivers, mountains, and the playable topology remain untouched.
    {
        std::vector<int> softLand;
        softLand.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const std::uint8_t biome = outBiomes[static_cast<std::size_t>(i)];
            if (biome == PLAIN || biome == DESERT || biome == FOREST) {
                softLand.push_back(i);
            }
        }

        const int targetDesert = std::clamp(static_cast<int>(std::lround(static_cast<double>(softLand.size()) * desertCov)), 0, static_cast<int>(softLand.size()));
        const int targetForest = std::clamp(static_cast<int>(std::lround(static_cast<double>(softLand.size()) * forestCov)), 0, static_cast<int>(softLand.size()) - targetDesert);
        std::vector<std::pair<float, int>> desertScores;
        std::vector<std::pair<float, int>> forestScores;
        desertScores.reserve(softLand.size());
        forestScores.reserve(softLand.size());
        for (int cell : softLand) {
            const int x = cell % w;
            const int y = cell / w;
            const float nx = static_cast<float>(x) / static_cast<float>(std::max(1, w - 1));
            const float ny = static_cast<float>(y) / static_cast<float>(std::max(1, h - 1));
            const float inland = Clamp01(static_cast<float>(seaDist[static_cast<std::size_t>(cell)]) / (maxD * 0.24f));
            const float mountainProximity = 1.0f - Clamp01(static_cast<float>(mountainDist[static_cast<std::size_t>(cell)]) / (maxD * 0.18f));
            const float detail = mapgen::Fbm2D(nx * 4.7f * scale, ny * 4.7f * scale, 3, 2.0f, 0.52f, params_.seed ^ 0x91f2ab37U);
            const float dryness = 1.0f - moisture[static_cast<std::size_t>(cell)];
            desertScores.push_back({dryness * 0.62f + inland * 0.20f + (1.0f - height[static_cast<std::size_t>(cell)]) * 0.10f + detail * 0.08f, cell});
            forestScores.push_back({mountainProximity * 0.44f + moisture[static_cast<std::size_t>(cell)] * 0.34f + inland * 0.10f + height[static_cast<std::size_t>(cell)] * 0.12f + detail * 0.04f, cell});
        }
        auto scoreOrder = [](const std::pair<float, int>& lhs, const std::pair<float, int>& rhs) {
            if (lhs.first != rhs.first) {
                return lhs.first > rhs.first;
            }
            return lhs.second < rhs.second;
        };
        std::sort(desertScores.begin(), desertScores.end(), scoreOrder);
        std::sort(forestScores.begin(), forestScores.end(), scoreOrder);
        std::unordered_set<int> desertCells;
        desertCells.reserve(static_cast<std::size_t>(targetDesert));
        for (int i = 0; i < targetDesert; ++i) {
            desertCells.insert(desertScores[static_cast<std::size_t>(i)].second);
        }
        std::unordered_set<int> forestCells;
        forestCells.reserve(static_cast<std::size_t>(targetForest));
        for (const auto& scored : forestScores) {
            if (static_cast<int>(forestCells.size()) >= targetForest) {
                break;
            }
            if (desertCells.find(scored.second) == desertCells.end()) {
                forestCells.insert(scored.second);
            }
        }
        for (int cell : softLand) {
            if (desertCells.find(cell) != desertCells.end()) {
                outBiomes[static_cast<std::size_t>(cell)] = DESERT;
            } else if (forestCells.find(cell) != forestCells.end()) {
                outBiomes[static_cast<std::size_t>(cell)] = FOREST;
            } else {
                outBiomes[static_cast<std::size_t>(cell)] = PLAIN;
            }
        }
    }

    return true;
}
