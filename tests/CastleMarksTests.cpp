#include "CastleMapImageGenerator.h"
#include "CastleMarks.h"

#include <iostream>
#include <stdexcept>
#include <string>

// The marks: the throne's cells and the antechamber's bench cells are painted in the map image, nothing else is
// changed, and the map's biomes are not touched. The editor reads that image: it turns each pixel into the biome of
// the nearest colour of its palette, so a mark must be a colour that the editor decodes as the same biome as the
// floor under it (the mark is only for people), whatever else it looks like.
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
bool Same(const mapgen::Rgb& a, const mapgen::Rgb& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// The editor's castle palette (castleColors in MapGenerator.sk, indexed by biome ID): keep it in step with it.
const int kEditorPalette[19][3] = {
    {128, 137, 116}, {190, 170, 112}, {76, 116, 72}, {126, 129, 136}, {76, 78, 87}, {150, 128, 94}, {211, 175, 72}, {46, 126, 198},
    {36, 78, 155}, {18, 22, 30}, {172, 153, 113}, {158, 119, 80}, {177, 177, 172}, {89, 61, 45}, {160, 156, 139},
    {38, 103, 155}, {100, 103, 113}, {25, 69, 42}, {139, 142, 145}};
// The biome ID the editor reads from a colour (the nearest palette colour), and how much closer it is than the next.
int Decode(const mapgen::Rgb& c, double* ratio = nullptr) {
    int best = -1;
    long long bestD = 0, secondD = 0;
    for (int i = 0; i < 19; ++i) {
        const long long dr = c.r - kEditorPalette[i][0], dg = c.g - kEditorPalette[i][1], db = c.b - kEditorPalette[i][2];
        const long long d = dr * dr + dg * dg + db * db;
        if (best < 0 || d < bestD) { secondD = bestD; bestD = d; best = i; }
        else if (secondD == 0 || d < secondD) secondD = d;
    }
    if (ratio != nullptr) *ratio = bestD == 0 ? 1e9 : static_cast<double>(secondD) / static_cast<double>(bestD);
    return best;
}
}  // namespace

