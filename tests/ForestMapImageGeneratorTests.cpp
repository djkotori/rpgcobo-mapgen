#include "ForestMapImageGenerator.h"
#include "MapQuality.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int North = 1;
constexpr int East = 2;
constexpr int South = 4;
constexpr int West = 8;
constexpr std::array<int, 4> Directions = {North, East, South, West};
constexpr std::array<int, 4> DeltaX = {0, 1, 0, -1};
constexpr std::array<int, 4> DeltaY = {-1, 0, 1, 0};

struct Cell {
    int row;
    int column;
    int ports;
    int x0;
    int y0;
    int x1;
    int y1;
    int corridorWidth;
};

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

MapGenParams BasicParams() {
    MapGenParams params;
    params.width = 40;
    params.depth = 40;
    params.extra["clearingRate"] = 0.0;
    params.extra["riverRate"] = 0.0;
    params.extra["cliffRate"] = 0.0;
    params.extra["meanderRate"] = 0.0;
    return params;
}

int Opposite(int direction) {
    return direction == North ? South : direction == East ? West :
        direction == South ? North : East;
}

int Count(const MapPlan& plan, std::uint8_t biome) {
    return static_cast<int>(std::count(plan.biomes.begin(), plan.biomes.end(), biome));
}

bool Road(std::uint8_t biome) {
    return biome == ROAD || biome == INNERROAD;
}

std::uint8_t WithoutDeepForest(std::uint8_t biome) {
    return biome == DEEPFOREST ? static_cast<std::uint8_t>(FOREST) : biome;
}

bool NaturalizedPixelMatches(const MapPlan& original, int x, int y, std::uint8_t actual) {
    const auto at = [&](int dx, int dy) {
        return WithoutDeepForest(original.biomes[static_cast<std::size_t>(
            (y + dy) * original.width + x + dx)]);
    };
    actual = WithoutDeepForest(actual);
    const auto center = at(0, 0);
    if (center == INNERROAD) {
        for (std::size_t direction = 0; direction < Directions.size(); ++direction) {
            const int nx = x + DeltaX[direction];
            const int ny = y + DeltaY[direction];
            if (nx < 0 || ny < 0 || nx >= original.width || ny >= original.depth) continue;
            const auto neighbor = original.biomes[static_cast<std::size_t>(ny * original.width + nx)];
            if (neighbor == FOREST || neighbor == CLIFF) return actual == ROAD;
        }
        return actual == INNERROAD;
    }
    if (x == 0 || y == 0 || x + 1 == original.width || y + 1 == original.depth) return actual == center;
    int cliffs = 0;
    bool forest = false;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            cliffs += at(dx, dy) == CLIFF ? 1 : 0;
            forest = forest || at(dx, dy) == FOREST;
        }
    }
    if (center == FOREST) return actual == (cliffs >= 5 ? CLIFF : FOREST);
    if (center != PLAIN && center != OPEN && center != LAKE && center != CLIFF) return actual == center;
    if (center != CLIFF && (cliffs > 0 || (center == LAKE && forest))) return actual == center;
    for (int corner = 0; corner < 4; ++corner) {
        const int sx = corner % 2 == 0 ? -1 : 1;
        const int sy = corner < 2 ? -1 : 1;
        const auto outside = at(-sx, -sy);
        bool matches = outside != center && outside != INNERROAD && (center != CLIFF || outside == FOREST);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const bool inside = (dx == 0 || dx == sx) && (dy == 0 || dy == sy);
                matches = matches && at(dx, dy) == (inside ? center : outside);
            }
        }
        if (matches) return actual == outside || ((center == PLAIN || center == OPEN) && actual == center);
    }
    return actual == center;
}

MapPlan RestoreRoomShapes(MapPlan plan) {
    for (auto& biome : plan.biomes) biome = WithoutDeepForest(biome);
    for (const auto& region : plan.regions) {
        if (region.kind != "forest_variant" || (region.role != "clearing" && region.role != "lake")) continue;
        const auto& cell = plan.regions[static_cast<std::size_t>(region.parentId)];
        const int x0 = std::clamp((cell.x0 + cell.x1) / 2 - region.width / 2,
            cell.x0 + 1, cell.x1 - region.width);
        const int y0 = std::clamp((cell.y0 + cell.y1) / 2 - region.width / 2,
            cell.y0 + 1, cell.y1 - region.width);
        for (int y = y0; y < y0 + region.width; ++y) {
            for (int x = x0; x < x0 + region.width; ++x) {
                const bool inside = x >= region.x0 && x <= region.x1 && y >= region.y0 && y <= region.y1;
                const bool bank = region.role == "lake" && x >= region.x0 - 2 && x <= region.x1 + 2 &&
                    y >= region.y0 - 2 && y <= region.y1 + 2;
                plan.biomes[static_cast<std::size_t>(y * plan.width + x)] = static_cast<std::uint8_t>(
                    inside ? (region.role == "lake" ? LAKE : OPEN) : bank ? OPEN : PLAIN);
            }
        }
    }
    return plan;
}

std::vector<const MapPlanRegion*> Cells(const MapPlan& plan) {
    std::vector<const MapPlanRegion*> cells;
    for (const auto& region : plan.regions) {
        if (region.kind == "forest_cell") cells.push_back(&region);
    }
    return cells;
}

int CountMarkers(const MapPlan& plan, const std::string& kind) {
    return static_cast<int>(std::count_if(plan.markers.begin(), plan.markers.end(), [&](const MapPlanMarker& marker) {
        return marker.kind == kind;
    }));
}

int CountCells(const MapPlan& plan, const std::string& role) {
    return static_cast<int>(std::count_if(plan.regions.begin(), plan.regions.end(), [&](const MapPlanRegion& region) {
        return region.kind == "forest_cell" && region.role == role;
    }));
}

int CountPorts(int mask) {
    int count = 0;
    for (const int direction : Directions) {
        if ((mask & direction) != 0) ++count;
    }
    return count;
}

int CountDeadEndBranches(const MapPlan& plan) {
    return static_cast<int>(std::count_if(plan.regions.begin(), plan.regions.end(), [](const MapPlanRegion& region) {
        return region.kind == "forest_cell" && region.objectVariant > 0;
    }));
}

void CheckCellRaster(const MapPlan& plan, const Cell& cell, const MapPlanRegion& metadata) {
    const int centerX = (cell.x0 + cell.x1) / 2;
    const int centerY = (cell.y0 + cell.y1) / 2;
    const int cellWidth = cell.x1 - cell.x0 + 1;
    const int cellHeight = cell.y1 - cell.y0 + 1;
    const int half = cell.corridorWidth / 2;
    std::array<int, 4> portX{};
    std::array<int, 4> portY{};
    for (std::size_t i = 0; i < Directions.size(); ++i) {
        const int direction = Directions[i];
        if ((cell.ports & direction) == 0) continue;
        portX[i] = direction == North || direction == South
            ? centerX : direction == West ? cell.x0 : cell.x1;
        portY[i] = direction == East || direction == West
            ? centerY : direction == North ? cell.y0 : cell.y1;
    }

    const int deadEndLength = cell.corridorWidth == 0 ? 0 :
        std::max(1, std::min(cell.x1 - cell.x0, cell.y1 - cell.y0) / 3);

    for (int y = cell.y0; y <= cell.y1; ++y) {
        for (int x = cell.x0; x <= cell.x1; ++x) {
            bool expected = false;
            for (std::size_t i = 0; i < Directions.size(); ++i) {
                const int direction = Directions[i];
                if ((cell.ports & direction) == 0) continue;
                if (direction == North || direction == South) {
                    const int y0 = std::min(portY[i], centerY);
                    const int y1 = std::max(portY[i], centerY);
                    expected = (x >= portX[i] - half && x <= portX[i] + cell.corridorWidth - half - 1 &&
                        y >= y0 && y <= y1) || expected;
                } else {
                    const int x0 = std::min(portX[i], centerX);
                    const int x1 = std::max(portX[i], centerX);
                    expected = (y >= portY[i] - half && y <= portY[i] + cell.corridorWidth - half - 1 &&
                        x >= x0 && x <= x1) || expected;
                }
            }
            if (metadata.objectVariant != 0) {
                const int endX = metadata.objectVariant == West ? centerX - deadEndLength :
                    metadata.objectVariant == East ? centerX + deadEndLength : centerX;
                const int endY = metadata.objectVariant == North ? centerY - deadEndLength :
                    metadata.objectVariant == South ? centerY + deadEndLength : centerY;
                if (metadata.objectVariant == North || metadata.objectVariant == South) {
                    expected = expected || (x >= centerX - half && x <= centerX + cell.corridorWidth - half - 1 &&
                        y >= std::min(centerY, endY) && y <= std::max(centerY, endY));
                } else {
                    expected = expected || (y >= centerY - half && y <= centerY + cell.corridorWidth - half - 1 &&
                        x >= std::min(centerX, endX) && x <= std::max(centerX, endX));
                }
            }
            const auto biome = plan.biomes[static_cast<std::size_t>(y * plan.width + x)];
            const bool actual = Road(biome);
            Require(actual == expected, "cell path contains pixels outside its straight arms and optional spur");
            bool inner = false;
            for (std::size_t i = 0; i < Directions.size(); ++i) {
                if ((cell.ports & Directions[i]) == 0) continue;
                inner = inner || (x >= std::min(centerX, portX[i]) && x <= std::max(centerX, portX[i]) &&
                    y >= std::min(centerY, portY[i]) && y <= std::max(centerY, portY[i]));
            }
            if (metadata.objectVariant != 0) {
                const int endX = centerX + (metadata.objectVariant == West ? -deadEndLength :
                    metadata.objectVariant == East ? deadEndLength : 0);
                const int endY = centerY + (metadata.objectVariant == North ? -deadEndLength :
                    metadata.objectVariant == South ? deadEndLength : 0);
                inner = inner || (x >= std::min(centerX, endX) && x <= std::max(centerX, endX) &&
                    y >= std::min(centerY, endY) && y <= std::max(centerY, endY));
            }
            bool touchesTerrain = false;
            for (std::size_t direction = 0; direction < Directions.size(); ++direction) {
                const int nx = x + DeltaX[direction];
                const int ny = y + DeltaY[direction];
                if (nx < 0 || ny < 0 || nx >= plan.width || ny >= plan.depth) continue;
                const auto neighbor = WithoutDeepForest(plan.biomes[static_cast<std::size_t>(ny * plan.width + nx)]);
                touchesTerrain = touchesTerrain || neighbor == FOREST || neighbor == CLIFF;
            }
            Require((biome == INNERROAD) == (inner && !touchesTerrain),
                "INNERROAD does not follow the filtered one-pixel cell centerline");
        }
    }
}

