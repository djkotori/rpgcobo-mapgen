#pragma once
#include "CastleRoomStyle.h"
#include "MapImageGenerator.h"
#include "CastleProgram.h"
#include <array>
#include <algorithm>
#include <queue>
#include <tuple>

// Native castle interiors: doors, furniture and floor surfaces are emitted into the Plan
// as castle_door / castle_fixture / castle_detail / castle_surface regions, which the
// editor places as real assets. (The drill-yard reservation of the old castle town is gone.)
namespace castle_interior {
inline bool Floor(std::uint8_t b){return b==PLAIN||b==DESERT||b==FOREST||b==ROAD||b==BRIDGE||
    b==TOWN_FLOOR||b==TOWN_WOOD_FLOOR||b==TOWN_STONE_FLOOR||b==TOWN_PAVEMENT;}

struct Object {int type,variant,w,d,h;};
inline Object Native(const MapGenParams& p,int type,int variant,int w,int d,int h){
    auto get=[&](const std::string& k,int value){auto i=p.extra.find(k);return i==p.extra.end()?value:static_cast<int>(i->second);};
    const auto key="castleObject"+std::to_string(type)+"_"+std::to_string(variant);
    return {type,variant,get(key+"Width",w),get(key+"Depth",d),get(key+"Height",h)};
}

// `doorsOnly`: place the doors and nothing else (the fallback for a castle that cannot be furnished: it keeps its doors).
// A room that cannot take its furniture (the throne, a stove, a bed) is left bare and noted in castleHistory ("bare_room"):
// the castle is still furnished, and every door stays.
inline bool Decorate(MapPlan& plan,const MapGenParams& params,std::string& error,bool doorsOnly=false){
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
        if(r.kind!="room_connection"&&r.kind!="castle_front_door"&&r.kind!="castle_side_door")continue;
        mark(r,clearance,3);
        auto landing=r;landing.kind="castle_clearance";landing.role="door_landing";
        landing.x0=std::max(0,r.x0-3);landing.y0=std::max(0,r.y0-3);
        landing.x1=std::min(w-1,r.x1+3);landing.y1=std::min(d-1,r.y1+3);
        plan.regions.push_back(landing);
        if(r.kind=="room_connection"||r.kind=="castle_side_door"){
            // The ceremonial (royal) door: the throne room's front door only, the one in the wall opposite the throne (the throne stands
            // at the room's top; its entrance is the bottom wall). The throne room's other doors are ordinary doors. In a palace the
            // entry rooms, the royal chambers and the chapels have ceremonial doors too.
            bool ceremonial=false,throneDoor=false;
            for(int id:rooms){const auto& room=plan.regions[id];
                if(room.id!=r.parentId&&room.id!=r.linkedRegionId)continue;
                if(room.role=="throne_room"){throneDoor=true;ceremonial|=r.rotation%2==0&&r.y0>=room.y1;continue;}
                ceremonial|=plan.archetype=="palace"&&
                    (IsCastleEntryRole(room.role)||room.role=="royal_chambers"||room.role.find("chapel")!=std::string::npos);
            }
            if(throneDoor){
                // A door of the throne room is royal only when it is the front door.
                bool front=false;
                for(int id:rooms){const auto& room=plan.regions[id];
                    if(room.role=="throne_room"&&(room.id==r.parentId||room.id==r.linkedRegionId))front=r.rotation%2==0&&r.y0>=room.y1;
                }
                ceremonial=front;
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
    if(doorsOnly)return true;
    auto bare=[&](const std::string& what){plan.castleHistory.push_back({"interior_design","bare_room: "+what,0,0.0});};
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
        for(int id:rooms){const auto&r=plan.regions[id];if(r.role=="corridor")continue;bool reached=false;
            for(int y=r.y0+2;y<r.y1;++y)for(int x=r.x0+2;x<r.x1;++x)reached|=seen[y*w+x]!=0;
            if(!reached)return false;
        }
        for(int id:fixtures)if(!adjacent(plan.regions[id],seen))return false;
        return !q.size();
    };
    if(!accessible()){error="Castle room graph or drill yard is unreachable before furnishing";return false;}
    int badLandingRoom=-1;  // the room whose door landings failed (for the message)
    std::string badLandingWhy;
    auto wideAccessible=[&](int onlyRoom=-1){
        for(int parent:rooms){const auto room=plan.regions[parent];
            if(room.kind!="room"||room.role=="corridor"||(onlyRoom>=0&&parent!=onlyRoom))continue;
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
                }else if(door.kind!="castle_front_door"||!IsCastleEntryRole(room.role))continue;
                const int x=door.rotation%2?(door.x0<=room.x0+1?door.x1+1:door.x0-4):
                    (door.kind=="castle_front_door"?(door.x0+door.x1-3)/2:door.x0);
                const int y=door.rotation%2?door.y0:(door.y0<=room.y0+1?door.y1+1:door.y0-4);
                if(!wide(x,y)){badLandingRoom=parent;badLandingWhy=" no 4x4 at "+std::to_string(x)+","+std::to_string(y)+" door "+std::to_string(door.x0)+","+std::to_string(door.y0)+" rot "+std::to_string(door.rotation);return false;}landings.push_back(y*w+x);
            }
            if(landings.empty()){badLandingRoom=parent;return false;}
            if(landings.size()==1)continue;
            std::vector<std::uint8_t> seen(area,0);std::queue<int> q;q.push(landings.front());seen[landings.front()]=1;
            while(!q.empty()){const int i=q.front();q.pop();
                for(auto step:std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}){
                    const int x=i%w+step.first,y=i/w+step.second;
                    if(wide(x,y)&&!seen[y*w+x]){seen[y*w+x]=1;q.push(y*w+x);}
                }
            }
            for(int at:landings)if(!seen[at]){badLandingRoom=parent;badLandingWhy=" disconnected landing at "+std::to_string(at%w)+","+std::to_string(at/w)+" of "+std::to_string(landings.size());return false;}
        }
        return true;
    };
    if(!wideAccessible()){
        error="Castle doors lack connected 4-block interior landings";
        if(badLandingRoom>=0){const auto&r=plan.regions[badLandingRoom];error+=" ("+r.role+" "+std::to_string(r.x1-r.x0+1)+"x"+std::to_string(r.y1-r.y0+1)+badLandingWhy+")";}
        return false;
    }
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
    auto place=[&](int parent,const std::string& role,Object model,int preferredX,int preferredY,int rotation=0,int maxMove=0,int inset=2){
        const auto room=plan.regions[parent];const int rw=rotation%2?model.d:model.w,rd=rotation%2?model.w:model.d;
        const int capacity=(room.x1-room.x0-2)*(room.y1-room.y0-2);
        if(furnishedArea[parent]+rw*rd>std::max(12,capacity*(room.role=="throne_room"?12:StoresFiller(room.role,plan.archetype)?35:23)/100))return -1;
        std::vector<std::tuple<int,int,int>> positions;
        const int outer=inset>=2?1:0;  // a corridor is narrow: its fixtures stand against the wall line
        for(int y=room.y0+inset;y+rd-1<=room.y1-outer;++y)for(int x=room.x0+inset;x+rw-1<=room.x1-outer;++x)
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
    const auto statue=Native(params,3,14,4,4,6);
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
        const int cx=(room.x0+room.x1)/2;
        // The throne stands on the room's axis at the back (3 in from the wall). A door in the back wall can put its landing over that
        // spot; the throne is then the one exception that moves: it steps forward (toward the entrance), never sideways, up to 6 cells.
        int top=room.y0+3,throneAt=-1;
        for(int shift=0;shift<=6&&throneAt<0;++shift)
            if(place(parent,"royal_throne",throne,cx-(throne.w-1)/2,top+shift,0,0)>=0){throneAt=shift;top+=shift;}
        if(throneAt<0){
            bare("throne room has no centred throne");continue;
        }
        const int audienceY=top+throne.d;
        if(cx-4<room.x0+1||cx+5>room.x1-1||audienceY+7>room.y1-2){bare("throne room has no audience area");continue;}
        reserve({"",0,cx-4,top-1,cx+5,audienceY-1},parent,"throne_buffer");
        reserve({"",0,cx-4,audienceY,cx+5,audienceY+7},parent,"audience_space");
        surface({"",0,cx-2,audienceY,cx+3,room.y1-2},parent,"royal_carpet",true);
    }
    // Route every door to a shared room hub before adding any secondary
    // furniture. The reserved bands are 4 blocks wide (6 in public halls),
    // with the 4-block doorway as their only local narrowing.
    for(int parent:rooms){
        const auto room=plan.regions[parent];if(room.kind!="room"||room.role=="corridor")continue;
        const int cx=(room.x0+room.x1-1)/2,cy=room.role=="throne_room"?room.y0+throne.d+8:(room.y0+room.y1-1)/2;
        auto inside=[&](int x,int y){return x>=room.x0+1&&y>=room.y0+1&&x+1<=room.x1-1&&y+1<=room.y1-1&&pass(x,y);};
        int hub=-1,best=100000;
        for(int y=room.y0+1;y<room.y1-1;++y)for(int x=room.x0+1;x<room.x1-1;++x)
            if(inside(x,y)&&std::abs(x-cx)+std::abs(y-cy)<best){best=std::abs(x-cx)+std::abs(y-cy);hub=y*w+x;}
        if(hub<0){bare(room.role+" has no circulation hub");continue;}
        std::vector<int> previous(area,-1);std::queue<int> pending;previous[hub]=hub;pending.push(hub);
        while(!pending.empty()){const int i=pending.front();pending.pop();
            for(auto step:std::array<std::pair<int,int>,4>{{{0,1},{1,0},{-1,0},{0,-1}}}){
                const int x=i%w+step.first,y=i/w+step.second,j=y*w+x;
                if(inside(x,y)&&previous[j]<0){previous[j]=i;pending.push(j);}
            }
        }
        std::vector<std::uint8_t> band(area,0);
        for(int i=0;i<architectural;++i){const auto door=plan.regions[i];
            if(door.kind=="room_connection"||door.kind=="castle_side_door"){
                if(door.parentId!=room.id&&door.linkedRegionId!=room.id)continue;
            }else if(door.kind!="castle_front_door"||!IsCastleEntryRole(room.role))continue;
            const int dx=std::clamp((door.x0+door.x1-1)/2,room.x0+1,room.x1-2);
            const int dy=std::clamp((door.y0+door.y1-1)/2,room.y0+1,room.y1-2);
            int start=-1,cost=100000;
            for(int y=std::max(room.y0+1,dy-4);y<=std::min(room.y1-2,dy+4);++y)
                for(int x=std::max(room.x0+1,dx-4);x<=std::min(room.x1-2,dx+4);++x)
                    if(previous[y*w+x]>=0&&std::abs(x-dx)+std::abs(y-dy)<cost){cost=std::abs(x-dx)+std::abs(y-dy);start=y*w+x;}
            if(start<0)continue;
            const int margin=room.role=="throne_room"||IsCastleEntryRole(room.role)?2:1;
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
    const double tw=std::clamp((plan.countryWealth-0.7)/1.8,0.0,1.0);
    const auto cooking_pot=Native(params,4,23,2,2,3),bucket=Native(params,4,41,2,2,2),piano=Native(params,4,40,4,2,3);
    for(int parent:rooms){
        const auto room=plan.regions[parent];const auto role=room.role;
        const RoomKind kind=ClassifyRoom(role,plan.archetype);
        const double r=RoomRichness(kind,tw,plan.archetype);
        // How many of an item to put: from `poor` (a poor castle) to `rich` (a rich one), by the room's richness r.
        auto Count=[&](int poor,int rich){return poor+static_cast<int>((rich-poor)*r+0.5);};
        const int left=room.x0+2,top=room.y0+2,right=room.x1-1,bottom=room.y1-1;
        const int cx=(left+right)/2,cy=(top+bottom)/2;
        auto put=[&](const std::string& name,Object object,int x,int y,int rot=0){return place(parent,name,object,x,y,rot);};
        // Items standing against the walls: along the top, the bottom, the left and the right wall in turn, at a pitch of the item's
        // width plus a gap, until `want` are placed (an item that does not fit, e.g. before a door, is skipped, never slid).
        auto wallRun=[&](const std::string& name,Object o,int want){
            int n=0;
            for(int x=left;n<want&&x+o.w-1<=right;x+=o.w+1)if(put(name,o,x,top)>=0)++n;
            for(int x=left;n<want&&x+o.w-1<=right;x+=o.w+1)if(put(name,o,x,bottom-o.d+1)>=0)++n;
            for(int y=top+o.d+1;n<want&&y+o.d-1<=bottom-o.d;y+=o.d+1)if(put(name,o,left,y)>=0)++n;
            for(int y=top+o.d+1;n<want&&y+o.d-1<=bottom-o.d;y+=o.d+1)if(put(name,o,right-o.w+1,y)>=0)++n;
            return n;
        };
        // Rows of long tables: each table is a chain of 4x4 tables at a pitch of 5 along the room's long axis, with a bench along each side.
        // `rows` rows of 12 across; `dishes`: put a dish on every table (otherwise on none). Returns the number of tables placed.
        auto longTables=[&](const std::string& name,int rows,bool dishes){
            const bool horiz=right-left>=bottom-top;
            const int L=(horiz?right-left:bottom-top)+1,Wd=(horiz?bottom-top:right-left)+1;
            const int count=std::max(1,(L-6)/5),u0=std::max(0,(L-(count*5-1))/2);
            rows=std::max(1,std::min(rows,(Wd+2)/12));
            const int v0=std::max(0,(Wd-(rows*12-2))/2);
            int placed=0;
            for(int row=0;row<rows;++row){
                const int v=v0+row*12;
                for(int k=0;k<count;++k){
                    const int u=u0+5*k;
                    const int tx=left+(horiz?u:v+3),ty=top+(horiz?v+3:u);
                    const int ti=put(name,table,tx,ty);
                    if(ti<0)continue;
                    ++placed;
                    if(dishes)detail(ti,"banquet_dishes",dish);
                    // Benches face the table: (horizontal) the upper one south (0), the lower one north (2); (vertical) the left one east (3), the right one west (1).
                    put("banquet_bench",bench,left+(horiz?u:v),top+(horiz?v:u),horiz?0:3);
                    put("banquet_bench",bench,left+(horiz?u:v+8),top+(horiz?v+8:u),horiz?2:1);
                }
            }
            return placed;
        };
        // The doors of this room, each {side (0 top, 1 right, 2 bottom, 3 left), x0, y0, x1, y1}.
        auto roomDoors=[&](){
            std::vector<std::array<int,5>> found;
            for(const auto& dr:plan.regions){
                if(dr.kind!="castle_door"||(dr.parentId!=room.id&&dr.linkedRegionId!=room.id))continue;
                found.push_back({dr.rotation%2==0?(dr.y0<=room.y0+1?0:2):(dr.x0<=room.x0+1?3:1),dr.x0,dr.y0,dr.x1,dr.y1});
            }
            return found;
        };
        // A carpet strip (kept clear of furniture); when `draw` is false only the path is kept clear.
        auto carpetRect=[&](int x0,int y0,int x1,int y1,bool draw){
            x0=std::max(x0,left);y0=std::max(y0,top);x1=std::min(x1,right);y1=std::min(y1,bottom);
            if(x1<x0||y1<y0)return;
            if(draw)surface({"",0,x0,y0,x1,y1},parent,"royal_carpet",true);
            else reserve({"",0,x0,y0,x1,y1},parent,"carpet_path");
        };
        if(room.kind=="castle_activity"){
            surface(room,parent,"drill_yard",false);
            if(put("training_dummy",{-2,0,4,2,5},left,top)<0){error="Drill yard has no usable practice target";return false;}
            put("training_dummy",{-2,0,4,2,5},right-3,top);
            put("training_weapon_rack",sword,left,bottom);
            put("training_bench",bench,right-3,bottom-1);continue;
        }
        if(kind==RoomKind::Throne){
            put("royal_guard_sword",sword,left,top);put("royal_guard_shield",shield,right-2,top);
            // Candles beside the throne and along the carpet, statues by the side walls: the richer the court, the more.
            int throneAt=-1;
            for(int id:fixtures)if(plan.regions[id].role=="royal_throne"&&plan.regions[id].parentIndex==parent)throneAt=id;
            if(throneAt>=0){
                const auto t=plan.regions[throneAt];
                // Candles in pairs from the back of the room: one pair beside the throne, then pairs along the carpet every 6 cells. The richer
                // the court, the farther down the room they reach (never thinned out along the way).
                std::vector<std::array<int,3>> spots{{t.x0-3,t.x1+2,t.y0}};
                for(int yy=t.y1+3;yy<bottom-2;yy+=6)spots.push_back({cx-6,cx+6,yy});
                const int pairs=Count(2,static_cast<int>(spots.size()));
                int lit=0;
                for(const auto& spot:spots){
                    if(lit>=pairs)break;
                    const int a=put("throne_candle",candle,spot[0],spot[2]),b=put("throne_candle",candle,spot[1],spot[2]);
                    if(a>=0||b>=0)++lit;
                }
            }
            if(r>=0.5){put("throne_statue",statue,left,top+3);put("throne_statue",statue,right-3,top+3);}
            if(r>=0.8){put("throne_statue",statue,left,top+9);put("throne_statue",statue,right-3,top+9);}
        }else if(kind==RoomKind::Scullery){
            // A scullery has no stove: wash buckets along a wall, a counter, a pot and a barrel.
            for(int n=0,x=left;n<Count(2,4)&&x<right-1;x+=3,++n)put("wash_bucket",bucket,x,top);
            put("preparation_counter",counter,right-3,bottom-1);put("cooking_pot",cooking_pot,left,bottom-1);put("pantry_barrel",barrel,right-2,top);
        }else if(kind==RoomKind::Kitchen){
            // A wall of fire along the top (stoves, each with its pot beside it), a line of counters along the bottom wall,
            // work islands in the middle (with dishes in a rich kitchen) and the stores in the corners.
            int stoves=0;
            for(int x=left;stoves<Count(1,3)&&x+3<=right;x+=7)if(put("cooking_stove",stove,x,top)>=0){++stoves;put("cooking_pot",cooking_pot,x+5,top);}
            if(stoves==0)for(auto anchor:{std::pair<int,int>{right-3,top},{left,bottom-1},{right-3,bottom-1}}){if(stoves==0&&put("cooking_stove",stove,anchor.first,anchor.second)>=0)++stoves;}
            if(stoves==0)bare(role+" has no stove");
            for(int n=0,x=left;n<Count(1,4)&&x+3<=right;x+=5,++n)put("preparation_counter",counter,x,bottom-1);
            const int islands=Count(1,3);
            for(int k=0;k<islands;++k){
                const int work=put("kitchen_worktable",table,cx-1+(k%2?4:-4)*(k>0?1:0),cy-3+(k>1?7:0));
                if(r>=0.5)detail(work,"prepared_dishes",dish);
            }
            put("pantry_barrel",barrel,left,bottom-4);put("water_bucket",bucket,right-1,bottom-1);
            if(r>=0.6)put("pantry_barrel",barrel,right-2,top+4);
        }else if(kind==RoomKind::Corridor){
            // A corridor holds only candelabra, on the wall-side lines so that the walkway stays
            // free: a pair of cells apart along each side, alternating between the two walls.
            const bool vertical=room.y1-room.y0>room.x1-room.x0;
            // Wherever a door's clearance leaves room: at least 7 apart, alternating between the walls.
            int side=0,placed=0,lastAlong=-100;
            for(int along=(vertical?room.y0:room.x0)+2;along<(vertical?room.y1:room.x1)-1&&placed<8;++along){
                if(along-lastAlong<7)continue;
                const int first=(vertical?room.x0:room.y0)+1,last=vertical?room.x1:room.y1;
                for(int attempt=0;attempt<2;++attempt){
                    const int across=((side+attempt)&1)?last:first;
                    if(place(parent,"corridor_candelabrum",candle,vertical?across:along,vertical?along:across,vertical?1:0,1,1)>=0){
                        ++placed;lastAlong=along;side=(side+attempt+1)&1;break;
                    }
                }
            }
            continue;
        }else if(kind==RoomKind::Antechamber){
            // The waiting room before the throne room: a carpet 4 cells wide runs from each door at a right angle into the room; doors
            // that are not in line are joined by a band across the middle (a Z: down from one door, across, down to the other), then
            // benches for the people waiting and candelabra at the corners. It is not a bedroom, so it never gets beds.
            {
                const auto doors=roomDoors();
                const bool draw=r>=0.6;
                // The carpet is 4 cells wide (the width of a door): a strip centred on c covers lo(c)..hi(c).
                const auto lo=[](int c){return c-1;};
                const auto hi=[&](int c){return lo(c)+3;};
                const int ym=(top+bottom)/2;
                int sum=0,count=0;
                for(const auto& dr:doors)if(dr[0]==0||dr[0]==2){sum+=(dr[1]+dr[3])/2;++count;}
                const int xc=count>0?sum/count:cx;
                int lowest=xc,highest=xc;
                for(const auto& dr:doors){
                    const int c=(dr[1]+dr[3])/2;
                    if(dr[0]==0){carpetRect(lo(c),top,hi(c),hi(ym),draw);lowest=std::min(lowest,c);highest=std::max(highest,c);}
                    else if(dr[0]==2){carpetRect(lo(c),lo(ym),hi(c),bottom,draw);lowest=std::min(lowest,c);highest=std::max(highest,c);}
                }
                carpetRect(lo(lowest),lo(ym),hi(highest),hi(ym),draw);
                for(const auto& dr:doors){
                    const int c=(dr[2]+dr[4])/2;
                    if(dr[0]==3)carpetRect(left,lo(c),hi(xc),hi(c),draw);
                    else if(dr[0]==1)carpetRect(lo(xc),lo(c),right,hi(c),draw);
                }
            }
            put("waiting_bench",bench,left,cy,3);put("waiting_bench",bench,right-1,cy,1);
            put("waiting_candelabrum",candle,left,top);put("waiting_candelabrum",candle,right-1,top);
            put("waiting_candelabrum",candle,left,bottom-1);put("waiting_candelabrum",candle,right-1,bottom-1);
            put("waiting_table",table,cx-1,top);
        }else if(kind==RoomKind::Bedroom||kind==RoomKind::PrivateRoom||kind==RoomKind::Barracks){
            const bool royal=role=="royal_chambers";
            // The king's and the queen's bedchambers hold one royal bed (of the ordinary size), not rows of garrison beds;
            // a private room (castellan, captain, officers, guests) holds one ordinary bed, a desk and a chest.
            const char* bedName=royal||role=="royal_bedchamber"||role=="queens_bedchamber"?"royal_bed":"garrison_bed";
            // The bed stands on the axis (the middle of the top wall); a chest on each side, a bench at its foot.
            const bool royalSize=royal||(role=="royal_bedchamber"&&r>=0.5);
            const Object bedModel=royalSize?royalBed:bed;
            int index=-1;
            if(kind!=RoomKind::Barracks){
                index=put(bedName,bedModel,cx-bedModel.w/2,top);
                if(index>=0){
                    const auto bd=plan.regions[index];
                    if(r>=0.3){put("night_chest",chest,bd.x0-3,top);if(r>=0.5)put("night_chest",chest,bd.x1+2,top);}
                    if(r>=0.5)put("bed_candle",candle,bd.x0-1,top);
                    if(r>=0.6)put("foot_bench",bench,bd.x0,bd.y1+2);
                    // A rich bedchamber lays a carpet from the bed's foot.
                    if(kind==RoomKind::Bedroom&&r>=0.55)surface({"",0,std::max(left,bd.x0-1),bd.y1+1,std::min(right,bd.x1+1),std::min(bottom,bd.y1+4)},parent,"royal_carpet",false);
                }
            }
            if(index<0)index=royal?put("royal_bed",royalBed,left,top):put(bedName,bed,left,top);
            if(index<0)index=put(bedName,bed,left,top);
            for(auto anchor:{std::pair<int,int>{right-3,top},{left,bottom-3},{right-3,bottom-3}}){
                if(index>=0)break;
                index=put(bedName,royal?royalBed:bed,anchor.first,anchor.second);
                if(index<0&&royal)index=put("royal_bed",bed,anchor.first,anchor.second);
            }
            // A bedchamber of the private suite, or a narrow one (under 12 wide, between two doors), may stay without its bed;
            // every other bedroom needs one.
            if(index<0&&(kind!=RoomKind::Bedroom||royal)&&room.x1-room.x0+1>=12)bare(role+" has no bed ("+std::to_string(room.x1-room.x0+1)+"x"+std::to_string(room.y1-room.y0+1)+")");
            if(kind==RoomKind::Barracks){
                // Beds in two rows along the long walls, a rack of swords by the door side and a table between.
                for(int x=left+4;x<=right-1;x+=5){put("garrison_bed",bed,x,top);put("garrison_bed",bed,x,bottom-3);}
                put("barracks_rack",sword,left,bottom);
                if(r>=0.5)put("barracks_chest",chest,right-1,cy);
            }else{
                put("wardrobe",chest,right-1,top);put("bedroom_bookcase",shelf,right-3,bottom-1);
                if(kind==RoomKind::Bedroom||r>=0.4){const int desk=put("bedroom_desk",table,cx-1,bottom-3);detail(desk,"bedroom_book",book);put("bedroom_chair",chair,cx-1,cy);}
            }
        }else if(kind==RoomKind::Dressing){
            for(int n=0,x=left;n<Count(2,6)&&x<right-1;x+=3,++n)put("wardrobe",chest,x,top);
            put("dressing_bench",bench,cx-1,cy,1);
        }else if(kind==RoomKind::Ballroom){
            MapPlanRegion dance{"",0,left+4,top+4,right-4,bottom-4};
            surface(dance,parent,"ballroom_floor",true);
            int music=put("ballroom_piano",piano,left,top);
            if(music<0)music=put("ballroom_piano",piano,right-3,top);
            // The piano is optional: a ballroom without room for it is still a ballroom.
            (void)music;
            for(int n=0,yy=top+6;n<Count(1,3)&&yy+3<=bottom-6;yy+=6,++n){put("ballroom_bench",bench,left,yy,3);put("ballroom_bench",bench,right-1,yy,1);}
            put("ballroom_candelabrum",candle,left,top);put("ballroom_candelabrum",candle,right-1,top);
            if(r>=0.5){put("ballroom_candelabrum",candle,left,bottom);put("ballroom_candelabrum",candle,right-1,bottom);}
            if(r>=0.4){const int snack=put("refreshment_table",table,cx-1,bottom-3);detail(snack,"refreshments",dish);}
        }else if(kind==RoomKind::Hall){
            const bool military=plan.archetype=="fortress"||plan.archetype=="hill";
            const bool trading=plan.archetype=="river";
            if(trading){
                // A customs hall: a line of counters across the middle with their ledgers, shelves of ledgers along the back wall, benches for those who wait.
                const bool horiz=right-left>=bottom-top;
                for(int u=horiz?left+3:top+3;(horiz?u+3<=right-3:u+3<=bottom-3);u+=5){
                    const int ti=put("customs_counter",counter,horiz?u:cx-1,horiz?cy-1:u);
                    detail(ti,"hall_documents",documents);
                }
                wallRun("customs_ledger_shelf",shelf,Count(1,4));
                for(int n=0,x=left;n<Count(1,3)&&x+3<=right;x+=5,++n)put("customs_bench",bench,x,bottom-1,2);
                put("trade_barrel",barrel,right-2,bottom-2);
                if(r>=0.5)put("trade_barrel",barrel,left,bottom-2);
            }else if(military){
                // A guard hall: racks of swords and shields side by side along the walls, one mess table with benches, a command desk with its documents.
                for(int n=0,x=left;n<Count(4,8)&&x+3<=right;x+=4,++n){if(n%2==0)put("guard_weapon_rack",sword,x,top);else put("guard_shield",shield,x,top);}
                longTables("garrison_mess_table",1,false);
                const int cmd=put("command_table",table,right-3,bottom-3);detail(cmd,"hall_documents",documents);
                put("guard_barrel",barrel,left,bottom-2);if(r>=0.5)put("guard_bucket",bucket,left+4,bottom-1);
            }else{
                // A banquet hall: rows of long tables with benches on both sides; the richer the hall, the more rows, with dishes and candles.
                longTables("banquet_table",Count(1,3),r>=0.4);
                if(r>=0.6){put("hall_candle",candle,left,top);put("hall_candle",candle,right-1,top);put("hall_candle",candle,left,bottom);put("hall_candle",candle,right-1,bottom);}
                wallRun("hall_barrel",barrel,Count(0,2));
            }
        }else if(kind==RoomKind::Council){
            // The council table in the middle with its chairs on both sides; bookcases and maps along the walls, more in a rich court.
            const int ti=put("council_table",table,cx-1,cy-1);detail(ti,"council_documents",documents);
            if(ti>=0){const auto t=plan.regions[ti];put("council_chair",chair,t.x0,t.y0-3);put("council_chair",chair,t.x0,t.y1+2,2);
                if(r>=0.5){put("council_chair",chair,t.x0-3,t.y0,3);put("council_chair",chair,t.x1+2,t.y0,1);}}
            wallRun("council_bookcase",shelf,Count(1,4));
            if(role=="war_room"){put("war_room_rack",sword,right-3,top);if(r>=0.5)put("war_room_shield",shield,left,bottom);}
            if(r>=0.6){put("council_candle",candle,left,bottom);put("council_candle",candle,right-1,top);}
        }else if(kind==RoomKind::Chapel){
            // The altar stands on the wall opposite the entrance. A door in the top wall would spoil that: the chapel is then turned a quarter
            // turn right (the altar on the right wall); doors in the top and right walls turn it left (the altar on the left wall); doors in
            // the top, left and right walls leave it as it is. All the work below is done in a local frame (u across, d from the altar wall).
            const auto doors=roomDoors();
            bool topDoor=false,rightDoor=false,leftDoor=false;
            for(const auto& dr:doors){topDoor|=dr[0]==0;rightDoor|=dr[0]==1;leftDoor|=dr[0]==3;}
            const int turn=!topDoor?0:rightDoor?(leftDoor?0:2):1;  // 0 as it is, 1 turned right, 2 turned left
            const int Wl=turn==0?right-left+1:bottom-top+1,Dl=turn==0?bottom-top+1:right-left+1;
            const auto wx=[&](int u,int d){return turn==0?left+u:turn==1?right-d:left+d;};
            const auto wy=[&](int u,int d){return turn==0?top+d:turn==1?top+u:bottom-u;};
            const auto lu=[&](int x,int y){return turn==0?x-left:turn==1?y-top:bottom-y;};
            const auto ldp=[&](int x,int y){return turn==0?y-top:turn==1?right-x:x-left;};
            const auto wr=[&](int u0,int d0,int u1,int d1){
                const int xa=wx(u0,d0),xb=wx(u1,d1),ya=wy(u0,d0),yb=wy(u1,d1);
                return std::array<int,4>{std::min(xa,xb),std::min(ya,yb),std::max(xa,xb),std::max(ya,yb)};
            };
            // Put an object whose footprint is w x d in the local frame; `rot` is its world rotation (odd when the frame is turned).
            const auto putLocal=[&](const std::string& name,Object o,int u,int d,int rot){
                const auto q=wr(u,d,u+o.w-1,d+o.d-1);
                return put(name,o,q[0],q[1],rot);
            };
            const int benchRot=turn==0?2:turn==1?3:1,faceRot=turn==0?0:turn==1?1:3,candleRot=turn==0?0:1;
            // The altar in the middle of its wall (it steps aside only if a door's landing is in the way).
            int au0=Wl/2-2,altar=-1;
            for(const int shift:{0,4,-4,8,-8}){
                if(au0+shift<2||au0+shift+4>Wl-2)continue;
                altar=putLocal("chapel_altar",table,au0+shift,0,0);
                if(altar>=0){au0+=shift;break;}
            }
            if(altar>=0)detail(altar,"altar_book",book);
            // The carpet: an aisle 2 wide from the altar, and from every door a strip at a right angle to its wall that meets it.
            // The carpet is 2 cells wide: a strip centred on c covers lo(c)..hi(c); the aisle is the middle of the 4-wide altar.
            const bool draw=r>=0.4;
            const int ca=au0+1,aF=4;
            const auto lo=[](int c){return c;};
            const auto hi=[](int c){return c+1;};
            const auto strip=[&](int u0,int d0,int u1,int d1){const auto q=wr(u0,d0,u1,d1);carpetRect(q[0],q[1],q[2],q[3],draw);};
            int aisleEnd=-1;
            for(const auto& dr:doors){
                static const int toLocal1[4]={3,0,1,2},toLocal2[4]={1,2,3,0};  // world side -> local side (0 altar wall, 1 right, 2 entrance wall, 3 left)
                const int side=turn==0?dr[0]:turn==1?toLocal1[dr[0]]:toLocal2[dr[0]];
                const int xm=(dr[1]+dr[3])/2,ym=(dr[2]+dr[4])/2,uc=lu(xm,ym),dc=ldp(xm,ym);
                if(side==2){                      // in the entrance wall: straight in, and across to the aisle if it is not in line
                    aisleEnd=Dl-1;
                    if(std::abs(uc-ca)>1){
                        const int dj=std::max(aF+2,Dl-5);
                        strip(lo(std::min(uc,ca)),lo(dj),hi(std::max(uc,ca)),hi(dj));
                        strip(lo(uc),dj,hi(uc),Dl-1);
                    }
                }else if(side==1||side==3){       // in a side wall: straight across to the aisle
                    const int row=std::clamp(std::max(dc,aF+1),1,Dl-2);
                    aisleEnd=std::max(aisleEnd,hi(row));
                    if(side==3)strip(0,lo(row),hi(ca),hi(row));else strip(lo(ca),lo(row),Wl-1,hi(row));
                }else if(hi(uc)<au0||lo(uc)>au0+3){  // in the altar wall (only when the chapel is not turned): down beside the altar, then across
                    strip(lo(uc),0,hi(uc),hi(aF));
                    strip(lo(std::min(uc,ca)),aF,hi(std::max(uc,ca)),hi(aF));
                }
            }
            strip(lo(ca),aF,hi(ca),aisleEnd<0?Dl-1:std::max(aisleEnd,hi(aF)));
            {const auto q=wr(0,0,1,0);put("chapel_candle",candle,q[0],q[1],candleRot);}
            {const auto q=wr(Wl-2,0,Wl-1,0);put("chapel_candle",candle,q[0],q[1],candleRot);}
            if(r>=0.5){putLocal("chapel_statue",statue,au0-5,0,faceRot);putLocal("chapel_statue",statue,au0+5,0,faceRot);}
            // Pews in aligned rows, 4 apart, on both sides of the aisle, all facing the altar; more rows and more pews a side in a rich chapel.
            const int rowsMax=Count(4,10),perSide=Count(2,4);
            int rows=0;
            for(int d=aF+2;d+1<=Dl-2&&rows<rowsMax;d+=4,++rows){
                for(int k=0;k<perSide;++k){
                    putLocal("chapel_pew",bench,ca-5-5*k,d,benchRot);
                    putLocal("chapel_pew",bench,ca+3+5*k,d,benchRot);
                }
            }
        }else if(kind==RoomKind::Armory){
            // Swords and shields side by side along the top wall, a worktable and a chest.
            for(int n=0,x=left;n<Count(3,6)&&x<right-2;x+=4,++n){if(n%2==0)put("weapon_rack",sword,x,top);else put("shield_rack",shield,x,top);}
            if(r>=0.6)for(int n=0,x=left;n<Count(0,6)&&x<right-2;x+=4,++n){if(n%2==0)put("weapon_rack",sword,x,bottom);else put("shield_rack",shield,x,bottom);}
            put("armory_worktable",table,left,bottom-3-(r>=0.6?2:0));put("armory_chest",chest,right-1,cy);
            if(r>=0.5)put("armory_barrel",barrel,right-2,top+3);
        }else if(kind==RoomKind::GuardRoom||kind==RoomKind::Signal){
            put("weapon_rack",sword,left,top);put("shield_rack",shield,right-2,top);
            if(kind==RoomKind::Signal)put("signal_brazier",stove,cx-1,top);
            const int gt=put("guard_table",table,left,bottom-3);
            if(gt>=0){const auto t=plan.regions[gt];put("guard_chair",chair,t.x1+2,t.y0);detail(gt,"guard_meal",dish);}
            put("guard_bench",bench,right-3,bottom-1);put("guard_barrel",barrel,right-2,cy);
        }else if(kind==RoomKind::Workshop){
            put("forge_stove",stove,left,top);put("forge_bucket",bucket,left+5,top);
            put("work_counter",counter,right-3,top);put("work_counter",counter,right-3,bottom-1);
            put("product_rack",sword,left,bottom);put("work_barrel",barrel,right-2,cy);
        }else if(kind==RoomKind::Treasury){
            // Chests along the top wall and the bottom wall; a ledger table in the middle for a richer vault.
            wallRun("storage_chest",chest,Count(2,10));
            if(r>=0.5){const int ledger=put("ledger_table",table,cx-1,cy);detail(ledger,"ledger_documents",documents);}
            if(role=="strongroom")put("strongroom_barrel",barrel,right-2,cy);
            if(r>=0.85)put("treasury_statue",statue,left,bottom-3);
        }else if(kind==RoomKind::Storage){
            // A grid of barrels with aisles, chests along a wall; the richer the castle, the fuller the store.
            const int barrels=Count(3,14);
            int n=wallRun("storage_barrel",barrel,barrels);
            for(int y=top+4;n<barrels&&y+2<=bottom-4;y+=5)for(int x=left+4;n<barrels&&x+2<=right-4;x+=4){if(put("storage_barrel",barrel,x,y)>=0)++n;}
            for(int k=0,x=left;k<Count(1,3)&&x<right-1;x+=3,++k)put("storage_chest",chest,x,cy);
        }else if(kind==RoomKind::Library){
            // Bookcases along the walls; a rich library adds parallel stacks with aisles between them; a reading desk with its chair and a book.
            wallRun("library_bookcase",shelf,Count(3,12));
            if(r>=0.6)for(int yy=top+6;yy+1<=bottom-6;yy+=6)for(int xx=left+5;xx+3<=right-5;xx+=5)put("library_stack",shelf,xx,yy);
            const int desk=put("reading_desk",table,right-3,bottom-3);detail(desk,"reading_book",book);put("reading_chair",chair,cx,cy);
        }else if(kind==RoomKind::Office){
            for(int n=0,x=left+4;n<Count(1,3)&&x+3<=right;x+=5,++n)put("office_counter",counter,x,top);
            const int desk=put("office_desk",table,right-3,bottom-3);detail(desk,"office_documents",documents);
            put("office_bookcase",shelf,left,top);put("office_bench",bench,left,bottom-1);if(r>=0.5)put("office_chest",chest,right-1,top);
        }else if(kind==RoomKind::Music){
            put("music_piano",piano,cx-1,top);put("music_bench",bench,cx-1,top+3,1);
            put("music_bookcase",shelf,left,top);put("music_candle",candle,right-1,top);put("music_candle",candle,left,bottom);
            put("audience_bench",bench,left,cy,1);put("audience_bench",bench,right-1,cy,1);
        }else{
            for(int xx=left;xx<right-2;xx+=6)put("study_bookcase",shelf,xx,top);
            const int desk=put("study_desk",table,right-3,bottom-3);detail(desk,"study_book",book);put("study_chair",chair,cx,cy);
        }
        // (the candle that every room used to get in its bottom-left corner is gone: candles are part of each room's own layout)
        if(r>=0.5&&kind!=RoomKind::Barracks&&kind!=RoomKind::Armory&&kind!=RoomKind::GuardRoom)put("room_candle",candle,left,bottom);
    }
    if(!accessible()||!wideAccessible()){error="Castle furnishing blocks a room, wide aisle or furniture approach";return false;}
    plan.castleHistory.push_back({"interior_design","native_fixtures_from_castle_type_and_room_purpose",static_cast<int>(fixtures.size()),1.0});
    return true;
}
}
