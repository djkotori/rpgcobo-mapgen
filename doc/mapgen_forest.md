# Forest Map Image Generator

## Goal and output

Generate a top-down, retro 2D RPG forest map as a PNG. Forest generation runs at one-quarter of the intended final resolution: the default 160x160 map is generated as 40x40 pixels. This generator does not enlarge or resample the result; scaling to the final resolution is a separate downstream step. Each generated pixel has exactly one biome color. The first generation stage builds a connected orthogonal maze from reusable cell shapes:

- `FOREST` fills the map and is impassable.
- `DEEPFOREST` replaces forest at least `deepForestDistance` pixels (default 5) away from any non-forest biome and is impassable.
- `ROAD` forms the three-pixel-wide walkable paths and internal spurs inside and between maze cells.
- `INNERROAD` is the one-pixel centerline painted over `ROAD`, including corners, junctions, dead ends, and internal spurs.
- `PLAIN` forms clearing/lake footprints and widened spaces beside cliffs.
- `OPEN` forms clearing interiors and two-pixel banks around `LAKE`.
- `RIVER` crosses straight paths at `BRIDGE` tiles.
- `CLIFF` occupies eligible northern areas without blocking the path network.

Paths are painted as `ROAD` first, then their one-pixel `INNERROAD` centerlines are overlaid. The total path width remains three pixels. Clearing/lake footprints are painted afterward with higher priority, replacing both road biomes inside their footprints with `PLAIN`, `OPEN`, or the variant's interior biome. Widened cliff-junction spaces replace `ROAD` with `PLAIN` but preserve existing `INNERROAD` pixels. Rivers replace both road biomes with `BRIDGE` at crossings. Region copies and edge extension preserve centerlines; margin selection and quality checks treat both biomes as road. All sizes below are measured at the generation resolution, before any external zoom.

## Cell grid

Cell generation uses an interior canvas inset by two pixels on all four sides: a requested 40x40 output uses a 36x36 cell canvas. For square maps, complexity 1–5 produces a 1x1 through 5x5 grid. The default complexity is 3. Cell pixel bounds use integer partitions of the interior, so leftover pixels are distributed across cells (the default 36-pixel interior divided by three produces 12-pixel cells).

For non-square maps, cell count is adjusted along the longer axis to keep cell dimensions close to square. The target cell size is approximately `min(width - 4, height - 4) / complexity`; for example, a 53x40 output uses a 49x36 interior and, at complexity 3, 4 columns by 3 rows. Each axis is capped to retain at least five pixels per cell, including the minimum 32x32 output at complexity 5.

Each cell's base tile is determined by its open sides:

| Open sides | Cell type |
|---|---|
| One | Dead end |
| Two opposite | Straight |
| Two adjacent | L-shaped corner |
| Three | T-junction |
| Four | Crossroads |
| None | Blank (no ports; placed only when the remaining path network stays connected) |

Paths are rasterized as filled, axis-aligned corridor arms with a fixed width of 3 generated pixels. All arms use the center of the corresponding cell edge, so adjacent cells meet on a straight, centered line. Cell metadata records the grid position, base tile type, orientation, four-bit port mask (`N=1`, `E=2`, `S=4`, `W=8`), and optional spur direction.

Each connected side is drawn as a straight, full-width arm toward the cell center. The arms are clipped to the cell bounds and combined directly; no extra rectangle is added around the center to bridge offset differences.

Eligible straight and L-shaped cells can be rendered with one additional short dead-end spur inside the cell. T-junctions can also receive one spur in their unused direction, creating a four-armed shape with one arm ending inside the cell. The spur is a local visual variation only: it never changes the cell's port mask or opens a connection into a neighboring cell.

## Maze generation and invariants

1. A seed-driven randomized spanning-tree traversal connects every cell to the grid. A small number of extra matching edges may then create loops.
2. The cell's open-side mask is derived from those graph edges. It is not selected independently from its neighbors.
3. Optional blank cells are selected only when removing that cell leaves all remaining nonblank cells connected. Entrance cells are protected from blanking. If the requested blank rate cannot be reached without isolating part of the maze, the generator uses fewer blank cells. Blank cells have no walkable ports, but may later contain river-only or cliff-only terrain.
4. Every internal port must have the reciprocal port on the adjacent cell. Boundary ports are allowed only where an entrance is placed.
5. All nonblank cells belong to the same connected path network. A blank cell has no ports and no corridor. Terrain variants preserve these graph connections, the three-pixel-wide boundary ports, and entrance reachability.
6. One to three entrances are placed on map edges (two by default). Each entrance opens a port from its boundary cell to the map edge and receives an `entrance` metadata marker.

