#include "VillageMapImageGenerator.h"
#include "AssetFoundation.h"
#include <iostream>
#include <stdexcept>

void Require(bool v,const std::string& message){if(!v)throw std::runtime_error(message);}
MapGenParams Parameters(int site,int arms,int seed=4242) {
    MapGenParams p;p.width=p.depth=160;p.seed=seed;
    p.extra={{"assetFoundation",1},{"assetCount",4},{"assetPlotCount",16},{"assetRoadWidth",2},
        {"villageMainRoadCount",arms},{"settlementSite",site},{"siteRotation",0},{"candidateCount",6}};
    // One rectangular house with an off-center two-block entrance, all turns.
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
MapPlan Generate(MapGenParams p) {
    MapPlan plan;std::string error;VillageMapImageGenerator g(p);
    Require(g.Generate(plan,error),error);
    Require(BuildAssetFoundation(plan,p,error),error);
    Require(EvaluateAssetFoundation(plan).Passed(),"growth routes disconnected");
    return plan;
}
int main(){try {
    int layouts=0,buildings=0;
    for(int site=0;site<7;++site) for(int arms=0;arms<=4;++arms) {
        auto args=Parameters(site,arms);const auto p=Generate(args);
        Require(std::none_of(p.biomes.begin(),p.biomes.end(),[](auto b){return b==TOWN_STONE_FLOOR;}),"Village yards are still paved in stone");
        int centers=0,roads=0,houses=0;
        for(const auto& r:p.regions) {
            if(r.kind=="village_open_space") {
                Require((r.role=="vacant_lot"||r.role=="farm"||r.role=="park") && r.doorX==-1 && r.doorY==-1,"Open lot still has a building entrance");
                for(int y=r.y0;y<=r.y1;++y)for(int x=r.x0;x<=r.x1;++x)
                    Require(p.biomes[y*p.width+x]==(site==6?DESERT:PLAIN),"Vacant lot retained its building foundation");
            }
            if(r.kind=="village_center") {
                ++centers;
                if(site==1||site==4||site==5||site==6) {
                    bool waterfront=false;
                    for(int y=r.y0;y<=r.y1;++y)for(int x=r.x0;x<=r.x1;++x)
                        for(const auto step:{std::pair<int,int>{1,0},{-1,0},{0,1},{0,-1}}) {
                            const auto b=p.biomes[(y+step.second)*p.width+x+step.first];
                            waterfront|=b==RIVER||b==LAKE||b==TOWN_SEA;
                        }
                    Require(waterfront,"Village center does not touch water");
                }
                for(int y=r.y0;y<=r.y1;++y)for(int x=r.x0;x<=r.x1;++x)
                    Require(p.biomes[y*p.width+x]==ROAD,"Village center is not leveled dirt");
            }
            if(r.kind=="village_main_road"){++roads;Require(r.width==8,"main width is not 4x");}
            if(r.kind=="village_middle_road")Require(r.width==4,"middle width is not 2x");
            if(r.kind!="asset_plot")continue;
            ++houses;++buildings;
            Require(SettlementBuildingOrientationAllowed(r.x1-r.x0+1,r.y1-r.y0+1,r.rotation,(2+r.rotation)%4),"Oversized sideways village building");
            Require(r.doorX>=r.x0&&r.doorX+1<=r.x1&&r.doorY>=r.y0&&r.doorY+1<=r.y1,"door outside plot");
            Require(r.frontageWidth==8||r.frontageWidth==4,"house does not front a public street");
            Require(r.x1-r.x0+1==(r.rotation%2?10:12),"rotated width changed");
            const int side=(2+r.rotation)%4;
            const int rx=side==1?r.x1+3:side==3?r.x0-3:r.doorX;
            const int ry=side==0?r.y0-3:side==2?r.y1+3:r.doorY;
            for(int k=0;k<2;++k) {
                const auto b=p.biomes[(ry+(side%2?k:0))*p.width+rx+(side%2?0:k)];
                Require(b==ROAD,"door is offset from its frontage road");
            }
            for(int y=r.y0;y<=r.y1;++y)for(int x=r.x0;x<=r.x1;++x)
                Require(p.biomes[y*p.width+x]==TOWN_WOOD_FLOOR,"street entered a house plot");
        }
        Require(centers==1,"village center is not mandatory/unique");
        Require(roads==arms,"main road count silently changed");Require(houses>0,"empty village");
        if(arms==0) {
            Require(std::none_of(p.markers.begin(),p.markers.end(),[](const auto& m){return m.kind=="village_outbound_road";}),"Zero main roads created an exit");
            for(const auto& r:p.regions) if(r.kind=="asset_plot")Require(r.frontageWidth==4,"Zero-road village does not face local streets");
        }
        if(site==3) {
            int openings=0;
            for(int side=0;side<4;++side) {
                bool previous=false;
                for(int k=0;k<160;++k) {
                    int x=side==0?k:side==1?159:side==2?159-k:0;
                    int y=side==0?0:side==1?k:side==2?159:159-k;
                    bool open=p.biomes[y*160+x]==ROAD;
                    if(open&&!previous) ++openings;
                    previous=open;
                    Require(open||p.biomes[y*160+x]==TOWN_TREES,"Unplanned forest opening");
                }
            }
            Require(openings==arms,"Forest exit count does not match main roads");
        }
        ++layouts;
        if(site==0&&arms==3){auto repeat=Generate(args);Require(repeat.biomes==p.biomes,"growth not deterministic");}
    }
    {
        auto args=Parameters(4,4);
        MapPlan river; river.Reset(160,160,args.seed,"village");
        river.biomes.assign(25600,PLAIN);river.settlementMask.assign(25600,1);
        for(int y=0;y<160;++y) for(int x=105;x<=111;++x) river.biomes[y*160+x]=RIVER;
        std::string error;
        Require(BuildAssetFoundation(river,args,error),error);
        int deck=0;for(auto b:river.biomes)if(b==BRIDGE)++deck;
        Require(deck>=7*8,"Main street did not bridge the river at its full width");
        Require(EvaluateAssetFoundation(river).Passed(),"Bridge does not connect to street network");
    }
    // A sparse initial road network must grow new frontages while dry land
    // remains, including villages explicitly configured with no main roads.
    for(int arms: {0,1}) for(int sidePercent:{0,75}) {
        auto args=Parameters(0,arms,57779217);args.extra["assetPlotCount"]=24;
        args.extra["sideFacingHouseKeepPercent"]=sidePercent;
        MapPlan open;open.Reset(160,160,args.seed,"village");
        open.biomes.assign(25600,PLAIN);open.settlementMask.assign(25600,1);
        std::string error;Require(BuildAssetFoundation(open,args,error),error);
        const auto count=std::count_if(open.regions.begin(),open.regions.end(),[](const auto& r){return r.kind=="asset_plot";});
        Require(count==24,"Growth stopped below target despite ample dry land: "+std::to_string(count));
        Require(std::any_of(open.regions.begin(),open.regions.end(),[](const auto& r){return r.kind=="village_open_space";}),"North-facing lots were not preserved as open space");
        Require(EvaluateAssetFoundation(open).Passed(),"Expanded local streets disconnected");
    }
    auto wide=Parameters(0,2);wide.extra["assetRoadWidth"]=3;
    for(const auto&r:Generate(wide).regions)if(r.kind=="village_main_road")Require(r.width==12,"base width ignored");
    auto impossible=Parameters(0,4);impossible.width=impossible.depth=64;impossible.extra["assetRoadWidth"]=6;
    MapPlan invalid;std::string reason;VillageMapImageGenerator impossibleGenerator(impossible);
    Require(impossibleGenerator.Generate(invalid,reason),"terrain failed before capacity check");
    Require(!BuildAssetFoundation(invalid,impossible,reason),"impossible road widths silently reduced");
    auto unknown=Parameters(0,3);unknown.extra["assetCount"]=0;
    Require(!village_growth::Generate(invalid,unknown,reason),"missing entrance metadata was guessed");
    {
        auto sideways=Parameters(0,0);sideways.extra["assetCount"]=1;
        sideways.extra["asset0Rotation"]=1;sideways.extra["asset0DoorSide"]=3;
        sideways.extra["asset0Width"]=14;
        MapPlan land;land.Reset(160,160,4242,"village");land.biomes.assign(25600,PLAIN);land.settlementMask.assign(25600,1);
        Require(!village_growth::Generate(land,sideways,reason),"Sideways-only large buildings were accepted");
        sideways.extra["asset0Width"]=sideways.extra["asset0Depth"]=12;
        sideways.extra["sideFacingHouseKeepPercent"]=100;
        sideways.extra["asset0DoorX"]=0;sideways.extra["asset0DoorY"]=2;sideways.extra["assetPlotCount"]=4;
        Require(village_growth::Generate(land,sideways,reason),"Small structures unnecessarily lost their rotations");
        Require(std::any_of(land.regions.begin(),land.regions.end(),[](const auto&r){return r.kind=="asset_plot"&&r.rotation==1;}),"Small structures did not retain authored rotations");
    }
    // Probabilities include both endpoints, and facility caps span IDs and rotations.
    for(int percent:{0,100}) {
        auto p=Parameters(0,4);p.extra["northFacingHouseKeepPercent"]=percent;p.extra["sideFacingHouseKeepPercent"]=percent;
        p.extra["assetPlotCount"]=24;
        MapPlan land;land.Reset(160,160,p.seed,"village");land.biomes.assign(25600,PLAIN);land.settlementMask.assign(25600,1);
        Require(village_growth::Generate(land,p,reason),reason);
        bool north=false,side=false;
        for(const auto&r:land.regions)if(r.kind=="asset_plot") {north|=r.rotation==2;side|=r.rotation%2==1;}
        Require(percent?north&&side:!north&&!side,"Orientation retention endpoint ignored");
    }
    for (int size : {-6,1,161}) {
        auto params=Parameters(0,4);
        params.extra["settlementPlazaSize"]=size;
        MapPlan land;
        land.Reset(160,160,params.seed,"village");
        land.biomes.assign(25600,PLAIN);
        Require(!village_growth::Generate(land,params,reason) && reason.find("plaza size")!=std::string::npos,
            "Invalid plaza size reached the centre search");
    }
    int churchesSeen=0;
    for(int seed=1;seed<=8;++seed){
        auto p=Parameters(0,4,seed);p.extra["assetCount"]=12;p.extra["assetPlotCount"]=24;
        for(int i=4;i<12;++i){auto src="asset"+std::to_string(i%4),dst="asset"+std::to_string(i);
            for(auto field:{"Width","Depth","Rotation","DoorSide","DoorX","DoorY"})p.extra[dst+field]=p.extra[src+field];
            p.extra[dst+"Id"]=i<8?8:9;p.extra[dst+"LimitGroup"]=1;p.extra[dst+"MaxCount"]=1;
        }
        MapPlan land;land.Reset(160,160,p.seed,"village");land.biomes.assign(25600,PLAIN);land.settlementMask.assign(25600,1);
        Require(village_growth::Generate(land,p,reason),reason);
        int count=0;for(const auto&r:land.regions)if(r.kind=="asset_plot"&&(r.role=="MA008"||r.role=="MA009"))++count;
        Require(count<=1,"Church cap bypassed through another ID, orientation, or rolled-back branch");churchesSeen+=count;
    }
    Require(churchesSeen>0,"Capped facilities were all removed instead of limited");
    std::cout<<"PASS "<<layouts<<" terrain/artery layouts, "<<buildings<<" door-aligned houses, final connectivity, width ratios and determinism\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