void CheckCellMaze(const MapPlan& plan, int expectedColumns, int expectedRows, int expectedEntrances) {
    Require(plan.type == "forest" && plan.archetype == "cell_maze", "cell maze metadata was not set");
    const auto regions = Cells(plan);
    Require(static_cast<int>(regions.size()) == expectedColumns * expectedRows,
        "cell grid has an unexpected number of cells");
    Require(Count(plan, FOREST) > 0 && Count(plan, ROAD) > 0,
        "maze is missing forest or walkable corridors");
    Require(Count(plan, OPEN) == 0 && Count(plan, PLAIN) == 0,
        "initial cell maze should contain only forest and road biomes");
    for (const std::uint8_t biome : plan.biomes) {
        Require(WithoutDeepForest(biome) == FOREST || Road(biome),
            "initial cell maze generated a biome other than forest or road");
    }

    std::map<std::pair<int, int>, Cell> grid;
    for (const MapPlanRegion* region : regions) {
        Require(region->kind == "forest_cell" && region->objectType >= 0 && region->objectType <= 15,
            "cell metadata is invalid");
        Require(region->parentId >= 0 && region->parentId < expectedRows &&
            region->parentIndex >= 0 && region->parentIndex < expectedColumns,
            "cell grid coordinates are invalid");
        Require(grid.emplace(std::make_pair(region->parentId, region->parentIndex),
            Cell{region->parentId, region->parentIndex, region->objectType,
                    region->x0, region->y0, region->x1, region->y1, region->width}).second,
            "cell grid contains duplicate coordinates");
        Require(region->role == "blank" || region->role == "dead_end" || region->role == "straight" ||
            region->role == "corner" || region->role == "tee" || region->role == "cross" ||
            region->role == "straight_with_dead_end" || region->role == "corner_with_dead_end" ||
            region->role == "tee_with_dead_end",
            "connected maze contains an unsupported cell type");
    }

    for (const auto& [coordinates, cell] : grid) {
        const int row = coordinates.first;
        const int column = coordinates.second;
        int degree = 0;
        int matchedPorts = 0;
        for (std::size_t i = 0; i < Directions.size(); ++i) {
            const int direction = Directions[i];
            if ((cell.ports & direction) == 0) continue;
            ++degree;
            const int neighborRow = row + DeltaY[i];
            const int neighborColumn = column + DeltaX[i];
            if (neighborRow >= 0 && neighborRow < expectedRows &&
                neighborColumn >= 0 && neighborColumn < expectedColumns) {
                const auto neighbor = grid.find({neighborRow, neighborColumn});
                Require(neighbor != grid.end() && (neighbor->second.ports & Opposite(direction)) != 0,
                    "cell has a connection that does not match the neighboring cell");
                const int centerX = (cell.x0 + cell.x1) / 2;
                const int centerY = (cell.y0 + cell.y1) / 2;
                const int expectedX = direction == North || direction == South
                    ? centerX
                    : direction == West ? cell.x0 : cell.x1;
                const int expectedY = direction == East || direction == West
                    ? centerY
                    : direction == North ? cell.y0 : cell.y1;
                Require(plan.biomes[static_cast<std::size_t>(expectedY * plan.width + expectedX)] == INNERROAD,
                    "cell corridor does not reach its centered boundary connection");
                ++matchedPorts;
            } else {
                const int centerX = (cell.x0 + cell.x1) / 2;
                const int centerY = (cell.y0 + cell.y1) / 2;
                const int boundaryX = direction == West ? 0 : direction == East ? plan.width - 1 :
                    centerX;
                const int boundaryY = direction == North ? 0 : direction == South ? plan.depth - 1 :
                    centerY;
                const bool hasEntrance = std::any_of(plan.markers.begin(), plan.markers.end(),
                    [&](const MapPlanMarker& marker) {
                        return marker.kind == "entrance" && marker.x == boundaryX && marker.y == boundaryY;
                    });
                Require(hasEntrance, "boundary cell has a corridor port without an entrance marker");
                Require(plan.biomes[static_cast<std::size_t>(boundaryY * plan.width + boundaryX)] == INNERROAD,
                    "entrance connector does not meet its centered cell path");
                ++matchedPorts;
            }
        }
        if (cell.ports == 0) {
            Require(degree == 0 && cell.corridorWidth == 0, "blank cell has a path or corridor");
        } else {
            Require(degree > 0 && cell.corridorWidth == 3,
                "active cell corridor width is not fixed at three pixels");
        }
        Require(matchedPorts == degree, "cell has an unpaired or missing connection");
        const std::string expectedType = degree == 1 ? "dead_end" :
            degree == 2 ? ((cell.ports == (North | South) || cell.ports == (East | West)) ?
                "straight" : "corner") :
            degree == 3 ? "tee" : degree == 4 ? "cross" : "blank";
        const auto region = std::find_if(regions.begin(), regions.end(), [&](const MapPlanRegion* candidate) {
            return candidate->parentId == row && candidate->parentIndex == column;
        });
        Require(region != regions.end(), "cell metadata is missing");
        const MapPlanRegion& metadata = **region;
        const bool hasDeadEnd = metadata.objectVariant > 0;
        if (hasDeadEnd) {
            Require((metadata.objectVariant & cell.ports) == 0 &&
                (metadata.objectVariant == North || metadata.objectVariant == East ||
                    metadata.objectVariant == South || metadata.objectVariant == West),
                "internal dead-end branch changes or duplicates a cell connection");
            Require(degree == 2 || degree == 3, "dead-end branch is attached to an unsupported cell type");
            const std::string expectedRole = expectedType + "_with_dead_end";
            Require(metadata.role == expectedRole, "dead-end branch tile metadata is inconsistent");

            const int centerX = (cell.x0 + cell.x1) / 2;
            const int centerY = (cell.y0 + cell.y1) / 2;
            const int sampleOffset = std::max(1, std::min(cell.x1 - cell.x0 + 1, cell.y1 - cell.y0 + 1) / 6);
            const int sampleX = metadata.objectVariant == West ? centerX - sampleOffset :
                metadata.objectVariant == East ? centerX + sampleOffset : centerX;
            const int sampleY = metadata.objectVariant == North ? centerY - sampleOffset :
                metadata.objectVariant == South ? centerY + sampleOffset : centerY;
            Require(Road(plan.biomes[static_cast<std::size_t>(sampleY * plan.width + sampleX)]),
                "dead-end spur geometry is missing inside its cell");
            const int edgeX = metadata.objectVariant == West ? cell.x0 :
                metadata.objectVariant == East ? cell.x1 : centerX;
            const int edgeY = metadata.objectVariant == North ? cell.y0 :
                metadata.objectVariant == South ? cell.y1 : centerY;
            Require(WithoutDeepForest(plan.biomes[static_cast<std::size_t>(edgeY * plan.width + edgeX)]) == FOREST,
                "internal dead-end branch incorrectly opens a neighboring cell");
        } else {
            Require(metadata.role == expectedType, "cell type does not match its connection shape");
        }
        CheckCellRaster(plan, cell, metadata);
    }

    Require(CountMarkers(plan, "entrance") == expectedEntrances, "requested entrance count was not honored");
    for (const auto& marker : plan.markers) {
        if (marker.kind != "entrance") continue;
        Require(marker.x == 0 || marker.x == plan.width - 1 || marker.y == 0 || marker.y == plan.depth - 1,
            "entrance marker is not on the map boundary");
    }

    std::queue<std::pair<int, int>> pending;
    std::map<std::pair<int, int>, bool> visited;
    const auto firstActive = std::find_if(grid.begin(), grid.end(), [](const auto& entry) {
        return entry.second.ports != 0;
    });
    Require(firstActive != grid.end(), "maze has no active cells");
    pending.push(firstActive->first);
    visited[firstActive->first] = true;
    while (!pending.empty()) {
        const auto [row, column] = pending.front();
        pending.pop();
        const Cell& cell = grid.at({row, column});
        for (std::size_t i = 0; i < Directions.size(); ++i) {
            if ((cell.ports & Directions[i]) == 0) continue;
            const int nextRow = row + DeltaY[i];
            const int nextColumn = column + DeltaX[i];
            if (nextRow < 0 || nextRow >= expectedRows || nextColumn < 0 || nextColumn >= expectedColumns) continue;
            const auto nextCell = grid.find({nextRow, nextColumn});
            if (nextCell != grid.end() && nextCell->second.ports != 0 &&
                visited.emplace(std::make_pair(nextRow, nextColumn), true).second) {
                pending.push({nextRow, nextColumn});
            }
        }
    }
    Require(static_cast<int>(visited.size()) == expectedRows * expectedColumns - CountCells(plan, "blank"),
        "not all nonblank cells are reachable from the maze");
    const MapQualityMetrics quality = EvaluateMapQuality(plan);
    Require(quality.markerCount == expectedEntrances, "quality metrics omitted entrance markers");
    Require(quality.reachableMarkers == quality.markerCount, "a maze entrance is unreachable");
}

void TestReproducibleConnectedCellMaze() {
    MapGenParams params = BasicParams();
    params.width = 40;
    params.depth = 40;
    params.seed = 12345;
    params.extra["complexity"] = 3.0;
    params.extra["entranceCount"] = 3.0;

    MapPlan first;
    MapPlan second;
    std::string error;
    ForestMapImageGenerator generator(params);
    Require(generator.Generate(first, error), error);
    Require(generator.Generate(second, error), error);
    Require(first.width == params.width && first.depth == params.depth,
        "Forest generator changed the requested low-resolution dimensions");
    Require(first.biomes == second.biomes, "same seed did not reproduce the biome grid");
    Require(first.markers.size() == second.markers.size(), "same seed did not reproduce markers");
    Require(first.regions.size() == second.regions.size(), "same seed did not reproduce cell layout");
    CheckCellMaze(first, 3, 3, 3);
    Require(Count(first, INNERROAD) > 0, "maze has no inner road centerline");
    const auto color = generator.BiomeToColor(INNERROAD);
    Require(color.r == 190 && color.g == 156 && color.b == 100, "INNERROAD palette color is missing");
    auto withoutCenterline = first;
    std::replace(withoutCenterline.biomes.begin(), withoutCenterline.biomes.end(),
        static_cast<std::uint8_t>(INNERROAD), static_cast<std::uint8_t>(ROAD));
    const auto quality = EvaluateMapQuality(first);
    const auto originalQuality = EvaluateMapQuality(withoutCenterline);
    Require(quality.walkableCells == originalQuality.walkableCells &&
        quality.roadCells == originalQuality.roadCells &&
        quality.largestWalkableComponentRatio == originalQuality.largestWalkableComponentRatio,
        "INNERROAD changed road or walkability metrics");
}