Generation is deterministic for the same dimensions, parameters, and seed. The maze is orthogonal; no diagonal or pixel-level noise is used.

## Cell terrain variants

Variants are applied after the base maze and internal dead-end spurs. Clearing/lake selection runs first, rivers second, and cliffs last. An active cell receives at most one terrain variant. Smaller cells shrink room geometry to fit inside the cell, leaving a one-pixel forest border except at declared path ports; a lake is omitted if even a one-pixel lake with its two-pixel bank cannot fit. A cliff is omitted if its northern band has no room above the three-pixel junction clearance.

### Clearings and lakes

`clearingRate` is the combined clearing/lake density, not two independent probabilities. The target count is `columns * rows * clearingRate`, stochastically rounded to an integer and capped by available active cells. With defaults, a 3x3 grid selects one or two cells. Selection is without replacement; dead-end cells have twice the selection weight of other active cells. All base path shapes, including corners, are eligible.

Each selected cell becomes a lake with probability `lakeRate`, otherwise a clearing. A centered square `PLAIN` footprint has side length 8-12 pixels. Dead ends use smaller sizes (8-9); junctions tend toward larger sizes (T: 9-11, cross: 10-12). A clearing contains a square `OPEN` interior of 5-8 pixels; a lake contains a square `LAKE` interior of 4-7 pixels. Interior sizes also tend to increase with port count. Cell size and bank/margin requirements can reduce these sizes.

The interior is randomly anchored at the center or one of four corners within the footprint. A clearing leaves at least one pixel of `PLAIN` around its interior. A centered lake retains two pixels of `OPEN` on all four sides. A corner lake sits flush with that corner of the footprint and has two-pixel `OPEN` banks only on the two inward sides: northwest lakes have east/south banks, northeast lakes west/south banks, southwest lakes east/north banks, and southeast lakes west/north banks. A lake corner is eligible only when neither of its outward sides has a path port; selection is uniform among the center and eligible corners. Thus a northwest lake is never selected when the cell connects north or west. The bank stays within the footprint and the remaining walkable area connects all path ports.

### Rivers

An undecorated straight cell without an internal spur is eligible with probability `riverRate`. The river is a straight, two-pixel-wide strip perpendicular to the path, spanning the cell. Its intersection with the three-pixel path is a 2x3 `BRIDGE`; bridges are walkable. In either direction, the river continues through consecutive blank cells until reaching the map edge or another active cell. Blank-cell river intersections merge as water and do not create walking connections. Rivers do not overwrite other active cells.

### Cliffs

An undecorated active cell is eligible only when it is on the north edge, or every cell in its column between it and that edge is blank. River-only blank cells exclude cliff placement. A northern cliff band stops three pixels before the cell's central junction; existing paths, including north-edge entrance paths, remain intact. After cliff propagation finishes, each horizontal run of cliff cells selects exactly one path-plus-cliff cell, uniformly using the map seed, for a small, up-to-5x5 `PLAIN` space around its junction. Cliff-only cells join the run but are not selection candidates. Other path-plus-cliff cells retain the original path width and spurs without extra widening. Runs without any path cell receive no space. A `cliff` variant's `objectVariant` is `1` for the selected space and `0` otherwise.

All blank cells north of the selected cell become cliff-only terrain. Remaining undecorated blank cells are also converted whenever a cliff cell exists immediately to their left, right, or south. Both path-plus-cliff and cliff-only cells act as sources. This deterministic propagation uses the previous pass's placement and repeats until no cells change, allowing horizontal expansion followed by further northward expansion and renewed horizontal expansion. River-only cells are never replaced, and no new cliff seeds are created by propagation when `cliffRate=0`.

