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
