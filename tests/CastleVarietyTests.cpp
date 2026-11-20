#include "CastleMapImageGenerator.h"
#include "CastleProgram.h"
#include "TestScale.h"
#include "MapQuality.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

MapPlan Generate(int size, int mode, unsigned seed) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"lotFacilities", 0}};
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    Require(generator.Generate(plan, error), std::to_string(size) + " mode=" + std::to_string(mode) +
        " seed=" + std::to_string(seed) + ": " + error);
    return plan;
}

// (A) The room catalog: every castle type owns a list of rooms with a presence
// percentage. No room is certain (the highest is 95), and the most likely ones are nearly always picked.
void TestCatalog() {
    for (const std::string archetype : {"cruciform_keep", "hill", "river", "palace", "fortress"}) {
        const CastleProgram program = GetCastleProgram(archetype);
        const auto catalog = GetCastleRoomCatalog(archetype);
        Require(catalog.size() >= 8, archetype + ": catalog is too small");
        std::set<std::string> roles;
        std::map<std::string, int> presence;
        for (const auto& spec : catalog) {
            Require(spec.presence >= 1 && spec.presence <= 100, archetype + ": presence out of range for " + spec.role);
            Require(roles.insert(spec.role).second, archetype + ": duplicate catalog role " + spec.role);
            presence[spec.role] = spec.presence;
        }
        for (const auto& spec : catalog) Require(spec.presence <= 95, archetype + ": " + spec.role + " is certain to appear");
        Require(presence[program.chambers[0]] >= 90 && presence[program.chambers[1]] >= 90,
            archetype + ": the principal rooms of the program are not very likely");
        // Left out of some picks, but picked in most.
        int picks = 0, firstPicked = 0, secondPicked = 0;
        for (unsigned seed = 1; seed <= 400; ++seed) {
            const auto picked = PickCastleRooms(catalog, 6, seed);
            std::set<std::string> names;
            for (const auto& spec : picked) names.insert(spec.role);
            ++picks;
            firstPicked += names.count(program.chambers[0]) > 0;
            secondPicked += names.count(program.chambers[1]) > 0;
        }
        Require(firstPicked < picks && secondPicked < picks, archetype + ": a principal room is never left out");
        Require(firstPicked * 100 >= picks * 85 && secondPicked * 100 >= picks * 85, archetype + ": a principal room is picked too rarely");

        std::set<std::set<std::string>> sets;
        for (unsigned seed = 1; seed <= 24; ++seed) {
            const auto picked = PickCastleRooms(catalog, 6, seed);
            Require(picked.size() == 6, archetype + ": wrong number of picked rooms");
            std::set<std::string> names;
            for (const auto& spec : picked) names.insert(spec.role);
            Require(PickCastleRooms(catalog, 6, seed).front().role == picked.front().role,
                archetype + ": picking is not deterministic");
            sets.insert(names);
        }
        Require(sets.size() >= 3, archetype + ": the picked rooms barely vary with the seed");
        Require(PickCastleRooms(catalog, 40, 7).size() == 40, archetype + ": cannot fill a large keep");
    }
}

// The union of all room floors: the silhouette of the castle.
std::string Silhouette(const MapPlan& plan) {
    std::string mask(static_cast<std::size_t>(plan.width * plan.depth), '0');
    for (const auto& region : plan.regions) {
        if (region.kind != "room") continue;
        for (int y = region.y0; y <= region.y1; ++y) for (int x = region.x0; x <= region.x1; ++x)
            mask[static_cast<std::size_t>(y * plan.width + x)] = '1';
    }
    return mask;
}

