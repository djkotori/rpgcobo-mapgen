#include "CastleSettlement.h"
#include "VillageMapImageGenerator.h"
#include "TownMapImageGenerator.h"
#include "SettlementRelief.h"
#include <iostream>
#include <set>
#include <stdexcept>

void Require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
MapGenParams Params(double wealth,int site=0){
    MapGenParams p;p.width=p.depth=160;p.seed=4242;
    p.extra={{"assetFoundation",1},{"settlementCivicPlan",1},{"countryWealth",wealth},{"assetCount",6},
        {"assetPlotCount",12},{"assetRoadWidth",2},{"villageMainRoadCount",2},{"settlementSite",site},
        {"siteRotation",0},{"candidateCount",6}};
    for(int i=0;i<6;++i){const auto k="asset"+std::to_string(i);
        p.extra[k+"Id"]=i<4?0:i==4?5:2;p.extra[k+"Rotation"]=i<4?i:0;
        p.extra[k+"Width"]=i==4?26:i==5?14:12;p.extra[k+"Depth"]=i==4?17:12;
        p.extra[k+"Residential"]=1;p.extra[k+"LimitGroup"]=0;
        p.extra[k+"DoorSide"]=i<4?(2+i)%4:2;
        p.extra[k+"DoorX"]=i==1?0:i==3?10:i==4?10:5;
        p.extra[k+"DoorY"]=i==0||i==5?10:i==1||i==3?5:i==4?15:0;
    }return p;
}
void Check(const MapPlan& p,const MapGenParams& params){
    const auto centre=std::find_if(p.markers.begin(),p.markers.end(),[](const auto&m){return m.kind=="village_center";});
    Require(centre!=p.markers.end(),"Civic plan lost its founding centre");
    const auto expected=settlement_civic::Program(params,p.countryWealth);
    std::set<std::string> roles;int mayors=0;
    std::vector<unsigned char> blocked(p.width*p.depth,0);
    for(const auto&r:p.regions){
        if(r.kind=="settlement_facility"){
            Require(roles.insert(r.role).second,"Duplicate central equipment");
            Require(r.parentIndex>=0&&p.regions[r.parentIndex].kind=="village_center","Equipment lost central parent");
            for(int y=r.y0-2;y<=r.y1+2;++y)for(int x=r.x0-2;x<=r.x1+2;++x){
                Require(p.heightOffsets.empty()||p.heightOffsets[y*p.width+x]==0,"Equipment ground was uplifted");
                Require(p.shoreCuts.empty()||p.shoreCuts[y*p.width+x]==0,"Equipment ground was eroded");
            }
        }
        if(r.kind=="asset_plot"&&r.residentProfile=="village_head"){
            ++mayors;Require(r.rotation==0&&std::max(r.x1-r.x0+1,r.y1-r.y0+1)>12,"Mayor is not in a large south-facing residence");
            Require(r.role=="MA005"||r.role=="MA002","Mayor was assigned a shop or ordinary small house");
        }
        if(r.kind!="asset_plot"&&r.kind!="settlement_facility"&&r.kind!="castle_fixture"&&r.kind!="castle_portal")continue;
        for(int y=r.y0;y<=r.y1;++y)for(int x=r.x0;x<=r.x1;++x){
            Require(!blocked[y*p.width+x]||r.kind=="castle_portal","Civic equipment overlaps a house or furnishing");blocked[y*p.width+x]=1;
        }
    }
    Require(roles.size()==expected.size(),"Wealth changed the intended equipment count");
    for(const auto&o:expected)Require(roles.count(o.role),"Wealth tier equipment missing: "+o.role);
    Require(mayors==(p.countryWealth>=2.0?1:0),"Mayor residence ignored the exact wealth threshold");
    auto pass=[&](int x,int y){
        if(x<0||y<0||x+1>=p.width||y+1>=p.depth)return false;
        for(int yy=y;yy<y+2;++yy)for(int xx=x;xx<x+2;++xx)
            if(blocked[yy*p.width+xx]||!castle_interior::Floor(p.biomes[yy*p.width+xx]))return false;
        return true;
    };
    Require(pass(centre->x,centre->y),"Equipment blocks the settlement's centre");
    std::vector<unsigned char> seen(p.width*p.depth,0);std::queue<int> q;
    q.push(centre->y*p.width+centre->x);seen[q.front()]=1;
    while(!q.empty()){const int i=q.front();q.pop();for(auto step:{std::pair<int,int>{1,0},{-1,0},{0,1},{0,-1}}){
        const int x=i%p.width+step.first,y=i/p.width+step.second;
        if(pass(x,y)&&!seen[y*p.width+x]){seen[y*p.width+x]=1;q.push(y*p.width+x);}
    }}
    for(const auto&m:p.markers)if(m.kind=="asset_approach"||m.kind=="settlement_facility_access"||m.role=="keep_public_entry"){
        Require(seen[m.y*p.width+m.x],"A house, equipment or keep cannot be reached by a 2x2 human");
        if(m.kind=="settlement_facility_access"||m.role=="village_head")
            Require(std::hypot(m.x-centre->x,m.y-centre->y)<=(m.kind=="settlement_facility_access"?24:32),"Civic facility or mayor residence is too far from the centre");
    }
}
int main(){try{
    for(auto [wealth,count]:std::vector<std::pair<double,int>>{{.7,1},{.999,1},{1.0,2},{1.499,2},{1.5,3},{1.999,3},{2.0,5},{2.5,5}})
        Require(settlement_civic::Program(Params(wealth),wealth).size()==static_cast<std::size_t>(count),"Equipment boundary comparison is wrong");
    int layouts=0;
    for (const auto& [width, depth] : {std::pair<int,int>{31,32}, {32,31}, {-1,32}}) {
        auto params=Params(1.0);
        params.width=width;
        params.depth=depth;
        MapPlan p;
        p.Reset(64,64,1,"previous");
        std::string error;
        Require(!VillageMapImageGenerator(params).Generate(p,error) && error.find("32x32")!=std::string::npos,
            "Asset village accepted invalid dimensions");
        Require(p.type=="previous" && p.width==64,"Invalid dimensions reset the output plan");
    }
    for(const std::string type:{"village","town"})for(int site=0;site<7;++site)for(double wealth:{1.999,2.0}){
        auto params=Params(wealth,site);MapPlan p;std::string error;
        if (type=="village" && wealth>=2.0) {
            params.extra.erase("asset4LimitGroup");
            params.extra["asset5LimitGroup"]=1;
        }
        bool ok=type=="village"?VillageMapImageGenerator(params).Generate(p,error):TownMapImageGenerator(params).Generate(p,error);
        Require(ok,error);ok=BuildAssetFoundation(p,params,error);Require(ok,type+" site="+std::to_string(site)+": "+error);
        ApplySettlementRelief(p,4);ApplySettlementShore(p);ApplySettlementDecoration(p,wealth);Check(p,params);++layouts;
    }
    for(int mode=0;mode<5;++mode){auto params=Params(2.0);params.extra["castleMode"]=mode;
        MapPlan p;std::string error;const bool ok=GenerateCastleSettlement(p,params,error);
        Require(ok,"Castle mode="+std::to_string(mode)+": "+error);Check(p,params);++layouts;
    }
    std::cout<<"PASS "<<layouts<<" civic layouts: exact wealth thresholds, all sites, five castles, unique mayor and solid-aware 2x2 access\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