int main() {
    try {
        int castles = 0, thrones = 0, benches = 0, corridorCells = 0;
        for (const int mode : {0, 1, 2, 3, 4}) for (unsigned seed = 1; seed <= 10; ++seed) {
            MapGenParams params;
            params.width = params.depth = 128;
            params.seed = seed;
            params.extra = {{"castleMode", mode}, {"assetFoundation", 1}};
            CastleMapImageGenerator generator(params);
            MapPlan plan;
            std::string error;
            const std::string context = "mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
            Require(generator.Generate(plan, error), context + ": " + error);
            const std::vector<std::uint8_t> biomesBefore = plan.biomes;
            std::vector<mapgen::Rgb> base;
            for (const std::uint8_t b : plan.biomes) { const MapGenColor c = generator.BiomeToColor(b); base.push_back({c.r, c.g, c.b}); }
            std::vector<mapgen::Rgb> marked = base;
            castle_marks::Paint(plan, marked);
            Require(plan.biomes == biomesBefore, context + ": the biomes changed");
            ++castles;
            // What the editor reads is unchanged: every pixel of the marked picture decodes to the biome of the map.
            for (std::size_t i = 0; i < base.size(); ++i) {
                Require(Decode(base[i]) == plan.biomes[i], context + ": the editor palette does not decode the plain picture to the map's biomes");
                Require(Decode(marked[i]) == plan.biomes[i], context + ": a mark is read by the editor as another biome (cell " + std::to_string(i) + ")");
            }
            // The floor of a corridor is a little darker than the other floors (and still read as the stone floor, as every
            // pixel is checked above): the stone-floor cells of a corridor room carry its colour.
            std::vector<bool> corridorFloor(base.size(), false);
            for (const auto& room : plan.regions) {
                if (room.kind != "room" || room.role != "corridor") continue;
                for (int y = room.y0; y <= room.y1; ++y) for (int x = room.x0; x <= room.x1; ++x) {
                    const std::size_t i = static_cast<std::size_t>(y * plan.width + x);
                    if (plan.biomes[i] != TOWN_STONE_FLOOR) continue;
                    ++corridorCells;
                    corridorFloor[i] = true;
                    Require(Same(marked[i], castle_marks::kCorridorFloor), context + ": a corridor floor cell has the wrong colour");
                }
            }
            std::vector<bool> expected(base.size(), false);
            for (const auto& item : plan.regions) {
                if (item.kind != "castle_fixture" || (item.role != "royal_throne" && item.role != "waiting_bench")) continue;
                (item.role == "royal_throne" ? thrones : benches) += 1;
                for (int y = item.y0; y <= item.y1; ++y) for (int x = item.x0; x <= item.x1; ++x) {
                    const std::size_t i = static_cast<std::size_t>(y * plan.width + x);
                    expected[i] = true;
                    Require(plan.biomes[i] != TOWN_STONE_FLOOR || Same(marked[i], castle_marks::kMark), context + ": a throne or bench cell is not marked");
                }
            }
            // A throne room or an antechamber without its furniture (the throne, the benches) is marked by a 3 x 3 square in its middle.
            for (const auto& room : plan.regions) {
                if (room.kind != "room" || (room.role != "throne_room" && room.role != "antechamber")) continue;
                bool furnished = false;
                for (const auto& item : plan.regions)
                    furnished |= item.kind == "castle_fixture" && item.parentId == room.id && item.role == (room.role == "throne_room" ? "royal_throne" : "waiting_bench");
                if (furnished) continue;
                const int cx = (room.x0 + room.x1) / 2, cy = (room.y0 + room.y1) / 2;
                for (int y = cy - 1; y <= cy + 1; ++y) for (int x = cx - 1; x <= cx + 1; ++x)
                    if (x >= 0 && y >= 0 && x < plan.width && y < plan.depth) expected[static_cast<std::size_t>(y * plan.width + x)] = true;
            }
            // Only marks were added: every other cell keeps its colour (the fallback marks of unfurnished rooms are the exception).
            bool unfurnished = false;
            for (const auto& h : plan.castleHistory) unfurnished |= h.change.rfind("unfurnished", 0) == 0;
            if (!unfurnished) for (std::size_t i = 0; i < base.size(); ++i)
                if (!expected[i] && !corridorFloor[i]) Require(Same(marked[i], base[i]), context + ": a cell that is not a throne, a bench or a corridor floor was changed");
        }
        Require(thrones > 0 && benches > 0, "no throne or bench was generated to check");
        // The mark's colour sits well inside the stone floor's share of the palette (not on the edge of another colour's).
        double ratio = 0;
        Require(Decode(castle_marks::kMark, &ratio) == 12, "the mark is not read as the stone floor");
        Require(ratio >= 1.25, "the mark is too close to another palette colour: " + std::to_string(ratio));
        Require(!Same(castle_marks::kMark, {177, 177, 172}), "the mark does not differ from the stone floor");
        // The corridor floor: darker than the stone floor, but not by much, and read as the stone floor with room to spare.
        const auto sum = [](const mapgen::Rgb& c) { return static_cast<int>(c.r) + c.g + c.b; };
        double corridorRatio = 0;
        Require(Decode(castle_marks::kCorridorFloor, &corridorRatio) == 12, "the corridor floor is not read as the stone floor");
        Require(corridorRatio >= 2.0, "the corridor floor is too close to another palace colour: " + std::to_string(corridorRatio));
        Require(sum(castle_marks::kCorridorFloor) <= sum({177, 177, 172}) - 15 && sum(castle_marks::kCorridorFloor) >= sum({177, 177, 172}) - 40,
            "the corridor floor is not slightly darker than the stone floor");
        Require(corridorCells > 0, "no corridor floor was checked");
        std::cout << "CASTLE_MARKS: OK castles=" << castles << " thrones=" << thrones << " benches=" << benches << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_MARKS: FAIL " << e.what() << '\n';
        return 1;
    }
}