// (B) Silhouettes and wing rooms vary with the seed, and (size limit) no room
// is oversized.
void TestVarietyAndLimits() {
    for (int mode = 0; mode < 5; ++mode) {
        std::set<std::string> silhouettes;
        std::set<std::multiset<std::string>> wingRoles;
        for (unsigned seed = 1; seed <= QuickSeeds(24); ++seed) {
            const auto plan = Generate(128, mode, seed);
            silhouettes.insert(Silhouette(plan));
            const std::string context = "mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
            int keepArea = 0;
            for (const auto& region : plan.regions)
                if (region.kind == "keep") keepArea = (region.x1 - region.x0 + 1) * (region.y1 - region.y0 + 1);
            Require(keepArea > 0, context + ": no keep");
            std::multiset<std::string> roles;
            MapPlanRegion throne{};
            bool hasThrone = false;
            for (const auto& region : plan.regions)
                if (region.kind == "room" && region.role == "throne_room") { throne = region; hasThrone = true; }
            for (const auto& region : plan.regions) {
                if (region.kind != "room") continue;
                const int width = region.x1 - region.x0 + 1, area = width * (region.y1 - region.y0 + 1);
                // Size limits: the throne room and the hall are the largest rooms; every
                // other room is far smaller. No room is wider than 60 cells.
                // (The hall that stands in for the entrance hall is as large as the entrance hall; a castle with no throne room
                // has a big room at the back of its axis; a room beside the narrowed throne room is as deep as it.)
                const bool besideThrone = hasThrone && width < 30 && region.y0 >= throne.y0 - 1 && region.y1 <= throne.y1 + 1 &&
                    (region.x1 + 1 == throne.x0 || region.x0 == throne.x1 + 1 || (region.x0 > throne.x0 - 24 && region.x1 < throne.x1 + 24));
                const int limitPercent = region.role == "entrance_hall" || region.role == "great_hall" || region.role == "guard_hall" ? 23 :
                    region.role == "throne_room" && hasThrone ? 26 : (!hasThrone && width >= 24) ? 26 : besideThrone ? 14 : region.role == "antechamber" ? 12 : 8;
                Require(area * 100 <= keepArea * limitPercent,
                    context + ": room too large: " + region.role + " " + std::to_string(area * 100 / keepArea) + "% of the keep");
                Require(width <= 60 || region.role == "corridor", context + ": room too wide: " + region.role + " " + std::to_string(width));  // (a corridor is long, not a room)
                if (region.role != "throne_room" && region.role != "entrance_hall" && region.role != "antechamber" &&
                    region.role.find("royal_") != 0 && region.role != "queens_bedchamber") roles.insert(region.role);
            }
            wingRoles.insert(roles);
        }
        if (!QuickMode()) Require(silhouettes.size() >= 3, "mode=" + std::to_string(mode) + ": castle silhouettes barely vary (" +
            std::to_string(silhouettes.size()) + ")");
        if (!QuickMode()) Require(wingRoles.size() >= 4, "mode=" + std::to_string(mode) + ": wing rooms barely vary (" +
            std::to_string(wingRoles.size()) + ")");
        std::cout << "  mode=" << mode << " silhouettes=" << silhouettes.size() << " wing-room sets=" << wingRoles.size() << '\n';
    }
}

// Terrain is independent of the castle type. Only a river castle has water: after
// the castle is placed, one side of the map (left, right or top) becomes water from
// the map edge up to the castle's outer wall.
MapPlan GenerateWith(int size, int mode, unsigned seed, double terrain) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"lotFacilities", 0}};
    if (terrain >= 0) {
        params.extra["exteriorTerrain"] = terrain;
        // An explicit terrain is checked with nature present; the count (1..3) follows the seed.
        params.extra["natureSideCount"] = 1 + static_cast<int>(seed % 3);
    }
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    Require(generator.Generate(plan, error), "terrain case: " + error);
    return plan;
}

// Nature lies along one to three of the left, right and top sides of a castle (never the
// gate side). Which nature it is does not depend on the castle type: forest, cliff or desert
// are drawn per map; only a river castle has water. A terrain can be requested explicitly
// (exteriorTerrain: 0 none, 1 desert, 2 forest, 3 mountain).
int CountNatureCells(const MapPlan& plan, std::uint8_t biome) {
    return static_cast<int>(std::count(plan.biomes.begin(), plan.biomes.end(), biome));
}

