#include "MapImageGenerator.h"
#include "SettlementSite.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct Terrain {
    MapPlan plan;
    std::vector<std::uint8_t> footprint, structure;
    Terrain(int site, int rotation, int seed, bool village = false, int width = 160, int height = 160,
        bool river = false, bool pond = false) {
        plan.Reset(width, height, seed, village ? "village" : "town");
        plan.biomes.assign(width * height, SEA);
        const auto ground = static_cast<std::uint8_t>(site == 6 ? DESERT : site == 3 ? FOREST : PLAIN);
        PrepareSettlementSite(plan, footprint, structure, static_cast<SettlementSite>(site), rotation,
            ground, village, river, pond);
    }
};
bool Water(int tile) { return tile == RIVER || tile == LAKE || tile == TOWN_SEA; }

int Components(const Terrain& terrain, bool water) {
    const int w = terrain.plan.width, h = terrain.plan.depth;
    std::vector<bool> seen(w * h, false);
    int components = 0;
    const auto included = [&](int i) { return water ? Water(terrain.plan.biomes[i]) : terrain.footprint[i] != 0; };
    for (int start = 0; start < w * h; ++start) {
        if (seen[start] || !included(start)) continue;
        ++components;
        std::queue<int> pending;
        pending.push(start); seen[start] = true;
        while (!pending.empty()) {
            const int i = pending.front(); pending.pop();
            const int neighbors[] = {i % w ? i - 1 : -1, i % w + 1 < w ? i + 1 : -1,
                i >= w ? i - w : -1, i + w < w * h ? i + w : -1};
            for (int n : neighbors) {
                if (n < 0 || seen[n] || !included(n)) continue;
                seen[n] = true; pending.push(n);
            }
        }
    }
    return components;
}

// A circle/ellipse is convex. Measure real bays and dry shoulders against the
// convex hull of the water's cell corners, not an arbitrary jagged perimeter.
double WaterSolidity(const MapPlan& plan) {
    using Point = std::pair<int, int>;
    std::vector<Point> points;
    int wet = 0;
    for (int y = 0; y < plan.depth; ++y) for (int x = 0; x < plan.width; ++x) {
        if (plan.biomes[y * plan.width + x] != LAKE) continue;
        ++wet;
        points.insert(points.end(), {{x,y}, {x+1,y}, {x,y+1}, {x+1,y+1}});
    }
    Require(wet > plan.width, "basin missing");
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    const auto cross = [](Point o, Point a, Point b) {
        return (a.first-o.first)*(b.second-o.second)-(a.second-o.second)*(b.first-o.first);
    };
    std::vector<Point> hull;
    for (const auto& p : points) {
        while (hull.size() >= 2 && cross(hull[hull.size()-2], hull.back(), p) <= 0) hull.pop_back();
        hull.push_back(p);
    }
    const auto lower = hull.size();
    for (auto it = points.rbegin()+1; it != points.rend(); ++it) {
        while (hull.size() > lower && cross(hull[hull.size()-2], hull.back(), *it) <= 0) hull.pop_back();
        hull.push_back(*it);
    }
    double area2 = 0;
    for (std::size_t i = 1; i < hull.size(); ++i)
        area2 += hull[i-1].first*hull[i].second-hull[i].first*hull[i-1].second;
    return wet / (std::abs(area2)*.5);
}

using Silhouette = std::array<std::uint8_t,32*32>;
Silhouette NormalizeWater(const MapPlan& plan) {
    int left = plan.width, right = 0, top = plan.depth, bottom = 0;
    for (int y = 0; y < plan.depth; ++y) for (int x = 0; x < plan.width; ++x) {
        if (!Water(plan.biomes[y*plan.width+x])) continue;
        left = std::min(left,x); right = std::max(right,x); top = std::min(top,y); bottom = std::max(bottom,y);
    }
    Require(left <= right && top <= bottom, "missing water in diversity sample");
    Silhouette result{};
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        const int sx = left + static_cast<int>((x+.5)*(right-left+1)/32);
        const int sy = top + static_cast<int>((y+.5)*(bottom-top+1)/32);
        result[y*32+x] = Water(plan.biomes[sy*plan.width+sx]);
    }
    return result;
}

double Similarity(const Silhouette& a, const Silhouette& b) {
    double best = 0;
    for (int reflection = 0; reflection < 2; ++reflection) for (int rotation = 0; rotation < 4; ++rotation) {
        int intersection = 0, combined = 0;
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            int bx = reflection ? 31-x : x, by = y;
            for (int r = 0; r < rotation; ++r) { const int previous = bx; bx = 31-by; by = previous; }
            intersection += a[y*32+x] && b[by*32+bx];
            combined += a[y*32+x] || b[by*32+bx];
        }
        best = std::max(best,static_cast<double>(intersection)/std::max(1,combined));
    }
    return best;
}

