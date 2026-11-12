#include "SettlementRelief.h"
#include <iostream>
#include <stdexcept>
int main() {
    for(int waterBiome : {7,8,15}) {
        MapPlan p;p.Reset(96,96,42,"village");p.biomes.assign(96*96,0);
        // A straight shore makes the expected profile explicit, including
        // erosion of the old six-block cliff bonus rather than uplift alone.
        for(int y=0;y<96;++y) for(int x=0;x<96;++x) {
            if(x<20) p.biomes[y*96+x]=static_cast<unsigned char>(waterBiome);
            else if(y<40) p.biomes[y*96+x]=16;
        }
        ApplySettlementShore(p);
        for(int y : {20,70}) for(int x=20;x<60;++x) {
            const int surface=(y<40?6:0)-p.shoreCuts[y*96+x];
            if(x<23 && surface!=-1) throw std::runtime_error("Water edge is not lowered to a low bank");
            if(x>20) {
                int previous=(y<40?6:0)-p.shoreCuts[y*96+x-1];
                if(surface<previous||surface-previous>1) throw std::runtime_error("Steep or reversed bank profile");
            }
        }
        for(int y=0;y<96;++y) for(int x=0;x<20;++x)
            if(p.shoreCuts[y*96+x]) throw std::runtime_error("Water level changed");
        const auto original=p.shoreCuts;ApplySettlementShore(p);
        if(original!=p.shoreCuts) throw std::runtime_error("Shore erosion is not deterministic");
        MapPlanRegion r;r.kind="asset_plot";r.x0=22;r.x1=32;r.y0=50;r.y1=64;p.regions.push_back(r);
        for(int x=18;x<40;++x)p.biomes[80*96+x]=x<20?6:5;
        ApplySettlementShore(p);
        for(int y=48;y<=66;++y)for(int x=20;x<=34;++x)
            if(p.shoreCuts[y*96+x]) throw std::runtime_error("Foundation setback eroded");
        for(int x=18;x<40;++x)if(p.shoreCuts[80*96+x]) throw std::runtime_error("Road or bridge eroded");
    }
    for (unsigned seed = 0; seed < 30; ++seed) {
        MapPlan p; p.Reset(160,160,seed,"village"); p.biomes.assign(25600,0);
        for (int y=0;y<160;++y) for(int x=76;x<84;++x) p.biomes[y*160+x]=5;
        MapPlanRegion r; r.kind="asset_plot"; r.x0=88;r.x1=102;r.y0=70;r.y1=86;
        p.regions.push_back(r);
        for (int x=0;x<160;++x) p.biomes[x]=7;
        ApplySettlementRelief(p,8);
        int maximum=0;
        for(int y=0;y<160;++y) for(int x=0;x<160;++x) {
            int i=y*160+x,h=p.heightOffsets[i]; maximum=std::max(maximum,h);
            if((p.biomes[i]==5 || p.biomes[i]==7 || (x>=86&&x<=104&&y>=68&&y<=88)) && h)
                throw std::runtime_error("Infrastructure or water raised");
            if(x && std::abs(h-p.heightOffsets[i-1])>1) throw std::runtime_error("Abrupt X grade");
            if(y && std::abs(h-p.heightOffsets[i-160])>1) throw std::runtime_error("Abrupt Y grade");
        }
        if(maximum<3) throw std::runtime_error("Missing hills");
        auto original=p.heightOffsets; ApplySettlementRelief(p,8);
        if(original!=p.heightOffsets) throw std::runtime_error("Not deterministic");
        ApplySettlementRelief(p,0);
        for(int h:p.heightOffsets) if(h) throw std::runtime_error("Flat setting ignored");
    }
    std::cout << "River/lake/sea bank profiles and protected bridge/foundations passed; 30 relief seeds passed\n";
}