void TestComplexityAndAspectRatio() {
    for (int complexity = 1; complexity <= 5; ++complexity) {
        MapGenParams params = BasicParams();
        params.width = 40;
        params.depth = 40;
        params.seed = static_cast<std::uint32_t>(7200 + complexity);
        params.extra["complexity"] = static_cast<double>(complexity);
        params.extra["entranceCount"] = 2.0;

        MapPlan plan;
        std::string error;
        ForestMapImageGenerator generator(params);
        Require(generator.Generate(plan, error), error);
        CheckCellMaze(plan, complexity, complexity, 2);
    }

    struct AspectCase {
        int width;
        int height;
        int columns;
        int rows;
    };
    for (const AspectCase test : {AspectCase{53, 40, 4, 3}, AspectCase{40, 53, 3, 4}}) {
        MapGenParams params = BasicParams();
        params.width = test.width;
        params.depth = test.height;
        params.seed = 9123;
        params.extra["complexity"] = 3.0;
        params.extra["entranceCount"] = 2.0;
        MapPlan plan;
        std::string error;
        ForestMapImageGenerator generator(params);
        Require(generator.Generate(plan, error), error);
        Require(plan.width == test.width && plan.depth == test.height,
            "Forest generator changed the requested low-resolution dimensions");
        CheckCellMaze(plan, test.columns, test.rows, 2);

        const auto regions = Cells(plan);
        int minCellWidth = test.width;
        int maxCellWidth = 0;
        int minCellHeight = test.height;
        int maxCellHeight = 0;
        for (const MapPlanRegion* cell : regions) {
            const int cellWidth = cell->x1 - cell->x0 + 1;
            const int cellHeight = cell->y1 - cell->y0 + 1;
            minCellWidth = std::min(minCellWidth, cellWidth);
            maxCellWidth = std::max(maxCellWidth, cellWidth);
            minCellHeight = std::min(minCellHeight, cellHeight);
            maxCellHeight = std::max(maxCellHeight, cellHeight);
        }
        Require(maxCellWidth - minCellWidth <= 1 && maxCellHeight - minCellHeight <= 1,
            "cell pixel sizes are not evenly distributed");
    }
}

void TestEntranceLimitsAndInvalidDimensions() {
    for (int entranceCount = 1; entranceCount <= 3; ++entranceCount) {
        MapGenParams params = BasicParams();
        params.width = 40;
        params.depth = 40;
        params.seed = static_cast<std::uint32_t>(4100 + entranceCount);
        params.extra["complexity"] = 3.0;
        params.extra["entranceCount"] = static_cast<double>(entranceCount);
        MapPlan plan;
        std::string error;
        ForestMapImageGenerator generator(params);
        Require(generator.Generate(plan, error), error);
        CheckCellMaze(plan, 3, 3, entranceCount);
    }

    MapGenParams invalid;
    invalid.width = 16;
    invalid.depth = 64;
    MapPlan plan;
    std::string error;
    ForestMapImageGenerator generator(invalid);
    Require(!generator.Generate(plan, error) && !error.empty(),
        "invalid map dimensions were not reported");
}

void TestCellRatesAndRandomDefaults() {
    MapGenParams noBlanks = BasicParams();
    noBlanks.width = 40;
    noBlanks.depth = 40;
    noBlanks.seed = 19231;
    noBlanks.extra["complexity"] = 3.0;
    noBlanks.extra["entranceCount"] = 2.0;
    noBlanks.extra["blankCellRate"] = 0.0;
    noBlanks.extra["deadEndRate"] = 0.0;
    noBlanks.extra["deadEndBranchRate"] = 0.0;

    MapPlan noBlankPlan;
    std::string error;
    ForestMapImageGenerator noBlankGenerator(noBlanks);
    Require(noBlankGenerator.Generate(noBlankPlan, error), error);
    Require(CountCells(noBlankPlan, "blank") == 0, "zero blank-cell rate still generated blank cells");
    Require(CountDeadEndBranches(noBlankPlan) == 0, "zero spur rate still generated dead-end branches");
    CheckCellMaze(noBlankPlan, 3, 3, 2);

    MapGenParams withBlanks = noBlanks;
    withBlanks.extra["blankCellRate"] = 0.34;
    MapPlan blankPlan;
    ForestMapImageGenerator blankGenerator(withBlanks);
    Require(blankGenerator.Generate(blankPlan, error), error);
    Require(CountCells(blankPlan, "blank") > 0 && CountCells(blankPlan, "blank") <= 3,
        "blank-cell rate did not produce the requested bounded number of blanks");
    CheckCellMaze(blankPlan, 3, 3, 2);

    MapGenParams highDeadEnds = noBlanks;
    highDeadEnds.extra["deadEndRate"] = 1.0;
    MapPlan highDeadEndPlan;
    ForestMapImageGenerator highDeadEndGenerator(highDeadEnds);
    Require(highDeadEndGenerator.Generate(highDeadEndPlan, error), error);
    Require(CountCells(noBlankPlan, "dead_end") <= CountCells(highDeadEndPlan, "dead_end"),
        "higher dead-end rate did not preserve more dead-end cells");

    MapGenParams defaults = BasicParams();
    defaults.width = 40;
    defaults.depth = 40;
    defaults.seed = 76543;
    defaults.extra["complexity"] = 3.0;
    defaults.extra["entranceCount"] = 2.0;
    MapPlan first;
    MapPlan second;
    ForestMapImageGenerator defaultGenerator(defaults);
    Require(defaultGenerator.Generate(first, error), error);
    Require(defaultGenerator.Generate(second, error), error);
    Require(first.biomes == second.biomes && first.regions.size() == second.regions.size(),
        "seeded random default rates were not reproducible");
    CheckCellMaze(first, 3, 3, 2);

    MapGenParams allSpurs = BasicParams();
    allSpurs.width = 40;
    allSpurs.depth = 40;
    allSpurs.seed = 19231;
    allSpurs.extra["complexity"] = 5.0;
    allSpurs.extra["entranceCount"] = 2.0;
    allSpurs.extra["blankCellRate"] = 0.0;
    allSpurs.extra["deadEndRate"] = 0.4;
    allSpurs.extra["deadEndBranchRate"] = 1.0;
    MapPlan allSpursPlan;
    ForestMapImageGenerator allSpursGenerator(allSpurs);
    Require(allSpursGenerator.Generate(allSpursPlan, error), error);
    int eligibleCellCount = 0;
    for (const MapPlanRegion& region : allSpursPlan.regions) {
        if (region.kind == "forest_cell" &&
            (CountPorts(region.objectType) == 2 || CountPorts(region.objectType) == 3)) {
            ++eligibleCellCount;
        }
    }
    Require(eligibleCellCount > 0 && CountDeadEndBranches(allSpursPlan) == eligibleCellCount,
        "100% spur rate did not replace every eligible straight, corner, and T cell");
    CheckCellMaze(allSpursPlan, 5, 5, 2);

}

bool Walkable(std::uint8_t biome) {
    return biome == PLAIN || biome == OPEN || Road(biome) || biome == BRIDGE;
}

void CheckVariantConnectivity(const MapPlan& plan, const MapPlan& base, int columns, int rows) {
    const auto cells = Cells(plan);
    const auto baseCells = Cells(base);
    std::map<int, const MapPlanRegion*> cliffRegions;
    for (const auto& region : plan.regions) {
        if (region.kind == "forest_variant" && (region.role == "cliff" || region.role == "cliff_only")) {
            cliffRegions.emplace(region.parentId, &region);
        }
    }
    for (int row = 0; row < rows; ++row) {
        int selected = 0;
        int candidates = 0;
        for (int column = 0; column <= columns; ++column) {
            const auto found = column < columns ? cliffRegions.find(row * columns + column) : cliffRegions.end();
            if (found != cliffRegions.end()) {
                if (found->second->role == "cliff") {
                    ++candidates;
                    Require(found->second->objectVariant == 0 || found->second->objectVariant == 1,
                        "cliff space selection metadata is missing");
                    selected += found->second->objectVariant;
                }
            } else {
                Require(selected == (candidates > 0 ? 1 : 0),
                    "horizontal cliff run does not have exactly one widened junction");
                selected = 0;
                candidates = 0;
            }
        }
    }
    Require(cells.size() == baseCells.size(), "variants changed the cell grid");
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto& cell = *cells[i];
        Require(cell.objectType == baseCells[i]->objectType &&
            cell.objectVariant == baseCells[i]->objectVariant, "variant changed graph ports or spurs");
        const auto cliff = cliffRegions.find(cell.id);
        if (cliff != cliffRegions.end() && cliff->second->role == "cliff" &&
            cliff->second->objectVariant == 0) {
            for (int y = cell.y0; y <= cell.y1; ++y) {
                for (int x = cell.x0; x <= cell.x1; ++x) {
                    const auto index = static_cast<std::size_t>(y * plan.width + x);
                    Require(Road(plan.biomes[index]) == Road(base.biomes[index]),
                        "unselected cliff cell still widens the base path");
                }
            }
        }
        const int centerX = (cell.x0 + cell.x1) / 2;
        const int centerY = (cell.y0 + cell.y1) / 2;
        if (cliff != cliffRegions.end() && cliff->second->role == "cliff" &&
            cliff->second->objectVariant == 1) {
            for (int y = std::max(cell.y0 + 1, centerY - 2); y <= std::min(cell.y1 - 1, centerY + 2); ++y) {
                for (int x = std::max(cell.x0 + 1, centerX - 2); x <= std::min(cell.x1 - 1, centerX + 2); ++x) {
                    const auto index = static_cast<std::size_t>(y * plan.width + x);
                    const auto biome = WithoutDeepForest(plan.biomes[index]);
                    const bool corner = (x == std::max(cell.x0 + 1, centerX - 2) ||
                        x == std::min(cell.x1 - 1, centerX + 2)) &&
                        (y == std::max(cell.y0 + 1, centerY - 2) || y == std::min(cell.y1 - 1, centerY + 2));
                    Require(base.biomes[index] != INNERROAD || biome == INNERROAD,
                        "cliff junction space overwrote an INNERROAD centerline");
                    Require(biome == INNERROAD || (biome == ROAD && Road(base.biomes[index])) ||
                        biome == PLAIN || (corner && biome == FOREST),
                        "selected cliff junction is missing its widened space");
                }
            }
        }
        for (std::size_t side = 0; side < Directions.size(); ++side) {
            const bool vertical = side == 0 || side == 2;
            const int low = vertical ? cell.x0 : cell.y0;
            const int high = vertical ? cell.x1 : cell.y1;
            const int center = vertical ? centerX : centerY;
            for (int coordinate = low; coordinate <= high; ++coordinate) {
                const int x = vertical ? coordinate : side == 1 ? cell.x1 : cell.x0;
                const int y = vertical ? (side == 0 ? cell.y0 : cell.y1) : coordinate;
                const bool expected = (cell.objectType & Directions[side]) != 0 &&
                    coordinate >= center - 1 && coordinate <= center + 1;
                Require(Walkable(plan.biomes[static_cast<std::size_t>(y * plan.width + x)]) == expected,
                    "variant changed a cell boundary port or its three-pixel width");
            }
        }
    }
    Require(static_cast<int>(cells.size()) == columns * rows, "unexpected variant grid size");
    const auto quality = EvaluateMapQuality(plan);
    Require(quality.reachableMarkers == quality.markerCount &&
        quality.markerCount == static_cast<int>(plan.markers.size()), "variant blocked an entrance");
    Require(quality.largestWalkableComponentRatio == 1.0, "variant split the walkable pixel network");
}

