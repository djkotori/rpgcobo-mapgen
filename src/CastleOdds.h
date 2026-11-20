#pragma once

#include <array>
#include <cstddef>

// 城の生成に関わる確率・重みの一覧。
// 生成器（CastleMapImageGenerator.cpp）の抽選に使う数値はここに集め、コード側に直接書かない。
// 単位は特記がなければパーセント（0〜100）。「重み」は合計 100 の配分。
// 部屋カタログごとの採用率は CastleProgram.h の GetCastleRoomCatalog() にある（別表）。
// 値を変えると同じ seed でも城が変わる。変更後は tests の CastleAxisVariety / CastleVariety を実行する。
namespace castle_odds {

// ---- 城の種類・環境 ----------------------------------------------------------------
// 城のタイプの重み（castleMode 指定がないとき。引数 castleModeWeight* で上書きできる）。比で、合計は 1.0。
inline constexpr double kModeWeightConcentric = 0.30;
inline constexpr double kModeWeightHill = 0.15;
inline constexpr double kModeWeightRiver = 0.15;
inline constexpr double kModeWeightPalace = 0.15;
inline constexpr double kModeWeightFortress = 0.25;
// 豊かさは一様分布。
inline constexpr double kWealthMin = 0.7;
inline constexpr double kWealthMax = 2.5;

// ---- 軸（玄関ホール・待機室・玉座の間・私室） ------------------------------------------
// それぞれ「ある」確率。どれも 100 ではない（必ず出る施設は作らない）。
inline constexpr int kThronePercent = 95;
// 正面の形（玄関ホール以外も含め、合計 100）。残りは、玄関ホールの代わりに大広間か衛兵の間が入口になる。
inline constexpr int kFrontHallPercent = 45;          // 玄関ホール（軸の幅いっぱいの大部屋）
inline constexpr int kFrontCorridorPercent = 20;      // 正門がそのまま廊下に開く（廊下＞待機室）
inline constexpr int kFrontVestibulePercent = 25;     // 小さな前室と、その両脇の小部屋
static_assert(kFrontHallPercent + kFrontCorridorPercent + kFrontVestibulePercent <= 100, "front form percentages");
// 前室: 軸の幅に対する割合と、幅・深さ・軸の幅の下限（足りないときは大広間にする）。
inline constexpr int kVestibuleWidthShare = 36, kVestibuleMinWidth = 14, kVestibuleMaxWidth = 20;
inline constexpr int kVestibuleMinDepth = 14, kVestibuleMinColumn = 34;
inline constexpr int kAntechamberPercent = 90;      // 玉座の間があるときだけ意味を持つ（Hill は常にあり）

// 私室群のパターン。添字は AxisPlan::suite。重みの合計は 100。
//   0 なし / 1 王の寝室のみ / 2 寝室＋化粧室＋書斎 / 3 王と王妃が別々 / 4 別々＋書斎
//   5 別々＋共用の化粧室 / 6 王の寝室＋書斎 / 7 別々＋子供部屋 / 8 別々＋化粧室＋書斎の 4 部屋
inline constexpr std::size_t kSuitePatternCount = 9;
inline constexpr std::array<int, kSuitePatternCount> kSuiteWeights = {8, 8, 18, 22, 12, 10, 8, 8, 6};
inline constexpr int kSuiteVaultPercent = 25;       // 私室の真ん中（化粧室か書斎）が宝物庫になる
inline constexpr int kSuiteInWingPercent = 35;      // 私室を玉座の裏でなく翼に置く（Hill は常に裏）

// 玉座の間の脇の形。重み（合計 100）。残りは「軸の全幅」。
inline constexpr int kThroneShapeSideRoomsPercent = 40;   // 左右に側室
inline constexpr int kThroneShapeShiftedPercent = 40;     // 片寄せ＋反対側に別の部屋
inline constexpr int kThroneShapeFullWidthPercent = 20;

// 玉座の間が無いときの奥。重み（合計 100）。
inline constexpr int kBackBigRoomPercent = 50;
inline constexpr int kBackCorridorCellsPercent = 35;
inline constexpr int kBackOpenGroundPercent = 15;

// ---- 玉座の間の側室 -------------------------------------------------------------------
// 側室を 1 部屋・2 部屋・3 部屋に分ける重み（合計 100。左右で別々に引く）。
inline constexpr int kSidePiecesOnePercent = 35;
inline constexpr int kSidePiecesTwoPercent = 40;
inline constexpr int kSidePiecesThreePercent = 25;
inline constexpr int kSideSpecialRoomPercent = 18;        // 1 部屋ずつが特別な部屋（宝物庫、タイプ別の部屋）になる
inline constexpr int kSideCourtyardPercent = 12;          // 分けた側室の 1 つが中庭になる（片側 1 つまで）
inline constexpr int kCourtyardMinSize = 12;              // 中庭にする側室の最小の幅・高さ（小さい中庭は通用口の対象にならないため）
inline constexpr int kSideCourtyardFrom = 82;             // 100 面の目のうち、中庭は [82, 94)（特別な部屋は [0, 18) と重ならない）

// ---- 翼・輪郭 -------------------------------------------------------------------------
inline constexpr int kSilhouetteKinds = 7;                // 輪郭の型は一様
// 以下は「range 個の目のうち N 個」で引く（出力を変えないため、元の抽選の目数をそのまま使う）。
inline constexpr int kWingCourtyardRange = 10;            // 翼が中庭（リング）になる確率 3/10（幅 22 以上、高さ 30 以上のとき）
inline constexpr int kWingCourtyardHits = 3;
inline constexpr int kStackedBlocksMax = 3;               // 段数 1〜3 は一様
inline constexpr int kNotchRange = 5;                     // 3 段のとき、真ん中を欠いた凹みにする確率 2/5
inline constexpr int kNotchHits = 2;

// ---- 廊下 -----------------------------------------------------------------------------
// 翼の廊下の基準確率（城のタイプ別）。縦・横を独立に引く。比（0〜1）。
inline constexpr double kCorridorChancePalace = 0.9;
inline constexpr double kCorridorChanceConcentric = 0.7;
inline constexpr double kCorridorChanceHill = 0.6;
inline constexpr double kCorridorChanceFortress = 0.3;
inline constexpr double kCorridorChanceRiver = 0.5;       // River とその他
// 豊かさが 1.0 から 1 上がるごとに足す確率。
inline constexpr double kCorridorChancePerWealth = 0.10;

// ---- 袖棟（keep_expansion）-------------------------------------------------------------
// 豊かさがこの値以上で 1 棟、2 棟。確率ではなく条件。
inline constexpr double kKeepAnnexWealthOne = 1.5;
inline constexpr double kKeepAnnexWealthTwo = 2.15;

// ---- 城の大きさ -----------------------------------------------------------------------
// 城の最小の大きさ（マス）。エディターは 128×128 以上だけを扱うので、それより小さい指定は 128 に引き上げる。
inline constexpr int kMinCastleSize = 128;

// ---- 部屋の形 -------------------------------------------------------------------------
// 長辺が短辺の何倍を超える部屋は、長辺の向きに分割する（10 倍値。30 なら 3:1）。玉座の間・待機室・前室・廊下・私室は除く。
inline constexpr int kRoomMaxAspectTenths = 30;
// 分割した 1 部屋の長辺が短辺の何倍以内になるように分けるか（10 倍値）。
inline constexpr int kSplitTargetAspectTenths = 20;
// 部屋を分割するときの切り目のゆらぎ（等分からの割合。±12% なら 2 分割の比は最大 約 1.6:1。大きいほうが部屋の面積の上限を超えない範囲）と、1 部屋の最小の長さ。
inline constexpr int kSplitJitterPercent = 12;
inline constexpr int kBandSplitJitterPercent = 5;  // Hill の帯の小部屋は、従来どおりほぼ等分（廊下に幅のある 2 列が要るため）
inline constexpr int kSplitMinPiece = 8;
inline constexpr int kSplitMinPieceFurnished = 12;  // 台所・寝室・兵舎などを分割するときの 1 部屋の最小の長さ

// ---- 廊下の幅 -------------------------------------------------------------------------
// すべての廊下（縦・横・Hill の帯・玄関の廊下）の幅。壁 1 マスを含む（8 なら床は 7 マス）。
inline constexpr int kCorridorWidth = 8;
// 廊下に必ずできる扉の最小数。足りなければ、廊下に接する他の部屋にも扉を付ける（玉座の間・私室・別の廊下は除く）。
inline constexpr int kCorridorMinDoors = 3;
// 翼の横向き廊下が「窓側（翼の上端か下端、外壁沿い、部屋は片側だけ）」になる割合（両方の置き方ができる翼のとき）
inline constexpr int kCorridorWindowSidePercent = 35;
// 横向き廊下が1本できた翼に、もう1本できる割合（翼の縦幅に余裕があるとき。王座の間の側壁にも廊下が開ける）
inline constexpr int kCorridorSecondPercent = 45;
// 窓側廊下に面する部屋の、廊下を引いたあとの最小の奥行き（通常の廊下は kCorridorMinRoomWidth=12）
inline constexpr int kWindowCorridorMinRoomDepth = 8;
// Hill の帯の廊下の両脇に残す部屋の最小の幅（廊下の幅を除く）。
inline constexpr int kBandCorridorMinRoomWidth = 9;

// ==== 寸法 =====================================================================================
// 軸・翼・帯の大きさを決める数値。単位はセルか、軸の高さ・城の幅に対するパーセント。
// 「Base + 乱数(Spread)」は、Base 以上 Base+Spread-1 以下の一様な値。
// 軸の高さ（A）に対する割合
inline constexpr int kPrivShare = 12, kPrivShareCompact = 10;         // 私室（玉座の裏）
inline constexpr int kAnteShare = 15, kAnteShareCompact = 11;         // 待機室
inline constexpr int kThroneShare = 32;                               // 玉座の間（標準）
inline constexpr int kThroneSharePalace = 40;
inline constexpr int kHillThroneShare = 30, kHillThroneShareCompact = 24;
inline constexpr int kThroneTargetShare = 46;                         // 玉座の間を奥に伸ばす目標（縦:横 約 2:1）
inline constexpr int kHallMinShare = 20, kHallMinDepth = 10;          // 伸ばしても残す玄関ホールの深さ（割合と最小セル）
// 最小のセル数（compact は小さい城）
inline constexpr int kPrivMin = 8, kPrivMinCompact = 6;
inline constexpr int kAnteMin = 10, kAnteMinCompact = 6;
inline constexpr int kSuiteMinDepth = 11;                             // 私室が玉座の裏にあるときの最小の深さ
inline constexpr int kThroneMinWidth = 14;
inline constexpr int kThroneMinWidthWithSuite = 22;                   // 私室の扉が玉座に開くときは、床を空けるため 22 以上
inline constexpr int kEntryCorridorMinDepth = 14, kEntryCorridorMinWidth = 28;  // 正門即廊下にできる玄関の最小の深さと軸の幅
// 軸の幅（城の幅 w に対する割合）
inline constexpr int kColumnBase = 30, kColumnSpread = 13, kColumnMin = 12;
inline constexpr int kHillColumnBase = 34, kHillColumnSpread = 27, kHillColumnMin = 20, kHillColumnTrimRange = 4;
inline constexpr int kHillColumnOffsetBase = 30, kHillColumnOffsetSpread = 41;   // 塔の位置（空き幅の 30〜70%）
inline constexpr int kHillHallDivisorBase = 4, kHillHallDivisorSpread = 3, kHillHallDivisorCompact = 5;  // 玄関の深さ h/(4〜6)
// Hill の帯の削り（段ごとの余白）
inline constexpr int kHillInsetRange = 3, kHillInsetStep = 5;         // 幅の 0・5・10%
inline constexpr int kHillTrimRange = 4, kHillTrimStep = 15;          // 高さの 0・15・30・45%
inline constexpr int kHillBandMinSpan = 14;                            // 帯の削りの後に、左右それぞれに残す最小の高さ（行）
inline constexpr int kHillOneSidedPercent = 12;                         // Hill の帯が、広いほうの片側だけにある割合
inline constexpr int kHillOneSidedMaxEmptyPercent = 20;                  // 帯のない側の幅が、城の幅のこの割合以下のときだけ片側にする
// Hill 以外の帯（軸の脇の翼）の高さと段ごとの余白
inline constexpr int kBandBase = 34, kBandSpread = 17;
inline constexpr int kBandInsetRange = 4, kBandInsetStep = 12;
// 翼の範囲
inline constexpr int kWingBackSpread = 16;                            // 翼の奥の端のゆらぎ
inline constexpr int kWingEndSpread = 21;                             // 翼の手前の端のゆらぎ
inline constexpr int kPalaceWingBase = 22, kPalaceWingSpread = 9;     // Palace の翼の幅 22〜30%
inline constexpr int kPalaceWingEndRange = 5;                         // Palace の翼が手前で止まる量（×10%）
inline constexpr int kPalaceFrontShare = 78;                          // Palace の正面の位置（高さの 78%）
// 翼の積み段
inline constexpr int kStackReachBase = 50, kStackReachSpread = 51;
inline constexpr int kHillStepInReachBase = 50;                        // Hill の帯で、幅いっぱいに届く段のない側の最小の張り出し（幅の 65〜100%）    // 段の張り出し（幅の 50〜100%）
// 翼の中庭（リング）
inline constexpr int kRingMinWidth = 22, kRingMinSpan = 30;
inline constexpr int kRingEndBase = 24, kRingEndSpread = 12;          // 両端の長さ（翼の長さの 24〜35%）
inline constexpr int kRingStripBase = 30, kRingStripSpread = 11;      // 外側の帯の幅（翼の幅の 30〜40%）
// 輪郭の型: {左の幅, 左の奥, 左の手前, 右の幅, 右の奥, 右の手前}（城の幅・高さに対する %）
inline constexpr std::array<std::array<int, 6>, kSilhouetteKinds> kSilhouettes = {{
    {28, 20, 80, 28, 20, 80},    // 十字
    {28, 35, 100, 28, 35, 100},  // 背の高い翼、低い冠
    {34, 0, 60, 22, 25, 100},    // 段違い
    {34, 0, 100, 0, 0, 0},       // L 字（左に翼）
    {0, 0, 0, 34, 0, 100},       // L 字（右に翼）
    {32, 10, 70, 32, 10, 70},    // ずんぐり
    {24, 15, 100, 30, 40, 100},  // 不均等
}};

// ---- 検査（コンパイル時）------------------------------------------------------------------
constexpr int Sum(const std::array<int, kSuitePatternCount>& values) {
    int total = 0;
    for (const int value : values) total += value;
    return total;
}
static_assert(Sum(kSuiteWeights) == 100, "the suite weights must add up to 100");
static_assert(kThroneShapeSideRoomsPercent + kThroneShapeShiftedPercent + kThroneShapeFullWidthPercent == 100, "throne shape weights");
static_assert(kBackBigRoomPercent + kBackCorridorCellsPercent + kBackOpenGroundPercent == 100, "back form weights");
static_assert(kSidePiecesOnePercent + kSidePiecesTwoPercent + kSidePiecesThreePercent == 100, "side piece weights");
static_assert(kModeWeightConcentric + kModeWeightHill + kModeWeightRiver + kModeWeightPalace + kModeWeightFortress > 0.999 &&
              kModeWeightConcentric + kModeWeightHill + kModeWeightRiver + kModeWeightPalace + kModeWeightFortress < 1.001, "mode weights");

// ---- 抽選の補助 -----------------------------------------------------------------------
// 重み表から、0〜99 の乱数に対応する添字を返す。
template <std::size_t N>
constexpr int PickByWeight(const std::array<int, N>& weights, int roll) {
    int cut = 0;
    for (std::size_t i = 0; i < N; ++i) {
        cut += weights[i];
        if (roll < cut) return static_cast<int>(i);
    }
    return static_cast<int>(N) - 1;
}

}  // namespace castle_odds
