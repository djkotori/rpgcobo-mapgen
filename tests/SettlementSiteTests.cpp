#include "TownMapImageGenerator.h"
#include "VillageMapImageGenerator.h"
#include "SettlementSite.h"
#include "MapQuality.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void TestDevelopmentConnectivityGate() {
    MapPlan plan;
    plan.Reset(32, 32, 1, "town");
    plan.biomes.assign(32*32, PLAIN);
    plan.settlementMask.assign(32*32, 0);
    for (int y = 0; y < 32; ++y) {
        plan.biomes[y*32+8] = RIVER;
        for (int x = 12; x < 30; ++x) plan.settlementMask[y*32+x] = 1;
    }
    plan.markers.push_back({"landmark", 24, 16, 0});
    plan.markers.back().role = "town_center";
    const auto naturalBank = EvaluateMapQuality(plan);
    Require(naturalBank.largestWalkableComponentRatio < .95 && naturalBank.townDevelopmentReachableRatio == 1.0 &&
        naturalBank.reachableMarkers == 1, "uninhabited opposite bank must not replace the town-centre access origin");
    // The same disconnected bank becomes a real failure if it is developed.
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 8; ++x) plan.settlementMask[y*32+x] = 1;
    const auto inhabitedBank = EvaluateMapQuality(plan);
    Require(inhabitedBank.townDevelopmentReachableRatio < .95 &&
        std::find(inhabitedBank.issues.begin(), inhabitedBank.issues.end(),
            "developed settlement land is not at least 95% connected to its centre") != inhabitedBank.issues.end(),
        "disconnected developed land escaped the connectivity gate");
    plan.biomes[0] = SEA;
    const auto blank = EvaluateMapQuality(plan);
    Require(std::find(blank.issues.begin(), blank.issues.end(), "settlement has blank background cells") != blank.issues.end(),
        "a blank corner escaped the full-canvas gate");
}

