#include "doctest.h"
#include "play/p_maputl.h"
#include "play/p_setup.h"

namespace {
// Minimal map with a single vertical BSP partition at x=256 splitting two subsectors.
// side 0 = right half (x>256), side 1 = left half (x<256).
struct MiniMap { MapData m; };
static MiniMap buildBspMap() {
    MiniMap mm;
    auto& m = mm.m;
    // sectors: 0 (right), 1 (left)
    sector_t s0; s0.floorheight = 0;   s0.ceilingheight = 128; m.sectors.push_back(s0);
    sector_t s1; s1.floorheight = 10;  s1.ceilingheight = 128; m.sectors.push_back(s1);
    // sides -> sectors
    side_t d0; d0.sector = 0; m.sides.push_back(d0);
    side_t d1; d1.sector = 1; m.sides.push_back(d1);
    // two segs, each in its own subsector
    seg_t g0; g0.linedef = 0; g0.side = 0; g0.frontsector = 0; g0.backsector = -1; m.segs.push_back(g0);
    seg_t g1; g1.linedef = 0; g1.side = 1; g1.frontsector = 1; g1.backsector = -1; m.segs.push_back(g1);
    subsector_t ss0; ss0.segcount = 1; ss0.firstseg = 0; m.subsectors.push_back(ss0);
    subsector_t ss1; ss1.segcount = 1; ss1.firstseg = 1; m.subsectors.push_back(ss1);
    // root node: partition from (256,0) going +Y (dx=0, dy>0)
    node_t n; n.x = 256; n.y = 0; n.dx = 0; n.dy = 256;
    n.children[0] = NF_SUBSECTOR | 0;   // right -> subsector 0
    n.children[1] = NF_SUBSECTOR | 1;   // left  -> subsector 1
    m.nodes.push_back(n);
    return mm;
}
} // namespace

TEST_CASE("R_PointOnSide picks front/back of a partition") {
    node_t n; n.x = 256; n.y = 0; n.dx = 0; n.dy = 256;   // vertical partition at x=256
    CHECK(R_PointOnSide(300, 100, n) == 0);   // right -> front (0)
    CHECK(R_PointOnSide(100, 100, n) == 1);   // left  -> back  (1)
}

TEST_CASE("R_PointInSubsector descends BSP to the right leaf") {
    auto mm = buildBspMap();
    CHECK(R_PointInSubsector(mm.m, 300, 100) == 0);   // right half -> subsector 0
    CHECK(R_PointInSubsector(mm.m, 100, 100) == 1);   // left half  -> subsector 1
}

TEST_CASE("R_PointInSubsector: no nodes returns subsector 0") {
    MapData m;
    subsector_t ss; ss.segcount = 0; ss.firstseg = 0; m.subsectors.push_back(ss);
    CHECK(R_PointInSubsector(m, 5, 5) == 0);
}

TEST_CASE("sectorOf resolves subsector -> sector via its first seg") {
    auto mm = buildBspMap();
    CHECK(sectorOf(mm.m, 0) == 0);   // seg0.frontsector = 0
    CHECK(sectorOf(mm.m, 1) == 1);   // seg1.frontsector = 1
    CHECK(sectorOf(mm.m, 99) == -1); // out of range
}

#include <cmath>

TEST_CASE("P_BoxOnLineSide: box straddling a line returns -1") {
    BBox bb{ /*left*/240, /*right*/272, /*bottom*/240, /*top*/272};
    // vertical line at x=270
    CHECK(P_BoxOnLineSide(bb, 270, 128, 270, 384) == -1);   // straddle -> collision candidate
}

TEST_CASE("P_BoxOnLineSide: box wholly on one side returns that side") {
    BBox left{100, 120, 100, 120};   // entirely left of vertical line x=270
    CHECK(P_BoxOnLineSide(left, 270, 128, 270, 384) != -1);
    BBox right{400, 420, 100, 120};  // entirely right
    CHECK(P_BoxOnLineSide(right, 270, 128, 270, 384) != -1);
}

TEST_CASE("P_LineOpening: two-sided -> range = min ceil - max floor") {
    MapData m;
    sector_t a; a.floorheight = 0;  a.ceilingheight = 128; m.sectors.push_back(a);
    sector_t b; b.floorheight = 16; b.ceilingheight = 200; m.sectors.push_back(b);
    side_t sa; sa.sector = 0; m.sides.push_back(sa);
    side_t sb; sb.sector = 1; m.sides.push_back(sb);
    line_t L; L.sidenum[0] = 0; L.sidenum[1] = 1; m.lines.push_back(L);
    Opening o = P_LineOpening(m, L);
    CHECK(o.top == doctest::Approx(128));    // min(128,200)
    CHECK(o.bottom == doctest::Approx(16));  // max(0,16)
    CHECK(o.lowfloor == doctest::Approx(0)); // min(0,16)
    CHECK(o.range == doctest::Approx(112));
}

TEST_CASE("P_LineOpening: one-sided line -> range 0") {
    MapData m;
    sector_t a; a.floorheight = 0; a.ceilingheight = 128; m.sectors.push_back(a);
    side_t sa; sa.sector = 0; m.sides.push_back(sa);
    line_t L; L.sidenum[0] = 0; L.sidenum[1] = -1; m.lines.push_back(L);
    Opening o = P_LineOpening(m, L);
    CHECK(o.range == 0.0f);
}

TEST_CASE("blockLinesInCell returns the cell's line list, empty out of range") {
    Blockmap bm;
    bm.orgx = 0; bm.orgy = 0; bm.width = 2; bm.height = 2;
    // same layout as the parseBlockmap test: cell(0,0) -> [5,7,-1]; others empty
    bm.lump = {0,0,2,2,  9,8,8,8,  -1,  5,7,-1};
    auto c00 = blockLinesInCell(bm, 0, 0);
    REQUIRE(c00.size() == 2);
    CHECK(c00[0] == 5);
    CHECK(c00[1] == 7);
    CHECK(blockLinesInCell(bm, 1, 0).empty());   // empty cell
    CHECK(blockLinesInCell(bm, -1, 0).empty());  // out of range
    CHECK(blockLinesInCell(bm, 99, 99).empty());
}
