#include "ForestMapImageGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

enum Direction : int {
    North = 1,
    East = 2,
    South = 4,
    West = 8,
};

struct GridEdge {
    int cell;
    int neighbor;
    int direction;
};

struct EntranceCandidate {
    int cell;
    int direction;
    int x;
    int y;
};

struct CellBounds {
    int x0;
    int y0;
    int x1;
    int y1;
};

constexpr std::array<int, 4> kDirections = {North, East, South, West};
constexpr std::array<int, 4> kDeltaX = {0, 1, 0, -1};
constexpr std::array<int, 4> kDeltaY = {-1, 0, 1, 0};

int Opposite(int direction) {
    return direction == North ? South : direction == East ? West :
        direction == South ? North : East;
}

int RotateClockwise(int mask) {
    int rotated = 0;
    if ((mask & North) != 0) rotated |= East;
    if ((mask & East) != 0) rotated |= South;
    if ((mask & South) != 0) rotated |= West;
    if ((mask & West) != 0) rotated |= North;
    return rotated;
}

int CountPorts(int mask) {
    int count = 0;
    for (const int direction : kDirections) {
        if ((mask & direction) != 0) ++count;
    }
    return count;
}

std::string TileType(int mask) {
    switch (CountPorts(mask)) {
    case 0: return "blank";
    case 1: return "dead_end";
    case 2:
        return mask == (North | South) || mask == (East | West) ? "straight" : "corner";
    case 3: return "tee";
    default: return "cross";
    }
}

int TileRotation(int mask) {
    int base = 0;
    const std::string type = TileType(mask);
    if (type == "dead_end") base = North;
    else if (type == "straight") base = North | South;
    else if (type == "corner") base = North | East;
    else if (type == "tee") base = North | East | West;
    else return 0;
    for (int rotation = 0; rotation < 4; ++rotation) {
        if (base == mask) return rotation;
        base = RotateClockwise(base);
    }
    return 0;
}

CellBounds GetCellBounds(int column, int row, int columns, int rows, int width, int height) {
    return {
        column * width / columns,
        row * height / rows,
        (column + 1) * width / columns - 1,
        (row + 1) * height / rows - 1
    };
}

using CellMask = std::vector<std::uint8_t>;

std::size_t CellMaskIndex(const CellBounds& bounds, int x, int y) {
    const int cellWidth = bounds.x1 - bounds.x0 + 1;
    return static_cast<std::size_t>((y - bounds.y0) * cellWidth + x - bounds.x0);
}

void FillCellMaskRect(CellMask& mask, const CellBounds& bounds, int x0, int y0, int x1, int y1) {
    x0 = std::max(bounds.x0, x0);
    y0 = std::max(bounds.y0, y0);
    x1 = std::min(bounds.x1, x1);
    y1 = std::min(bounds.y1, y1);
    if (x0 > x1 || y0 > y1) return;
    const int cellWidth = bounds.x1 - bounds.x0 + 1;
    for (int y = y0; y <= y1; ++y) {
        const std::size_t start = static_cast<std::size_t>((y - bounds.y0) * cellWidth + x0 - bounds.x0);
        std::fill(mask.begin() + static_cast<std::ptrdiff_t>(start),
            mask.begin() + static_cast<std::ptrdiff_t>(start + x1 - x0 + 1), static_cast<std::uint8_t>(1));
    }
}

void DrawCellSegment(CellMask& mask, const CellBounds& bounds,
    int x0, int y0, int x1, int y1, int corridorWidth) {
    const int half = corridorWidth / 2;
    if (y0 == y1) {
        FillCellMaskRect(mask, bounds, std::min(x0, x1), y0 - half,
            std::max(x0, x1), y0 + corridorWidth - half - 1);
    } else {
        FillCellMaskRect(mask, bounds, x0 - half, std::min(y0, y1),
            x0 + corridorWidth - half - 1, std::max(y0, y1));
    }
}

void MergeCellMask(MapPlan& plan, const CellBounds& bounds, const CellMask& mask, MapGenBiomeID biome) {
    for (int y = bounds.y0; y <= bounds.y1; ++y) {
        for (int x = bounds.x0; x <= bounds.x1; ++x) {
            if (mask[CellMaskIndex(bounds, x, y)] != 0) {
                plan.biomes[static_cast<std::size_t>(y * plan.width + x)] = static_cast<std::uint8_t>(biome);
            }
        }
    }
}

void DrawCell(MapPlan& plan, const CellBounds& bounds, int ports, int corridorWidth,
    int deadEndDirection, int deadEndLength) {
    const int centerX = (bounds.x0 + bounds.x1) / 2;
    const int centerY = (bounds.y0 + bounds.y1) / 2;
    const int cellWidth = bounds.x1 - bounds.x0 + 1;
    const int cellHeight = bounds.y1 - bounds.y0 + 1;
    CellMask cellMask(static_cast<std::size_t>(cellWidth * cellHeight), 0);
    CellMask innerMask(cellMask.size(), 0);
    const auto segment = [&](int x0, int y0, int x1, int y1) {
        DrawCellSegment(cellMask, bounds, x0, y0, x1, y1, corridorWidth);
        DrawCellSegment(innerMask, bounds, x0, y0, x1, y1, 1);
    };
    std::array<int, 4> endpointX{};
    std::array<int, 4> endpointY{};

    for (std::size_t i = 0; i < kDirections.size(); ++i) {
        const int direction = kDirections[i];
        if ((ports & direction) == 0) continue;
        endpointX[i] = direction == North || direction == South
            ? centerX : direction == West ? bounds.x0 : bounds.x1;
        endpointY[i] = direction == East || direction == West
            ? centerY : direction == North ? bounds.y0 : bounds.y1;
    }

    for (std::size_t i = 0; i < kDirections.size(); ++i) {
        const int direction = kDirections[i];
        if ((ports & direction) == 0) continue;
        if (direction == North || direction == South) {
            segment(endpointX[i], endpointY[i], endpointX[i], centerY);
        } else {
            segment(endpointX[i], endpointY[i], centerX, endpointY[i]);
        }
    }

    if (deadEndDirection != 0) {
        int endX = centerX;
        int endY = centerY;
        if (deadEndDirection == North) endY -= deadEndLength;
        else if (deadEndDirection == East) endX += deadEndLength;
        else if (deadEndDirection == South) endY += deadEndLength;
        else endX -= deadEndLength;
        segment(centerX, centerY, endX, endY);
    }

    MergeCellMask(plan, bounds, cellMask, ROAD);
    MergeCellMask(plan, bounds, innerMask, INNERROAD);
}