void TestShapeDiversity() {
    for (int site : {1,5,6}) {
        std::vector<Silhouette> shapes;
        std::set<Silhouette> unique;
        int minimumArea = 160*160, maximumArea = 0, concave = 0;
        for (int seed = 1; seed <= 32; ++seed) {
            const Terrain terrain(site,0,seed);
            Require(Components(terrain,true) == 1, "diversity sample has fragmented water: site=" +
                std::to_string(site) + " seed=" + std::to_string(seed));
            const int area = static_cast<int>(std::count_if(terrain.plan.biomes.begin(),terrain.plan.biomes.end(),Water));
            minimumArea = std::min(minimumArea,area); maximumArea = std::max(maximumArea,area);
            if (site != 1 && WaterSolidity(terrain.plan) < .90) ++concave;
            shapes.push_back(NormalizeWater(terrain.plan)); unique.insert(shapes.back());
        }
        double totalSimilarity = 0;
        int pairs = 0, verySimilar = 0;
        for (std::size_t a = 0; a < shapes.size(); ++a) for (std::size_t b = a+1; b < shapes.size(); ++b) {
            const double similarity = Similarity(shapes[a],shapes[b]);
            totalSimilarity += similarity; ++pairs;
            if (similarity > .95) ++verySimilar;
        }
        const double average = totalSimilarity/pairs;
        std::cout << "DIVERSITY " << SettlementSiteName(static_cast<SettlementSite>(site)) <<
            " unique=" << unique.size() << "/32 normalized_similarity=" << average <<
            " nearly_identical_pairs=" << verySimilar << "/" << pairs <<
            " area_range=" << minimumArea << ".." << maximumArea << '\n';
        Require(unique.size() >= 30 && average < .90 && verySimilar < pairs/5,
            "terrain diversity comes only from translation, scaling or quarter-turns/reflection");
        Require(maximumArea > minimumArea*1.5, "water extent barely varies across seeds");
        if (site != 1) Require(concave >= 16, "basins reverted to predominantly oval stamps");
    }
}

void TestOasisVegetation() {
    for (int seed = 1; seed <= 32; ++seed) {
        const Terrain terrain(6, 0, seed);
        int shoreline = 0, shorelineTrees = 0, trees = 0;
        for (int y = 0; y < terrain.plan.depth; ++y) for (int x = 0; x < terrain.plan.width; ++x) {
            const int i = y * terrain.plan.width + x;
            const int tile = terrain.plan.biomes[i];
            trees += tile == TOWN_TREES;
            if (tile == TOWN_TREES) {
                bool nextToWater = false;
                for (const auto& offset : {std::pair<int,int>{-1,0}, {1,0}, {0,-1}, {0,1}}) {
                    const int nx = x + offset.first, ny = y + offset.second;
                    if (nx >= 0 && ny >= 0 && nx < terrain.plan.width && ny < terrain.plan.depth &&
                        terrain.plan.biomes[ny * terrain.plan.width + nx] == LAKE) nextToWater = true;
                }
                shorelineTrees += nextToWater;
            } else if (!Water(tile)) {
                for (const auto& offset : {std::pair<int,int>{-1,0}, {1,0}, {0,-1}, {0,1}}) {
                    const int nx = x + offset.first, ny = y + offset.second;
                    if (nx >= 0 && ny >= 0 && nx < terrain.plan.width && ny < terrain.plan.depth &&
                        terrain.plan.biomes[ny * terrain.plan.width + nx] == LAKE) { ++shoreline; break; }
                }
            }
        }
        Require(trees > 0, "oasis has no vegetation grove at seed=" + std::to_string(seed));
        Require(shoreline + shorelineTrees > 0 && shorelineTrees * 100 < (shoreline + shorelineTrees) * 85,
            "oasis vegetation forms a continuous shoreline ring at seed=" + std::to_string(seed));
    }
}

