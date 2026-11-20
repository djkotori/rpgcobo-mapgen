#include "CastleMapImageGenerator.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Outline variety baseline. This version only prints the statistics of the castle
// outline signature per castle mode; it fails only when generation throws.
//
// OutlineSignature: the mask is the union of the "room" and "keep_expansion" regions.
// The outer boundary of its largest 4-connected component is traced into a rectilinear
// polygon. Boundary runs shorter than 3 cells are absorbed (jitter), and the result is
// encoded as L/R turns, canonicalised over rotation, reversal and mirror. The signature
// is that string followed by '|' and the number of holes (background components not
// touching the map border, with at least 16 cells).
namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

MapPlan Generate(int size, int mode, unsigned seed) {
    MapGenParams params;
    params.width = params.depth = size;
    params.seed = seed;
    params.extra = {{"castleMode", mode}, {"assetFoundation", 1}};
    CastleMapImageGenerator generator(params);
    MapPlan plan;
    std::string error;
    Require(generator.Generate(plan, error), std::to_string(size) + " mode=" + std::to_string(mode) +
        " seed=" + std::to_string(seed) + ": " + error);
    return plan;
}

// Labels the 4-connected components of the true cells. Returns the label per cell
// (-1 for false cells) and the size of each component.
std::vector<int> LabelComponents(const std::vector<std::uint8_t>& on, int w, int h, std::vector<int>& sizes) {
    std::vector<int> label(on.size(), -1);
    sizes.clear();
    for (int start = 0; start < w * h; ++start) {
        if (!on[static_cast<std::size_t>(start)] || label[static_cast<std::size_t>(start)] >= 0) continue;
        const int id = static_cast<int>(sizes.size());
        sizes.push_back(0);
        std::vector<int> stack{start};
        label[static_cast<std::size_t>(start)] = id;
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            ++sizes[static_cast<std::size_t>(id)];
            const int x = c % w, y = c / w;
            const int nx[4] = {x + 1, x - 1, x, x};
            const int ny[4] = {y, y, y + 1, y - 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h) continue;
                const int n = ny[k] * w + nx[k];
                if (on[static_cast<std::size_t>(n)] && label[static_cast<std::size_t>(n)] < 0) {
                    label[static_cast<std::size_t>(n)] = id;
                    stack.push_back(n);
                }
            }
        }
    }
    return label;
}

// Smallest rotation of a cyclic string.
std::string MinRotation(const std::string& s) {
    std::string best = s;
    for (std::size_t i = 1; i < s.size(); ++i) {
        const std::string r = s.substr(i) + s.substr(0, i);
        if (r < best) best = r;
    }
    return best;
}

std::string SwapTurns(std::string s) {
    for (char& c : s) c = c == 'L' ? 'R' : 'L';
    return s;
}

// Canonical form over rotation, reversal of traversal and mirror.
std::string CanonicalTurns(const std::string& s) {
    const std::string mirror = SwapTurns(s);
    const std::string reversed(s.rbegin(), s.rend());
    const std::string reversedFlipped = SwapTurns(reversed);
    const std::vector<std::string> variants = {s, mirror, reversed, reversedFlipped};
    std::string best;
    bool has = false;
    for (const std::string& v : variants) {
        const std::string r = MinRotation(v);
        if (!has || r < best) { best = r; has = true; }
    }
    return best;
}

