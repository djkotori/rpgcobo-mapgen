#include "AssetFoundation.h"
#include "SettlementRelief.h"
#include <iostream>
#include <stdexcept>

void Require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
MapGenParams Params(int wallHeight=0) {
    MapGenParams p;
    p.extra={{"assetCount",4},{"asset0Id",0},{"asset0Width",12},{"asset0Depth",12},
        {"asset1Id",5},{"asset1Width",26},{"asset1Depth",17},
        {"asset2Id",27},{"asset2Width",6},{"asset2Depth",9},{"asset2Category",1},
        {"asset3Id",38},{"asset3Width",13},{"asset3Depth",11},{"asset3Category",2},
        {"wallHeight",wallHeight}};
    return p;
}
MapPlan Terrain() {
    // Castle retains the generic foundation planner. Village and town now
    // use the shared door-aware growth planner tested separately.
    MapPlan p; p.Reset(160,160,42,"castle"); p.biomes.assign(160*160,PLAIN);
    for(int y=76;y<82;++y) for(int x=0;x<160;++x) p.biomes[y*160+x]=RIVER;
    for(int y=76;y<82;++y) for(int x=77;x<83;++x) p.biomes[y*160+x]=BRIDGE;
    for(int i=4;i<156;++i) {
        p.biomes[4*160+i]=WALL; p.biomes[155*160+i]=WALL;
        p.biomes[i*160+4]=WALL; p.biomes[i*160+155]=WALL;
    }
    p.regions.push_back({"wall",0,4,4,155,155});
    p.biomes[20*160+20]=BRIDGE;
    p.markers.push_back({"building_door",20,20,1});
    return p;
}
int main() {
    try {
        for (const auto* kind : {"castle_activity", "castle_fixture", "settlement_prop"}) {
            MapPlan plan;
            plan.Reset(32,32,42,"castle");
            plan.biomes.assign(32*32,PLAIN);
            plan.biomes[4*32+4]=WALL;
            std::vector<unsigned char> perimeter(32*32,0);
            perimeter[4*32+4]=1;
            plan.regions.push_back({kind,0,6,6,8,8});
            ReserveSettlementWallAssets(plan,Params(6),perimeter);
            Require(plan.regions.size()==1,"Wall reservation overlaps protected castle use");
            plan.regions.clear();
            const auto original=plan.biomes;
            ReserveSettlementWallAssets(plan,Params(6),perimeter);
            Require(plan.regions.size()==1 && plan.regions.front().kind=="asset_wall",
                "Unoccupied wall module site was not reserved");
            Require(plan.biomes==original,"Wall reservation changed the original terrain");
        }
        for(int walls : {0,6}) for(int seed : {1,42,4242}) for(int roadWidth : {2,4,6}) {
            auto p=Terrain(); auto args=Params(walls); args.seed=seed; std::string error;
            args.extra["assetRoadWidth"]=roadWidth;
            Require(BuildAssetFoundation(p,args,error),error.c_str());
            Require(EvaluateAssetFoundation(p).Passed(),"2x2 route disconnected");
            Require(p.biomes[20*160+20]!=RIVER,"dry doorway became a puddle");
            for(const auto& m:p.markers) for(int dy=0;dy<roadWidth;++dy) for(int dx=0;dx<roadWidth;++dx) {
                Require(m.x+dx<p.width && m.y+dy<p.depth,"road outside map");
                const auto b=p.biomes[(m.y+dy)*p.width+m.x+dx];
                Require(b==ROAD || b==BRIDGE,"requested road width was clipped");
            }
            bool bridge=false,wall=false;
            for(const auto& r:p.regions) {
                const int width=r.x1-r.x0+1, depth=r.y1-r.y0+1;
                if(r.kind=="asset_plot") {
                    Require((r.role=="MA000" && width==12 && depth==12) ||
                        (r.role=="MA005" && width==26 && depth==17),"asset size changed");
                    for(int y=r.y0;y<=r.y1;++y) for(int x=r.x0;x<=r.x1;++x)
                        Require(p.biomes[y*p.width+x]==TOWN_WOOD_FLOOR,"plot is not flat");
                }
                if(r.kind=="asset_bridge") {bridge=true; Require(width==6 && depth==9,"bridge size changed");}
                if(r.kind=="asset_wall") {wall=true; Require(width==13 && depth==11,"wall size changed");}
            }
            Require(bridge,"bridge footprint missing");
            Require(wall==(walls>0),"wall option ignored");
            if(!walls) for(auto b:p.biomes) Require(b!=WALL && b!=INNERWALL,"wall remains at height zero");
            const auto repeatTerrain=Terrain(); auto repeat=repeatTerrain;
            Require(BuildAssetFoundation(repeat,args,error) && repeat.biomes==p.biomes,"not deterministic");
            // Simulate an obstacle at an approach after planning. Validation
            // must catch a blockage for the full 2x2 human, even one cell.
            const auto m=p.markers.front(); p.biomes[m.y*p.width+m.x+1]=WALL;
            Require(!EvaluateAssetFoundation(p).Passed(),"2x2 obstruction undetected");
        }
        // Exercise the actual early-return growth paths, not the legacy planner.
        for(const auto* type:{"village","town"}) for(int walls:{0,6}) {
            auto p=Terrain();p.type=type;p.settlementMask.assign(160*160,0);
            for(int y=16;y<144;++y)for(int x=16;x<144;++x)p.settlementMask[y*160+x]=1;
            auto args=Params(walls);args.extra["assetPlotCount"]=8;
            args.extra["asset0DoorSide"]=2;args.extra["asset0DoorX"]=5;args.extra["asset0DoorY"]=10;
            args.extra["asset1DoorSide"]=2;args.extra["asset1DoorX"]=12;args.extra["asset1DoorY"]=15;
            std::string error;Require(BuildAssetFoundation(p,args,error),error.c_str());
            int bridges=0,wallModules=0;
            Require(p.regions.front().kind=="village_center","Retained bridge broke centre indexing");
            for(const auto&r:p.regions){
                if(r.kind=="asset_wall"){
                    ++wallModules;Require(r.x1-r.x0+1==13&&r.y1-r.y0+1==11,"Growth changed wall dimensions");
                }
                if(r.kind!="asset_bridge")continue;++bridges;
                Require(r.x1-r.x0+1==6&&r.y1-r.y0+1==9,"Growth discarded native bridge dimensions");
                for(const auto&b:p.regions)if(b.kind=="asset_plot"||b.kind=="settlement_facility"||b.kind=="village_center")
                    Require(b.x1<r.x0-2||b.x0>r.x1+2||b.y1<r.y0-2||b.y0>r.y1+2,"Building encroaches on bridge setback");
            }
            Require(bridges>0,"Growth discarded preplanned bridge");
            Require((wallModules>0)==(walls>0),"Growth skipped wall module reservation");
            ApplySettlementRelief(p,8);ApplySettlementShore(p);
            for(const auto&r:p.regions)if(r.kind=="asset_bridge"||r.kind=="asset_wall")
                for(int y=r.y0-2;y<=r.y1+2;++y)for(int x=r.x0-2;x<=r.x1+2;++x)
                    Require(!p.heightOffsets[y*160+x]&&!p.shoreCuts[y*160+x],"Bridge/wall setback deformed after growth");
        }
        auto p=Terrain(); auto args=Params(); args.extra["asset0Width"]=1000; args.extra["asset1Width"]=1000;
        {
            auto rotated=Terrain();
            auto parameters=Params();
            parameters.extra["asset2Width"]=9;
            parameters.extra["asset2Depth"]=8;
            parameters.extra["asset2Rotation"]=1;
            std::string error;
            Require(BuildAssetFoundation(rotated,parameters,error),error.c_str());
            const auto bridge=std::find_if(rotated.regions.begin(),rotated.regions.end(),
                [](const auto& r){return r.kind=="asset_bridge";});
            Require(bridge!=rotated.regions.end() && bridge->rotation==1 &&
                bridge->x1-bridge->x0+1==9 && bridge->y1-bridge->y0+1==8,
                "Rotated bridge metadata differs from its footprint");
        }
        std::string error;
        Require(!BuildAssetFoundation(p,args,error),"oversized assets were silently shrunk");
        std::cout<<"PASS asset footprints, bridge shores, optional walls, determinism and 2x2 clearance\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
