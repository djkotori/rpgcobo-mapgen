#pragma once
#include "MapImageGenerator.h"
#include <array>
#include <algorithm>
#include <queue>
#include <tuple>

namespace castle_interior {
inline bool Floor(std::uint8_t b){return b==PLAIN||b==DESERT||b==FOREST||b==ROAD||b==BRIDGE||
    b==TOWN_FLOOR||b==TOWN_WOOD_FLOOR||b==TOWN_STONE_FLOOR||b==TOWN_PAVEMENT;}

// Reserve a connected drill yard before the native town occupies the bailey.
inline bool ReserveTraining(MapPlan& plan,MapPlan& town,std::string& error){
    const int w=plan.width,d=plan.depth;MapPlanRegion best;int score=100000000;int ax=-1,ay=-1;
    for(int size:{12,10,8}){
        for(int y=3;y+size<d-3;++y)for(int x=3;x+size<w-3;++x){
            bool fits=true;
            for(int yy=y;yy<y+size&&fits;++yy)for(int xx=x;xx<x+size;++xx)
                if(!town.settlementMask[yy*w+xx]||plan.castleStreetMask[yy*w+xx]){fits=false;break;}
            if(!fits)continue;
            auto street=[&](int xx,int yy){return xx>=0&&yy>=0&&xx+1<w&&yy+1<d&&
                plan.castleStreetMask[yy*w+xx]&&plan.castleStreetMask[yy*w+xx+1]&&
                plan.castleStreetMask[(yy+1)*w+xx]&&plan.castleStreetMask[(yy+1)*w+xx+1];};
            int px=-1,py=-1;
            for(int k=2;k<size-3&&px<0;++k){
                for(auto p:{std::pair<int,int>{x+k,y-2},{x+k,y+size},{x-2,y+k},{x+size,y+k}})
                    if(street(p.first,p.second)){px=p.first;py=p.second;break;}
            }
            if(px<0)continue;
            const int cost=std::abs(x+size/2-w/2)+std::abs(y+size/2-d*2/3);
            if(cost>=score)continue;score=cost;ax=px;ay=py;
            best={"castle_activity",50000,x,y,x+size-1,y+size-1};best.role="training_ground";
        }
        if(ax>=0)break;
    }
    if(ax<0){error="No connected drill yard fits beside the castle access street";return false;}
    for(int y=best.y0;y<=best.y1;++y)for(int x=best.x0;x<=best.x1;++x){
        town.settlementMask[y*w+x]=0;town.biomes[y*w+x]=MOUNTAIN;plan.biomes[y*w+x]=DESERT;
    }
    plan.regions.push_back(best);
    MapPlanMarker entry{"castle_activity_access",ax,ay,best.id};entry.role="training_ground_access";
    plan.markers.push_back(entry);return true;
}

struct Object {int type,variant,w,d,h;};
inline Object Native(const MapGenParams& p,int type,int variant,int w,int d,int h){
    auto get=[&](const std::string& k,int value){auto i=p.extra.find(k);return i==p.extra.end()?value:static_cast<int>(i->second);};
    const auto key="castleObject"+std::to_string(type)+"_"+std::to_string(variant);
    return {type,variant,get(key+"Width",w),get(key+"Depth",d),get(key+"Height",h)};
}

inline bool Decorate(MapPlan& plan,const MapGenParams& params,std::string& error){
    const int w=plan.width,d=plan.depth,area=w*d;
    std::vector<std::uint8_t> blocked(area,0),clearance(area,0);
    std::vector<int> rooms,fixtures;
    const int architectural=static_cast<int>(plan.regions.size());
    auto mark=[&](const MapPlanRegion&r,std::vector<std::uint8_t>& mask,int margin=0){
        for(int y=std::max(0,r.y0-margin);y<=std::min(d-1,r.y1+margin);++y)
            for(int x=std::max(0,r.x0-margin);x<=std::min(w-1,r.x1+margin);++x)mask[y*w+x]=1;
    };
    for(int i=0;i<architectural;++i){
        const auto r=plan.regions[i];if(r.kind=="room"||r.kind=="castle_activity")rooms.push_back(i);
        if(r.kind=="asset_plot"||r.kind=="settlement_facility")mark(r,blocked);
        if(r.kind!="room_connection"&&r.kind!="castle_front_door")continue;
        mark(r,clearance,3);
        auto landing=r;landing.kind="castle_clearance";landing.role="door_landing";
        landing.x0=std::max(0,r.x0-3);landing.y0=std::max(0,r.y0-3);
        landing.x1=std::min(w-1,r.x1+3);landing.y1=std::min(d-1,r.y1+3);
        plan.regions.push_back(landing);
        if(r.kind=="room_connection"){
            bool ceremonial=false;
            for(int id:rooms){const auto& room=plan.regions[id];
                if(room.id!=r.parentId&&room.id!=r.linkedRegionId)continue;
                ceremonial|=room.role=="throne_room"||(plan.archetype=="palace"&&
                    (room.role=="entrance_hall"||room.role=="royal_chambers"||room.role.find("chapel")!=std::string::npos));
            }
            const auto model=Native(params,1,ceremonial?13:8,4,1,4);
            const int width=r.x1-r.x0+1,depth=r.y1-r.y0+1;
            if((r.rotation%2?model.d:model.w)>width||(r.rotation%2?model.w:model.d)>depth){
                error="Native room door exceeds the shared-wall opening";return false;
            }
            MapPlanRegion door=r;door.kind="castle_door";door.role=ceremonial?"ceremonial_door":"timber_door";
            door.objectType=1;door.objectVariant=model.variant;door.objectHeight=model.h;
            const int doorIndex=static_cast<int>(plan.regions.size());plan.regions.push_back(door);
            // Two stone jambs project one block into either room. Their solid
            // footprint participates in navigation, rather than being an
            // unmodelled obstacle added by the editor after the fact.
            for(int side:{-1,1}){
                auto post=r;post.kind="castle_portal";post.role="stone_door_jamb";
                post.parentIndex=doorIndex;post.parentId=door.id;post.objectHeight=model.h+1;
                if(r.rotation%2){post.x0--;post.x1++;post.y0=post.y1=side<0?r.y0-1:r.y1+1;}
                else{post.y0--;post.y1++;post.x0=post.x1=side<0?r.x0-1:r.x1+1;}
                mark(post,blocked);plan.regions.push_back(post);
            }
        }
    }
    auto pass=[&](int x,int y){
        if(x<0||y<0||x+1>=w||y+1>=d)return false;
        for(int yy=y;yy<y+2;++yy)for(int xx=x;xx<x+2;++xx)
            if(blocked[yy*w+xx]||!Floor(plan.biomes[yy*w+xx]))return false;
        return true;
    };
    auto adjacent=[&](const MapPlanRegion&r,const std::vector<std::uint8_t>& seen){
        for(int y=r.y0-1;y<=r.y1;++y)for(int x:{r.x0-2,r.x1+1})
            if(x>=0&&y>=0&&x<w&&y<d&&seen[y*w+x])return true;
        for(int x=r.x0-1;x<=r.x1;++x)for(int y:{r.y0-2,r.y1+1})
            if(x>=0&&y>=0&&x<w&&y<d&&seen[y*w+x])return true;
        return false;
    };
    auto accessible=[&](){
        std::vector<std::uint8_t> seen(area,0);std::queue<int> q;
        for(const auto&m:plan.markers)if(m.role=="keep_public_entry"&&pass(m.x,m.y)){q.push(m.y*w+m.x);seen[m.y*w+m.x]=1;break;}
        while(!q.empty()){
            const int i=q.front();q.pop();
            for(auto step:std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}){
                const int x=i%w+step.first,y=i/w+step.second;
                if(pass(x,y)&&!seen[y*w+x]){seen[y*w+x]=1;q.push(y*w+x);}
            }
        }
        for(int id:rooms){const auto&r=plan.regions[id];bool reached=false;
            for(int y=r.y0+2;y<r.y1;++y)for(int x=r.x0+2;x<r.x1;++x)reached|=seen[y*w+x]!=0;
            if(!reached)return false;
        }
        for(int id:fixtures)if(!adjacent(plan.regions[id],seen))return false;
        return !q.size();
    };
    if(!accessible()){error="Castle room graph or drill yard is unreachable before furnishing";return false;}
    auto wideAccessible=[&](int onlyRoom=-1){
        for(int parent:rooms){const auto room=plan.regions[parent];
            if(room.kind!="room"||(onlyRoom>=0&&parent!=onlyRoom))continue;
            auto wide=[&](int x,int y){
                if(x<room.x0||y<room.y0||x+3>room.x1||y+3>room.y1)return false;
                for(int yy=y;yy<y+4;++yy)for(int xx=x;xx<x+4;++xx)
                    if(blocked[yy*w+xx]||!Floor(plan.biomes[yy*w+xx]))return false;
                return true;
            };
            std::vector<int> landings;
            for(const auto&door:plan.regions){
                if(door.kind=="castle_door"){
                    if(door.parentId!=room.id&&door.linkedRegionId!=room.id)continue;
                }else if(door.kind!="castle_front_door"||room.role!="entrance_hall")continue;
                const int x=door.rotation%2?(door.x0<=room.x0+1?door.x1+1:door.x0-4):
                    (door.kind=="castle_front_door"?(door.x0+door.x1-3)/2:door.x0);
                const int y=door.rotation%2?door.y0:(door.y0<=room.y0+1?door.y1+1:door.y0-4);
                if(!wide(x,y))return false;landings.push_back(y*w+x);
            }
            if(landings.empty())return false;
            if(landings.size()==1)continue;
            std::vector<std::uint8_t> seen(area,0);std::queue<int> q;q.push(landings.front());seen[landings.front()]=1;
            while(!q.empty()){const int i=q.front();q.pop();
                for(auto step:std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}){
                    const int x=i%w+step.first,y=i/w+step.second;
                    if(wide(x,y)&&!seen[y*w+x]){seen[y*w+x]=1;q.push(y*w+x);}
                }
            }
            for(int at:landings)if(!seen[at])return false;
        }
        return true;
    };
    if(!wideAccessible()){error="Castle doors lack connected 4-block interior landings";return false;}
    auto surface=[&](const MapPlanRegion& bounds,int parent,const std::string& role,bool keepClear){
        if(bounds.x1<bounds.x0||bounds.y1<bounds.y0)return;
        auto r=bounds;r.kind="castle_surface";r.role=role;r.parentIndex=parent;r.parentId=plan.regions[parent].id;
        plan.regions.push_back(r);if(keepClear)mark(r,clearance);
    };
    auto reserve=[&](MapPlanRegion bounds,int parent,const std::string& role){
        bounds.kind="castle_clearance";bounds.role=role;bounds.parentIndex=parent;bounds.parentId=plan.regions[parent].id;
        plan.regions.push_back(bounds);mark(bounds,clearance);
    };
    std::vector<int> furnishedArea(architectural,0);
    auto place=[&](int parent,const std::string& role,Object model,int preferredX,int preferredY,int rotation=0,int maxMove=3){
        const auto room=plan.regions[parent];const int rw=rotation%2?model.d:model.w,rd=rotation%2?model.w:model.d;
        const int capacity=(room.x1-room.x0-2)*(room.y1-room.y0-2);
        if(furnishedArea[parent]+rw*rd>std::max(12,capacity*(room.role=="throne_room"?12:23)/100))return -1;
        std::vector<std::tuple<int,int,int>> positions;
        for(int y=room.y0+2;y+rd-1<=room.y1-1;++y)for(int x=room.x0+2;x+rw-1<=room.x1-1;++x)
            if(std::abs(x-preferredX)+std::abs(y-preferredY)<=maxMove)
                positions.emplace_back(std::abs(x-preferredX)+std::abs(y-preferredY),y,x);
        std::sort(positions.begin(),positions.end());
        for(auto [distance,y,x]:positions){
            bool fits=true;
            for(int yy=y;yy<y+rd&&fits;++yy)for(int xx=x;xx<x+rw;++xx)
                if(blocked[yy*w+xx]||clearance[yy*w+xx]||!Floor(plan.biomes[yy*w+xx])){fits=false;break;}
            for(int yy=y-1;yy<=y+rd&&fits;++yy)for(int xx=x-1;xx<=x+rw;++xx)
                if(blocked[yy*w+xx]){fits=false;break;}
            if(!fits)continue;
            MapPlanRegion r{"castle_fixture",60000+static_cast<int>(plan.regions.size()),x,y,x+rw-1,y+rd-1};
            r.role=role;r.rotation=rotation;r.objectType=model.type;r.objectVariant=model.variant;r.objectHeight=model.h;
            r.parentIndex=parent;r.parentId=room.id;
            mark(r,blocked);const int index=static_cast<int>(plan.regions.size());plan.regions.push_back(r);fixtures.push_back(index);
            if(accessible()&&wideAccessible(parent)){furnishedArea[parent]+=rw*rd;return index;}
            fixtures.pop_back();plan.regions.pop_back();
            for(int yy=y;yy<y+rd;++yy)for(int xx=x;xx<x+rw;++xx)blocked[yy*w+xx]=0;
        }
        return -1;
    };
    const auto chair=Native(params,4,4,2,2,3),table=Native(params,4,7,4,4,2),bench=Native(params,4,5,4,2,3);
    const auto shelf=Native(params,4,9,4,2,4),chest=Native(params,4,11,2,2,4),barrel=Native(params,4,21,3,3,3);
    const auto bed=Native(params,4,14,2,4,2),royalBed=Native(params,4,15,4,4,3);
    const auto counter=Native(params,4,10,4,2,2),stove=Native(params,2,7,4,2,3),candle=Native(params,2,4,2,1,2);
    const auto sword=Native(params,4,30,4,1,3),shield=Native(params,4,28,3,1,3);
    auto detail=[&](int parent,const std::string& role,Object model){
        if(parent<0)return;
        const auto table=plan.regions[parent];
        if(model.w>table.x1-table.x0+1||model.d>table.y1-table.y0+1)return;
        const int x=(table.x0+table.x1+1-model.w)/2,y=(table.y0+table.y1+1-model.d)/2;
        MapPlanRegion r{"castle_detail",70000+static_cast<int>(plan.regions.size()),x,y,x+model.w-1,y+model.d-1};
        r.role=role;r.objectType=model.type;r.objectVariant=model.variant;r.objectHeight=model.h;
        r.parentId=table.id;r.parentIndex=parent;plan.regions.push_back(r);
    };
    const auto documents=Native(params,4,34,2,2,1),book=Native(params,4,33,2,2,1),dish=Native(params,4,35,2,3,1);
    // Fix the focal point before routing circulation. Optional furniture may
    // be omitted, but the throne must never slide sideways to dodge a door.
    const auto throne=Native(params,4,31,4,3,4);
    for(int parent:rooms){const auto room=plan.regions[parent];if(room.role!="throne_room")continue;
        const int cx=(room.x0+room.x1)/2,top=room.y0+3;
        if(place(parent,"royal_throne",throne,cx-(throne.w-1)/2,top,0,0)<0){
            error="Throne room cannot fit a centred throne away from its doors";return false;
        }
        const int audienceY=top+throne.d;
        if(cx-4<room.x0+1||cx+5>room.x1-1||audienceY+7>room.y1-2){error="Throne room lacks a full audience area";return false;}
        reserve({"",0,cx-4,top-1,cx+5,audienceY-1},parent,"throne_buffer");
        reserve({"",0,cx-4,audienceY,cx+5,audienceY+7},parent,"audience_space");
        surface({"",0,cx-2,audienceY,cx+3,room.y1-2},parent,"royal_carpet",true);
    }
    // Route every door to a shared room hub before adding any secondary
    // furniture. The reserved bands are 4 blocks wide (6 in public halls),
    // with the 4-block doorway as their only local narrowing.
    for(int parent:rooms){
        const auto room=plan.regions[parent];if(room.kind!="room")continue;
        const int cx=(room.x0+room.x1-1)/2,cy=room.role=="throne_room"?room.y0+throne.d+8:(room.y0+room.y1-1)/2;
        auto inside=[&](int x,int y){return x>=room.x0+1&&y>=room.y0+1&&x+1<=room.x1-1&&y+1<=room.y1-1&&pass(x,y);};
        int hub=-1,best=100000;
        for(int y=room.y0+1;y<room.y1-1;++y)for(int x=room.x0+1;x<room.x1-1;++x)
            if(inside(x,y)&&std::abs(x-cx)+std::abs(y-cy)<best){best=std::abs(x-cx)+std::abs(y-cy);hub=y*w+x;}
        if(hub<0){error="Castle room has no circulation hub";return false;}
        std::vector<int> previous(area,-1);std::queue<int> pending;previous[hub]=hub;pending.push(hub);
        while(!pending.empty()){const int i=pending.front();pending.pop();
            for(auto step:std::array<std::pair<int,int>,4>{{{0,1},{1,0},{-1,0},{0,-1}}}){
                const int x=i%w+step.first,y=i/w+step.second,j=y*w+x;
                if(inside(x,y)&&previous[j]<0){previous[j]=i;pending.push(j);}
            }
        }
        std::vector<std::uint8_t> band(area,0);
        for(int i=0;i<architectural;++i){const auto door=plan.regions[i];
            if(door.kind=="room_connection"){
                if(door.parentId!=room.id&&door.linkedRegionId!=room.id)continue;
            }else if(door.kind!="castle_front_door"||room.role!="entrance_hall")continue;
            const int dx=std::clamp((door.x0+door.x1-1)/2,room.x0+1,room.x1-2);
            const int dy=std::clamp((door.y0+door.y1-1)/2,room.y0+1,room.y1-2);
            int start=-1,cost=100000;
            for(int y=std::max(room.y0+1,dy-4);y<=std::min(room.y1-2,dy+4);++y)
                for(int x=std::max(room.x0+1,dx-4);x<=std::min(room.x1-2,dx+4);++x)
                    if(previous[y*w+x]>=0&&std::abs(x-dx)+std::abs(y-dy)<cost){cost=std::abs(x-dx)+std::abs(y-dy);start=y*w+x;}
            if(start<0){error="Castle door has no usable interior landing";return false;}
            const int margin=room.role=="throne_room"||room.role=="entrance_hall"?2:1;
            for(int at=start;;at=previous[at]){
                for(int y=std::max(room.y0+1,at/w-margin);y<=std::min(room.y1-1,at/w+1+margin);++y)
                    for(int x=std::max(room.x0+1,at%w-margin);x<=std::min(room.x1-1,at%w+1+margin);++x)
                        if(!blocked[y*w+x]&&Floor(plan.biomes[y*w+x]))band[y*w+x]=1;
                if(at==hub)break;
            }
        }
        // Export the reserved space as compact row runs for independent QA.
        std::vector<MapPlanRegion> strips;
        for(int y=room.y0+1;y<room.y1;++y)for(int x=room.x0+1;x<room.x1;){
            if(!band[y*w+x]){++x;continue;}const int begin=x;
            while(x<room.x1&&band[y*w+x])++x;
            auto match=std::find_if(strips.begin(),strips.end(),[&](const auto&r){return r.x0==begin&&r.x1==x-1&&r.y1==y-1;});
            if(match!=strips.end())match->y1=y;else strips.push_back({"",0,begin,y,x-1,y});
        }
        for(const auto&r:strips)reserve(r,parent,"circulation");
    }
    for(int parent:rooms){
        const auto room=plan.regions[parent];const auto role=room.role;
        const int left=room.x0+2,top=room.y0+2,right=room.x1-1,bottom=room.y1-1;
        const int cx=(left+right)/2,cy=(top+bottom)/2;
        auto put=[&](const std::string& name,Object object,int x,int y,int rot=0){return place(parent,name,object,x,y,rot);};
        if(room.kind=="castle_activity"){
            surface(room,parent,"drill_yard",false);
            if(put("training_dummy",{-2,0,4,2,5},left,top)<0){error="Drill yard has no usable practice target";return false;}
            put("training_dummy",{-2,0,4,2,5},right-3,top);
            put("training_weapon_rack",sword,left,bottom);
            put("training_bench",bench,right-3,bottom-1);continue;
        }
        if(role=="throne_room"){
            put("royal_guard_sword",sword,left,top);put("royal_guard_shield",shield,right-2,top);
        }else if(role.find("kitchen")!=std::string::npos||role=="scullery"){
            int cooking=put("cooking_stove",stove,left,top);
            if(cooking<0)cooking=put("cooking_stove",stove,right-3,top);
            if(cooking<0)cooking=put("cooking_stove",stove,left,bottom-1);
            if(cooking<0){error="Kitchen cannot fit a working stove away from circulation";return false;}
            put("preparation_counter",counter,right-3,top);put("kitchen_worktable",table,cx-1,cy);
            put("pantry_barrel",barrel,left,bottom-2);put("water_bucket",Native(params,4,41,2,2,2),right-1,bottom-1);
            put("cooking_pot",Native(params,4,23,2,2,3),right-1,top+4);
            if((right-left)*(bottom-top)>250){
                put("cooking_stove",stove,left+6,top);
                for(int yy=top+8;yy<bottom-6;yy+=10){const int work=put("kitchen_worktable",table,cx-1,yy);detail(work,"prepared_dishes",dish);}
            }
        }else if((role.find("chamber")!=std::string::npos&&role.find("council")==std::string::npos)||role.find("quarters")!=std::string::npos||role=="guest_apartment"){
            const bool royal=role=="royal_chambers";
            int index=royal?put("royal_bed",royalBed,left,top):put("garrison_bed",bed,left,top);
            if(index<0)index=put(royal?"royal_bed":"garrison_bed",bed,left,top);
            for(auto anchor:{std::pair<int,int>{right-3,top},{left,bottom-3},{right-3,bottom-3}}){
                if(index>=0)break;
                index=put(royal?"royal_bed":"garrison_bed",royal?royalBed:bed,anchor.first,anchor.second);
                if(index<0&&royal)index=put("royal_bed",bed,anchor.first,anchor.second);
            }
            if(index<0){error="Bedroom cannot fit a usable bed";return false;}
            if(!royal)for(int x=left+4;x<=right-1;x+=5)put("garrison_bed",bed,x,top);
            put("wardrobe",chest,right-1,top);put("bedroom_bookcase",shelf,right-3,bottom-1);
            if(royal){const int desk=put("bedroom_desk",table,cx-1,bottom-3);detail(desk,"bedroom_book",book);put("bedroom_chair",chair,cx-1,cy);}
        }else if(role=="entrance_hall"&&plan.archetype=="palace"){
            MapPlanRegion dance{"",0,left+4,top+4,right-4,bottom-4};
            surface(dance,parent,"ballroom_floor",true);
            const auto piano=Native(params,4,40,4,2,3);
            int music=put("ballroom_piano",piano,left,top);
            if(music<0)music=put("ballroom_piano",piano,right-3,top);
            if(music<0){error="Palace ballroom cannot fit its piano beside the dance floor";return false;}
            put("ballroom_bench",bench,left,cy,1);put("ballroom_bench",bench,right-1,cy,1);
            put("ballroom_candelabrum",candle,left,top);put("ballroom_candelabrum",candle,right-1,top);
        }else if(role=="entrance_hall"||role=="great_hall"||role=="guard_hall"){
            const bool military=plan.archetype=="fortress"||plan.archetype=="hill";
            const bool trading=plan.archetype=="river";
            const int columns=std::clamp((right-left+1)/14,1,military?2:3);
            const int rows=std::clamp((bottom-top+1)/12,1,trading?3:4);
            for(int row=0;row<rows;++row)for(int column=0;column<columns;++column){
                const bool command=military&&row==0&&column==0;
                const std::string name=command?"command_table":trading?"customs_counter":military?"garrison_mess_table":"banquet_table";
                const int ti=put(name,trading?counter:table,left+3+column*14,top+4+row*12);
                detail(ti,command||trading?"hall_documents":"banquet_dishes",command||trading?documents:dish);
                if(ti>=0){const auto t=plan.regions[ti];put(command?"officer_chair":"dining_chair",chair,t.x0,t.y1+2,2);
                    put(command?"officer_chair":"dining_chair",chair,t.x0,t.y0-3);}
            }
            if(military){put("guard_weapon_rack",sword,right-3,top);put("guard_shield",shield,left,bottom);}
            if(trading){put("customs_ledger_shelf",shelf,right-3,top);put("trade_barrel",barrel,right-2,bottom-2);}
            if(!military&&!trading)put("banquet_bench",bench,right-5,bottom-1);
        }else if(role.find("council")!=std::string::npos||role.find("command")!=std::string::npos||role=="war_room"||role=="map_room"){
            const int ti=put("council_table",table,cx-1,cy-1);detail(ti,"council_documents",documents);
            if(ti>=0){const auto t=plan.regions[ti];put("council_chair",chair,t.x0,t.y0-3);put("council_chair",chair,t.x0,t.y1+2,2);}
            put("council_bookcase",shelf,left,top);
        }else if(role.find("chapel")!=std::string::npos){
            const int altar=put("chapel_altar",table,cx-1,top);detail(altar,"altar_book",book);
            put("chapel_candle",candle,left,top);put("chapel_candle",candle,right-1,top);
            for(int yy=top+8;yy<bottom-2;yy+=6){put("chapel_pew",bench,left,yy,2);put("chapel_pew",bench,right-3,yy,2);}
        }else if(role=="armory"||role.find("guard")!=std::string::npos){
            put("weapon_rack",sword,left,top);put("shield_rack",shield,right-2,top);put("armory_worktable",table,left,bottom-3);
        }else if(role.find("store")!=std::string::npos||role=="pantry"||role=="treasury"||role=="strongroom"){
            put("storage_chest",chest,left,top);put("storage_barrel",barrel,right-2,top);put("storage_chest",chest,left,bottom-1);
        }else{
            for(int xx=left;xx<right-2;xx+=6)put("study_bookcase",shelf,xx,top);
            const int desk=put("study_desk",table,right-3,bottom-3);detail(desk,"study_book",book);put("study_chair",chair,cx,cy);
        }
        put("room_candle",candle,left,bottom);
    }
    if(!accessible()||!wideAccessible()){error="Castle furnishing blocks a room, wide aisle or furniture approach";return false;}
    plan.castleHistory.push_back({"interior_design","native_fixtures_from_castle_type_and_room_purpose",static_cast<int>(fixtures.size()),1.0});
    return true;
}
}