void FillTerrain(MapPlan& plan, const CellBounds& bounds, MapGenBiomeID biome) {
    for (int y = bounds.y0; y <= bounds.y1; ++y) {
        for (int x = bounds.x0; x <= bounds.x1; ++x) {
            plan.biomes[static_cast<std::size_t>(y * plan.width + x)] = static_cast<std::uint8_t>(biome);
        }
    }
}

void FillCliffsNorthward(MapPlan& plan) {
    for (int x = 0; x < plan.width; ++x) {
        bool cliffBelow = false;
        for (int y = plan.depth - 1; y >= 0; --y) {
            auto& biome = plan.biomes[static_cast<std::size_t>(y * plan.width + x)];
            if (biome == FOREST && cliffBelow) biome = CLIFF;
            cliffBelow = biome == CLIFF;
        }
    }
}

bool ForestWalkable(std::uint8_t biome) {
    return biome == ROAD || biome == INNERROAD || biome == PLAIN || biome == OPEN || biome == BRIDGE;
}

bool ConnectedForestImage(const MapPlan& plan) {
    const auto start = std::find_if(plan.biomes.begin(), plan.biomes.end(), ForestWalkable);
    if (start == plan.biomes.end()) return false;
    std::vector<std::uint8_t> visited(plan.biomes.size(), 0);
    std::vector<int> pending = {static_cast<int>(start - plan.biomes.begin())};
    visited[static_cast<std::size_t>(pending.front())] = 1;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const int index = pending[i];
        for (std::size_t direction = 0; direction < kDirections.size(); ++direction) {
            const int x = index % plan.width + kDeltaX[direction];
            const int y = index / plan.width + kDeltaY[direction];
            if (x < 0 || y < 0 || x >= plan.width || y >= plan.depth) continue;
            const auto next = static_cast<std::size_t>(y * plan.width + x);
            if (visited[next] || !ForestWalkable(plan.biomes[next])) continue;
            visited[next] = 1;
            pending.push_back(static_cast<int>(next));
        }
    }
    return pending.size() == static_cast<std::size_t>(
        std::count_if(plan.biomes.begin(), plan.biomes.end(), ForestWalkable));
}

void ExtendMapEdges(MapPlan& plan) {
    constexpr int inset = 2;
    const int width = plan.width + 2 * inset;
    const int depth = plan.depth + 2 * inset;
    std::vector<std::uint8_t> biomes(static_cast<std::size_t>(width) * depth);
    for (int y = 0; y < depth; ++y) {
        const int sourceY = std::clamp(y - inset, 0, plan.depth - 1);
        for (int x = 0; x < width; ++x) {
            const int sourceX = std::clamp(x - inset, 0, plan.width - 1);
            biomes[static_cast<std::size_t>(y * width + x)] =
                plan.biomes[static_cast<std::size_t>(sourceY * plan.width + sourceX)];
        }
    }
    for (auto& region : plan.regions) {
        region.x0 += inset;
        region.x1 += inset;
        region.y0 += inset;
        region.y1 += inset;
    }
    plan.width = width;
    plan.depth = depth;
    plan.biomes = std::move(biomes);
}

void FillDeepForest(MapPlan& plan, int minimumDistance) {
    std::vector<int> distances(plan.biomes.size(), minimumDistance);
    for (int y = 0; y < plan.depth; ++y) {
        for (int x = 0; x < plan.width; ++x) {
            const auto index = static_cast<std::size_t>(y * plan.width + x);
            if (plan.biomes[index] != FOREST) distances[index] = 0;
            if (x > 0) distances[index] = std::min(distances[index], distances[index - 1] + 1);
            if (y > 0) distances[index] = std::min(distances[index], distances[index - plan.width] + 1);
        }
    }
    for (int y = plan.depth - 1; y >= 0; --y) {
        for (int x = plan.width - 1; x >= 0; --x) {
            const auto index = static_cast<std::size_t>(y * plan.width + x);
            if (x + 1 < plan.width) distances[index] = std::min(distances[index], distances[index + 1] + 1);
            if (y + 1 < plan.depth) distances[index] = std::min(distances[index], distances[index + plan.width] + 1);
            if (plan.biomes[index] == FOREST && distances[index] >= minimumDistance) plan.biomes[index] = DEEPFOREST;
        }
    }
}

bool ExpandRoadCopyMargins(const std::vector<std::uint8_t>& biomes, int width, int depth, CellBounds& source) {
    const auto road = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < depth &&
            (biomes[static_cast<std::size_t>(y * width + x)] == ROAD ||
                biomes[static_cast<std::size_t>(y * width + x)] == INNERROAD);
    };
    for (;;) {
        CellBounds expanded = source;
        const auto includeCrossSection = [&](int x, int y, bool horizontal) {
            if (!road(x, y)) return;
            const int position = horizontal ? x : y;
            const auto occupied = [&](int offset) {
                return road(horizontal ? offset : x, horizontal ? y : offset);
            };
            int first = position;
            int last = position;
            while (position - first < 3 && occupied(first - 1)) --first;
            while (last - first < 3 && occupied(last + 1)) ++last;
            if (last - first >= 3) return;
            if (horizontal) {
                expanded.x0 = std::min(expanded.x0, first - 1);
                expanded.x1 = std::max(expanded.x1, last + 1);
            } else {
                expanded.y0 = std::min(expanded.y0, first - 1);
                expanded.y1 = std::max(expanded.y1, last + 1);
            }
        };
        for (int y = source.y0; y <= source.y1; ++y) {
            includeCrossSection(source.x0, y, true);
            includeCrossSection(source.x1, y, true);
        }
        for (int x = source.x0; x <= source.x1; ++x) {
            includeCrossSection(x, source.y0, false);
            includeCrossSection(x, source.y1, false);
        }
        if (expanded.x0 < 0 || expanded.y0 < 0 ||
            expanded.x1 >= width || expanded.y1 >= depth) return false;
        if (expanded.x0 == source.x0 && expanded.y0 == source.y0 &&
            expanded.x1 == source.x1 && expanded.y1 == source.y1) return true;
        source = expanded;
    }
}

