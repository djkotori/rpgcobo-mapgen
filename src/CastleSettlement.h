#pragma once
#include "CastleMapImageGenerator.h"
#include "VillageGrowth.h"
#include "SettlementDecoration.h"
#include "AssetFoundation.h"
#include "CastleInterior.h"
#include <array>

// The original castle owns the entire domain. Reuse its bailey as the town.
inline bool GenerateCastleSettlementCandidate(MapPlan& plan,const MapGenParams& params,std::string& error,int layoutCandidate) {
    const int w=params.width,d=params.depth;
    if(w<128||d<128){error="Castle with a 2x2 human requires at least 128x128 blocks";return false;}
    auto cp=params;cp.width=w/2;cp.depth=d/2;cp.extra["castleHumanScale"]=2;
    if(cp.extra.count("wallHeight") && cp.extra.at("wallHeight")<=0){
        cp.extra["outerWallDefenseStrength"]=0;
        cp.extra.erase("outerDefense");
    }
    cp.extra["keepScale"]=2;cp.extra["sharedKeepWalls"]=1;cp.extra["nativeAssetTown"]=1;
    cp.extra["candidateCount"]=1;cp.extra["castleCandidateOffset"]=layoutCandidate;
    if(!cp.extra.count("mainRoadWidth"))cp.extra["mainRoadWidth"]=3;
    if(!cp.extra.count("keepWidth"))cp.extra["keepWidth"]=cp.width*(cp.extra.count("keepWidthRatio")?cp.extra.at("keepWidthRatio"):0.375);
    if(!cp.extra.count("keepHeight"))cp.extra["keepHeight"]=cp.depth*(cp.extra.count("keepDepthRatio")?cp.extra.at("keepDepthRatio"):0.25);
    MapPlan original;CastleMapImageGenerator generator(cp);
    if(!generator.Generate(original,error))return false;
    // Retain the castle's access tree, but release obsolete service-building
    // spurs before growing native houses. Otherwise a larger keep leaves the
    // remaining bailey cut into plots too small for any existing asset.
    std::vector<std::uint8_t> access(original.biomes.size(),0);
    std::vector<int> previous(original.biomes.size(),-1);std::queue<int> pending;
    for(const auto&m:original.markers)if(m.role=="keep_public_entry"){
        const int i=m.y*original.width+m.x;previous[i]=i;pending.push(i);break;
    }
    while(!pending.empty()){
        const int i=pending.front();pending.pop();
        for(const auto&step:std::array<std::pair<int,int>,4>{{{0,-1},{1,0},{0,1},{-1,0}}}){
            const int x=i%original.width+step.first,y=i/original.width+step.second;
            if(x<0||y<0||x>=original.width||y>=original.depth)continue;
            const int j=y*original.width+x;
            if(previous[j]<0&&original.castleStreetMask[j]){previous[j]=i;pending.push(j);}
        }
    }
    for(const auto&m:original.markers)if(m.kind=="gate"||
        m.role=="open_castle_approach"||m.role=="inner_ward_entry"){
        int i=m.y*original.width+m.x;if(previous[i]<0)continue;
        for(;;){
            for(int y=std::max(0,i/original.width-1);y<=std::min(original.depth-1,i/original.width+1);++y)
                for(int x=std::max(0,i%original.width-1);x<=std::min(original.width-1,i%original.width+1);++x)
                    if(original.castleStreetMask[y*original.width+x])access[y*original.width+x]=1;
            if(previous[i]==i)break;i=previous[i];
        }
    }
    plan=original;plan.width=w;plan.depth=d;
    auto enlarge=[&](const std::vector<std::uint8_t>& source,std::uint8_t padding){
        std::vector<std::uint8_t> result(w*d,padding);if(source.empty())return result;
        for(int y=0;y<d;++y)for(int x=0;x<w;++x)
            result[y*w+x]=source[std::min(y/2,original.depth-1)*original.width+std::min(x/2,original.width-1)];
        return result;
    };
    plan.biomes=enlarge(original.biomes,PLAIN);plan.castleDefenseMask=enlarge(original.castleDefenseMask,0);
    plan.castleStreetMask=enlarge(access,0);const auto domain=enlarge(original.settlementMask,0);
    for(auto&r:plan.regions){r.x0*=2;r.y0*=2;r.x1=std::min(w-1,r.x1*2+1);r.y1=std::min(d-1,r.y1*2+1);
        if(r.doorX>=0)r.doorX*=2;if(r.doorY>=0)r.doorY*=2;r.width*=2;r.frontageWidth*=2;}
    for(auto&m:plan.markers){m.x*=2;m.y*=2;}
    std::vector<std::uint8_t> protectedCells(w*d,0),removedFacility(w*d,0);
    auto mark=[&](const MapPlanRegion&r,std::vector<std::uint8_t>& mask,int margin){
        for(int y=std::max(0,r.y0-margin);y<=std::min(d-1,r.y1+margin);++y)
            for(int x=std::max(0,r.x0-margin);x<=std::min(w-1,r.x1+margin);++x)mask[y*w+x]=1;
    };
    for(const auto&r:plan.regions)if(r.kind=="keep"||r.kind=="keep_expansion"||r.kind=="inner_ward"||r.kind=="tower"||r.kind=="gatehouse")mark(r,protectedCells,1);
    std::vector<int> removedIds;
    for(const auto&r:plan.regions)if(r.kind=="facility" && !protectedCells[((r.y0+r.y1)/2)*w+(r.x0+r.x1)/2]){
        mark(r,removedFacility,0);removedIds.push_back(r.id);
    }
    std::vector<int> remap(plan.regions.size(),-1);std::vector<MapPlanRegion> retained;
    for(std::size_t i=0;i<plan.regions.size();++i){auto r=plan.regions[i];const int at=((r.y0+r.y1)/2)*w+(r.x0+r.x1)/2;
        if(r.kind=="facility" && std::find(removedIds.begin(),removedIds.end(),r.id)!=removedIds.end()){
            r.kind="castle_town_seed";r.parentIndex=-1;remap[i]=static_cast<int>(retained.size());retained.push_back(r);continue;
        }
        if(!protectedCells[at] && (r.kind=="furniture"||r.kind=="street_tree"||r.kind=="garden"||r.kind=="service_court"||r.kind=="training_ground"))continue;
        remap[i]=static_cast<int>(retained.size());retained.push_back(r);
    }
    for(auto&r:retained)if(r.parentIndex>=0&&r.parentIndex<static_cast<int>(remap.size()))r.parentIndex=remap[r.parentIndex];
    plan.regions=std::move(retained);
    plan.markers.erase(std::remove_if(plan.markers.begin(),plan.markers.end(),[&](const auto&m){
        return (m.kind=="service_entry"||m.kind=="landmark")&&std::find(removedIds.begin(),removedIds.end(),m.id)!=removedIds.end();
    }),plan.markers.end());
    for(auto&m:plan.markers)if(m.regionIndex>=0&&m.regionIndex<static_cast<int>(remap.size()))m.regionIndex=remap[m.regionIndex];
    MapPlan town;town.Reset(w,d,params.seed,"town");town.settlementSite="plains";town.cityType="castle_town";
    town.biomes.assign(w*d,MOUNTAIN);town.settlementMask.assign(w*d,0);
    for(int i=0;i<w*d;++i){
        const auto b=plan.biomes[i];const bool fortification=plan.castleDefenseMask[i]!=0;
        if(!domain[i]||protectedCells[i]||fortification||b==RIVER||b==LAKE||b==TOWN_SEA)continue;
        town.settlementMask[i]=1;town.biomes[i]=plan.castleStreetMask[i]&&!removedFacility[i]?ROAD:PLAIN;
    }
    if(!castle_interior::ReserveTraining(plan,town,error))return false;
    auto tp=params;tp.extra["villageMainRoadCount"]=0;tp.extra["preserveSettlementRoads"]=1;tp.extra["settlementPlazaSize"]=8;
    // The original castle alone owns the curtain. The irregular bailey mask
    // borders the keep/towers too; fencing it again creates stray wall stubs.
    tp.extra["wallHeight"]=0;
    tp.extra["existingRoyalResidence"]=1;
    tp.extra["countryWealth"]=plan.countryWealth;
    if(!village_growth::Generate(town,tp,error)){
        error += " (castle access cells="+std::to_string(std::count(access.begin(),access.end(),1))+", buildable="+
            std::to_string(std::count(town.settlementMask.begin(),town.settlementMask.end(),1))+", public roads="+
            std::to_string(std::count(town.biomes.begin(),town.biomes.end(),ROAD))+")";return false;
    }
    ApplySettlementDecoration(town,plan.countryWealth,params);
    plan.settlementMask=town.settlementMask;plan.heightOffsets.assign(w*d,0);plan.shoreCuts.assign(w*d,0);
    for(int i=0;i<w*d;++i)if(town.settlementMask[i])plan.biomes[i]=town.biomes[i];
    const int offset=static_cast<int>(plan.regions.size());
    for(auto r:town.regions){if(r.parentIndex>=0)r.parentIndex+=offset;plan.regions.push_back(r);}
    for(auto m:town.markers){if(m.regionIndex>=0)m.regionIndex+=offset;plan.markers.push_back(m);}
    MapPlanMarker gate;bool found=false;
    for(const auto&m:plan.markers)if(m.role=="main_gate"||m.role=="open_castle_approach"){gate=m;found=true;break;}
    if(found){gate.kind="castle_town_connection";plan.markers.push_back(gate);}
    plan.plazaEnabled=true;plan.plazaStyle="outer_bailey_market";plan.cityType="castle_town";
    plan.selectionReason="castle_and_asset_town";plan.castleDevelopmentModel="scaled_castle_with_inner_asset_town";
    plan.castleHistory.push_back({"human_scale","original_castle_layout_scaled_to_2x2",2,1.0});
    plan.castleHistory.push_back({"keep_scale","double_keep_dimensions_with_shared_partitions",2,1.0});
    plan.castleHistory.push_back({"castle_town","native_asset_growth_inside_original_outer_bailey",static_cast<int>(removedIds.size()),1.0});
    plan.castleHistory.push_back({"layout_selection","castle_and_native_frontage_candidate",layoutCandidate,1.0});
    if(!castle_interior::Decorate(plan,params,error))return false;
    std::vector<unsigned char> perimeter(w*d,0);
    for(int i=0;i<w*d;++i)perimeter[i]=plan.castleDefenseMask[i]==2;
    ReserveSettlementWallAssets(plan,params,perimeter);
    return true;
}