Cliff placement is finalized for the whole grid before rasterization. Path-plus-cliff cells have a three-pixel horizontal `FOREST` inset on each side without an adjacent cliff cell, including map edges; sides shared with cliff cells remain connected. For very narrow cells the inset is reduced to retain at least one column of cliff footprint. These side insets are resolved before eight-neighbor joins, so a northern cliff cannot undo an exposed side inset.

The renderer examines all eight neighboring cells' actual cliff footprints, including the inset cliff bands in path cells. Cliff-only cells retain a three-pixel `FOREST` inset toward exposed non-cliff neighbors (12 pixels after external 4x zoom), but adjoining cliff footprints are joined without a forest seam. Joining takes priority over cliff-only insets where neighboring demands overlap, including diagonal corners and small cells. Existing walkable pixels are never overwritten. The base cliff probability is `cliffRate`; with `n` blank cells to the north, it becomes `1 - (1 - cliffRate)^(n + 1)`, favoring larger northern blank areas. River and clearing/lake variants take precedence over cliffs.

After all cells and terrain variants are rendered, a separate image pass scans each column from south to north. Every cliff pixel fills consecutive `FOREST` pixels immediately above it with `CLIFF`, up to the map edge or the first other biome. This removes lower side bulges and north-facing recesses across cell boundaries. Paths, clearings, bridges, rivers, and lakes stop the fill and are never overwritten. This final pass can fill an earlier side inset if a cliff farther south projects into that column. Cell variant regions continue to describe their original rendering footprints; the biome grid contains the final northward-filled silhouette.

### Plan metadata

Base `forest_cell` regions keep their existing ports, roles and spur metadata. Additional `forest_variant` regions reference their owning cell ID in `parentId`. Roles are `clearing`, `lake`, `river`, `river_only`, `cliff`, or `cliff_only`. Clearing/lake bounds describe the inner square and `width` records the enclosing `PLAIN` footprint size; `rotation` records the anchor (`0` center, `1` northwest, `2` northeast, `3` southwest, `4` southeast), not an angle. River bounds describe the two-pixel strip; cliff bounds describe the northern band or blank-cell extent. A blank cell with perpendicular rivers has one region per axis.

## Meandering image copies

After terrain rendering and the northward cliff-fill pass, `meanderRate` selects a target fraction of grid cells for shifted region copies (default `0.5`, targeting 4-5 cells in a 3x3 grid). The target is stochastically rounded; at most one copy is applied per active cell. Blank cells are not selected. Cells without clearings/lakes are shuffled and tried before those with clearings/lakes.

Each ordinary source starts as a random rectangle spanning two-thirds to all of the cell's width and height, with a minimum of five pixels per side. Before copying, its bounds are repeatedly expanded wherever they touch a road cross-section of up to three pixels, until the entire cross-section and a one-pixel margin on both sides fit. Roads crossing the rectangle along their length do not force expansion along that length. Expansion can cross cell boundaries; candidates that cannot fit the image or avoid other copies are rejected. This prevents partial-width copies from leaving the old road beside the shifted road and widening it.

The source is copied opaquely from the completed, unmodified terrain image and pasted with independently selected X/Y offsets in `-1, 0, +1`, excluding `(0,0)`. The original source is **not erased**: only the destination rectangle is overwritten, including its forest pixels. A source containing a clearing/lake must include the complete square footprint and a one-pixel outer margin, as well as the road margins above. Ordinary copies may not overwrite clearing `PLAIN` or `OPEN` with another biome.

The union of a source and its destination may cross cell boundaries but cannot overlap any earlier copy's union. Copies are kept inside the image, preserve all map-edge pixels and entrance positions, and are rejected if the resulting walkable image is disconnected. Each cell has up to 16 candidate attempts; the actual fraction can be lower than requested when geometry, interference, or connectivity prevents safe placement. `meanderRate=0` disables this pass exactly, without consuming additional random values. Existing terrain and cell metadata describe the pre-copy geometry; the biome grid is the final result.

Each accepted copy adds a `forest_meander` region: `parentId` is the source cell ID, bounds are the source rectangle, and `doorX`/`doorY` encode the signed X/Y offsets for replay against the pre-copy biome image.

## Edge extension

Immediately after meandering, the interior canvas is centered in the requested output with a two-pixel offset. Each outer pixel repeats the nearest interior edge pixel (clamped X/Y coordinates); corners repeat the corresponding interior corner. This is edge extension, not image scaling. No extra random values are consumed.

