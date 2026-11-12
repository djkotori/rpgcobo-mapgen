#include "TownMapImageGenerator.h"
#include "VillageMapImageGenerator.h"
#include "AssetFoundation.h"
#include "SettlementRelief.h"
#include <iostream>
#include <numeric>
#include <stdexcept>
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
MapGenParams Params(int site,int arms) {
    MapGenParams p;p.width=p.depth=160;p.seed=57779217;
    p.extra={{"assetFoundation",1},{"assetCount",4},{"assetPlotCount",24},{"assetRoadWidth",2},
        {"villageMainRoadCount",arms},{"settlementSite",site},{"siteRotation",0},{"candidateCount",6},{"townCityType",2}};
    for(int i=0;i<4;++i) {
        const auto k="asset"+std::to_string(i);
        p.extra[k+"Id"]=0;p.extra[k+"Rotation"]=i;
        p.extra[k+"Width"]=i%2?10:12;p.extra[k+"Depth"]=i%2?12:10;
        p.extra[k+"DoorSide"]=(2+i)%4;
        p.extra[k+"DoorX"]=i==0?3:i==1?0:i==2?7:8;
        p.extra[k+"DoorY"]=i==0?8:i==1?3:i==2?0:7;
    }
    return p;
}
int main(){try {
    {
        auto params=Params(0,2);
        params.width=31;
        MapPlan previous;
        previous.Reset(64,64,1,"previous");
        std::string error;
        Require(!TownMapImageGenerator(params).Generate(previous,error) && previous.type=="previous",
            "Invalid asset town dimensions reset the output plan");
    }
    for(int site=0;site<7;++site)for(int arms:{0,2,4}) {
        auto args=Params(site,arms);MapPlan p;std::string error;TownMapImageGenerator g(args);
        Require(g.Generate(p,error),error.c_str());
        Require(p.cityType=="paved_city","Town identity was lost");
        Require(BuildAssetFoundation(p,args,error),error.c_str());
        Require(p.selectionReason=="town_growth","Town used legacy random plots");
        int center=0,main=0,houses=0;
        for(const auto& r:p.regions) {
            if(r.kind=="village_center") {
                ++center;Require(r.x1-r.x0+1>=16,"Town market square is not larger");
                if(site==1||site==4||site==5||site==6) {
                    bool waterfront=false;
                    for(int y=r.y0;y<=r.y1;++y)for(int x=r.x0;x<=r.x1;++x)
                        for(const auto step:{std::pair<int,int>{1,0},{-1,0},{0,1},{0,-1}}) {
                            const auto b=p.biomes[(y+step.second)*p.width+x+step.first];
                            waterfront|=b==RIVER||b==LAKE||b==TOWN_SEA;
                        }
                    Require(waterfront,"Town center does not touch water");
                }
            }
            if(r.kind=="village_main_road") {++main;Require(r.width==8,"Town main width changed");}
            if(r.kind=="village_middle_road")Require(r.width==4,"Town middle width changed");
            if(r.kind!="asset_plot")continue;
            ++houses;const int side=(2+r.rotation)%4;
            Require(SettlementBuildingOrientationAllowed(r.x1-r.x0+1,r.y1-r.y0+1,r.rotation,side),"Oversized sideways town building");
            const int rx=side==1?r.x1+3:side==3?r.x0-3:r.doorX;
            const int ry=side==0?r.y0-3:side==2?r.y1+3:r.doorY;
            for(int k=0;k<2;++k)Require(p.biomes[(ry+(side%2?k:0))*160+rx+(side%2?0:k)]==TOWN_PAVEMENT,"Town entrance is not aligned to paved frontage");
        }
        Require(center==1&&main==arms&&houses>0,"Town center, main count or buildings missing");
        Require(EvaluateAssetFoundation(p).Passed(),"Town streets disconnected");
        Require(std::none_of(p.biomes.begin(),p.biomes.end(),[](auto b){return b==ROAD;}),"Paved City retained dirt streets");
        ApplySettlementRelief(p,2);ApplySettlementShore(p);
        for(const auto& r:p.regions)if(r.kind=="asset_plot")
            for(int y=r.y0-2;y<=r.y1+2;++y)for(int x=r.x0-2;x<=r.x1+2;++x)
                Require(p.heightOffsets[y*160+x]==0&&p.shoreCuts[y*160+x]==0,"Town yard is not level");
        Require(std::none_of(p.biomes.begin(),p.biomes.end(),[](auto b){return b==WALL;}),"Town created optional walls by default");
        if(site==0&&arms==2) {
            MapPlan rural;VillageMapImageGenerator v(args);Require(v.Generate(rural,error),error.c_str());
            Require(std::accumulate(p.settlementMask.begin(),p.settlementMask.end(),0)>
                std::accumulate(rural.settlementMask.begin(),rural.settlementMask.end(),0),"Town did not gain an urban development area");
        }
    }
    std::cout<<"PASS 21 urban layouts: shared growth, exact doors, paving, larger center/footprint, relief and optional walls\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