// Judge the complete settlement, rather than choosing a well-furnished keep
// first and discovering afterward that no usable native frontages remain.
inline bool GenerateCastleSettlement(MapPlan& plan,const MapGenParams& params,std::string& error) {
    const auto setting=params.extra.find("candidateCount");
    const int count=setting==params.extra.end()?6:std::clamp(static_cast<int>(setting->second),1,12);
    MapPlan best;int bestHomes=-1;double bestScore=-std::numeric_limits<double>::infinity();
    std::vector<MapPlanCastleHistoryStage> trials;
    for(int i=0;i<count;++i){
        MapPlan candidate;std::string failure;
        if(!GenerateCastleSettlementCandidate(candidate,params,failure,i)){error=failure;continue;}
        if(!EvaluateAssetFoundation(candidate).Passed()){error="Castle town entrances are disconnected for a 2x2 human";continue;}
        const int homes=static_cast<int>(std::count_if(candidate.regions.begin(),candidate.regions.end(),[](const auto&r){return r.kind=="asset_plot";}));
        trials.push_back({"layout_trial","native_houses_in_candidate_"+std::to_string(i),homes,1.0});
        if(homes>bestHomes || (homes==bestHomes && candidate.qualityScore>bestScore)){
            bestHomes=homes;bestScore=candidate.qualityScore;best=std::move(candidate);
        }
    }
    if(!std::isfinite(bestScore))return false;
    best.castleHistory.insert(best.castleHistory.end(),trials.begin(),trials.end());
    plan=std::move(best);error.clear();return true;
}
