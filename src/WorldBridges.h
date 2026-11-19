#pragma once

#include <cstdint>
#include <vector>

namespace mapgen {

enum class BridgeAxis : std::uint8_t { None, Horizontal, Vertical };

bool IsRiverCrossing(const std::vector<std::uint8_t>& biomes, int width, int height,
    int x, int y, BridgeAxis axis);

}  // namespace mapgen
