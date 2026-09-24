#include "DungeonMapImageGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <queue>
#include <random>
#include <utility>
#include <vector>

namespace {

struct Point {
    int x = 0;
    int y = 0;
};

struct Rect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;

    int Width() const { return x1 - x0; }
    int Height() const { return y1 - y0; }
    int CenterX() const { return (x0 + x1) / 2; }
    int CenterY() const { return (y0 + y1) / 2; }
};

struct Room {
    Rect rect;
    bool key = false;
    bool structured = false;
};

struct Edge {
    int a = 0;
    int b = 0;
    int d = 0;
};

struct DoorPair {
    Point c0;
    Point c1;
    Point center;
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

enum class LayoutMode {
    Hub,
    Ring,
    Branch,
};

inline int Index(int x, int y, int w) {
    return y * w + x;
}

inline bool InBounds(int x, int y, int w, int h) {
    return x >= 0 && y >= 0 && x < w && y < h;
}

bool RectOverlapWithMargin(const Rect& a, const Rect& b, int margin) {
    return a.x0 - margin < b.x1 &&
        a.x1 + margin > b.x0 &&
        a.y0 - margin < b.y1 &&
        a.y1 + margin > b.y0;
}

LayoutMode ChooseLayoutMode(std::mt19937& rng, double wHub, double wRing, double wBranch) {
    std::discrete_distribution<int> dist({std::max(0.0, wHub), std::max(0.0, wRing), std::max(0.0, wBranch)});
    const int mode = dist(rng);
    if (mode == 0) {
        return LayoutMode::Hub;
    }
    if (mode == 1) {
        return LayoutMode::Ring;
    }
    return LayoutMode::Branch;
}

}  // namespace

DungeonMapImageGenerator::DungeonMapImageGenerator(MapGenParams params) : MapImageGenerator(std::move(params)) {}

MapGenColor DungeonMapImageGenerator::BiomeToColor(std::uint8_t biome) const {
    switch (biome) {
        case PLAIN:      // FLOOR1
            return {54, 146, 96};
        case DESERT:     // FLOOR2
            return {216, 128, 56};
        case FOREST:     // UPPERFLOOR
            return {102, 82, 133};
        case MOUNTAIN:   // WALL (1-tile)
            return {188, 193, 198};
        case EXMOUNTAIN: // not used for dungeon now
            return {112, 118, 126};
        case ROAD:       // CORRIDOR
            return {56, 86, 214};
        case BRIDGE:     // DOOR
            return {170, 128, 68};
        case RIVER:
            return {44, 103, 219};
        case LAKE:
            return {57, 126, 231};
        case SEA:        // void/background
            return {0, 0, 0};
        default:
            return {255, 0, 255};
    }
}