All region bounds, including terrain variants and meander source rectangles, are translated by `(2,2)` into output coordinates. Cell grid indices, IDs, widths, and signed meander offsets remain unchanged. Entrance markers move to the actual output edges, with straight three-pixel-wide road extensions connecting them to the inset cell ports. The output remains exactly the requested width and height.

## Deep forest post-process

After edge extension and the naturalization filter below, the completed output image is classified using the shortest four-neighbor (Manhattan) distance to any biome other than `FOREST`. Forest pixels at distance **`deepForestDistance` or greater** become `DEEPFOREST`; the default is 5, so distance 4 remains `FOREST`. All non-forest biomes, including water and cliffs, act as distance sources. Pixels outside the image are treated as forest, so image borders do not reduce the distance. Classification uses the pre-conversion image, not newly generated deep forest pixels. This deterministic pass consumes no random values and does not change paths, water, cliffs, or metadata. It also runs when `meanderRate=0`. Forest shading at extended edges can therefore differ from the interior edge after this final classification.

## Naturalization filter

The final terrain filter runs **after edge extension, immediately before DEEPFOREST classification**. It makes one row-major pass over the output, reading only a frozen pre-filter biome grid. All eight neighbors must exist; outermost pixels are unchanged. Metadata continues to describe the pre-filter shapes. `ROAD`, `INNERROAD`, `BRIDGE`, and `RIVER` are never replaced.

A one-pixel convex corner consists of the center and three neighbors forming a same-biome 2x2 square, with all five remaining neighbors sharing one different biome. All four orientations are supported. Rounding replaces the center with that surrounding biome:

`INNERROAD` is never a replacement target: a corner surrounded by centerline pixels is left unchanged. This prevents cliff-junction `PLAIN` spaces from thickening L/T centerlines. Existing `INNERROAD` pixels are also unchanged by the filter.

| Center biome | Rule | Probability |
|---|---|---:|
| `PLAIN` | Round a convex corner unless any of the eight neighbors is `CLIFF`. | 0.7 |
| `OPEN` | Round a convex corner unless any of the eight neighbors is `CLIFF`. | 0.7 |
| `LAKE` | Round a convex corner unless any of the eight neighbors is `FOREST` or `CLIFF`. | 1.0 |
| `FOREST` | Replace with `CLIFF` when at least five of the eight neighbors are `CLIFF`. No corner pattern required. | 1.0 |
| `CLIFF` | Round a convex corner only when the five outside neighbors are all `FOREST`. | 1.0 |

PLAIN/OPEN decisions use the seed-driven generator only for eligible corners, preserving reproducibility. Changes do not cascade within the pass. Filtering may modify the extended cliff border and rounded room/lake bounds; deep forest distances are computed from the resulting image, not the rectangular source geometry.

## Parameters

Parameters are supplied through `MapGenParams::extra`:

| Name | Default | Description |
|---|---:|---|
| `complexity` | `3` | Square-grid dimension; clamped to 1–5. |
| `entranceCount` | `2` | Number of map-edge entrances; clamped to 1–3. |
| `blankCellRate` | Random `0.05`–`0.20` | Requested fraction of cells to leave blank, from `0.0` to `1.0`. The actual rate can be lower to preserve connectivity. |
| `deadEndRate` | Random `0.25`–`0.65` | Dead-end tendency from `0.0` to `1.0`. Higher values preserve more dead ends; lower values add loops from dead-end cells to neighboring cells. |
| `deadEndBranchRate` | `0.20` | Probability from `0.0` to `1.0` of adding an internal dead-end spur to each eligible straight, L-shaped, or T-junction cell. |
| `clearingRate` | `0.20` | Combined clearing/lake target density per grid cell; dead ends have twice the selection weight. |
| `lakeRate` | `0.50` | Lake selection probability within clearing/lake cells. `0` means clearings only; `1` means lakes only. |
| `riverRate` | `0.50` | River probability per eligible undecorated straight cell without a spur. |
| `cliffRate` | `0.50` | Base cliff probability per eligible undecorated northern cell; boosted by northern blank cells. |
| `meanderRate` | `0.50` | Target fraction of grid cells receiving a one-pixel shifted opaque region copy; constrained by interference and connectivity. |
| `deepForestDistance` | `5` | Minimum Manhattan distance from non-forest terrain, in generated pixels. Requires a finite value; clamped to 1 through width + height, then rounded to the nearest integer. |

