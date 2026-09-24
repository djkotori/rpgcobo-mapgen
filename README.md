# AIMapImageGen

> [日本語版 README はこちら](README.ja.md)

A command-line tool for procedurally generating map images for 2D RPGs.  
Outputs top-view PNG maps in six types: world, dungeon, cave, town, village, and castle, with quality metrics.

---

## Table of Contents

1. [Overview](#overview)
2. [Folder Structure](#folder-structure)
3. [Requirements](#requirements)
4. [Building](#building)
5. [Usage](#usage)
6. [Batch Files](#batch-files)
7. [Extending the Code](#extending-the-code)

---

## Overview

Map types available:

| Type | Description | Default Size |
|------|-------------|--------------|
| `world` | Field / world map | 256×256 |
| `dungeon` | Indoor dungeon (rooms + corridors) | 160×160 |
| `cave` | Cave dungeon (natural terrain) | 160×160 |
| `town` | Town (entrances, streets, plaza, buildings, parks) | 160×160 |
| `village` | Rural village (winding paths, cottages, fields, ponds) | 160×160 |
| `castle` | Castle (moat, walls, gate, towers, keep, halls) | 160×160 |

Each pixel corresponds to one block (one character tile), and biomes are rendered as solid colors.  
The output images are intended to be further processed into heightmaps or texture maps for a game.

## Sample Output

![WorldMap](out_world.png)
![CaveMap](out_cave.png)
![DungeonMap](out_dungeon.png)
![TownMap](out_town.png)
![VillageMap](out_village.png)
![CastleMap](out_castle.png)

---

## Folder Structure

```
AIMapImageGen/
├── mapimggen.exe        ← executable copied here after build
├── bat/                 ← batch files (build & bulk generation)
│   ├── build.bat
│   ├── generate_30_maps.bat
│   ├── generate_30_dungeon_160.bat
│   ├── generate_30_town_160.bat
│   ├── generate_30_village_160.bat
│   ├── generate_30_castle_160.bat
│   └── ...
├── build/               ← CMake build output (auto-generated)
├── doc/                 ← spec documents (generation algorithm details)
│   ├── mapgen_world.md
│   ├── mapgen_dungeon.md
│   └── mapgen_cave.md
│   ├── mapgen_town.md
│   ├── mapgen_village.md
│   └── mapgen_castle.md
├── sample/              ← sample images
│   ├── worldmap/
│   ├── dungeon/
│   └── cave/
└── src/                 ← C++ source code
    ├── CMakeLists.txt
    ├── main.cpp
    ├── MapImageGenerator.h
    ├── MapPlan.h
    ├── MapQuality.{h,cpp}
    ├── WorldMapImageGenerator.{h,cpp}
    ├── DungeonMapImageGenerator.{h,cpp}
    ├── CaveMapImageGenerator.{h,cpp}
    ├── TownMapImageGenerator.{h,cpp}
    ├── VillageMapImageGenerator.{h,cpp}
    ├── CastleMapImageGenerator.{h,cpp}
    └── utils.{h,cpp}
```

> Batch files are kept in `/bat` and build artifacts in `/build`.  
> After a successful build, `bat/build.bat` automatically copies `mapimggen.exe` to the root folder.

---

## Requirements

| Software | Version | Notes |
|---|---|---|
| **Visual Studio** | 2019 or later recommended | Install the "Desktop development with C++" workload |
| **MSVC (Visual C++)** | C++17 or later | Included with Visual Studio |
| **CMake** | 3.16 or later | Download from [cmake.org](https://cmake.org/download/) or add via the Visual Studio Installer |

### Verifying the Installation

```powershell
cmake --version
cl
```

If both commands are found, you are ready to build.  
If `cl` is not found, run from the **x64 Native Tools Command Prompt for VS**.

---

## Building

### Option 1: Using the batch file (recommended)

```bat
bat\build.bat
```

This batch file automatically:

1. Configures CMake with `src/` as the source and `/build` as the output directory
2. Builds in Release configuration
3. Copies `build\Release\mapimggen.exe` to the root folder

### Option 2: Manual build

```bat
cd src
cmake -S . -B ..\build -DCMAKE_BUILD_TYPE=Release
cmake --build ..\build --config Release
copy ..\build\Release\mapimggen.exe ..
```

---

## Usage

```
mapimggen.exe -type <world|dungeon|cave|town|village|castle> [options]
```

### Options

| Option | Description | Default |
|---|---|---|
| `-type <world\|dungeon\|cave\|town\|village\|castle>` | Map type | `world` |
| `-w <width>` | Width in pixels. Range: 64–1024 | world:256 / dungeon,cave,town,village,castle:160 |
| `-d <depth>` | Height in pixels. Range: 64–1024 | world:256 / dungeon,cave,town,village,castle:160 |
| `-seed <number>` | Random seed (for reproducibility) | Random |
| `-dir <directory>` | Output directory | `.` (current directory) |
| `-out <filename>` | Output filename (no extension) | `out` |
| `--png` | Write a PNG image | Enabled |
| `--data` | Write a biome CSV file | Disabled |
| `--report` | Write a statistics report | Disabled |
| `--plan` | Write biome data plus regions/markers as JSON | Disabled |
| `--strict` | Exit with code 7 when quality validation fails | Disabled |
| `-p <key=value>` | Override a specific generation parameter | — |

### Examples

```bat
REM World map (256x256, fixed seed)
mapimggen.exe -type world -w 256 -d 256 -seed 12345 -dir out -out world_001 --png --report

REM Dungeon (160x160, with CSV data)
mapimggen.exe -type dungeon -w 160 -d 160 -dir out -out dungeon_001 --png --data

REM Cave (default size, PNG only)
mapimggen.exe -type cave -dir out -out cave_001 --png

REM Town (structured plan output)
mapimggen.exe -type town -seed 12345 -dir out -out town_001 --png --report --plan

REM Village (rural settlement with furniture regions)
mapimggen.exe -type village -seed 12345 -dir out -out village_001 --png --report --plan --strict

REM Coastal town / forest village (use the newly built executable)
build\Release\mapimggen.exe -type town -w 160 -d 160 -p settlementSite=1 -dir out -out coastal_town --png --plan --strict
build\Release\mapimggen.exe -type village -w 160 -d 160 -p settlementSite=3 -dir out -out forest_village --png --plan --strict

REM Castle (fail CI on a quality warning)
mapimggen.exe -type castle -seed 12345 -dir out -out castle_001 --png --report --plan --strict
```

### Output Files

| Flag | Example filename | Content |
|---|---|---|
| `--png` | `world_001.png` | Biome color-coded image |
| `--data` | `world_001.csv` | 2D array of biome IDs |
| `--report` | `world_001_report.txt` | Biome statistics summary |
| `--plan` | `world_001_plan.json` | Biome array, archetype, regions/markers, quality result, and semantic links |

---

## Batch Files

| File | Description |
|---|---|
| bat/generate_30_all.bat | Evaluate all six types, 30 seeds each, with PNG/CSV/Plan/report output |
| `bat/build.bat` | Build and copy the exe to the root |
| `bat/generate_30_maps.bat` | Generate 30 world maps in bulk (256×256) |
| `bat/generate_30_maps_128.bat` | Generate 30 world maps (128×128) |
| `bat/generate_30_maps_512.bat` | Generate 30 world maps (512×512) |
| `bat/generate_30_dungeon_128.bat` | Generate 30 dungeons (128×128) |
| `bat/generate_30_dungeon_160.bat` | Generate 30 dungeons (160×160) |
| `bat/generate_30_dungeon_256.bat` | Generate 30 dungeons (256×256) |
| `bat/generate_30_cave_160.bat` | Generate 30 caves (160×160) |
| `bat/generate_30_town_160.bat` | Generate 30 towns with plans/reports (160×160) |
| `bat/generate_30_village_160.bat` | Generate 30 villages with plans/reports (160×160) |
| `bat/generate_settlement_sites_160.bat [seed]` | Generate paired towns/villages at all 7 sites (14 images) |
| `bat/generate_30_castle_160.bat` | Generate 30 castles with strict quality checks (160×160) |

| Script | Description |
|---|---|
| `tools/evaluate_town_castle.ps1` | Evaluate 30 town/village/castle seeds, all archetype coverage, quality gates, archetype distribution, plan diversity between seeds, and same-seed reproducibility. |
| `tools/evaluate_town_castle_sizes.ps1` | Strictly generate a 64–1024 size matrix to check scale adaptation. |
| `tools/validate_town_castle_plan.ps1` | Validate plan dimensions, coordinates, marker-to-region links, semantic roles, door/bridge tile consistency, and region parent/child relationships. |

Results are saved to `out/<timestamp>/`.

---

## Extending the Code

### Adding a New Map Type

The existing spec documents (`doc/mapgen_world.md` → `doc/mapgen_dungeon.md` → `doc/mapgen_cave.md` → `doc/mapgen_town.md` → `doc/mapgen_castle.md`) are real examples of how each type was derived from the previous one.
Follow these steps to add a new map type.

#### 1. Write a spec document

Create `doc/mapgen_<newtype>.md` describing the generation algorithm, biomes, and target terrain.  
Use an existing spec (especially `mapgen_dungeon.md`) as a template and focus on describing only the differences.

#### 2. Generate code with GitHub Copilot

Once the spec is ready, open Copilot Chat in VS Code (agent mode) and prompt it like this:

```
Following the spec in doc/mapgen_<newtype>.md, implement a <NewType>MapImageGenerator class
that inherits from MapImageGenerator.
Place the result in src/<NewType>MapImageGenerator.h and src/<NewType>MapImageGenerator.cpp.
Use the existing DungeonMapImageGenerator as a reference.
```

> Providing the existing spec documents and implementation files as context helps Copilot understand the project structure and produce better code.

#### 3. Base class interface

```cpp
// src/MapImageGenerator.h
class MapImageGenerator {
public:
    virtual ~MapImageGenerator() = default;
    virtual bool Generate(MapPlan& outPlan, std::string& outError) = 0;
};
```

Any new class only needs to implement `Generate()` to integrate with `main.cpp`.

#### 4. Register the new type in main.cpp

Add the new type to the `MapTypes()` registry in `src/main.cpp`:

```cpp
#include "<NewType>MapImageGenerator.h"

{"<newtype>", 160, 160, [](MapGenParams p) {
    return std::unique_ptr<MapImageGenerator>(
        std::make_unique<NewTypeMapImageGenerator>(std::move(p)));
}}
```

#### 5. Add the source file to CMakeLists.txt

```cmake
add_executable(mapimggen
    main.cpp
    <NewType>MapImageGenerator.cpp
    ...
)
```

#### Tips for spec-driven development with Copilot

- List the target terrain characteristics as bullet points in the spec — this helps Copilot choose appropriate parameter values.
- Placing sample images in `sample/` alongside the spec gives Copilot visual context and improves output quality.
- If the generated result does not match expectations, refine the relevant section of the spec and ask Copilot to regenerate — iterating on the spec is more effective than patching generated code directly.

---


## License

This project is released under the [MIT License](LICENSE).

You are free to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of this software.
