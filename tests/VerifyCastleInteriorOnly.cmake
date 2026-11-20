if(NOT DEFINED MAPGEN_EXE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "MAPGEN_EXE and OUTPUT_DIR are required")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
execute_process(
    COMMAND "${MAPGEN_EXE}" -type castle -w 160 -d 160 -seed 4242
        -p assetFoundation=1 -p outerWallDefenseStrength=2 --plan --strict
        -dir "${OUTPUT_DIR}" -out castle_interior
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Interior-only castle CLI failed (${result}): ${stdout}\n${stderr}")
endif()

set(plan_path "${OUTPUT_DIR}/castle_interior_plan.json")
if(NOT EXISTS "${plan_path}")
    message(FATAL_ERROR "CLI did not write ${plan_path}")
endif()
file(READ "${plan_path}" plan)
string(JSON city_type ERROR_VARIABLE json_error GET "${plan}" cityType)
if(json_error OR NOT city_type STREQUAL "")
    message(FATAL_ERROR "Castle CLI output contains a settlement cityType: ${city_type}")
endif()
string(JSON settlement_length ERROR_VARIABLE json_error LENGTH "${plan}" settlementMask)
if(json_error OR NOT settlement_length EQUAL 0)
    message(FATAL_ERROR "Castle CLI output contains settlementMask cells")
endif()
string(REGEX MATCH "\"castleDefenseMask\"[ \t\r\n]*:[ \t\r\n]*\\[[^]]*\\]" defense_property "${plan}")
if(defense_property STREQUAL "")
    message(FATAL_ERROR "Castle CLI output is missing castleDefenseMask")
endif()
string(REGEX MATCH "[1-9]" defense_value "${defense_property}")
if(NOT defense_value STREQUAL "")
    message(FATAL_ERROR "Castle CLI output retains outer-defense mask cells")
endif()

string(JSON region_count LENGTH "${plan}" regions)
set(has_castle_ground FALSE)
math(EXPR region_last "${region_count} - 1")
foreach(index RANGE 0 ${region_last})
    string(JSON kind GET "${plan}" regions ${index} kind)
    if(kind STREQUAL "castle_ground")
        set(has_castle_ground TRUE)
    endif()
    if(kind MATCHES "^(moat|curtain_wall|gatehouse|tower|wall_walk|fortress|outer_bailey|town_footprint)$")
        message(FATAL_ERROR "Castle CLI output contains unwanted region ${kind}")
    endif()
endforeach()
if(NOT has_castle_ground)
    message(FATAL_ERROR "Castle CLI output is missing its neutral castle_ground region")
endif()

string(JSON marker_count LENGTH "${plan}" markers)
if(marker_count GREATER 0)
    math(EXPR marker_last "${marker_count} - 1")
    foreach(index RANGE 0 ${marker_last})
        string(JSON kind GET "${plan}" markers ${index} kind)
        if(kind MATCHES "^(gate|entrance|castle_gate|wall_access|water_crossing)$")
            message(FATAL_ERROR "Castle CLI output contains unwanted marker ${kind}")
        endif()
    endforeach()
endif()
# The default castle canvas is 128x128 when no size is requested.
execute_process(
    COMMAND "${MAPGEN_EXE}" -type castle -seed 42 --plan --strict
        -dir "${OUTPUT_DIR}" -out castle_default_size
    RESULT_VARIABLE default_result
    OUTPUT_VARIABLE default_stdout
    ERROR_VARIABLE default_stderr
)
if(NOT default_result EQUAL 0)
    message(FATAL_ERROR "Default-size castle CLI failed (${default_result}): ${default_stdout}\n${default_stderr}")
endif()
file(READ "${OUTPUT_DIR}/castle_default_size_plan.json" default_plan)
string(JSON default_width GET "${default_plan}" width)
string(JSON default_depth GET "${default_plan}" depth)
if(NOT default_width EQUAL 128 OR NOT default_depth EQUAL 128)
    message(FATAL_ERROR "Default castle size is ${default_width}x${default_depth}, expected 128x128")
endif()

message(STATUS "Castle CLI dispatched directly to the interior generator and passed strict quality")