MapPlan GenerateForest(const MapGenParams& params) {
    MapPlan plan;
    std::string error;
    ForestMapImageGenerator generator(params);
    Require(generator.Generate(plan, error), error);
    return plan;
}

int VariantCount(const MapPlan& plan, const std::string& role) {
    return static_cast<int>(std::count_if(plan.regions.begin(), plan.regions.end(), [&](const MapPlanRegion& region) {
        return region.kind == "forest_variant" && region.role == role;
    }));
}

void TestRoomsAndLakes() {
    std::array<bool, 5> observedDegrees{};
    std::array<bool, 5> observedAnchors{};
    std::array<bool, 5> observedLakeAnchors{};
    int totalRooms = 0;
    int totalLakes = 0;
    double deadEndSize = 0.0;
    double junctionSize = 0.0;
    int deadEndRooms = 0;
    int junctionRooms = 0;
    int eligibleDeadEnds = 0;
    int eligibleOthers = 0;
    int selectedDeadEnds = 0;
    int selectedOthers = 0;
    bool straightRoom = false;
    bool cornerRoom = false;
    for (std::uint32_t seed = 1; seed <= 80; ++seed) {
        MapGenParams params = BasicParams();
        params.seed = seed;
        params.extra["blankCellRate"] = 0.0;
        params.extra["deadEndBranchRate"] = 0.0;
        const MapPlan base = GenerateForest(params);
        params.extra["clearingRate"] = 0.2;
        const MapPlan mixed = GenerateForest(params);
        const int count = VariantCount(mixed, "clearing") + VariantCount(mixed, "lake");
        Require(count == 1 || count == 2, "3x3 combined room budget is not one or two");
        totalRooms += count;
        totalLakes += VariantCount(mixed, "lake");
        CheckVariantConnectivity(mixed, base, 3, 3);
        const auto mixedCells = Cells(mixed);
        for (const auto* cell : mixedCells) {
            if (CountPorts(cell->objectType) == 1) ++eligibleDeadEnds;
            else ++eligibleOthers;
        }
        for (const auto& region : mixed.regions) {
            if (region.kind != "forest_variant") continue;
            if (CountPorts(mixedCells[static_cast<std::size_t>(region.parentId)]->objectType) == 1) {
                ++selectedDeadEnds;
            } else {
                ++selectedOthers;
            }
        }

        params.extra["clearingRate"] = 1.0;
        for (const bool lake : {false, true}) {
            params.extra["lakeRate"] = lake ? 1.0 : 0.0;
            const MapPlan plan = GenerateForest(params);
            Require(VariantCount(plan, lake ? "lake" : "clearing") == 9,
                "100% room or lake selection did not decorate every active cell");
            Require(VariantCount(plan, lake ? "clearing" : "lake") == 0,
                "lakeRate endpoint selected the wrong room type");
            CheckVariantConnectivity(plan, base, 3, 3);
            const auto cells = Cells(plan);
            const auto rectangular = RestoreRoomShapes(plan);
            for (const auto& region : plan.regions) {
                if (region.kind != "forest_variant") continue;
                const auto& cell = *cells[static_cast<std::size_t>(region.parentId)];
                const int degree = CountPorts(cell.objectType);
                straightRoom = straightRoom || cell.role == "straight";
                cornerRoom = cornerRoom || cell.role == "corner";
                observedDegrees[static_cast<std::size_t>(degree)] = true;
                Require(region.width >= 8 && region.width <= 12, "room is outside the 8-12 pixel range");
                const int innerSize = region.x1 - region.x0 + 1;
                Require(innerSize >= (lake ? 4 : 5) && innerSize <= (lake ? 7 : 8) &&
                    region.y1 - region.y0 + 1 == innerSize, "room interior has incorrect dimensions");
                const int centerX = (cell.x0 + cell.x1) / 2;
                const int centerY = (cell.y0 + cell.y1) / 2;
                const int roomX = std::clamp(centerX - region.width / 2, cell.x0 + 1, cell.x1 - region.width);
                const int roomY = std::clamp(centerY - region.width / 2, cell.y0 + 1, cell.y1 - region.width);
                const int margin = lake ? 2 : 1;
                const int anchor = region.rotation;
                Require(anchor >= 0 && anchor <= 4, "room anchor metadata is invalid");
                const int cornerMargin = lake ? 0 : margin;
                const bool centered = region.x0 == roomX + (region.width - innerSize) / 2 &&
                    region.y0 == roomY + (region.width - innerSize) / 2;
                if (anchor == 0) {
                    Require(centered, "centered room moved away from the center");
                } else {
                    Require(region.x0 == (anchor == 1 || anchor == 3 ? roomX + cornerMargin :
                        roomX + region.width - cornerMargin - innerSize) &&
                        region.y0 == (anchor <= 2 ? roomY + cornerMargin :
                            roomY + region.width - cornerMargin - innerSize),
                        "corner interior does not align with the chosen footprint corner");
                }
                (lake ? observedLakeAnchors : observedAnchors)[static_cast<std::size_t>(anchor)] = true;
                if (lake && anchor != 0) {
                    const int forbidden = (anchor <= 2 ? North : South) |
                        (anchor == 1 || anchor == 3 ? West : East);
                    Require((cell.objectType & forbidden) == 0,
                        "corner lake was placed toward a connected path");
                }
                for (int y = roomY; y < roomY + region.width; ++y) {
                    for (int x = roomX; x < roomX + region.width; ++x) {
                        const auto biome = plan.biomes[static_cast<std::size_t>(y * plan.width + x)];
                        Require(NaturalizedPixelMatches(rectangular, x, y, biome),
                            "room footprint, interior, or surrounding ring has the wrong biome");
                    }
                }
                if (!lake && degree == 1) { deadEndSize += region.width; ++deadEndRooms; }
                if (!lake && degree >= 3) { junctionSize += region.width; ++junctionRooms; }
            }
        }
    }
    Require(totalRooms >= 130 && totalRooms <= 158, "default room density is not near 20%");
    Require(totalLakes > totalRooms / 3 && totalLakes < totalRooms * 2 / 3,
        "default lake selection is not near 50%");
    Require(eligibleDeadEnds > 0 && eligibleOthers > 0 &&
        static_cast<double>(selectedDeadEnds) / eligibleDeadEnds >
            static_cast<double>(selectedOthers) / eligibleOthers,
        "dead-end cells do not have a higher clearing/lake selection rate");
    Require(straightRoom && cornerRoom, "rooms did not appear in both straight and corner cells");
    Require(std::all_of(observedDegrees.begin() + 1, observedDegrees.end(), [](bool found) { return found; }),
        "rooms did not appear at all connection degrees");
    Require(std::all_of(observedAnchors.begin(), observedAnchors.end(), [](bool found) { return found; }),
        "room interiors did not use all five anchors");
    Require(std::all_of(observedLakeAnchors.begin(), observedLakeAnchors.end(), [](bool found) { return found; }),
        "lakes did not use the center and every unconnected corner");
    Require(deadEndRooms > 0 && junctionRooms > 0 &&
        deadEndSize / deadEndRooms < junctionSize / junctionRooms,
        "dead-end rooms are not smaller than junction rooms");
}

std::pair<int, int> CheckCliffJoins(const MapPlan& plan, int columns) {
    std::map<int, const MapPlanRegion*> cliffs;
    for (const auto& region : plan.regions) {
        if (region.kind == "forest_variant" && (region.role == "cliff" || region.role == "cliff_only")) {
            Require(cliffs.emplace(region.parentId, &region).second, "duplicate cliff cell metadata");
        }
    }
    int pathJoins = 0;
    int diagonalJoins = 0;
    const auto biomeAt = [&](int x, int y) { return plan.biomes[static_cast<std::size_t>(y * plan.width + x)]; };
    for (const auto& [id, first] : cliffs) {
        for (const auto& [otherId, second] : cliffs) {
            if (otherId <= id) continue;
            const int dx = otherId % columns - id % columns;
            const int dy = otherId / columns - id / columns;
            if (std::abs(dx) > 1 || std::abs(dy) > 1) continue;
            bool possible = false;
            bool joined = false;
            for (int y = first->y0; y <= first->y1; ++y) {
                for (int x = first->x0; x <= first->x1; ++x) {
                    const int nx = std::clamp(x, second->x0, second->x1);
                    const int ny = std::clamp(y, second->y0, second->y1);
                    if (std::max(std::abs(nx - x), std::abs(ny - y)) > 1) continue;
                    if (!Walkable(biomeAt(x, y)) && !Walkable(biomeAt(nx, ny))) {
                        possible = true;
                        Require(biomeAt(x, y) == CLIFF && biomeAt(nx, ny) == CLIFF,
                            "an eight-neighbor cliff boundary still contains an inset seam: seed=" +
                            std::to_string(plan.seed) + " cells=" + std::to_string(id) + "," +
                            std::to_string(otherId) + " pixels=" + std::to_string(x) + "," +
                            std::to_string(y) + ":" + std::to_string(nx) + "," + std::to_string(ny));
                    }
                    if (biomeAt(x, y) == CLIFF && biomeAt(nx, ny) == CLIFF) joined = true;
                }
            }
            if (!possible) continue;
            Require(joined, "neighboring cliff footprints are separated by an artificial forest seam: seed=" +
                std::to_string(plan.seed) + " size=" + std::to_string(plan.width) + "x" +
                std::to_string(plan.depth) + " columns=" + std::to_string(columns) + " cells=" +
                std::to_string(id) + ":" + first->role + "," + std::to_string(otherId) + ":" + second->role);
            if (first->role != second->role) ++pathJoins;
            if (dx != 0 && dy != 0) ++diagonalJoins;
        }
    }
    return {pathJoins, diagonalJoins};
}