void TestTerrainIndependentOfType() {
    int sideCounts[4] = {0, 0, 0, 0}, natures[4] = {0, 0, 0, 0};
    for (const int size : {128, 160}) for (int mode = 0; mode < 5; ++mode) for (unsigned seed = 1; seed <= QuickSeeds(12); ++seed) {
        if (mode == 2) continue;  // the river castle is checked below
        const std::string context = std::to_string(size) + " mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
        const auto plan = GenerateWith(size, mode, seed, -1);
        int sides = 0, waterSides = 0;
        std::string drawn;
        for (const auto& region : plan.regions) {
            if (region.kind == "waterway") ++waterSides;
            if (region.kind != "natural_terrain" && region.kind != "waterway") continue;
            ++sides;
            drawn = region.role;
        }
        // Water is a terrain: it appears only where a waterway region says so.
        if (waterSides == 0)
            for (const std::uint8_t b : plan.biomes) Require(b != RIVER && b != LAKE && b != SEA, context + ": water appeared with no waterway");
        Require(waterSides == 0 || waterSides == sides, context + ": water and another terrain on one castle");
        Require(sides >= 0 && sides <= 3, context + ": nature is on " + std::to_string(sides) + " sides, not zero to three");
        ++sideCounts[sides];
        if (sides > 0) ++natures[waterSides > 0 ? 3 : drawn == "forest_flank" ? 0 : drawn == "cliff_flank" ? 1 : 2];
    }
    if (!QuickMode()) Require(sideCounts[0] > 0 && sideCounts[1] > 0 && sideCounts[2] > 0 && sideCounts[3] > 0, "nature never used zero, one, two and three sides");
    if (!QuickMode()) Require(natures[0] > 0 && natures[1] > 0 && natures[2] > 0 && natures[3] > 0, "forest, cliff, desert and water were not all drawn");
    // A terrain can still be requested explicitly, for any castle type; 0 means none.
    // Mountain and desert nature are both drawn as cliff (TOWN_CLIFF): a natural mountain no
    // longer shares the wall's tile, and the desert's rim is cliff until dunes exist.
    const std::pair<int, std::uint8_t> terrains[] = {{1, TOWN_CLIFF}, {2, FOREST}, {3, TOWN_CLIFF}};
    for (int mode = 0; mode < 5; ++mode) {
        if (mode == 2) continue;
        const auto none = GenerateWith(128, mode, 3, 0);
        for (const auto& region : none.regions) Require(region.kind != "natural_terrain", "terrain 0 still drew nature");
        for (const auto& [code, biome] : terrains) {
            const auto plan = GenerateWith(128, mode, 3, code);
            int natural = 0;
            for (const auto& region : plan.regions) if (region.kind == "natural_terrain") ++natural;
            Require(natural >= 1 && natural <= 3, "mode=" + std::to_string(mode) + " terrain=" + std::to_string(code) + ": nature is not on one to three sides");
            Require(CountNatureCells(plan, biome) > 128, "mode=" + std::to_string(mode) + " terrain=" + std::to_string(code) + ": the requested terrain was ignored");
        }
    }
}

// How the shore sits along the stretch of the building that sticks out furthest
// (`nearest` cells from the water's map edge): lines there, lines with bare ground
// (gap >= 1), lines where the water reaches the wall, and the largest gap.
struct OuterShore { int lines = 0, bare = 0, touch = 0, farthest = 0; };

OuterShore MeasureOuterShore(const std::vector<int>& waterWidth, const std::vector<int>& wall, int nearest) {
    OuterShore result;
    for (std::size_t line = 0; line < wall.size(); ++line) {
        if (wall[line] != nearest) continue;
        const int gap = nearest - waterWidth[line];
        ++result.lines;
        if (gap >= 1) ++result.bare;
        if (gap == 0) ++result.touch;
        result.farthest = std::max(result.farthest, gap);
    }
    return result;
}

// How jagged the shore is: pairs of neighbouring lines, those whose water widths
// differ by at most one cell, and the largest difference.
struct ShoreSteps { int pairs = 0, gentle = 0, largest = 0, reversals = 0; };

ShoreSteps MeasureShoreSteps(const std::vector<int>& waterWidth) {
    ShoreSteps result;
    int direction = 0;
    for (std::size_t line = 1; line < waterWidth.size(); ++line) {
        const int delta = waterWidth[line] - waterWidth[line - 1];
        const int step = std::abs(delta);
        ++result.pairs;
        if (step <= 1) ++result.gentle;
        result.largest = std::max(result.largest, step);
        if (delta != 0) {
            const int sign = delta > 0 ? 1 : -1;
            if (direction != 0 && sign != direction) ++result.reversals;
            direction = sign;
        }
    }
    return result;
}

