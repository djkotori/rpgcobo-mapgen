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
    CastleMapImageGenerator.cpp
    DungeonMapImageGenerator.cpp
    TownMapImageGenerator.cpp
    TownPaving.cpp
    SettlementSite.cpp
    VillageMapImageGenerator.cpp
    WorldMapImageGenerator.cpp
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
    add_executable(settlement_civic_tests ../tests/SettlementCivicTests.cpp CastleMapImageGenerator.cpp
        VillageMapImageGenerator.cpp TownMapImageGenerator.cpp SettlementSite.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(settlement_civic_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME settlement_civic COMMAND settlement_civic_tests)
    add_executable(castle_interior_tests ../tests/CastleInteriorTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_interior_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_interior COMMAND castle_interior_tests)
    add_executable(castle_settlement_tests ../tests/CastleSettlementTests.cpp CastleMapImageGenerator.cpp TownPaving.cpp MapQuality.cpp)
    target_include_directories(castle_settlement_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME castle_settlement COMMAND castle_settlement_tests)
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
endif()
