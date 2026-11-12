#pragma once
#include "MapImageGenerator.h"
#include "SettlementAssetPolicy.h"
#include "SettlementCivic.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <random>
#include <string>

namespace village_growth {
struct Asset { int id,w,d,rotation,doorX,doorY,side,limitGroup,maxCount,residential; };
struct Point { int x,y; };
inline bool Grow(MapPlan& plan,const MapGenParams& params,int attempt,std::string& error) {
    auto get=[&](const std::string& key,int fallback) {
        auto i=params.extra.find(key); return i==params.extra.end()?fallback:static_cast<int>(std::lround(i->second));
    };
    const int w=plan.width,d=plan.depth,area=w*d;
    const bool urban=plan.type=="town";
    const int base=std::clamp(get("assetRoadWidth",2),2,6),mainWidth=base*4,middleWidth=base*2;
    const int mainCount=std::clamp(get("villageMainRoadCount",urban?4:3),0,4);
    const int target=std::clamp(get("assetPlotCount",urban?28:16),1,128);
    const int northKeep=std::clamp(get("northFacingHouseKeepPercent",10),0,100);
    const int sideKeep=std::clamp(get("sideFacingHouseKeepPercent",75),0,100);
    const bool civicEnabled=get("settlementCivicPlan",0)>0;
    const double wealth=settlement_civic::Wealth(plan,params);
    plan.countryWealth=wealth;
    std::mt19937 rng(params.seed+static_cast<unsigned>(attempt)*0x9e3779b9U);
    std::vector<Asset> assets;
    for(int i=0;i<get("assetCount",0);++i) {
        const auto k="asset"+std::to_string(i);
        if(get(k+"Category",0)!=0 || get(k+"DoorSide",-1)<0) continue;
        if(get("existingRoyalResidence",0) && get(k+"LimitGroup",0)==2)continue;
        Asset a{get(k+"Id",-1),get(k+"Width",0),get(k+"Depth",0),get(k+"Rotation",0),
            get(k+"DoorX",-1),get(k+"DoorY",-1),get(k+"DoorSide",-1),get(k+"LimitGroup",get(k+"Id",-1)+1000),get(k+"MaxCount",0),get(k+"Residential",0)};
        if(!SettlementBuildingOrientationAllowed(a.w,a.d,a.rotation,a.side))continue;
        if(a.id>=0 && a.w>=2 && a.d>=2 && a.doorX>=0 && a.doorY>=0 && a.doorX+1<a.w && a.doorY+1<a.d)
            assets.push_back(a);
    }
    if(assets.empty()) {error="Settlement growth needs front-facing buildings with identified exterior doors";return false;}
    std::vector<Asset> mayorAssets;
    if(civicEnabled&&wealth>=2.0){
        for (const auto& asset : assets) {
            const bool ordinaryResidence = asset.limitGroup == 0 || asset.limitGroup == asset.id + 1000;
            if (ordinaryResidence && asset.residential && asset.side == 2 && std::max(asset.w,asset.d) > 12)
                mayorAssets.push_back(asset);
        }
        std::stable_sort(mayorAssets.begin(),mayorAssets.end(),[](const auto&a,const auto&b){return a.w*a.d>b.w*b.d;});
        if(mayorAssets.empty()){error="Wealth 2.0 requires a large residential asset with a south-facing exterior door";return false;}
    }
    const bool frontOnlyBuildings = sideKeep == 0 ||
        std::all_of(assets.begin(), assets.end(), [](const auto& asset) {
            return asset.side == 0 || asset.side == 2;
        });
    std::shuffle(assets.begin(),assets.end(),rng);
    // Include compact alternatives in the candidate search, so an early
    // large manor cannot consume all frontage needed by the requested homes.
    if(attempt%3==2) std::stable_sort(assets.begin(),assets.end(),[](const Asset& a,const Asset& b){return a.w*a.d<b.w*b.d;});
    std::vector<int> rank(area,0),roadId(area,-1),occupied(area,0);
    std::vector<MapPlanRegion> bridges;
    std::vector<unsigned char> bridgeEnvelope(area,0);
    for(auto r:plan.regions)if(r.kind=="asset_bridge"){
        r.parentId=-1;r.parentIndex=-1;bridges.push_back(r);
        for(int y=std::max(0,r.y0-2);y<=std::min(d-1,r.y1+2);++y)
            for(int x=std::max(0,r.x0-2);x<=std::min(w-1,r.x1+2);++x)bridgeEnvelope[y*w+x]=1;
    }
    auto ground=[&](int x,int y) {
        if(x<1||y<1||x>=w-1||y>=d-1) return false;
        const auto b=plan.biomes[y*w+x];
        return b==PLAIN||b==DESERT||b==FOREST||b==ROAD||b==BRIDGE||b==TOWN_PAVEMENT;
    };
    const bool forest = plan.settlementSite == "forest";
    // Forest openings are cut by the selected main roads, never inherited
    // from the terrain planner's historical approach paths.
    if (forest) for(int y=0;y<d;++y) for(int x=0;x<w;++x)
        if(x<4||y<4||x>=w-4||y>=d-4) plan.biomes[y*w+x]=TOWN_TREES;
    auto square=[&](int x,int y,int width,bool developed,bool clearing=false) {
        const int margin=clearing?0:1;
        if(x<margin||y<margin||x+width>w-margin||y+width>d-margin) return false;
        for(int yy=y;yy<y+width;++yy) for(int xx=x;xx<x+width;++xx)
            if(occupied[yy*w+xx] || (!ground(xx,yy)&&!(clearing&&plan.biomes[yy*w+xx]==TOWN_TREES)) ||
               (developed&&!plan.settlementMask.empty()&&!plan.settlementMask[yy*w+xx])) return false;
        return true;
    };
    // Waterfront settlements grow from a dry square touching the shoreline.
    std::vector<int> waterDistance(area,w+d);
    std::queue<int> water;
    for(int i=0;i<area;++i) if(plan.biomes[i]==RIVER||plan.biomes[i]==LAKE||plan.biomes[i]==TOWN_SEA) {
        waterDistance[i]=0;water.push(i);
    }
    while(!water.empty()) {
        int i=water.front();water.pop();
        for(Point p: {Point{i%w-1,i/w},Point{i%w+1,i/w},Point{i%w,i/w-1},Point{i%w,i/w+1}})
            if(p.x>=0&&p.y>=0&&p.x<w&&p.y<d && waterDistance[p.y*w+p.x]>waterDistance[i]+1) {
                waterDistance[p.y*w+p.x]=waterDistance[i]+1;water.push(p.y*w+p.x);
            }
    }
    const bool waterfront=plan.settlementSite=="oasis" || plan.settlementSite=="lake" ||
        plan.settlementSite=="river" || plan.settlementSite=="coast";
    const int plazaSize=get("settlementPlazaSize",mainWidth+(urban?8:4)+(waterfront&&mainCount==4?mainWidth:0));
    if (plazaSize < 2 || plazaSize > std::min(w, d)) {
        error = "Settlement plaza size must be between 2 and the shorter map dimension";
        return false;
    }
    Point center{-1,-1}; double best=-1e20;
    auto findCenter=[&](){
    for(int y=plazaSize;y<d-plazaSize;y+=(waterfront?1:2)) for(int x=plazaSize;x<w-plazaSize;x+=(waterfront?1:2)) {
        if(!square(x-plazaSize/2,y-plazaSize/2,plazaSize,true)) continue;
        bool bridgeOverlap=false;
        for(int yy=y-plazaSize/2;yy<y-plazaSize/2+plazaSize;++yy)
            for(int xx=x-plazaSize/2;xx<x-plazaSize/2+plazaSize;++xx)bridgeOverlap|=bridgeEnvelope[yy*w+xx]!=0;
        if(bridgeOverlap)continue;
        if(get("preserveSettlementRoads",0)) {
            bool seedRoad=false;
            for(int yy=y-plazaSize/2;yy<y-plazaSize/2+plazaSize;++yy)for(int xx=x-plazaSize/2;xx<x-plazaSize/2+plazaSize;++xx)
                seedRoad|=plan.biomes[yy*w+xx]==ROAD || plan.biomes[yy*w+xx]==TOWN_PAVEMENT;
            if(!seedRoad)continue;
        }
        if(waterfront) {
            int shore=w+d;
            const int left=x-plazaSize/2,top=y-plazaSize/2;
            for(int k=0;k<plazaSize;++k) {
                shore=std::min({shore,waterDistance[top*w+left+k],waterDistance[(top+plazaSize-1)*w+left+k],
                    waterDistance[(top+k)*w+left],waterDistance[(top+k)*w+left+plazaSize-1]});
            }
            if(shore!=1)continue;
        }
        int room=0;
        for(int yy=std::max(1,y-20);yy<std::min(d-1,y+20);yy+=2)
            for(int xx=std::max(1,x-20);xx<std::min(w-1,x+20);xx+=2)
                if(ground(xx,yy)&&(plan.settlementMask.empty()||plan.settlementMask[yy*w+xx])) ++room;
        const double score=room*.35-(std::abs(x-w/2)+std::abs(y-d/2))*.6-
            std::min(40,std::abs(waterDistance[y*w+x]-12))*.3+std::uniform_real_distribution<double>(0,8)(rng);
        if(score>best) {best=score;center={x,y};}
    }
    };
    findCenter();
    if(center.x<0 && waterfront && !plan.settlementMask.empty()) {
        // A shorter inlet can leave the historic clearing inland. Extend it
        // over connected dry ground so the plaza can still touch the bank.
        std::queue<int> fringe;
        std::vector<int> steps(area,0);
        for(int i=0;i<area;++i) {
            if(!plan.settlementMask[i] || !ground(i%w,i/w)) continue;
            if((i%w>0 && !plan.settlementMask[i-1]) ||
                (i%w+1<w && !plan.settlementMask[i+1]) ||
                (i>=w && !plan.settlementMask[i-w]) ||
                (i+w<area && !plan.settlementMask[i+w])) fringe.push(i);
        }
        while(!fringe.empty()) {
            const int i=fringe.front();fringe.pop();
            if(steps[i]>=plazaSize*2) continue;
            for(const Point delta:{Point{-1,0},Point{1,0},Point{0,-1},Point{0,1}}) {
                const int x=i%w+delta.x,y=i/w+delta.y;
                if(!ground(x,y) || plan.settlementMask[y*w+x]) continue;
                const int next=y*w+x;
                plan.settlementMask[next]=1;
                steps[next]=steps[i]+1;
                fringe.push(next);
            }
        }
        findCenter();
    }
    if(center.x<0) {error="No dry village center can fit the requested main-road width";return false;}
    plan.regions.clear();plan.markers.clear();
    const int px=center.x-plazaSize/2,py=center.y-plazaSize/2;
    auto inPlaza=[&](int x,int y) {return x>=px&&y>=py&&x<px+plazaSize&&y<py+plazaSize;};
    for(int y=py;y<py+plazaSize;++y) for(int x=px;x<px+plazaSize;++x) {
        plan.biomes[y*w+x]=urban?TOWN_PAVEMENT:ROAD; rank[y*w+x]=mainWidth;
    }
    plan.plazaEnabled=true;plan.plazaStyle=urban?"market_square":"founding_square";
    plan.regions.push_back({"village_center",0,px,py,px+plazaSize-1,py+plazaSize-1});
    plan.regions.back().growthStage=0;
    // Centre remains index zero for civic parent links; retained crossings follow it.
    plan.regions.insert(plan.regions.end(),bridges.begin(),bridges.end());
    plan.markers.push_back({"village_center",center.x,center.y,0});
    std::vector<std::vector<Point>> mainPaths;
    auto paint=[&](const std::vector<Point>& path,int width,int id) {
        int x0=w,y0=d,x1=0,y1=0;
        for(Point p:path) for(int dy=0;dy<width;++dy) for(int dx=0;dx<width;++dx) {
            int x=p.x+dx,y=p.y+dy,i=y*w+x;
            x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);
            if(inPlaza(x,y)) continue;
            if(rank[i]<width) {rank[i]=width;roadId[i]=id;}
            if(plan.biomes[i]==RIVER) plan.biomes[i]=BRIDGE;
            else if(plan.biomes[i]!=BRIDGE) plan.biomes[i]=ROAD;
        }
        plan.regions.push_back({width==mainWidth?"village_main_road":"village_middle_road",id,x0,y0,x1,y1});
        auto& r=plan.regions.back();r.width=width;r.growthStage=width==mainWidth?1:2;
    };
    if(get("preserveSettlementRoads",0)) {
        const auto inherited=plan.biomes;
        for(int y=1;y<d-1;++y)for(int x=1;x<w-1;++x)if(inherited[y*w+x]==ROAD||inherited[y*w+x]==TOWN_PAVEMENT) {
            rank[y*w+x]=std::max(rank[y*w+x],base);
            const int sx=x-middleWidth/2,sy=y-middleWidth/2;
            if(square(sx,sy,middleWidth,true))for(int yy=sy;yy<sy+middleWidth;++yy)for(int xx=sx;xx<sx+middleWidth;++xx){
                rank[yy*w+xx]=std::max(rank[yy*w+xx],middleWidth);roadId[yy*w+xx]=0;
                if(!inPlaza(xx,yy))plan.biomes[yy*w+xx]=ROAD;
            }
        }
    }
    // Build a road BEFORE any houses. Wide-road footprints are checked at
    // every search node, so a centerline cannot squeeze through a narrow gap.
    auto route=[&](Point start,Point dir,int width,bool major,int maxLength) {
        std::vector<Point> path;
        if(!square(start.x,start.y,width,!major,major&&forest)) return path;
        const int parentWidth = std::max(width, rank[start.y*w+start.x]);
        // A river crossing needs dry full-width land at BOTH ends. Probe in
        // the street's direction, limiting bridge spans to 24 cells; lakes,
        // sea and roads running along the river never qualify.
        auto riverSquare=[&](int x,int y) {
            if(x<1||y<1||x+width>=w||y+width>=d) return false;
            for(int yy=y;yy<y+width;++yy) for(int xx=x;xx<x+width;++xx)
                if(!ground(xx,yy)&&plan.biomes[yy*w+xx]!=RIVER) return false;
            return true;
        };
        std::vector<signed char> crossingAllowed(area,-1);
        auto crossing=[&](int x,int y) {
            if(!major || !riverSquare(x,y)) return false;
            int i=y*w+x;
            if(crossingAllowed[i]>=0) return crossingAllowed[i]!=0;
            bool both=true;
            for(int sign : {-1,1}) {
                bool bank=false;
                for(int k=1;k<=24;++k) {
                    int xx=x+dir.x*k*sign, yy=y+dir.y*k*sign;
                    if(square(xx,yy,width,false)) {bank=true;break;}
                    if(!riverSquare(xx,yy)) break;
                }
                both=both&&bank;
            }
            crossingAllowed[i]=both?1:0;
            return both;
        };
        std::vector<int> prev(area,-1),distance(area,0);
        std::queue<int> q; const int s=start.y*w+start.x;
        q.push(s);prev[s]=s;int goal=-1;double goalScore=0;
        while(!q.empty()) {
            int i=q.front();q.pop();Point p{i%w,i/w};
            const int forward=(p.x-start.x)*dir.x+(p.y-start.y)*dir.y;
            const int sideways=std::abs((p.x-start.x)*dir.y-(p.y-start.y)*dir.x);
            const double score=waterfront?std::max(forward-sideways*.55,sideways*.25+forward*.1):forward-sideways*.55;
            const bool reachesEdge = !forest || !major ||
                (dir.x<0&&p.x==0)||(dir.x>0&&p.x+width==w)||
                (dir.y<0&&p.y==0)||(dir.y>0&&p.y+width==d);
            if(reachesEdge && square(p.x,p.y,width,!major,major&&forest) && (forward>=width*2 || (waterfront&&sideways>=width*2)) && score>goalScore) {goal=i;goalScore=score;}
            if(distance[i]>=maxLength) continue;
            for(Point step: {Point{0,-1},Point{1,0},Point{0,1},Point{-1,0}}) {
                Point n{p.x+step.x,p.y+step.y};
                if(!square(n.x,n.y,width,!major,major&&forest) && !crossing(n.x,n.y)) continue;
                const int ni=n.y*w+n.x;
                if(prev[ni]>=0) continue;
                const int f=(n.x-start.x)*dir.x+(n.y-start.y)*dir.y;
                if(f < -width) continue;
                bool blocked=false;
                if(major) for(int yy=n.y;yy<n.y+width&&!blocked;++yy) for(int xx=n.x;xx<n.x+width;++xx)
                    if(rank[yy*w+xx] && !inPlaza(xx,yy)) {blocked=true;break;}
                // A middle street leaves its parent; it must not travel
                // sideways along an existing road and merely relabel it.
                if(!major && f>parentWidth && rank[ni]>=middleWidth) blocked=true;
                if(blocked) continue;
                prev[ni]=i;distance[ni]=distance[i]+1;q.push(ni);
            }
        }
        if(goal<0) return path;
        for(int i=goal;;i=prev[i]) {path.push_back({i%w,i/w});if(i==s)break;}
        std::reverse(path.begin(),path.end());return path;
    };
    const Point dirs[4]={{0,-1},{1,0},{0,1},{-1,0}};
    const int turn=static_cast<int>(rng()%4);
    std::vector<int> directions;
    for(int i=0;i<mainCount;++i) directions.push_back((turn+(mainCount==2?i*2:i))%4);
    int id=1;
    for(int direction:directions) {
        const Point dir=dirs[direction];
        Point start{center.x-mainWidth/2,center.y-mainWidth/2};
        if(dir.x>0)start.x=px+plazaSize-mainWidth;
        if(dir.x<0)start.x=px;
        if(dir.y>0)start.y=py+plazaSize-mainWidth;
        if(dir.y<0)start.y=py;
        auto path=route(start,dir,mainWidth,true,w+d);
        if(path.empty()) {error="Requested main roads cannot leave the village center on this terrain";return false;}
        paint(path,mainWidth,id++);mainPaths.push_back(path);
        auto end=path.back();plan.markers.push_back({"village_outbound_road",end.x,end.y,id-1});
    }
    // A village without outward trading routes still needs local access.
    // These streets stay within the developed clearing and create no exits.
    if(mainCount==0) for(int i=0;i<4;++i) {
        const Point dir=dirs[(turn+i)%4];
        Point start{center.x-middleWidth/2,center.y-middleWidth/2};
        if(dir.x>0)start.x=px+plazaSize-middleWidth;
        if(dir.x<0)start.x=px;
        if(dir.y>0)start.y=py+plazaSize-middleWidth;
        if(dir.y<0)start.y=py;
        auto path=route(start,dir,middleWidth,false,std::max(24,std::min(w,d)/3));
        if(!path.empty()) paint(path,middleWidth,id++);
    }
    // As households increase, side streets open from the trading routes.
    // Each has room for several frontages rather than one route per house.
    int branchBudget=std::min(static_cast<int>(mainPaths.size())*2,std::max(0,(target-4)/3));
    for(std::size_t arm=0;arm<mainPaths.size() && branchBudget>0;++arm) {
        const auto& path=mainPaths[arm]; const Point axis=dirs[directions[arm]];
        for(int branch=0;branch<2 && branchBudget>0;++branch) {
            Point start=path[std::min(path.size()-1,path.size()*static_cast<std::size_t>(branch+1)/3)];
            const Point side=branch==0?Point{-axis.y,axis.x}:Point{axis.y,-axis.x};
            auto middle=route(start,side,middleWidth,false,std::max(24,std::min(w,d)/3));
            if(!middle.empty()) {paint(middle,middleWidth,id++);--branchBudget;}
        }
    }
    int placed=0,openLots=0;
    std::map<int,int> facilityCounts;
    const int openLotBudget=std::max(1,(target+3)/4);
    bool needsMayor=!mayorAssets.empty(),civicReady=!civicEnabled;
    struct CivicFront {int x,y,side,width;double distance;};
    auto civicFronts=[&](){
        std::vector<CivicFront> result;
        for(int y=3;y<d-3;++y)for(int x=3;x<w-3;++x){
            const int width=rank[y*w+x];if(width<middleWidth)continue;
            if(std::hypot(x-center.x,y-center.y)>33)continue;
            for(int side=0;side<4;++side){const auto n=dirs[side];
                const double distance=std::hypot(x-(side==3)-center.x,y-(side==0)-center.y);if(distance>32)continue;
                if(rank[(y-n.y)*w+x-n.x])continue;
                if(rank[(y+(side%2))*w+x+(side%2==0)]!=width)continue;
                result.push_back({x,y,side,width,distance});
            }
        }
        std::stable_sort(result.begin(),result.end(),[](const auto&a,const auto&b){return a.distance<b.distance;});
        return result;
    };
    auto reserveCivicLot=[&](const CivicFront& f,int width,int depth,int doorX,int doorY,MapPlanRegion region,bool house){
        int x=f.x-doorX,y=f.y-doorY;
        if(f.side==0)y=f.y+3;if(f.side==1)x=f.x-width-2;
        if(f.side==2)y=f.y-depth-2;if(f.side==3)x=f.x+3;
        if(x<3||y<3||x+width+3>=w||y+depth+3>=d)return false;
        for(int yy=y-2;yy<y+depth+2;++yy)for(int xx=x-2;xx<x+width+2;++xx){
            const int at=yy*w+xx;
            if(occupied[at]||bridgeEnvelope[at]||rank[at]||!ground(xx,yy)||(!plan.settlementMask.empty()&&!plan.settlementMask[at]))return false;
        }
        int strongest=0;
        for(int yy=y-3;yy<=y+depth+2;++yy)for(int xx=x-3;xx<=x+width+2;++xx)strongest=std::max(strongest,rank[yy*w+xx]);
        if(strongest>f.width)return false;
        for(int yy=y-2;yy<y+depth+2;++yy)for(int xx=x-2;xx<x+width+2;++xx){
            occupied[yy*w+xx]=1;
            plan.biomes[yy*w+xx]=static_cast<std::uint8_t>(urban?TOWN_STONE_FLOOR:(plan.settlementSite=="oasis"?DESERT:PLAIN));
        }
        if(house)for(int yy=y;yy<y+depth;++yy)for(int xx=x;xx<x+width;++xx)plan.biomes[yy*w+xx]=TOWN_WOOD_FLOOR;
        const int dx=x+doorX,dy=y+doorY;
        if(f.side%2==0){
            const int from=f.side==0?f.y:y+depth,to=f.side==0?y-1:f.y;
            for(int yy=from;yy<=to;++yy)for(int k=0;k<2;++k)plan.biomes[yy*w+dx+k]=ROAD;
        }else{
            const int from=f.side==3?f.x:x+width,to=f.side==3?x-1:f.x;
            for(int xx=from;xx<=to;++xx)for(int k=0;k<2;++k)plan.biomes[(dy+k)*w+xx]=ROAD;
        }
        region.x0=x;region.y0=y;region.x1=x+width-1;region.y1=y+depth-1;
        region.parentIndex=0;region.parentId=0;region.growthStage=0;region.frontageWidth=f.width;
        if(house){region.doorX=dx;region.doorY=dy;}
        const int index=static_cast<int>(plan.regions.size());plan.regions.push_back(region);
        const int ax=f.x-(f.side==3),ay=f.y-(f.side==0);
        MapPlanMarker access{house?"asset_approach":"settlement_facility_access",ax,ay,region.id};
        access.regionIndex=index;access.regionId=region.id;access.role=house?"village_head":region.role;plan.markers.push_back(access);
        if(house){
            MapPlanMarker home{"settlement_mayor",ax,ay,region.id};
            home.role="village_head";home.regionIndex=index;home.regionId=region.id;plan.markers.push_back(home);
        }
        return true;
    };
    auto prepareCivic=[&](){
        if(civicReady)return true;
        if(needsMayor){
            const auto fronts=civicFronts();
            for(const auto&a:mayorAssets){
                for(const auto&f:fronts){
                    if(f.side!=2)continue;
                    MapPlanRegion house{"asset_plot",placed};
                    house.role="MA"+std::string(a.id<10?"00":a.id<100?"0":"")+std::to_string(a.id);
                    house.rotation=a.rotation;house.residentProfile="village_head";house.livingArrangement="mayors_residence";
                    if(!reserveCivicLot(f,a.w,a.d,a.doorX,a.doorY,house,true))continue;
                    ++placed;++facilityCounts[a.limitGroup];needsMayor=false;break;
                }
                if(!needsMayor)break;
            }
            if(needsMayor)return false;
        }
        // All requested equipment is reserved as a unit, before ordinary
        // homes. Failed street trials must not leave half a civic centre.
        const auto beforePlan=plan;const auto beforeOccupied=occupied;
        const auto fronts=civicFronts();int number=0;
        for(const auto&object:settlement_civic::Program(params,wealth)){
            bool fitted=false;
            for(const auto&f:fronts){
                if(f.distance>24)continue;
                const int rotation=(f.side+2)%4;
                const int width=rotation%2?object.d:object.w,depth=rotation%2?object.w:object.d;
                const int doorX=f.side==1?width-2:f.side==3?0:std::max(0,(width-2)/2);
                const int doorY=f.side==0?0:f.side==2?depth-2:std::max(0,(depth-2)/2);
                MapPlanRegion facility{"settlement_facility",90000+number};facility.role=object.role;
                facility.rotation=rotation;facility.objectType=object.type;facility.objectVariant=object.variant;facility.objectHeight=object.h;
                if(!reserveCivicLot(f,width,depth,doorX,doorY,facility,false))continue;
                fitted=true;++number;break;
            }
            if(!fitted){plan=beforePlan;occupied=beforeOccupied;return false;}
        }
        civicReady=true;return true;
    };
    auto populateFrontages=[&]() {
    struct Front { int x,y,side,width;double score; };
    std::vector<Front> fronts;
    for(int y=3;y<d-3;++y) for(int x=3;x<w-3;++x) {
        const int r=rank[y*w+x]; if(r<middleWidth||inPlaza(x,y))continue;
        // Door side points from building to street. The two street cells
        // beneath the opening must both belong to that street hierarchy.
        for(int side=0;side<4;++side) {
            const Point n=dirs[side];
            const int outside=(y-n.y)*w+x-n.x;
            if(rank[outside])continue;
            const int neighbor=(side%2==0)?y*w+x+1:(y+1)*w+x;
            if(rank[neighbor]!=r)continue;
            const double dist=std::hypot(x-center.x,y-center.y);
            fronts.push_back({x,y,side,r,dist+(r==mainWidth?0:30)+std::uniform_real_distribution<double>(0,3)(rng)});
        }
    }
    std::sort(fronts.begin(),fronts.end(),[](const Front&a,const Front&b){return a.score<b.score;});
    for(const Front& f:fronts) {
        if(placed>=target)break;
        for(std::size_t ai=0;ai<assets.size();++ai) {
            const Asset& a=assets[(ai+static_cast<std::size_t>(placed))%assets.size()];
            if(a.side!=f.side)continue;
            int x=f.x-a.doorX,y=f.y-a.doorY;
            if(f.side==0)y=f.y+3;
            if(f.side==1)x=f.x-a.w-2;
            if(f.side==2)y=f.y-a.d-2;
            if(f.side==3)x=f.x+3;
            // One stable draw per frontage, independent of asset alternatives
            // and branch retries: trying another church rotation cannot reroll
            // the orientation probability or evade the shared facility cap.
            std::uint32_t draw=plan.seed ^ (static_cast<std::uint32_t>(f.x)*73856093U) ^
                (static_cast<std::uint32_t>(f.y)*19349663U) ^ (static_cast<std::uint32_t>(f.side)*83492791U);
            draw^=draw>>16;draw*=0x7feb352dU;draw^=draw>>15;draw*=0x846ca68bU;draw^=draw>>16;
            const int keep=a.side==0?northKeep:(a.side%2?sideKeep:100);
            const bool openLot=draw%100>=static_cast<unsigned>(keep);
            if(openLot && openLots>=openLotBudget)continue;
            if(!openLot && a.maxCount>0 && facilityCounts[a.limitGroup]>=a.maxCount)continue;
            if(x<3||y<3||x+a.w+3>=w||y+a.d+3>=d)continue;
            bool fits=true;
            for(int yy=y-2;yy<y+a.d+2&&fits;++yy) for(int xx=x-2;xx<x+a.w+2;++xx) {
                const int i=yy*w+xx;
                if(occupied[i]||bridgeEnvelope[i]||rank[i]||!ground(xx,yy)||(!plan.settlementMask.empty()&&!plan.settlementMask[i])) {fits=false;break;}
            }
            if(!fits)continue;
            int strongest=0;
            for(int yy=y-3;yy<=y+a.d+2;++yy) for(int xx=x-3;xx<=x+a.w+2;++xx)
                strongest=std::max(strongest,rank[yy*w+xx]);
            if(strongest>f.width)continue;
            for(int yy=y-2;yy<y+a.d+2;++yy) for(int xx=x-2;xx<x+a.w+2;++xx) {
                occupied[yy*w+xx]=1;
                plan.biomes[yy*w+xx]=static_cast<uint8_t>(urban?TOWN_STONE_FLOOR:(plan.settlementSite=="oasis"?DESERT:PLAIN));
            }
            for(int yy=y;yy<y+a.d;++yy) for(int xx=x;xx<x+a.w;++xx) plan.biomes[yy*w+xx]=TOWN_WOOD_FLOOR;
            const int dx=x+a.doorX,dy=y+a.doorY;
            // Exact door projection, including recessed/off-center entrances.
            if(f.side%2==0) {
                const int from=f.side==0?f.y:y+a.d;
                const int to=f.side==0?y-1:f.y;
                for(int yy=from;yy<=to;++yy) for(int k=0;k<2;++k) plan.biomes[yy*w+dx+k]=ROAD;
            } else {
                const int from=f.side==3?f.x:x+a.w;
                const int to=f.side==3?x-1:f.x;
                for(int xx=from;xx<=to;++xx) for(int k=0;k<2;++k) plan.biomes[(dy+k)*w+xx]=ROAD;
            }
            const int lotId=openLot?target+openLots:placed;
            MapPlanRegion r{openLot?"village_open_space":"asset_plot",lotId,x,y,x+a.w-1,y+a.d-1};
            r.role="MA"+std::string(a.id<10?"00":(a.id<100?"0":""))+std::to_string(a.id);
            r.rotation=a.rotation;r.doorX=dx;r.doorY=dy;r.frontageWidth=f.width;
            r.growthStage=f.width==mainWidth?2:3;
            if(openLot){
                const unsigned use=(plan.seed+static_cast<unsigned>(lotId))%3;
                r.role=use==0?"farm":use==1?"park":"vacant_lot";
                r.rotation=0;r.doorX=r.doorY=-1;
                for(int yy=y-2;yy<y+a.d+2;++yy)for(int xx=x-2;xx<x+a.w+2;++xx){
                    auto& b=plan.biomes[yy*w+xx];
                    if(b==TOWN_WOOD_FLOOR||b==TOWN_STONE_FLOOR)b=static_cast<std::uint8_t>(plan.settlementSite=="oasis"?DESERT:PLAIN);
                }
            }
            plan.regions.push_back(r);
            int ax=f.x,ay=f.y;
            if(f.side==0)--ay;
            if(f.side==3)--ax;
            plan.markers.push_back({openLot?"village_open_space_approach":"asset_approach",ax,ay,lotId});
            if(openLot)++openLots;else {++placed;++facilityCounts[a.limitGroup];}
            break;
        }
    }
    };
    if(prepareCivic())populateFrontages();
    // Grow the local street network only when the existing frontages are
    // exhausted. Trial streets that add no homes are rolled back completely.
    for(int wave=0;(placed<target||!civicReady) && wave<target+8;++wave) {
        struct Branch {Point start,dir;double score;};
        std::vector<Branch> branches;
        auto offer=[&](Point p,Point dir) {
            if(!square(p.x,p.y,middleWidth,true)) return;
            int free=0;
            for(int f=middleWidth;f<=32;f+=4) for(int s=-16;s<=16;s+=4) {
                int x=p.x+dir.x*f-dir.y*s,y=p.y+dir.y*f+dir.x*s;
                if(x>2&&y>2&&x<w-3&&y<d-3 && ground(x,y) && !rank[y*w+x] && !occupied[y*w+x] &&
                   (plan.settlementMask.empty()||plan.settlementMask[y*w+x])) ++free;
            }
            if(free<8) return;
            branches.push_back({p,dir,free-std::hypot(p.x-center.x,p.y-center.y)*.25+
                std::uniform_real_distribution<double>(0,2)(rng)});
        };
        for(Point dir:dirs) {
            Point p{center.x-middleWidth/2,center.y-middleWidth/2};
            if(dir.x>0)p.x=px+plazaSize-middleWidth;
            if(dir.x<0)p.x=px;
            if(dir.y>0)p.y=py+plazaSize-middleWidth;
            if(dir.y<0)p.y=py;
            offer(p,dir);
        }
        for(int y=3;y<d-middleWidth-3;++y) for(int x=3;x<w-middleWidth-3;++x) {
            if((x+y)%8 || inPlaza(x,y) || rank[y*w+x]<middleWidth) continue;
            bool publicSquare=true;
            for(int yy=y;yy<y+middleWidth && publicSquare;++yy) for(int xx=x;xx<x+middleWidth;++xx)
                if(rank[yy*w+xx]<middleWidth) {publicSquare=false;break;}
            if(publicSquare) for(Point dir:dirs) offer({x,y},dir);
        }
        std::sort(branches.begin(),branches.end(),[](const Branch& a,const Branch& b){return a.score>b.score;});
        bool grew=false;
        for(std::size_t trial=0;trial<branches.size() && trial<32;++trial) {
            const auto& b=branches[trial];
            auto path=route(b.start,b.dir,middleWidth,false,std::max(32,std::min(w,d)/2));
            if(path.empty()) continue;
            auto oldPlan=plan;auto oldRank=rank;auto oldRoadId=roadId;auto oldOccupied=occupied;auto oldRng=rng;
            auto oldFacilityCounts=facilityCounts;
            const bool oldNeedsMayor=needsMayor,oldCivicReady=civicReady;
            const int before=placed;
            const int beforeOpenLots=openLots;
            int newRoads=1;
            // Front-only houses need east/west frontage. A north/south
            // connector must open its cross street in the same trial;
            // rolling it back for having no side-facing houses prevents
            // the settlement from ever reaching the next residential row.
            if(frontOnlyBuildings && b.dir.y && path.size()>static_cast<std::size_t>(middleWidth*3))
                path.resize(std::min(path.size(),static_cast<std::size_t>(25)));
            paint(path,middleWidth,id);
            if(frontOnlyBuildings && b.dir.y){
                for(const auto& dir:{Point{-1,0},Point{1,0}}){
                    auto cross=route(path.back(),dir,middleWidth,false,std::max(32,std::min(w,d)/2));
                    if(!cross.empty())paint(cross,middleWidth,id+newRoads++);
                }
            }
            if(prepareCivic())populateFrontages();
            if(placed>before||(!oldCivicReady&&civicReady)) {id+=newRoads;grew=true;break;}
            plan=std::move(oldPlan);rank=std::move(oldRank);roadId=std::move(oldRoadId);
            occupied=std::move(oldOccupied);rng=oldRng;
            facilityCounts=std::move(oldFacilityCounts);
            needsMayor=oldNeedsMayor;civicReady=oldCivicReady;
            openLots=beforeOpenLots;
        }
        if(!grew) break;
    }
    if(needsMayor||!civicReady){error=needsMayor?"No large south-facing mayor residence fits near the centre":"No connected civic equipment sites fit near the settlement centre";return false;}
    if(!placed) {error="No door-aligned building frontages fit this village; increase map size or reduce road width";return false;}
    if(urban) {
        const bool fullyPaved=plan.cityType=="paved_city";
        for(int i=0;i<area;++i) if(plan.biomes[i]==ROAD && (fullyPaved || rank[i]>=mainWidth))
            plan.biomes[i]=TOWN_PAVEMENT;
    }
    if(get("wallHeight",0)>0 && !plan.settlementMask.empty()) {
        for(int y=1;y<d-1;++y) for(int x=1;x<w-1;++x) {
            const int i=y*w+x;
            if(!plan.settlementMask[i]||rank[i]||occupied[i]||bridgeEnvelope[i]||!ground(x,y))continue;
            bool boundary=false;
            for(Point n:dirs) if(!plan.settlementMask[(y+n.y)*w+x+n.x])boundary=true;
            if(boundary)plan.biomes[i]=WALL;
        }
    }
    plan.selectionReason=urban?"town_growth":"village_growth";
    double distanceToCenter=0;
    for(const auto&r:plan.regions) if(r.kind=="asset_plot")
        distanceToCenter+=std::hypot((r.x0+r.x1)*.5-center.x,(r.y0+r.y1)*.5-center.y);
    plan.qualityScore=placed*1000.0-distanceToCenter*.1;
    return true;
}

inline bool Generate(MapPlan& plan,const MapGenParams& params,std::string& error) {
    const auto it=params.extra.find("candidateCount");
    const int count=it==params.extra.end()?6:std::clamp(static_cast<int>(it->second),1,12);
    MapPlan best;double score=-std::numeric_limits<double>::infinity();
    for(int i=0;i<count;++i) {
        MapPlan candidate=plan;
        if(!Grow(candidate,params,i,error))continue;
        if(candidate.qualityScore>score) {score=candidate.qualityScore;best=std::move(candidate);}
    }
    if(!std::isfinite(score))return false;
    plan=std::move(best);
    return true;
}
}