// MSVC 19.44 (toolset 14.44.35207) crashes with an internal compiler error (C1001, p2
// main.cpp:258, "c2.dll" access violation) while inlining in this function when it is
// optimised (/O1, /O2, /Ob1; /Ob0 and /Od are fine). Compile it without optimisation.
#pragma optimize("", off)
void TestRiverCastleWater() {
    int sidesSeen[3] = {0, 0, 0}, countSeen[4] = {0, 0, 0, 0}, wobbly = 0, cases = 0, bareCases = 0, touchCases = 0, farCases = 0;
    int stepPairs = 0, gentlePairs = 0, reversalCount = 0;
    // Water is a terrain like forest, cliff and desert (exteriorTerrain 4): any castle type can have it.
    for (const int size : {128, 160}) for (unsigned seed = 1; seed <= QuickSeeds(14); ++seed) for (const int variant : {0, 1}) {
        const bool water = variant == 0;
        const int mode = water ? (seed % 2 ? 2 : 0) : 0;
        const auto plan = GenerateWith(size, mode, seed, water ? 4 : (seed % 2 ? 2 : 1));  // water; or forest or desert
        const std::uint8_t natureBiome = water ? RIVER : (seed % 2 ? FOREST : TOWN_CLIFF);
        const std::string context = std::string(water ? "water " : "terrain ") + std::to_string(size) + " seed=" + std::to_string(seed) + " mode=" + std::to_string(mode);
        std::vector<std::uint8_t> building(static_cast<std::size_t>(size * size), 0);
        const auto mark = [&](int x0, int y0, int x1, int y1, int wall) {  // inclusive cells, plus the wall around them
            for (int y = std::max(0, y0 - wall); y <= std::min(size - 1, y1 + wall); ++y)
                for (int x = std::max(0, x0 - wall); x <= std::min(size - 1, x1 + wall); ++x)
                    building[static_cast<std::size_t>(y * size + x)] = 1;
        };
        // The castle's outside wall is two cells (inner wall + outer wall) around the rooms.
        for (const auto& region : plan.regions) {
            if (region.kind == "room") mark(region.x0, region.y0, region.x1, region.y1, 2);
            if (region.kind == "keep_expansion") mark(region.x0, region.y0, region.x1, region.y1, 1);
        }
        const auto nature = [&](int x, int y) { return plan.biomes[static_cast<std::size_t>(y * size + x)] == natureBiome; };
        for (int i = 0; i < size * size; ++i)
            Require(!(building[static_cast<std::size_t>(i)] && plan.biomes[static_cast<std::size_t>(i)] == natureBiome),
                context + ": nature covers a building");
        // Which sides: one region per side, found by where its bounding box touches the map edge.
        const std::string kind = water ? "waterway" : "natural_terrain";
        bool sideOn[3] = {false, false, false};
        int sideCount = 0, topThickness = 0, leftWidth = 0, rightStart = size;
        for (const auto& region : plan.regions) {
            if (region.kind != kind) continue;
            ++sideCount;
            if (region.x0 == 0 && region.x1 < size - 1) { sideOn[0] = true; leftWidth = region.x1 + 1; }
            else if (region.x1 == size - 1 && region.x0 > 0) { sideOn[1] = true; rightStart = region.x0; }
            else if (region.y0 == 0) { sideOn[2] = true; topThickness = region.y1 + 1; }
        }
        Require(sideCount >= 1 && sideCount <= 3, context + ": nature is not on one to three sides");
        ++countSeen[sideCount];
        for (int side = 0; side < 3; ++side) {
            if (!sideOn[side]) continue;
            ++sidesSeen[side];
            const bool alongRows = side != 2, right = side == 1;
            std::vector<int> waterWidth(static_cast<std::size_t>(size), 0), wall(static_cast<std::size_t>(size), -1);
            for (int line = 0; line < size; ++line) {
                for (int k = 0; k < size; ++k) {
                    const int pos = right ? size - 1 - k : k;
                    const int x = alongRows ? pos : line, y = alongRows ? line : pos;
                    if (nature(x, y) && waterWidth[static_cast<std::size_t>(line)] == k) waterWidth[static_cast<std::size_t>(line)] = k + 1;
                    if (wall[static_cast<std::size_t>(line)] < 0 && building[static_cast<std::size_t>(y * size + x)]) wall[static_cast<std::size_t>(line)] = k;
                }
            }
            // Only the lines that face the castle count: nature continues past the castle's ends
            // (and into the corner where two sides meet), where there is no wall to measure against.
            {
                std::vector<int> keptWidth, keptWall;
                for (int line = 0; line < size; ++line) {
                    if (wall[static_cast<std::size_t>(line)] < 0) continue;
                    // The rounded corner where two sides meet is free land with no wall to measure against.
                    if (side != 2 && sideOn[2] && line < topThickness + 26) continue;
                    if (side == 2 && ((sideOn[0] && line < leftWidth + 26) || (sideOn[1] && line > rightStart - 26))) continue;
                    keptWidth.push_back(waterWidth[static_cast<std::size_t>(line)]);
                    keptWall.push_back(wall[static_cast<std::size_t>(line)]);
                }
                waterWidth.swap(keptWidth);
                wall.swap(keptWall);
            }
            const int lines = static_cast<int>(waterWidth.size());
            int widest = 0, nearest = size, lo = size, hi = 0;
            for (int line = 0; line < lines; ++line) {
                if (wall[static_cast<std::size_t>(line)] >= 0) nearest = std::min(nearest, wall[static_cast<std::size_t>(line)]);
                widest = std::max(widest, waterWidth[static_cast<std::size_t>(line)]);
                lo = std::min(lo, waterWidth[static_cast<std::size_t>(line)]); hi = std::max(hi, waterWidth[static_cast<std::size_t>(line)]);
            }
            Require(widest <= nearest, context + ": nature is wider than the building's outermost point (" +
                std::to_string(widest) + " > " + std::to_string(nearest) + "): it filled a notch");
            // The baseline of the shore is two cells short of the building's outermost point;
            // the wobble moves it closer to the wall and further from it.
            const OuterShore outer = MeasureOuterShore(waterWidth, wall, nearest);
            Require(outer.lines > 0, context + ": no outermost point of the building");
            // Forest, cliff and desert reach in at most 10% of the map, so on a small castle they may stay
            // well away from the wall; the water of a river castle follows the wall.
            if (water) Require(outer.farthest <= 8, context + ": the shore drifts too far from the wall (" + std::to_string(outer.farthest) + ")");
            else Require(widest <= size / 10, context + ": nature reaches " + std::to_string(widest) + " cells, more than 10% of the map");
            const ShoreSteps steps = MeasureShoreSteps(waterWidth);
            Require(steps.largest <= 1, context + ": the shore has a jump of " + std::to_string(steps.largest) + " cells between neighbouring lines");
            stepPairs += steps.pairs; gentlePairs += steps.gentle; reversalCount += steps.reversals;
            if (water) {
                ++bareCases;
                if (outer.touch > 0) ++touchCases;
                if (outer.farthest >= 3) ++farCases;
            }
            // Wide enough to read: at least 3% of the map (the castle keeps 5% clear of the edge, so a side may be thin), and no annex is built on this side to squeeze it.
            Require(widest * 100 >= size * 3, context + ": the nature is too narrow (" + std::to_string(widest) + " cells)");
            wobbly += (hi - lo >= 2) ? 1 : 0;
            ++cases;
        }
    }
    if (!QuickMode()) Require(sidesSeen[0] > 0 && sidesSeen[1] > 0 && sidesSeen[2] > 0, "nature never used one of the left, right or top sides");
    if (!QuickMode()) Require(countSeen[1] > 0 && countSeen[2] > 0 && countSeen[3] > 0, "nature never used one, two and three sides");
    Require(wobbly * 10 >= cases * 8, "the shore is too straight: only " + std::to_string(wobbly) + " of " + std::to_string(cases) + " shores wobble");
    // Smooth: the shore changes direction rarely, and every step to the next line is at most one cell.
    Require(reversalCount * 100 <= stepPairs * 2, "the shore wiggles: " + std::to_string(reversalCount) + " direction changes in " + std::to_string(stepPairs) + " lines");
    Require(gentlePairs * 100 >= stepPairs * 90, "the shore is jagged: " + std::to_string(gentlePairs) + " of " + std::to_string(stepPairs) + " steps are gentle");
    // Some shores come back to the wall and some drift away from it.
    Require(touchCases * 10 >= bareCases * 2, "the shore rarely comes back to the wall: " + std::to_string(touchCases) + " of " + std::to_string(bareCases));
    Require(farCases * 10 >= bareCases * 3, "the shore rarely drifts away from the wall: " + std::to_string(farCases) + " of " + std::to_string(bareCases));
}