void TestCliffPropagationAndSideInsets() {
    int propagatedCells = 0;
    int repeatedPassCells = 0;
    int exposedSides = 0;
    for (std::uint32_t seed = 1; seed <= 120; ++seed) {
        MapGenParams params = BasicParams();
        params.seed = seed;
        params.extra["complexity"] = 5.0;
        params.extra["blankCellRate"] = 0.6;
        params.extra["riverRate"] = seed % 2 == 0 ? 1.0 : 0.0;
        params.extra["cliffRate"] = 0.5;
        const MapPlan plan = GenerateForest(params);
        const auto cells = Cells(plan);
        std::map<int, const MapPlanRegion*> cliffs;
        std::vector<bool> riverCells(cells.size(), false);
        for (const auto& region : plan.regions) {
            if (region.kind != "forest_variant") continue;
            if (region.role == "cliff" || region.role == "cliff_only") cliffs.emplace(region.parentId, &region);
            if (region.role == "river" || region.role == "river_only") {
                riverCells[static_cast<std::size_t>(region.parentId)] = true;
            }
        }
        std::vector<int> distance(cells.size(), -1);
        std::queue<int> pending;
        for (const auto& [id, region] : cliffs) {
            if (region->role != "cliff") continue;
            distance[static_cast<std::size_t>(id)] = 0;
            pending.push(id);
            const auto& cell = *cells[static_cast<std::size_t>(id)];
            const int inset = std::min(3, (cell.x1 - cell.x0) / 2);
            for (const int side : {-1, 1}) {
                const int column = id % 5 + side;
                const bool exposed = column < 0 || column >= 5 || cliffs.count(id + side) == 0;
                if (!exposed) continue;
                ++exposedSides;
                Require((side < 0 ? region->x0 == cell.x0 + inset : region->x1 == cell.x1 - inset),
                    "path cliff is missing its exposed side inset");
                for (int y = region->y0; y <= region->y1; ++y) {
                    for (int i = 0; i < inset; ++i) {
                        const int x = side < 0 ? cell.x0 + i : cell.x1 - i;
                        if (plan.biomes[static_cast<std::size_t>(y * plan.width + x)] != CLIFF) continue;
                        const bool cliffFurtherSouth = std::any_of(cliffs.begin(), cliffs.end(), [&](const auto& entry) {
                            const auto& source = *entry.second;
                            return x >= source.x0 && x <= source.x1 && source.y1 > y;
                        });
                        Require(cliffFurtherSouth, "path cliff overwrote its inset without a southern cliff source");
                    }
                }
            }
        }
        while (!pending.empty()) {
            const int id = pending.front();
            pending.pop();
            for (const int delta : {-1, 1, -5}) {
                const int next = id + delta;
                if (next < 0 || next >= static_cast<int>(cells.size()) ||
                    (delta != -5 && next / 5 != id / 5) ||
                    cells[static_cast<std::size_t>(next)]->objectType != 0 ||
                    riverCells[static_cast<std::size_t>(next)] ||
                    distance[static_cast<std::size_t>(next)] != -1) continue;
                Require(cliffs.count(next) != 0, "cliff propagation stopped before reaching a blank neighbor");
                Require(cliffs.at(next)->role == "cliff_only", "propagated cliff has the wrong cell type");
                distance[static_cast<std::size_t>(next)] = distance[static_cast<std::size_t>(id)] + 1;
                pending.push(next);
                ++propagatedCells;
                if (distance[static_cast<std::size_t>(next)] >= 2) ++repeatedPassCells;
            }
        }
        const auto quality = EvaluateMapQuality(plan);
        Require(quality.largestWalkableComponentRatio == 1.0 &&
            quality.reachableMarkers == quality.markerCount, "cliff propagation blocked a path");
        CheckCliffJoins(plan, 5);
    }
    Require(propagatedCells > 0 && repeatedPassCells > 0 && exposedSides > 0,
        "cliff regressions did not exercise propagation chains and side insets");
}

void TestCliffNorthwardImagePass() {
    int protectedPixels = 0;
    for (std::uint32_t seed = 1; seed <= 120; ++seed) {
        MapGenParams params = BasicParams();
        params.seed = seed;
        params.extra["blankCellRate"] = 0.45;
        params.extra["clearingRate"] = 0.2;
        params.extra["riverRate"] = 0.5;
        const MapPlan withoutCliffs = GenerateForest(params);
        params.extra["cliffRate"] = 1.0;
        const MapPlan plan = GenerateForest(params);
        const auto unroundedBase = RestoreRoomShapes(withoutCliffs);
        const auto unroundedCliffs = RestoreRoomShapes(plan);
        for (int y = 0; y < plan.depth; ++y) {
            for (int x = 0; x < plan.width; ++x) {
                const auto index = static_cast<std::size_t>(y * plan.width + x);
                if (WithoutDeepForest(withoutCliffs.biomes[index]) != FOREST) {
                    const bool cliffSpace = std::any_of(plan.regions.begin(), plan.regions.end(),
                        [&](const MapPlanRegion& region) {
                            if (region.kind != "forest_variant" || region.role != "cliff" ||
                                region.objectVariant != 1) return false;
                            const auto& cell = plan.regions[static_cast<std::size_t>(region.parentId)];
                            const int centerX = (cell.x0 + cell.x1) / 2;
                            const int centerY = (cell.y0 + cell.y1) / 2;
                            return x >= std::max(cell.x0 + 1, centerX - 2) &&
                                x <= std::min(cell.x1 - 1, centerX + 2) &&
                                y >= std::max(cell.y0 + 1, centerY - 2) &&
                                y <= std::min(cell.y1 - 1, centerY + 2);
                        });
                    const auto baseline = unroundedBase.biomes[index];
                    const bool roundedRoom = (baseline == PLAIN || baseline == OPEN || baseline == LAKE) &&
                        unroundedCliffs.biomes[index] == baseline &&
                        NaturalizedPixelMatches(unroundedCliffs, x, y, plan.biomes[index]);
                    Require(plan.biomes[index] == withoutCliffs.biomes[index] || roundedRoom ||
                        (Road(plan.biomes[index]) && Road(withoutCliffs.biomes[index])) ||
                        (cliffSpace && withoutCliffs.biomes[index] == ROAD && plan.biomes[index] == PLAIN),
                        "northward cliff fill overwrote a path, clearing, bridge, or water");
                    ++protectedPixels;
                }
                if (plan.biomes[index] != CLIFF) continue;
                if (y > 0) {
                    Require(WithoutDeepForest(plan.biomes[index - plan.width]) != FOREST,
                        "cliff protrusion still has an unfilled forest pixel to its north");
                }
            }
        }
    }
    MapGenParams regression = BasicParams();
    regression.width = 44;
    regression.depth = 44;
    regression.seed = 2;
    regression.extra["complexity"] = 5.0;
    regression.extra["blankCellRate"] = 0.6;
    regression.extra["cliffRate"] = 1.0;
    const MapPlan filled = GenerateForest(regression);
    for (const int y : {4, 11, 12}) {
        Require(filled.biomes[static_cast<std::size_t>((y + 2) * filled.width + 10)] == CLIFF,
            "northward image pass left a known forest recess above a cliff bulge");
    }
    Require(protectedPixels > 0, "northward cliff regression did not exercise terrain protection");
}

