#pragma once

#include "MapPlan.h"

#include <cstdint>
#include <utility>
#include <vector>

enum class SettlementSite { Plains, Coast, MountainValley, Forest, River, Lake, Oasis };

const char* SettlementSiteName(SettlementSite site);
const char* SettlementSiteEconomy(SettlementSite site);
const char* SettlementSiteFacility(SettlementSite site);

// Terrain precedes public spaces, buildings and roads. Rotation is fixed for
// the settlement; candidate selection may vary detail but cannot change its site.
void PrepareSettlementSite(MapPlan& plan, std::vector<std::uint8_t>& footprint,
    std::vector<std::uint8_t>& structure, SettlementSite site, int rotation,
    std::uint8_t ground, bool village, bool extraRiver = false, bool extraPond = false);

// Dry frontage near the actual resource geometry, avoiding existing structures.
std::pair<int, int> SettlementResourceAnchor(const MapPlan& plan, SettlementSite site);
