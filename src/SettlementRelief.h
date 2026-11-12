#pragma once
#include "MapPlan.h"
#include <algorithm>
#include <cmath>
#include <queue>

// Grade dry banks towards the water and taper the exposed edges of cliffs.
// Independent of uplift: even a flat settlement needs a low water's edge.
inline void ApplySettlementShore(MapPlan& plan) {
    const int w=plan.width,d=plan.depth,n=w*d;
    plan.shoreCuts.assign(n,0);
    if(!n || static_cast<int>(plan.biomes.size())!=n) return;
    auto floodDistance=[&](const std::vector<unsigned char>& sources) {
        std::vector<int> distance(n,w+d);
        std::queue<int> pending;
        for(int i=0;i<n;++i) if(sources[i]) {distance[i]=0;pending.push(i);}
        while(!pending.empty()) {
            const int i=pending.front();pending.pop();
            for(int j : {i%w?i-1:-1,i%w<w-1?i+1:-1,i>=w?i-w:-1,i<n-w?i+w:-1})
                if(j>=0 && distance[j]>distance[i]+1) {
                    distance[j]=distance[i]+1;pending.push(j);
                }
        }
        return distance;
    };
    std::vector<unsigned char> water(n,0),protectedLand(n,0);
    bool hasWater=false,hasCliff=false;
    for(int i=0;i<n;++i) {
        const int b=plan.biomes[i];
        if(b==7||b==8||b==15) {water[i]=1;hasWater=true;}
        if(b==16) hasCliff=true;
        if(b==3||b==4||b==5||b==6||(b>=10&&b<=14)||b==18) protectedLand[i]=1;
    }
    if(!hasWater&&!hasCliff) return;
    for(const auto& r:plan.regions) {
        if(r.kind!="asset_plot" && r.kind!="asset_bridge" && r.kind!="asset_wall" && r.kind!="village_center" && r.kind!="village_open_space" && r.kind!="settlement_facility") continue;
        for(int y=std::max(0,r.y0-2);y<=std::min(d-1,r.y1+2);++y)
            for(int x=std::max(0,r.x0-2);x<=std::min(w-1,r.x1+2);++x) protectedLand[y*w+x]=1;
    }
    const auto waterDistance=hasWater?floodDistance(water):std::vector<int>{};
    const auto structureDistance=hasWater?floodDistance(protectedLand):std::vector<int>{};
    std::vector<int> cliffDistance;
    if(hasCliff) {
        std::vector<unsigned char> lowGround(n);
        for(int i=0;i<n;++i) lowGround[i]=plan.biomes[i]!=16;
        cliffDistance=floodDistance(lowGround);
    }
    int erodedCliffs=0,erodedBanks=0;
    for(int i=0;i<n;++i) {
        const int b=plan.biomes[i];
        if(protectedLand[i] || (b!=0&&b!=1&&b!=2&&b!=16&&b!=17)) continue;
        // One height step every two cells removes thin ridges and leaves the
        // full rock height in the interior of a broad mountain.
        const int cliffCut=b==16?6-std::min(6,(cliffDistance[i]+1)/2):0;
        plan.shoreCuts[i]=cliffCut;
        if(cliffCut) ++erodedCliffs;
        if(!hasWater) continue;
        // Three cells per vertical block form a broad low bank. Quantization
        // follows the curved shoreline, rather than a vertical retaining wall.
        const int ceiling=(waterDistance[i]-1)/3-1;
        const int uplift=plan.heightOffsets.size()==plan.biomes.size()?plan.heightOffsets[i]:0;
        const int surface=uplift+(b==16?6:0)-cliffCut;
        const int cut=std::min(std::max(0,surface-ceiling),structureDistance[i]/2);
        plan.shoreCuts[i]+=cut;
        if(cut) ++erodedBanks;
    }
    if(hasWater) plan.terrainHistory.push_back({"shore_erosion_and_low_banks",erodedBanks});
    if(hasCliff) plan.terrainHistory.push_back({"cliff_edge_erosion",erodedCliffs});
}

// Broad overlapping uplifts, bounded by distance to protected infrastructure.
// Keeping the entire road and foundation envelope level preserves 2x2 access.
inline void ApplySettlementRelief(MapPlan& plan, int amplitude) {
    const int w = plan.width, d = plan.depth, n = w * d;
    amplitude = std::clamp(amplitude, 0, 12);
    plan.heightOffsets.assign(n, 0);
    if (!amplitude || n == 0) return;
    std::vector<int> distance(n, n);
    std::queue<int> pending;
    auto protect = [&](int i) {
        if (distance[i] != 0) { distance[i] = 0; pending.push(i); }
    };
    for (int i = 0; i < n; ++i) {
        const int b = plan.biomes[i];
        if (b == 5 || b == 6 || b == 7 || b == 8 || b == 9 ||
            b == 10 || b == 11 || b == 12 || b == 14 || b == 15) protect(i);
    }
    for (const auto& r : plan.regions) {
        if (r.kind != "asset_plot" && r.kind != "asset_bridge" && r.kind != "asset_wall" && r.kind != "village_center" && r.kind != "village_open_space" && r.kind != "settlement_facility") continue;
        for (int y = std::max(0, r.y0 - 2); y <= std::min(d - 1, r.y1 + 2); ++y)
            for (int x = std::max(0, r.x0 - 2); x <= std::min(w - 1, r.x1 + 2); ++x)
                protect(y * w + x);
    }
    while (!pending.empty()) {
        int i = pending.front(); pending.pop();
        for (int j : {i % w ? i - 1 : -1, i % w < w - 1 ? i + 1 : -1,
                      i >= w ? i - w : -1, i < n - w ? i + w : -1}) {
            if (j >= 0 && distance[j] > distance[i] + 1) {
                distance[j] = distance[i] + 1; pending.push(j);
            }
        }
    }
    const double phase = (plan.seed % 6283) * .001;
    for (int y = 0; y < d; ++y) for (int x = 0; x < w; ++x) {
        int i = y * w + x;
        // Wavelengths of 48..100 cells avoid isolated spikes and pits.
        double uplift = .5 + .25 * std::sin(x * .065 + phase) * std::cos(y * .081 - phase)
                            + .25 * std::sin((x + y) * .043 + phase * 1.7);
        plan.heightOffsets[i] = std::min(static_cast<int>(std::floor(amplitude * uplift)),
                                        std::max(0, (distance[i] - 2) / 4));
    }
    plan.terrainHistory.push_back({"broad_uplift_and_settlement_grading", n});
}
