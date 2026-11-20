#pragma once

#include "CastleInterior.h"
#include "MapPlan.h"

#include <algorithm>
#include <functional>
#include <random>
#include <string>
#include <vector>

// 屋外設備（yard_*）の、人工的な配置。区画の形（矩形、角を削った矩形、中庭の任意形状）に応じて、
// 通路・縁取り・列・対称で並べる。入力は区画の内側の判定と、置いてはいけないマス（扉の前など）だけで、
// 出力は置くものの一覧（Plan の区画にするのは呼び出し側）。乱数は離散の選択にだけ使い、位置は揺らさない。
// 使うアセットは、エディターが城マップでも読む区画の種類だけ:
//   castle_fixture（castleObject<type>_<variant>）、settlement_prop（木・低木・岩）、settlement_flower、
//   settlement_plant、settlement_crop。
namespace castle_lot {

struct Piece {
    std::string kind, role;
    int x = 0, y = 0, w = 1, d = 1;   // 世界座標の左上と、回転後の幅・奥行き
    int rotation = 0;
    int type = -1, variant = -1, height = 0;
    int biome = -1;                    // kind が "paint" のとき、塗る地面（バイオーム）
    int parent = -1;                   // kind が "castle_detail" のとき、土台にする部品の番号（pieces の添字）
};

struct LayoutInput {
    std::string role;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;    // 区画の外接矩形（両端を含む）
    int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;  // 整列の基準にする内側の矩形（コア。両端を含む）
    std::function<bool(int, int)> inside;     // 区画の内側か
    std::function<bool(int, int)> blocked;    // 置いてはいけないマス（扉の前など）
    bool desert = false;
    double wealth = 1.0;                      // 裕福度（0.7〜2.5）
    int mode = 0;                             // 城のタイプ（0 同心円・1 丘・2 川・3 宮殿・4 要塞）
    std::mt19937* rng = nullptr;
    const MapGenParams* params = nullptr;
};

namespace layout_detail {

struct Dim { int w, d; };

inline Dim PropDim(const MapGenParams& params, const std::string& role) {
    Dim dim{2, 2};
    if (role == "shade_tree" || role == "conifer" || role == "palm") dim = {3, 3};
    const auto find = [&](const std::string& suffix, int& value) {
        const auto it = params.extra.find("settlementProp_" + role + suffix);
        if (it != params.extra.end() && it->second >= 1) value = static_cast<int>(it->second);
    };
    find("_width", dim.w);
    find("_depth", dim.d);
    return dim;
}

class Builder {
public:
    // orient: 0 は長辺を主軸にする、1 は横（x）、2 は縦（y）を主軸にする。
    explicit Builder(const LayoutInput& in, int orient = 0) : in_(in) {
        bw_ = in.x1 - in.x0 + 1; bh_ = in.y1 - in.y0 + 1;
        solid_.assign(static_cast<std::size_t>(bw_ * bh_), 0);
        path_ = solid_;
        light_ = solid_;
        cw_ = in.cx1 - in.cx0 + 1; ch_ = in.cy1 - in.cy0 + 1;
        horizontal_ = orient == 1 ? true : orient == 2 ? false : cw_ >= ch_;
        A = horizontal_ ? cw_ : ch_;   // 主軸（長辺）の長さ
        B = horizontal_ ? ch_ : cw_;   // 短辺
        mu = A / 2; mv = B / 2;
    }

    int A = 0, B = 0, mu = 0, mv = 0;
    std::vector<Piece> pieces;

    int Pick(int n) { return n <= 1 ? 0 : static_cast<int>((*in_.rng)() % static_cast<unsigned>(n)); }

    // 主軸（u）と短軸（v）の座標から世界のマスへ。
    int X(int u, int v) const { return in_.cx0 + (horizontal_ ? u : v); }
    int Y(int u, int v) const { return in_.cy0 + (horizontal_ ? v : u); }
    bool Horizontal() const { return horizontal_; }