void ApplyMeandering(MapPlan& plan, int columns, int rows, double rate, std::mt19937& random) {
    if (rate == 0.0) return;
    const auto original = plan.biomes;
    std::vector<int> ordinary;
    std::vector<int> rooms;
    const int cellCount = columns * rows;
    std::vector<CellBounds> roomBounds(static_cast<std::size_t>(cellCount));
    std::vector<bool> hasRoom(static_cast<std::size_t>(cellCount), false);
    for (const auto& region : plan.regions) {
        if (region.kind != "forest_variant" || (region.role != "clearing" && region.role != "lake")) continue;
        const int cell = region.parentId;
        const CellBounds bounds = GetCellBounds(cell % columns, cell / columns,
            columns, rows, plan.width, plan.depth);
        const int x0 = std::clamp((bounds.x0 + bounds.x1) / 2 - region.width / 2,
            bounds.x0 + 1, bounds.x1 - region.width);
        const int y0 = std::clamp((bounds.y0 + bounds.y1) / 2 - region.width / 2,
            bounds.y0 + 1, bounds.y1 - region.width);
        roomBounds[static_cast<std::size_t>(cell)] = {x0 - 1, y0 - 1, x0 + region.width, y0 + region.width};
        hasRoom[static_cast<std::size_t>(cell)] = true;
    }
    for (int cell = 0; cell < cellCount; ++cell) {
        if (plan.regions[static_cast<std::size_t>(cell)].objectType == 0) continue;
        (hasRoom[static_cast<std::size_t>(cell)] ? rooms : ordinary).push_back(cell);
    }
    std::shuffle(ordinary.begin(), ordinary.end(), random);
    std::shuffle(rooms.begin(), rooms.end(), random);
    ordinary.insert(ordinary.end(), rooms.begin(), rooms.end());
    const double requested = rate * cellCount;
    int target = static_cast<int>(std::floor(requested));
    if (requested > target && std::bernoulli_distribution(requested - target)(random)) ++target;
    std::vector<CellBounds> occupied;
    const auto overlaps = [](const CellBounds& a, const CellBounds& b) {
        return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1;
    };
    const auto contains = [](const CellBounds& a, const CellBounds& b) {
        return a.x0 <= b.x0 && a.y0 <= b.y0 && a.x1 >= b.x1 && a.y1 >= b.y1;
    };
    int applied = 0;
    for (const int cell : ordinary) {
        if (applied >= target) break;
        const CellBounds bounds = GetCellBounds(cell % columns, cell / columns,
            columns, rows, plan.width, plan.depth);
        for (int attempt = 0; attempt < 16; ++attempt) {
            const int dx = std::uniform_int_distribution<int>(-1, 1)(random);
            const int dy = std::uniform_int_distribution<int>(-1, 1)(random);
            if (dx == 0 && dy == 0) continue;
            CellBounds source;
            if (hasRoom[static_cast<std::size_t>(cell)]) {
                source = roomBounds[static_cast<std::size_t>(cell)];
            } else {
                const int width = std::uniform_int_distribution<int>(
                    std::max(5, (bounds.x1 - bounds.x0 + 1) * 2 / 3), bounds.x1 - bounds.x0 + 1)(random);
                const int height = std::uniform_int_distribution<int>(
                    std::max(5, (bounds.y1 - bounds.y0 + 1) * 2 / 3), bounds.y1 - bounds.y0 + 1)(random);
                const int x = std::uniform_int_distribution<int>(bounds.x0, bounds.x1 - width + 1)(random);
                const int y = std::uniform_int_distribution<int>(bounds.y0, bounds.y1 - height + 1)(random);
                source = {x, y, x + width - 1, y + height - 1};
            }
            if (!ExpandRoadCopyMargins(original, plan.width, plan.depth, source)) continue;
            const CellBounds destination{source.x0 + dx, source.y0 + dy, source.x1 + dx, source.y1 + dy};
            const CellBounds affected{std::min(source.x0, destination.x0), std::min(source.y0, destination.y0),
                std::max(source.x1, destination.x1), std::max(source.y1, destination.y1)};
            if (affected.x0 < 0 || affected.y0 < 0 || affected.x1 >= plan.width || affected.y1 >= plan.depth ||
                std::any_of(occupied.begin(), occupied.end(),
                    [&](const CellBounds& previous) { return overlaps(previous, affected); })) continue;
            bool safe = true;
            for (int room = 0; room < cellCount; ++room) {
                if (!hasRoom[static_cast<std::size_t>(room)]) continue;
                const auto& footprint = roomBounds[static_cast<std::size_t>(room)];
                if (overlaps(affected, footprint) && !contains(source, footprint)) safe = false;
            }
            if (!safe) continue;
            auto candidate = plan.biomes;
            bool changed = false;
            bool copiedRoad = false;
            for (int y = source.y0; y <= source.y1; ++y) {
                for (int x = source.x0; x <= source.x1; ++x) {
                    const auto from = static_cast<std::size_t>(y * plan.width + x);
                    const auto to = static_cast<std::size_t>((y + dy) * plan.width + x + dx);
                    const auto biome = original[from];
                    if ((x + dx == 0 || x + dx == plan.width - 1 || y + dy == 0 ||
                        y + dy == plan.depth - 1) && biome != candidate[to]) safe = false;
                    if (!hasRoom[static_cast<std::size_t>(cell)] &&
                        (candidate[to] == PLAIN || candidate[to] == OPEN) && biome != candidate[to]) safe = false;
                    copiedRoad = copiedRoad || biome == ROAD || biome == INNERROAD;
                    changed = changed || candidate[to] != biome;
                    candidate[to] = biome;
                }
            }
            if (!safe || !changed || !copiedRoad) continue;
            plan.biomes.swap(candidate);
            if (!ConnectedForestImage(plan)) {
                plan.biomes.swap(candidate);
                continue;
            }
            MapPlanRegion region;
            region.kind = "forest_meander";
            region.id = static_cast<int>(plan.regions.size());
            region.parentId = cell;
            region.x0 = source.x0;
            region.y0 = source.y0;
            region.x1 = source.x1;
            region.y1 = source.y1;
            region.doorX = dx;
            region.doorY = dy;
            plan.regions.push_back(std::move(region));
            occupied.push_back(affected);
            ++applied;
            break;
        }
    }
}

