#include "SettlementSite.h"

#include "MapImageGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <utility>

const char* SettlementSiteName(SettlementSite site) {
    static const char* names[] = {"plains", "coast", "mountain_valley", "forest", "river", "lake", "oasis"};
    return names[static_cast<int>(site)];
}
const char* SettlementSiteEconomy(SettlementSite site) {
    static const char* names[] = {"farming_trade", "fishing_sea_trade", "mining_crafts", "forestry",
        "river_trade", "lake_fishing", "caravan_trade"};
    return names[static_cast<int>(site)];
}
const char* SettlementSiteFacility(SettlementSite site) {
    static const char* names[] = {"barn", "fishery", "mine_office", "lumbermill", "warehouse", "fishery", "caravanserai"};
    return names[static_cast<int>(site)];
}

namespace {
struct Point { double u, v, width; };
struct Course { Point a, b; int kind; }; // 0 dry valley, 1 stream, 2 drowned valley
struct Fan { double u, v, du, dv, length, spread, height; };

std::pair<double, double> Rotate(double u, double v, int rotation) {
    for (int i = 0; i < rotation; ++i) { const double before = u; u = 1.0 - v; v = before; }
    return {u, v};
}
double Random(int x, int y, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x9e3779b9U ^
        static_cast<std::uint32_t>(y) * 0x85ebca6bU ^ seed;
    h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16;
    return static_cast<double>(h & 0xffffffU) / 0xffffffU;
}
double Noise(double u, double v, double frequency, std::uint32_t seed) {
    const double x = u * frequency, y = v * frequency;
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const auto smooth = [](double t) { return t * t * (3.0 - 2.0 * t); };
    const double tx = smooth(x - ix), ty = smooth(y - iy);
    const double a = Random(ix, iy, seed) * (1 - tx) + Random(ix + 1, iy, seed) * tx;
    const double b = Random(ix, iy + 1, seed) * (1 - tx) + Random(ix + 1, iy + 1, seed) * tx;
    return a * (1 - ty) + b * ty;
}
Point Mix(const Point& a, const Point& b, double t) {
    return {a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t, a.width + (b.width - a.width) * t};
}
std::vector<Point> AddCourse(std::vector<Course>& result, std::vector<Point> points, int kind) {
    // Controls already come from a seeded drainage graph. Return the smoothed
    // centreline so tributaries attach to the actual bed, not a displaced knot.
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<Point> rounded{points.front()};
        for (std::size_t i = 1; i < points.size(); ++i) {
            rounded.push_back(Mix(points[i - 1], points[i], 0.25));
            rounded.push_back(Mix(points[i - 1], points[i], 0.75));
        }
        rounded.push_back(points.back());
        points = std::move(rounded);
    }
    for (std::size_t i = 1; i < points.size(); ++i) result.push_back({points[i - 1], points[i], kind});
    return points;
}

struct Dice {
    std::uint32_t seed;
    int cursor = 0;
    double Range(double low, double high) { return low + (high-low)*Random(++cursor, 907, seed); }
    int Count(int low, int high) { return std::min(high, low + static_cast<int>(Range(0, high-low+1.0))); }
};

Point Along(const std::vector<Point>& path, double t) {
    const double position = std::clamp(t, 0.0, 1.0)*(path.size()-1);
    const auto index = std::min(path.size()-2, static_cast<std::size_t>(position));
    return Mix(path[index], path[index+1], position-index);
}

struct TerrainSkeleton {
    std::vector<Course> courses;
    std::vector<Fan> fans;
    std::vector<double> coast;
    double coastSlope = 1;
    Point basin{}, pond{};

