#include "CastleMapImageGenerator.h"
#include "CastleProgram.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Room shapes:
//  1. No room is long and thin: the ratio of its long side to its short side stays at 3:1 or less (a long room is
//     split into several). The throne room, the antechamber, the front room and the corridors are left out: they are long
//     on purpose. The rooms of the private suite are not: a long bedchamber is split like any other room.
//  3. The pieces of a split have different roles (a bedchamber, a dressing room, a study...), not the same one.
//  2. Where a room is split into two or more, the pieces are not all the same size: the ratio of the two pieces of
//     a split varies from castle to castle.
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool Exempt(const std::string& role) {
    return role == "corridor" || role == "throne_room" || role == "antechamber" || IsCastleEntryRole(role);
}
}  // namespace

int main() {
    try {
        int rooms = 0, thin = 0, pairs = 0, equalPairs = 0, unevenPairs = 0, sameRole = 0, privatePieces = 0;
        double worst = 0;
        std::string worstCase;
        for (const int size : {128, 160}) for (const int mode : {0, 1, 2, 3, 4}) for (unsigned seed = 1; seed <= 24; ++seed) {
            MapGenParams params;
            params.width = params.depth = size;
            params.seed = seed;
            params.extra = {{"castleMode", mode}, {"assetFoundation", 1}, {"lotFacilities", 0}};
            CastleMapImageGenerator generator(params);
            MapPlan plan;
            std::string error;
            const std::string context = "size=" + std::to_string(size) + " mode=" + std::to_string(mode) + " seed=" + std::to_string(seed);
            Require(generator.Generate(plan, error), context + ": " + error);
            std::vector<const MapPlanRegion*> list;
            for (const auto& r : plan.regions) if (r.kind == "room" && !Exempt(r.role)) list.push_back(&r);
            for (const auto* r : list) {
                const double w = r->x1 - r->x0 + 1, h = r->y1 - r->y0 + 1;
                const double aspect = std::max(w, h) / std::min(w, h);
                ++rooms;
                if (aspect > 3.5) {
                    ++thin;
                    if (aspect > worst) { worst = aspect; worstCase = context + " " + r->role + " " + std::to_string(static_cast<int>(w)) + "x" + std::to_string(static_cast<int>(h)); }
                }
            }
            // Two rooms side by side with the same extent (the pieces of one split): how unequal they are.
            for (std::size_t i = 0; i < list.size(); ++i) for (std::size_t j = 0; j < list.size(); ++j) {
                const MapPlanRegion& a = *list[i];
                const MapPlanRegion& b = *list[j];
                const bool stacked = a.x0 == b.x0 && a.x1 == b.x1 && a.y1 + 1 == b.y0;
                const bool abreast = a.y0 == b.y0 && a.y1 == b.y1 && a.x1 + 1 == b.x0;
                if (!stacked && !abreast) continue;
                const double la = stacked ? a.y1 - a.y0 + 1 : a.x1 - a.x0 + 1, lb = stacked ? b.y1 - b.y0 + 1 : b.x1 - b.x0 + 1;
                if (std::min(la, lb) < 8) continue;
                ++pairs;
                sameRole += a.role == b.role;
                privatePieces += IsCastlePrivateRole(a.role) && IsCastlePrivateRole(b.role);
                const double ratio = std::max(la, lb) / std::min(la, lb);
                equalPairs += ratio <= 1.1;
                unevenPairs += ratio >= 1.35;
            }
        }
        std::cout << "  rooms " << rooms << ", thinner than 3.5:1 " << thin << ", pairs " << pairs << ", nearly equal " << equalPairs
                  << ", uneven (>=1.35) " << unevenPairs << '\n';
        std::cout << "  pairs with the same role " << sameRole << ", pairs of private rooms " << privatePieces << '\n';
        Require(sameRole * 100 <= pairs * 12, "the pieces of a split have the same role too often: " + std::to_string(sameRole) + " of " + std::to_string(pairs));
        Require(thin == 0, "a room is long and thin (" + worstCase + "): " + std::to_string(thin) + " of " + std::to_string(rooms));
        Require(pairs >= 40, "too few split rooms to judge their proportions: " + std::to_string(pairs));
        Require(equalPairs * 100 < pairs * 50, "the pieces of a split are mostly the same size: " + std::to_string(equalPairs) + " of " + std::to_string(pairs));
        Require(unevenPairs * 100 >= pairs * 20, "the pieces of a split rarely differ in size: " + std::to_string(unevenPairs) + " of " + std::to_string(pairs));
        std::cout << "CASTLE_ROOM_SHAPE: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CASTLE_ROOM_SHAPE: FAIL " << e.what() << '\n';
        return 1;
    }
}
