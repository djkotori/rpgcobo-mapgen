#pragma once

#include "MapPlan.h"

#include <string>
#include <vector>

struct MapQualityMetrics {
    int totalCells = 0;
    int walkableCells = 0;
    int largestWalkableComponent = 0;
    int walkableComponents = 0;
    int deadEndCells = 0;
    int oneCellCorridorCells = 0;
    int doorCells = 0;
    int dryLandCells = 0;
    int largestDryLandRegion = 0;
    int dryLandRegions = 0;
    int waterCells = 0;
    int waterComponents = 0;
    int roadCells = 0;
    int largestRoadComponent = 0;
    int roadComponents = 0;
    int roadEndpointCells = 0;
    int roadJunctionCells = 0;
    int townBuildingCount = 0;
    int townDistrictCount = 0;
    int townLandmarkCount = 0;
    int townEntranceCount = 0;
    int townCenterMarkerCount = 0;
    int townBuildingDoorCount = 0;
    int townReachableBuildingDoors = 0;
    int townDevelopedWalkableCells = 0;
    int townReachableDevelopedCells = 0;
    int townPlazaCells = 0;
    int townBuildingCells = 0;
    int townPavedCells = 0;
    int townBuildingKindCount = 0;
    int townInvalidRoadWidths = 0;
    int townParkCells = 0;
    int townWaterFeatureCells = 0;
    int townInvalidBuildingDoors = 0;
    int townInvalidEntrances = 0;
    int invalidSemanticMarkers = 0;
    int castleTowerCount = 0;
    int castleGateCount = 0;
    int castleWatchTowerCount = 0;
    int castleMainGateCount = 0;
    int castlePosternGateCount = 0;
    int castleWallWalkCells = 0;
    int castleRoomCount = 0;
    int castleFacilityCount = 0;
    int castleReachableRooms = 0;
    int castleReachableFacilities = 0;
    int castleGatehouseCount = 0;
    int castleGardenCells = 0;
    int castleTrainingGroundCells = 0;
    int castleInvalidRoomDoors = 0;
    int castleInvalidGateMarkers = 0;
    int castleInvalidWaterCrossings = 0;
    int castleMoatCells = 0;
    int castleMoatGapCells = 0;
    int castleWallGapCells = 0;
    int castleInnerGateCount = 0;
    int castleDefenseBypassCount = 0;
    int castleInvalidFacilityDoors = 0;
    int castleFacilityRoleCount = 0;
    int castleProgramMissingRoles = 0;
    int castleFootprintCells = 0;
    int castleKeepFootprintCells = 0;
    int castleHallCells = 0;
    int castleKitchenCells = 0;
    int castleEquipmentCount = 0;
    int castleCourtyardEmptyCells = 0;
    int castleCourtyardCells = 0;
    double castleMoatCoverage = 0.0;
    double castleCourtyardEmptyRatio = 0.0;
    double castleKeepCenterOffset = 0.0;
    int markerCount = 0;
    int reachableMarkers = 0;
    double walkableRatio = 0.0;
    double landRatio = 0.0;
    double largestWalkableComponentRatio = 0.0;
    double floorRatio = 0.0;
    double corridorRatio = 0.0;
    double deadEndRatio = 0.0;
    double dryLandRatio = 0.0;
    double largestDryLandRegionRatio = 0.0;
    double waterRatio = 0.0;
    double roadRatio = 0.0;
    double largestRoadComponentRatio = 0.0;
    double townPlazaRatio = 0.0;
    double townDevelopmentReachableRatio = 0.0;
    double townRoadRatio = 0.0;
    double townBuildingRatio = 0.0;
    double townPavingRatio = 0.0;
    double townPavingTargetDelta = 0.0;
    double townParkWaterRatio = 0.0;
    std::vector<std::string> issues;

    bool Passed() const { return issues.empty(); }
};

MapQualityMetrics EvaluateMapQuality(const MapPlan& plan);
