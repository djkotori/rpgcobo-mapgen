#include "CastleSettlement.h"
#include "AssetFoundation.h"
#include <iostream>
#include <stdexcept>
void Require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
MapGenParams Parameters(int mode,int size,unsigned seed=4242) {
    MapGenParams p;p.width=p.depth=size;p.seed=seed;
    p.extra={{"assetCount",4},{"assetPlotCount",16},{"assetRoadWidth",2},{"candidateCount",6},{"castleMode",mode}};
    for(int i=0;i<4;++i){auto k="asset"+std::to_string(i);
        p.extra[k+"Id"]=0;p.extra[k+"Width"]=i%2?10:12;p.extra[k+"Depth"]=i%2?12:10;
        p.extra[k+"Rotation"]=i;p.extra[k+"DoorSide"]=(2+i)%4;
        p.extra[k+"DoorX"]=i==0?3:i==1?0:i==2?7:8;p.extra[k+"DoorY"]=i==0?8:i==1?3:i==2?0:7;
    }return p;
}
bool Masonry(std::uint8_t b){return b==MOUNTAIN||b==EXMOUNTAIN||b==SEA;}
void CheckSharedWallsAndRooms(const MapPlan& plan){
    const int w=plan.width,d=plan.depth;int partitions=0;
    for(const auto&a:plan.regions)if(a.kind=="room")for(const auto&b:plan.regions)if(b.kind=="room"){
        if(a.x1+1==b.x0)for(int y=std::max(a.y0,b.y0)+2;y<=std::min(a.y1,b.y1)-2;y+=2){
            Require(!Masonry(plan.biomes[y*w+a.x1])&&!Masonry(plan.biomes[y*w+b.x0+2]),"Vertical partition uses both room walls");
            if(Masonry(plan.biomes[y*w+b.x0])){++partitions;Require(Masonry(plan.biomes[y*w+b.x0+1]),"Shared wall lost its 2-block thickness");}
        }
        if(a.y1+1==b.y0)for(int x=std::max(a.x0,b.x0)+2;x<=std::min(a.x1,b.x1)-2;x+=2){
            Require(!Masonry(plan.biomes[a.y1*w+x])&&!Masonry(plan.biomes[(b.y0+2)*w+x]),"Horizontal partition uses both room walls");
            if(Masonry(plan.biomes[b.y0*w+x])){++partitions;Require(Masonry(plan.biomes[(b.y0+1)*w+x]),"Shared wall lost its 2-block thickness");}
        }
    }
    Require(partitions>0,"No shared room partitions");
    auto pass=[&](int x,int y){
        if(x<0||y<0||x+1>=w||y+1>=d)return false;
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const auto b=plan.biomes[(y+dy)*w+x+dx];
            if(b!=ROAD&&b!=BRIDGE&&b!=TOWN_FLOOR&&b!=TOWN_WOOD_FLOOR&&b!=TOWN_STONE_FLOOR&&b!=TOWN_PAVEMENT)return false;
        }return true;
    };
    std::vector<bool> seen(w*d,false);std::queue<int> q;
    for(const auto&m:plan.markers)if(m.role=="keep_public_entry"){
        Require(pass(m.x,m.y),"Keep entry is too narrow for a 2x2 human");const int i=m.y*w+m.x;seen[i]=true;q.push(i);break;
    }
    Require(!q.empty(),"Missing keep public entry");
    while(!q.empty()){const int i=q.front();q.pop();for(auto step:std::array<std::pair<int,int>,4>{{{0,-1},{1,0},{0,1},{-1,0}}}){
        const int x=i%w+step.first,y=i/w+step.second;if(pass(x,y)&&!seen[y*w+x]){seen[y*w+x]=true;q.push(y*w+x);}
    }}
    for(const auto&r:plan.regions)if(r.kind=="room"){
        bool reached=false;for(int y=r.y0+2;y<r.y1;++y)for(int x=r.x0+2;x<r.x1;++x)reached=reached||seen[y*w+x];
        Require(reached,"Room unreachable by 2x2 human: "+r.role);
    }
    for(const auto&m:plan.markers)if(m.kind=="village_center"||m.kind=="castle_town_connection")
        Require(seen[m.y*w+m.x],"Keep entry is disconnected from the castle town or main gate");
}
int main(){try {
    for(int mode=0;mode<5;++mode)for(int size:{160,240}) {
        auto p=Parameters(mode,size);MapPlan plan;std::string error;
        const bool generated=GenerateCastleSettlement(plan,p,error);
        Require(generated,"mode="+std::to_string(mode)+" size="+std::to_string(size)+": "+error);
        auto cp=p;cp.width/=2;cp.depth/=2;cp.extra["castleHumanScale"]=2;cp.extra["mainRoadWidth"]=3;
        cp.extra["keepScale"]=2;cp.extra["sharedKeepWalls"]=1;cp.extra["nativeAssetTown"]=1;
        cp.extra["keepWidth"]=cp.width*.375;cp.extra["keepHeight"]=cp.depth*.25;
        cp.extra["candidateCount"]=1;
        for(const auto&s:plan.castleHistory)if(s.phase=="layout_selection")cp.extra["castleCandidateOffset"]=s.additions;
        MapPlan original;CastleMapImageGenerator old(cp);Require(old.Generate(original,error),error);
        const auto keep=std::find_if(original.regions.begin(),original.regions.end(),[](const auto&r){return r.kind=="keep";});
        Require(keep!=original.regions.end(),"Original keep missing");
        CheckSharedWallsAndRooms(plan);
        // Every tile in the original keep, including narrow doors and furniture,
        // must become exactly 2x2 tiles and survive town placement unchanged.
        for(int y=keep->y0;y<=keep->y1;++y)for(int x=keep->x0;x<=keep->x1;++x)
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                Require(plan.biomes[(y*2+dy)*size+x*2+dx]==original.biomes[y*cp.width+x],"Town damaged the 2x2 castle keep");
        int homes=0,seeds=0;
        for(const auto&r:plan.regions){
            if(r.kind=="castle_town_seed")++seeds;
            if(r.kind!="asset_plot")continue;++homes;
            Require(r.doorX>=0&&r.doorY>=0&&SettlementBuildingOrientationAllowed(r.x1-r.x0+1,r.y1-r.y0+1,r.rotation,(2+r.rotation)%4),"Castle-town orientation invalid");
            for(int y=r.y0-2;y<=r.y1+2;++y)for(int x=r.x0-2;x<=r.x1+2;++x){
                Require(original.settlementMask[(y/2)*cp.width+x/2],"Town created outside original castle domain");
                Require(plan.settlementMask[y*size+x],"Native house escaped the outer-bailey building area");
            }
        }
        Require(homes>0&&seeds>0,"Original town seeds or native houses missing");
        Require(EvaluateAssetFoundation(plan).Passed(),"Castle gate and native town doors lack 2x2 connectivity");
        std::cout<<"PASS mode="<<mode<<" size="<<size<<" homes="<<homes<<" seeds="<<seeds<<'\n';
    }
    // With sufficient land, both dimensions must be exactly twice the old
    // keep, while room partitions remain one shared wall (2 output blocks).
    for(int mode=0;mode<5;++mode){
        auto p=Parameters(mode,160);p.extra["keepWidth"]=24;p.extra["keepHeight"]=18;
        p.extra["outerWallDefenseStrength"]=1;p.extra["wallThickness"]=1;
        MapPlan doubled;std::string error;const bool ok=GenerateCastleSettlement(doubled,p,error);Require(ok,error);
        auto cp=p;cp.width/=2;cp.depth/=2;cp.extra["castleHumanScale"]=2;cp.extra["mainRoadWidth"]=3;
        MapPlan oldPlan;CastleMapImageGenerator old(cp);Require(old.Generate(oldPlan,error),error);
        auto keep=[](const MapPlan& m){return *std::find_if(m.regions.begin(),m.regions.end(),[](const auto&r){return r.kind=="keep";});};
        const auto before=keep(oldPlan),after=keep(doubled);
        Require(after.x1-after.x0+1==(before.x1-before.x0+1)*4&&after.y1-after.y0+1==(before.y1-before.y0+1)*4,
            "Keep dimensions did not double relative to the previous 2x2 output");
        CheckSharedWallsAndRooms(doubled);
    }
    for(double defense:{0.0,1.0,1.8}){
        auto p=Parameters(0,240,51);p.extra["outerWallDefenseStrength"]=defense;
        p.extra["wallHeight"]=6;
        MapPlan plan;std::string error;Require(GenerateCastleSettlement(plan,p,error),error);
        Require(plan.outerWallDefenseStrength==defense,"Defense override lost");
        Require(std::any_of(plan.regions.begin(),plan.regions.end(),[](const auto&r){return r.kind=="moat";})==(defense>=1.5),"Moat option lost");
        Require(EvaluateAssetFoundation(plan).Passed(),"Defense option disconnected native town");
        for(int i=0;i<plan.width*plan.depth;++i)if(plan.settlementMask[i])
            Require(plan.biomes[i]!=MOUNTAIN&&plan.biomes[i]!=EXMOUNTAIN,"Castle town added stray walls to the bailey boundary");
        int greatGates=0;
        for(const auto&r:plan.regions)if(r.kind=="castle_front_door"){
            ++greatGates;Require(r.x1-r.x0+1==10&&r.y1-r.y0+1==2,"Great gate opening is not 10x2 blocks");
            for(int y=r.y0;y<=r.y1;++y)for(int x=r.x0;x<=r.x1;++x)
                Require(plan.biomes[y*plan.width+x]==BRIDGE,"Great gate opening was blocked");
        }
        Require(greatGates==1,"Castle does not have exactly one great front door");
    }
    {
        auto p=Parameters(0,160);p.extra["wallHeight"]=0;
        p.extra["outerDefense"]=1.8;p.extra["outerWallDefenseStrength"]=1.8;
        MapPlan plan;std::string error;Require(GenerateCastleSettlement(plan,p,error),error);
        Require(plan.outerWallDefenseStrength==0&&std::none_of(plan.castleDefenseMask.begin(),plan.castleDefenseMask.end(),[](auto b){return b!=0;}),
            "Legacy outerDefense alias re-enabled zero-height perimeter");
        Require(std::any_of(plan.biomes.begin(),plan.biomes.end(),[](auto b){return b==WALL||b==INNERWALL;}),"Zero outer wall height removed keep masonry");
    }
    for(double defense : {1.0,1.8}){
        auto p=Parameters(0,240);p.extra["wallHeight"]=6;p.extra["outerWallDefenseStrength"]=defense;
        p.extra["wallThickness"]=2;
        p.extra["assetCount"]=5;p.extra["asset4Id"]=38;p.extra["asset4Category"]=2;
        p.extra["asset4Width"]=4;p.extra["asset4Depth"]=2;
        MapPlan plan;std::string error;Require(GenerateCastleSettlement(plan,p,error),error);
        int modules=0;
        for(const auto&r:plan.regions)if(r.kind=="asset_wall"){
            ++modules;Require(r.x1-r.x0+1==4&&r.y1-r.y0+1==2,"Castle wall module dimensions changed");
            bool perimeter=false;
            for(int y=r.y0;y<=r.y1;++y)for(int x=r.x0;x<=r.x1;++x){
                const int i=y*240+x;
                perimeter |= plan.castleDefenseMask[i]==2 &&
                    plan.biomes[i]==(defense>=1.8?CASTLE_WALL_WALK:WALL);
            }
            Require(perimeter,"Castle wall module did not reserve outer curtain");
        }
        Require(modules>0,"Castle bypassed wall module reservation");
        Require(EvaluateAssetFoundation(plan).Passed(),"Wall module obstructed castle routing");
    }
    // Editor defaults with a moat and wider original approach exposed side
    // gates/towers being sealed by the enlarged keep, especially hill castles.
    for(int mode=0;mode<5;++mode){
        auto p=Parameters(mode,160);p.extra["keepWidthRatio"]=.4;p.extra["keepDepthRatio"]=.25;
        p.extra["outerWallDefenseStrength"]=1.8;p.extra["mainRoadWidth"]=4;
        p.extra["gateCountMin"]=1;p.extra["gateCountMax"]=3;p.extra["towerCountMin"]=4;p.extra["towerCountMax"]=8;
        p.extra["keepRoomCountMin"]=4;p.extra["keepRoomCountMax"]=12;
        p.extra["outerFacilityCountMin"]=4;p.extra["outerFacilityCountMax"]=12;
        MapPlan plan;std::string error;const bool ok=GenerateCastleSettlement(plan,p,error);Require(ok,error);
        CheckSharedWallsAndRooms(plan);Require(EvaluateAssetFoundation(plan).Passed(),"Enlarged keep sealed the native town's gate access");
        MapPlan first;std::string firstError;
        if(GenerateCastleSettlementCandidate(first,p,firstError,0)&&EvaluateAssetFoundation(first).Passed()){
            auto homes=[](const MapPlan& m){return std::count_if(m.regions.begin(),m.regions.end(),[](const auto&r){return r.kind=="asset_plot";});};
            Require(homes(plan)>=homes(first),"Joint castle/town search selected fewer usable houses than its first candidate");
        }
        if(mode==0){MapPlan again;Require(GenerateCastleSettlement(again,p,error),error);Require(plan.biomes==again.biomes,"Joint castle/town selection is not deterministic");}
    }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