void AddVariant(MapPlan& plan, int cell, const std::string& role, const CellBounds& bounds,
    int roomSize = 0, int anchor = 0) {
    MapPlanRegion region;
    region.kind = "forest_variant";
    region.id = static_cast<int>(plan.regions.size());
    region.parentId = cell;
    region.role = role;
    region.x0 = bounds.x0;
    region.y0 = bounds.y0;
    region.x1 = bounds.x1;
    region.y1 = bounds.y1;
    region.width = roomSize;
    region.rotation = anchor;
    plan.regions.push_back(std::move(region));
}

bool DrawRoom(MapPlan& plan, const CellBounds& cell, int ports, bool lake,
    std::mt19937& random, int cellId) {
    const int degree = CountPorts(ports);
    const int maximumSize = std::min(cell.x1 - cell.x0 + 1, cell.y1 - cell.y0 + 1) - 2;
    const int minimumSize = lake ? 5 : 3;
    if (maximumSize < minimumSize) return false;
    const int size = std::min(maximumSize, std::uniform_int_distribution<int>(
        8 + std::max(0, degree - 2), 8 + degree)(random));
    const int centerX = (cell.x0 + cell.x1) / 2;
    const int centerY = (cell.y0 + cell.y1) / 2;
    const int x0 = std::clamp(centerX - size / 2, cell.x0 + 1, cell.x1 - size);
    const int y0 = std::clamp(centerY - size / 2, cell.y0 + 1, cell.y1 - size);
    const CellBounds room{x0, y0, x0 + size - 1, y0 + size - 1};
    FillTerrain(plan, room, PLAIN);

    const int margin = lake ? 2 : 1;
    const int desiredSize = std::uniform_int_distribution<int>(
        (lake ? 4 : 5) + std::max(0, degree - 2),
        (lake ? 4 : 5) + degree - 1)(random);
    std::array<int, 5> anchors = {0, 1, 2, 3, 4};
    int anchorCount = 1;
    constexpr std::array<int, 5> cornerSides = {0, North | West, North | East, South | West, South | East};
    for (int corner = 1; corner <= 4; ++corner) {
        if (!lake || (ports & cornerSides[static_cast<std::size_t>(corner)]) == 0) {
            anchors[static_cast<std::size_t>(anchorCount++)] = corner;
        }
    }
    const int anchor = anchors[static_cast<std::size_t>(
        std::uniform_int_distribution<int>(0, anchorCount - 1)(random))];
    const int innerSize = std::min(size - (lake && anchor != 0 ? 2 : margin * 2), desiredSize);
    const int cornerMargin = lake ? 0 : margin;
    const int innerX = anchor == 0 ? x0 + (size - innerSize) / 2 :
        (anchor == 1 || anchor == 3 ? x0 + cornerMargin : room.x1 - cornerMargin - innerSize + 1);
    const int innerY = anchor == 0 ? y0 + (size - innerSize) / 2 :
        (anchor == 1 || anchor == 2 ? y0 + cornerMargin : room.y1 - cornerMargin - innerSize + 1);
    const CellBounds interior{innerX, innerY, innerX + innerSize - 1, innerY + innerSize - 1};
    if (lake) {
        FillTerrain(plan, {std::max(room.x0, interior.x0 - 2), std::max(room.y0, interior.y0 - 2),
            std::min(room.x1, interior.x1 + 2), std::min(room.y1, interior.y1 + 2)}, OPEN);
    }
    FillTerrain(plan, interior, lake ? LAKE : OPEN);
    AddVariant(plan, cellId, lake ? "lake" : "clearing", interior, size, anchor);
    return true;
}