    std::vector<Point> Trace(std::vector<Point> points, int kind) {
        return AddCourse(courses, std::move(points), kind);
    }
    void Deposit(Point from, Point to, Dice& dice, double scale = 1.0) {
        const double du = to.u-from.u, dv = to.v-from.v;
        const double length = std::hypot(du,dv);
        if (length < .005) return;
        fans.push_back({from.u,from.v,du/length,dv/length,dice.Range(.07,.16)*scale,
            dice.Range(.025,.065)*scale,dice.Range(.07,.15)});
    }
    double CoastAt(double v) const {
        const double at = std::clamp(v,0.0,1.0)*(coast.size()-1);
        const auto index = std::min(coast.size()-2,static_cast<std::size_t>(at));
        const double t = at-index, smooth = t*t*(3-2*t);
        return coast[index]*(1-smooth)+coast[index+1]*smooth;
    }
};

TerrainSkeleton MakeSkeleton(SettlementSite site, std::uint32_t seed, bool extraRiver, bool extraPond) {
    TerrainSkeleton shape;
    Dice dice{seed ^ (0x713a51c9U + static_cast<std::uint32_t>(site)*0x9e3779b9U)};
    if (site == SettlementSite::Coast) {
        const int knots = dice.Count(4,8), mouths = dice.Count(1,4);
        const double shore = dice.Range(.67,.84), amplitude = dice.Range(.045,.14), tilt = dice.Range(-.16,.16);
        shape.coastSlope = dice.Range(.85,1.4);
        for (int n = 0; n < knots; ++n) {
            const double v = static_cast<double>(n)/(knots-1);
            shape.coast.push_back(std::clamp(shore+tilt*(v-.5)+dice.Range(-amplitude,amplitude),.56,.91));
        }
        for (int n = 0; n < mouths; ++n) {
            const double v = .10+.80*(n+dice.Range(.15,.85))/mouths;
            const double bank = shape.CoastAt(v), reach = dice.Range(.07,.23), mouthWidth = dice.Range(.035,.085);
            const Point head{std::max(.50,bank-reach),v+dice.Range(-.10,.10),dice.Range(.013,.030)};
            const auto channel = shape.Trace({head,{bank-.035,v,mouthWidth},
                {1.04,v+dice.Range(-.12,.12),mouthWidth*dice.Range(1.2,2.1)}},2);
            shape.Deposit(Along(channel,.30),Along(channel,.60),dice);
        }
    } else if (site == SettlementSite::Lake) {
        const int knots = dice.Count(3,7), branches = dice.Count(1,5);
        const double top = dice.Range(.07,.24), bottom = dice.Range(.77,.94);
        const double axis = dice.Range(.80,.92), bends = dice.Range(.025,.11), breadth = dice.Range(.06,.125);
        std::vector<Point> controls;
        for (int n = 0; n < knots; ++n) {
            const double t = static_cast<double>(n)/(knots-1);
            controls.push_back({std::clamp(axis+dice.Range(-bends,bends),.72,.96),
                top+(bottom-top)*t,breadth*dice.Range(.55,1.20)});
        }
        const auto trunk = shape.Trace(controls,2);
        shape.basin = Along(trunk,.5);
        for (int n = 0; n < branches; ++n) {
            const Point join = Along(trunk,.10+.80*(n+dice.Range(.1,.9))/branches);
            const Point head{std::max(.49,join.u-dice.Range(.14,.32)),
                std::clamp(join.v+dice.Range(-.14,.14),.10,.90),dice.Range(.016,.032)};
            Point bend = Mix(head,join,dice.Range(.35,.65));
            bend.v += dice.Range(-.055,.055); bend.width = join.width*dice.Range(.40,.75);
            const auto branch = shape.Trace({head,bend,join},2);
            shape.Deposit(Along(branch,.32),join,dice);
        }
        const Point inlet = trunk.front(), outlet = trunk.back();
        shape.Trace({{std::clamp(inlet.u+dice.Range(-.1,.1),.62,.96),-.02,dice.Range(.010,.015)},
            {inlet.u,inlet.v,.019}},1);
        shape.Trace({{outlet.u,outlet.v,.018},
            {std::clamp(outlet.u+dice.Range(-.10,.10),.68,.98),1.02,dice.Range(.020,.029)}},1);
    } else if (site == SettlementSite::Oasis) {
        const int knots = dice.Count(3,7), branches = dice.Count(1,4);
        const double cu = dice.Range(.63,.76), cv = dice.Range(.34,.68);
        const double angle = dice.Range(0,6.283185307179586), du = std::cos(angle), dv = std::sin(angle);
        const double reach = dice.Range(.12,.23), bends = dice.Range(.025,.085), breadth = dice.Range(.032,.058);
        std::vector<Point> controls;
        for (int n = 0; n < knots; ++n) {
            const double along = reach*(2.0*n/(knots-1)-1), across = dice.Range(-bends,bends);
            controls.push_back({std::clamp(cu+du*along-dv*across,.51,.89),
                std::clamp(cv+dv*along+du*across,.12,.89),breadth*dice.Range(.65,1.25)});
        }
        const auto trunk = shape.Trace(controls,2);
        shape.basin = Along(trunk,.5);
        for (int n = 0; n < branches; ++n) {
            const Point join = Along(trunk,dice.Range(.12,.88));
            const double branchAngle = angle + (n%2 ? -1 : 1)*dice.Range(.8,2.2);
            const double length = dice.Range(.07,.17);
            const Point head{std::clamp(join.u+std::cos(branchAngle)*length,.48,.92),
                std::clamp(join.v+std::sin(branchAngle)*length,.10,.92),breadth*dice.Range(.45,.80)};
            Point bend = Mix(head,join,.5); bend.width = breadth*dice.Range(.65,1.1);
            const auto branch = shape.Trace({head,bend,join},2);
            shape.Deposit(Along(branch,.25),join,dice,.7);
        }
    } else {
        const bool mountain = site == SettlementSite::MountainValley;
        const int knots = dice.Count(3,7);
        const double axis = mountain ? dice.Range(.46,.54) : dice.Range(.45,.70);
        const double bends = mountain ? dice.Range(.02,.075) : dice.Range(.04,.13);
        std::vector<Point> controls;
        for (int n = 0; n < knots; ++n) controls.push_back({axis+dice.Range(-bends,bends),
            -.02+1.04*n/(knots-1),mountain ? dice.Range(.23,.31) : dice.Range(.10,.23)});
        const auto trunk = shape.Trace(controls,0);
        if (mountain) {
            const int branches = dice.Count(1,4);
            for (int n = 0; n < branches; ++n) {
                const Point join = Along(trunk,.18+.64*(n+dice.Range(.1,.9))/branches);
                const Point head{n%2 ? dice.Range(.82,.95) : dice.Range(.05,.18),
                    std::clamp(join.v+dice.Range(-.12,.12),.10,.90),dice.Range(.025,.05)};
                Point bend = Mix(head,join,.50); bend.width = dice.Range(.05,.10);
                const auto branch = shape.Trace({head,bend,{join.u,join.v,dice.Range(.12,.16)}},0);
                shape.Deposit(Along(branch,.60),join,dice);
            }
        }
    }
    if (site == SettlementSite::River || extraRiver) {
        // Independent stream so enabling an optional river never changes the
        // already selected regional geology or a remnant pool's parameters.
        Dice river{seed ^ 0xabe84729U};
        const int knots = river.Count(3,8), branches = river.Count(0,3);
        const double axis = river.Range(.60,.76), bends = river.Range(.03,.12);
        const double upstream = river.Range(.013,.021), growth = river.Range(1.35,1.9);
        std::vector<Point> controls;
        for (int n = 0; n < knots; ++n) {
            const double t = static_cast<double>(n)/(knots-1);
            controls.push_back({std::clamp(axis+river.Range(-bends,bends),.50,.88),-.02+1.04*t,upstream*(1+(growth-1)*t)});
        }
        const auto trunk = shape.Trace(controls,1);
        for (int n = 0; n < branches; ++n) {
            const Point join = Along(trunk,.22+.60*(n+river.Range(.1,.9))/std::max(1,branches));
            const Point head{std::min(.99,join.u+river.Range(.12,.25)),std::max(.01,join.v-river.Range(.10,.20)),.009};
            Point bend = Mix(head,join,.50); bend.u += river.Range(-.025,.025); bend.width = .013;
            const auto branch = shape.Trace({head,bend,join},1);
            shape.Deposit(Along(branch,.55),join,river,.65);
        }
        // A point bar remains meaningful even for a reach without tributaries.
        shape.Deposit(Along(trunk,.35),Along(trunk,.6),river,.6);
    }
    if (extraPond && (site == SettlementSite::Plains || site == SettlementSite::Forest)) {
        Dice pool{seed ^ 0x217949b3U};
        const double cu = pool.Range(.66,.80), cv = pool.Range(.62,.80), angle = pool.Range(0,6.283185307179586);
        const double reach = pool.Range(.04,.075), arc = pool.Range(1.7,3.8);
        std::vector<Point> controls;
        for (int n = 0; n < 5; ++n) controls.push_back({cu+reach*std::cos(angle+arc*n/4),
            cv+reach*std::sin(angle+arc*n/4),pool.Range(.016,.024)});
        const auto remnant = shape.Trace(controls,2);
        shape.pond = Along(remnant,.5);
        shape.Deposit(Along(remnant,.20),Along(remnant,.7),pool,.4);
    }
    return shape;
}
}

