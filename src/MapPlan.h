#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct MapPlanMarker {
    std::string kind;
    int x = 0;
    int y = 0;
    int id = -1;
    // Optional semantic linkage for downstream editors. Existing generators
    // keep using id for backwards compatibility; exporters may resolve it.
    int regionId = -1;
    int regionIndex = -1;
    std::string role;
};

struct MapPlanRegion {
    std::string kind;
    int id = -1;
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    int parentId = -1;
    int parentIndex = -1;
    std::string role;
    // Settlement building metadata. Empty/default values keep non-building
    // regions compatible while allowing consumers to understand who lives or
    // works there and why its interior contains particular facilities.
    int householdId = -1;
    std::string residentProfile;
    int residentCount = 0;
    int workerCount = 0;
    std::string livingArrangement;
    std::string waterSource;
    std::string hearth;
    int linkedRegionId = -1;
    int width = 0;
};

struct MapPlanPavingStage {
    std::string role;
    int projects = 0;
    int cells = 0;
    int accessCells = 0;
};

struct MapPlanTerrainStage {
    std::string process;
    int affectedCells = 0;
};

struct MapPlanCastleHistoryStage {
    std::string phase;
    std::string change;
    int additions = 0;
    double condition = 1.0;
};

struct MapPlan {
    int width = 0;
    int depth = 0;
    std::uint32_t seed = 0;
    std::string type;
    std::string archetype;
    std::string cityType;
    std::string settlementSite;
    int siteRotation = 0;
    std::string localEconomy;
    std::string terrainModel;
    std::vector<MapPlanTerrainStage> terrainHistory;
    double countryWealth = 1.0;
    std::string wealthClass;
    double outerWallDefenseStrength = 1.0;
    std::string castleWallForm;
    int castleDefenseLevel = 0;
    std::string castleDevelopmentModel;
    std::vector<MapPlanCastleHistoryStage> castleHistory;
    bool plazaEnabled = false;
    std::string plazaStyle;
    double pavingTargetRatio = 0.0;
    double pavingActualRatio = 0.0;
    std::string pavingModel;
    std::vector<MapPlanPavingStage> pavingStages;
    std::string environment;
    std::vector<std::uint8_t> biomes;
    // Town/village development area, independent of the full-canvas terrain.
    std::vector<std::uint8_t> settlementMask;
    // Castle construction intent, kept independently of the final tiles so
    // validation can detect eroded walls and accidental moat crossings.
    // 0: other, 1: moat, 2: curtain wall, 3: inner wall,
    // 4: authorized moat crossing, 5: outer gate, 6: inner gate.
    std::vector<std::uint8_t> castleDefenseMask;
    std::vector<MapPlanMarker> markers;
    std::vector<MapPlanRegion> regions;
    double qualityScore = 0.0;
    std::string selectionReason;

    void Reset(int w, int d, std::uint32_t s, const std::string& mapType) {
        width = w;
        depth = d;
        seed = s;
        type = mapType;
        archetype.clear();
        cityType.clear();
        settlementSite.clear();
        siteRotation = 0;
        localEconomy.clear();
        terrainModel.clear();
        terrainHistory.clear();
        countryWealth = 1.0;
        wealthClass.clear();
        outerWallDefenseStrength = 1.0;
        castleWallForm.clear();
        castleDefenseLevel = 0;
        castleDevelopmentModel.clear();
        castleHistory.clear();
        plazaEnabled = false;
        plazaStyle.clear();
        pavingTargetRatio = 0.0;
        pavingActualRatio = 0.0;
        pavingModel.clear();
        pavingStages.clear();
        environment.clear();
        biomes.clear();
        settlementMask.clear();
        castleDefenseMask.clear();
        markers.clear();
        regions.clear();
        qualityScore = 0.0;
        selectionReason.clear();
    }
};