If `blankCellRate` or `deadEndRate` is omitted, its default is independently randomized within the documented range using the map seed. Thus generation remains reproducible for the same seed and parameters. Rates supplied outside `0.0`–`1.0` are clamped. Dead-end rates are constrained by grid topology, blank cells, and entrance locations, so the resulting percentage is approximate rather than an exact quota.

The terrain-variant and meandering rates have fixed defaults, require finite numbers, and are clamped to `0.0`-`1.0`. Set `clearingRate=0`, `riverRate=0`, `cliffRate=0`, and `meanderRate=0` to obtain the unmodified base maze. CLI example:

```bat
mapimggen.exe -type forest -seed 12345 -p clearingRate=0.2 -p lakeRate=0.5 -p riverRate=0.5 -p cliffRate=0.5 --plan --strict
```

Maps must be between 32 and 4096 generated pixels in each dimension and large enough to retain at least five pixels in the smallest cell dimension. The default generated size is 40x40; no zooming is performed by `ForestMapImageGenerator`.

## Biome colors

| Biome | ID | Color (RGB) | Traversal |
|---|---:|---|---|
| `PLAIN` | 0 | `(151, 196, 91)` | Passable |
| `OPEN` | 1 | `(239, 239, 223)` | Passable |
| `FOREST` | 2 | `(28, 83, 43)` | Impassable |
| `DEEPFOREST` | 3 | `(17, 54, 29)` | Impassable |
| `CLIFF` | 4 | `(114, 78, 51)` | Impassable |
| `ROAD` | 5 | `(154, 116, 76)` | Passable |
| `INNERROAD` | 21 | `(190, 156, 100)` | Passable |
| `BRIDGE` | 6 | `(176, 153, 114)` | Passable |
| `RIVER` | 7 | `(66, 122, 190)` | Impassable |
| `LAKE` | 8 | `(80, 148, 196)` | Impassable |

## Validation expectations

Forest-only tests verify deterministic output, square and aspect-adjusted grids, valid base and spur tile types, reciprocal neighbor ports and centered corridor geometry, non-connecting spur geometry, entrances for every boundary port, reachability of every nonblank cell and entrance, blank-cell and dead-end rates, spur-rate endpoints, and the 1–3 entrance limit.

Variant tests additionally check clearing/lake density and selection endpoints, interior sizes and anchors, centered/corner lake banks and forbidden corner directions, river eligibility and 2x3 bridges, river-only/cliff-only blank cells, northern cliff eligibility, exposed path-cliff side insets, multi-pass horizontal/northern cliff propagation with river protection, seam-free eight-neighbor cliff joins (including path/blank pairs), finite/clamped rates, palette coverage, and pixel-level connectivity across multiple resolutions, complexities, and seeds.

Base-geometry tests disable meandering. Room and meandering tests reconstruct rectangular room shapes and validate the permitted naturalization results after opaque-copy replay. They check zero/default rates, deterministic output, one-pixel offsets, the target-count bound, non-interference, whole-room source coverage, cell-edge crossings, unchanged road entrances, and preserved reachability across multiple sizes, complexities, and seeds.

Deep forest tests compare the final biome grid against an independent local Manhattan-distance search at thresholds 4, 5, and 7, including threshold boundaries, diagonal cases, image-edge deep forest, and maps with and without meandering. They also verify the default, finite-value validation, clamping, and rounding. Copy replay ignores forest/deep-forest shading, which is recomputed after copying.

Inset tests verify interior cell partitions, translated region bounds, edge/corner repetition allowing subsequent cliff filtering and deep forest shading, unchanged output dimensions, and entrance extensions and reachability across square/rectangular outputs and all complexities.

Filter tests enumerate all binary eight-neighbor patterns for each pair of supported biomes, test all corner orientations and protection rules, measure 0.7/1.0 rounding probabilities, and verify snapshot-based cliff filtering and unchanged image-edge pixels.
