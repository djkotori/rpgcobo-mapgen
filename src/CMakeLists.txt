cmake_minimum_required(VERSION 3.16)
project(mapimggen LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_executable(mapimggen
    main.cpp
    MapQuality.cpp
    CaveMapImageGenerator.cpp
    ForestMapImageGenerator.cpp
    CastleMapImageGenerator.cpp
    DungeonMapImageGenerator.cpp
    TownMapImageGenerator.cpp
    TownPaving.cpp
    SettlementSite.cpp
    VillageMapImageGenerator.cpp
    WorldMapImageGenerator.cpp
    WorldBiomes.cpp
    WorldBridges.cpp
    utils.cpp
)

target_include_directories(mapimggen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(mapimggen PRIVATE /W4 /permissive-)
else()
    target_compile_options(mapimggen PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(world_biome_tests ../tests/WorldBiomeTests.cpp WorldMapImageGenerator.cpp WorldBiomes.cpp WorldBridges.cpp MapQuality.cpp utils.cpp)
    target_include_directories(world_biome_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME world_biomes COMMAND world_biome_tests)
    add_executable(forest_map_tests ../tests/ForestMapImageGeneratorTests.cpp ForestMapImageGenerator.cpp MapQuality.cpp utils.cpp)
    target_include_directories(forest_map_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME forest_map COMMAND forest_map_tests)
    add_executable(settlement_civic_tests ../tests/SettlementCivicTests.cpp CastleMapImageGenerator.cpp
        VillageMapImageGenerator.cpp TownMapImageGenerator.cpp SettlementSite.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(settlement_civic_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME settlement_civic COMMAND settlement_civic_tests)
    add_executable(town_growth_tests ../tests/TownGrowthTests.cpp VillageMapImageGenerator.cpp
        TownMapImageGenerator.cpp SettlementSite.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(town_growth_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME town_growth COMMAND town_growth_tests)
    add_executable(settlement_decoration_tests ../tests/SettlementDecorationTests.cpp)
    target_include_directories(settlement_decoration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME settlement_decoration COMMAND settlement_decoration_tests)
    add_executable(settlement_relief_tests ../tests/SettlementReliefTests.cpp)
    target_include_directories(settlement_relief_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME settlement_relief COMMAND settlement_relief_tests)
    add_executable(village_growth_tests ../tests/VillageGrowthTests.cpp VillageMapImageGenerator.cpp
        TownMapImageGenerator.cpp SettlementSite.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(village_growth_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME village_growth COMMAND village_growth_tests)
    add_executable(asset_foundation_tests ../tests/AssetFoundationTests.cpp)
    target_include_directories(asset_foundation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME asset_foundation COMMAND asset_foundation_tests)
    add_executable(town_paving_tests ../tests/TownPavingTests.cpp TownPaving.cpp)
    target_include_directories(town_paving_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME town_paving COMMAND town_paving_tests)
    add_executable(settlement_site_tests ../tests/SettlementSiteTests.cpp TownMapImageGenerator.cpp
        VillageMapImageGenerator.cpp SettlementSite.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(settlement_site_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME settlement_sites COMMAND settlement_site_tests)
    add_executable(terrain_history_tests ../tests/TerrainHistoryTests.cpp SettlementSite.cpp)
    target_include_directories(terrain_history_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME terrain_history COMMAND terrain_history_tests)
    add_executable(castle_layout_tests ../tests/CastleLayoutTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_layout_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_layout COMMAND castle_layout_tests)
    add_executable(castle_interior_only_tests ../tests/CastleInteriorOnlyTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_interior_only_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_interior_only COMMAND castle_interior_only_tests)
    add_executable(castle_wall_thickness_tests ../tests/CastleWallThicknessTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_wall_thickness_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_wall_thickness COMMAND castle_wall_thickness_tests)
    add_executable(castle_interior_furnishing_tests ../tests/CastleInteriorFurnishingTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_interior_furnishing_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_interior_furnishing COMMAND castle_interior_furnishing_tests)
    add_executable(castle_outline_variety_tests ../tests/CastleOutlineVarietyTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_outline_variety_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_outline_variety COMMAND castle_outline_variety_tests)
    add_executable(castle_terrain_biome_tests ../tests/CastleTerrainBiomeTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_terrain_biome_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_terrain_biome COMMAND castle_terrain_biome_tests)
    add_executable(castle_service_door_tests ../tests/CastleServiceDoorTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_service_door_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_service_door COMMAND castle_service_door_tests)
    add_executable(castle_corridor_tests ../tests/CastleCorridorTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_corridor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_corridor COMMAND castle_corridor_tests)
    add_executable(castle_window_corridor_tests ../tests/CastleWindowCorridorTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_window_corridor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
    add_test(NAME castle_window_corridor COMMAND castle_window_corridor_tests)
    add_executable(castle_lot_facility_tests ../tests/CastleLotFacilityTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_lot_facility_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
    add_test(NAME castle_lot_facility COMMAND castle_lot_facility_tests)
    add_executable(castle_corner_smooth_tests ../tests/CastleCornerSmoothTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_corner_smooth_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_corner_smooth COMMAND castle_corner_smooth_tests)
    add_executable(castle_nature_width_tests ../tests/CastleNatureWidthTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_nature_width_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_nature_width COMMAND castle_nature_width_tests)
    add_executable(castle_margin_tests ../tests/CastleMarginTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_margin_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_margin COMMAND castle_margin_tests)
    add_executable(castle_axis_variety_tests ../tests/CastleAxisVarietyTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_axis_variety_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_axis_variety COMMAND castle_axis_variety_tests)
    add_executable(castle_marks_tests ../tests/CastleMarksTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_marks_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_marks COMMAND castle_marks_tests)
    add_executable(castle_room_shape_tests ../tests/CastleRoomShapeTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_room_shape_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_room_shape COMMAND castle_room_shape_tests)
    add_executable(castle_size_tests ../tests/CastleSizeTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_size_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_size COMMAND castle_size_tests)
    add_executable(castle_variety_tests ../tests/CastleVarietyTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_variety_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        # VS 2022 Build Tools の CL.exe が、このテストのインライン展開で異常終了する（0xC0000005）。
        target_compile_options(castle_variety_tests PRIVATE /Ob0)
    endif()
    add_test(NAME castle_variety COMMAND castle_variety_tests)
    add_executable(castle_nature_odds_tests ../tests/CastleNatureOddsTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_nature_odds_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_nature_odds COMMAND castle_nature_odds_tests)
    add_executable(castle_road_width_tests ../tests/CastleRoadWidthTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_road_width_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_road_width COMMAND castle_road_width_tests)
    add_executable(quality_policy_tests ../tests/QualityPolicyTests.cpp MapQuality.cpp)
    target_include_directories(quality_policy_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME quality_policy COMMAND quality_policy_tests)
    add_test(NAME castle_cli_interior_only
        COMMAND ${CMAKE_COMMAND}
            -DMAPGEN_EXE=$<TARGET_FILE:mapimggen>
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/castle_cli_interior_only
            -P ${CMAKE_CURRENT_SOURCE_DIR}/../tests/VerifyCastleInteriorOnly.cmake)

    # 単独で約25秒以上かかるテストには "slow" ラベルを付ける（2026年11月19日に Release で計測、直列実行）。
    # 日常の確認は `ctest -LE slow`、コミット前は全件（ラベル指定なし）を実行する。
    # 新しいテストが25秒を超えたら、この一覧に追加する。
    set_tests_properties(
        castle_variety castle_service_door castle_layout castle_terrain_biome
        castle_axis_variety castle_nature_width castle_corner_smooth castle_wall_thickness
        castle_corridor castle_window_corridor castle_lot_facility castle_margin castle_outline_variety castle_cli_interior_only castle_nature_odds
        settlement_sites
        PROPERTIES LABELS slow)

    # 割合・出現頻度・種類数を数える統計的なテストには "statistical" ラベルを付ける。反復数を減らすと合否が変わるため、
    # クイックモードで縮めず、日常用（`ctest -LE statistical`）では実行しない。コミット前のフルテストで実行する。
    set_property(TEST castle_corridor castle_window_corridor castle_lot_facility castle_room_shape castle_axis_variety castle_outline_variety world_biomes castle_nature_odds
        APPEND PROPERTY LABELS statistical)
endif()
