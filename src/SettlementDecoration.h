#pragma once
#include "MapImageGenerator.h"
#include <algorithm>
#include <cmath>

// Decorative cells are separate from terrain and navigation. Stable per-cell
// samples make richer versions a superset without changing houses or roads.
inline void ApplySettlementDecoration(MapPlan& plan,double wealth,const MapGenParams& params = {}) {
    if(plan.type!="village" && plan.type!="town") return;
    wealth=std::clamp(wealth,0.7,2.5);
    plan.countryWealth=wealth;
    plan.wealthClass=wealth<0.9?"strained":wealth<1.15?"ordinary":wealth<1.65?"prosperous":wealth<2.15?"wealthy":"opulent";
    auto& regions=plan.regions;
    regions.erase(std::remove_if(regions.begin(),regions.end(),[](const auto& r){return r.kind=="settlement_flower" || r.kind=="settlement_prop" || r.kind=="settlement_crop" || r.kind=="settlement_plant";}),regions.end());
    const int w=plan.width,d=plan.depth,n=w*d;
    if(n<=0 || static_cast<int>(plan.biomes.size())!=n) return;
    std::vector<unsigned char> reserved(n,0),garden(n,0);
    std::vector<MapPlanRegion> lots;
    for(const auto& r:regions) {
        if(r.kind=="asset_plot" || r.kind=="asset_bridge" || r.kind=="asset_wall" || r.kind=="settlement_facility") {
            for(int y=std::max(0,r.y0-2);y<=std::min(d-1,r.y1+2);++y)
                for(int x=std::max(0,r.x0-2);x<=std::min(w-1,r.x1+2);++x)reserved[y*w+x]=1;
        }
        if(r.kind=="village_open_space") {
            lots.push_back(r);
            lots.back().growthStage=static_cast<int>((plan.seed+static_cast<unsigned>(r.id)*2654435761U)%4);
            for(int y=r.y0+1;y<r.y1;++y)for(int x=r.x0+1;x<r.x1;++x) {
                garden[y*w+x]=r.role=="park";
                if(r.role=="farm")reserved[y*w+x]=1;
            }
        }
    }
    for(const auto& m:plan.markers) if(m.kind=="asset_approach" || m.kind=="village_open_space_approach" || m.kind=="village_center" || m.kind=="settlement_facility_access")
        for(int y=std::max(0,m.y-3);y<=std::min(d-1,m.y+3);++y)
            for(int x=std::max(0,m.x-3);x<=std::min(w-1,m.x+3);++x)reserved[y*w+x]=1;
    auto surface=[&](int i) {
        return (plan.heightOffsets.size()==plan.biomes.size()?plan.heightOffsets[i]:0)-
            (plan.shoreCuts.size()==plan.biomes.size()?plan.shoreCuts[i]:0);
    };
    const double density=wealth<1.0?0.0:0.04+(wealth-1.0)/1.5*0.51;
    int id=0;
    auto prop=[&](int x,int y,int width,int depth,const std::string& role) {
        const auto key = "settlementProp_" + role;
        const auto modelWidth = params.extra.find(key + "_width");
        const auto modelDepth = params.extra.find(key + "_depth");
        if (modelWidth != params.extra.end()) {
            if (!(modelWidth->second >= 1 && modelWidth->second <= w)) return false;
            width = static_cast<int>(modelWidth->second);
        }
        if (modelDepth != params.extra.end()) {
            if (!(modelDepth->second >= 1 && modelDepth->second <= d)) return false;
            depth = static_cast<int>(modelDepth->second);
        }
        if(x<2||y<2||x+width>=w-2||y+depth>=d-2)return false;
        const int level=surface(y*w+x);
        for(int yy=y-1;yy<=y+depth;++yy)for(int xx=x-1;xx<=x+width;++xx) {
            const int i=yy*w+xx,b=plan.biomes[i];
            if(reserved[i]||(b!=0&&b!=1&&b!=2)||surface(i)!=level||level<0||(!plan.settlementMask.empty()&&!plan.settlementMask[i]))return false;
        }
        for(int yy=y-1;yy<=y+depth;++yy)for(int xx=x-1;xx<=x+width;++xx)reserved[yy*w+xx]=1;
        MapPlanRegion r{"settlement_prop",id++,x,y,x+width-1,y+depth-1};r.role=role;regions.push_back(r);return true;
    };
    const bool desert=plan.settlementSite=="oasis";
    for(const auto& lot:lots) {
        if(lot.role=="farm") {
            const int style=lot.growthStage;
            for(int y=lot.y0+2;y<=lot.y1-2;y+=(style==1?4:2))for(int x=lot.x0+2;x<=lot.x1-2;x+=(style==1?2:4)) {
                if(style==2 && (std::abs(x-(lot.x0+lot.x1)/2)<2 || std::abs(y-(lot.y0+lot.y1)/2)<2))continue;
                MapPlanRegion r{"settlement_crop",id++,x,y,std::min(x+(style==1?0:1),lot.x1-2),std::min(y+(style==1?1:0),lot.y1-2)};
                r.role=style==0?"leafy_crop":style==1?"tall_crop":style==2?"herb_crop":((y-lot.y0)%4==0?"leafy_crop":"tall_crop");
                regions.push_back(r);
            }
        } else if(lot.role=="park") {
            const std::string tree=desert?"palm":plan.settlementSite=="forest"?"conifer":"shade_tree";
            prop(lot.x1-3,lot.y0+1,3,3,tree);
            prop(lot.x0+1,lot.y1-2,4,2,"bench");
            if(lot.growthStage==1) {
                prop(lot.x0+1,lot.y0+1,3,3,tree);
                prop(lot.x1-4,lot.y1-2,4,2,"bench");
            } else if(lot.growthStage==2) {
                prop(lot.x0+1,lot.y0+1,desert?3:2,desert?3:2,desert?"cactus":"shrub");
                prop(lot.x1-2,lot.y1-2,2,2,"shrub");
            } else if(lot.growthStage==3) {
                prop(lot.x0+1,lot.y0+1,2,2,"rock");
                prop((lot.x0+lot.x1)/2,(lot.y0+lot.y1)/2,2,2,desert?"rock":"shrub");
            }
        }
    }
    int benches=0,plants=0;
    for(int y=4;y<d-5;y+=4)for(int x=4;x<w-5;x+=4) {
        const int i=y*w+x;
        bool roadside=false;
        for(int dy=-3;dy<=3&&!roadside;++dy)for(int dx=-3;dx<=3;++dx) {
            const int b=plan.biomes[(y+dy)*w+x+dx];
            if(std::abs(dx)+std::abs(dy)<=3&&(b==5||b==14)){roadside=true;break;}
        }
        if(!roadside)continue;
        const unsigned sample=(plan.seed+static_cast<unsigned>(i)*2654435761U)%17;
        if(sample==0 && benches<(plan.type=="town"?4:2) && prop(x,y,4,2,"bench"))++benches;
        else if(sample<5 && plants<12) {
            const auto b=plan.biomes[i];
            const bool dry=desert||b==1;
            const std::string role=dry?"cactus":plan.settlementSite=="mountain_valley"?"rock":"shrub";
            if(prop(x,y,dry?3:2,dry?3:2,role))++plants;
        }
    }
    for(int y=2;y<d-2;++y)for(int x=2;x<w-2;++x) {
        const int i=y*w+x,b=plan.biomes[i];
        if(reserved[i] || (b!=0&&b!=1&&b!=2) || (!plan.settlementMask.empty()&&!plan.settlementMask[i]))continue;
        if(b==1 || desert)continue; // Sand receives sparse cactus/palm props, never flowers.
        if(surface(i)<0)continue; // Never put flowers in eroded wet banks.
        bool roadside=false;
        for(int dy=-2;dy<=2&&!roadside;++dy)for(int dx=-2;dx<=2;++dx) {
            const int j=(y+dy)*w+x+dx;
            if(std::abs(dx)+std::abs(dy)<=2 && (plan.biomes[j]==5||plan.biomes[j]==14) && surface(j)==surface(i)) {roadside=true;break;}
        }
        if(!roadside && !garden[i])continue;
        uint32_t hash=plan.seed^(static_cast<uint32_t>(i)*0x9e3779b9U);
        hash^=hash>>16;hash*=0x7feb352dU;hash^=hash>>15;hash*=0x846ca68bU;hash^=hash>>16;
        const double sample=(hash&0xffffffU)/16777216.0;
        if(sample>=density*(roadside?1.0:0.35))continue;
        MapPlanRegion r{"settlement_flower",id++,x,y,x,y};
        r.role=hash&0x1000000U?"flower_red":"flower_yellow";
        if(plan.settlementSite=="forest" || b==2) {r.kind="settlement_plant";r.role=(hash&0x1000000U)?"fern":"mushroom";}
        else if(plan.settlementSite=="coast") r.role=hash&0x1000000U?"coastal_white":"coastal_blossom";
        else if(plan.settlementSite=="river") {r.kind="settlement_plant";r.role="river_grass";}
        else if(plan.settlementSite=="lake") r.role=hash&0x1000000U?"lakeside_pink":"lakeside_white";
        else if(plan.settlementSite=="mountain_valley") r.role=hash&0x1000000U?"alpine_yellow":"alpine_red";
        regions.push_back(r);
    }
}