void ApplyCellVariants(MapPlan& plan, int columns, int rows, const std::vector<int>& ports,
    const std::vector<int>& deadEndBranches, double clearingRate, double lakeRate,
    double riverRate, double cliffRate, std::mt19937& random) {
    const int cellCount = columns * rows;
    const auto boundsFor = [&](int cell) {
        return GetCellBounds(cell % columns, cell / columns, columns, rows, plan.width, plan.depth);
    };
    std::vector<bool> decorated(static_cast<std::size_t>(cellCount), false);
    std::vector<double> roomWeights(static_cast<std::size_t>(cellCount), 0.0);
    int eligibleRooms = 0;
    for (int cell = 0; cell < cellCount; ++cell) {
        if (ports[static_cast<std::size_t>(cell)] == 0) continue;
        roomWeights[static_cast<std::size_t>(cell)] =
            CountPorts(ports[static_cast<std::size_t>(cell)]) == 1 ? 2.0 : 1.0;
        ++eligibleRooms;
    }
    const double requestedRooms = clearingRate * cellCount;
    int roomCount = static_cast<int>(std::floor(requestedRooms));
    const double remainder = requestedRooms - roomCount;
    if (remainder > 0.0 && std::bernoulli_distribution(remainder)(random)) ++roomCount;
    roomCount = std::min(roomCount, eligibleRooms);
    for (int i = 0; i < roomCount; ++i) {
        const int cell = std::discrete_distribution<int>(roomWeights.begin(), roomWeights.end())(random);
        roomWeights[static_cast<std::size_t>(cell)] = 0.0;
        const bool lake = std::bernoulli_distribution(lakeRate)(random);
        decorated[static_cast<std::size_t>(cell)] =
            DrawRoom(plan, boundsFor(cell), ports[static_cast<std::size_t>(cell)], lake, random, cell);
    }

    std::vector<int> riverAxes(static_cast<std::size_t>(cellCount), 0);
    for (int cell = 0; cell < cellCount; ++cell) {
        const int mask = ports[static_cast<std::size_t>(cell)];
        if (decorated[static_cast<std::size_t>(cell)] || deadEndBranches[static_cast<std::size_t>(cell)] != 0 ||
            (mask != (North | South) && mask != (East | West)) ||
            riverRate == 0.0 || !std::bernoulli_distribution(riverRate)(random)) continue;
        const bool horizontal = mask == (North | South);
        const CellBounds source = boundsFor(cell);
        const int line = horizontal ? (source.y0 + source.y1) / 2 : (source.x0 + source.x1) / 2;
        const auto drawRiver = [&](int target) {
            const int axis = horizontal ? 1 : 2;
            if ((riverAxes[static_cast<std::size_t>(target)] & axis) != 0) return;
            riverAxes[static_cast<std::size_t>(target)] |= axis;
            const CellBounds bounds = boundsFor(target);
            const CellBounds river = horizontal ? CellBounds{bounds.x0, line - 1, bounds.x1, line} :
                CellBounds{line - 1, bounds.y0, line, bounds.y1};
            for (int y = river.y0; y <= river.y1; ++y) {
                for (int x = river.x0; x <= river.x1; ++x) {
                    auto& biome = plan.biomes[static_cast<std::size_t>(y * plan.width + x)];
                    biome = static_cast<std::uint8_t>(
                        biome == ROAD || biome == INNERROAD || biome == BRIDGE ? BRIDGE : RIVER);
                }
            }
            AddVariant(plan, target, target == cell ? "river" : "river_only", river);
            decorated[static_cast<std::size_t>(target)] = true;
        };
        drawRiver(cell);
        for (const int step : {-1, 1}) {
            int column = cell % columns + (horizontal ? step : 0);
            int row = cell / columns + (horizontal ? 0 : step);
            while (column >= 0 && column < columns && row >= 0 && row < rows) {
                const int target = row * columns + column;
                if (ports[static_cast<std::size_t>(target)] != 0) break;
                drawRiver(target);
                column += horizontal ? step : 0;
                row += horizontal ? 0 : step;
            }
        }
    }

    std::vector<int> cliffCells(static_cast<std::size_t>(cellCount), 0);
    for (int cell = 0; cell < cellCount; ++cell) {
        if (ports[static_cast<std::size_t>(cell)] == 0 || decorated[static_cast<std::size_t>(cell)] ||
            cliffRate == 0.0) continue;
        const int row = cell / columns;
        bool eligible = true;
        for (int north = cell - columns; north >= 0; north -= columns) {
            if (ports[static_cast<std::size_t>(north)] != 0 || decorated[static_cast<std::size_t>(north)]) {
                eligible = false;
                break;
            }
        }
        if (!eligible || !std::bernoulli_distribution(1.0 - std::pow(1.0 - cliffRate, row + 1))(random)) continue;
        const CellBounds bounds = boundsFor(cell);
        const int centerY = (bounds.y0 + bounds.y1) / 2;
        const CellBounds cliff{bounds.x0, bounds.y0, bounds.x1, centerY - 3};
        if (cliff.y1 < cliff.y0) continue;
        AddVariant(plan, cell, "cliff", cliff);
        cliffCells[static_cast<std::size_t>(cell)] = 1;
        decorated[static_cast<std::size_t>(cell)] = true;

        for (int north = cell - columns; north >= 0; north -= columns) {
            const CellBounds empty = boundsFor(north);
            cliffCells[static_cast<std::size_t>(north)] = 2;
            AddVariant(plan, north, "cliff_only", empty);
            decorated[static_cast<std::size_t>(north)] = true;
        }
    }

    bool cliffsChanged = true;
    while (cliffsChanged) {
        cliffsChanged = false;
        const auto previousCliffs = cliffCells;
        for (int cell = 0; cell < cellCount; ++cell) {
            if (ports[static_cast<std::size_t>(cell)] != 0 || decorated[static_cast<std::size_t>(cell)]) continue;
            const int column = cell % columns;
            const bool besideCliff =
                (column > 0 && previousCliffs[static_cast<std::size_t>(cell - 1)] != 0) ||
                (column + 1 < columns && previousCliffs[static_cast<std::size_t>(cell + 1)] != 0);
            const bool northOfCliff = cell + columns < cellCount &&
                previousCliffs[static_cast<std::size_t>(cell + columns)] != 0;
            if (!besideCliff && !northOfCliff) continue;
            cliffCells[static_cast<std::size_t>(cell)] = 2;
            decorated[static_cast<std::size_t>(cell)] = true;
            AddVariant(plan, cell, "cliff_only", boundsFor(cell));
            cliffsChanged = true;
        }
    }

    std::vector<bool> cliffSpaces(static_cast<std::size_t>(cellCount), false);
    for (int row = 0; row < rows; ++row) {
        int column = 0;
        while (column < columns) {
            std::vector<int> candidates;
            while (column < columns && cliffCells[static_cast<std::size_t>(row * columns + column)] != 0) {
                const int cell = row * columns + column++;
                if (cliffCells[static_cast<std::size_t>(cell)] == 1) candidates.push_back(cell);
            }
            if (!candidates.empty()) {
                const int cell = candidates[std::uniform_int_distribution<std::size_t>(
                    0, candidates.size() - 1)(random)];
                cliffSpaces[static_cast<std::size_t>(cell)] = true;
                const CellBounds bounds = boundsFor(cell);
                const int centerX = (bounds.x0 + bounds.x1) / 2;
                const int centerY = (bounds.y0 + bounds.y1) / 2;
                for (int y = std::max(bounds.y0 + 1, centerY - 2);
                    y <= std::min(bounds.y1 - 1, centerY + 2); ++y) {
                    for (int x = std::max(bounds.x0 + 1, centerX - 2);
                        x <= std::min(bounds.x1 - 1, centerX + 2); ++x) {
                        auto& biome = plan.biomes[static_cast<std::size_t>(y * plan.width + x)];
                        if (biome != INNERROAD) biome = PLAIN;
                    }
                }
            }
            ++column;
        }
    }

    // Resolve insets only after every cliff footprint is known, including path cells.
    const auto cliffBoundsFor = [&](int cell) {
        CellBounds bounds = boundsFor(cell);
        if (cliffCells[static_cast<std::size_t>(cell)] == 1) {
            bounds.y1 = (bounds.y0 + bounds.y1) / 2 - 3;
            const int column = cell % columns;
            const int inset = std::min(3, (bounds.x1 - bounds.x0) / 2);
            if (column == 0 || cliffCells[static_cast<std::size_t>(cell - 1)] == 0) bounds.x0 += inset;
            if (column + 1 == columns || cliffCells[static_cast<std::size_t>(cell + 1)] == 0) bounds.x1 -= inset;
        }
        return bounds;
    };
    for (auto& region : plan.regions) {
        if (region.kind != "forest_variant" || region.role != "cliff") continue;
        const CellBounds bounds = cliffBoundsFor(region.parentId);
        region.x0 = bounds.x0;
        region.x1 = bounds.x1;
        region.objectVariant = cliffSpaces[static_cast<std::size_t>(region.parentId)] ? 1 : 0;
    }
    CellMask cliffMask(plan.biomes.size(), 0);
    for (int cell = 0; cell < cellCount; ++cell) {
        if (cliffCells[static_cast<std::size_t>(cell)] == 0) continue;
        const CellBounds bounds = cliffBoundsFor(cell);
        for (int y = bounds.y0; y <= bounds.y1; ++y) {
            for (int x = bounds.x0; x <= bounds.x1; ++x) {
                const auto index = static_cast<std::size_t>(y * plan.width + x);
                if (plan.biomes[index] == FOREST) cliffMask[index] = 1;
            }
        }
    }
    for (int cell = 0; cell < cellCount; ++cell) {
        if (cliffCells[static_cast<std::size_t>(cell)] == 0) continue;
        const CellBounds bounds = boundsFor(cell);
        for (int y = bounds.y0; y <= bounds.y1; ++y) {
            for (int x = bounds.x0; x <= bounds.x1; ++x) {
                const auto index = static_cast<std::size_t>(y * plan.width + x);
                if (cliffMask[index] == 0) continue;
                bool inset = false;
                bool neighborJoin = false;
                if (cliffCells[static_cast<std::size_t>(cell)] == 2) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            const int column = cell % columns + dx;
                            const int row = cell / columns + dy;
                            if (column < 0 || column >= columns || row < 0 || row >= rows) continue;
                            const CellBounds neighbor = boundsFor(row * columns + column);
                            const int nx = std::clamp(x, neighbor.x0, neighbor.x1);
                            const int ny = std::clamp(y, neighbor.y0, neighbor.y1);
                            if (std::max(std::abs(nx - x), std::abs(ny - y)) > 3) continue;
                            const bool neighborCliff =
                                cliffMask[static_cast<std::size_t>(ny * plan.width + nx)] != 0;
                            if (neighborCliff) neighborJoin = true;
                            if (!neighborCliff) inset = true;
                            for (int sy = std::max(neighbor.y0, y - 3);
                                sy <= std::min(neighbor.y1, y + 3) && !neighborJoin; ++sy) {
                                for (int sx = std::max(neighbor.x0, x - 3);
                                    sx <= std::min(neighbor.x1, x + 3); ++sx) {
                                    if (cliffMask[static_cast<std::size_t>(sy * plan.width + sx)] != 0) {
                                        neighborJoin = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                if (!inset || neighborJoin) plan.biomes[index] = CLIFF;
            }
        }
    }
}

bool IsBoundaryCell(int column, int row, int columns, int rows, int direction) {
    switch (direction) {
    case North: return row == 0;
    case East: return column == columns - 1;
    case South: return row == rows - 1;
    default: return column == 0;
    }
}

EntranceCandidate MakeEntranceCandidate(int column, int row, int columns, int rows,
    int width, int height, int direction) {
    const CellBounds bounds = GetCellBounds(column, row, columns, rows, width, height);
    int x = (bounds.x0 + bounds.x1) / 2;
    int y = (bounds.y0 + bounds.y1) / 2;
    if (direction == North) y = 0;
    else if (direction == East) x = width - 1;
    else if (direction == South) y = height - 1;
    else x = 0;
    return {row * columns + column, direction, x, y};
}

std::vector<GridEdge> MakeEdges(int columns, int rows) {
    std::vector<GridEdge> edges;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int cell = row * columns + column;
            if (column + 1 < columns) edges.push_back({cell, cell + 1, East});
            if (row + 1 < rows) edges.push_back({cell, cell + columns, South});
        }
    }
    return edges;
}

bool ActiveCellsConnected(const std::vector<bool>& active, int columns, int rows) {
    const auto start = std::find(active.begin(), active.end(), true);
    if (start == active.end()) return false;
    std::vector<bool> visited(active.size(), false);
    std::vector<int> pending = {static_cast<int>(start - active.begin())};
    visited[static_cast<std::size_t>(pending.front())] = true;
    std::size_t reached = 0;
    while (!pending.empty()) {
        const int cell = pending.back();
        pending.pop_back();
        ++reached;
        const int column = cell % columns;
        const int row = cell / columns;
        for (std::size_t i = 0; i < kDirections.size(); ++i) {
            const int nextColumn = column + kDeltaX[i];
            const int nextRow = row + kDeltaY[i];
            if (nextColumn < 0 || nextColumn >= columns || nextRow < 0 || nextRow >= rows) continue;
            const int neighbor = nextRow * columns + nextColumn;
            if (!active[static_cast<std::size_t>(neighbor)] || visited[static_cast<std::size_t>(neighbor)]) continue;
            visited[static_cast<std::size_t>(neighbor)] = true;
            pending.push_back(neighbor);
        }
    }
    return reached == static_cast<std::size_t>(std::count(active.begin(), active.end(), true));
}

}  // namespace