// Where two nature sides meet (top with left or right), the corner of the land is rounded
// by an arc whose radius follows the free space, with a gentle wobble. A square corner would
// leave a single land cell with nature straight above it and straight beside it; a rounded
// one leaves a diagonal run of them.
void TestNatureCornersAreRounded() {
    int roomy = 0, rounded = 0;
    for (const int size : {128, 160}) for (unsigned seed = 1; seed <= QuickSeeds(24); ++seed) for (const int terrain : {2, 1}) {
        const auto plan = GenerateWith(size, 0, seed, terrain);
        const std::uint8_t nature = terrain == 2 ? FOREST : TOWN_CLIFF;
        const std::string context = "corner " + std::to_string(size) + " seed=" + std::to_string(seed) + " terrain=" + std::to_string(terrain);
        bool left = false, right = false, top = false;
        int topThickness = 0, leftWidth = 0, rightStart = size;
        for (const auto& region : plan.regions) {
            if (region.kind != "natural_terrain") continue;
            if (region.x0 == 0 && region.x1 < size - 1) { left = true; leftWidth = region.x1 + 1; }
            else if (region.x1 == size - 1 && region.x0 > 0) { right = true; rightStart = region.x0; }
            else if (region.y0 == 0) { top = true; topThickness = region.y1 + 1; }
        }
        {
            // Nature is one connected piece per side: no floating islands, and no slit of land one
            // cell wide between two pieces of nature (a gap in the rounded corner).
            std::vector<std::uint8_t> seen(static_cast<std::size_t>(size * size), 0);
            std::vector<int> stack;
            for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
                const bool edge = x == 0 || y == 0 || x == size - 1 || y == size - 1;
                if (!edge || plan.biomes[static_cast<std::size_t>(y * size + x)] != nature) continue;
                seen[static_cast<std::size_t>(y * size + x)] = 1;
                stack.push_back(y * size + x);
            }
            while (!stack.empty()) {
                const int cell = stack.back(); stack.pop_back();
                const int cx = cell % size, cy = cell / size;
                const int nx[4] = {cx + 1, cx - 1, cx, cx}, ny[4] = {cy, cy, cy + 1, cy - 1};
                for (int d = 0; d < 4; ++d) {
                    if (nx[d] < 0 || ny[d] < 0 || nx[d] >= size || ny[d] >= size) continue;
                    const std::size_t index = static_cast<std::size_t>(ny[d] * size + nx[d]);
                    if (seen[index] || plan.biomes[index] != nature) continue;
                    seen[index] = 1;
                    stack.push_back(static_cast<int>(index));
                }
            }
            // (Single street trees along the avenue use the forest tile too: pieces under four
            // cells are not nature.)
            int islands = 0, slits = 0;
            for (int y = 1; y + 1 < size; ++y) for (int x = 1; x + 1 < size; ++x) {
                const std::size_t index = static_cast<std::size_t>(y * size + x);
                if (plan.biomes[index] == nature && !seen[index]) {
                    int piece = 0;
                    stack.push_back(y * size + x);
                    seen[index] = 2;
                    while (!stack.empty()) {
                        const int cell = stack.back(); stack.pop_back();
                        ++piece;
                        const int cx = cell % size, cy = cell / size;
                        const int nx[4] = {cx + 1, cx - 1, cx, cx}, ny[4] = {cy, cy, cy + 1, cy - 1};
                        for (int d = 0; d < 4; ++d) {
                            if (nx[d] < 0 || ny[d] < 0 || nx[d] >= size || ny[d] >= size) continue;
                            const std::size_t other = static_cast<std::size_t>(ny[d] * size + nx[d]);
                            if (seen[other] || plan.biomes[other] != nature) continue;
                            seen[other] = 2;
                            stack.push_back(static_cast<int>(other));
                        }
                    }
                    if (piece >= 4) islands += piece;
                }
                if (plan.biomes[index] == nature || plan.biomes[index] == ROAD || plan.biomes[index] == BRIDGE) continue;
                // Only the rounded corners are checked: the shore itself may dip a cell now and then.
                if (!(top && y < topThickness + 26 && ((left && x < leftWidth + 26) || (right && x > rightStart - 26)))) continue;
                bool nearRoad = false;  // street trees stand beside the avenue: not a gap in the nature
                for (int dy = -3; dy <= 3 && !nearRoad; ++dy) for (int dx = -3; dx <= 3; ++dx) {
                    const int rx = x + dx, ry = y + dy;
                    if (rx < 0 || ry < 0 || rx >= size || ry >= size) continue;
                    const std::uint8_t other = plan.biomes[static_cast<std::size_t>(ry * size + rx)];
                    if (other == ROAD || other == BRIDGE) { nearRoad = true; break; }
                }
                if (nearRoad) continue;
                const auto isNature = [&](int cx, int cy) { return plan.biomes[static_cast<std::size_t>(cy * size + cx)] == nature; };
                if ((isNature(x - 1, y) && isNature(x + 1, y)) || (isNature(x, y - 1) && isNature(x, y + 1))) ++slits;
            }
            Require(islands == 0, context + ": " + std::to_string(islands) + " cells of nature float apart from the map edge");
            Require(slits == 0, context + ": " + std::to_string(slits) + " land cells are pinched between nature (a gap)");
        }
        if (!top || (!left && !right)) continue;
        // Cells of the castle: rooms with their two-cell wall, wings with one.
        std::vector<std::uint8_t> building(static_cast<std::size_t>(size * size), 0);
        const auto mark = [&](int x0, int y0, int x1, int y1, int wall) {
            for (int y = std::max(0, y0 - wall); y <= std::min(size - 1, y1 + wall); ++y)
                for (int x = std::max(0, x0 - wall); x <= std::min(size - 1, x1 + wall); ++x)
                    building[static_cast<std::size_t>(y * size + x)] = 1;
        };
        for (const auto& region : plan.regions) {
            if (region.kind == "room") mark(region.x0, region.y0, region.x1, region.y1, 2);
            if (region.kind == "keep_expansion") mark(region.x0, region.y0, region.x1, region.y1, 1);
        }
        const auto at = [&](int x, int y) { return plan.biomes[static_cast<std::size_t>(y * size + x)]; };
        for (const bool rightCorner : {true, false}) {
            if (rightCorner ? !right : !left) continue;
            // The staircase: land cells with nature in a straight line above them to the map
            // edge and in a straight line beside them (toward the side) to the map edge.
            int staircase = 0;
            for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
                if (at(x, y) == nature) continue;
                bool up = true, across = true;
                for (int k = 0; k < y && up; ++k) up = at(x, k) == nature;
                if (rightCorner) for (int k = x + 1; k < size && across; ++k) across = at(k, y) == nature;
                else for (int k = 0; k < x && across; ++k) across = at(k, y) == nature;
                if (up && across) ++staircase;
            }
            // Free space: a 12x12 square at the extreme corner of the land with no castle in it
            // (and two cells to spare).
            const int cornerX = rightCorner ? rightStart - 1 : leftWidth;
            bool free = true;
            for (int y = topThickness; y < topThickness + 12 && free; ++y)
                for (int dx = -2; dx < 14 && free; ++dx) {
                    const int x = rightCorner ? cornerX - dx + 2 : cornerX + dx - 2;
                    if (x < 0 || x >= size || y >= size) { free = false; break; }
                    if (building[static_cast<std::size_t>(y * size + x)]) free = false;
                }
            if (!free) continue;
            ++roomy;
            if (staircase >= 5) ++rounded;
            Require(staircase >= 5, context + ": the corner of the land is square (staircase " + std::to_string(staircase) + ") though there is room to round it");
        }
    }
    if (!QuickMode()) Require(roomy >= 6, "too few roomy corners were checked: " + std::to_string(roomy));
    (void)rounded;
}