MapPlan Generate(bool village, int site, int rotation, int seed, int width = 160, int height = 160) {
    MapGenParams params;
    params.width = width; params.depth = height; params.seed = seed;
    params.extra["settlementSite"] = site;
    params.extra["siteRotation"] = rotation;
    params.extra["townCityType"] = rotation;
    MapPlan plan;
    std::string error;
    const bool generated = village ? VillageMapImageGenerator(params).Generate(plan, error) : TownMapImageGenerator(params).Generate(plan, error);
    const std::string context = std::string(village ? "village" : "town") + " site=" + std::to_string(site) +
        " rotation=" + std::to_string(rotation) + " seed=" + std::to_string(seed) + " size=" + std::to_string(width);
    Require(generated, context + ": " + error);
    const auto quality = EvaluateMapQuality(plan);
    std::string issues;
    for (const auto& issue : quality.issues) issues += issue + "; ";
    Require(quality.Passed(), context + ": " + issues);
    Require(quality.townDevelopmentReachableRatio >= .95, context + ": developed land disconnected");
    Require(plan.settlementSite == SettlementSiteName(static_cast<SettlementSite>(site)), context + ": site changed during selection");
    Require(plan.siteRotation == rotation, context + ": orientation changed");
    Require(plan.localEconomy == SettlementSiteEconomy(static_cast<SettlementSite>(site)), context + ": economy missing");
    Require(plan.terrainModel == "catchment_and_settlement_v2" && plan.terrainHistory.size() == 6,
        context + ": terrain history missing");
    if (site != 0 && site != 3) {
        MapPlan natural;
        natural.Reset(width, height, seed, "town");
        natural.biomes.assign(width * height, SEA);
        std::vector<std::uint8_t> footprint, structure;
        PrepareSettlementSite(natural, footprint, structure, static_cast<SettlementSite>(site), rotation,
            site == 6 ? DESERT : PLAIN, village);
        for (int i = 0; i < width * height; ++i) {
            const int tile = natural.biomes[i];
            if (tile == TOWN_SEA || tile == LAKE || tile == TOWN_CLIFF || tile == TOWN_TREES)
                Require(plan.biomes[i] == tile, context + ": development overwrote protected natural terrain");
            if (tile == RIVER) Require(plan.biomes[i] == RIVER || plan.biomes[i] == BRIDGE,
                context + ": river was overwritten without a bridge");
        }
    }
    const auto tileCount = [&](int tile) { return std::count(plan.biomes.begin(), plan.biomes.end(), tile); };
    Require(tileCount(SEA) == 0, context + ": map contains blank background");
    Require(plan.settlementMask.size() == plan.biomes.size(), context + ": development mask missing");
    std::set<std::pair<int,int>> exits;
    for (const auto& marker : plan.markers) {
        if (marker.kind != "map_exit") continue;
        Require(marker.x == 0 || marker.y == 0 || marker.x == width-1 || marker.y == height-1,
            context + ": external road does not reach the image edge");
        Require(plan.biomes[marker.y*width+marker.x] == ROAD, context + ": map exit has no road");
        exits.insert({marker.x,marker.y});
    }
    Require(exits.size() >= 2, context + ": fewer than two distinct map-edge connections");
    // Entire approach routes, including city gates, must be in the one road
    // network. Terrain outside development must not acquire buildings/paving.
    for (int i = 0; i < width*height; ++i) {
        if (plan.settlementMask[i]) continue;
        Require(plan.biomes[i] != TOWN_PAVEMENT && plan.biomes[i] != MOUNTAIN &&
            plan.biomes[i] != TOWN_FURNITURE && plan.biomes[i] != TOWN_WOOD_FLOOR &&
            plan.biomes[i] != TOWN_STONE_FLOOR && plan.biomes[i] != TOWN_FLOOR,
            context + ": development leaked into the countryside");
    }
    if (village) Require(tileCount(TOWN_PAVEMENT) == 0 && plan.pavingActualRatio == 0, context + ": rural paving is not zero");
    const int signatureTile[] = {-1, TOWN_SEA, TOWN_CLIFF, TOWN_TREES, RIVER, LAKE, LAKE};
    if (site > 0) Require(tileCount(signatureTile[site]) > width, context + ": defining terrain was erased");
    if (site == 6) Require(plan.environment == "arid" && tileCount(TOWN_TREES) > 0, context + ": oasis lost sand/palms");
    const auto facility = std::find_if(plan.regions.begin(), plan.regions.end(), [&](const auto& r) {
        return (r.kind == "building" || r.kind == "market") && r.role == SettlementSiteFacility(static_cast<SettlementSite>(site));
    });
    Require(facility != plan.regions.end(), context + ": economic facility missing");
    Require(facility->householdId >= 0 && !facility->residentProfile.empty(), context + ": facility has no household");
    if (site > 0) {
        int distance = width + height;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            if (plan.biomes[y * width + x] != signatureTile[site]) continue;
            const int dx = std::max({facility->x0 - x, 0, x - facility->x1});
            const int dy = std::max({facility->y0 - y, 0, y - facility->y1});
            distance = std::min(distance, dx + dy);
        }
        Require(distance <= std::min(width, height) / 4, context + ": economic facility is too far from its resource");
    }
    for (const auto& entrance : plan.markers) {
        if (entrance.kind != "entrance") continue;
        if (site == 2) {
            double u = static_cast<double>(entrance.x) / (width - 1), v = static_cast<double>(entrance.y) / (height - 1);
            for (int i = 0; i < (4 - rotation) % 4; ++i) { const double old = u; u = 1.0 - v; v = old; }
            Require(v < 0.12 || v > 0.88, context + ": mountain exit goes through a cliff instead of a pass");
        }
    }
    return plan;
}
}

int main() {
    try {
        TestDevelopmentConnectivityGate();
        int count = 0;
        for (int site = 0; site < 7; ++site) {
            for (bool village : {false, true}) {
                for (int rotation = 0; rotation < 4; ++rotation) {
                    for (int seed : {1, 4242}) { Generate(village, site, rotation, seed); ++count; }
                }
            }
            std::cout << "PASS " << SettlementSiteName(static_cast<SettlementSite>(site)) << " (town/village, all orientations/city types)\n";
        }
        for (int site = 1; site < 7; ++site) {
            for (bool village : {false, true}) { Generate(village, site, 1, 42, 96, 128); ++count; }
        }
        // Regression: counting the mountains as housing land made the 320px
        // valley require 96 buildings where the real valley could fit only 78.
        for (int site : {1, 2, 3, 6}) {
            for (bool village : {false, true}) { Generate(village, site, 2, 4242, 320, 320); ++count; }
        }
        const auto first = Generate(false, 1, 2, 4242);
        const auto repeat = Generate(false, 1, 2, 4242);
        Require(first.biomes == repeat.biomes && first.qualityScore == repeat.qualityScore, "site determinism failed");
        std::cout << "PASS settlement-site integration: " << count << " layouts; rectangular sizes and determinism\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