void ForestMapFilters::Naturalize(MapPlan& plan, std::mt19937& random) {
    const auto original = plan.biomes;
    for (int y = 1; y + 1 < plan.depth; ++y) {
        for (int x = 1; x + 1 < plan.width; ++x) {
            const auto index = static_cast<std::size_t>(y * plan.width + x);
            const auto center = original[index];
            if (center != PLAIN && center != OPEN && center != LAKE &&
                center != FOREST && center != CLIFF) continue;
            const auto at = [&](int dx, int dy) {
                return original[static_cast<std::size_t>((y + dy) * plan.width + x + dx)];
            };
            int cliffs = 0;
            bool forestNeighbor = false;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    cliffs += at(dx, dy) == CLIFF ? 1 : 0;
                    forestNeighbor = forestNeighbor || at(dx, dy) == FOREST;
                }
            }
            if (center == FOREST) {
                if (cliffs >= 5) plan.biomes[index] = CLIFF;
                continue;
            }
            if ((center == PLAIN || center == OPEN || center == LAKE) && cliffs > 0) continue;
            if (center == LAKE && forestNeighbor) continue;
            for (const int sx : {-1, 1}) {
                bool replaced = false;
                for (const int sy : {-1, 1}) {
                    if (at(sx, 0) != center || at(0, sy) != center || at(sx, sy) != center) continue;
                    const auto surrounding = at(-sx, -sy);
                    if (surrounding == INNERROAD) continue;
                    if (surrounding == center || (center == CLIFF && surrounding != FOREST)) continue;
                    if (at(-sx, 0) != surrounding || at(0, -sy) != surrounding ||
                        at(sx, -sy) != surrounding || at(-sx, sy) != surrounding) continue;
                    if ((center != PLAIN && center != OPEN) || std::bernoulli_distribution(0.7)(random)) {
                        plan.biomes[index] = surrounding;
                    }
                    replaced = true;
                    break;
                }
                if (replaced) break;
            }
        }
    }
}