    bool Ok(int x, int y, bool needFree) const {
        if (x < in_.x0 || y < in_.y0 || x > in_.x1 || y > in_.y1) return false;
        if (!in_.inside(x, y) || in_.blocked(x, y)) return false;
        const std::size_t i = Idx(x, y);
        if (path_[i] || solid_[i]) return false;
        return !needFree || !light_[i];
    }
    void ReservePath(int u0, int v0, int u1, int v1) {
        for (int v = v0; v <= v1; ++v) for (int u = u0; u <= u1; ++u) {
            const int x = X(u, v), y = Y(u, v);
            if (x >= in_.x0 && y >= in_.y0 && x <= in_.x1 && y <= in_.y1) path_[Idx(x, y)] = 1;
        }
    }
    bool Free(int u, int v) const { return Ok(X(u, v), Y(u, v), true); }

    // 固い物（家具・木・岩）。主軸方向の幅 wu と短軸方向の奥行き dv は、主軸が縦のときは入れ替えて回転 1 にする。
    // gap: ほかの固い物との間に空けるマス。
    bool Solid(const std::string& kind, const std::string& role, int u, int v, int wu, int dv, int type, int variant, int height, int gap = 1, bool turned = false, int face = 0) {
        if (turned) std::swap(wu, dv);
        const int x = X(u, v), y = Y(u, v);
        const int ww = horizontal_ ? wu : dv, dd = horizontal_ ? dv : wu;
        // 主軸が縦のとき、(u, v) は左上ではなく (v, u) の対応なので、矩形の左上は X(u,v), Y(u,v) のまま（u,v が最小角）。
        for (int yy = y - gap; yy < y + dd + gap; ++yy) for (int xx = x - gap; xx < x + ww + gap; ++xx) {
            const bool footprint = xx >= x && xx < x + ww && yy >= y && yy < y + dd;
            if (xx < in_.x0 || yy < in_.y0 || xx > in_.x1 || yy > in_.y1) { if (footprint) return false; continue; }
            if (footprint) { if (!Ok(xx, yy, true)) return false; }
            else if (solid_[Idx(xx, yy)]) return false;
        }
        for (int yy = y; yy < y + dd; ++yy) for (int xx = x; xx < x + ww; ++xx) solid_[Idx(xx, yy)] = 1;
        Piece p;
        p.kind = kind; p.role = role; p.x = x; p.y = y; p.w = ww; p.d = dd;
        // face: 向きを回す数（2 で反対向き）。主軸が縦のときは、その分だけさらに 1 つ回っている。
        // 主軸が縦で回していない物は、局所の +v（東）を向く回転 3。横の庭で回した物は 1。
        p.rotation = kind == "castle_fixture" ? (((!horizontal_ && !turned) ? 3 : (horizontal_ && turned) ? 1 : 0) + face) % 4 : 0;
        p.type = type; p.variant = variant; p.height = height;
        pieces.push_back(p);
        return true;
    }
    // 街の設備（井戸・街灯・像）。エディターが登録したアセットの寸法（civicObject<type>_<variant>Width/Depth/Height）で置く。
    bool Civic(const std::string& role, int type, int variant, int defW, int defD, int defH, int u, int v, int gap = 1, int turns = 0) {
        const std::string key = "civicObject" + std::to_string(type) + "_" + std::to_string(variant);
        const auto get = [&](const std::string& k, int value) { const auto it = in_.params->extra.find(k); return it == in_.params->extra.end() ? value : static_cast<int>(it->second); };
        const int w = get(key + "Width", defW), d = get(key + "Depth", defD), h = get(key + "Height", defH);
        // (a quarter turn swaps the footprint; a half turn or none keeps it)
        const bool swap = (turns % 2 != 0) != !horizontal_;
        if (!Solid("settlement_facility", role, u, v, swap ? d : w, swap ? w : d, type, variant, h, gap, false, 0)) return false;
        pieces.back().rotation = turns % 4;
        return true;
    }
    int CivicW(int type, int variant, int defW) const {
        const auto it = in_.params->extra.find("civicObject" + std::to_string(type) + "_" + std::to_string(variant) + "Width");
        return it == in_.params->extra.end() ? defW : static_cast<int>(it->second);
    }
    int CivicD(int type, int variant, int defD) const {
        const auto it = in_.params->extra.find("civicObject" + std::to_string(type) + "_" + std::to_string(variant) + "Depth");
        return it == in_.params->extra.end() ? defD : static_cast<int>(it->second);
    }
    int Gap() const { return 1; }
    const MapGenParams& Params() const { return *in_.params; }
    bool Prop(const std::string& role, int u, int v, int gap = 1) {
        const Dim dim = PropDim(*in_.params, role);
        // (A tree is placed unturned by the editor: its footprint is width x depth in the world, whichever way the yard runs.)
        return horizontal_ ? Solid("settlement_prop", role, u, v, dim.w, dim.d, -1, -1, 0, gap) : Solid("settlement_prop", role, u, v, dim.d, dim.w, -1, -1, 0, gap);
    }
    bool Fixture(const std::string& role, int type, int variant, int w, int d, int h, int u, int v, int gap = 1, bool turned = false, int face = 0) {
        const castle_interior::Object m = castle_interior::Native(*in_.params, type, variant, w, d, h);
        return Solid("castle_fixture", role, u, v, m.w, m.d, m.type, m.variant, m.h, gap, turned, face);
    }
    // 土台の上に重ねる物（樽の二段目以降）。場所は取らず、土台の高さの分だけ持ち上がる。戻り値は部品の番号。
    int Detail(const std::string& role, int type, int variant, int w, int d, int h, int u, int v, int parent) {
        Piece p;
        p.kind = "castle_detail"; p.role = role; p.x = X(u, v); p.y = Y(u, v); p.w = w; p.d = d;
        p.type = type; p.variant = variant; p.height = h; p.parent = parent;
        pieces.push_back(p);
        return static_cast<int>(pieces.size()) - 1;
    }
    // 地面の塗り（射撃位置の線など）。区画の内側のマスだけ塗る。
    void Paint(int u0, int v0, int u1, int v1, int biome) {
        const int xa = std::min(X(u0, v0), X(u1, v1)), xb = std::max(X(u0, v0), X(u1, v1));
        const int ya = std::min(Y(u0, v0), Y(u1, v1)), yb = std::max(Y(u0, v0), Y(u1, v1));
        Piece p;
        p.kind = "paint"; p.x = xa; p.y = ya; p.w = xb - xa + 1; p.d = yb - ya + 1; p.biome = biome;
        pieces.push_back(p);
    }
    // 軽い物（花・下草・作物）。固い物とも通路とも重ならない。
    bool Light(const std::string& kind, const std::string& role, int u0, int v0, int u1, int v1) {
        const int xa = std::min(X(u0, v0), X(u1, v1)), xb = std::max(X(u0, v0), X(u1, v1));
        const int ya = std::min(Y(u0, v0), Y(u1, v1)), yb = std::max(Y(u0, v0), Y(u1, v1));
        for (int y = ya; y <= yb; ++y) for (int x = xa; x <= xb; ++x) if (!Ok(x, y, true)) return false;
        for (int y = ya; y <= yb; ++y) for (int x = xa; x <= xb; ++x) light_[Idx(x, y)] = 1;
        Piece p;
        p.kind = kind; p.role = role; p.x = xa; p.y = ya; p.w = xb - xa + 1; p.d = yb - ya + 1;
        pieces.push_back(p);
        return true;
    }

private:
    std::size_t Idx(int x, int y) const { return static_cast<std::size_t>((y - in_.y0) * bw_ + (x - in_.x0)); }
    const LayoutInput& in_;
    int bw_ = 0, bh_ = 0, cw_ = 0, ch_ = 0;
    bool horizontal_ = true;
    std::vector<std::uint8_t> solid_, path_, light_;
};

}  // namespace layout_detail

// 屋外設備を、役割ごとの型に沿って並べる。コアが小さすぎて何も入らなければ、空のまま返す。
inline std::vector<Piece> LayoutYardPiecesFull(const LayoutInput& in) {
    using namespace layout_detail;
    Builder b(in, in.role == "yard_archery_range" ? 2 : (in.role == "yard_drill_ground" || in.role == "yard_herb_garden") ? 1 : 0);
    const MapGenParams& P = b.Params();
    const std::string& role = in.role;
    // 裕福度を 0〜1 にしたもの。貧しいほど小さい。
    const double tw = std::clamp((in.wealth - 0.7) / 1.8, 0.0, 1.0);
    const int keepPercent = static_cast<int>(40 + 60 * tw);  // 花壇・畑を残す割合（貧しいと間引く）
    const int A = b.A, B = b.B, mu = b.mu, mv = b.mv;
    const std::string tree = in.desert ? "palm" : "shade_tree", conifer = in.desert ? "palm" : "conifer", shrub = in.desert ? "cactus" : "shrub";
    const int T = std::max(PropDim(P, tree).w, PropDim(P, tree).d), S = std::max(PropDim(P, shrub).w, PropDim(P, shrub).d), C = std::max(PropDim(P, conifer).w, PropDim(P, conifer).d);
    const auto bucket = [&](int u, int v) { return b.Fixture("water_bucket", 4, 41, 2, 2, 2, u, v); };
    const auto barrel = [&](int u, int v, int gap = 1) { return b.Fixture("barrel", 4, 21, 3, 3, 3, u, v, gap); };
    // face 2: 反対向き（既定は +y 向き）。ベンチは、座る人が通路や広場のほうを向くように回す。
    const auto bench = [&](int u, int v, int face = 0) { return b.Fixture("bench", 4, 5, 4, 2, 3, u, v, 1, false, face); };
    // 縁取り: コアの外周に、低木を等間隔（pitch）で並べる。
    // turns: 向きを回す数（0 は +y＝南向き）。
    const auto lamp = [&](int u, int v, int turns = 0) { return b.Civic("street_lamp", 2, 1, 3, 3, 6, u, v, 1, turns); };
    const auto statue = [&](int u, int v, int gap = 1) { return b.Civic("statue", 3, 14, 4, 4, 6, u, v, gap); };
    const auto hedge = [&](int pitch, int gap) {
        for (int u = 0; u + S <= A; u += pitch) { b.Prop(shrub, u, 0, gap); b.Prop(shrub, u, B - S, gap); }
        for (int v = pitch; v + S <= B - S; v += pitch) { b.Prop(shrub, 0, v, gap); b.Prop(shrub, A - S, v, gap); }
    };

    if (role == "yard_garden_grass") {
        if (A < 14 || B < 14) {
            // 小さい庭: 中央の木を先に置き、縁取りと街灯をその外に。
            b.Prop(tree, (A - T) / 2, (B - T) / 2);
            hedge(S + 2, 1);
            return b.pieces;
        }
        b.ReservePath(mu - 1, 0, mu, B - 1);
        b.ReservePath(0, mv - 1, A - 1, mv);
        bench(mu - 5, mv + 1, 2);
        bench(mu + 2, mv + 1, 2);
        // 花壇があるので、木は植えない（木が花壇をつぶすため）。
        // 街灯: 通路ぞいに二つ。通路をはさんで向き合うように、互いの側を向ける（主軸が縦なら u は南北、横なら東西）。
        // 向き: 主軸の + 方向は、縦なら南（0）、横なら東（3）。- 方向は縦なら北（2）、横なら西（1）。
        int lamps = 0;
        for (const auto& at : {std::pair<int, int>{mu + 1, mv - 4}, {mu - 4, mv - 4}, {mu + 1, mv + 3}, {mu - 4, mv + 3}}) {
            const bool beyond = at.first > mu;  // 通路の + 側にある街灯は、- 側（通路のほう）を向く
            const int turns = beyond ? (b.Horizontal() ? 1 : 2) : (b.Horizontal() ? 3 : 0);
            if (lamps < 2 && lamp(at.first, at.second, turns)) ++lamps;
        }
        hedge(4, 1);
        if (!in.desert) {
            // 花壇: 四つの象限に、縁を白、中を縞にして、対角を同じ色にする。
            for (int q = 0; q < 4; ++q) {
                const bool right = (q & 1) != 0, low = (q & 2) != 0;
                const std::string color = (right == low) ? "flower_red" : "flower_yellow";
                for (int v = 3; v <= mv - 3; ++v) for (int u = 3; u <= mu - 3; ++u) {
                    const int uu = right ? A - 1 - u : u, vv = low ? B - 1 - v : v;
                    const bool ring = u == 3 || v == 3 || u == mu - 3 || v == mv - 3;
                    if (b.Pick(100) >= keepPercent) continue;  // 貧しい庭は、花を間引く
                    if (ring) b.Light("settlement_flower", "flower_white", uu, vv, uu, vv);
                    else if (v % 2 == 0) b.Light("settlement_flower", color, uu, vv, uu, vv);
                }
            }
        }
        return b.pieces;
    }
    if (role == "yard_garden_forest") {
        // 砂漠の森の庭は、ヤシを格子に並べず、サボテンと低木を混ぜて疎らに植える（ヤシは背が高く、砂漠の庭には重い）。
        const int D = std::max(std::max(PropDim(P, "cactus").w, PropDim(P, "cactus").d), std::max(PropDim(P, "shrub").w, PropDim(P, "shrub").d));
        const auto desertPlant = [&]() -> std::string { return b.Pick(100) < 55 ? "cactus" : "shrub"; };
        if (A < 12 || B < 12) { b.Prop(in.desert ? desertPlant() : conifer, (A - (in.desert ? D : C)) / 2, (B - (in.desert ? D : C)) / 2); return b.pieces; }
        // 中央に空き地（ベンチ二つ）を取り、そのまわりに針葉樹を千鳥に並べる。木の大きさ（C×Cd）は登録アセットに合わせる。
        const int Cd = C;
        // ベンチは並べて置き、向かい合わせにしない（それぞれ外側を向く）。
        bench(mu - 2, mv - 3, 2);
        bench(mu - 2, mv + 1);
        b.ReservePath(mu - 4, mv - 4, mu + 3, mv + 3);
        lamp(mu + 4, mv - 3); lamp(mu - 7, mv + 1);
        if (in.desert) {
            for (int r = 0, v = 1; v + D <= B; v += D + 3, ++r)
                for (int u = 1 + (r % 2) * ((D + 3) / 2); u + D <= A; u += D + 3) {
                    if (b.Pick(100) >= 80) continue;  // 疎らに間引く
                    b.Prop(desertPlant(), u, v);
                }
            return b.pieces;
        }
        for (int r = 0, v = 1; v + Cd <= B; v += Cd + 2, ++r)
            for (int u = 1 + (r % 2) * ((C + 2) / 2); u + C <= A; u += C + 2) {
                if (b.Prop(conifer, u, v)) b.Light("settlement_plant", (u + v) % 3 == 0 ? "mushroom" : "fern", u + 1, v + Cd, u + 1, v + Cd);
            }
        return b.pieces;
    }
    if (role == "yard_well") {
        // 井戸（登録アセット）を中央に。まわりに桶と樽。
        const int WW = b.CivicW(3, 26, 4), WD = b.CivicD(3, 26, 4);
        const int cu = mu - WW / 2, cv = mv - WD / 2;
        if (A < WW + 4 || B < WD + 4) { b.Civic("well", 3, 26, 4, 4, 5, std::max(0, (A - WW) / 2), std::max(0, (B - WD) / 2), 1, 1); return b.pieces; }
        b.Civic("well", 3, 26, 4, 4, 5, cu, cv, 1, 1);
        bucket(cu - 3, cv); bucket(cu + WW + 1, cv + WD - 2); bucket(cu + 1, cv - 3); bucket(cu + WW - 3, cv + WD + 1);
        barrel(cu + WW + 4, cv); barrel(cu - 7, cv);
        if (A >= WW + 14 && B >= WD + 14) {
            // 広い井戸場の外側: 貧しければ石が少し散らばり、豊かならベンチ、さらに花壇と街灯を置く。
            if (tw < 0.5) {
                const int want = static_cast<int>((1.0 - 2.0 * tw) * A * B / 70.0);
                for (int tries = 0, got = 0; tries < want * 6 && got < want; ++tries)
                    if (b.Prop("rock", 1 + b.Pick(std::max(1, A - 4)), 1 + b.Pick(std::max(1, B - 4)), 2)) ++got;
            } else {
                bench(cu + WW / 2 - 2, cv - 6, 0);
                bench(cu + WW / 2 - 2, cv + WD + 4, 2);
                if (tw >= 0.75) {
                    lamp(cu - 6, cv - 6); lamp(cu + WW + 3, cv + WD + 3);
                    for (const int cuu : {2, A - 6}) for (const int cvv : {2, B - 6})
                        for (int j = 0; j < 4; ++j) for (int i = 0; i < 4; ++i) {
                            const bool ring = i == 0 || j == 0 || i == 3 || j == 3;
                            b.Light("settlement_flower", ring ? "flower_white" : (cuu < mu ? "flower_red" : "flower_yellow"), cuu + i, cvv + j, cuu + i, cvv + j);
                        }
                }
            }
        }
        return b.pieces;
    }
    if (role == "yard_graveyard") {
        b.ReservePath(0, mv - 1, A - 1, mv);
        const int u0 = 2 + ((A - 4) % 4) / 2;
        // 墓は 3 段階: 岩（ふつうの墓）・ヤシ・像。上の段階ほど、豊かな城で出やすい。
        const int statuePercent = static_cast<int>(3 + 27 * tw), palmPercent = static_cast<int>(8 + 22 * tw);
        // 戦いと縁遠い城ほど墓は少ない（要塞・丘は多く、宮殿は少ない）。
        static constexpr int kGraveKeep[5] = {75, 90, 55, 40, 100};
        const int graveKeep = kGraveKeep[std::clamp(in.mode, 0, 4)];
        const auto grave = [&](int u, int v, int flowerV) {
            if (b.Pick(100) >= graveKeep) return;
            const int roll = b.Pick(100);
            if (roll < statuePercent && statue(u - 1, v - 1, 0)) return;
            if (roll < statuePercent + palmPercent && b.Prop("palm", u - 2, v - 1, 0)) return;
            if (b.Prop("rock", u, v) && !in.desert) b.Light("settlement_flower", "flower_white", u, flowerV, u, flowerV);
        };
        for (int j = 0;; ++j) {
            const int vN = mv - 4 - 6 * j, vS = mv + 2 + 6 * j;
            if (vN < 1 && vS + 2 > B - 1) break;
            for (int u = u0; u + 2 <= A - 2; u += 4) {
                if (vN >= 1) grave(u, vN, vN + 2);
                if (vS + 2 <= B - 1) grave(u, vS, vS - 1);
            }
        }
        for (const int u : {1, A - 3}) { b.Fixture("candle", 2, 4, 2, 1, 2, u, mv - 3); b.Fixture("candle", 2, 4, 2, 1, 2, u, mv + 2); }
        return b.pieces;
    }
    if (role == "yard_herb_garden") {
        if (B < 8 || A < 8) return b.pieces;
        barrel(1, B - 3); barrel(5, B - 3); bucket(9, B - 3);
        bench(A - 5, B - 2);
        // 畝は縦に 8 マスずつ並べ、その間に幅 2 の通路を取る。
        const int bedEnd = B - 5;
        for (int v0 = 1; v0 <= bedEnd; v0 += 10) if (v0 + 7 < bedEnd) b.ReservePath(0, v0 + 8, A - 1, v0 + 9);
        int bed = 0;
        for (int u = 1; u + 1 <= A - 2; u += 4, ++bed) {
            if (u <= mu && mu <= u + 1) continue;  // 中央の通路
            for (int v0 = 1; v0 <= bedEnd; v0 += 10) {
                if (b.Pick(100) >= keepPercent) continue;  // 貧しい畑は、畝を間引く
                b.Light("settlement_crop", (bed + v0 / 10) % 2 == 0 ? "herb_crop" : "leafy_crop", u, v0, u + 1, std::min(v0 + 7, bedEnd));
            }
        }
        return b.pieces;
    }
    if (role == "yard_cargo") {
        // 荷置き場: 荷車の通る道（幅 4）と樽の山の帯を、奥行き全体に交互に並べる。山は 1〜3 列 × 1〜2 段で、ところどころ抜け、間隔も不ぞろい。
        // 横に並んだ 2 樽や 3 樽の上には、樽を重ねることがある（裕福な城ほど多く、3 段は低確率）。山の端には、水桶をときどき置く。
        const castle_interior::Object bm = castle_interior::Native(P, 4, 21, 3, 3, 3);
        const int BW = bm.w, BD = bm.d, BH = bm.h;
        const int stackPercent = static_cast<int>(5 + 55 * tw), topPercent = static_cast<int>(2 + 13 * tw);
        int v = 1;
        bool lane = b.Pick(2) == 0;  // 先頭を道にするかどうか
        while (v + BD <= B - 1) {
            if (lane) { b.ReservePath(0, v, A - 1, v + 3); v += 4; lane = false; continue; }
            const int rows = 1 + b.Pick(2);
            const int bandDepth = std::min(rows * BD, B - 1 - v);
            for (int u = 1 + b.Pick(3); u + BW <= A - 1;) {
                const int cols = 1 + b.Pick(3), r = std::min(rows, bandDepth / BD);
                for (int row = 0; row < std::max(1, r); ++row) {
                    std::vector<int> made;
                    for (int c = 0; c < cols; ++c) {
                        if ((row > 0 || c > 0) && b.Pick(6) == 0) { made.push_back(-1); continue; }
                        made.push_back(barrel(u + BW * c, v + BD * row, 0) ? static_cast<int>(b.pieces.size()) - 1 : -1);
                    }
                    bool whole = !made.empty();
                    for (const int m : made) whole = whole && m >= 0;
                    if (!whole || cols < 2 || b.Pick(100) >= stackPercent) continue;
                    // 二段目: 並んだ 2 樽の継ぎ目の上にまたがらせる（樽 2 つ → 1 つ、3 つ → 2 つ）。
                    const int o1 = BW - BW / 2;
                    const bool third = cols == 3 && b.Pick(100) < topPercent;
                    const int mid1 = b.Detail("barrel", 4, 21, BW, BD, third ? 2 * BH : BH, u + o1, v + BD * row, made[0]);
                    int mid2 = -1;
                    if (cols == 3) mid2 = b.Detail("barrel", 4, 21, BW, BD, third ? 2 * BH : BH, u + o1 + BW, v + BD * row, made[1]);
                    // 三段目: 二段目の 2 樽の継ぎ目の上（二段目の上面は 2 樽ぶんの高さ）。
                    if (third && mid2 >= 0) b.Detail("barrel", 4, 21, BW, BD, BH, u + o1 + BW - BW / 2, v + BD * row, mid1);
                }
                if (b.Pick(3) == 0) bucket(u + BW * cols, v + 1);
                u += BW * cols + 2 + b.Pick(4);
            }
            v += bandDepth;
            lane = true;
        }
        return b.pieces;
    }
    if (role == "yard_drill_ground") {
        if (A < 24 || B < 12) return b.pieces;
        for (int off = -(A / 12); off <= A / 12; ++off) {
            const int u = mu - 2 + 6 * off;
            if (u < 1 || u + 4 > A - 1) continue;
            if (off % 2 == 0) b.Fixture("sword", 4, 30, 4, 1, 3, u, 0); else b.Fixture("shield", 4, 28, 3, 1, 3, u, 0);
        }
        for (int off = -(A / 16); off <= A / 16; ++off) bench(mu - 2 + 8 * off, B - 2, 2);
        barrel(0, 2); barrel(A - 3, 2); barrel(0, B - 6); barrel(A - 3, B - 6);
        return b.pieces;
    }
    if (role == "yard_paddock") {
        if (A < 12 || B < 8) return b.pieces;
        // 門は南（画像の下）の辺の中央。主軸が横なら下の長辺、縦なら下の短辺。
        const bool h = b.Horizontal();
        if (h) b.ReservePath(mu - 2, B - S, mu + 1, B - 1); else b.ReservePath(A - S, mv - 2, A - 1, mv + 1);
        hedge(S, 0);
        if (h) {
            bucket(mu - 5, B - S - 3); bucket(mu + 4, B - S - 3);
            barrel(mu - 10, B - S - 4); barrel(mu + 8, B - S - 4);
        } else {
            bucket(A - S - 3, mv - 5); bucket(A - S - 3, mv + 4);
            barrel(A - S - 4, mv - 10); barrel(A - S - 4, mv + 8);
        }
        return b.pieces;
    }
    if (role == "yard_archery_range") {
        // 射線は南北（主軸が縦）。的（盾）は北の奥、その背後に低木。射撃位置は南に、線で印を付け、ベンチを置く。
        if (A < 20 || B < 8) return b.pieces;
        for (int off = -(B / 12); off <= B / 12; ++off) {
            const int v = mv - 1 + 6 * off;
            if (v < 1 || v + 3 > B - 1) continue;
            b.Fixture("shield", 4, 28, 3, 1, 3, 3, v, 1, true);
        }
        for (int v = 1 + ((B - 2 * S) % 4) / 2; v + S <= B - 1; v += 4) b.Prop(shrub, 0, v, 0);
        // 的から射撃位置までの間は、砂の道。射撃位置には石畳の線を引く（ベンチは置かない）。
        for (int off = -(B / 12); off <= B / 12; ++off) {
            const int v = mv - 1 + 6 * off;
            if (v < 1 || v + 3 > B - 1) continue;
            b.Paint(4, v, A - 8, v + 2, DESERT);
        }
        b.Paint(A - 7, 1, A - 7, B - 2, TOWN_PAVEMENT);  // 射撃位置の線
        return b.pieces;
    }
    return b.pieces;
}


// （向きは南向きに固定する。正面（作業線の前、畑の入口、門、射撃位置）は、画像の下＝南。）
// 配置の型が入らないほど小さい区画でも、中央に役割らしい物を1つ置く。
inline std::vector<Piece> LayoutYardPieces(const LayoutInput& in) {
    std::vector<Piece> pieces = LayoutYardPiecesFull(in);
    if (!pieces.empty()) return pieces;
    layout_detail::Builder b(in);
    const int cu = (b.A - 3) / 2, cv = (b.B - 3) / 2;
    if (b.A < 4 || b.B < 4) return pieces;
    const std::string& role = in.role;
    if (role == "yard_garden_grass" || role == "yard_garden_forest") b.Prop(in.desert ? (role == "yard_garden_forest" ? "cactus" : "palm") : role == "yard_garden_forest" ? "conifer" : "shade_tree", cu, cv);
    else if (role == "yard_well") b.Civic("well", 3, 26, 4, 4, 5, (b.A - 4) / 2, (b.B - 4) / 2, 1, 1);
    else if (role == "yard_graveyard") b.Prop("rock", (b.A - 2) / 2, (b.B - 2) / 2);
    else b.Fixture("barrel", 4, 21, 3, 3, 3, cu, cv);
    return b.pieces;
}

}  // namespace castle_lot