void TestRiversAndCliffs() {
    int rivers = 0;
    int riverOnly = 0;
    int cliffs = 0;
    int cliffOnly = 0;
    int pathCliffJoins = 0;
    int diagonalCliffJoins = 0;
    for (std::uint32_t seed = 1; seed <= 120; ++seed) {
        MapGenParams params = BasicParams();
        params.seed = seed;
        params.extra["blankCellRate"] = 0.45;
        params.extra["deadEndBranchRate"] = 0.0;
        const MapPlan base = GenerateForest(params);
        for (const bool river : {true, false}) {
            params.extra["riverRate"] = river ? 1.0 : 0.0;
            params.extra["cliffRate"] = river ? 0.0 : 1.0;
            const MapPlan plan = GenerateForest(params);
            CheckVariantConnectivity(plan, base, 3, 3);
            const auto joins = CheckCliffJoins(plan, 3);
            pathCliffJoins += joins.first;
            diagonalCliffJoins += joins.second;
            const auto cells = Cells(plan);
            rivers += VariantCount(plan, "river");
            riverOnly += VariantCount(plan, "river_only");
            cliffs += VariantCount(plan, "cliff");
            cliffOnly += VariantCount(plan, "cliff_only");
            for (const auto& region : plan.regions) {
                if (region.kind != "forest_variant") continue;
                const auto& cell = *cells[static_cast<std::size_t>(region.parentId)];
                if (region.role == "river") {
                    const bool horizontal = cell.objectType == (North | South);
                    Require(horizontal || cell.objectType == (East | West), "river is not in a straight cell");
                    Require((horizontal ? region.y1 - region.y0 : region.x1 - region.x0) == 1,
                        "river width is not two pixels");
                    int bridges = 0;
                    for (int y = region.y0; y <= region.y1; ++y) {
                        for (int x = region.x0; x <= region.x1; ++x) {
                            const auto biome = plan.biomes[static_cast<std::size_t>(y * plan.width + x)];
                            Require(biome == RIVER || biome == BRIDGE, "river strip contains a non-water pixel");
                            if (biome == BRIDGE) ++bridges;
                        }
                    }
                    Require(bridges == 6, "river crossing is not a two-by-three bridge");
                    for (const int step : {-1, 1}) {
                        int column = cell.parentIndex + (horizontal ? step : 0);
                        int row = cell.parentId + (horizontal ? 0 : step);
                        while (column >= 0 && column < 3 && row >= 0 && row < 3) {
                            const auto& target = *cells[static_cast<std::size_t>(row * 3 + column)];
                            if (target.objectType != 0) break;
                            const int x0 = horizontal ? target.x0 : region.x0;
                            const int x1 = horizontal ? target.x1 : region.x1;
                            const int y0 = horizontal ? region.y0 : target.y0;
                            const int y1 = horizontal ? region.y1 : target.y1;
                            for (int y = y0; y <= y1; ++y) {
                                for (int x = x0; x <= x1; ++x) {
                                    Require(plan.biomes[static_cast<std::size_t>(y * plan.width + x)] == RIVER,
                                        "river stopped before the end of a consecutive blank-cell run");
                                }
                            }
                            column += horizontal ? step : 0;
                            row += horizontal ? 0 : step;
                        }
                    }
                } else if (region.role == "river_only") {
                    Require(cell.objectType == 0, "river-only terrain is not in a blank cell");
                } else if (region.role == "cliff") {
                    Require(region.y0 == cell.y0, "cliff is not on the north side");
                    for (int north = region.parentId - 3; north >= 0; north -= 3) {
                        Require(cells[static_cast<std::size_t>(north)]->objectType == 0,
                            "cliff has an active cell between itself and the north edge");
                    }
                } else if (region.role == "cliff_only") {
                    Require(cell.objectType == 0, "cliff-only terrain is not in a blank cell");
                    int cliffPixels = 0;
                    for (int y = cell.y0; y <= cell.y1; ++y) {
                        for (int x = cell.x0; x <= cell.x1; ++x) {
                            const auto biome = plan.biomes[static_cast<std::size_t>(y * plan.width + x)];
                            Require(biome == CLIFF || WithoutDeepForest(biome) == FOREST,
                                "cliff-only cell contains walkable terrain");
                            if (biome == CLIFF) ++cliffPixels;
                        }
                        Require(cliffPixels > 0, "cliff-only cell lost its entire cliff footprint");
                    }
                }
            }
        }
    }
    Require(rivers > 0 && riverOnly > 0 && cliffs > 0 && cliffOnly > 0,
        "a river, cliff, or blank-cell extension was never generated");
    Require(pathCliffJoins > 0 && diagonalCliffJoins > 0,
        "cliff tests did not cover mixed path/blank or diagonal neighbors");
}