#pragma optimize("", on)

// The water's edge is graded like a village's (ApplySettlementShore): dry banks
// beside the water are cut low and gently, and nothing near a building is touched.
void TestRiverBanksAreGraded() {
    for (const int size : {128, 160}) for (unsigned seed = 1; seed <= QuickSeeds(6); ++seed) {
        const auto plan = GenerateWith(size, seed % 2 ? 2 : 0, seed, 4);  // water on a river castle and on another type
        const std::string context = "water " + std::to_string(size) + " seed=" + std::to_string(seed);
        Require(plan.shoreCuts.size() == plan.biomes.size(), context + ": the water's edge is not graded");
        std::vector<int> waterDistance(plan.biomes.size(), 1000);
        for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
            if (plan.biomes[static_cast<std::size_t>(y * size + x)] != RIVER) continue;
            for (int dy = -4; dy <= 4; ++dy) for (int dx = -4; dx <= 4; ++dx) {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= size || ny >= size) continue;
                auto& d = waterDistance[static_cast<std::size_t>(ny * size + nx)];
                d = std::min(d, std::abs(dx) + std::abs(dy));
            }
        }
        int cut = 0;
        for (std::size_t i = 0; i < plan.biomes.size(); ++i) {
            if (plan.shoreCuts[i] <= 0) continue;
            ++cut;
            Require(waterDistance[i] <= 4, context + ": a bank far from the water was cut");
            const std::uint8_t b = plan.biomes[i];
            Require(b != MOUNTAIN && b != EXMOUNTAIN && (b < TOWN_FLOOR || b > TOWN_PAVEMENT) && b != ROAD && b != BRIDGE,
                context + ": a building or road was cut");
        }
        Require(cut > 0, context + ": no bank beside the water was graded");
    }
    // Without water there are no banks to grade: the only cells ever cut are the cliff's own
    // edge (a cliff tapers off the way it does in a village).
    const auto dry = GenerateWith(128, 0, 3, 2);  // forest
    Require(dry.shoreCuts.size() == dry.biomes.size(), "a castle without water has no cut map");
    for (std::size_t i = 0; i < dry.shoreCuts.size(); ++i)
        Require(dry.shoreCuts[i] == 0 || dry.biomes[i] == TOWN_CLIFF, "a castle without water has graded banks");
}
}

