#pragma once

#include "MapPlan.h"

#include <cstdint>
#include <vector>

void ApplyWorldBiomes(MapPlan& plan, const std::vector<float>& height,
    std::uint32_t seed, float coastRate, float snowfieldRate, float mountainSnowfieldRate = 1.0f);
