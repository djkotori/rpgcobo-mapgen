# Village map specification

`village` is the rural settlement generator. It is intentionally separate from
`town`: its roads are narrower and more irregular, its footprint follows a
smaller terrain-guided clearing, and its buildings are smaller and more dispersed.
Village shops and workshops normally include the owner's living space, unlike business-only
urban shops which may be linked to a separate house.

The entire requested width and height is terrain, with no black background or
blank border. Unsettled surroundings remain meadow, sand, forest, rock or water.
The `settlementMask` separates the smaller rural development area from the
full-canvas `biomes`; village density and default 0% paving are unchanged.

## Generation

The shared settlement planner selects a site (`plains`, `coast`, `mountain_valley`,
`forest`, `river`, `lake`, or `oasis`) once per seed. Woodland and oasis sites
default to forest ground and sand; other sites default to meadow. It generates:

1. substrate and relief, incised valleys, deposits, connected water, moisture
   and woodland, followed by a dry terrain-guided clearing;
2. a common area and two-to-three entrances;
3. purposeful winding paths from entrances to the common area and from houses
   or businesses to the existing network;
4. a small set of cottages, houses, barns, workshops, shrines, and an inn;
5. resident profiles first, followed by planned parks and village furniture;
6. connected doors, regions, markers, and a strict quality report.

Village buildings use different profiles rather than a single rectangle size.
Each site reserves an economic facility near its resources: a barn, fishery,
mine office, lumbermill, riverside warehouse, or caravanserai. Natural sea,
cliffs and dense trees constrain roads/buildings and are exported separately
from background, house walls and ordinary green ground. Mountain entrances
use the valley ends; woodland approaches use preserved openings in the forest.
Each settlement entrance also connects across dry countryside to a `map_exit`
on the image edge. These exterior roads do not enlarge the building/paving budget.

Lakes occupy submerged branching valleys; oasis pools occupy old wadi channels
and retain sediment shoulders. They are not elliptical cutouts. Optional ponds
are remnant depressions generated before clearing, not rectangular decorations
inserted after houses. Water/rock geometry is shared with towns of the same
seed, site, orientation, dimensions and water settings; only development extent
and retained forest change. `terrainModel` and `terrainHistory` record the actual
formation stages and affected cell counts (see `mapgen_town.md` section 6).

The v2 terrain model generates the drainage skeleton itself from the seed:
trunk controls, branch counts, confluences, lengths, widths, basin positions and
oasis axes vary. Coasts vary both the regional shoreline and river-mouth shapes.
Tributaries attach to the smoothed trunk; sediment follows the generated channels.
Resource facilities search actual dry frontage rather than a fixed resource position.
Holding `siteRotation=0` still produces different silhouettes across seeds.
Oasis vegetation is also patch-based: alluvial soil and seeded low-frequency
grove noise select clusters, the immediate water edge stays open for water
collection, and a sparse seed receives at most a small best-location grove
instead of a uniform green outline around the pool.

Building interiors are exported with floor tiles and furniture regions such as
`bed`, `table`, `counter`, `altar`, `bench`, `shelf`, `hearth`, `forge`, and
`water_barrel`. Building regions also record the household profile, population,
living arrangement, water source, hearth, and worker count.

## Parameters

- `settlementSite`: `0=plains`, `1=coast`, `2=mountain_valley`, `3=forest`,
  `4=river`, `5=lake`, `6=oasis`; omitted values are selected once from the seed.
- `siteRotation`: `0..3`, clockwise quarter turns, seed-selected when omitted.
- `townTerrain`: `0=meadow`, `1=arid`, `2=woodland`; overrides the site's ground.
- `buildingCountMin`, `buildingCountMax`: rural building budget.
- `districtCountMin`, `districtCountMax`: path district budget.
- `waterFeatureChance`: pond/stream chance.
- `candidateCount`: deterministic candidate count.
- `pavingRatio`: defaults to `0`. Outdoor ground stays unpaved unless explicitly
  overridden; roads and building floors keep their separate semantic tiles.
  An override uses the activity corridor projects documented in `mapgen_town.md`.
- `mainRoadWidth`, `districtRoadWidth`, `alleyWidth`: shared road classes;
  defaults are `6`, `4`, and `2` tiles.

## Quality targets

- all doors and entrances are reachable;
- at least 95% of developed walkable land is reachable from the village centre;
- no background cells exist, and at least two distinct road exits reach image edges;
- the path network is one connected component;
- at least four buildings and two districts exist;
- building regions have semantic roles and furniture child regions;
- resident profiles are created before interiors, and integrated shop/farm
  households expose their living equipment;
- road regions explain why each connection exists and record its tile width;
- the plan passes `--strict` and `tools/validate_town_castle_plan.ps1`.

Run `bat/generate_settlement_sites_160.bat` for paired town/village previews of
all seven sites. See `mapgen_town.md` section 3.2 for site metadata and rules.
The full-map walkable component may include disconnected uninhabited land across
water; it is reported separately from the developed-area connectivity gate.

For shape comparisons run `bat/generate_settlement_diversity_160.bat [first_seed]`:
it produces 48 town/village previews across coast, lake and oasis, with eight
seeds per case and the same orientation. The first seed defaults to 1.
