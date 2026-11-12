#pragma once
#include <algorithm>

// Door side is measured after rotating the asset in map coordinates.
inline bool SettlementBuildingOrientationAllowed(int width,int depth,int /*rotation*/,int doorSide,int category=0) {
    return category!=0 || std::max(width,depth)<=12 || doorSide==2;
}
