#pragma once

#include "MapImageGenerator.h"
#include "MapQuality.h"
#include "VillageGrowth.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <random>

// Reserve native wall modules after growth, without taking road/plot setbacks.
// A caller supplies the perimeter mask so keep masonry and mountains are not
// mistaken for the castle's outer curtain.
inline void ReserveSettlementWallAssets(MapPlan& plan,const MapGenParams& params,
    const std::vector<unsigned char>& perimeter) {
    auto get=[&](const std::string& key,int fallback){
        const auto it=params.extra.find(key);return it==params.extra.end()?fallback:static_cast<int>(std::lround(it->second));
    };
    const int w=plan.width,d=plan.depth;
    if(get("wallHeight",0)<=0 || perimeter.size()!=plan.biomes.size())return;
    std::vector<unsigned char> occupied(w*d,0);
    for(const auto&r:plan.regions)if(r.kind=="asset_plot"||r.kind=="asset_bridge"||r.kind=="asset_wall"||
        r.kind=="village_center"||r.kind=="village_open_space"||r.kind=="settlement_facility"||
        r.kind=="keep"||r.kind=="keep_expansion"||r.kind=="tower"||r.kind=="gatehouse"||
        r.kind=="castle_activity"||r.kind=="castle_fixture"||r.kind=="settlement_prop"){
        for(int y=std::max(0,r.y0-2);y<=std::min(d-1,r.y1+2);++y)
            for(int x=std::max(0,r.x0-2);x<=std::min(w-1,r.x1+2);++x)occupied[y*w+x]=1;
    }
    for(int n=0;n<get("assetCount",0);++n){
        const auto key="asset"+std::to_string(n);
        const int id=get(key+"Id",-1),aw=get(key+"Width",0),ad=get(key+"Depth",0);
        if(get(key+"Category",0)!=2||id<0||aw<=0||ad<=0||aw>w-4||ad>d-4)continue;
        bool done=false;
        for(int y=2;y+ad<=d-2&&!done;++y)for(int x=2;x+aw<=w-2&&!done;++x){
            bool touches=false,fits=true;
            for(int yy=y;yy<y+ad&&fits;++yy)for(int xx=x;xx<x+aw;++xx){
                const int i=yy*w+xx;const auto b=plan.biomes[i];
                touches|=perimeter[i]!=0;
                const bool curtain = perimeter[i] && (b==WALL || b==CASTLE_WALL_WALK);
                if(occupied[i] || (b!=PLAIN&&b!=DESERT&&b!=FOREST&&!curtain)){fits=false;break;}
            }
            if(!fits||!touches)continue;
            MapPlanRegion r{"asset_wall",id,x,y,x+aw-1,y+ad-1};
            r.role="MA"+std::string(id<10?"00":id<100?"0":"")+std::to_string(id);
            r.rotation=get(key+"Rotation",0);plan.regions.push_back(r);
            // The editor records this reservation but does not place the module.
            // Keep the terrain and curtain intact until a module is placed.
            for(int yy=y-2;yy<y+ad+2;++yy)for(int xx=x-2;xx<x+aw+2;++xx)occupied[yy*w+xx]=1;
            done=true;
        }
    }
}