std::pair<int, int> SettlementResourceAnchor(const MapPlan& plan, SettlementSite site) {
    const int width = plan.width, height = plan.depth, area = width*height;
    if (site == SettlementSite::Plains) {
        const auto [u,v] = Rotate(.40,.72,plan.siteRotation);
        return {static_cast<int>(u*(width-1)),static_cast<int>(v*(height-1))};
    }
    const int resource = site == SettlementSite::Coast ? TOWN_SEA : site == SettlementSite::MountainValley ? TOWN_CLIFF :
        site == SettlementSite::Forest ? TOWN_TREES : site == SettlementSite::River ? RIVER : LAKE;
    std::vector<int> distance(area,area), occupied((width+1)*(height+1),0);
    std::queue<int> queue;
    for (int i = 0; i < area; ++i) {
        if (plan.biomes[i] == resource) { distance[i] = 0; queue.push(i); }
    }
    while (!queue.empty()) {
        const int i = queue.front(); queue.pop();
        const int neighbors[] = {i%width ? i-1 : -1, i%width+1<width ? i+1 : -1,
            i>=width ? i-width : -1, i+width<area ? i+width : -1};
        for (int n : neighbors) {
            if (n < 0 || distance[n] <= distance[i]+1) continue;
            distance[n] = distance[i]+1; queue.push(n);
        }
    }
    // Find useful dry frontage, not just the nearest shoreline pixel. The
    // integral mask rejects slivers and existing plazas/castle/building plots.
    const auto dry = [&](int i) { return plan.settlementMask[i] && plan.biomes[i] <= FOREST; };
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int p = (y+1)*(width+1)+x+1;
        occupied[p] = !dry(y*width+x) + occupied[p-1] + occupied[p-width-1] - occupied[p-width-2];
    }
    int best = -1;
    double bestScore = 1e20;
    const int shortSide = std::min(width,height), margin = std::clamp(shortSide/32,3,5);
    const int frontage = std::clamp(shortSide/20,5,12);
    for (int pass = 0; pass < 2 && best < 0; ++pass) {
        for (int y = margin; y < height-margin; ++y) for (int x = margin; x < width-margin; ++x) {
            const int i = y*width+x;
            if (!dry(i)) continue;
            const int left = x-margin, right = x+margin+1, top = y-margin, bottom = y+margin+1;
            const int blocked = occupied[bottom*(width+1)+right]-occupied[top*(width+1)+right]-
                occupied[bottom*(width+1)+left]+occupied[top*(width+1)+left];
            if (pass == 0 && blocked) continue;
            const double dx = x-width*.47, dy = y-height*.49;
            const double score = std::abs(distance[i]-frontage)*5.0+(dx*dx+dy*dy)/shortSide;
            if (score < bestScore) { bestScore = score; best = i; }
        }
    }
    return best < 0 ? std::pair<int,int>{width/2,height/2} : std::pair<int,int>{best%width,best/width};
}