// Traces the outer boundary of the cells in `comp` (the top-left-most cell's top edge
// starts the walk) and returns the canonical L/R turn string of the simplified outline.
std::string OutlineTurns(const std::vector<std::uint8_t>& comp, int w, int h) {
    auto inside = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && comp[static_cast<std::size_t>(y * w + x)] != 0;
    };
    // Directions in screen order: 0 right, 1 down, 2 left, 3 up.
    static const int dx[4] = {1, 0, -1, 0};
    static const int dy[4] = {0, 1, 0, -1};
    const int vw = w + 1;

    // Directed unit edges with the component on the same side; key = vertex * 4 + dir.
    std::set<std::pair<int, int>> edges;
    int startX = -1, startY = -1;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        if (!inside(x, y)) continue;
        if (startX < 0) { startX = x; startY = y; }
        if (!inside(x, y - 1)) edges.insert({y * vw + x, 0});
        if (!inside(x + 1, y)) edges.insert({y * vw + (x + 1), 1});
        if (!inside(x, y + 1)) edges.insert({(y + 1) * vw + (x + 1), 2});
        if (!inside(x - 1, y)) edges.insert({(y + 1) * vw + x, 3});
    }
    if (startX < 0) return "none";

    std::vector<int> units;
    std::pair<int, int> cur{startY * vw + startX, 0};
    for (;;) {
        edges.erase(cur);
        units.push_back(cur.second);
        const int v = cur.first;
        const int vx = v % vw + dx[cur.second], vy = v / vw + dy[cur.second];
        const int endKey = vy * vw + vx;
        // At a pinch vertex prefer the left turn, then the right turn, then straight.
        const int order[3] = {(cur.second + 3) % 4, (cur.second + 1) % 4, cur.second};
        bool found = false;
        for (int nd : order) {
            if (edges.count({endKey, nd})) { cur = {endKey, nd}; found = true; break; }
        }
        if (!found) break;
    }

    // Run-length encode the unit steps, cyclically.
    std::vector<std::pair<int, int>> seg;  // (direction, length)
    for (int d : units) {
        if (!seg.empty() && seg.back().first == d) ++seg.back().second;
        else seg.push_back({d, 1});
    }
    if (seg.size() > 1 && seg.front().first == seg.back().first) {
        seg.front().second += seg.back().second;
        seg.pop_back();
    }
    if (seg.size() < 4) return "degenerate";

    // Absorb runs shorter than 3 cells into their neighbours (jitter).
    while (seg.size() >= 5) {
        std::size_t k = seg.size();
        for (std::size_t i = 0; i < seg.size(); ++i) if (seg[i].second < 3) { k = i; break; }
        if (k == seg.size()) break;
        const std::size_t n = seg.size();
        // Rotate so that the neighbour before the short run is first.
        std::vector<std::pair<int, int>> rot;
        for (std::size_t i = 0; i < n; ++i) rot.push_back(seg[(k + n - 1 + i) % n]);
        const std::pair<int, int> a = rot[0], c = rot[2];
        std::pair<int, int> merged;
        if (a.first == c.first) {
            merged = {a.first, a.second + c.second};
        } else {
            const int net = a.second - c.second;
            merged = net >= 0 ? std::pair<int, int>{a.first, net} : std::pair<int, int>{c.first, -net};
        }
        seg.clear();
        seg.push_back(merged);
        for (std::size_t i = 3; i < n; ++i) seg.push_back(rot[i]);
    }

    // Encode each corner as R (clockwise in screen coordinates) or L.
    std::string turns;
    const std::size_t n = seg.size();
    for (std::size_t i = 0; i < n; ++i) {
        const int next = seg[(i + 1) % n].first;
        turns += next == (seg[i].first + 1) % 4 ? 'R' : 'L';
    }
    return CanonicalTurns(turns);
}
}  // namespace