// Editor-only foundation mode. Dimensions are block counts decoded by the
// editor from the current map_asset database, including roof overhangs.
inline bool BuildAssetFoundation(MapPlan& plan, const MapGenParams& params, std::string& error) {
    auto value = [&](const std::string& key, int fallback) {
        const auto it = params.extra.find(key);
        return it == params.extra.end() ? fallback : static_cast<int>(std::lround(it->second));
    };
    struct Asset { int id, w, d, category, rotation; };
    std::vector<Asset> assets;
    for (int i = 0; i < value("assetCount", 0); ++i) {
        const std::string key = "asset" + std::to_string(i);
        Asset a{value(key + "Id", -1), value(key + "Width", 0), value(key + "Depth", 0),
            value(key + "Category", 0), value(key + "Rotation", 0)};
        const int doorSide = value(key + "DoorSide", a.rotation == 0 ? 2 : -1);
        if(!SettlementBuildingOrientationAllowed(a.w,a.d,a.rotation,doorSide,a.category))continue;
        if (a.id >= 0 && a.w > 0 && a.d > 0) assets.push_back(a);
    }
    if (assets.empty()) { error = "No named construction assets found in map_asset"; return false; }
    const int w = plan.width, d = plan.depth;
    const bool walls = value("wallHeight", 0) > 0;
    // Keep terrain and crossings, remove the old one-person-scale structures.
    // Their metadata no longer describes the new, independently reserved plots.
    const auto oldRegions = plan.regions;
    // Town uses BRIDGE for both real crossings and dry building/settlement
    // entrances. Remove obsolete dry markers before interpreting crossings.
    std::vector<unsigned char> crossing(w*d,0);
    for(const auto& m:plan.markers) if(m.kind=="water_crossing" && m.x>=0 && m.y>=0 && m.x<w && m.y<d)
        crossing[m.y*w+m.x]=1;
    for(const auto& m:plan.markers) if((m.kind=="building_door" || m.kind=="entrance") &&
        m.x>=0 && m.y>=0 && m.x<w && m.y<d && !crossing[m.y*w+m.x] && plan.biomes[m.y*w+m.x]==BRIDGE)
        plan.biomes[m.y*w+m.x]=PLAIN;
    std::vector<unsigned char> outer(w*d, 0), occupied(w*d, 0), bridgePlot(w*d, 0);
    if (walls) for (const auto& r : oldRegions) if (r.kind == "wall" || r.kind == "curtain_wall") {
        for (int y = std::max(0, r.y0); y <= std::min(d-1, r.y1); ++y)
            for (int x = std::max(0, r.x0); x <= std::min(w-1, r.x1); ++x)
                if (plan.biomes[y*w+x] == WALL) outer[y*w+x] = 1;
    }
    for (int i = 0; i < w*d; ++i) {
        auto& b = plan.biomes[i];
        if (b == WALL || b == INNERWALL || b == SEA || b == TOWN_FURNITURE || b == CASTLE_WALL_WALK ||
            b == ROAD || b == TOWN_FLOOR || b == TOWN_WOOD_FLOOR || b == TOWN_STONE_FLOOR || b == TOWN_PAVEMENT) b = PLAIN;
        if (outer[i]) b = WALL;
    }
    plan.markers.clear(); plan.regions.clear(); plan.castleDefenseMask.clear();
    plan.castleHistory.clear(); plan.pavingStages.clear();
    plan.plazaEnabled=false; plan.plazaStyle.clear();
    plan.pavingTargetRatio=0; plan.pavingActualRatio=0; plan.pavingModel.clear();
    plan.castleDevelopmentModel="asset_foundation";
    plan.qualityScore=0;
    plan.selectionReason = "asset_foundation";
    // Fit full bridge assets over existing crossings. Both opposite ends must
    // meet dry land; an undersized asset must never produce a mid-river plot.
    for (const Asset& a : assets) if (a.category == 1 && a.w < w-4 && a.d < d-4) {
        bool done = false;
        for (int cy = 2; cy < d-2 && !done; ++cy) for (int cx = 2; cx < w-2 && !done; ++cx) {
            if (plan.biomes[cy*w+cx] != BRIDGE) continue;
            const int x = cx-a.w/2, y = cy-a.d/2;
            if (x < 2 || y < 2 || x+a.w >= w-2 || y+a.d >= d-2) continue;
            auto dry = [&](int xx, int yy) {
                const auto b=plan.biomes[yy*w+xx];
                return b==PLAIN || b==DESERT || b==FOREST || b==BRIDGE;
            };
            bool horizontal=true, vertical=true, fits=true, water=false;
            for (int yy=y; yy<y+a.d; ++yy) horizontal = horizontal && dry(x,yy) && dry(x+a.w-1,yy);
            for (int xx=x; xx<x+a.w; ++xx) vertical = vertical && dry(xx,y) && dry(xx,y+a.d-1);
            for (int yy=y; yy<y+a.d; ++yy) for (int xx=x; xx<x+a.w; ++xx) {
                const auto b=plan.biomes[yy*w+xx];
                water = water || b==RIVER || b==LAKE || b==TOWN_SEA;
                fits = fits && !bridgePlot[yy*w+xx] && !outer[yy*w+xx] && (dry(xx,yy) || b==RIVER || b==LAKE || b==TOWN_SEA);
            }
            if (!fits || !water || (!horizontal && !vertical)) continue;
            for (int yy=y; yy<y+a.d; ++yy) for (int xx=x; xx<x+a.w; ++xx) {
                plan.biomes[yy*w+xx]=BRIDGE; bridgePlot[yy*w+xx]=1;
            }
            MapPlanRegion r{"asset_bridge", a.id, x,y,x+a.w-1,y+a.d-1};
            r.rotation = a.rotation;
            r.role="MA" + std::string(a.id<10 ? "00" : (a.id<100 ? "0" : "")) + std::to_string(a.id);
            plan.regions.push_back(r);
            done=true;
        }
    }
    // Unsupported old crossings cannot be advertised as asset-ready bridges.
    for (int i=0;i<w*d;++i) if (plan.biomes[i]==BRIDGE && !bridgePlot[i]) plan.biomes[i]=RIVER;
    if(plan.type=="village" || plan.type=="town") {
        if(!village_growth::Generate(plan,params,error))return false;
        for(int i=0;i<w*d;++i)outer[i]=plan.biomes[i]==WALL;
        ReserveSettlementWallAssets(plan,params,outer);
        return true;
    }
    auto terrain = [&](int x, int y) {
        if (x < 1 || y < 1 || x >= w-1 || y >= d-1) return false;
        const auto b = plan.biomes[y*w+x];
        return !occupied[y*w+x] && (b == PLAIN || b == DESERT || b == FOREST || b == ROAD || b == BRIDGE);
    };
    const int roadWidth=std::clamp(value("assetRoadWidth",2),2,6);
    // Each path node reserves the complete requested road cross-section.
    auto passable = [&](int x, int y) {
        for(int dy=0;dy<roadWidth;++dy) for(int dx=0;dx<roadWidth;++dx)
            if(!terrain(x+dx,y+dy)) return false;
        return true;
    };
    int root = -1;
    for (int radius = 0; radius < std::max(w,d) && root < 0; ++radius)
        for (int y = std::max(1,d/2-radius); y <= std::min(d-3,d/2+radius) && root < 0; ++y)
            for (int x = std::max(1,w/2-radius); x <= std::min(w-3,w/2+radius); ++x)
                if (passable(x,y)) { root = y*w+x; break; }
    if (root < 0) { error = "No 2x2 walkable construction area"; return false; }
    auto road = [&](int node) {
        for (int dy = 0; dy < roadWidth; ++dy) for (int dx = 0; dx < roadWidth; ++dx) {
            auto& b = plan.biomes[node+dy*w+dx];
            if (b != BRIDGE) b = ROAD;
        }
    };
    road(root);
    std::mt19937 rng(params.seed);
    std::shuffle(assets.begin(), assets.end(), rng);
    int placed = 0;
    const int target = value("assetPlotCount", plan.type == "village" ? 16 : 28);
    std::vector<Asset> buildings;
    for (const Asset& a : assets) if (a.category == 0) buildings.push_back(a);
    for (int round = 0; round < target && !buildings.empty(); ++round) {
        const Asset& a = buildings[static_cast<std::size_t>(round) % buildings.size()];
        const int edge=roadWidth+2;
        if (a.w+2*edge+1 >= w || a.d+2*edge+1 >= d) continue;
        for (int attempt = 0; attempt < 500; ++attempt) {
            const int x = std::uniform_int_distribution<int>(edge, w-a.w-edge-1)(rng);
            const int y = std::uniform_int_distribution<int>(edge, d-a.d-edge-1)(rng);
            bool fits = true;
            for (int yy = y-2; yy < y+a.d+2 && fits; ++yy) for (int xx = x-2; xx < x+a.w+2; ++xx) {
                const int i = yy*w+xx;
                const auto b = plan.biomes[i];
                if (occupied[i] || outer[i] || (b != PLAIN && b != DESERT && b != FOREST) ||
                    (!plan.settlementMask.empty() && !plan.settlementMask[i])) { fits = false; break; }
            }
            if (!fits) continue;
            // Reserve the full asset extent before routing, so no route can
            // cross a roof or another asset's two-block setback.
            for (int yy = y-2; yy < y+a.d+2; ++yy) for (int xx = x-2; xx < x+a.w+2; ++xx) occupied[yy*w+xx] = 1;
            const int doorX = x + a.w/2-roadWidth/2, doorY = y+a.d+2;
            const int start = doorY*w+doorX;
            std::vector<int> previous(w*d, -1);
            std::queue<int> queue;
            if (passable(doorX,doorY)) { queue.push(start); previous[start] = start; }
            while (!queue.empty() && previous[root] < 0) {
                const int i = queue.front(); queue.pop();
                for (int step : {-1,1,-w,w}) {
                    const int n = i+step;
                    if (n < 0 || n >= w*d || previous[n] >= 0 || !passable(n%w,n/w)) continue;
                    previous[n] = i; queue.push(n);
                }
            }
            if (previous[root] < 0) {
                for (int yy = y-2; yy < y+a.d+2; ++yy) for (int xx = x-2; xx < x+a.w+2; ++xx) occupied[yy*w+xx] = 0;
                continue;
            }
            for (int i = root; ; i = previous[i]) { road(i); if (i == start) break; }
            for (int yy = y-2; yy < y+a.d+2; ++yy) for (int xx = x-2; xx < x+a.w+2; ++xx)
                plan.biomes[yy*w+xx] = TOWN_STONE_FLOOR;
            for (int yy = y; yy < y+a.d; ++yy) for (int xx = x; xx < x+a.w; ++xx)
                plan.biomes[yy*w+xx] = TOWN_WOOD_FLOOR;
            // Match the approach width to the full connecting road.
            for (int yy = y+a.d; yy < doorY+roadWidth; ++yy) for (int dx = 0; dx < roadWidth; ++dx)
                plan.biomes[yy*w+doorX+dx] = ROAD;
            MapPlanRegion r{"asset_plot", placed, x, y, x+a.w-1, y+a.d-1};
            r.rotation = a.rotation;
            r.role = "MA" + std::string(a.id < 10 ? "00" : (a.id < 100 ? "0" : "")) + std::to_string(a.id);
            plan.regions.push_back(r);
            plan.markers.push_back({"asset_approach", doorX, doorY, placed});
            ++placed;
            break;
        }
    }
    if (placed == 0) { error = "No asset fits the construction area. Increase map size or choose smaller assets."; return false; }
    // Reserve wall modules only when requested, at the existing perimeter.
    if (walls) for (const Asset& a : assets) if (a.category == 2 && a.w < w-4 && a.d < d-4) {
        bool done=false;
        for (int y=2; y+a.d<d-2 && !done; ++y) for (int x=2; x+a.w<w-2 && !done; ++x) {
            bool touches=false, fits=true;
            for (int yy=y; yy<y+a.d && fits; ++yy) for (int xx=x; xx<x+a.w; ++xx) {
                const int i=yy*w+xx; const auto b=plan.biomes[i];
                touches=touches || outer[i];
                if (occupied[i] || (b!=PLAIN && b!=DESERT && b!=FOREST && b!=WALL)) { fits=false; break; }
            }
            if (!fits || !touches) continue;
            for (int yy=y; yy<y+a.d; ++yy) for (int xx=x; xx<x+a.w; ++xx) {
                occupied[yy*w+xx]=1;
            }
            MapPlanRegion r{"asset_wall", a.id,x,y,x+a.w-1,y+a.d-1};
            r.rotation = a.rotation;
            r.role="MA" + std::string(a.id<10 ? "00" : (a.id<100 ? "0" : "")) + std::to_string(a.id);
            plan.regions.push_back(r); done=true;
        }
    }
    return true;
}