// Water is one of the exterior terrains, drawn with forest, mountain and desert: the river castle type
// (its rooms: harbour, navigation, toll) no longer brings water with it, and any type can have water.
void TestWaterIsATerrain() {
    int waterCastles = 0, riverTypeDry = 0, riverTypeWet = 0, otherWet = 0, drawn = 0;
    const auto hasWater = [](const MapPlan& plan) { return std::count(plan.biomes.begin(), plan.biomes.end(), RIVER) > 200; };
    for (unsigned seed = 1; seed <= QuickSeeds(24); ++seed) {
        // 1. Requested: water on any castle type, with a waterway region.
        for (const int mode : {0, 1, 2, 3, 4}) {
            const auto plan = GenerateWith(128, mode, seed, 4);
            const std::string context = "water terrain mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
            Require(hasWater(plan), context + ": no water");
            Require(std::any_of(plan.regions.begin(), plan.regions.end(), [](const auto& r) { return r.kind == "waterway"; }), context + ": no waterway region");
            ++waterCastles;
        }
        // 2. The river castle type with another terrain (or none) has no water.
        for (const int terrain : {0, 2, 3, 1}) {
            const auto plan = GenerateWith(128, 2, seed, terrain);
            Require(!hasWater(plan), "river castle with terrain " + std::to_string(terrain) + " seed=" + std::to_string(seed) + ": water appeared");
        }
        // 3. Drawn (no terrain given): water is one of the draws, for the river type and for the others alike.
        for (const int mode : {2, 0}) {
            const auto plan = GenerateWith(128, mode, seed, -1);
            ++drawn;
            if (mode == 2) (hasWater(plan) ? riverTypeWet : riverTypeDry)++;
            else otherWet += hasWater(plan);
        }
    }
    if (!QuickMode()) Require(riverTypeDry > 0, "the river castle type always has water: it is not detached from the terrain");
    if (!QuickMode()) Require(riverTypeWet > 0 && otherWet > 0, "water is never drawn as a terrain");
    if (!QuickMode()) Require(waterCastles == 24 * 5 && drawn == 48, "internal count");
}

int main() {
    try {
        TestCatalog();
        TestWaterIsATerrain();
        TestTerrainIndependentOfType();
        TestRiverCastleWater();
        TestNatureCornersAreRounded();
        TestRiverBanksAreGraded();
        TestVarietyAndLimits();
        std::cout << "CASTLE_VARIETY: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CASTLE_VARIETY: FAIL " << error.what() << '\n';
        return 1;
    }
}