// The outline signature of a castle plan: canonical turn string of the outer outline of
// the room/keep_expansion mask, then '|' and the number of enclosed holes.
static std::string OutlineSignature(const MapPlan& plan) {
    const int w = plan.width, h = plan.depth;
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    std::vector<std::uint8_t> mask(n, 0);
    for (const auto& region : plan.regions) {
        if (region.kind != "room" && region.kind != "keep_expansion") continue;
        for (int y = std::max(0, region.y0); y <= std::min(h - 1, region.y1); ++y)
            for (int x = std::max(0, region.x0); x <= std::min(w - 1, region.x1); ++x)
                mask[static_cast<std::size_t>(y * w + x)] = 1;
    }

    std::vector<int> sizes;
    const std::vector<int> label = LabelComponents(mask, w, h, sizes);
    if (sizes.empty()) return "empty|0";
    const int largest = static_cast<int>(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
    std::vector<std::uint8_t> comp(n, 0);
    for (std::size_t i = 0; i < n; ++i) comp[i] = label[i] == largest ? 1 : 0;
    const std::string turns = OutlineTurns(comp, w, h);

    // Holes: background components that do not touch the map border, at least 16 cells.
    std::vector<std::uint8_t> background(n, 0);
    for (std::size_t i = 0; i < n; ++i) background[i] = mask[i] ? 0 : 1;
    std::vector<int> bgSizes;
    const std::vector<int> bgLabel = LabelComponents(background, w, h, bgSizes);
    std::vector<bool> touchesBorder(bgSizes.size(), false);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        if (x != 0 && y != 0 && x != w - 1 && y != h - 1) continue;
        const int id = bgLabel[static_cast<std::size_t>(y * w + x)];
        if (id >= 0) touchesBorder[static_cast<std::size_t>(id)] = true;
    }
    int holes = 0;
    for (std::size_t id = 0; id < bgSizes.size(); ++id)
        if (!touchesBorder[id] && bgSizes[id] >= 16) ++holes;

    return turns + "|" + std::to_string(holes);
}

int main() {
    try {
        std::set<std::string> overall;
        int failures = 0, unfurnished = 0, castles = 0;
        for (int mode = 0; mode < 5; ++mode) {
            std::map<std::string, int> counts;
            int samples = 0;
            for (const int size : {128, 160}) for (unsigned seed = 1; seed <= 24; ++seed) {
                const MapPlan plan = Generate(size, mode, seed);
                const std::string signature = OutlineSignature(plan);
                ++counts[signature];
                overall.insert(signature);
                ++samples;
                ++castles;
                for (const auto& step : plan.castleHistory)
                    if (step.phase == "interior_design" && step.change.rfind("unfurnished", 0) == 0) {
                        std::cerr << "note: unfurnished castle mode=" << mode << " size=" << size << " seed=" << seed << '\n';
                        ++unfurnished;
                    }
            }
            int top = 0;
            for (const auto& entry : counts) top = std::max(top, entry.second);
            const double share = 100.0 * top / samples;
            std::cout << "OUTLINE_VARIETY mode=" << mode << " samples=" << samples
                      << " distinct=" << counts.size() << " top_share=" << std::fixed << std::setprecision(1)
                      << share << "%\n";
            // Hill (1) and river (2) castles are one tower column plus a band, and the shore follows
            // their faces, so they have less room to vary than the others. Before outlines were
            // composed from parts they had 2 and 1 signatures, with 92% and 100% in the top one.
            const bool narrow = mode == 1 || mode == 2;
            const std::size_t minDistinct = narrow ? 5 : 8;
            // (Hill's band must also keep the castle at least two thirds of the map wide, which leaves its outline less to vary:
            // its top share may be higher.)
            const double maxShare = mode == 1 ? 85.0 : narrow ? 62.0 : 45.0;
            if (counts.size() < minDistinct) { std::cerr << "FAIL: mode " << mode << " has fewer than " << minDistinct << " outline signatures\n"; ++failures; }
            if (share > maxShare) { std::cerr << "FAIL: mode " << mode << " top outline share above " << maxShare << "%\n"; ++failures; }
        }
        std::cout << "OUTLINE_VARIETY overall distinct=" << overall.size() << '\n';
        if (overall.size() < 25) { std::cerr << "FAIL: fewer than 25 outline signatures overall\n"; ++failures; }
        // A castle that cannot be furnished is allowed to be rare (one in fifty), not the usual outcome.
        if (unfurnished * 50 > castles) { std::cerr << "FAIL: too many unfurnished castles (" << unfurnished << " of " << castles << ")\n"; ++failures; }
        return failures == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
