param(
    [string]$Exe = "",
    [string]$OutputRoot = "",
    [int]$SeedCount = 3,
    [int[]]$Sizes = @(64, 96, 128, 160, 192, 256, 384, 512, 768, 1024)
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $repoRoot "build\Release\mapimggen.exe"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "tmp\town_castle_size_quality"
}
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "mapimggen executable was not found: $Exe"
}
if ($SeedCount -lt 1) {
    throw "SeedCount must be at least 1"
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$summary = @()
foreach ($type in @("town", "village", "castle")) {
    foreach ($size in $Sizes) {
        if ($size -lt 64) {
            throw "Size $size is below the minimum supported town/castle size"
        }
        $directory = Join-Path $OutputRoot ("{0}_{1}" -f $type, $size)
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
        for ($seed = 1; $seed -le $SeedCount; ++$seed) {
            $name = "seed$seed"
            & $Exe -type $type -w $size -d $size -seed $seed -dir $directory -out $name --report --plan --strict *> (Join-Path $directory "$name.log")
            if ($LASTEXITCODE -ne 0) {
                throw "$type size $size seed $seed failed. See $(Join-Path $directory "$name.log")"
            }
            $reportPath = Join-Path $directory "${name}_report.txt"
            $report = Get-Content -Raw -LiteralPath $reportPath
            if (-not $report.Contains("QUALITY_PASS=true")) {
                throw "$type size $size seed $seed did not report QUALITY_PASS=true"
            }
            $getNumber = {
                param([string]$key)
                $line = $report -split "`r?`n" | Where-Object { $_ -like "$key=*" } | Select-Object -First 1
                if ($null -eq $line) { return 0.0 }
                return [double]$line.Substring($key.Length + 1)
            }
            $summary += [pscustomobject]@{
                type = $type
                size = $size
                seed = $seed
                archetype = (($report -split "`r?`n" | Where-Object { $_ -like "archetype=*" } | Select-Object -First 1).Substring(10))
                planHash = (Get-FileHash -LiteralPath (Join-Path $directory "${name}_plan.json") -Algorithm SHA256).Hash
                buildings = & $getNumber "townBuildingCount"
                facilities = & $getNumber "castleFacilityCount"
                rooms = & $getNumber "castleRoomCount"
                emptyCourtyard = & $getNumber "castleCourtyardEmptyRatio"
            }
        }
    }
}

& (Join-Path $PSScriptRoot "validate_town_castle_plan.ps1") -PlanPath $OutputRoot
$summary | Group-Object type, size | ForEach-Object {
    $group = $_.Group
    $uniqueArchetypes = @($group.archetype | Select-Object -Unique).Count
    if ($SeedCount -ge 8 -and $group[0].type -ne "village" -and $uniqueArchetypes -lt 2) {
        throw "$($group[0].type) size $($group[0].size) has insufficient archetype variation: $uniqueArchetypes"
    }
    [pscustomobject]@{
        type = $group[0].type
        size = $group[0].size
        samples = $group.Count
        uniquePlans = @($group.planHash | Select-Object -Unique).Count
        uniqueArchetypes = $uniqueArchetypes
        archetypes = (($group | Group-Object archetype | ForEach-Object { "$($_.Name):$($_.Count)" }) -join ",")
        minBuildings = ($group.buildings | Measure-Object -Minimum).Minimum
        maxBuildings = ($group.buildings | Measure-Object -Maximum).Maximum
        minFacilities = ($group.facilities | Measure-Object -Minimum).Minimum
        maxFacilities = ($group.facilities | Measure-Object -Maximum).Maximum
        maxEmptyCourtyard = ($group.emptyCourtyard | Measure-Object -Maximum).Maximum
    }
} | Sort-Object type, size | Format-Table -AutoSize

Write-Output "SIZE_QUALITY: PASS samples=$($summary.Count)"