inline MapQualityMetrics EvaluateAssetFoundation(const MapPlan& plan) {
    MapQualityMetrics result;
    result.totalCells = plan.width*plan.depth;
    for (const auto& r:plan.regions) if (r.kind=="asset_plot") ++result.townBuildingCount;
    for (auto b:plan.biomes) {
        if (b==ROAD || b==BRIDGE || b==TOWN_PAVEMENT) ++result.roadCells;
        if (b==RIVER || b==LAKE || b==TOWN_SEA) ++result.waterCells;
        if (b==PLAIN || b==DESERT || b==FOREST || b==ROAD || b==BRIDGE || b==TOWN_PAVEMENT || b==TOWN_WOOD_FLOOR || b==TOWN_STONE_FLOOR)
            ++result.walkableCells;
    }
    result.walkableRatio=static_cast<double>(result.walkableCells)/result.totalCells;
    result.roadRatio=static_cast<double>(result.roadCells)/result.totalCells;
    result.waterRatio=static_cast<double>(result.waterCells)/result.totalCells;
    result.landRatio=1.0-result.waterRatio;
    auto relevant=[&](const MapPlanMarker& m) {
        return plan.selectionReason!="castle_and_asset_town" || m.role=="keep_public_entry" || m.kind=="asset_approach" || m.kind=="village_center" ||
            m.kind=="village_open_space_approach" || m.kind=="castle_town_connection" || m.kind=="settlement_facility_access" || m.kind=="settlement_mayor";
    };
    result.markerCount = static_cast<int>(std::count_if(plan.markers.begin(),plan.markers.end(),relevant));
    if (plan.regions.empty()) result.issues.push_back("no asset plots");
    // All approaches must remain connected after subsequent plot reservations.
    auto pass = [&](int x, int y) {
        if (x < 0 || y < 0 || x+1 >= plan.width || y+1 >= plan.depth) return false;
        for (int dy=0; dy<2; ++dy) for (int dx=0; dx<2; ++dx) {
            const auto b=plan.biomes[(y+dy)*plan.width+x+dx];
            if (b != ROAD && b != BRIDGE && b != TOWN_PAVEMENT) return false;
        }
        return true;
    };
    std::vector<unsigned char> seen(result.totalCells,0);
    std::queue<int> queue;
    const auto root=std::find_if(plan.markers.begin(),plan.markers.end(),relevant);
    if (root!=plan.markers.end()) {
        const auto& m=*root;
        if (pass(m.x,m.y)) { queue.push(m.y*plan.width+m.x); seen[queue.front()]=1; }
    }
    while (!queue.empty()) {
        const int i=queue.front(); queue.pop();
        for (int step : {-1,1,-plan.width,plan.width}) {
            const int n=i+step;
            if (n<0 || n>=result.totalCells || seen[n] || !pass(n%plan.width,n/plan.width)) continue;
            seen[n]=1; queue.push(n);
        }
    }
    for (const auto& m:plan.markers) if (relevant(m) && seen[m.y*plan.width+m.x]) ++result.reachableMarkers;
    if (result.reachableMarkers != result.markerCount) result.issues.push_back("asset approach is not reachable by a 2x2 human");
    return result;
}