void Check(int site, int rotation, int seed, int width = 160, int height = 160) {
    const std::string context = "site=" + std::to_string(site) + " rotation=" + std::to_string(rotation) +
        " seed=" + std::to_string(seed) + " size=" + std::to_string(width) + ": ";
    const Terrain town(site, rotation, seed, false, width, height);
    const Terrain village(site, rotation, seed, true, width, height);
    const Terrain repeat(site, rotation, seed, false, width, height);
    Require(town.plan.biomes == repeat.plan.biomes && town.footprint == repeat.footprint &&
        town.structure == repeat.structure, context + "terrain is not deterministic");
    Require(town.plan.terrainModel == "catchment_and_settlement_v2", context + "wrong terrain model");
    const char* stages[] = {"substrate_and_relief", "catchment_incision", "alluvial_deposition",
        "connected_inundation", "moisture_and_woodland", "terrain_guided_clearing"};
    Require(town.plan.terrainHistory.size() == 6, context + "missing history");
    for (int stage = 0; stage < 6; ++stage) {
        const auto& record = town.plan.terrainHistory[stage];
        Require(record.process == stages[stage] && record.affectedCells >= 0 && record.affectedCells <= width*height,
            context + "invalid process metadata");
    }
    Require(town.plan.terrainHistory[0].affectedCells == width*height && town.plan.terrainHistory[1].affectedCells > 0,
        context + "relief/incision did not run");
    if (site != 0 && site != 3) Require(town.plan.terrainHistory[2].affectedCells > 0, context + "no sediment deposition");
    for (const Terrain* terrain : {&town, &village}) {
        Require(Components(*terrain, false) == 1, context + "clearing has isolated pieces");
        int wet = 0, wooded = 0, cleared = 0;
        for (int i = 0; i < width*height; ++i) {
            const int tile = terrain->plan.biomes[i];
            Require(tile != SEA, context + "full-canvas terrain contains a blank cell");
            wet += Water(tile); wooded += tile == TOWN_TREES; cleared += terrain->footprint[i] != 0;
            if (tile == LAKE || tile == TOWN_SEA || tile == TOWN_CLIFF || tile == TOWN_TREES)
                Require(!terrain->footprint[i], context + "clearing consumes water/rock/retained trees");
            Require((tile == TOWN_CLIFF || tile == TOWN_TREES) == (terrain->structure[i] != 0), context + "bad obstacle mask");
        }
        Require(terrain->plan.settlementMask == terrain->footprint, context + "development mask missing");
        Require(terrain->plan.terrainHistory[3].affectedCells == wet && terrain->plan.terrainHistory[4].affectedCells == wooded &&
            terrain->plan.terrainHistory[5].affectedCells == cleared, context + "history counters disagree with geometry wet=" +
            std::to_string(wet) + ",historyWet=" + std::to_string(terrain->plan.terrainHistory[3].affectedCells) +
            ",trees=" + std::to_string(wooded) + ",historyTrees=" + std::to_string(terrain->plan.terrainHistory[4].affectedCells) +
            ",cleared=" + std::to_string(cleared) + ",historyCleared=" + std::to_string(terrain->plan.terrainHistory[5].affectedCells));
        if (site == 1 || site == 4 || site == 5 || site == 6)
            Require(Components(*terrain, true) == 1, context + "water system fragmented");
        // Individual compact pools may be nearly convex; the population must
        // contain genuinely different concave shapes (TestShapeDiversity).
    }
    for (int i = 0; i < width*height; ++i) {
        const int a = town.plan.biomes[i], b = village.plan.biomes[i];
        if (Water(a) || Water(b) || a == TOWN_CLIFF || b == TOWN_CLIFF)
            Require(a == b, context + "settlement size changed geological water/rock geometry");
        if (village.footprint[i]) Require(town.footprint[i], context + "city expansion lost old cleared land");
    }
}
}

int main() {
    try {
        int cases = 0;
        for (int site = 0; site < 7; ++site) {
            for (int rotation = 0; rotation < 4; ++rotation) {
                for (int seed : {1, 42, 4242, 18416881}) { Check(site, rotation, seed); ++cases; }
                Check(site, rotation, 4242, 96, 128); ++cases;
            }
            std::cout << "PASS terrain " << SettlementSiteName(static_cast<SettlementSite>(site)) << '\n';
        }
        for (int seed : {1, 42, 4242}) for (int site : {0, 3}) {
            const Terrain pond(site, 0, seed, false, 160, 160, false, true);
            const int wet = static_cast<int>(std::count_if(pond.plan.biomes.begin(), pond.plan.biomes.end(), Water));
            Require(wet > 100 && wet < 160*160/12, "optional remnant pond flooded the entire dry valley");
            Require(Components(pond, true) == 1, "remnant pond is fragmented");
        }
        Require(Terrain(5,0,1).plan.biomes != Terrain(5,0,4242).plan.biomes, "seed does not affect terrain");
        TestShapeDiversity();
        TestOasisVegetation();
        std::cout << "PASS terrain history: " << cases << " paired/duplicate cases; optional pools and seed variation\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