void TestMixedVariantsAndParameters() {
    for (const auto dimensions : {std::pair<int, int>{32, 32}, {40, 40}, {53, 40}, {40, 53}, {160, 160}}) {
        for (int complexity = 1; complexity <= 5; ++complexity) {
            for (std::uint32_t seed = 1; seed <= 15; ++seed) {
                MapGenParams params = BasicParams();
                params.width = dimensions.first;
                params.depth = dimensions.second;
                params.seed = seed;
                params.extra["complexity"] = complexity;
                params.extra["blankCellRate"] = 0.4;
                const MapPlan base = GenerateForest(params);
                params.extra.erase("clearingRate");
                params.extra.erase("riverRate");
                params.extra.erase("cliffRate");
                const MapPlan plan = GenerateForest(params);
                const int size = std::min(params.width - 4, params.depth - 4) / complexity;
                const int columns = std::clamp((params.width - 4 + size / 2) / size, 1, (params.width - 4) / 5);
                const int rows = std::clamp((params.depth - 4 + size / 2) / size, 1, (params.depth - 4) / 5);
                CheckVariantConnectivity(plan, base, columns, rows);
                CheckCliffJoins(plan, columns);
                const MapPlan repeated = GenerateForest(params);
                Require(plan.biomes == repeated.biomes && plan.regions.size() == repeated.regions.size(),
                    "variant generation is not deterministic");
                ForestMapImageGenerator generator(params);
                for (const auto biome : plan.biomes) {
                    const auto color = generator.BiomeToColor(biome);
                    Require(!(color.r == 255 && color.g == 0 && color.b == 255),
                        "variant biome has no palette color");
                }
            }
        }
    }
    for (const char* name : {"clearingRate", "lakeRate", "riverRate", "cliffRate", "meanderRate"}) {
        MapGenParams params = BasicParams();
        MapPlan plan;
        std::string error;
        for (const double value : {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
            params.extra[name] = value;
            Require(!ForestMapImageGenerator(params).Generate(plan, error) && error.find(name) != std::string::npos,
                "non-finite variant rate was not reported");
        }
        params.extra[name] = -1.0;
        const auto negative = GenerateForest(params);
        params.extra[name] = 0.0;
        Require(negative.biomes == GenerateForest(params).biomes, "negative variant rate was not clamped");
        params.extra[name] = 2.0;
        const auto excessive = GenerateForest(params);
        params.extra[name] = 1.0;
        Require(excessive.biomes == GenerateForest(params).biomes, "excessive variant rate was not clamped");
    }
}

void CheckRoadCopyMargins(const MapPlan& base, const MapPlanRegion& source) {
    for (const bool horizontal : {false, true}) {
        const int length = horizontal ? base.width : base.depth;
        const int firstLine = horizontal ? source.y0 : source.x0;
        const int lastLine = horizontal ? source.y1 : source.x1;
        const int lower = horizontal ? source.x0 : source.y0;
        const int upper = horizontal ? source.x1 : source.y1;
        for (int line = firstLine; line <= lastLine; ++line) {
            int position = 0;
            while (position < length) {
                const auto road = [&](int p) {
                    return Road(base.biomes[static_cast<std::size_t>(
                        horizontal ? line * base.width + p : p * base.width + line)]);
                };
                if (!road(position)) {
                    ++position;
                    continue;
                }
                const int start = position;
                while (position < length && road(position)) ++position;
                if (position - start <= 3 && start <= upper && position > lower) {
                    Require(lower < start && upper >= position,
                        "meander source cuts a road cross-section or omits its one-pixel margin");
                }
            }
        }
    }
}

void TestMeanderingCopies() {
    int totalCopies = 0;
    int roomCopies = 0;
    int borderCrossings = 0;
    for (const auto& dimensions : {std::pair<int, int>{32, 32}, {40, 40}, {53, 40}}) {
        for (std::uint32_t seed = 1; seed <= 30; ++seed) {
            for (const double rate : {0.0, 0.5, 1.0}) {
                MapGenParams params = BasicParams();
                params.width = dimensions.first;
                params.depth = dimensions.second;
                params.seed = seed;
                params.extra["complexity"] = 1 + (seed % 3) * 2;
                params.extra["clearingRate"] = seed % 2 == 0 ? 1.0 : 0.2;
                params.extra["riverRate"] = 0.0;
                params.extra["cliffRate"] = 0.0;
                const MapPlan base = GenerateForest(params);
                const auto unfiltered = RestoreRoomShapes(base);
                params.extra["meanderRate"] = rate;
                const MapPlan plan = GenerateForest(params);
                auto expected = unfiltered.biomes;
                std::transform(expected.begin(), expected.end(), expected.begin(), WithoutDeepForest);
                std::vector<std::array<int, 4>> occupied;
                std::vector<int> selectedCells;
                bool selectedRoom = false;
                int count = 0;
                for (const auto& region : plan.regions) {
                    if (region.kind != "forest_meander") continue;
                    CheckRoadCopyMargins(base, region);
                    ++count;
                    ++totalCopies;
                    const int dx = region.doorX;
                    const int dy = region.doorY;
                    Require(std::abs(dx) <= 1 && std::abs(dy) <= 1 && (dx != 0 || dy != 0),
                        "meander offset is not a nonzero one-pixel shift");
                    const auto& cell = base.regions[static_cast<std::size_t>(region.parentId)];
                    Require(std::find(selectedCells.begin(), selectedCells.end(), region.parentId) ==
                        selectedCells.end(), "meander selected the same cell twice");
                    selectedCells.push_back(region.parentId);
                    const std::array<int, 4> footprint = {std::min(region.x0, region.x0 + dx),
                        std::min(region.y0, region.y0 + dy), std::max(region.x1, region.x1 + dx),
                        std::max(region.y1, region.y1 + dy)};
                    for (const auto& previous : occupied) {
                        Require(footprint[2] < previous[0] || footprint[0] > previous[2] ||
                            footprint[3] < previous[1] || footprint[1] > previous[3],
                            "meander copy regions interfere");
                    }
                    occupied.push_back(footprint);
                    if (footprint[0] < cell.x0 || footprint[1] < cell.y0 ||
                        footprint[2] > cell.x1 || footprint[3] > cell.y1) ++borderCrossings;
                    bool room = false;
                    for (const auto& variant : base.regions) {
                        if (variant.kind != "forest_variant" || variant.parentId != region.parentId ||
                            (variant.role != "clearing" && variant.role != "lake")) continue;
                        ++roomCopies;
                        room = true;
                        const int x0 = std::clamp((cell.x0 + cell.x1) / 2 - variant.width / 2,
                            cell.x0 + 1, cell.x1 - variant.width);
                        const int y0 = std::clamp((cell.y0 + cell.y1) / 2 - variant.width / 2,
                            cell.y0 + 1, cell.y1 - variant.width);
                        Require(region.x0 <= x0 - 1 && region.y0 <= y0 - 1 &&
                            region.x1 >= x0 + variant.width && region.y1 >= y0 + variant.width,
                            "meander copies only part of a clearing or lake");
                    }
                    Require(!selectedRoom || room, "meander selected ordinary cells after room cells");
                    selectedRoom = selectedRoom || room;
                    for (int y = region.y0; y <= region.y1; ++y) {
                        for (int x = region.x0; x <= region.x1; ++x) {
                            Require(x + dx >= 0 && x + dx < plan.width &&
                                y + dy >= 0 && y + dy < plan.depth, "meander copy is outside the image");
                            expected[static_cast<std::size_t>((y + dy) * plan.width + x + dx)] =
                                unfiltered.biomes[static_cast<std::size_t>(y * plan.width + x)];
                        }
                    }
                }
                auto actual = plan.biomes;
                std::transform(actual.begin(), actual.end(), actual.begin(), WithoutDeepForest);
                for (int y = 0; y < plan.depth; ++y) {
                    for (int x = 0; x < plan.width; ++x) {
                        expected[static_cast<std::size_t>(y * plan.width + x)] =
                            expected[static_cast<std::size_t>(
                                std::clamp(y, 2, plan.depth - 3) * plan.width + std::clamp(x, 2, plan.width - 3))];
                    }
                }
                auto copied = base;
                copied.biomes = std::move(expected);
                for (int y = 0; y < plan.depth; ++y) {
                    for (int x = 0; x < plan.width; ++x) {
                        const auto index = static_cast<std::size_t>(y * plan.width + x);
                        Require((Road(copied.biomes[index]) && Road(actual[index])) ||
                            NaturalizedPixelMatches(copied, x, y, actual[index]),
                            "meander result does not match opaque copies followed by naturalization");
                    }
                }
                Require(count <= static_cast<int>(std::ceil(rate * Cells(base).size())),
                    "meander count exceeds the requested cell ratio");
                if (rate == 0.0) Require(plan.biomes == base.biomes && count == 0, "zero rate changed the map");
                for (int y = 0; y < plan.depth; ++y) {
                    for (int x = 0; x < plan.width; ++x) {
                        if (x != 0 && y != 0 && x != plan.width - 1 && y != plan.depth - 1) continue;
                        const auto index = static_cast<std::size_t>(y * plan.width + x);
                        Require(WithoutDeepForest(plan.biomes[index]) == WithoutDeepForest(base.biomes[index]),
                            "meander changed a map-edge entrance");
                    }
                }
                const auto quality = EvaluateMapQuality(plan);
                const auto baseQuality = EvaluateMapQuality(base);
                Require(quality.largestWalkableComponentRatio >= baseQuality.largestWalkableComponentRatio &&
                    quality.reachableMarkers == quality.markerCount, "meander disconnected a path or entrance: seed=" +
                    std::to_string(seed) + " size=" + std::to_string(params.width) + " rate=" +
                    std::to_string(rate) + " component=" + std::to_string(quality.largestWalkableComponentRatio) +
                    " markers=" + std::to_string(quality.reachableMarkers) + "/" + std::to_string(quality.markerCount));
                if (count > 0) Require(quality.largestWalkableComponentRatio == 1.0,
                    "meander accepted a copy with disconnected walkable pixels");
                Require(plan.biomes == GenerateForest(params).biomes, "meander is not seed deterministic");
                if (rate == 0.5) {
                    params.extra.erase("meanderRate");
                    Require(plan.biomes == GenerateForest(params).biomes, "default meander rate is not 0.5");
                }
            }
        }
    }
    Require(totalCopies > 0 && roomCopies > 0 && borderCrossings > 0,
        "meander tests did not exercise copying, whole rooms, and crossing cell edges");
}

void TestNaturalizationFilter() {
    std::mt19937 random(9876);
    for (const auto neighbor : {FOREST, CLIFF, DEEPFOREST, PLAIN, OPEN, ROAD, INNERROAD, BRIDGE, LAKE}) {
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= 3 || ny >= 3) continue;
                        MapPlan original;
                        original.Reset(3, 3, 0, "forest");
                        original.biomes.assign(9, ROAD);
                        const auto index = static_cast<std::size_t>(y * 3 + x);
                        original.biomes[index] = INNERROAD;
                        original.biomes[static_cast<std::size_t>(ny * 3 + nx)] =
                            static_cast<std::uint8_t>(neighbor);
                        auto result = original;
                        ForestMapFilters::Naturalize(result, random);
                        const bool replace = std::abs(dx) + std::abs(dy) == 1 &&
                            (neighbor == FOREST || neighbor == CLIFF);
                        Require(result.biomes[index] == (replace ? ROAD : INNERROAD),
                            "INNERROAD rule must use only in-bounds cardinal FOREST/CLIFF neighbors");
                        Require(std::equal(original.biomes.begin(), original.biomes.end(), result.biomes.begin(),
                            [&](std::uint8_t before, std::uint8_t after) {
                                return before == after || (before == INNERROAD && after == ROAD);
                            }), "INNERROAD filter changed unrelated terrain");
                    }
                }
            }
        }
    }
    for (const int sx : {-1, 1}) {
        for (const int sy : {-1, 1}) {
            MapPlan corner;
            corner.Reset(3, 3, 0, "forest");
            corner.biomes.assign(9, INNERROAD);
            for (const auto& offset : {std::pair<int, int>{0, 0}, {sx, 0}, {0, sy}, {sx, sy}}) {
                corner.biomes[static_cast<std::size_t>((1 + offset.second) * 3 + 1 + offset.first)] = PLAIN;
            }
            for (int sample = 0; sample < 100; ++sample) {
                auto result = corner;
                ForestMapFilters::Naturalize(result, random);
                Require(result.biomes == corner.biomes,
                    "rounding the cliff-junction PLAIN corner thickened INNERROAD");
            }
        }
    }
    for (const auto center : {PLAIN, OPEN, LAKE, CLIFF}) {
        for (const int sx : {-1, 1}) {
            for (const int sy : {-1, 1}) {
                for (int pattern = 0; pattern < 243; ++pattern) {
                    MapPlan original;
                    original.Reset(3, 3, 0, "forest");
                    original.biomes.assign(9, static_cast<std::uint8_t>(center));
                    const std::array<MapGenBiomeID, 3> alternatives = center == PLAIN
                        ? std::array<MapGenBiomeID, 3>{FOREST, OPEN, ROAD}
                        : center == OPEN ? std::array<MapGenBiomeID, 3>{FOREST, PLAIN, ROAD}
                        : std::array<MapGenBiomeID, 3>{OPEN, PLAIN, ROAD};
                    int digits = pattern;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if ((dx == 0 || dx == sx) && (dy == 0 || dy == sy)) continue;
                            original.biomes[static_cast<std::size_t>((1 + dy) * 3 + 1 + dx)] =
                                static_cast<std::uint8_t>(alternatives[static_cast<std::size_t>(digits % 3)]);
                            digits /= 3;
                        }
                    }
                    int changes = 0;
                    for (int sample = 0; sample < 30; ++sample) {
                        auto result = original;
                        ForestMapFilters::Naturalize(result, random);
                        Require(NaturalizedPixelMatches(original, 1, 1, result.biomes[4]),
                            "corner rounding did not require a uniform surrounding biome");
                        changes += result.biomes[4] != center ? 1 : 0;
                    }
                    const bool uniform = pattern == 0 || pattern == 121 || pattern == 242;
                    Require(center == CLIFF || !uniform ? changes == 0 : changes > 0,
                        "mixed-biome corner was rounded or a uniform corner was skipped");
                }
            }
        }
    }
    for (const auto center : {PLAIN, OPEN, LAKE, FOREST, CLIFF, ROAD, INNERROAD, BRIDGE, RIVER}) {
        for (const auto surrounding : {PLAIN, OPEN, LAKE, FOREST, CLIFF, ROAD, INNERROAD, BRIDGE, RIVER}) {
            for (int mask = 0; mask < 256; ++mask) {
                MapPlan original;
                original.Reset(3, 3, 0, "forest");
                original.biomes.assign(9, static_cast<std::uint8_t>(center));
                int bit = 0;
                for (int i = 0; i < 9; ++i) {
                    if (i == 4) continue;
                    if ((mask & (1 << bit++)) != 0) original.biomes[static_cast<std::size_t>(i)] =
                        static_cast<std::uint8_t>(surrounding);
                }
                auto result = original;
                ForestMapFilters::Naturalize(result, random);
                Require(NaturalizedPixelMatches(original, 1, 1, result.biomes[4]),
                    "naturalizing filter violated an eight-neighbor rule");
                for (int i = 0; i < 9; ++i) {
                    if (i != 4) Require(NaturalizedPixelMatches(original, i % 3, i / 3,
                        result.biomes[static_cast<std::size_t>(i)]), "naturalization changed an image-edge pixel incorrectly");
                }
            }
        }
    }
    for (const auto center : {PLAIN, OPEN, LAKE, CLIFF}) {
        for (const int sx : {-1, 1}) {
            for (const int sy : {-1, 1}) {
                MapPlan original;
                original.Reset(3, 3, 0, "forest");
                const auto outside = center == LAKE ? OPEN : FOREST;
                original.biomes.assign(9, static_cast<std::uint8_t>(outside));
                for (const auto& offset : {std::pair<int, int>{0, 0}, {sx, 0}, {0, sy}, {sx, sy}}) {
                    original.biomes[static_cast<std::size_t>((1 + offset.second) * 3 + 1 + offset.first)] =
                        static_cast<std::uint8_t>(center);
                }
                int changed = 0;
                for (int sample = 0; sample < 1000; ++sample) {
                    auto result = original;
                    ForestMapFilters::Naturalize(result, random);
                    changed += result.biomes[4] == outside ? 1 : 0;
                }
                Require(center == PLAIN || center == OPEN ? changed >= 640 && changed <= 760 : changed == 1000,
                    "corner-rounding probability is not 0.7 or 1.0");
                if (center == CLIFF) continue;
                for (int position = 0; position < 9; ++position) {
                    if (position == 4) continue;
                    auto protectedCorner = original;
                    protectedCorner.biomes[static_cast<std::size_t>(position)] = CLIFF;
                    ForestMapFilters::Naturalize(protectedCorner, random);
                    Require(protectedCorner.biomes[4] == center, "cliff neighbor did not protect a corner");
                    if (center == LAKE) {
                        protectedCorner = original;
                        protectedCorner.biomes[static_cast<std::size_t>(position)] = FOREST;
                        ForestMapFilters::Naturalize(protectedCorner, random);
                        Require(protectedCorner.biomes[4] == LAKE, "forest neighbor did not protect a lake corner");
                    }
                }
            }
        }
    }
    for (int sample = 0; sample < 200; ++sample) {
        MapPlan original;
        original.Reset(8, 8, 0, "forest");
        for (int i = 0; i < 64; ++i) {
            original.biomes.push_back(static_cast<std::uint8_t>(
                std::uniform_int_distribution<int>(0, 1)(random) == 0 ? FOREST : CLIFF));
        }
        auto result = original;
        ForestMapFilters::Naturalize(result, random);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                Require(NaturalizedPixelMatches(original, x, y,
                    result.biomes[static_cast<std::size_t>(y * 8 + x)]),
                    "filter used already-modified neighbors instead of a snapshot");
            }
        }
    }
}

