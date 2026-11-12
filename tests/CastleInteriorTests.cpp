#include "CastleSettlement.h"
#include <iostream>
#include <stdexcept>
#include <set>
void Require(bool b,const std::string& reason){if(!b)throw std::runtime_error(reason);}
MapGenParams Params(int mode,int size,int count){
    MapGenParams p;p.width=p.depth=size;p.seed=4242;
    p.extra={{"castleMode",mode},{"assetCount",4},{"assetPlotCount",8},{"candidateCount",3},
        {"keepRoomCountMin",count},{"keepRoomCountMax",count}};
    for(int i=0;i<4;++i){const auto k="asset"+std::to_string(i);
        p.extra[k+"Id"]=0;p.extra[k+"Rotation"]=i;p.extra[k+"Width"]=12;p.extra[k+"Depth"]=12;
        p.extra[k+"DoorSide"]=(2+i)%4;
        p.extra[k+"DoorX"]=i==1?0:i==3?10:5;p.extra[k+"DoorY"]=i==0?10:i==2?0:5;
    }return p;
}
int main(){try{
    int checked=0;
    for(int mode=0;mode<5;++mode)for(int count:{4,10}){
        auto p=Params(mode,count==4?160:240,count);MapPlan plan;std::string error;
        const bool generated=GenerateCastleSettlement(plan,p,error);
        Require(generated,std::to_string(mode)+" rooms="+std::to_string(count)+": "+error);
        int rooms=0,doors=0,beds=0,stoves=0,thrones=0,targets=0,pianos=0;std::set<std::string> roles;
        std::vector<std::uint8_t> occupied(plan.width*plan.depth,0);
        for(const auto&r:plan.regions)if(r.kind=="castle_portal")
            for(int y=r.y0;y<=r.y1;++y)for(int x=r.x0;x<=r.x1;++x)occupied[y*plan.width+x]=1;
        for(const auto&r:plan.regions){
            if(r.kind=="room")++rooms;
            if(r.kind=="castle_door"){
                ++doors;Require(r.parentIndex>=0&&r.linkedRegionId>=0,"Door lost room graph linkage");
                Require(r.x1-r.x0+1==(r.rotation%2?2:4)&&r.y1-r.y0+1==(r.rotation%2?4:2),"Shared-wall opening cannot fit a native door");
                for(int y=r.y0;y<=r.y1;++y)for(int x=r.x0;x<=r.x1;++x)
                    Require(castle_interior::Floor(plan.biomes[y*plan.width+x]),"Room door is blocked by masonry");
            }
            if(r.kind!="castle_fixture")continue;
            Require(r.parentIndex>=0,"Fixture has no semantic parent");const auto&parent=plan.regions[r.parentIndex];
            Require(parent.kind=="room"||parent.kind=="castle_activity","Fixture is not linked to a room/activity");
            Require(r.x0>=parent.x0&&r.x1<=parent.x1&&r.y0>=parent.y0&&r.y1<=parent.y1,"Furniture escaped its room");
            for(int y=r.y0;y<=r.y1;++y)for(int x=r.x0;x<=r.x1;++x){
                Require(!occupied[y*plan.width+x],"Fixtures overlap");occupied[y*plan.width+x]=1;
            }
            beds+=r.role=="royal_bed"||r.role=="garrison_bed";stoves+=r.role=="cooking_stove";
            thrones+=r.role=="royal_throne";targets+=r.role=="training_dummy";pianos+=r.role=="ballroom_piano";roles.insert(r.role);
        }
        Require(rooms==count&&doors==count-1,"Room count or native door spanning tree changed");
        int audience=0,jambs=0;
        for(const auto&r:plan.regions){
            if(r.kind=="castle_portal")++jambs;
            if(r.kind!="castle_clearance")continue;
            if(r.role=="audience_space"){
                ++audience;Require(r.x1-r.x0+1>=10&&r.y1-r.y0+1>=8,"Throne has no generous audience space");
            }
            for(const auto&f:plan.regions)if(f.kind=="castle_fixture"){
                if(r.role=="throne_buffer"&&f.role=="royal_throne")continue;
                Require(f.x1<r.x0||f.x0>r.x1||f.y1<r.y0||f.y0>r.y1,"Furniture encroaches on a landing, aisle or audience space");
            }
        }
        Require(audience==1&&jambs==2*doors,"Throne clearance or integrated door frames missing");
        Require(beds>0&&stoves>0&&thrones==1&&targets>0,"Castle has no complete living/working/training program");
        Require((mode==3)==(pianos>0),"Ballroom is not specific to the palace");
        Require(roles.count(mode==1||mode==4?"command_table":mode==2?"customs_counter":mode==3?"ballroom_piano":"banquet_table"),"Castle archetype did not change the hall's function");
        Require(std::none_of(plan.biomes.begin(),plan.biomes.end(),[](auto b){return b==TOWN_FURNITURE;}),"Native interiors still use anonymous furniture pixels");
        for(const auto&s:plan.regions)if(s.kind=="castle_surface"&&s.role=="ballroom_floor")
            for(int y=s.y0;y<=s.y1;++y)for(int x=s.x0;x<=s.x1;++x)Require(!occupied[y*plan.width+x],"Dance floor is obstructed");
        if(mode==0&&count==4){MapPlan repeat;Require(GenerateCastleSettlement(repeat,p,error),error);
            Require(plan.biomes==repeat.biomes&&plan.regions.size()==repeat.regions.size(),"Interior layout is not deterministic");
            for(std::size_t i=0;i<plan.regions.size();++i)Require(plan.regions[i].role==repeat.regions[i].role&&
                plan.regions[i].x0==repeat.regions[i].x0&&plan.regions[i].y0==repeat.regions[i].y0,"Fixture placement is not deterministic");
        }
        ++checked;
    }
    for(int depth:{2,4,6}){
        auto p=Params(0,240,4);p.extra["castleObject4_31Depth"]=depth;
        MapPlan plan;std::string error;Require(GenerateCastleSettlement(plan,p,error),error);
        const auto throne=std::find_if(plan.regions.begin(),plan.regions.end(),[](const auto&r){return r.role=="royal_throne";});
        Require(throne!=plan.regions.end()&&throne->y1-throne->y0+1==depth,"Native throne depth lost");
        int audience=0,carpet=0;
        for(const auto&r:plan.regions){
            if(r.role=="audience_space"){++audience;Require(r.y0==throne->y1+1&&r.y1-r.y0+1>=8,"Audience overlaps resized throne");}
            if(r.role=="royal_carpet"){++carpet;Require(r.y0==throne->y1+1,"Carpet starts underneath resized throne");}
        }
        Require(audience==1&&carpet==1,"Resized throne lost its audience/carpet");++checked;
    }
    std::cout<<"PASS "<<checked<<" castles: audience space, reserved circulation, door landings, integrated frames and living rooms\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
