#pragma once
#include "MapImageGenerator.h"
#include <algorithm>
#include <vector>

namespace settlement_civic {
struct Equipment { std::string role; int type,variant,w,d,h; };
inline double Wealth(const MapPlan& plan,const MapGenParams& params){
    const auto it=params.extra.find("countryWealth");
    return std::clamp(it==params.extra.end()?plan.countryWealth:it->second,0.7,2.5);
}
inline std::vector<Equipment> Program(const MapGenParams& params,double wealth){
    auto object=[&](const char* role,int type,int variant,int w,int d,int h){
        const auto key="civicObject"+std::to_string(type)+"_"+std::to_string(variant);
        auto get=[&](const std::string& suffix,int fallback){auto it=params.extra.find(key+suffix);return it==params.extra.end()?fallback:static_cast<int>(it->second);};
        return Equipment{role,type,variant,get("Width",w),get("Depth",d),get("Height",h)};
    };
    std::vector<Equipment> result{object("community_well",3,26,7,7,4)};
    if(wealth>=1.0)result.push_back(object("public_bench",4,5,4,2,3));
    if(wealth>=1.5)result.push_back(object("square_lamp",2,1,3,3,6));
    if(wealth>=2.0){
        result.push_back(object("market_stall",3,11,6,6,6));
        result.push_back(object("civic_statue",3,14,4,4,6));
    }
    return result;
}
}
