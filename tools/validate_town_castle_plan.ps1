param(
    [string]$PlanPath = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($PlanPath)) {
    throw "PlanPath is required"
}

$files = @()
if (Test-Path -LiteralPath $PlanPath -PathType Container) {
    $files = @(Get-ChildItem -LiteralPath $PlanPath -Recurse -Filter '*_plan.json' -File)
} elseif (Test-Path -LiteralPath $PlanPath -PathType Leaf) {
    $files = @(Get-Item -LiteralPath $PlanPath)
} else {
    throw "Plan path does not exist: $PlanPath"
}

if ($files.Count -eq 0) {
    throw "No plan JSON files found: $PlanPath"
}

function Test-ContainsRegionCenter {
    param($Parent, $Child)
    # C++ exporter uses integer division, so explicitly floor odd sums here.
    $cx = [int][math]::Floor(($Child.x0 + $Child.x1) / 2.0)
    $cy = [int][math]::Floor(($Child.y0 + $Child.y1) / 2.0)
    return $cx -ge $Parent.x0 -and $cx -le $Parent.x1 -and $cy -ge $Parent.y0 -and $cy -le $Parent.y1
}

$validated = 0
foreach ($file in $files) {
    $plan = Get-Content -Raw -LiteralPath $file.FullName | ConvertFrom-Json
    if ($plan.type -notin @('town', 'village', 'castle')) {
        throw "$($file.Name): unexpected plan type '$($plan.type)'"
    }
    if ([int]$plan.width -le 0 -or [int]$plan.depth -le 0) {
        throw "$($file.Name): invalid dimensions"
    }
    if (@($plan.biomes).Count -ne ([int]$plan.width * [int]$plan.depth)) {
        throw "$($file.Name): biome count does not match dimensions"
    }
    $biomes = @($plan.biomes)
    if ($plan.type -eq 'castle') {
        $defense = @($plan.castleDefenseMask)
        if ($defense.Count -ne $biomes.Count) { throw "$($file.Name): missing castle defense mask" }
        for ($cell = 0; $cell -lt $defense.Count; ++$cell) {
            $intent = [int]$defense[$cell]
            $tile = [int]$biomes[$cell]
            if ($intent -lt 0 -or $intent -gt 6 -or
                ($intent -eq 1 -and $tile -notin @(7,8)) -or
                ($intent -eq 2 -and $tile -notin @(3,4,18)) -or
                ($intent -eq 3 -and $tile -notin @(3,4)) -or
                ($intent -in @(4,5,6) -and $tile -ne 6)) {
                throw "$($file.Name): broken castle defense at cell $cell"
            }
        }
        if ($plan.quality.castleReachableRooms -ne $plan.quality.castleRoomCount -or
            $plan.quality.castleReachableFacilities -ne $plan.quality.castleFacilityCount -or
            $plan.quality.castleDefenseBypassCount -ne 0) {
            throw "$($file.Name): castle rooms, facilities or defensive layers failed validation"
        }
        $wealth = [double]$plan.countryWealth
        $outerDefense = [double]$plan.outerWallDefenseStrength
        $wealthClasses = @('strained', 'ordinary', 'prosperous', 'wealthy', 'opulent')
        if ($wealth -lt 0.7 -or $wealth -gt 2.5 -or [string]$plan.wealthClass -notin $wealthClasses -or
            $outerDefense -lt 0.0 -or $outerDefense -gt 2.0 -or
            [string]$plan.castleDevelopmentModel -ne 'wealth_and_construction_history_v1' -or
            [string]::IsNullOrWhiteSpace([string]$plan.castleWallForm) -or
            [int]$plan.castleDefenseLevel -lt 1 -or [int]$plan.castleDefenseLevel -gt 5) {
            throw "$($file.Name): invalid castle wealth or defense profile"
        }
        $history = @($plan.castleHistory)
        $requiredPhases = @('foundation', 'keep_construction', 'bailey_development')
        if ($history.Count -lt $requiredPhases.Count) { throw "$($file.Name): castle construction history is incomplete" }
        for ($stageIndex = 0; $stageIndex -lt $history.Count; ++$stageIndex) {
            $stage = $history[$stageIndex]
            if (($stageIndex -lt $requiredPhases.Count -and [string]$stage.phase -ne $requiredPhases[$stageIndex]) -or
                [string]::IsNullOrWhiteSpace([string]$stage.change) -or [int]$stage.additions -lt 0 -or
                [double]$stage.condition -lt 0.0 -or [double]$stage.condition -gt 1.0) {
                throw "$($file.Name): invalid castle construction history stage $stageIndex"
            }
        }
        $moatRegions = @($plan.regions | Where-Object { $_.kind -eq 'moat' }).Count
        $wallRegions = @($plan.regions | Where-Object { $_.kind -eq 'curtain_wall' }).Count
        if ($outerDefense -lt 0.5) {
            if ($moatRegions -ne 0 -or $wallRegions -ne 0 -or [int]$plan.quality.castleGateCount -ne 0) {
                throw "$($file.Name): open castle retained outer defenses"
            }
        } elseif ($outerDefense -lt 1.5) {
            if ($moatRegions -ne 0 -or $wallRegions -ne 1 -or [int]$plan.quality.castleGateCount -lt 1) {
                throw "$($file.Name): dry-wall castle has incorrect outer defenses"
            }
        } elseif ($moatRegions -ne 1 -or $wallRegions -ne 1 -or [int]$plan.quality.castleMoatCells -le 0) {
            throw "$($file.Name): moated castle has incorrect outer defenses"
        }
        if ($outerDefense -gt 1.1) {
            if ([int]$plan.quality.castleWallWalkCells -le 0 -or [int]$plan.quality.castleMainGateCount -ne 1 -or
                [int]$plan.quality.castlePosternGateCount -ne ([int]$plan.quality.castleGateCount - 1) -or
                ([math]::Min([int]$plan.width, [int]$plan.depth) -ge 96 -and [int]$plan.quality.castleWatchTowerCount -le 0)) {
                throw "$($file.Name): fortified wall equipment or gate hierarchy is incomplete"
            }
        } elseif ([int]$plan.quality.castleWallWalkCells -ne 0 -or [int]$plan.quality.castleTowerCount -ne 0 -or
            [int]$plan.quality.castleGatehouseCount -ne 0) {
            throw "$($file.Name): low-defense castle retained fortified wall equipment"
        }
        $wallWalkCells = 0
        for ($cell = 0; $cell -lt $biomes.Count; ++$cell) {
            if ([int]$biomes[$cell] -ne 18) { continue }
            ++$wallWalkCells
            if ([int]$defense[$cell] -ne 2) { throw "$($file.Name): wall walk is not on the curtain wall at cell $cell" }
        }
        if ($wallWalkCells -ne [int]$plan.quality.castleWallWalkCells -or
            ($outerDefense -gt 1.1 -and @($plan.markers | Where-Object { $_.kind -eq 'wall_access' }).Count -lt 1)) {
            throw "$($file.Name): wall-top patrol cells or access markers are inconsistent"
        }
        if (($outerDefense -gt 1.1 -and -not @($plan.regions | Where-Object { $_.kind -eq 'wall_walk' })) -or
            ($outerDefense -ge 0.5 -and (@($plan.markers | Where-Object { $_.role -eq 'main_gate' }).Count -ne 1 -or
            @($plan.markers | Where-Object { $_.role -eq 'postern_gate' }).Count -ne ([int]$plan.quality.castleGateCount - 1)))) {
            throw "$($file.Name): castle defense semantics are missing from regions or markers"
        }
    }
    if ($plan.qualityPass -ne $true) {
        throw "$($file.Name): qualityPass is false"
    }
    if ([string]::IsNullOrWhiteSpace([string]$plan.archetype)) {
        throw "$($file.Name): archetype is missing"
    }
    if ($plan.type -eq 'town' -and [string]$plan.cityType -notin @('medieval_city', 'castle_town', 'paved_city', 'walled_city')) {
        throw "$($file.Name): invalid town cityType '$($plan.cityType)'"
    }
    if ($plan.type -eq 'village' -and [string]$plan.cityType -ne 'village') {
        throw "$($file.Name): village cityType must be 'village'"
    }
    if ($plan.type -in @('town', 'village')) {
        if ($biomes -contains 9 -or @($plan.settlementMask).Count -ne $biomes.Count -or
            @($plan.settlementMask | Where-Object { $_ -notin @(0,1) }).Count -gt 0) {
            throw "$($file.Name): blank terrain or invalid development mask"
        }
        if ($null -eq $plan.quality.townDevelopmentReachableRatio -or $plan.quality.townDevelopmentReachableRatio -lt 0.95) {
            throw "$($file.Name): developed land is not 95% reachable from its centre"
        }
        $mapExits = @($plan.markers | Where-Object { $_.kind -eq 'map_exit' })
        if (@($mapExits | ForEach-Object { "$($_.x),$($_.y)" } | Select-Object -Unique).Count -lt 2) {
            throw "$($file.Name): fewer than two distinct map-edge connections"
        }
        foreach ($mapExit in $mapExits) {
            if (($mapExit.x -ne 0 -and $mapExit.y -ne 0 -and $mapExit.x -ne $plan.width-1 -and $mapExit.y -ne $plan.depth-1) -or
                $biomes[$mapExit.y*$plan.width+$mapExit.x] -notin @(5,6)) {
                throw "$($file.Name): map exit does not reach an edge on a road"
            }
        }
        $siteFacilities = @{ plains='barn'; coast='fishery'; mountain_valley='mine_office'; forest='lumbermill'; river='warehouse'; lake='fishery'; oasis='caravanserai' }
        $site = [string]$plan.settlementSite
        if (-not $siteFacilities.ContainsKey($site) -or [string]::IsNullOrWhiteSpace([string]$plan.localEconomy) -or
            [int]$plan.siteRotation -lt 0 -or [int]$plan.siteRotation -gt 3) {
            throw "$($file.Name): missing or invalid settlement site"
        }
        if (-not @($plan.regions | Where-Object { $_.kind -in @('building','market') -and $_.role -eq $siteFacilities[$site] })) {
            throw "$($file.Name): site economy has no matching facility"
        }
        $siteTiles = @{ coast=15; mountain_valley=16; forest=17; river=7; lake=8; oasis=8 }
        if ($siteTiles.ContainsKey($site) -and $biomes -notcontains $siteTiles[$site]) {
            throw "$($file.Name): site-defining terrain is missing"
        }
        $terrainProcesses = @('substrate_and_relief', 'catchment_incision', 'alluvial_deposition',
            'connected_inundation', 'moisture_and_woodland', 'terrain_guided_clearing')
        if ($plan.terrainModel -ne 'catchment_and_settlement_v2' -or @($plan.terrainHistory).Count -ne $terrainProcesses.Count) {
            throw "$($file.Name): missing terrain history model"
        }
        for ($processIndex = 0; $processIndex -lt $terrainProcesses.Count; ++$processIndex) {
            $record = $plan.terrainHistory[$processIndex]
            if ($record.process -ne $terrainProcesses[$processIndex] -or $null -eq $record.affectedCells -or
                $record.affectedCells -lt 0 -or $record.affectedCells -gt $biomes.Count) {
                throw "$($file.Name): invalid terrain process $processIndex"
            }
        }
        if ($plan.terrainHistory[0].affectedCells -ne $biomes.Count -or $plan.terrainHistory[1].affectedCells -le 0 -or
            $plan.terrainHistory[5].affectedCells -le 0) {
            throw "$($file.Name): terrain relief, incision or clearing did not run"
        }
        if ([string]$plan.pavingModel -ne 'activity_corridors_v2') {
            throw "$($file.Name): expected activity corridor paving metadata"
        }
        $expectedStages = @('avenue_frontage', 'waterfront', 'large_building_forecourt', 'normal_road_frontage', 'dense_housing', 'block_infill')
        if (@($plan.pavingStages).Count -ne $expectedStages.Count) {
            throw "$($file.Name): missing paving development stages"
        }
        $addedPavement = 0
        for ($stageIndex = 0; $stageIndex -lt $expectedStages.Count; ++$stageIndex) {
            $stage = $plan.pavingStages[$stageIndex]
            if ($stage.role -ne $expectedStages[$stageIndex] -or $stage.priority -ne ($stageIndex + 1) -or
                $stage.cells -lt 0 -or $stage.projects -lt 0 -or $stage.accessCells -lt 0 -or $stage.accessCells -gt $stage.cells) {
                throw "$($file.Name): invalid paving stage $stageIndex"
            }
            $addedPavement += [int]$stage.cells
        }
        if ($addedPavement -gt [int]$plan.quality.townPavedCells -or
            ([double]$plan.pavingTargetRatio -eq 0.0 -and $addedPavement -ne 0)) {
            throw "$($file.Name): paving stage totals disagree with the map or zero budget"
        }
        if ([string]::IsNullOrWhiteSpace([string]$plan.plazaStyle)) {
            throw "$($file.Name): plazaStyle is missing"
        }
        if ([double]$plan.pavingTargetRatio -lt 0.0 -or [double]$plan.pavingTargetRatio -gt 1.0 -or
            [double]$plan.pavingActualRatio -lt 0.0 -or [double]$plan.pavingActualRatio -gt 1.0) {
            throw "$($file.Name): paving ratios must be within [0, 1]"
        }
        $hasPlazaRegion = @($plan.regions | Where-Object { [string]$_.kind -eq 'plaza' }).Count -gt 0
        if ([bool]$plan.plazaEnabled -and -not $hasPlazaRegion) {
            throw "$($file.Name): plazaEnabled is true but no plaza region exists"
        }
    }

    $regions = @($plan.regions)
    for ($regionIndex = 0; $regionIndex -lt $regions.Count; ++$regionIndex) {
        $region = $regions[$regionIndex]
        if ($region.x0 -lt 0 -or $region.y0 -lt 0 -or $region.x1 -ge $plan.width -or $region.y1 -ge $plan.depth -or $region.x0 -gt $region.x1 -or $region.y0 -gt $region.y1) {
            throw "$($file.Name): region $regionIndex is out of bounds"
        }
        if ([int]$region.parentIndex -ge 0) {
            if ([int]$region.parentIndex -ge $regions.Count -or [int]$region.parentIndex -eq $regionIndex) {
                throw "$($file.Name): invalid parentIndex on region $regionIndex"
            }
            if (-not (Test-ContainsRegionCenter $regions[[int]$region.parentIndex] $region)) {
                throw "$($file.Name): parentIndex does not contain region $regionIndex"
            }
        }
    }

    for ($regionIndex = 0; $regionIndex -lt $regions.Count; ++$regionIndex) {
        $seen = @{}
        $current = $regionIndex
        while ([int]$regions[$current].parentIndex -ge 0) {
            if ($seen.ContainsKey($current)) {
                throw "$($file.Name): region parent cycle detected at region $regionIndex"
            }
            $seen[$current] = $true
            $current = [int]$regions[$current].parentIndex
            if ($current -lt 0 -or $current -ge $regions.Count) {
                throw "$($file.Name): parent chain escapes region list from region $regionIndex"
            }
        }
    }

    $markers = @($plan.markers)
    foreach ($region in $regions) {
        if ([string]::IsNullOrWhiteSpace([string]$region.role)) {
            throw "$($file.Name): region '$($region.kind)' is missing a semantic role"
        }
    }
    foreach ($marker in $markers) {
        if ([string]::IsNullOrWhiteSpace([string]$marker.role)) {
            throw "$($file.Name): marker '$($marker.kind)' is missing a semantic role"
        }
        $biome = [int]$biomes[[int]$marker.y * [int]$plan.width + [int]$marker.x]
        if (@('entrance', 'gate', 'water_crossing', 'building_door', 'room_door', 'service_entry', 'stairs') -contains [string]$marker.kind -and $biome -ne 6) {
            throw "$($file.Name): marker '$($marker.kind)' at ($($marker.x),$($marker.y)) is not on a bridge/door biome"
        }
    }
    $roles = @($regions | ForEach-Object { [string]$_.role }) + @($markers | ForEach-Object { [string]$_.role })
    $requiredRoles = if ($plan.type -in @('town', 'village')) {
        @('town_center', 'town_entrance', 'settlement_footprint', 'district', 'public_entry')
    } else {
        $castleRoles = @('fortress_footprint', 'keep')
        if ($outerDefense -ge 0.5) { $castleRoles += @('curtain_wall', 'main_gate', 'main_entrance') }
        if ($outerDefense -ge 1.5) { $castleRoles += @('moat', 'main_gate_bridge') }
        if ($outerDefense -gt 1.1) { $castleRoles += 'great_gatehouse' }
        $castleRoles
    }
    foreach ($requiredRole in $requiredRoles) {
        if ($roles -notcontains $requiredRole) {
            throw "$($file.Name): required semantic role '$requiredRole' is missing"
        }
    }
    if ($plan.type -eq 'town' -and [string]$plan.cityType -eq 'castle_town') {
        foreach ($requiredRole in @('castle_courtyard', 'castle_entrance')) {
            if ($roles -notcontains $requiredRole) {
                throw "$($file.Name): castle town role '$requiredRole' is missing"
            }
        }
    }

    foreach ($marker in $markers) {
        if ($marker.x -lt 0 -or $marker.y -lt 0 -or $marker.x -ge $plan.width -or $marker.y -ge $plan.depth) {
            throw "$($file.Name): marker '$($marker.kind)' is out of bounds"
        }
        if ([int]$marker.regionIndex -ge 0) {
            if ([int]$marker.regionIndex -ge $regions.Count) {
                throw "$($file.Name): marker '$($marker.kind)' has invalid regionIndex"
            }
            if ([int]$marker.regionId -ne [int]$regions[[int]$marker.regionIndex].id) {
                throw "$($file.Name): marker '$($marker.kind)' regionId does not match regionIndex"
            }
        }
    }
    ++$validated
}

"PLAN_VALIDATION: PASS files=$validated"