void PrepareSettlementSite(MapPlan& plan, std::vector<std::uint8_t>& footprint,
    std::vector<std::uint8_t>& structure, SettlementSite site, int rotation,
    std::uint8_t ground, bool village, bool extraRiver, bool extraPond) {
    plan.settlementSite = SettlementSiteName(site);
    plan.siteRotation = rotation;
    plan.localEconomy = SettlementSiteEconomy(site);
    plan.terrainModel = "catchment_and_settlement_v2";
    plan.terrainHistory.clear();
    const int width = plan.width, height = plan.depth, area = width * height;
    const auto seed = plan.seed;
    const auto shape = MakeSkeleton(site, seed, extraRiver, extraPond);
    const auto& courses = shape.courses;
    const auto& fans = shape.fans;

    std::vector<double> elevation(area), hardness(area), incision(area), deposit(area), moisture(area);
    std::vector<double> us(area), vs(area);
    std::vector<std::uint8_t> stream(area, 0), water(area, 0), natural(area, ground);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int i = y * width + x;
        const auto [u,v] = Rotate(static_cast<double>(x) / (width - 1), static_cast<double>(y) / (height - 1), (4 - rotation) % 4);
        us[i] = u; vs[i] = v;
        const double rock = .65 * Noise(u,v,4,seed+79) + .35 * Noise(u,v,10,seed+83);
        hardness[i] = rock;
        double before = .65 + .20 * (Noise(u,v,3,seed+89)-.5) + .09 * (Noise(u,v,8,seed+97)-.5);
        if (site == SettlementSite::Coast) before = .40 + (shape.CoastAt(v)-u)*shape.coastSlope + .08*(Noise(u,v,4,seed+101)-.5);
        if (site == SettlementSite::MountainValley) before += .85 * std::abs(u-.50);
        double after = before;
        for (const auto& course : courses) {
            const double du = course.b.u-course.a.u, dv = course.b.v-course.a.v;
            const double t = std::clamp(((u-course.a.u)*du+(v-course.a.v)*dv) / std::max(1e-10,du*du+dv*dv),0.0,1.0);
            const double cu = course.a.u+du*t, cv = course.a.v+dv*t;
            const double radius = (course.a.width+(course.b.width-course.a.width)*t) * (1.12-.24*rock);
            const double ratio2 = ((u-cu)*(u-cu)+(v-cv)*(v-cv)) / (radius*radius);
            if (ratio2 > 4.0) continue;
            double bed;
            if (course.kind == 0) {
                // Dry terrace floors must stay above standing-water level;
                // an oxbow pool must not inundate the entire ancient valley.
                bed = site == SettlementSite::MountainValley
                    ? .32 + .40*ratio2 + .035*rock : .48 + .22*ratio2 + .035*rock;
            } else if (course.kind == 1) {
                // Downstream surface and bed fall together; greater downstream
                // discharge widens the active channel.
                const double surface = .43-.08*v;
                bed = surface-.085+.085*ratio2+.005*(rock-.5);
                if (ratio2 < .90) stream[i] = 1;
            } else {
                bed = .16+.24*std::pow(ratio2,.8)+.035*(rock-.5);
            }
            after = std::min(after,bed);
        }
        incision[i] = std::max(0.0,before-after);
        for (const auto& fan : fans) {
            const double along = (u-fan.u)*fan.du+(v-fan.v)*fan.dv;
            if (along < 0 || along > fan.length) continue;
            const double across = std::abs((u-fan.u)*fan.dv-(v-fan.v)*fan.du);
            const double spread = fan.spread*(.25+.75*along/fan.length);
            if (across < spread) deposit[i] += fan.height*(1-along/fan.length)*(1-across/spread);
        }
        // A small active thalweg is retained through river point-bar deposits.
        elevation[i] = after + deposit[i]*(stream[i] ? .15 : 1.0);
    }

    const auto adjacent = [&](int i, const auto& visit) {
        if (i%width) visit(i-1);
        if (i%width+1<width) visit(i+1);
        if (i>=width) visit(i-width);
        if (i+width<area) visit(i+width);
    };
    const auto inundate = [&](std::vector<int> starts, int tile) {
        std::queue<int> pending;
        for (int i : starts) if (elevation[i] < .40 && !water[i]) { water[i] = static_cast<std::uint8_t>(tile); pending.push(i); }
        while (!pending.empty()) {
            const int i = pending.front(); pending.pop();
            adjacent(i,[&](int n) {
                if (water[n] || elevation[n] >= .40) return;
                water[n] = static_cast<std::uint8_t>(tile); pending.push(n);
            });
        }
    };
    const auto lowestNear = [&](double u, double v, double reach) {
        int best = -1;
        double lowest = 1e10;
        for (int i=0;i<area;++i) {
            if (std::abs(us[i]-u)+std::abs(vs[i]-v)>reach) continue;
            if (elevation[i]<lowest) { lowest=elevation[i]; best=i; }
        }
        return best;
    };
    if (site == SettlementSite::Coast) {
        std::vector<int> edge;
        for (int i=0;i<area;++i) if (us[i]>.99) edge.push_back(i);
        inundate(edge,TOWN_SEA);
    } else if (site == SettlementSite::Lake || site == SettlementSite::Oasis) {
        const int spring = lowestNear(shape.basin.u,shape.basin.v,.10);
        if (spring>=0) inundate({spring},LAKE);
    }
    if (extraPond && (site == SettlementSite::Plains || site == SettlementSite::Forest)) {
        const int pool = lowestNear(shape.pond.u,shape.pond.v,.045);
        if (pool>=0) inundate({pool},LAKE);
    }
    for (int i=0;i<area;++i) if (stream[i] && !water[i]) water[i]=RIVER;

    // Moisture follows connected water and permeability. It drives vegetation
    // retention and clearing cost rather than a separately drawn green oval.
    std::vector<int> waterDistance(area,area);
    std::queue<int> pending;
    for (int i=0;i<area;++i) if (water[i]) { waterDistance[i]=0; pending.push(i); }
    while (!pending.empty()) {
        const int i=pending.front(); pending.pop();
        adjacent(i,[&](int n) {
            if (waterDistance[n]<=waterDistance[i]+1) return;
            waterDistance[n]=waterDistance[i]+1; pending.push(n);
        });
    }
    const double wetReach = std::max(4.0,std::min(width,height)*.055);
    int incised=0, deposited=0, flooded=0, wooded=0;
    int oasisGroveCandidate=-1;
    double oasisGroveBest=-1.0;
    std::vector<std::uint8_t> passable(area,0);
    std::vector<double> cost(area,0);
    for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
        const int i=y*width+x;
        moisture[i]=std::max(0.0,1-waterDistance[i]/wetReach)*(1.1-.3*hardness[i]);
        double slope=0;
        adjacent(i,[&](int n){ slope=std::max(slope,std::abs(elevation[n]-elevation[i])); });
        slope *= std::min(width,height)/160.0;
        if (water[i]) natural[i]=water[i];
        else if (site==SettlementSite::MountainValley && elevation[i]>.69) natural[i]=TOWN_CLIFF;
        else if (site==SettlementSite::Oasis) {
            // An oasis is not a uniformly green shoreline. Groundwater,
            // alluvial soil and seeded grove patches determine where palms and
            // reeds survive; direct banks remain open for drawing water and
            // pack animals, while other banks stay dry desert.
            const double groveNoise = .52 * Noise(us[i], vs[i], 3, seed + 127) +
                .33 * Noise(us[i], vs[i], 7, seed + 131) +
                .15 * Noise(us[i], vs[i], 12, seed + 137);
            const double oasisWetness = std::max(0.0, 1.0 -
                static_cast<double>(waterDistance[i]) / (wetReach * 1.15));
            const double alluvialSoil = std::clamp(deposit[i] * 9.0, 0.0, 1.0);
            const double groveScore = .58 * groveNoise + .25 * oasisWetness + .17 * alluvialSoil;
            const bool openBank = waterDistance[i] <= 1;
            if (!openBank && waterDistance[i] <= static_cast<int>(wetReach * 1.15) &&
                groveScore > oasisGroveBest) {
                oasisGroveBest = groveScore;
                oasisGroveCandidate = i;
            }
            if (!openBank && waterDistance[i] <= static_cast<int>(wetReach * 1.15) &&
                groveScore > .58 && hardness[i] > .30) natural[i]=TOWN_TREES;
        } else if (site==SettlementSite::Forest && slope>.105 && hardness[i]>.53) natural[i]=TOWN_TREES;
        const bool frame = x<4 || y<4 || x>=width-4 || y>=height-4;
        passable[i]=!frame && natural[i]!=TOWN_CLIFF && natural[i]!=TOWN_TREES && natural[i]!=LAKE && natural[i]!=TOWN_SEA;
        cost[i]=1.0+hardness[i]*1.4+slope*18.0+moisture[i]*.8;
        if (natural[i]==RIVER) cost[i]+=7.0;
        if (incision[i]>.015) ++incised;
        if (deposit[i]>.001) ++deposited;
        if (water[i]) ++flooded;
    }
    if (site == SettlementSite::Oasis && oasisGroveCandidate >= 0) {
        // Even a sparse oasis has a surviving grove. It is selected by the
        // strongest groundwater/alluvial patch, not by drawing a shoreline
        // outline, so dry open banks remain possible. Fill only a small local
        // cluster when the seeded vegetation field would otherwise leave a
        // single palm, never an all-around water border.
        int groveCells = static_cast<int>(std::count(natural.begin(), natural.end(), TOWN_TREES));
        const int candidateX = oasisGroveCandidate % width, candidateY = oasisGroveCandidate / width;
        for (int radius = 0; radius <= 3 && groveCells < 8; ++radius) {
            for (int dy = -radius; dy <= radius && groveCells < 8; ++dy) {
                for (int dx = -radius; dx <= radius && groveCells < 8; ++dx) {
                    if (std::abs(dx) + std::abs(dy) != radius) continue;
                    const int x = candidateX + dx, y = candidateY + dy;
                    if (x < 0 || y < 0 || x >= width || y >= height) continue;
                    const int i = y*width+x;
                    if (water[i] || waterDistance[i] <= 1 || natural[i] == TOWN_TREES || hardness[i] <= .25) continue;
                    natural[i] = TOWN_TREES;
                    passable[i] = 0;
                    ++groveCells;
                }
            }
        }
    }

    // Historical clearing expands from a dry nucleus. Low terraces and fertile
    // corridors are cheap to settle; steep/wet/rocky sites resist expansion.
    const auto [coreU,coreV]=Rotate(.47,.49,rotation);
    int core=-1, nearest=area;
    for (int i=0;i<area;++i) {
        if (!passable[i] || natural[i]==RIVER) continue;
        const int d=std::abs(i%width-static_cast<int>(coreU*width))+std::abs(i/width-static_cast<int>(coreV*height));
        if (d<nearest) { nearest=d; core=i; }
    }
    std::vector<double> distance(area,std::numeric_limits<double>::infinity());
    std::vector<int> previous(area,-1), order;
    using Entry=std::pair<double,int>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> frontier;
    if (core>=0) { distance[core]=0; frontier.push({0,core}); }
    while (!frontier.empty()) {
        const auto [d,i]=frontier.top(); frontier.pop();
        if (d!=distance[i]) continue;
        order.push_back(i);
        // Diagonal steps follow dry terrain, never cut across a river corner.
        for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
            if ((!dx&&!dy) || i%width+dx<0 || i%width+dx>=width || i/width+dy<0 || i/width+dy>=height) continue;
            const int n=i+dy*width+dx;
            if (!passable[n]) continue;
            if (dx&&dy && (!passable[i+dx] || !passable[i+dy*width])) continue;
            const double alongValley=std::abs(vs[n]-vs[i])>std::abs(us[n]-us[i]) ? .82 : 1.0;
            const double next=d+(dx&&dy?1.41421356:1.0)*(cost[i]+cost[n])*.5*alongValley;
            if (next>=distance[n]) continue;
            distance[n]=next; previous[n]=i; frontier.push({next,n});
        }
    }
    footprint.assign(area,0);
    const double development=village?.36:.74;
    const int target=std::min(static_cast<int>(area*development),static_cast<int>(order.size()*(village?.78:.94)));
    for (int n=0;n<target;++n) footprint[order[n]]=1;
    if (site==SettlementSite::Forest || site==SettlementSite::MountainValley) {
        // Old land approaches remain through the woods/passes; they are actual
        // traversable terrain, not black cross-shaped gaps through a forest.
        for (int side=0;side<2;++side) {
            int end=-1;
            double best=1e10;
            const int valleyLength = rotation % 2 ? width : height;
            const double edge = 4.5 / (valleyLength - 1);
            for (int i:order) {
                if (side==0 ? vs[i]>edge : vs[i]<1.0-edge) continue;
                const double score=distance[i]+std::abs(us[i]-.50)*width;
                if (score<best) { best=score; end=i; }
            }
            for (int i=end;i>=0;i=previous[i]) {
                for (int dy=-3;dy<=3;++dy) for (int dx=-3;dx<=3;++dx) {
                    const int x=i%width+dx,y=i/width+dy;
                    if (x<4||y<4||x>=width-4||y>=height-4) continue;
                    const int n=y*width+x;
                    if (passable[n]) footprint[n]=1;
                }
            }
        }
    }
    // The development mask is not a crop mask. Forest continues to the image
    // edge too, except for the existing woodland approaches through the
    // four-cell building setback. Keep those approaches open to the outside.
    auto woodlandClearing = footprint;
    if (site == SettlementSite::Forest) {
        for (int x = 4; x < width - 4; ++x) for (int offset = 0; offset < 4; ++offset) {
            if (footprint[4 * width + x]) woodlandClearing[offset * width + x] = 1;
            if (footprint[(height - 5) * width + x]) woodlandClearing[(height - 1 - offset) * width + x] = 1;
        }
        for (int y = 4; y < height - 4; ++y) for (int offset = 0; offset < 4; ++offset) {
            if (footprint[y * width + 4]) woodlandClearing[y * width + offset] = 1;
            if (footprint[y * width + width - 5]) woodlandClearing[y * width + width - 1 - offset] = 1;
        }
    }
    structure.assign(area,0);
    for (int i=0;i<area;++i) {
        auto tile=natural[i];
        if (site==SettlementSite::Forest && !woodlandClearing[i] && !water[i]) tile=TOWN_TREES;
        if (tile==TOWN_TREES || tile==TOWN_CLIFF) { structure[i]=1; footprint[i]=0; }
        if (tile==TOWN_TREES) ++wooded;
        plan.biomes[i] = tile;
    }
    plan.settlementMask = footprint;
    plan.terrainHistory = {{"substrate_and_relief",area},{"catchment_incision",incised},
        {"alluvial_deposition",deposited},{"connected_inundation",flooded},
        {"moisture_and_woodland",wooded},{"terrain_guided_clearing",std::accumulate(footprint.begin(),footprint.end(),0)}};

    for (const auto& [tile, role] : {
        std::pair<int,const char*>{TOWN_SEA,"drowned_coast"}, {TOWN_CLIFF,"eroded_valley_flanks"},
        {TOWN_TREES,"retained_woodland"}, {RIVER,"catchment_channel"}, {LAKE,"flooded_depression"}}) {
        int x0=width,y0=height,x1=-1,y1=-1;
        for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
            if (plan.biomes[y*width+x]!=tile) continue;
            x0=std::min(x0,x); y0=std::min(y0,y); x1=std::max(x1,x); y1=std::max(y1,y);
        }
        if (x1<0) continue;
        plan.regions.push_back({tile==TOWN_CLIFF||tile==TOWN_TREES?"landscape":"waterway",-100-tile,x0,y0,x1,y1});
        plan.regions.back().role=role;
    }
}