ForestMapImageGenerator::ForestMapImageGenerator(MapGenParams params)
    : MapImageGenerator(std::move(params)) {}

MapGenColor ForestMapImageGenerator::BiomeToColor(std::uint8_t biome) const {
    switch (biome) {
    case PLAIN: return {151, 196, 91};
    case ROAD: return {154, 116, 76};
    case INNERROAD: return {190, 156, 100};
    case OPEN: return {239, 239, 223};
    case FOREST: return {28, 83, 43};
    case DEEPFOREST: return {17, 54, 29};
    case CLIFF: return {114, 78, 51};
    case BRIDGE: return {176, 153, 114};
    case RIVER: return {66, 122, 190};
    case LAKE: return {80, 148, 196};
    default: return {255, 0, 255};
    }
}

bool ForestMapImageGenerator::Generate(MapPlan& outPlan, std::string& outError) {
    outError.clear();
    if (params_.width < 32 || params_.depth < 32 || params_.width > 4096 || params_.depth > 4096) {
        outError = "Forest map dimensions must be between 32 and 4096 pixels.";
        return false;
    }
    const int width = params_.width - 4;
    const int height = params_.depth - 4;

    const int complexity = std::clamp(static_cast<int>(std::lround(GetParam("complexity", 3.0))), 1, 5);
    const int entranceCount = std::clamp(static_cast<int>(std::lround(GetParam("entranceCount", 2.0))), 1, 3);
    const int baseCellSize = std::max(1, std::min(width, height) / complexity);
    const int columns = std::clamp((width + baseCellSize / 2) / baseCellSize, 1, width / 5);
    const int rows = std::clamp((height + baseCellSize / 2) / baseCellSize, 1, height / 5);
    const int cellMinWidth = width / columns;
    const int cellMinHeight = height / rows;
    if (std::min(cellMinWidth, cellMinHeight) < 5) {
        outError = "Forest map is too small for the requested complexity.";
        return false;
    }
    constexpr int corridorWidth = 3;
    const int cellCount = columns * rows;

    outPlan.Reset(width, height, params_.seed, "forest");
    outPlan.archetype = "cell_maze";
    outPlan.environment = "forest";
    outPlan.biomes.assign(static_cast<std::size_t>(width) * height, FOREST);

    std::mt19937 random(params_.seed);
    const auto rateParameter = [&](const std::string& key, double defaultMin, double defaultMax, double& rate) {
        const auto parameter = params_.extra.find(key);
        if (parameter != params_.extra.end()) {
            if (!std::isfinite(parameter->second)) {
                outError = key + " must be a finite number.";
                return false;
            }
            rate = std::clamp(parameter->second, 0.0, 1.0);
            return true;
        }
        rate = std::uniform_real_distribution<double>(defaultMin, defaultMax)(random);
        return true;
    };
    double blankCellRate = 0.0;
    double deadEndRate = 0.0;
    double deadEndBranchRate = 0.2;
    if (!rateParameter("blankCellRate", 0.05, 0.20, blankCellRate) ||
        !rateParameter("deadEndRate", 0.25, 0.65, deadEndRate)) return false;
    if (!rateParameter("deadEndBranchRate", 0.2, 0.2, deadEndBranchRate)) return false;
    std::array<double, 5> variantRates{};
    const std::array<const char*, 5> variantNames = {"clearingRate", "lakeRate", "riverRate", "cliffRate", "meanderRate"};
    const std::array<double, 5> variantDefaults = {0.2, 0.5, 0.5, 0.5, 0.5};
    for (std::size_t i = 0; i < variantRates.size(); ++i) {
        const double value = GetParam(variantNames[i], variantDefaults[i]);
        if (!std::isfinite(value)) {
            outError = std::string(variantNames[i]) + " must be a finite number.";
            return false;
        }
        variantRates[i] = std::clamp(value, 0.0, 1.0);
    }

    const double deepForestDistanceValue = GetParam("deepForestDistance", 5.0);
    if (!std::isfinite(deepForestDistanceValue)) {
        outError = "deepForestDistance must be a finite number.";
        return false;
    }
    const int deepForestDistance = static_cast<int>(
        std::lround(std::clamp(deepForestDistanceValue, 1.0, static_cast<double>(params_.width + params_.depth))));

    std::vector<EntranceCandidate> candidates;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            for (const int direction : kDirections) {
                if (IsBoundaryCell(column, row, columns, rows, direction)) {
                    candidates.push_back(MakeEntranceCandidate(
                        column, row, columns, rows, width, height, direction));
                }
            }
        }
    }
    std::shuffle(candidates.begin(), candidates.end(), random);
    std::vector<EntranceCandidate> entrances;
    const int minimumEntranceDistance = std::max(12, baseCellSize * 2 / 3);
    for (const EntranceCandidate& candidate : candidates) {
        bool separated = true;
        for (const EntranceCandidate& prior : entrances) {
            if ((prior.cell == candidate.cell && prior.direction == candidate.direction) ||
                std::hypot(candidate.x - prior.x, candidate.y - prior.y) < minimumEntranceDistance) {
                separated = false;
                break;
            }
        }
        if (!separated) continue;
        entrances.push_back(candidate);
        if (static_cast<int>(entrances.size()) == entranceCount) break;
    }
    if (static_cast<int>(entrances.size()) != entranceCount) {
        outError = "Could not place the requested number of separated forest entrances.";
        return false;
    }

    std::vector<bool> active(static_cast<std::size_t>(cellCount), true);
    std::vector<bool> entranceCell(static_cast<std::size_t>(cellCount), false);
    for (const EntranceCandidate& entrance : entrances) {
        entranceCell[static_cast<std::size_t>(entrance.cell)] = true;
    }
    std::vector<int> blankCandidates;
    blankCandidates.reserve(static_cast<std::size_t>(cellCount));
    for (int cell = 0; cell < cellCount; ++cell) {
        if (!entranceCell[static_cast<std::size_t>(cell)]) blankCandidates.push_back(cell);
    }
    std::shuffle(blankCandidates.begin(), blankCandidates.end(), random);
    const int requestedBlankCount = static_cast<int>(std::lround(blankCellRate * cellCount));
    int blankCount = 0;
    for (const int cell : blankCandidates) {
        if (blankCount >= requestedBlankCount) break;
        active[static_cast<std::size_t>(cell)] = false;
        if (ActiveCellsConnected(active, columns, rows)) {
            ++blankCount;
        } else {
            active[static_cast<std::size_t>(cell)] = true;
        }
    }

    std::vector<int> ports(static_cast<std::size_t>(cellCount), 0);
    std::vector<int> parent(static_cast<std::size_t>(cellCount), -1);
    const int root = static_cast<int>(std::find(active.begin(), active.end(), true) - active.begin());
    std::vector<int> stack = {root};
    parent[static_cast<std::size_t>(root)] = root;

    while (!stack.empty()) {
        const int cell = stack.back();
        const int column = cell % columns;
        const int row = cell / columns;
        std::array<int, 4> order = {0, 1, 2, 3};
        std::shuffle(order.begin(), order.end(), random);
        bool advanced = false;
        for (const int directionIndex : order) {
            const int direction = kDirections[static_cast<std::size_t>(directionIndex)];
            const int nextColumn = column + kDeltaX[static_cast<std::size_t>(directionIndex)];
            const int nextRow = row + kDeltaY[static_cast<std::size_t>(directionIndex)];
            if (nextColumn < 0 || nextColumn >= columns || nextRow < 0 || nextRow >= rows) continue;
            const int neighbor = nextRow * columns + nextColumn;
            if (!active[static_cast<std::size_t>(neighbor)]) continue;
            if (parent[static_cast<std::size_t>(neighbor)] != -1) continue;
            ports[static_cast<std::size_t>(cell)] |= direction;
            ports[static_cast<std::size_t>(neighbor)] |= Opposite(direction);
            parent[static_cast<std::size_t>(neighbor)] = cell;
            stack.push_back(neighbor);
            advanced = true;
            break;
        }
        if (!advanced) stack.pop_back();
    }

    for (const EntranceCandidate& entrance : entrances) {
        ports[static_cast<std::size_t>(entrance.cell)] |= entrance.direction;
    }

    std::vector<bool> originalDeadEnd(static_cast<std::size_t>(cellCount), false);
    for (int cell = 0; cell < cellCount; ++cell) {
        originalDeadEnd[static_cast<std::size_t>(cell)] =
            active[static_cast<std::size_t>(cell)] && CountPorts(ports[static_cast<std::size_t>(cell)]) == 1;
    }
    auto edges = MakeEdges(columns, rows);
    std::shuffle(edges.begin(), edges.end(), random);
    for (const GridEdge& edge : edges) {
        if (!active[static_cast<std::size_t>(edge.cell)] ||
            !active[static_cast<std::size_t>(edge.neighbor)] ||
            (ports[static_cast<std::size_t>(edge.cell)] & edge.direction) != 0) continue;
        const bool touchesDeadEnd = originalDeadEnd[static_cast<std::size_t>(edge.cell)] ||
            originalDeadEnd[static_cast<std::size_t>(edge.neighbor)];
        const double loopChance = touchesDeadEnd ? 1.0 - deadEndRate : 0.12;
        if (std::bernoulli_distribution(loopChance)(random)) {
            ports[static_cast<std::size_t>(edge.cell)] |= edge.direction;
            ports[static_cast<std::size_t>(edge.neighbor)] |= Opposite(edge.direction);
        }
    }

    std::vector<int> deadEndBranches(static_cast<std::size_t>(cellCount), 0);
    for (int cell = 0; cell < cellCount; ++cell) {
        if (!active[static_cast<std::size_t>(cell)]) continue;
        const int portsForCell = ports[static_cast<std::size_t>(cell)];
        const int portCount = CountPorts(portsForCell);
        if (portCount != 2 && portCount != 3) continue;
        if (!std::bernoulli_distribution(deadEndBranchRate)(random)) continue;

        std::array<int, 4> missingDirections{};
        int missingCount = 0;
        for (const int direction : kDirections) {
            if ((portsForCell & direction) == 0) missingDirections[static_cast<std::size_t>(missingCount++)] = direction;
        }
        if (missingCount == 0) continue;
        const int directionIndex = std::uniform_int_distribution<int>(0, missingCount - 1)(random);
        deadEndBranches[static_cast<std::size_t>(cell)] =
            missingDirections[static_cast<std::size_t>(directionIndex)];
    }

    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int cell = row * columns + column;
            const int mask = ports[static_cast<std::size_t>(cell)];
            const CellBounds bounds = GetCellBounds(column, row, columns, rows, width, height);
            const int spurDirection = deadEndBranches[static_cast<std::size_t>(cell)];
            const int deadEndLength = std::max(1, std::min(bounds.x1 - bounds.x0, bounds.y1 - bounds.y0) / 3);
            if (active[static_cast<std::size_t>(cell)]) {
                DrawCell(outPlan, bounds, mask, corridorWidth, spurDirection, deadEndLength);
            }

            MapPlanRegion region;
            region.kind = "forest_cell";
            region.id = cell;
            region.parentId = row;
            region.parentIndex = column;
            const std::string baseType = TileType(mask);
            region.role = spurDirection == 0 ? baseType : baseType + "_with_dead_end";
            region.x0 = bounds.x0;
            region.y0 = bounds.y0;
            region.x1 = bounds.x1;
            region.y1 = bounds.y1;
            region.width = active[static_cast<std::size_t>(cell)] ? corridorWidth : 0;
            region.rotation = TileRotation(mask);
            region.objectType = mask;
            region.objectVariant = spurDirection;
            outPlan.regions.push_back(std::move(region));
        }
    }

    ApplyCellVariants(outPlan, columns, rows, ports, deadEndBranches,
        variantRates[0], variantRates[1], variantRates[2], variantRates[3], random);
    FillCliffsNorthward(outPlan);
    ApplyMeandering(outPlan, columns, rows, variantRates[4], random);
    ExtendMapEdges(outPlan);
    ForestMapFilters::Naturalize(outPlan, random);
    FillDeepForest(outPlan, deepForestDistance);

    for (const EntranceCandidate& entrance : entrances) {
        const int x = entrance.direction == West ? 0 :
            entrance.direction == East ? outPlan.width - 1 : entrance.x + 2;
        const int y = entrance.direction == North ? 0 :
            entrance.direction == South ? outPlan.depth - 1 : entrance.y + 2;
        outPlan.markers.push_back({"entrance", x, y});
    }
    return true;
}