void TestCliffInnerRoadWidths() {
    int corners = 0;
    int tees = 0;
    for (std::uint32_t seed = 1; seed <= 120; ++seed) {
        auto params = BasicParams();
        params.seed = seed;
        const auto base = GenerateForest(params);
        params.extra["cliffRate"] = 1.0;
        const auto plan = GenerateForest(params);
        for (const auto& region : plan.regions) {
            if (region.kind != "forest_variant" || region.role != "cliff" || region.objectVariant != 1) continue;
            const auto& cell = plan.regions[static_cast<std::size_t>(region.parentId)];
            if (cell.role == "corner" || cell.role == "corner_with_dead_end") ++corners;
            if (cell.role == "tee" || cell.role == "tee_with_dead_end") ++tees;
        }
        for (std::size_t index = 0; index < plan.biomes.size(); ++index) {
            Require(plan.biomes[index] != INNERROAD || Road(base.biomes[index]),
                "cliff decoration added an INNERROAD outside the original road footprint");
        }
    }
    Require(corners > 0 && tees > 0, "cliff centerline test missed widened L and T junctions");
}

void TestInsetAndEdgeExtension() {
    int entrancePixels = 0;
    for (const auto& dimensions : {std::pair<int, int>{32, 32}, {40, 40}, {53, 40}, {40, 53}}) {
        for (std::uint32_t seed = 1; seed <= 30; ++seed) {
            MapGenParams params = BasicParams();
            params.width = dimensions.first;
            params.depth = dimensions.second;
            params.seed = seed;
            params.extra["complexity"] = 1 + seed % 5;
            params.extra["meanderRate"] = (seed % 3) * 0.5;
            params.extra["clearingRate"] = 0.2;
            params.extra["riverRate"] = 0.5;
            params.extra["cliffRate"] = 0.5;
            const auto plan = GenerateForest(params);
            Require(plan.width == params.width && plan.depth == params.depth &&
                plan.biomes.size() == static_cast<std::size_t>(params.width * params.depth),
                "edge extension changed the requested output dimensions");
            const auto cells = Cells(plan);
            int columns = 0;
            int rows = 0;
            for (const auto* cell : cells) {
                columns = std::max(columns, cell->parentIndex + 1);
                rows = std::max(rows, cell->parentId + 1);
            }
            for (const auto* cell : cells) {
                Require(cell->x0 == 2 + cell->parentIndex * (plan.width - 4) / columns &&
                    cell->x1 == 1 + (cell->parentIndex + 1) * (plan.width - 4) / columns &&
                    cell->y0 == 2 + cell->parentId * (plan.depth - 4) / rows &&
                    cell->y1 == 1 + (cell->parentId + 1) * (plan.depth - 4) / rows,
                    "cell bounds do not partition the two-pixel-inset interior");
            }
            for (const auto& region : plan.regions) {
                Require(region.x0 >= 2 && region.y0 >= 2 &&
                    region.x1 < plan.width - 2 && region.y1 < plan.depth - 2,
                    "region metadata was not translated into the inset interior");
            }
            for (int y = 0; y < plan.depth; ++y) {
                for (int x = 0; x < plan.width; ++x) {
                    if (x >= 2 && y >= 2 && x < plan.width - 2 && y < plan.depth - 2) continue;
                    const auto source = static_cast<std::size_t>(
                        std::clamp(y, 2, plan.depth - 3) * plan.width + std::clamp(x, 2, plan.width - 3));
                    const auto edge = WithoutDeepForest(plan.biomes[static_cast<std::size_t>(y * plan.width + x)]);
                    const auto inner = WithoutDeepForest(plan.biomes[source]);
                    Require(edge == inner || ((edge == CLIFF || edge == FOREST) &&
                        (inner == CLIFF || inner == FOREST)),
                        "border or corner differs from the nearest inner pixel outside cliff naturalization");
                }
            }
            for (const auto& marker : plan.markers) {
                Require(marker.x == 0 || marker.y == 0 ||
                    marker.x == plan.width - 1 || marker.y == plan.depth - 1,
                    "entrance marker was not moved to the output edge");
                const int innerX = std::clamp(marker.x, 2, plan.width - 3);
                const int innerY = std::clamp(marker.y, 2, plan.depth - 3);
                for (int y = std::min(marker.y, innerY); y <= std::max(marker.y, innerY); ++y) {
                    for (int x = std::min(marker.x, innerX); x <= std::max(marker.x, innerX); ++x) {
                        Require(plan.biomes[static_cast<std::size_t>(y * plan.width + x)] == INNERROAD,
                            "extended entrance does not connect the inner road to the output edge");
                        ++entrancePixels;
                    }
                }
            }
            const auto quality = EvaluateMapQuality(plan);
            Require(quality.markerCount == quality.reachableMarkers, "edge extension disconnected an entrance");
        }
    }
    Require(entrancePixels > 0, "edge-extension test did not exercise entrances");
}

void TestDeepForestDistances() {
    int belowThreshold = 0;
    int atThreshold = 0;
    int deepEdges = 0;
    int diagonalBoundaries = 0;
    for (const auto& dimensions : {std::pair<int, int>{32, 32}, {40, 40}, {53, 40}}) {
        for (std::uint32_t seed = 1; seed <= 30; ++seed) {
            auto params = BasicParams();
            params.width = dimensions.first;
            params.depth = dimensions.second;
            params.seed = seed;
            params.extra["complexity"] = 1 + seed % 5;
            params.extra["meanderRate"] = seed % 2 == 0 ? 1.0 : 0.0;
            params.extra["clearingRate"] = 0.4;
            params.extra["riverRate"] = 0.5;
            params.extra["cliffRate"] = 0.5;
            const int threshold = seed % 3 == 0 ? 5 : seed % 3 == 1 ? 4 : 7;
            params.extra["deepForestDistance"] = threshold;
            const auto plan = GenerateForest(params);
            if (threshold == 5) {
                params.extra.erase("deepForestDistance");
                Require(GenerateForest(params).biomes == plan.biomes,
                    "default deep forest distance is not five pixels");
            }
            for (int y = 0; y < plan.depth; ++y) {
                for (int x = 0; x < plan.width; ++x) {
                    const auto biome = plan.biomes[static_cast<std::size_t>(y * plan.width + x)];
                    if (WithoutDeepForest(biome) != FOREST) continue;
                    int distance = threshold + 1;
                    int diagonalDistance = threshold + 1;
                    for (int dy = -threshold; dy <= threshold; ++dy) {
                        for (int dx = -threshold; dx <= threshold; ++dx) {
                            const int xx = x + dx;
                            const int yy = y + dy;
                            if (xx < 0 || yy < 0 || xx >= plan.width || yy >= plan.depth) continue;
                            if (WithoutDeepForest(plan.biomes[static_cast<std::size_t>(yy * plan.width + xx)]) ==
                                FOREST) continue;
                            distance = std::min(distance, std::abs(dx) + std::abs(dy));
                            diagonalDistance = std::min(diagonalDistance, std::max(std::abs(dx), std::abs(dy)));
                        }
                    }
                    Require((biome == DEEPFOREST) == (distance >= threshold),
                        "deep forest does not match the final image's four-neighbor distance threshold");
                    if (distance == threshold - 1) ++belowThreshold;
                    if (distance == threshold) ++atThreshold;
                    if (biome == DEEPFOREST && diagonalDistance < threshold) ++diagonalBoundaries;
                    if (biome == DEEPFOREST &&
                        (x == 0 || y == 0 || x + 1 == plan.width || y + 1 == plan.depth)) ++deepEdges;
                }
            }
        }
    }
    Require(belowThreshold > 0 && atThreshold > 0 && deepEdges > 0 && diagonalBoundaries > 0,
        "deep forest tests missed threshold, outside-image, or Manhattan-diagonal cases");
    ForestMapImageGenerator generator(BasicParams());
    const auto color = generator.BiomeToColor(DEEPFOREST);
    Require(color.r == 17 && color.g == 54 && color.b == 29, "deep forest palette color is missing");
    auto params = BasicParams();
    MapPlan plan;
    std::string error;
    for (const double value : {std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
        params.extra["deepForestDistance"] = value;
        Require(!ForestMapImageGenerator(params).Generate(plan, error) &&
            error.find("deepForestDistance") != std::string::npos, "non-finite deep forest distance was not reported");
    }
    for (const auto& values : {std::pair<double, double>{-1.0, 1.0}, {1e300, 80.0}, {4.6, 5.0}}) {
        params.extra["deepForestDistance"] = values.first;
        const auto adjusted = GenerateForest(params);
        params.extra["deepForestDistance"] = values.second;
        Require(adjusted.biomes == GenerateForest(params).biomes,
            "deep forest distance was not clamped or rounded correctly");
    }
}

void TestMeanderingRoadWidths() {
    int copies = 0;
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        auto params = BasicParams();
        params.seed = seed;
        params.extra["complexity"] = 1 + seed % 5;
        const auto base = GenerateForest(params);
        params.extra["meanderRate"] = 1.0;
        const auto plan = GenerateForest(params);
        for (const auto& region : plan.regions) {
            if (region.kind != "forest_meander") continue;
            CheckRoadCopyMargins(base, region);
            ++copies;
        }
        for (int y = 0; y + 3 < plan.depth; ++y) {
            for (int x = 0; x + 3 < plan.width; ++x) {
                bool wide = true;
                bool alreadyWide = true;
                for (int yy = y; yy < y + 4; ++yy) {
                    for (int xx = x; xx < x + 4; ++xx) {
                        const auto index = static_cast<std::size_t>(yy * plan.width + xx);
                        wide = wide && Road(plan.biomes[index]);
                        alreadyWide = alreadyWide && Road(base.biomes[index]);
                    }
                }
                Require(!wide || alreadyWide, "meander widened a three-pixel road to a four-pixel block: seed=" +
                    std::to_string(seed) + " x=" + std::to_string(x) + " y=" + std::to_string(y));
            }
        }
    }
    Require(copies >= 200, "road-margin restrictions prevented meaningful meandering");
}

}  // namespace

int main() {
    try {
        TestReproducibleConnectedCellMaze();
        TestComplexityAndAspectRatio();
        TestEntranceLimitsAndInvalidDimensions();
        TestCellRatesAndRandomDefaults();
        TestRoomsAndLakes();
        TestRiversAndCliffs();
        TestCliffPropagationAndSideInsets();
        TestCliffNorthwardImagePass();
        TestMixedVariantsAndParameters();
        TestMeanderingCopies();
        TestMeanderingRoadWidths();
        TestDeepForestDistances();
        TestInsetAndEdgeExtension();
        TestNaturalizationFilter();
        TestCliffInnerRoadWidths();
        std::cout << "ForestMapImageGenerator tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ForestMapImageGenerator test failed: " << error.what() << '\n';
        return 1;
    }
}
