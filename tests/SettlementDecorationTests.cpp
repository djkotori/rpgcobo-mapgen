#include "SettlementDecoration.h"
#include <iostream>
#include <set>
#include <stdexcept>
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::set<int> Flowers(const MapPlan& p) {
    std::set<int> cells;
    for(const auto& r:p.regions)if(r.kind=="settlement_flower")cells.insert(r.y0*p.width+r.x0);
    return cells;
}
int main(){try {
    for(const std::string type:{"village","town"}) {
        MapPlan base;base.Reset(80,80,312,type);base.biomes.assign(6400,0);
        base.settlementMask.assign(6400,1);base.heightOffsets.assign(6400,0);base.shoreCuts.assign(6400,0);
        for(int y=2;y<78;++y)for(int x=38;x<=41;++x)base.biomes[y*80+x]=5;
        base.regions.push_back({"asset_plot",0,25,25,35,35});
        base.regions.push_back({"village_open_space",1,15,45,30,60});
        base.regions.back().growthStage=3;
        base.markers.push_back({"asset_approach",38,30,0});
        // Wet banks and undeveloped land must stay bare, even beside roads.
        for(int y=2;y<15;++y)for(int x=35;x<=44;++x)base.shoreCuts[y*80+x]=1;
        for(int y=65;y<78;++y)for(int x=35;x<=44;++x)base.settlementMask[y*80+x]=0;
        auto poor=base,middle=base,rich=base;
        ApplySettlementDecoration(poor,.7);ApplySettlementDecoration(middle,1.5);ApplySettlementDecoration(rich,2.5);
        auto custom=base;
        custom.regions[1].role="park";
        MapGenParams models;
        models.extra={{"settlementProp_bench_width",5},{"settlementProp_bench_depth",3}};
        ApplySettlementDecoration(custom,2.5,models);
        const auto customBench=std::find_if(custom.regions.begin(),custom.regions.end(),
            [](const auto& region){return region.kind=="settlement_prop" && region.role=="bench";});
        Require(customBench!=custom.regions.end() && customBench->x1-customBench->x0+1==5 &&
            customBench->y1-customBench->y0+1==3,
            "Live bench dimensions were not reserved");
        Require(rich.regions[1].growthStage==3,"Garden style replaced the exported growth stage");
        const auto a=Flowers(poor),b=Flowers(middle),c=Flowers(rich);
        Require(a.empty()&&b.size()>10&&c.size()>b.size(),"Wealth did not increase flowers");
        auto threshold=base;
        ApplySettlementDecoration(threshold,.999);
        Require(Flowers(threshold).empty(),"Wealth below 1.0 must have no flowers");
        ApplySettlementDecoration(threshold,1.0);
        Require(!Flowers(threshold).empty(),"Wealth 1.0 must enable flowers");
        for(int i:b)Require(c.count(i)>0,"Richer settlement lost existing flower locations");
        for(int i:c) {
            const int x=i%80,y=i/80;
            Require(base.biomes[i]!=5,"Flower blocks a road");
            Require(base.shoreCuts[i]==0&&base.settlementMask[i],"Flower on wet or undeveloped land");
            Require(!(x>=23&&x<=37&&y>=23&&y<=37),"Flower blocks a building envelope");
            Require(!(x>=35&&x<=41&&y>=27&&y<=33),"Flower blocks a door approach");
        }
        Require(rich.biomes==base.biomes&&rich.markers.size()==base.markers.size(),"Decorations changed navigation");
        ApplySettlementDecoration(rich,2.5);Require(Flowers(rich)==c,"Decorations are not deterministic/idempotent");
        ApplySettlementDecoration(rich,100);Require(rich.countryWealth==2.5,"Wealth upper limit ignored");
        ApplySettlementDecoration(rich,-1);Require(rich.countryWealth==.7&&Flowers(rich).empty(),"Wealth lower limit ignored");
        auto themed=base;
        themed.regions.push_back({"village_open_space",2,48,43,66,61});themed.regions.back().role="park";
        themed.regions.push_back({"village_open_space",3,10,43,28,61});themed.regions.back().role="farm";
        themed.settlementSite="oasis";std::fill(themed.biomes.begin(),themed.biomes.end(),1);
        for(int y=2;y<78;++y)for(int x=38;x<=41;++x)themed.biomes[y*80+x]=5;
        ApplySettlementDecoration(themed,2.5);
        Require(Flowers(themed).empty(),"Desert still has flowers");
        int cactus=0,bench=0,crops=0,palms=0;std::set<int> props;
        for(const auto& r:themed.regions) {
            if(r.kind=="settlement_crop"){++crops;Require(r.x0>=12&&r.x1<=26&&r.y0>=45&&r.y1<=59,"Crops escaped farm");}
            if(r.kind!="settlement_prop")continue;
            cactus+=r.role=="cactus";bench+=r.role=="bench";palms+=r.role=="palm";
            for(int y=r.y0;y<=r.y1;++y)for(int x=r.x0;x<=r.x1;++x){
                Require(themed.biomes[y*80+x]!=5,"Solid decoration blocks a road");
                Require(props.insert(y*80+x).second,"Solid decorations overlap");
            }
        }
        Require(cactus>0&&bench>0&&crops>0&&palms>0,"Desert, park or farm decoration missing");
        std::set<std::string> farmLayouts,parkLayouts;
        std::set<std::string> cropTypes;
        bool verticalBeds=false,horizontalBeds=false;
        for(unsigned seed=312;seed<316;++seed) {
            auto varied=base;varied.seed=seed;
            varied.regions.push_back({"village_open_space",2,48,43,66,61});varied.regions.back().role="park";
            varied.regions.push_back({"village_open_space",3,10,43,28,61});varied.regions.back().role="farm";
            ApplySettlementDecoration(varied,2.5);
            std::string farm,park;
            for(const auto& r:varied.regions) {
                if(r.kind=="settlement_crop") {
                    Require(r.x0>=12&&r.x1<=26&&r.y0>=45&&r.y1<=59,"Varied crop escaped farm");
                    cropTypes.insert(r.role);verticalBeds|=r.y1>r.y0;horizontalBeds|=r.x1>r.x0;
                    farm+=r.role+std::to_string(r.x0)+","+std::to_string(r.y0)+";";
                }
                if(r.kind=="settlement_prop"&&r.x0>=48&&r.x1<=66&&r.y0>=43&&r.y1<=61)
                    park+=r.role+std::to_string(r.x0)+","+std::to_string(r.y0)+";";
            }
            farmLayouts.insert(farm);parkLayouts.insert(park);
            auto repeat=varied;ApplySettlementDecoration(repeat,2.5);
            Require(repeat.regions.size()==varied.regions.size(),"Varied gardens are not idempotent");
        }
        Require(farmLayouts.size()==4&&parkLayouts.size()==4&&cropTypes.size()==3&&verticalBeds&&horizontalBeds,"Farm/park variations missing");
        auto woods=base;woods.settlementSite="forest";ApplySettlementDecoration(woods,2.5);
        Require(Flowers(woods).empty()&&std::any_of(woods.regions.begin(),woods.regions.end(),[](const auto& r){return r.kind=="settlement_plant"&&r.role=="mushroom";}),"Forest has no distinct understory");
        std::set<std::string> usedRoles;
        for(const std::string site:{"plains","coast","river","lake","mountain_valley","forest"}) {
            auto themedRoad=base;themedRoad.settlementSite=site;ApplySettlementDecoration(themedRoad,2.5);
            std::set<std::string> roles;
            for(const auto& r:themedRoad.regions)if(r.kind=="settlement_flower"||r.kind=="settlement_plant")roles.insert(r.role);
            Require(!roles.empty(),"Site has no roadside flora");
            for(const auto& role:roles)Require(usedRoles.insert(role).second,"Different sites share roadside flora");
            ApplySettlementDecoration(themedRoad,.999);
            Require(std::none_of(themedRoad.regions.begin(),themedRoad.regions.end(),[](const auto& r){return r.kind=="settlement_flower"||r.kind=="settlement_plant";}),"Roadside flora ignored wealth threshold");
        }
    }
    std::cout<<"PASS village/town wealth, deterministic flowers, roads, entrances, banks and bounds\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
