#include "WorldBridges.h"

#include "MapImageGenerator.h"

#include <array>

namespace mapgen {

bool IsRiverCrossing(const std::vector<std::uint8_t>& biomes, int width, int height,
    int x, int y, BridgeAxis axis) {
    constexpr int radius = 2;
    constexpr int side = 2 * radius + 1;
    if (axis == BridgeAxis::None || x < radius || y < radius || x >= width - radius || y >= height - radius) return false;
    const auto at = [&](int dx, int dy) { return biomes[(y + dy) * width + x + dx]; };
    const auto river = [](std::uint8_t b) { return b == RIVER || b == BRIDGE; };
    const auto bank = [](std::uint8_t b) {
        return b == ROAD || b == PLAIN || b == DESERT || b == FOREST;
    };
    const auto water = [&](std::uint8_t b) { return river(b) || b == LAKE || b == SEA; };
    if (!river(at(0, 0))) return false;
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        if (at(dx, dy) == SEA) return false;
    }

    const bool horizontal = axis == BridgeAxis::Horizontal;
    const int ax = horizontal ? 1 : 0;
    const int ay = horizontal ? 0 : 1;
    if (!bank(at(-ax, -ay)) || !bank(at(ax, ay))) return false;
    // Both sides of the river must continue transversely, possibly diagonally.
    for (int sign : {-1, 1}) {
        bool continues = false;
        for (int offset = -1; offset <= 1; ++offset) {
            if (river(at(offset * ax + sign * ay, offset * ay + sign * ax))) continues = true;
        }
        if (!continues) return false;
    }

    std::array<bool, side * side> seen{};
    std::array<int, side * side> queue{};
    constexpr int center = radius * side + radius;
    const int step = ax + ay * side;
    int tail = 0;
    queue[tail++] = center - step;
    seen[center - step] = true;
    // Four-connected land and eight-connected river permit pixel-diagonal channels.
    constexpr int dx[] = {1, -1, 0, 0};
    constexpr int dy[] = {0, 0, 1, -1};
    for (int head = 0; head < tail; ++head) {
        const int cur = queue[head];
        if (cur == center + step) return false;
        for (int k = 0; k < 4; ++k) {
            const int nx = cur % side + dx[k], ny = cur / side + dy[k];
            if (nx < 0 || nx >= side || ny < 0 || ny >= side) continue;
            const int next = ny * side + nx;
            if (seen[next] || water(at(nx - radius, ny - radius))) continue;
            seen[next] = true;
            queue[tail++] = next;
        }
    }
    return true;
}

}  // namespace mapgen