bool DungeonMapImageGenerator::Generate(MapPlan& outPlan, std::string& outError) {
    const int w = params_.width;
    const int h = params_.depth;
    if (w <= 0 || h <= 0) {
        outError = "Invalid map size";
        return false;
    }

    const int n = w * h;
    outPlan.Reset(w, h, params_.seed, "dungeon");
    std::vector<std::uint8_t>& outBiomes = outPlan.biomes;
    outBiomes.assign(static_cast<std::size_t>(n), static_cast<std::uint8_t>(SEA));

    std::mt19937 rng(params_.seed);
    std::uniform_real_distribution<float> ur(0.0f, 1.0f);

    const double defaultJitter = std::clamp(GetParam("defaultJitter", 0.12), 0.0, 0.5);
    auto readParam = [&](const char* key, double base, double minValue, double maxValue, double jitterScale) {
        auto it = params_.extra.find(key);
        if (it != params_.extra.end()) {
            return std::clamp(it->second, minValue, maxValue);
        }
        double v = base;
        if (defaultJitter > 0.0 && jitterScale > 0.0 && base != 0.0) {
            const double amp = defaultJitter * jitterScale;
            std::uniform_real_distribution<double> dj(-amp, amp);
            v = base * (1.0 + dj(rng));
        }
        return std::clamp(v, minValue, maxValue);
    };

    const float roomRate = static_cast<float>(readParam("roomRate", 0.38, 0.18, 0.72, 1.0));
    const float roomSize = static_cast<float>(readParam("roomSize", 1.0, 0.55, 2.2, 0.6));
    const int roomMinSize = static_cast<int>(std::round(readParam("roomMinSize", 12.0, 8.0, 48.0, 0.5)));
    const int roomMaxSize = static_cast<int>(std::clamp(std::round(readParam("roomMaxSize", 32.0, static_cast<double>(roomMinSize), 80.0, 0.5)), static_cast<double>(roomMinSize), 80.0));
    const int referenceArea = 256 * 256;
    const int roomCountMinDefault = std::max(6, referenceArea / 9000);
    const int roomCountMaxDefault = std::max(roomCountMinDefault, referenceArea / 2600);
    const int roomCountMin = static_cast<int>(std::round(readParam("roomCountMin", static_cast<double>(roomCountMinDefault), 3.0, 64.0, 0.6)));
    const int roomCountMax = static_cast<int>(std::clamp(std::round(readParam("roomCountMax", static_cast<double>(std::max(roomCountMin, roomCountMaxDefault)), static_cast<double>(roomCountMin), 128.0, 0.6)), static_cast<double>(roomCountMin), 128.0));
    const float largeRoomChance = static_cast<float>(readParam("largeRoomChance", 0.18, 0.0, 0.75, 1.0));
    const float hazardRate = static_cast<float>(readParam("hazardRate", 0.14, 0.0, 0.65, 1.0));
    const float corridorTargetRatio = static_cast<float>(readParam("corridorTargetRatio", 0.14, 0.08, 0.32, 0.8));
    const int corridorWidth = static_cast<int>(std::clamp(GetParam("corridorWidth", 4.0), 4.0, 4.0));
    const int mainCorridorWidth = static_cast<int>(std::clamp(GetParam("mainCorridorWidth", 8.0), 8.0, 8.0));
    const float loopRate = static_cast<float>(readParam("loopRate", 0.30, 0.0, 1.4, 1.0));
    const float deadEndRate = static_cast<float>(readParam("deadEndRate", 0.18, 0.0, 0.7, 1.0));
    const int doorMinInterval = static_cast<int>(std::round(readParam("doorMinInterval", 3.0, 1.0, 16.0, 0.5)));
    const float keyRoomDoorBias = static_cast<float>(readParam("keyRoomDoorBias", 1.7, 1.0, 4.0, 0.5));
    const int doorLength = 4;
    const float linearWingChance = static_cast<float>(readParam("linearWingChance", 0.33, 0.0, 8.0, 1.0));
    const int linearWingRoomMin = static_cast<int>(std::round(readParam("linearWingRoomMin", 2.0, 2.0, 4.0, 0.5)));
    const int linearWingRoomMax = static_cast<int>(std::clamp(std::round(readParam("linearWingRoomMax", 4.0, static_cast<double>(linearWingRoomMin), 6.0, 0.5)), static_cast<double>(linearWingRoomMin), 6.0));
    const int linearWingAxis = static_cast<int>(std::clamp(GetParam("linearWingAxis", 0.0), 0.0, 2.0)); // 0:auto, 1:Y, 2:X
    const int linearWingSide = static_cast<int>(std::clamp(GetParam("linearWingSide", 0.0), 0.0, 3.0)); // 0:auto, 1:left/up, 2:right/down, 3:both
    const float mirrorXChance = static_cast<float>(readParam("mirrorXChance", 0.45, 0.0, 1.0, 1.0));
    const float mirrorXApproxRate = static_cast<float>(readParam("mirrorXApproxRate", 0.80, 0.5, 1.0, 0.4));
    const int mirrorXRoomCount = static_cast<int>(std::round(readParam("mirrorXRoomCount", 0.0, 0.0, 10.0, 0.0))); // 0 means auto

    const LayoutMode mode = ChooseLayoutMode(
        rng,
        GetParam("layoutHubWeight", 0.40),
        GetParam("layoutRingWeight", 0.30),
        GetParam("layoutBranchWeight", 0.30));

    const int preferredLinkLen = std::max(10, static_cast<int>(std::round(static_cast<float>(w + h) * 0.14f)));

    float effectiveLoopRate = loopRate;
    float effectiveDeadEndRate = deadEndRate;
    int roomPlacementMargin = std::max(1, corridorWidth);
    if (mode == LayoutMode::Hub) {
        effectiveLoopRate = std::clamp(loopRate * 1.15f, 0.0f, 1.6f);
        effectiveDeadEndRate = std::clamp(deadEndRate * 0.70f, 0.0f, 0.7f);
    } else if (mode == LayoutMode::Ring) {
        effectiveLoopRate = std::clamp(loopRate * 1.35f, 0.0f, 1.8f);
        effectiveDeadEndRate = std::clamp(deadEndRate * 0.60f, 0.0f, 0.7f);
    } else {
        effectiveLoopRate = std::clamp(loopRate * 0.55f, 0.0f, 1.2f);
        effectiveDeadEndRate = std::clamp(deadEndRate * 1.45f, 0.0f, 0.7f);
    }

    const int targetRoomArea = static_cast<int>(std::round(static_cast<float>(n) * roomRate));
    const int minSide = std::max(4, static_cast<int>(std::round(static_cast<float>(roomMinSize) * roomSize)));
    const int maxSide = std::max(minSide, static_cast<int>(std::round(static_cast<float>(roomMaxSize) * roomSize)));
    const int border = 2;

    std::vector<Room> rooms;
    rooms.reserve(static_cast<std::size_t>(std::max(8, n / 1800)));
    std::vector<int> roomLinearWingId;
    roomLinearWingId.reserve(static_cast<std::size_t>(std::max(8, n / 1800)));
    std::vector<std::uint8_t> roomMask(static_cast<std::size_t>(n), 0);

    auto canPlaceRect = [&](const Rect& r, int margin) {
        if (r.x0 < border || r.y0 < border || r.x1 >= w - border || r.y1 >= h - border) {
            return false;
        }
        for (const Room& rm : rooms) {
            if (RectOverlapWithMargin(r, rm.rect, margin)) {
                return false;
            }
        }
        return true;
    };

    auto carveRoom = [&](const Room& room) {
        const Rect& r = room.rect;
        const std::uint8_t floorTile = static_cast<std::uint8_t>(room.structured ? DESERT : PLAIN);
        for (int y = r.y0; y < r.y1; ++y) {
            for (int x = r.x0; x < r.x1; ++x) {
                const int i = Index(x, y, w);
                outBiomes[static_cast<std::size_t>(i)] = floorTile;
                roomMask[static_cast<std::size_t>(i)] = 1;
            }
        }
    };

    int carvedRoomArea = 0;
    struct LinearWingLayout {
        std::vector<int> roomIndices;
        int spineAxis = -1;   // 0:Y-spine(vertical), 1:X-spine(horizontal)
        int spinePos = -1;
        int spineStart = -1;
        int spineEnd = -1;
    };
    std::vector<LinearWingLayout> linearWingLayouts;

    auto tryAddRoom = [&](Rect r, bool key, int margin, bool structured, int linearWingId) {
        if (!canPlaceRect(r, margin)) {
            return false;
        }
        rooms.push_back({r, key, structured});
        roomLinearWingId.push_back(linearWingId);
        carveRoom(rooms.back());
        carvedRoomArea += r.Width() * r.Height();
        return true;
    };

    if (mode == LayoutMode::Hub) {
        const int rw = std::min(w - border * 2 - 2, std::max(minSide + 4, maxSide + maxSide / 4));
        const int rh = std::min(h - border * 2 - 2, std::max(minSide + 4, maxSide + maxSide / 4));
        Rect hub{(w - rw) / 2, (h - rh) / 2, (w - rw) / 2 + rw, (h - rh) / 2 + rh};
        (void)tryAddRoom(hub, true, 2, false, -1);
    } else if (mode == LayoutMode::Ring) {
        const int rw = std::min(w - border * 2 - 2, std::max(minSide + 2, maxSide));
        const int rh = std::min(h - border * 2 - 2, std::max(minSide + 2, maxSide));
        Rect center{(w - rw) / 2, (h - rh) / 2, (w - rw) / 2 + rw, (h - rh) / 2 + rh};
        (void)tryAddRoom(center, true, 2, false, -1);

        const int spokes = 6;
        const float rx = static_cast<float>(w) * 0.30f;
        const float ry = static_cast<float>(h) * 0.28f;
        for (int i = 0; i < spokes; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(spokes) * 6.2831853f;
            const int sw = std::uniform_int_distribution<int>(minSide, maxSide)(rng);
            const int sh = std::uniform_int_distribution<int>(minSide, maxSide)(rng);
            const int cx = static_cast<int>(std::round(static_cast<float>(w) * 0.5f + std::cos(t) * rx));
            const int cy = static_cast<int>(std::round(static_cast<float>(h) * 0.5f + std::sin(t) * ry));
            Rect r{cx - sw / 2, cy - sh / 2, cx - sw / 2 + sw, cy - sh / 2 + sh};
            (void)tryAddRoom(r, ur(rng) < 0.25f, 2, false, -1);
        }
    }

    if (ur(rng) < mirrorXChance) {
        int pairTarget = std::uniform_int_distribution<int>(2, 4)(rng);
        int centerTarget = std::uniform_int_distribution<int>(1, 3)(rng);
        if (mirrorXRoomCount > 0) {
            int bestP = 0;
            int bestC = 1;
            int bestCost = 1000000;
            for (int p = 0; p <= 4; ++p) {
                for (int c = 0; c <= 3; ++c) {
                    if (p == 0 && c == 0) {
                        continue;
                    }
                    const int total = p * 2 + c;
                    const int cost = std::abs(total - mirrorXRoomCount);
                    if (cost < bestCost || (cost == bestCost && p > bestP)) {
                        bestCost = cost;
                        bestP = p;
                        bestC = c;
                    }
                }
            }
            pairTarget = bestP;
            centerTarget = bestC;
        }
        const int cx = w / 2;
        const int cy = h / 2;

        auto canPlaceAgainstExisting = [&](const Rect& r) {
            if (r.x0 < border || r.y0 < border || r.x1 >= w - border || r.y1 >= h - border) {
                return false;
            }
            for (const Room& rm : rooms) {
                if (RectOverlapWithMargin(r, rm.rect, roomPlacementMargin)) {
                    return false;
                }
            }
            return true;
        };

        int pairPlaced = 0;
        for (int ai = 0; ai < 96 && pairPlaced < pairTarget; ++ai) {
            const int rw = std::uniform_int_distribution<int>(std::max(6, minSide), std::max(std::max(6, minSide), maxSide))(rng);
            const int rh = std::uniform_int_distribution<int>(std::max(6, minSide), std::max(std::max(6, minSide), maxSide))(rng);
            if (rw + border * 2 + 2 >= w || rh + border * 2 + 2 >= h) {
                continue;
            }

            const int leftMaxX0 = cx - rw - 2;
            if (leftMaxX0 <= border) {
                continue;
            }

            const int x0 = std::uniform_int_distribution<int>(border, leftMaxX0)(rng);
            const int y0 = std::uniform_int_distribution<int>(border, h - rh - border - 1)(rng);
            Rect left{x0, y0, x0 + rw, y0 + rh};
            if (!canPlaceAgainstExisting(left)) {
                continue;
            }

            int rx0 = 2 * cx - left.x1;
            int ry0 = y0;
            int rrw = rw;
            int rrh = rh;
            if (ur(rng) > mirrorXApproxRate) {
                rx0 += std::uniform_int_distribution<int>(-2, 2)(rng);
                ry0 += std::uniform_int_distribution<int>(-2, 2)(rng);
                rrw = std::clamp(rw + std::uniform_int_distribution<int>(-1, 1)(rng), std::max(6, minSide), maxSide);
                rrh = std::clamp(rh + std::uniform_int_distribution<int>(-1, 1)(rng), std::max(6, minSide), maxSide);
            }

            Rect right{rx0, ry0, rx0 + rrw, ry0 + rrh};
            if (!canPlaceAgainstExisting(right)) {
                continue;
            }
            if (RectOverlapWithMargin(left, right, roomPlacementMargin)) {
                continue;
            }

            (void)tryAddRoom(left, false, roomPlacementMargin, true, -1);
            (void)tryAddRoom(right, false, roomPlacementMargin, true, -1);
            ++pairPlaced;
        }

        int centerPlaced = 0;
        for (int ci = 0; ci < 64 && centerPlaced < centerTarget; ++ci) {
            const int rw = std::uniform_int_distribution<int>(std::max(6, minSide), std::max(std::max(6, minSide), maxSide))(rng);
            const int rh = std::uniform_int_distribution<int>(std::max(6, minSide), std::max(std::max(6, minSide), maxSide))(rng);
            if (rw + border * 2 + 2 >= w || rh + border * 2 + 2 >= h) {
                continue;
            }

            const int y0Min = std::max(border, cy - rh + 2);
            const int y0Max = std::min(h - rh - border - 1, cy - 1);
            if (y0Min > y0Max) {
                continue;
            }

            const int x0 = std::uniform_int_distribution<int>(border, w - rw - border - 1)(rng);
            const int y0 = std::uniform_int_distribution<int>(y0Min, y0Max)(rng);
            Rect center{x0, y0, x0 + rw, y0 + rh};
            if (!canPlaceAgainstExisting(center)) {
                continue;
            }

            (void)tryAddRoom(center, false, roomPlacementMargin, true, -1);
            ++centerPlaced;
        }
    }

    int linearWingBuildTarget = 0;
    if (linearWingChance <= 1.0f) {
        linearWingBuildTarget = (ur(rng) < linearWingChance) ? 1 : 0;
    } else {
        const int baseCount = static_cast<int>(std::floor(linearWingChance));
        const float frac = linearWingChance - static_cast<float>(baseCount);
        linearWingBuildTarget = baseCount + ((ur(rng) < frac) ? 1 : 0);
    }

    for (int wingBuild = 0; wingBuild < linearWingBuildTarget; ++wingBuild) {
        const int slots = std::uniform_int_distribution<int>(linearWingRoomMin, linearWingRoomMax)(rng);
        const int axisMode = (linearWingAxis == 0) ? ((ur(rng) < 0.7f) ? 1 : 2) : linearWingAxis;
        const bool verticalSpine = (axisMode == 1);
        int sideMode = 0; // 0:left/up, 1:right/down, 2:both
        if (linearWingSide == 0) {
            sideMode = std::uniform_int_distribution<int>(0, 2)(rng);
        } else if (linearWingSide == 1) {
            sideMode = 0;
        } else if (linearWingSide == 2) {
            sideMode = 1;
        } else {
            sideMode = 2;
        }
        const int linearMargin = 1;
        const int wallGap = 1;
        const int corridorHalfNeg = std::max(0, (corridorWidth - 1) / 2);
        const int corridorHalfPos = std::max(0, corridorWidth / 2);

        const int baseWMin = std::max(6, minSide);
        const int baseWMax = std::max(baseWMin, std::min(maxSide, baseWMin + 4));
        const int baseHMin = std::max(6, minSide);
        const int baseHMax = std::max(baseHMin, std::min(maxSide, baseHMin + 4));

        bool built = false;
        for (int wingTry = 0; wingTry < 10 && !built; ++wingTry) {
            const int roomBaseW = std::uniform_int_distribution<int>(baseWMin, baseWMax)(rng);
            const int roomBaseH = std::uniform_int_distribution<int>(baseHMin, baseHMax)(rng);
            const bool allowUneven = (ur(rng) < 0.24f);

            int spinePos = 0;
            int spineStart = 0;
            int spineEnd = 0;

            if (verticalSpine) {
                const int leftNeed = roomBaseW + 2;
                const int rightNeed = roomBaseW + 2;
                const int minX = border + leftNeed;
                const int maxX = w - border - rightNeed - 1;
                if (minX >= maxX) {
                    continue;
                }
                spinePos = std::uniform_int_distribution<int>(minX, maxX)(rng);
            } else {
                const int upNeed = roomBaseH + 2;
                const int downNeed = roomBaseH + 2;
                const int minY = border + upNeed;
                const int maxY = h - border - downNeed - 1;
                if (minY >= maxY) {
                    continue;
                }
                spinePos = std::uniform_int_distribution<int>(minY, maxY)(rng);
            }

            std::vector<Rect> candidate;
            auto canPlaceCandidate = [&](const Rect& r) {
                if (r.x0 < border || r.y0 < border || r.x1 >= w - border || r.y1 >= h - border) {
                    return false;
                }
                for (const Room& rm : rooms) {
                    if (RectOverlapWithMargin(r, rm.rect, linearMargin)) {
                        return false;
                    }
                }
                for (const Rect& cr : candidate) {
                    if (RectOverlapWithMargin(r, cr, linearMargin)) {
                        return false;
                    }
                }
                return true;
            };

            if (verticalSpine) {
                int totalH = 0;
                std::vector<int> slotHeights;
                slotHeights.reserve(static_cast<std::size_t>(slots));
                for (int si = 0; si < slots; ++si) {
                    const int rh = std::clamp(roomBaseH + (allowUneven ? std::uniform_int_distribution<int>(-2, 2)(rng) : 0), std::max(6, minSide), maxSide);
                    slotHeights.push_back(rh);
                    totalH += rh;
                    if (si + 1 < slots) {
                        totalH += wallGap;
                    }
                }

                if (totalH + border * 2 + 2 >= h) {
                    continue;
                }
                const int yMin = border + 1;
                const int yMax = h - border - totalH - 1;
                if (yMin > yMax) {
                    continue;
                }
                int yCursor = std::uniform_int_distribution<int>(yMin, yMax)(rng);
                spineStart = yCursor;

                bool failed = false;
                for (int si = 0; si < slots; ++si) {
                    const int rh = slotHeights[static_cast<std::size_t>(si)];
                    const int rw = std::clamp(roomBaseW + (allowUneven ? std::uniform_int_distribution<int>(-1, 1)(rng) : 0), std::max(6, minSide), maxSide);
                    const int y0 = yCursor;
                    const int y1 = y0 + rh;

                    if (sideMode == 0 || sideMode == 2) {
                        const int x1 = spinePos - corridorHalfNeg - 1;
                        Rect l{x1 - rw, y0, x1, y1};
                        if (!canPlaceCandidate(l)) {
                            failed = true;
                            break;
                        }
                        candidate.push_back(l);
                    }
                    if (sideMode == 1 || sideMode == 2) {
                        const int x0 = spinePos + corridorHalfPos + 2;
                        Rect r{x0, y0, x0 + rw, y1};
                        if (!canPlaceCandidate(r)) {
                            failed = true;
                            break;
                        }
                        candidate.push_back(r);
                    }

                    yCursor = y1 + wallGap;
                }

                if (failed) {
                    continue;
                }
                // Rect end is exclusive; convert to inclusive spine endpoint.
                spineEnd = yCursor - wallGap - 1;
            } else {
                int totalW = 0;
                std::vector<int> slotWidths;
                slotWidths.reserve(static_cast<std::size_t>(slots));
                for (int si = 0; si < slots; ++si) {
                    const int rw = std::clamp(roomBaseW + (allowUneven ? std::uniform_int_distribution<int>(-2, 2)(rng) : 0), std::max(6, minSide), maxSide);
                    slotWidths.push_back(rw);
                    totalW += rw;
                    if (si + 1 < slots) {
                        totalW += wallGap;
                    }
                }

                if (totalW + border * 2 + 2 >= w) {
                    continue;
                }
                const int xMin = border + 1;
                const int xMax = w - border - totalW - 1;
                if (xMin > xMax) {
                    continue;
                }
                int xCursor = std::uniform_int_distribution<int>(xMin, xMax)(rng);
                spineStart = xCursor;

                bool failed = false;
                for (int si = 0; si < slots; ++si) {
                    const int rw = slotWidths[static_cast<std::size_t>(si)];
                    const int rh = std::clamp(roomBaseH + (allowUneven ? std::uniform_int_distribution<int>(-1, 1)(rng) : 0), std::max(6, minSide), maxSide);
                    const int x0 = xCursor;
                    const int x1 = x0 + rw;

                    if (sideMode == 0 || sideMode == 2) {
                        const int y1 = spinePos - corridorHalfNeg - 1;
                        Rect u{x0, y1 - rh, x1, y1};
                        if (!canPlaceCandidate(u)) {
                            failed = true;
                            break;
                        }
                        candidate.push_back(u);
                    }
                    if (sideMode == 1 || sideMode == 2) {
                        const int y0 = spinePos + corridorHalfPos + 2;
                        Rect d{x0, y0, x1, y0 + rh};
                        if (!canPlaceCandidate(d)) {
                            failed = true;
                            break;
                        }
                        candidate.push_back(d);
                    }

                    xCursor = x1 + wallGap;
                }

                if (failed) {
                    continue;
                }
                // Rect end is exclusive; convert to inclusive spine endpoint.
                spineEnd = xCursor - wallGap - 1;
            }

            if (candidate.size() < 2) {
                continue;
            }

            std::vector<int> localIndices;
            localIndices.reserve(candidate.size());
            const int wingId = wingBuild;
            for (const Rect& r : candidate) {
                const std::size_t before = rooms.size();
                if (tryAddRoom(r, false, linearMargin, true, wingId) && rooms.size() > before) {
                    localIndices.push_back(static_cast<int>(rooms.size()) - 1);
                }
            }

            if (localIndices.size() < 2) {
                continue;
            }

            linearWingLayouts.push_back({std::move(localIndices), verticalSpine ? 0 : 1, spinePos, spineStart, spineEnd});
            built = true;
        }
    }

    const int placementAttempts = std::max(800, n / 5);
    for (int attempt = 0; attempt < placementAttempts && carvedRoomArea < targetRoomArea; ++attempt) {
        if (static_cast<int>(rooms.size()) >= roomCountMax) {
            break;
        }

        int rw = std::uniform_int_distribution<int>(minSide, maxSide)(rng);
        int rh = std::uniform_int_distribution<int>(minSide, maxSide)(rng);
        if (ur(rng) < largeRoomChance) {
            rw = std::min(maxSide + std::max(2, maxSide / 3), w - border * 2 - 2);
            rh = std::min(maxSide + std::max(2, maxSide / 3), h - border * 2 - 2);
        }

        if (rw + border * 2 + 2 >= w || rh + border * 2 + 2 >= h) {
            continue;
        }

        int x0 = border;
        int y0 = border;
        if (mode == LayoutMode::Ring) {
            const float ang = ur(rng) * 6.2831853f;
            const float rr = 0.22f + ur(rng) * 0.26f;
            const int cx = static_cast<int>(std::round(static_cast<float>(w) * 0.5f + std::cos(ang) * static_cast<float>(w) * rr));
            const int cy = static_cast<int>(std::round(static_cast<float>(h) * 0.5f + std::sin(ang) * static_cast<float>(h) * rr));
            x0 = std::clamp(cx - rw / 2, border, w - rw - border - 1);
            y0 = std::clamp(cy - rh / 2, border, h - rh - border - 1);
        } else {
            x0 = std::uniform_int_distribution<int>(border, w - rw - border - 1)(rng);
            y0 = std::uniform_int_distribution<int>(border, h - rh - border - 1)(rng);
        }

        Rect r{x0, y0, x0 + rw, y0 + rh};
        if (!canPlaceRect(r, roomPlacementMargin)) {
            continue;
        }

        const bool key = (mode == LayoutMode::Hub) ? (ur(rng) < 0.18f) : (ur(rng) < 0.12f);
        (void)tryAddRoom(r, key, roomPlacementMargin, false, -1);
    }

    if (static_cast<int>(rooms.size()) < roomCountMin) {
        outError = "Failed to place enough rooms";
        return false;
    }

    auto canResizeRoomRect = [&](int idx, const Rect& candidate) {
        if (candidate.x0 < border || candidate.y0 < border || candidate.x1 >= w - border || candidate.y1 >= h - border) {
            return false;
        }
        if (candidate.Width() < minSide || candidate.Height() < minSide) {
            return false;
        }
        for (int j = 0; j < static_cast<int>(rooms.size()); ++j) {
            if (j == idx) {
                continue;
            }
            if (RectOverlapWithMargin(candidate, rooms[static_cast<std::size_t>(j)].rect, 1)) {
                return false;
            }
        }
        return true;
    };

    auto tryFixDoubleWallGap = [&](int ia, int ib) {
        Rect& a = rooms[static_cast<std::size_t>(ia)].rect;
        Rect& b = rooms[static_cast<std::size_t>(ib)].rect;

        const int yOverlap = std::min(a.y1, b.y1) - std::max(a.y0, b.y0);
        if (yOverlap >= 2) {
            if (a.x1 <= b.x0) {
                const int gap = b.x0 - a.x1;
                if (gap == 2) {
                    const bool preferA = (a.Width() * a.Height()) <= (b.Width() * b.Height());
                    if (preferA) {
                        Rect grownA = a;
                        grownA.x1 += 1;
                        if (canResizeRoomRect(ia, grownA)) {
                            a = grownA;
                            return true;
                        }
                        Rect grownB = b;
                        grownB.x0 -= 1;
                        if (canResizeRoomRect(ib, grownB)) {
                            b = grownB;
                            return true;
                        }
                    } else {
                        Rect grownB = b;
                        grownB.x0 -= 1;
                        if (canResizeRoomRect(ib, grownB)) {
                            b = grownB;
                            return true;
                        }
                        Rect grownA = a;
                        grownA.x1 += 1;
                        if (canResizeRoomRect(ia, grownA)) {
                            a = grownA;
                            return true;
                        }
                    }
                }
            } else if (b.x1 <= a.x0) {
                const int gap = a.x0 - b.x1;
                if (gap == 2) {
                    const bool preferA = (a.Width() * a.Height()) <= (b.Width() * b.Height());
                    if (preferA) {
                        Rect grownA = a;
                        grownA.x0 -= 1;
                        if (canResizeRoomRect(ia, grownA)) {
                            a = grownA;
                            return true;
                        }
                        Rect grownB = b;
                        grownB.x1 += 1;
                        if (canResizeRoomRect(ib, grownB)) {
                            b = grownB;
                            return true;
                        }
                    } else {
                        Rect grownB = b;
                        grownB.x1 += 1;
                        if (canResizeRoomRect(ib, grownB)) {
                            b = grownB;
                            return true;
                        }
                        Rect grownA = a;
                        grownA.x0 -= 1;
                        if (canResizeRoomRect(ia, grownA)) {
                            a = grownA;
                            return true;
                        }
                    }
                }
            }
        }

        const int xOverlap = std::min(a.x1, b.x1) - std::max(a.x0, b.x0);
        if (xOverlap >= 2) {
            if (a.y1 <= b.y0) {
                const int gap = b.y0 - a.y1;
                if (gap == 2) {
                    const bool preferA = (a.Width() * a.Height()) <= (b.Width() * b.Height());
                    if (preferA) {
                        Rect grownA = a;
                        grownA.y1 += 1;
                        if (canResizeRoomRect(ia, grownA)) {
                            a = grownA;
                            return true;
                        }
                        Rect grownB = b;
                        grownB.y0 -= 1;
                        if (canResizeRoomRect(ib, grownB)) {
                            b = grownB;
                            return true;
                        }
                    } else {
                        Rect grownB = b;
                        grownB.y0 -= 1;
                        if (canResizeRoomRect(ib, grownB)) {
                            b = grownB;
                            return true;
                        }
                        Rect grownA = a;
                        grownA.y1 += 1;
                        if (canResizeRoomRect(ia, grownA)) {
                            a = grownA;
                            return true;
                        }
                    }
                }
            } else if (b.y1 <= a.y0) {
                const int gap = a.y0 - b.y1;
                if (gap == 2) {
                    const bool preferA = (a.Width() * a.Height()) <= (b.Width() * b.Height());
                    if (preferA) {
                        Rect grownA = a;
                        grownA.y0 -= 1;
                        if (canResizeRoomRect(ia, grownA)) {
                            a = grownA;
                            return true;
                        }
                        Rect grownB = b;
                        grownB.y1 += 1;
                        if (canResizeRoomRect(ib, grownB)) {
                            b = grownB;
                            return true;
                        }
                    } else {
                        Rect grownB = b;
                        grownB.y1 += 1;
                        if (canResizeRoomRect(ib, grownB)) {
                            b = grownB;
                            return true;
                        }
                        Rect grownA = a;
                        grownA.y0 -= 1;
                        if (canResizeRoomRect(ia, grownA)) {
                            a = grownA;
                            return true;
                        }
                    }
                }
            }
        }

        return false;
    };

    bool wallGapFixed = false;
    for (int pass = 0; pass < 3; ++pass) {
        bool changed = false;
        for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
            for (int j = i + 1; j < static_cast<int>(rooms.size()); ++j) {
                if (tryFixDoubleWallGap(i, j)) {
                    changed = true;
                    wallGapFixed = true;
                }
            }
        }
        if (!changed) {
            break;
        }
    }

    if (wallGapFixed) {
        std::fill(outBiomes.begin(), outBiomes.end(), static_cast<std::uint8_t>(SEA));
        std::fill(roomMask.begin(), roomMask.end(), static_cast<std::uint8_t>(0));
        for (const Room& room : rooms) {
            carveRoom(room);
        }
    }

    if (mode == LayoutMode::Branch) {
        int maxArea = -1;
        int maxIdx = -1;
        for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
            const Rect& r = rooms[static_cast<std::size_t>(i)].rect;
            const int a = r.Width() * r.Height();
            if (a > maxArea) {
                maxArea = a;
                maxIdx = i;
            }
        }
        if (maxIdx >= 0) {
            rooms[static_cast<std::size_t>(maxIdx)].key = true;
        }
    }

    int centralRoomIdx = 0;
    int centralRoomScore = 1000000000;
    for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
        const int cx = rooms[static_cast<std::size_t>(i)].rect.CenterX();
        const int cy = rooms[static_cast<std::size_t>(i)].rect.CenterY();
        int score = std::abs(cx - w / 2) + std::abs(cy - h / 2);
        if (rooms[static_cast<std::size_t>(i)].key) {
            score -= std::max(6, std::min(w, h) / 8);
        }
        if (score < centralRoomScore) {
            centralRoomScore = score;
            centralRoomIdx = i;
        }
    }

    // Reapply the semantic floor after any room rebuild performed by the gap
    // fixer above.
    for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
        if (!rooms[static_cast<std::size_t>(i)].key && i != centralRoomIdx) continue;
        const Rect& r = rooms[static_cast<std::size_t>(i)].rect;
        for (int y = r.y0 + 2; y < r.y1 - 2; ++y) {
            for (int x = r.x0 + 2; x < r.x1 - 2; ++x) {
                const int cell = Index(x, y, w);
                if (outBiomes[static_cast<std::size_t>(cell)] == PLAIN ||
                    outBiomes[static_cast<std::size_t>(cell)] == DESERT) {
                    outBiomes[static_cast<std::size_t>(cell)] = FOREST;
                }
            }
        }
    }

    std::vector<int> desiredDoors(rooms.size(), 1);
    for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
        const Rect& r = rooms[static_cast<std::size_t>(i)].rect;
        const int area = r.Width() * r.Height();
        const int largeThreshold = std::max(90, static_cast<int>(std::round(static_cast<float>(minSide * minSide) * 1.9f)));
        const int hallThreshold = std::max(170, static_cast<int>(std::round(static_cast<float>(maxSide * maxSide) * 0.78f)));

        int d = 1;
        if (area >= largeThreshold || ur(rng) < 0.22f) {
            d = 2;
        }

        if (rooms[static_cast<std::size_t>(i)].key || i == centralRoomIdx) {
            d = std::max(d, 3);
            if (area >= hallThreshold || ur(rng) < std::clamp((keyRoomDoorBias - 1.0f) * 0.35f, 0.0f, 0.5f)) {
                d = 4;
            }
        }

        desiredDoors[static_cast<std::size_t>(i)] = std::clamp(d, 1, 4);
    }

    std::vector<Edge> edges;
    edges.reserve((rooms.size() * (rooms.size() - 1)) / 2);
    for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
        for (int j = i + 1; j < static_cast<int>(rooms.size()); ++j) {
            const int wi = roomLinearWingId[static_cast<std::size_t>(i)];
            const int wj = roomLinearWingId[static_cast<std::size_t>(j)];
            if (wi >= 0 && wj >= 0) {
                continue;
            }
            const int manhattan = std::abs(rooms[static_cast<std::size_t>(i)].rect.CenterX() - rooms[static_cast<std::size_t>(j)].rect.CenterX()) +
                std::abs(rooms[static_cast<std::size_t>(i)].rect.CenterY() - rooms[static_cast<std::size_t>(j)].rect.CenterY());
            int score = manhattan;
            if (mode == LayoutMode::Hub && (rooms[static_cast<std::size_t>(i)].key || rooms[static_cast<std::size_t>(j)].key)) {
                score = std::max(1, static_cast<int>(std::round(static_cast<float>(manhattan) * 0.62f)));
            } else if (mode == LayoutMode::Branch) {
                const int ci = std::abs(rooms[static_cast<std::size_t>(i)].rect.CenterX() - w / 2) + std::abs(rooms[static_cast<std::size_t>(i)].rect.CenterY() - h / 2);
                const int cj = std::abs(rooms[static_cast<std::size_t>(j)].rect.CenterX() - w / 2) + std::abs(rooms[static_cast<std::size_t>(j)].rect.CenterY() - h / 2);
                score += std::abs(ci - cj) / 2;
            }

            const int over = std::max(0, manhattan - preferredLinkLen);
            if (over > 0) {
                const int penaltyWeight = (mode == LayoutMode::Branch) ? 2 : 3;
                score += over * penaltyWeight;
            }
            edges.push_back({i, j, score});
        }
    }
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) { return a.d < b.d; });

    std::vector<int> selected;
    std::vector<std::uint8_t> selectedIsMst;
    std::vector<int> degree(rooms.size(), 0);
    std::vector<std::uint8_t> usedEdge(edges.size(), 0);
    DisjointSet ds(static_cast<int>(rooms.size()));

    for (int ei = 0; ei < static_cast<int>(edges.size()); ++ei) {
        const Edge& e = edges[static_cast<std::size_t>(ei)];
        if (ds.Unite(e.a, e.b)) {
            selected.push_back(ei);
            selectedIsMst.push_back(1);
            usedEdge[static_cast<std::size_t>(ei)] = 1;
            ++degree[static_cast<std::size_t>(e.a)];
            ++degree[static_cast<std::size_t>(e.b)];
        }
    }

    int freeDoorSlots = 0;
    for (int i = 0; i < static_cast<int>(degree.size()); ++i) {
        freeDoorSlots += std::max(0, desiredDoors[static_cast<std::size_t>(i)] - degree[static_cast<std::size_t>(i)]);
    }

    const int rawExtraEdges = static_cast<int>(std::round(static_cast<float>(selected.size()) * effectiveLoopRate));
    const int extraEdges = std::max(0, std::min(rawExtraEdges, freeDoorSlots / 2));
    int loopAdded = 0;
    for (int ei = 0; ei < static_cast<int>(edges.size()) && loopAdded < extraEdges; ++ei) {
        if (usedEdge[static_cast<std::size_t>(ei)] != 0) {
            continue;
        }
        const Edge& e = edges[static_cast<std::size_t>(ei)];
        if (degree[static_cast<std::size_t>(e.a)] >= desiredDoors[static_cast<std::size_t>(e.a)] ||
            degree[static_cast<std::size_t>(e.b)] >= desiredDoors[static_cast<std::size_t>(e.b)]) {
            continue;
        }
        usedEdge[static_cast<std::size_t>(ei)] = 1;
        selected.push_back(ei);
        selectedIsMst.push_back(0);
        ++degree[static_cast<std::size_t>(e.a)];
        ++degree[static_cast<std::size_t>(e.b)];
        ++loopAdded;
    }

    bool improved = true;
    while (improved) {
        improved = false;
        for (int ei = 0; ei < static_cast<int>(edges.size()); ++ei) {
            if (usedEdge[static_cast<std::size_t>(ei)] != 0) {
                continue;
            }
            const Edge& e = edges[static_cast<std::size_t>(ei)];
            if (degree[static_cast<std::size_t>(e.a)] >= desiredDoors[static_cast<std::size_t>(e.a)] ||
                degree[static_cast<std::size_t>(e.b)] >= desiredDoors[static_cast<std::size_t>(e.b)]) {
                continue;
            }
            usedEdge[static_cast<std::size_t>(ei)] = 1;
            selected.push_back(ei);
            selectedIsMst.push_back(0);
            ++degree[static_cast<std::size_t>(e.a)];
            ++degree[static_cast<std::size_t>(e.b)];
            improved = true;
            break;
        }
    }

    std::vector<std::uint8_t> roadBlockMask(static_cast<std::size_t>(n), 0);

    auto carveDot = [&](int cx, int cy, int width, std::uint8_t tile) {
        const int hw0 = std::max(0, (width - 1) / 2);
        const int hw1 = std::max(0, width / 2);
        for (int y = cy - hw0; y <= cy + hw1; ++y) {
            for (int x = cx - hw0; x <= cx + hw1; ++x) {
                if (!InBounds(x, y, w, h)) {
                    continue;
                }
                const int i = Index(x, y, w);
                if (tile == ROAD && (roomMask[static_cast<std::size_t>(i)] != 0 || roadBlockMask[static_cast<std::size_t>(i)] != 0)) {
                    continue;
                }
                outBiomes[static_cast<std::size_t>(i)] = tile;
            }
        }
    };

    auto carveSegment = [&](Point a, Point b, int width, std::uint8_t tile) {
        const int hw0 = std::max(0, (width - 1) / 2);
        const int hw1 = std::max(0, width / 2);

        auto paintStrip = [&](int cx, int cy, bool vertical) {
            if (vertical) {
                for (int x = cx - hw0; x <= cx + hw1; ++x) {
                    if (!InBounds(x, cy, w, h)) {
                        continue;
                    }
                    const int i = Index(x, cy, w);
                    if (tile == ROAD && (roomMask[static_cast<std::size_t>(i)] != 0 || roadBlockMask[static_cast<std::size_t>(i)] != 0)) {
                        continue;
                    }
                    outBiomes[static_cast<std::size_t>(i)] = tile;
                }
            } else {
                for (int y = cy - hw0; y <= cy + hw1; ++y) {
                    if (!InBounds(cx, y, w, h)) {
                        continue;
                    }
                    const int i = Index(cx, y, w);
                    if (tile == ROAD && (roomMask[static_cast<std::size_t>(i)] != 0 || roadBlockMask[static_cast<std::size_t>(i)] != 0)) {
                        continue;
                    }
                    outBiomes[static_cast<std::size_t>(i)] = tile;
                }
            }
        };

        if (a.x == b.x) {
            const int y0 = std::min(a.y, b.y);
            const int y1 = std::max(a.y, b.y);
            for (int y = y0; y <= y1; ++y) {
                paintStrip(a.x, y, true);
            }
        } else {
            const int x0 = std::min(a.x, b.x);
            const int x1 = std::max(a.x, b.x);
            for (int x = x0; x <= x1; ++x) {
                paintStrip(x, a.y, false);
            }
        }
    };

    std::vector<std::uint8_t> protectedRoad(static_cast<std::size_t>(n), 0);
    auto markProtectedSegment = [&](Point a, Point b, int width) {
        const int hw0 = std::max(0, (width - 1) / 2);
        const int hw1 = std::max(0, width / 2);

        auto markStrip = [&](int cx, int cy, bool vertical) {
            if (vertical) {
                for (int x = cx - hw0; x <= cx + hw1; ++x) {
                    if (!InBounds(x, cy, w, h)) {
                        continue;
                    }
                    const int i = Index(x, cy, w);
                    if (roomMask[static_cast<std::size_t>(i)] != 0) {
                        continue;
                    }
                    protectedRoad[static_cast<std::size_t>(i)] = 1;
                }
            } else {
                for (int y = cy - hw0; y <= cy + hw1; ++y) {
                    if (!InBounds(cx, y, w, h)) {
                        continue;
                    }
                    const int i = Index(cx, y, w);
                    if (roomMask[static_cast<std::size_t>(i)] != 0) {
                        continue;
                    }
                    protectedRoad[static_cast<std::size_t>(i)] = 1;
                }
            }
        };

        if (a.x == b.x) {
            const int y0 = std::min(a.y, b.y);
            const int y1 = std::max(a.y, b.y);
            for (int y = y0; y <= y1; ++y) {
                markStrip(a.x, y, true);
            }
        } else {
            const int x0 = std::min(a.x, b.x);
            const int x1 = std::max(a.x, b.x);
            for (int x = x0; x <= x1; ++x) {
                markStrip(x, a.y, false);
            }
        }
    };

    std::vector<Point> allDoors;
    auto farEnoughDoor = [&](const Point& p) {
        for (const Point& q : allDoors) {
            if (std::abs(p.x - q.x) + std::abs(p.y - q.y) < doorMinInterval) {
                return false;
            }
        }
        return true;
    };

    auto makeDoor = [&](const Rect& from, const Rect& to) {
        const int cfx = from.CenterX();
        const int cfy = from.CenterY();
        const int ctx = to.CenterX();
        const int cty = to.CenterY();
        const int centerOffset = (doorLength - 1) / 2;
        const std::array<int, 7> offsets = {0, 1, -1, 2, -2, 3, -3};

        if (std::abs(ctx - cfx) > std::abs(cty - cfy)) {
            const bool right = ctx > cfx;
            const int x = right ? from.x1 : (from.x0 - 1);
            const int yStartMin = from.y0 + 1;
            const int yStartMax = std::max(yStartMin, from.y1 - doorLength - 1);
            const int yBase = std::clamp(cty - centerOffset, yStartMin, yStartMax);
            for (int off : offsets) {
                const int y = std::clamp(yBase + off, yStartMin, yStartMax);
                const DoorPair d{{x, y}, {x, y + (doorLength - 1)}, {x, y + centerOffset}};
                if (farEnoughDoor(d.c0) && farEnoughDoor(d.c1)) {
                    return d;
                }
            }
            return DoorPair{{x, yBase}, {x, yBase + (doorLength - 1)}, {x, yBase + centerOffset}};
        }

        const bool down = cty > cfy;
        const int y = down ? from.y1 : (from.y0 - 1);
        const int xStartMin = from.x0 + 1;
        const int xStartMax = std::max(xStartMin, from.x1 - doorLength - 1);
        const int xBase = std::clamp(ctx - centerOffset, xStartMin, xStartMax);
        for (int off : offsets) {
            const int x = std::clamp(xBase + off, xStartMin, xStartMax);
            const DoorPair d{{x, y}, {x + (doorLength - 1), y}, {x + centerOffset, y}};
            if (farEnoughDoor(d.c0) && farEnoughDoor(d.c1)) {
                return d;
            }
        }
        return DoorPair{{xBase, y}, {xBase + (doorLength - 1), y}, {xBase + centerOffset, y}};
    };

    auto isWallLike = [&](int x, int y) {
        if (!InBounds(x, y, w, h)) {
            return false;
        }
        const std::uint8_t b = outBiomes[static_cast<std::size_t>(Index(x, y, w))];
        return b == SEA || b == MOUNTAIN || b == EXMOUNTAIN;
    };

    auto isRoomFloorLike = [&](int x, int y) {
        if (!InBounds(x, y, w, h)) {
            return false;
        }
        const std::uint8_t b = outBiomes[static_cast<std::size_t>(Index(x, y, w))];
        return b == PLAIN || b == DESERT || b == FOREST;
    };

    auto isCorridorLike = [&](int x, int y) {
        if (!InBounds(x, y, w, h)) {
            return false;
        }
        const std::uint8_t b = outBiomes[static_cast<std::size_t>(Index(x, y, w))];
        return b == ROAD || b == BRIDGE;
    };

    auto tryPlaceDoorPair = [&](const DoorPair& d, bool /*forceBridge*/) {
        if (!InBounds(d.c0.x, d.c0.y, w, h) || !InBounds(d.c1.x, d.c1.y, w, h)) {
            return false;
        }

        const bool vertical = (d.c0.x == d.c1.x) && (std::abs(d.c0.y - d.c1.y) == (doorLength - 1));
        const bool horizontal = (d.c0.y == d.c1.y) && (std::abs(d.c0.x - d.c1.x) == (doorLength - 1));
        if (!vertical && !horizontal) {
            return false;
        }

        const bool c0WallSlot = isWallLike(d.c0.x, d.c0.y) || isCorridorLike(d.c0.x, d.c0.y);
        const bool c1WallSlot = isWallLike(d.c1.x, d.c1.y) || isCorridorLike(d.c1.x, d.c1.y);
        if (!c0WallSlot || !c1WallSlot) {
            return false;
        }

        bool sideWallsOk = false;
        if (vertical) {
            const int x = d.c0.x;
            const int y0 = std::min(d.c0.y, d.c1.y);
            const int y1 = std::max(d.c0.y, d.c1.y);
            sideWallsOk = isWallLike(x, y0 - 1) && isWallLike(x, y1 + 1);
        } else {
            const int y = d.c0.y;
            const int x0 = std::min(d.c0.x, d.c1.x);
            const int x1 = std::max(d.c0.x, d.c1.x);
            sideWallsOk = isWallLike(x0 - 1, y) && isWallLike(x1 + 1, y);
        }
        if (!sideWallsOk) {
            return false;
        }

        std::array<Point, 4> doorCells{};
        if (vertical) {
            const int x = d.c0.x;
            const int y0 = std::min(d.c0.y, d.c1.y);
            for (int k = 0; k < doorLength; ++k) {
                doorCells[static_cast<std::size_t>(k)] = {x, y0 + k};
            }
        } else {
            const int y = d.c0.y;
            const int x0 = std::min(d.c0.x, d.c1.x);
            for (int k = 0; k < doorLength; ++k) {
                doorCells[static_cast<std::size_t>(k)] = {x0 + k, y};
            }
        }

        for (const Point& dc : doorCells) {
            if (!InBounds(dc.x, dc.y, w, h)) {
                return false;
            }
            if (!isWallLike(dc.x, dc.y) && !isCorridorLike(dc.x, dc.y)) {
                return false;
            }
        }

        int nearRoom = 0;
        int nearCorridor = 0;
        const std::array<Point, 4> n4 = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        for (const Point& dc : doorCells) {
            if (isCorridorLike(dc.x, dc.y)) {
                ++nearCorridor;
            }
            for (const Point& n : n4) {
                const int nx = dc.x + n.x;
                const int ny = dc.y + n.y;
                if (isRoomFloorLike(nx, ny)) {
                    ++nearRoom;
                }
                if (isCorridorLike(nx, ny)) {
                    ++nearCorridor;
                }
            }
        }
        if (nearRoom == 0 || nearCorridor == 0) {
            return false;
        }

        const std::uint8_t doorTile = static_cast<std::uint8_t>(BRIDGE);
        for (const Point& dc : doorCells) {
            outBiomes[static_cast<std::size_t>(Index(dc.x, dc.y, w))] = doorTile;
        }
        return true;
    };

    auto forcePlaceDoorPair = [&](const DoorPair& d) {
        const bool vertical = (d.c0.x == d.c1.x) && (std::abs(d.c0.y - d.c1.y) == (doorLength - 1));
        const bool horizontal = (d.c0.y == d.c1.y) && (std::abs(d.c0.x - d.c1.x) == (doorLength - 1));
        if (!vertical && !horizontal) {
            return false;
        }

        std::array<Point, 4> doorCells{};
        if (vertical) {
            const int x = d.c0.x;
            const int y0 = std::min(d.c0.y, d.c1.y);
            for (int k = 0; k < doorLength; ++k) {
                doorCells[static_cast<std::size_t>(k)] = {x, y0 + k};
            }
        } else {
            const int y = d.c0.y;
            const int x0 = std::min(d.c0.x, d.c1.x);
            for (int k = 0; k < doorLength; ++k) {
                doorCells[static_cast<std::size_t>(k)] = {x0 + k, y};
            }
        }

        for (const Point& dc : doorCells) {
            if (!InBounds(dc.x, dc.y, w, h)) {
                return false;
            }
        }

        for (const Point& dc : doorCells) {
            outBiomes[static_cast<std::size_t>(Index(dc.x, dc.y, w))] = static_cast<std::uint8_t>(BRIDGE);
        }
        return true;
    };

    auto isNearRoomInterior = [&](const Point& p, int radius) {
        for (int oy = -radius; oy <= radius; ++oy) {
            for (int ox = -radius; ox <= radius; ++ox) {
                const int nx = p.x + ox;
                const int ny = p.y + oy;
                if (!InBounds(nx, ny, w, h)) {
                    continue;
                }
                if (roomMask[static_cast<std::size_t>(Index(nx, ny, w))] != 0) {
                    return true;
                }
            }
        }
        return false;
    };

    auto canCarveStraightCorridor = [&](Point a, Point b, int width) {
        if (a.x != b.x && a.y != b.y) {
            return false;
        }
        const int hw0 = std::max(0, (width - 1) / 2);
        const int hw1 = std::max(0, width / 2);
        if (a.x == b.x) {
            const int y0 = std::min(a.y, b.y);
            const int y1 = std::max(a.y, b.y);
            for (int y = y0; y <= y1; ++y) {
                for (int xx = a.x - hw0; xx <= a.x + hw1; ++xx) {
                    if (!InBounds(xx, y, w, h)) {
                        return false;
                    }
                    const int i = Index(xx, y, w);
                    if (roomMask[static_cast<std::size_t>(i)] != 0 || roadBlockMask[static_cast<std::size_t>(i)] != 0) {
                        return false;
                    }
                }
            }
            return true;
        }

        const int x0 = std::min(a.x, b.x);
        const int x1 = std::max(a.x, b.x);
        for (int x = x0; x <= x1; ++x) {
            for (int yy = a.y - hw0; yy <= a.y + hw1; ++yy) {
                if (!InBounds(x, yy, w, h)) {
                    return false;
                }
                const int i = Index(x, yy, w);
                if (roomMask[static_cast<std::size_t>(i)] != 0 || roadBlockMask[static_cast<std::size_t>(i)] != 0) {
                    return false;
                }
            }
        }
        return true;
    };

    auto tryMakeStraightDoorPairs = [&](const Rect& ra, const Rect& rb, int edgeCorridorWidth, DoorPair& outA, DoorPair& outB) {
        const std::array<int, 7> offsets = {0, 1, -1, 2, -2, 3, -3};
        const int cax = ra.CenterX();
        const int cay = ra.CenterY();
        const int cbx = rb.CenterX();
        const int cby = rb.CenterY();

        auto tryHorizontal = [&](const Rect& left, const Rect& right, bool aIsLeft, bool requireDoorSpacing) {
            const int xL = left.x1;
            const int xR = right.x0 - 1;
            if (xL >= xR) {
                return false;
            }
            const int yMin = std::max(left.y0 + 1, right.y0 + 1);
            const int yMax = std::min(left.y1 - doorLength - 1, right.y1 - doorLength - 1);
            if (yMin > yMax) {
                return false;
            }

            const int yBase = std::clamp((left.CenterY() + right.CenterY()) / 2, yMin, yMax);
            for (int off : offsets) {
                const int y = std::clamp(yBase + off, yMin, yMax);
                DoorPair dLeft{{xL, y}, {xL, y + (doorLength - 1)}, {xL, y + (doorLength - 1) / 2}};
                DoorPair dRight{{xR, y}, {xR, y + (doorLength - 1)}, {xR, y + (doorLength - 1) / 2}};
                if (requireDoorSpacing && (!farEnoughDoor(dLeft.c0) || !farEnoughDoor(dLeft.c1) || !farEnoughDoor(dRight.c0) || !farEnoughDoor(dRight.c1))) {
                    continue;
                }
                if (!canCarveStraightCorridor(dLeft.center, dRight.center, edgeCorridorWidth)) {
                    continue;
                }
                if (aIsLeft) {
                    outA = dLeft;
                    outB = dRight;
                } else {
                    outA = dRight;
                    outB = dLeft;
                }
                return true;
            }
            return false;
        };

        auto tryVertical = [&](const Rect& top, const Rect& bottom, bool aIsTop, bool requireDoorSpacing) {
            const int yT = top.y1;
            const int yB = bottom.y0 - 1;
            if (yT >= yB) {
                return false;
            }
            const int xMin = std::max(top.x0 + 1, bottom.x0 + 1);
            const int xMax = std::min(top.x1 - doorLength - 1, bottom.x1 - doorLength - 1);
            if (xMin > xMax) {
                return false;
            }

            const int xBase = std::clamp((top.CenterX() + bottom.CenterX()) / 2, xMin, xMax);
            for (int off : offsets) {
                const int x = std::clamp(xBase + off, xMin, xMax);
                DoorPair dTop{{x, yT}, {x + (doorLength - 1), yT}, {x + (doorLength - 1) / 2, yT}};
                DoorPair dBottom{{x, yB}, {x + (doorLength - 1), yB}, {x + (doorLength - 1) / 2, yB}};
                if (requireDoorSpacing && (!farEnoughDoor(dTop.c0) || !farEnoughDoor(dTop.c1) || !farEnoughDoor(dBottom.c0) || !farEnoughDoor(dBottom.c1))) {
                    continue;
                }
                if (!canCarveStraightCorridor(dTop.center, dBottom.center, edgeCorridorWidth)) {
                    continue;
                }
                if (aIsTop) {
                    outA = dTop;
                    outB = dBottom;
                } else {
                    outA = dBottom;
                    outB = dTop;
                }
                return true;
            }
            return false;
        };

        auto tryByPriority = [&](bool requireDoorSpacing) {
            const bool horizontalFirst = std::abs(cbx - cax) >= std::abs(cby - cay);
            if (horizontalFirst) {
                if (cax <= cbx) {
                    if (tryHorizontal(ra, rb, true, requireDoorSpacing)) {
                        return true;
                    }
                } else {
                    if (tryHorizontal(rb, ra, false, requireDoorSpacing)) {
                        return true;
                    }
                }
                if (cay <= cby) {
                    return tryVertical(ra, rb, true, requireDoorSpacing);
                }
                return tryVertical(rb, ra, false, requireDoorSpacing);
            }

            if (cay <= cby) {
                if (tryVertical(ra, rb, true, requireDoorSpacing)) {
                    return true;
                }
            } else {
                if (tryVertical(rb, ra, false, requireDoorSpacing)) {
                    return true;
                }
            }
            if (cax <= cbx) {
                return tryHorizontal(ra, rb, true, requireDoorSpacing);
            }
            return tryHorizontal(rb, ra, false, requireDoorSpacing);
        };

        // First pass respects global door spacing. Second pass relaxes spacing to keep
        // room-to-room axis aligned when a straight corridor is otherwise possible.
        if (tryByPriority(true)) {
            return true;
        }
        return tryByPriority(false);
    };

    std::vector<int> roomDoorCount(rooms.size(), 0);

    if (mode == LayoutMode::Hub) {
        int hubIdx = -1;
        for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
            if (rooms[static_cast<std::size_t>(i)].key) {
                hubIdx = i;
                break;
            }
        }
        if (hubIdx >= 0) {
            std::array<int, 4> best = {-1, -1, -1, -1};
            std::array<int, 4> bestD = {1000000, 1000000, 1000000, 1000000};
            const int hx = rooms[static_cast<std::size_t>(hubIdx)].rect.CenterX();
            const int hy = rooms[static_cast<std::size_t>(hubIdx)].rect.CenterY();
            for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
                if (i == hubIdx) {
                    continue;
                }
                const int x = rooms[static_cast<std::size_t>(i)].rect.CenterX();
                const int y = rooms[static_cast<std::size_t>(i)].rect.CenterY();
                int q = 0;
                if (x >= hx && y < hy) {
                    q = 0;
                } else if (x < hx && y < hy) {
                    q = 1;
                } else if (x < hx && y >= hy) {
                    q = 2;
                } else {
                    q = 3;
                }
                const int d = std::abs(x - hx) + std::abs(y - hy);
                if (d < bestD[static_cast<std::size_t>(q)]) {
                    bestD[static_cast<std::size_t>(q)] = d;
                    best[static_cast<std::size_t>(q)] = i;
                }
            }

            auto edgeExists = [&](int a, int b) {
                for (int sel : selected) {
                    const Edge& e = edges[static_cast<std::size_t>(sel)];
                    if ((e.a == a && e.b == b) || (e.a == b && e.b == a)) {
                        return true;
                    }
                }
                return false;
            };

            for (int i = 0; i < 4; ++i) {
                const int t = best[static_cast<std::size_t>(i)];
                if (t < 0 || edgeExists(hubIdx, t)) {
                    continue;
                }
                if (degree[static_cast<std::size_t>(hubIdx)] >= desiredDoors[static_cast<std::size_t>(hubIdx)] ||
                    degree[static_cast<std::size_t>(t)] >= desiredDoors[static_cast<std::size_t>(t)]) {
                    continue;
                }
                edges.push_back({hubIdx, t, 1});
                selected.push_back(static_cast<int>(edges.size()) - 1);
                selectedIsMst.push_back(1);
                ++degree[static_cast<std::size_t>(hubIdx)];
                ++degree[static_cast<std::size_t>(t)];
            }
        }
    }

    const int doorCornerPadding = 2;
    const int minBentLegLength = 8;
    auto doorCornerClearance = [&](const Rect& room, const DoorPair& d) {
        if (d.c0.x == d.c1.x) {
            const int y0 = std::min(d.c0.y, d.c1.y);
            const int y1 = std::max(d.c0.y, d.c1.y);
            return std::min(y0 - room.y0, (room.y1 - 1) - y1);
        }
        const int x0 = std::min(d.c0.x, d.c1.x);
        const int x1 = std::max(d.c0.x, d.c1.x);
        return std::min(x0 - room.x0, (room.x1 - 1) - x1);
    };

    auto adjustDoorAwayFromCorners = [&](const Rect& from, const Rect& to, DoorPair& d) {
        const int cfx = from.CenterX();
        const int cfy = from.CenterY();
        const int ctx = to.CenterX();
        const int cty = to.CenterY();
        const int centerOffset = (doorLength - 1) / 2;

        if (d.c0.x == d.c1.x) {
            const bool right = ctx > cfx;
            const int x = right ? from.x1 : (from.x0 - 1);
            const int relaxedMin = from.y0 + 1;
            const int relaxedMax = std::max(relaxedMin, from.y1 - doorLength - 1);
            int yMin = from.y0 + doorCornerPadding;
            int yMax = from.y1 - doorLength - doorCornerPadding;
            if (yMin > yMax) {
                yMin = relaxedMin;
                yMax = relaxedMax;
            }

            bool found = false;
            DoorPair best = d;
            int bestScore = 1000000;
            for (int y = yMin; y <= yMax; ++y) {
                DoorPair cand{{x, y}, {x, y + (doorLength - 1)}, {x, y + centerOffset}};
                if (!farEnoughDoor(cand.c0) || !farEnoughDoor(cand.c1)) {
                    continue;
                }
                const int score = std::abs((y + centerOffset) - cty);
                if (!found || score < bestScore) {
                    found = true;
                    best = cand;
                    bestScore = score;
                }
            }
            if (found) {
                d = best;
            }
            return;
        }

        const bool down = cty > cfy;
        const int y = down ? from.y1 : (from.y0 - 1);
        const int relaxedMin = from.x0 + 1;
        const int relaxedMax = std::max(relaxedMin, from.x1 - doorLength - 1);
        int xMin = from.x0 + doorCornerPadding;
        int xMax = from.x1 - doorLength - doorCornerPadding;
        if (xMin > xMax) {
            xMin = relaxedMin;
            xMax = relaxedMax;
        }

        bool found = false;
        DoorPair best = d;
        int bestScore = 1000000;
        for (int x = xMin; x <= xMax; ++x) {
            DoorPair cand{{x, y}, {x + (doorLength - 1), y}, {x + centerOffset, y}};
            if (!farEnoughDoor(cand.c0) || !farEnoughDoor(cand.c1)) {
                continue;
            }
            const int score = std::abs((x + centerOffset) - ctx);
            if (!found || score < bestScore) {
                found = true;
                best = cand;
                bestScore = score;
            }
        }
        if (found) {
            d = best;
        }
    };

    auto tryMakeBentDoorPairs = [&](const Rect& ra, const Rect& rb, int edgeCorridorWidth, DoorPair& outA, DoorPair& outB, Point& outCorner) {
        const std::array<int, 9> offsets = {0, 1, -1, 2, -2, 3, -3, 4, -4};
        const int centerOffset = (doorLength - 1) / 2;

        auto buildDoorWithAxis = [&](const Rect& from, const Point& toward, bool wantVertical, bool requireDoorSpacing, DoorPair& out) {
            if (wantVertical) {
                const int x = toward.x > from.CenterX() ? from.x1 : (from.x0 - 1);
                const int relaxedMin = from.y0 + 1;
                const int relaxedMax = std::max(relaxedMin, from.y1 - doorLength - 1);
                int yMin = from.y0 + doorCornerPadding;
                int yMax = from.y1 - doorLength - doorCornerPadding;
                if (yMin > yMax) {
                    yMin = relaxedMin;
                    yMax = relaxedMax;
                }
                if (yMin > yMax) {
                    return false;
                }
                const int yBase = std::clamp(toward.y - centerOffset, yMin, yMax);
                for (int off : offsets) {
                    const int y = std::clamp(yBase + off, yMin, yMax);
                    DoorPair cand{{x, y}, {x, y + (doorLength - 1)}, {x, y + centerOffset}};
                    if (requireDoorSpacing && (!farEnoughDoor(cand.c0) || !farEnoughDoor(cand.c1))) {
                        continue;
                    }
                    out = cand;
                    return true;
                }

                bool found = false;
                DoorPair best{};
                int bestScore = 1000000;
                for (int y = yMin; y <= yMax; ++y) {
                    DoorPair cand{{x, y}, {x, y + (doorLength - 1)}, {x, y + centerOffset}};
                    if (requireDoorSpacing && (!farEnoughDoor(cand.c0) || !farEnoughDoor(cand.c1))) {
                        continue;
                    }
                    const int score = std::abs((y + centerOffset) - toward.y);
                    if (!found || score < bestScore) {
                        found = true;
                        best = cand;
                        bestScore = score;
                    }
                }
                if (!found) {
                    return false;
                }
                out = best;
                return true;
            }

            const int y = toward.y > from.CenterY() ? from.y1 : (from.y0 - 1);
            const int relaxedMin = from.x0 + 1;
            const int relaxedMax = std::max(relaxedMin, from.x1 - doorLength - 1);
            int xMin = from.x0 + doorCornerPadding;
            int xMax = from.x1 - doorLength - doorCornerPadding;
            if (xMin > xMax) {
                xMin = relaxedMin;
                xMax = relaxedMax;
            }
            if (xMin > xMax) {
                return false;
            }
            const int xBase = std::clamp(toward.x - centerOffset, xMin, xMax);
            for (int off : offsets) {
                const int x = std::clamp(xBase + off, xMin, xMax);
                DoorPair cand{{x, y}, {x + (doorLength - 1), y}, {x + centerOffset, y}};
                if (requireDoorSpacing && (!farEnoughDoor(cand.c0) || !farEnoughDoor(cand.c1))) {
                    continue;
                }
                out = cand;
                return true;
            }

            bool found = false;
            DoorPair best{};
            int bestScore = 1000000;
            for (int x = xMin; x <= xMax; ++x) {
                DoorPair cand{{x, y}, {x + (doorLength - 1), y}, {x + centerOffset, y}};
                if (requireDoorSpacing && (!farEnoughDoor(cand.c0) || !farEnoughDoor(cand.c1))) {
                    continue;
                }
                const int score = std::abs((x + centerOffset) - toward.x);
                if (!found || score < bestScore) {
                    found = true;
                    best = cand;
                    bestScore = score;
                }
            }
            if (!found) {
                return false;
            }
            out = best;
            return true;
        };

        auto tryPattern = [&](bool aVertical, bool requireDoorSpacing, DoorPair& outDa, DoorPair& outDb, Point& outC, int& outScore) {
            DoorPair daCand{};
            DoorPair dbCand{};
            const Point towardA{rb.CenterX(), rb.CenterY()};
            const Point towardB{ra.CenterX(), ra.CenterY()};
            if (!buildDoorWithAxis(ra, towardA, aVertical, requireDoorSpacing, daCand)) {
                return false;
            }
            if (!buildDoorWithAxis(rb, towardB, !aVertical, requireDoorSpacing, dbCand)) {
                return false;
            }

            Point corner{};
            if (aVertical) {
                corner = {dbCand.center.x, daCand.center.y};
            } else {
                corner = {daCand.center.x, dbCand.center.y};
            }
            if (!InBounds(corner.x, corner.y, w, h)) {
                return false;
            }

            if (!canCarveStraightCorridor(daCand.center, corner, edgeCorridorWidth)) {
                return false;
            }
            if (!canCarveStraightCorridor(corner, dbCand.center, edgeCorridorWidth)) {
                return false;
            }

            const int legA = std::abs(corner.x - daCand.center.x) + std::abs(corner.y - daCand.center.y);
            const int legB = std::abs(corner.x - dbCand.center.x) + std::abs(corner.y - dbCand.center.y);
            if (legA < minBentLegLength || legB < minBentLegLength) {
                return false;
            }
            const int minLeg = std::min(legA, legB);
            const int maxLeg = std::max(legA, legB);
            const int interiorPenalty = isNearRoomInterior(corner, 1) ? 4 : 0;
            // Prefer corners that avoid tiny stubs and avoid hugging room interiors.
            outScore = interiorPenalty + std::abs(legA - legB) - (minLeg * 3) - maxLeg;
            outDa = daCand;
            outDb = dbCand;
            outC = corner;
            return true;
        };

        bool found = false;
        DoorPair bestA{};
        DoorPair bestB{};
        Point bestCorner{};
        int bestScore = 1000000;
        for (int pass = 0; pass < 2; ++pass) {
            const bool requireDoorSpacing = (pass == 0);
            for (int pattern = 0; pattern < 2; ++pattern) {
                const bool aVertical = (pattern == 0);
                DoorPair candA{};
                DoorPair candB{};
                Point candCorner{};
                int score = 0;
                if (!tryPattern(aVertical, requireDoorSpacing, candA, candB, candCorner, score)) {
                    continue;
                }
                if (!found || score < bestScore) {
                    found = true;
                    bestScore = score;
                    bestA = candA;
                    bestB = candB;
                    bestCorner = candCorner;
                }
            }
            if (found) {
                break;
            }
        }

        if (!found) {
            return false;
        }
        outA = bestA;
        outB = bestB;
        outCorner = bestCorner;
        return true;
    };

    for (std::size_t si = 0; si < selected.size(); ++si) {
        const int sel = selected[si];
        const Edge& e = edges[static_cast<std::size_t>(sel)];
        DoorPair da = makeDoor(rooms[static_cast<std::size_t>(e.a)].rect, rooms[static_cast<std::size_t>(e.b)].rect);
        DoorPair db = makeDoor(rooms[static_cast<std::size_t>(e.b)].rect, rooms[static_cast<std::size_t>(e.a)].rect);
        const int edgeCorridorWidth = corridorWidth;
        Point forcedCorner{};
        bool useForcedCorner = false;

        const bool lockedStraightDoors = tryMakeStraightDoorPairs(
            rooms[static_cast<std::size_t>(e.a)].rect,
            rooms[static_cast<std::size_t>(e.b)].rect,
            edgeCorridorWidth,
            da,
            db);

        if (!lockedStraightDoors) {
            useForcedCorner = tryMakeBentDoorPairs(
                rooms[static_cast<std::size_t>(e.a)].rect,
                rooms[static_cast<std::size_t>(e.b)].rect,
                edgeCorridorWidth,
                da,
                db,
                forcedCorner);
        }

        if (!lockedStraightDoors && !useForcedCorner) {
            adjustDoorAwayFromCorners(rooms[static_cast<std::size_t>(e.a)].rect, rooms[static_cast<std::size_t>(e.b)].rect, da);
            adjustDoorAwayFromCorners(rooms[static_cast<std::size_t>(e.b)].rect, rooms[static_cast<std::size_t>(e.a)].rect, db);
        }

        Point p1 = da.center;
        Point p2 = db.center;
        const bool straight = (p1.x == p2.x || p1.y == p2.y);

        const bool nonEssential = (selectedIsMst[si] == 0);
        if (!straight && nonEssential) {
            continue;
        }

        if (!straight && !useForcedCorner) {
            // Do not carve L-turns unless we found a perpendicular-door pair.
            continue;
        }

        if (straight) {
            carveSegment(p1, p2, edgeCorridorWidth, ROAD);
            markProtectedSegment(p1, p2, edgeCorridorWidth);
        } else {
            const int bendWidth = (edgeCorridorWidth >= mainCorridorWidth) ? corridorWidth : edgeCorridorWidth;

            carveSegment(p1, forcedCorner, bendWidth, ROAD);
            carveSegment(forcedCorner, p2, bendWidth, ROAD);
            carveDot(forcedCorner.x, forcedCorner.y, bendWidth, ROAD);
            markProtectedSegment(p1, forcedCorner, bendWidth);
            markProtectedSegment(forcedCorner, p2, bendWidth);
        }

        bool placedA = tryPlaceDoorPair(da, false);
        bool placedB = tryPlaceDoorPair(db, false);
        if (!placedA) {
            placedA = forcePlaceDoorPair(da);
        }
        if (!placedB) {
            placedB = forcePlaceDoorPair(db);
        }
        if (placedA) {
            allDoors.push_back(da.c0);
            allDoors.push_back(da.c1);
            ++roomDoorCount[static_cast<std::size_t>(e.a)];
        }
        if (placedB) {
            allDoors.push_back(db.c0);
            allDoors.push_back(db.c1);
            ++roomDoorCount[static_cast<std::size_t>(e.b)];
        }
    }

    std::vector<Point> baseCorridorCells;
    baseCorridorCells.reserve(static_cast<std::size_t>(n / 10));
    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            const int i = Index(x, y, w);
            if (roomMask[static_cast<std::size_t>(i)] != 0) {
                continue;
            }
            if (isCorridorLike(x, y)) {
                baseCorridorCells.push_back({x, y});
            }
        }
    }

    auto connectWingSpineToMain = [&](const LinearWingLayout& wing) {
        if (baseCorridorCells.empty()) {
            return;
        }

        std::vector<Point> spineCells;
        if (wing.spineAxis == 0) {
            for (int y = wing.spineStart; y <= wing.spineEnd; ++y) {
                if (InBounds(wing.spinePos, y, w, h)) {
                    spineCells.push_back({wing.spinePos, y});
                }
            }
        } else {
            for (int x = wing.spineStart; x <= wing.spineEnd; ++x) {
                if (InBounds(x, wing.spinePos, w, h)) {
                    spineCells.push_back({x, wing.spinePos});
                }
            }
        }
        if (spineCells.empty()) {
            return;
        }

        std::vector<std::uint8_t> isBase(static_cast<std::size_t>(n), 0);
        auto isRoadCell = [&](int x, int y) {
            if (!InBounds(x, y, w, h)) {
                return false;
            }
            return outBiomes[static_cast<std::size_t>(Index(x, y, w))] == ROAD;
        };
        auto isRoadCenterLike = [&](int x, int y) {
            if (!isRoadCell(x, y)) {
                return false;
            }
            const bool horiz = isRoadCell(x - 1, y) && isRoadCell(x + 1, y);
            const bool vert = isRoadCell(x, y - 1) && isRoadCell(x, y + 1);
            return horiz || vert;
        };

        bool hasCenterBase = false;
        for (const Point& p : baseCorridorCells) {
            if (InBounds(p.x, p.y, w, h) && isRoadCenterLike(p.x, p.y)) {
                isBase[static_cast<std::size_t>(Index(p.x, p.y, w))] = 1;
                hasCenterBase = true;
            }
        }

        if (!hasCenterBase) {
            for (const Point& p : baseCorridorCells) {
                if (InBounds(p.x, p.y, w, h)) {
                    isBase[static_cast<std::size_t>(Index(p.x, p.y, w))] = 1;
                }
            }
        }

        std::vector<int> parent(static_cast<std::size_t>(n), -1);
        std::vector<std::uint8_t> seen(static_cast<std::size_t>(n), 0);
        std::vector<int> queue;
        queue.reserve(static_cast<std::size_t>(n));
        std::size_t qHead = 0;

        for (const Point& s : spineCells) {
            const int si = Index(s.x, s.y, w);
            if (roomMask[static_cast<std::size_t>(si)] != 0 || seen[static_cast<std::size_t>(si)] != 0) {
                continue;
            }
            seen[static_cast<std::size_t>(si)] = 1;
            parent[static_cast<std::size_t>(si)] = si;
            queue.push_back(si);
        }

        int target = -1;
        const std::array<Point, 4> dirs = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        while (qHead < queue.size()) {
            const int cur = queue[qHead++];
            const int cx = cur % w;
            const int cy = cur / w;
            if (isBase[static_cast<std::size_t>(cur)] != 0) {
                target = cur;
                break;
            }

            for (const Point& d : dirs) {
                const int nx = cx + d.x;
                const int ny = cy + d.y;
                if (!InBounds(nx, ny, w, h)) {
                    continue;
                }
                const int ni = Index(nx, ny, w);
                if (seen[static_cast<std::size_t>(ni)] != 0) {
                    continue;
                }
                if (roomMask[static_cast<std::size_t>(ni)] != 0) {
                    continue;
                }
                seen[static_cast<std::size_t>(ni)] = 1;
                parent[static_cast<std::size_t>(ni)] = cur;
                queue.push_back(ni);
            }
        }

        if (target < 0) {
            return;
        }

        std::vector<Point> path;
        for (int cur = target; cur >= 0;) {
            path.push_back({cur % w, cur / w});
            const int p = parent[static_cast<std::size_t>(cur)];
            if (p == cur) {
                break;
            }
            cur = p;
        }
        if (path.size() < 2) {
            return;
        }
        std::reverse(path.begin(), path.end());

        Point segStart = path.front();
        Point prev = path.front();
        Point prevDir{0, 0};
        for (std::size_t i = 1; i < path.size(); ++i) {
            const Point cur = path[i];
            const Point dir{cur.x - prev.x, cur.y - prev.y};
            if (i == 1) {
                prevDir = dir;
            }
            if (dir.x != prevDir.x || dir.y != prevDir.y) {
                carveSegment(segStart, prev, corridorWidth, ROAD);
                markProtectedSegment(segStart, prev, corridorWidth);
                segStart = prev;
                prevDir = dir;
            }
            prev = cur;
        }
        carveSegment(segStart, path.back(), corridorWidth, ROAD);
        markProtectedSegment(segStart, path.back(), corridorWidth);

        for (const Point& p : path) {
            baseCorridorCells.push_back(p);
        }
    };

    for (const LinearWingLayout& wing : linearWingLayouts) {
        if (wing.roomIndices.empty() || wing.spinePos < 0 || wing.spineEnd <= wing.spineStart) {
            continue;
        }

        if (wing.spineAxis == 0) {
            carveSegment({wing.spinePos, wing.spineStart}, {wing.spinePos, wing.spineEnd}, corridorWidth, ROAD);
            markProtectedSegment({wing.spinePos, wing.spineStart}, {wing.spinePos, wing.spineEnd}, corridorWidth);
        } else {
            carveSegment({wing.spineStart, wing.spinePos}, {wing.spineEnd, wing.spinePos}, corridorWidth, ROAD);
            markProtectedSegment({wing.spineStart, wing.spinePos}, {wing.spineEnd, wing.spinePos}, corridorWidth);
        }

        bool wingHasExternalDoor = false;
        for (int ri : wing.roomIndices) {
            if (ri >= 0 && ri < static_cast<int>(roomDoorCount.size()) && roomDoorCount[static_cast<std::size_t>(ri)] > 0) {
                wingHasExternalDoor = true;
                break;
            }
        }
        if (!wingHasExternalDoor) {
            connectWingSpineToMain(wing);
        }

        for (int ri : wing.roomIndices) {
            if (ri < 0 || ri >= static_cast<int>(rooms.size())) {
                continue;
            }
            const Rect& r = rooms[static_cast<std::size_t>(ri)].rect;

            // Keep the wing-facing wall from being overwritten by later ROAD carving.
            if (wing.spineAxis == 0) {
                const bool roomOnLeft = r.CenterX() < wing.spinePos;
                const int xWall = roomOnLeft ? r.x1 : (r.x0 - 1);
                for (int y = r.y0; y < r.y1; ++y) {
                    if (InBounds(xWall, y, w, h)) {
                        roadBlockMask[static_cast<std::size_t>(Index(xWall, y, w))] = 1;
                    }
                }
            } else {
                const bool roomOnTop = r.CenterY() < wing.spinePos;
                const int yWall = roomOnTop ? r.y1 : (r.y0 - 1);
                for (int x = r.x0; x < r.x1; ++x) {
                    if (InBounds(x, yWall, w, h)) {
                        roadBlockMask[static_cast<std::size_t>(Index(x, yWall, w))] = 1;
                    }
                }
            }

            if (wing.spineAxis == 0) {
                // Vertical spine: place doors on the single wall tile between room and corridor.
                const bool roomOnLeft = r.CenterX() < wing.spinePos;
                const int xDoor = roomOnLeft ? r.x1 : (r.x0 - 1);
                const int yDoor = std::clamp(r.CenterY() - ((doorLength - 1) / 2), r.y0 + 1, std::max(r.y0 + 1, r.y1 - doorLength - 1));
                const DoorPair d{{xDoor, yDoor}, {xDoor, yDoor + (doorLength - 1)}, {xDoor, yDoor + (doorLength - 1) / 2}};
                bool placed = tryPlaceDoorPair(d, true);
                if (!placed) {
                    placed = forcePlaceDoorPair(d);
                }
                if (placed) {
                    allDoors.push_back(d.c0);
                    allDoors.push_back(d.c1);
                }
                if (placed && ri >= 0 && ri < static_cast<int>(roomDoorCount.size())) {
                    roomDoorCount[static_cast<std::size_t>(ri)] = std::max(roomDoorCount[static_cast<std::size_t>(ri)], 1);
                }
            } else {
                // Horizontal spine: place doors on the single wall tile between room and corridor.
                const bool roomOnTop = r.CenterY() < wing.spinePos;
                const int yDoor = roomOnTop ? r.y1 : (r.y0 - 1);
                const int xDoor = std::clamp(r.CenterX() - ((doorLength - 1) / 2), r.x0 + 1, std::max(r.x0 + 1, r.x1 - doorLength - 1));
                const DoorPair d{{xDoor, yDoor}, {xDoor + (doorLength - 1), yDoor}, {xDoor + (doorLength - 1) / 2, yDoor}};
                bool placed = tryPlaceDoorPair(d, true);
                if (!placed) {
                    placed = forcePlaceDoorPair(d);
                }
                if (placed) {
                    allDoors.push_back(d.c0);
                    allDoors.push_back(d.c1);
                }
                if (placed && ri >= 0 && ri < static_cast<int>(roomDoorCount.size())) {
                    roomDoorCount[static_cast<std::size_t>(ri)] = std::max(roomDoorCount[static_cast<std::size_t>(ri)], 1);
                }
            }
        }
    }

    auto tryRouteRoomPair = [&](int a, int b, bool requireStraightOnly, bool allowForcedDoorFallback) {
        if (a < 0 || b < 0 || a >= static_cast<int>(rooms.size()) || b >= static_cast<int>(rooms.size()) || a == b) {
            return false;
        }

        DoorPair da = makeDoor(rooms[static_cast<std::size_t>(a)].rect, rooms[static_cast<std::size_t>(b)].rect);
        DoorPair db = makeDoor(rooms[static_cast<std::size_t>(b)].rect, rooms[static_cast<std::size_t>(a)].rect);
        Point forcedCorner{};
        bool useForcedCorner = false;

        const bool lockedStraightDoors = tryMakeStraightDoorPairs(
            rooms[static_cast<std::size_t>(a)].rect,
            rooms[static_cast<std::size_t>(b)].rect,
            corridorWidth,
            da,
            db);

        if (!lockedStraightDoors && !requireStraightOnly) {
            useForcedCorner = tryMakeBentDoorPairs(
                rooms[static_cast<std::size_t>(a)].rect,
                rooms[static_cast<std::size_t>(b)].rect,
                corridorWidth,
                da,
                db,
                forcedCorner);
        }

        if (!lockedStraightDoors && !useForcedCorner) {
            return false;
        }

        if (requireStraightOnly && !lockedStraightDoors) {
            return false;
        }

        const Point p1 = da.center;
        const Point p2 = db.center;
        std::vector<std::uint8_t> biomeBackup = outBiomes;

        if (p1.x == p2.x || p1.y == p2.y) {
            carveSegment(p1, p2, corridorWidth, ROAD);
        } else {
            carveSegment(p1, forcedCorner, corridorWidth, ROAD);
            carveSegment(forcedCorner, p2, corridorWidth, ROAD);
            carveDot(forcedCorner.x, forcedCorner.y, corridorWidth, ROAD);
        }

        bool placedA = tryPlaceDoorPair(da, false);
        bool placedB = tryPlaceDoorPair(db, false);
        if (!placedA && allowForcedDoorFallback) {
            placedA = forcePlaceDoorPair(da);
        }
        if (!placedB && allowForcedDoorFallback) {
            placedB = forcePlaceDoorPair(db);
        }

        if (!(placedA && placedB)) {
            outBiomes.swap(biomeBackup);
            return false;
        }

        if (p1.x == p2.x || p1.y == p2.y) {
            markProtectedSegment(p1, p2, corridorWidth);
        } else {
            markProtectedSegment(p1, forcedCorner, corridorWidth);
            markProtectedSegment(forcedCorner, p2, corridorWidth);
        }

        if (placedA) {
            allDoors.push_back(da.c0);
            allDoors.push_back(da.c1);
            ++roomDoorCount[static_cast<std::size_t>(a)];
        }
        if (placedB) {
            allDoors.push_back(db.c0);
            allDoors.push_back(db.c1);
            ++roomDoorCount[static_cast<std::size_t>(b)];
        }
        return true;
    };

    // Rewire pass: reconnect rooms that ended up isolated by strict bend constraints.
    for (int pass = 0; pass < static_cast<int>(rooms.size()); ++pass) {
        std::vector<int> isolated;
        isolated.reserve(rooms.size());
        for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
            if (roomDoorCount[static_cast<std::size_t>(i)] == 0) {
                isolated.push_back(i);
            }
        }
        if (isolated.empty()) {
            break;
        }

        bool progress = false;
        for (int ri : isolated) {
            std::vector<std::pair<int, int>> candidates;
            candidates.reserve(rooms.size());

            const int cx = rooms[static_cast<std::size_t>(ri)].rect.CenterX();
            const int cy = rooms[static_cast<std::size_t>(ri)].rect.CenterY();
            for (int j = 0; j < static_cast<int>(rooms.size()); ++j) {
                if (j == ri || roomDoorCount[static_cast<std::size_t>(j)] == 0) {
                    continue;
                }
                const int dx = std::abs(cx - rooms[static_cast<std::size_t>(j)].rect.CenterX());
                const int dy = std::abs(cy - rooms[static_cast<std::size_t>(j)].rect.CenterY());
                candidates.push_back({dx + dy, j});
            }

            if (candidates.empty()) {
                for (int j = 0; j < static_cast<int>(rooms.size()); ++j) {
                    if (j == ri) {
                        continue;
                    }
                    const int dx = std::abs(cx - rooms[static_cast<std::size_t>(j)].rect.CenterX());
                    const int dy = std::abs(cy - rooms[static_cast<std::size_t>(j)].rect.CenterY());
                    candidates.push_back({dx + dy, j});
                }
            }

            std::sort(candidates.begin(), candidates.end(), [](const std::pair<int, int>& lhs, const std::pair<int, int>& rhs) {
                return lhs.first < rhs.first;
            });

            const int trialCount = std::min(48, static_cast<int>(candidates.size()));
            for (int ci = 0; ci < trialCount; ++ci) {
                if (tryRouteRoomPair(ri, candidates[static_cast<std::size_t>(ci)].second, true, false)) {
                    progress = true;
                    break;
                }
            }
        }

        if (!progress) {
            break;
        }
    }

    const float branchRate = static_cast<float>(readParam("branchRate", static_cast<double>(effectiveDeadEndRate), 0.0, 6.0, 0.0));
    const int branchTryMultiplier = static_cast<int>(std::round(readParam("branchTryMultiplier", 10.0, 1.0, 80.0, 0.0)));
    const float branchFromProtectedChance = static_cast<float>(readParam("branchFromProtectedChance", 0.0, 0.0, 1.0, 0.0));
    const int branchMinSpacing = static_cast<int>(std::round(readParam("branchMinSpacing", static_cast<double>(corridorWidth), 0.0, 24.0, 0.0)));
    const int branchAttempts = std::max(0, static_cast<int>(std::round(static_cast<float>(selected.size()) * branchRate)));
    if (branchAttempts > 0) {
        std::vector<Point> corridorCells;
        auto isRoad = [&](int x, int y) {
            if (!InBounds(x, y, w, h)) {
                return false;
            }
            return outBiomes[static_cast<std::size_t>(Index(x, y, w))] == ROAD;
        };

        auto isInteriorBranchAnchor = [&](const Point& p, const Point& dir) {
            auto runSpan = [&](int dx, int dy) {
                int span = 1;
                for (int s = 1; s <= corridorWidth + 2; ++s) {
                    if (!isRoad(p.x + dx * s, p.y + dy * s)) {
                        break;
                    }
                    ++span;
                }
                for (int s = 1; s <= corridorWidth + 2; ++s) {
                    if (!isRoad(p.x - dx * s, p.y - dy * s)) {
                        break;
                    }
                    ++span;
                }
                return span;
            };

            const int spanX = runSpan(1, 0);
            const int spanY = runSpan(0, 1);
            if (spanX == spanY) {
                return false;
            }
            const int minWidthSpan = std::max(2, corridorWidth - 1);
            const int maxWidthSpan = corridorWidth + 1;

            if (dir.x != 0) {
                // Horizontal branch must depart from a vertical-dominant corridor anchor.
                if (!(spanY > spanX)) {
                    return false;
                }
                if (spanX < minWidthSpan || spanX > maxWidthSpan) {
                    return false;
                }
                // Avoid edge anchors inside even-width corridors.
                return isRoad(p.x - 1, p.y) && isRoad(p.x + 1, p.y);
            }
            // Vertical branch must depart from a horizontal-dominant corridor anchor.
            if (!(spanX > spanY)) {
                return false;
            }
            if (spanY < minWidthSpan || spanY > maxWidthSpan) {
                return false;
            }
            // Avoid edge anchors inside even-width corridors.
            return isRoad(p.x, p.y - 1) && isRoad(p.x, p.y + 1);
        };

        for (int y = 2; y < h - 2; ++y) {
            for (int x = 2; x < w - 2; ++x) {
                const int i = Index(x, y, w);
                if (outBiomes[static_cast<std::size_t>(i)] != ROAD) {
                    continue;
                }
                if (protectedRoad[static_cast<std::size_t>(i)] == 0 || ur(rng) < branchFromProtectedChance) {
                    corridorCells.push_back({x, y});
                }
            }
        }

        std::shuffle(corridorCells.begin(), corridorCells.end(), rng);
        const std::array<Point, 4> dirs = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

        if (!corridorCells.empty()) {
            int done = 0;
            std::vector<Point> branchStarts;
            branchStarts.reserve(static_cast<std::size_t>(branchAttempts));
            const int maxBranchTries = std::max(static_cast<int>(corridorCells.size()), branchAttempts * branchTryMultiplier);
            for (int ti = 0; ti < maxBranchTries && done < branchAttempts; ++ti) {
                const Point& p = corridorCells[static_cast<std::size_t>(std::uniform_int_distribution<int>(0, static_cast<int>(corridorCells.size()) - 1)(rng))];
                const Point d = dirs[static_cast<std::size_t>(std::uniform_int_distribution<int>(0, 3)(rng))];
                if (!isInteriorBranchAnchor(p, d)) {
                    continue;
                }

                Point launch = p;
                for (;;) {
                    const Point next{launch.x + d.x, launch.y + d.y};
                    if (!InBounds(next.x, next.y, w, h) || !isRoad(next.x, next.y)) {
                        break;
                    }
                    launch = next;
                }

                const Point first{launch.x + d.x, launch.y + d.y};
                if (!InBounds(first.x, first.y, w, h)) {
                    continue;
                }
                bool tooNear = false;
                for (const Point& s : branchStarts) {
                    if (std::abs(first.x - s.x) + std::abs(first.y - s.y) < branchMinSpacing) {
                        tooNear = true;
                        break;
                    }
                }
                if (tooNear) {
                    continue;
                }
                const int firstIndex = Index(first.x, first.y, w);
                if (outBiomes[static_cast<std::size_t>(firstIndex)] != SEA) {
                    continue;
                }
                if (protectedRoad[static_cast<std::size_t>(firstIndex)] != 0 || roadBlockMask[static_cast<std::size_t>(firstIndex)] != 0 || roomMask[static_cast<std::size_t>(firstIndex)] != 0) {
                    continue;
                }

                const int len = std::uniform_int_distribution<int>(4, std::max(5, std::min(w, h) / 8))(rng);
                Point end = launch;
                bool ok = true;

                for (int step = 0; step < len; ++step) {
                    Point next{end.x + d.x, end.y + d.y};
                    if (!InBounds(next.x, next.y, w, h)) {
                        ok = false;
                        break;
                    }
                    const int i = Index(next.x, next.y, w);
                    const std::uint8_t b = outBiomes[static_cast<std::size_t>(i)];
                    if (b != SEA) {
                        ok = false;
                        break;
                    }
                    if (protectedRoad[static_cast<std::size_t>(i)] != 0) {
                        ok = false;
                        break;
                    }
                    if (roadBlockMask[static_cast<std::size_t>(i)] != 0 || roomMask[static_cast<std::size_t>(i)] != 0) {
                        ok = false;
                        break;
                    }
                    end = next;
                }

                if (ok) {
                    carveSegment(first, end, corridorWidth, ROAD);
                    branchStarts.push_back(first);
                    ++done;
                }
            }
        }
    }

    auto pruneOrphanRoadComponents = [&]() {
        std::vector<std::uint8_t> seen(static_cast<std::size_t>(n), 0);
        const std::array<Point, 4> dirs4 = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int start = Index(x, y, w);
                if (seen[static_cast<std::size_t>(start)] != 0 || outBiomes[static_cast<std::size_t>(start)] != ROAD) {
                    continue;
                }

                std::vector<int> comp;
                comp.reserve(64);
                std::vector<int> queue;
                queue.reserve(64);
                std::size_t qHead = 0;
                queue.push_back(start);
                seen[static_cast<std::size_t>(start)] = 1;

                bool anchored = false;
                while (qHead < queue.size()) {
                    const int cur = queue[qHead++];
                    comp.push_back(cur);
                    const int cx = cur % w;
                    const int cy = cur / w;

                    for (const Point& d : dirs4) {
                        const int nx = cx + d.x;
                        const int ny = cy + d.y;
                        if (!InBounds(nx, ny, w, h)) {
                            continue;
                        }
                        const int ni = Index(nx, ny, w);
                        const std::uint8_t nb = outBiomes[static_cast<std::size_t>(ni)];
                        if (nb == ROAD) {
                            if (seen[static_cast<std::size_t>(ni)] == 0) {
                                seen[static_cast<std::size_t>(ni)] = 1;
                                queue.push_back(ni);
                            }
                            continue;
                        }
                        if (nb == BRIDGE || roomMask[static_cast<std::size_t>(ni)] != 0) {
                            anchored = true;
                        }
                    }
                }

                if (!anchored) {
                    for (int ci : comp) {
                        outBiomes[static_cast<std::size_t>(ci)] = SEA;
                        protectedRoad[static_cast<std::size_t>(ci)] = 0;
                    }
                }
            }
        }
    };

    pruneOrphanRoadComponents();

    for (std::size_t ri = 0; ri < rooms.size(); ++ri) {
        if (roomDoorCount[ri] > 0) {
            continue;
        }
        std::vector<std::pair<int, int>> candidates;
        candidates.reserve(rooms.size());
        const int cx = rooms[ri].rect.CenterX();
        const int cy = rooms[ri].rect.CenterY();
        for (int j = 0; j < static_cast<int>(rooms.size()); ++j) {
            if (j == static_cast<int>(ri)) {
                continue;
            }
            const int dx = std::abs(cx - rooms[static_cast<std::size_t>(j)].rect.CenterX());
            const int dy = std::abs(cy - rooms[static_cast<std::size_t>(j)].rect.CenterY());
            candidates.push_back({dx + dy, j});
        }
        std::sort(candidates.begin(), candidates.end(), [](const std::pair<int, int>& lhs, const std::pair<int, int>& rhs) {
            return lhs.first < rhs.first;
        });

        const int retryCount = std::min(64, static_cast<int>(candidates.size()));
        bool reconnected = false;
        for (int ci = 0; ci < retryCount; ++ci) {
            if (tryRouteRoomPair(static_cast<int>(ri), candidates[static_cast<std::size_t>(ci)].second, true, false)) {
                reconnected = true;
                break;
            }
        }

        if (!reconnected) {
            // Avoid creating a dangling 1-step stub corridor; leave room for future retries.
            continue;
        }
    }

    // Add compact hazards inside rooms only.  A failed connectivity check
    // restores the pre-hazard plan, so hazards never split the playable graph.
    const std::vector<std::uint8_t> hazardBackup = outBiomes;
    for (std::size_t ri = 0; ri < rooms.size(); ++ri) {
        if (rooms[ri].key || static_cast<int>(ri) == centralRoomIdx || ur(rng) > hazardRate) {
            continue;
        }
        const Rect& r = rooms[ri].rect;
        const int innerWidth = r.Width() - 4;
        const int innerHeight = r.Height() - 4;
        if (innerWidth < 4 || innerHeight < 4) {
            continue;
        }
        const int patchWidth = std::max(2, std::min(innerWidth / 3, 6));
        const int patchHeight = std::max(2, std::min(innerHeight / 3, 5));
        const int x0 = r.x0 + 2 + std::max(0, (innerWidth - patchWidth) / 2);
        const int y0 = r.y0 + 2 + std::max(0, (innerHeight - patchHeight) / 2);
        const std::uint8_t hazardTile = (ur(rng) < 0.30f) ? static_cast<std::uint8_t>(LAKE) : static_cast<std::uint8_t>(RIVER);
        for (int y = y0; y < y0 + patchHeight; ++y) {
            for (int x = x0; x < x0 + patchWidth; ++x) {
                const int cell = Index(x, y, w);
                if (roomMask[static_cast<std::size_t>(cell)] != 0 &&
                    (outBiomes[static_cast<std::size_t>(cell)] == PLAIN ||
                     outBiomes[static_cast<std::size_t>(cell)] == DESERT ||
                     outBiomes[static_cast<std::size_t>(cell)] == FOREST)) {
                    outBiomes[static_cast<std::size_t>(cell)] = hazardTile;
                }
            }
        }
    }

    auto isDungeonWalkable = [](std::uint8_t biome) {
        return biome == PLAIN || biome == DESERT || biome == FOREST || biome == ROAD || biome == BRIDGE;
    };
    int walkableCount = 0;
    int largestComponent = 0;
    std::vector<std::uint8_t> hazardVisited(static_cast<std::size_t>(n), 0);
    for (int start = 0; start < n; ++start) {
        if (hazardVisited[static_cast<std::size_t>(start)] != 0 ||
            !isDungeonWalkable(outBiomes[static_cast<std::size_t>(start)])) {
            continue;
        }
        int componentSize = 0;
        std::queue<int> pending;
        pending.push(start);
        hazardVisited[static_cast<std::size_t>(start)] = 1;
        while (!pending.empty()) {
            const int current = pending.front();
            pending.pop();
            ++componentSize;
            ++walkableCount;
            const int cx = current % w;
            const int cy = current / w;
            for (const Point& direction : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                const int nx = cx + direction.x;
                const int ny = cy + direction.y;
                if (!InBounds(nx, ny, w, h)) {
                    continue;
                }
                const int next = Index(nx, ny, w);
                if (hazardVisited[static_cast<std::size_t>(next)] == 0 &&
                    isDungeonWalkable(outBiomes[static_cast<std::size_t>(next)])) {
                    hazardVisited[static_cast<std::size_t>(next)] = 1;
                    pending.push(next);
                }
            }
        }
        largestComponent = std::max(largestComponent, componentSize);
    }
    if (walkableCount == 0 || largestComponent * 100 < walkableCount * 95) {
        outBiomes = hazardBackup;
    }

    // Existing layouts tend to produce thin corridors.  Grow connected road
    // cells toward a measurable target while preserving room masks and doors.
    const int targetCorridorCells = static_cast<int>(std::round(static_cast<double>(n) * corridorTargetRatio));
    int currentCorridorCells = 0;
    for (std::uint8_t biome : outBiomes) {
        if (biome == ROAD) {
            ++currentCorridorCells;
        }
    }
    for (int pass = 0; pass < 12 && currentCorridorCells < targetCorridorCells; ++pass) {
        std::vector<int> candidates;
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                const int cell = Index(x, y, w);
                if (outBiomes[static_cast<std::size_t>(cell)] != SEA ||
                    roomMask[static_cast<std::size_t>(cell)] != 0 ||
                    roadBlockMask[static_cast<std::size_t>(cell)] != 0) {
                    continue;
                }
                int roadNeighbors = 0;
                for (const Point& direction : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                    const int nx = x + direction.x;
                    const int ny = y + direction.y;
                    if (InBounds(nx, ny, w, h) && outBiomes[static_cast<std::size_t>(Index(nx, ny, w))] == ROAD) {
                        ++roadNeighbors;
                    }
                }
                if (roadNeighbors > 0) {
                    candidates.push_back(cell);
                }
            }
        }
        std::shuffle(candidates.begin(), candidates.end(), rng);
        if (candidates.empty()) {
            break;
        }
        for (int cell : candidates) {
            if (currentCorridorCells >= targetCorridorCells) {
                break;
            }
            if (outBiomes[static_cast<std::size_t>(cell)] == SEA) {
                outBiomes[static_cast<std::size_t>(cell)] = ROAD;
                ++currentCorridorCells;
            }
        }
    }

    // Enforce 1-tile wall thickness: create a single wall ring around solid cells.
    auto isSolid = [](std::uint8_t b) {
        return b == PLAIN || b == DESERT || b == FOREST || b == ROAD || b == BRIDGE || b == RIVER || b == LAKE;
    };

    std::vector<std::uint8_t> next = outBiomes;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int i = Index(x, y, w);
            if (outBiomes[static_cast<std::size_t>(i)] != SEA) {
                continue;
            }
            bool nearSolid = false;
            for (int oy = -1; oy <= 1 && !nearSolid; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    if (ox == 0 && oy == 0) {
                        continue;
                    }
                    const int nx = x + ox;
                    const int ny = y + oy;
                    if (!InBounds(nx, ny, w, h)) {
                        continue;
                    }
                    if (isSolid(outBiomes[static_cast<std::size_t>(Index(nx, ny, w))])) {
                        nearSolid = true;
                        break;
                    }
                }
            }
            if (nearSolid) {
                next[static_cast<std::size_t>(i)] = MOUNTAIN;
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        if (next[static_cast<std::size_t>(i)] == EXMOUNTAIN) {
            next[static_cast<std::size_t>(i)] = MOUNTAIN;
        }
    }

    outBiomes.swap(next);
    for (std::size_t i = 0; i < rooms.size(); ++i) {
        const Rect& r = rooms[i].rect;
        outPlan.regions.push_back({rooms[i].key ? "key_room" : (i == static_cast<std::size_t>(centralRoomIdx) ? "central_room" : "room"),
            static_cast<int>(i), r.x0, r.y0, r.x1 - 1, r.y1 - 1});
    }
    return true;
}
