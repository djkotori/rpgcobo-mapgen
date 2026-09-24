#pragma once

#include "MapPlan.h"

#include <cstdint>
#include <vector>

// roadClass records the actual painted road surface: 1 alley, 2 normal,
// 3 avenue. Influence is measured from that surface, not its centreline.
void ApplyTownPaving(
    MapPlan& plan,
    const std::vector<std::uint8_t>& footprint,
    const std::vector<std::uint8_t>& structure,
    const std::vector<std::uint8_t>& plazaMask,
    const std::vector<std::uint8_t>& roadClass,
    std::uint8_t groundTile);
