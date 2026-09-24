param(
    [string]$Exe = "",
    [string]$OutputRoot = "",
    [int]$SeedCount = 30
)

$ErrorActionPreference = "Stop"
if ($SeedCount -le 0) {
    throw "SeedCount must be positive: $SeedCount"
}
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $repoRoot "build\Release\mapimggen.exe"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "tmp\town_castle_quality"
}
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "mapimggen executable was not found: $Exe"
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

function Invoke-StrictMap {
    param(
        [string]$Type,
        [int]$Size,
        [int]$Seed,
        [string]$Directory,
        [string]$Name,
        [string[]]$ExtraArgs = @()
    )
    New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    & $Exe -type $Type -w $Size -d $Size -seed $Seed -dir $Directory -out $Name --report --plan --strict @ExtraArgs *> (Join-Path $Directory "$Name.log")
    if ($LASTEXITCODE -ne 0) {
        throw "$Type seed $Seed failed. See $(Join-Path $Directory "$Name.log")"
    }
    return Get-Content -Raw -LiteralPath (Join-Path $Directory "${Name}_report.txt")
}

function Get-ReportNumber {
    param([string]$Report, [string]$Key)
    $line = $Report -split "`r?`n" | Where-Object { $_ -like "$Key=*" } | Select-Object -First 1
    if ($null -eq $line) { return 0.0 }
    return [double]($line.Substring($Key.Length + 1))
}

function Get-ReportText {
    param([string]$Report, [string]$Key)
    $line = $Report -split "`r?`n" | Where-Object { $_ -like "$Key=*" } | Select-Object -First 1
    if ($null -eq $line) { return "" }
    return $line.Substring($Key.Length + 1)
}

$summary = @()
foreach ($typeSpec in @(@("town", 160), @("village", 160), @("castle", 160))) {
    $type = $typeSpec[0]
    $size = [int]$typeSpec[1]
    $typeOutput = Join-Path $OutputRoot $type
    for ($seed = 1; $seed -le $SeedCount; ++$seed) {
        $report = Invoke-StrictMap -Type $type -Size $size -Seed $seed -Directory $typeOutput -Name ("seed{0}" -f $seed)
        $planPath = Join-Path $typeOutput ("seed{0}_plan.json" -f $seed)
        $summary += [pscustomobject]@{
            type = $type
            seed = $seed
            archetype = Get-ReportText $report "archetype"
            planHash = (Get-FileHash -LiteralPath $planPath -Algorithm SHA256).Hash
            buildings = Get-ReportNumber $report "townBuildingCount"
            facilities = Get-ReportNumber $report "castleFacilityCount"
            rooms = Get-ReportNumber $report "castleRoomCount"
            emptyCourtyard = Get-ReportNumber $report "castleCourtyardEmptyRatio"
            markers = Get-ReportNumber $report "reachableMarkers"
        }
    }
}

foreach ($mode in @(0, 2, 3, 4, 5)) {
    Invoke-StrictMap -Type town -Size 160 -Seed (1000 + $mode) -Directory (Join-Path $OutputRoot "town_modes") -Name "mode$mode" -ExtraArgs @("-p", "townMode=$mode") | Out-Null
}
foreach ($mode in 0..4) {
    Invoke-StrictMap -Type castle -Size 160 -Seed (2000 + $mode) -Directory (Join-Path $OutputRoot "castle_modes") -Name "mode$mode" -ExtraArgs @("-p", "castleMode=$mode") | Out-Null
}

Write-Output "DETERMINISM"
$determinismSummary = @()
foreach ($typeSpec in @(@("town", 160), @("village", 160), @("castle", 160))) {
    $type = $typeSpec[0]
    $size = [int]$typeSpec[1]
    $seed = 424242
    $firstDirectory = Join-Path $OutputRoot "determinism\${type}_a"
    $secondDirectory = Join-Path $OutputRoot "determinism\${type}_b"
    Invoke-StrictMap -Type $type -Size $size -Seed $seed -Directory $firstDirectory -Name "sample" | Out-Null
    Invoke-StrictMap -Type $type -Size $size -Seed $seed -Directory $secondDirectory -Name "sample" | Out-Null
    $firstHash = (Get-FileHash -LiteralPath (Join-Path $firstDirectory "sample_plan.json") -Algorithm SHA256).Hash
    $secondHash = (Get-FileHash -LiteralPath (Join-Path $secondDirectory "sample_plan.json") -Algorithm SHA256).Hash
    if ($firstHash -ne $secondHash) {
        throw "$type is not deterministic for seed $seed"
    }
$determinismSummary += [pscustomobject]@{ type = $type; seed = $seed; planHash = $firstHash }
}
$determinismSummary | Format-Table -AutoSize
Write-Output "DETERMINISM: PASS"

$summary | Group-Object type | ForEach-Object {
    $group = $_.Group
    [pscustomobject]@{
        type = $_.Name
        samples = $group.Count
        uniquePlans = @($group.planHash | Select-Object -Unique).Count
        minBuildings = ($group.buildings | Measure-Object -Minimum).Minimum
        maxBuildings = ($group.buildings | Measure-Object -Maximum).Maximum
        minFacilities = ($group.facilities | Measure-Object -Minimum).Minimum
        maxFacilities = ($group.facilities | Measure-Object -Maximum).Maximum
        maxEmptyCourtyard = ($group.emptyCourtyard | Measure-Object -Maximum).Maximum
    }
} | Format-Table -AutoSize

if ($SeedCount -ge 10) {
    foreach ($type in @("town", "village", "castle")) {
        $group = @($summary | Where-Object { $_.type -eq $type })
        $uniqueCount = @($group.planHash | Select-Object -Unique).Count
        $minimumUnique = [math]::Max(2, [int][math]::Ceiling($SeedCount * 0.80))
        if ($uniqueCount -lt $minimumUnique) {
            throw "$type plan diversity is too low: $uniqueCount unique plans out of $SeedCount (minimum $minimumUnique)"
        }
    }
}

Write-Output "ARCHETYPE_DISTRIBUTION"
$summary | Group-Object type, archetype | ForEach-Object {
    [pscustomobject]@{
        type = $_.Group[0].type
        archetype = $_.Group[0].archetype
        samples = $_.Count
    }
} | Sort-Object type, archetype | Format-Table -AutoSize

if ($SeedCount -ge 20) {
    $requiredArchetypes = @{
        town = @("grid", "river", "walled", "market", "organic")
        village = @("village")
        castle = @("concentric", "hill", "river", "palace", "fortress")
    }
    foreach ($type in @("town", "village", "castle")) {
        $observed = @($summary | Where-Object { $_.type -eq $type } | Select-Object -ExpandProperty archetype -Unique)
        $missing = @($requiredArchetypes[$type] | Where-Object { $observed -notcontains $_ })
        if ($missing.Count -gt 0) {
            throw "$type archetype coverage is incomplete: $($missing -join ', ')"
        }
    }
}

Write-Output "QUALITY: PASS"
