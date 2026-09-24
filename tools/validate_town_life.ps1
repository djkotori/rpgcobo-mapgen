param(
    [Parameter(Mandatory = $true)]
    [string]$PlanPath
)

$ErrorActionPreference = "Stop"

if (Test-Path -LiteralPath $PlanPath -PathType Container) {
    $files = @(Get-ChildItem -LiteralPath $PlanPath -Recurse -Filter '*_plan.json' -File)
} elseif (Test-Path -LiteralPath $PlanPath -PathType Leaf) {
    $files = @(Get-Item -LiteralPath $PlanPath)
} else {
    throw "Plan path does not exist: $PlanPath"
}
if ($files.Count -eq 0) { throw "No plan JSON files found: $PlanPath" }

function Get-ChildFurniture {
    param($Regions, [int]$ParentId)
    @($Regions | Where-Object { $_.kind -eq 'furniture' -and [int]$_.parentId -eq $ParentId })
}

$validated = 0
foreach ($file in $files) {
    $plan = Get-Content -Raw -LiteralPath $file.FullName | ConvertFrom-Json
    if ($plan.type -notin @('town', 'village')) { continue }
    $regions = @($plan.regions)
    $buildings = @($regions | Where-Object { $_.kind -in @('building', 'market') })
    $byId = @{}
    foreach ($region in $buildings) { $byId[[int]$region.id] = $region }

    foreach ($building in $buildings) {
        if ([int]$building.householdId -lt 0 -or [string]::IsNullOrWhiteSpace([string]$building.residentProfile)) {
            throw "$($file.Name): building $($building.id) has no resident profile"
        }
        $furniture = @(Get-ChildFurniture $regions ([int]$building.id))
        $furnitureRoles = @($furniture | ForEach-Object { [string]$_.role })
        $isLiving = [string]$building.livingArrangement -in @('integrated', 'separate_residence')
        if ($isLiving -and ($furnitureRoles -notcontains 'furniture_bed')) {
            throw "$($file.Name): living building $($building.id) has no bed"
        }
        if ([string]$building.waterSource -ne 'none' -and
            -not @($furnitureRoles | Where-Object { $_ -like 'furniture_water_*' })) {
            throw "$($file.Name): building $($building.id) declares water source '$($building.waterSource)' without a water fixture"
        }
        if ([string]$building.hearth -ne 'none' -and
            -not @($furnitureRoles | Where-Object { $_ -in @('furniture_hearth', 'furniture_forge') })) {
            throw "$($file.Name): building $($building.id) declares hearth '$($building.hearth)' without a hearth/forge"
        }
        if ([string]$building.livingArrangement -eq 'business_only' -and [int]$building.residentCount -gt 0) {
            $linkedId = [int]$building.linkedRegionId
            if ($linkedId -lt 0 -or -not $byId.ContainsKey($linkedId)) {
                throw "$($file.Name): business building $($building.id) has no linked residence"
            }
            $residence = $byId[$linkedId]
            if ([int]$residence.householdId -ne [int]$building.householdId -or
                [string]$residence.livingArrangement -ne 'separate_residence') {
                throw "$($file.Name): linked residence for building $($building.id) does not match its household"
            }
        }
    }

    foreach ($road in @($regions | Where-Object { $_.kind -eq 'road' })) {
        if ([int]$road.width -lt 2 -or [int]$road.width -gt 6) {
            throw "$($file.Name): road $($road.id) has invalid width $($road.width)"
        }
        if ([string]::IsNullOrWhiteSpace([string]$road.role)) {
            throw "$($file.Name): road $($road.id) has no reason"
        }
    }
    ++$validated
}

if ($validated -eq 0) { throw "No town or village plans were validated: $PlanPath" }

"LIFE_VALIDATION: PASS files=$validated"
