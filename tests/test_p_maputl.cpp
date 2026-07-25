#include "doctest.h"
#include "play/p_maputl.h"
#include "play/p_setup.h"

namespace {
// Minimal map with a single vertical BSP partition at x=256 splitting two subsectors.
// side 0 = right half (x>256), side 1 = left half (x<256).
struct MiniMap { MapData m; };
static fixed_t F(int m) { return static_cast<fixed_t>(m) << FRACBITS; }
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
    node_t n; n.x = F(256); n.y = F(0); n.dx = F(0); n.dy = F(256);
    n.children[0] = NF_SUBSECTOR | 0;   // right -> subsector 0
    n.children[1] = NF_SUBSECTOR | 1;   // left  -> subsector 1
    m.nodes.push_back(n);
    return mm;
}
} // namespace

TEST_CASE("R_PointOnSide picks front/back of a partition") {
    node_t n; n.x = F(256); n.y = F(0); n.dx = F(0); n.dy = F(256);
    CHECK(R_PointOnSide(F(300), F(100), n) == 0);
    CHECK(R_PointOnSide(F(100), F(100), n) == 1);
}

TEST_CASE("R_PointInSubsector descends BSP to the right leaf") {
    auto mm = buildBspMap();
    CHECK(R_PointInSubsector(mm.m, F(300), F(100)) == 0);
    CHECK(R_PointInSubsector(mm.m, F(100), F(100)) == 1);
}

TEST_CASE("R_PointInSubsector: no nodes returns subsector 0") {
    MapData m;
    subsector_t ss; ss.segcount = 0; ss.firstseg = 0; m.subsectors.push_back(ss);
    CHECK(R_PointInSubsector(m, F(5), F(5)) == 0);
}

TEST_CASE("sectorOf resolves subsector -> sector via its first seg") {
    auto mm = buildBspMap();
    CHECK(sectorOf(mm.m, 0) == 0);   // seg0.frontsector = 0
    CHECK(sectorOf(mm.m, 1) == 1);   // seg1.frontsector = 1
    CHECK(sectorOf(mm.m, 99) == -1); // out of range
}

TEST_CASE("P_BoxOnLineSide: box straddling a line returns -1") {
    BBox bb{ F(240), F(272), F(240), F(272) };   // left,right,bottom,top
    CHECK(P_BoxOnLineSide(bb, F(270), F(128), F(270), F(384)) == -1);
}

TEST_CASE("P_BoxOnLineSide: box wholly on one side returns that side") {
    BBox left{F(100),F(120),F(100),F(120)};
    CHECK(P_BoxOnLineSide(left, F(270),F(128),F(270),F(384)) != -1);
    BBox right{F(400),F(420),F(100),F(120)};
    CHECK(P_BoxOnLineSide(right, F(270),F(128),F(270),F(384)) != -1);
}

TEST_CASE("P_LineOpening: two-sided -> range = min ceil - max floor") {
    MapData m;
    sector_t a; a.floorheight = 0;  a.ceilingheight = 128; m.sectors.push_back(a);
    sector_t b; b.floorheight = 16; b.ceilingheight = 200; m.sectors.push_back(b);
    side_t sa; sa.sector = 0; m.sides.push_back(sa);
    side_t sb; sb.sector = 1; m.sides.push_back(sb);
    line_t L; L.sidenum[0] = 0; L.sidenum[1] = 1; m.lines.push_back(L);
    Opening o = P_LineOpening(m, L);
    CHECK(o.top      == 128 << FRACBITS);
    CHECK(o.bottom   == 16  << FRACBITS);
    CHECK(o.lowfloor == 0);
    CHECK(o.range    == 112 << FRACBITS);
}

TEST_CASE("P_LineOpening: one-sided line -> range 0") {
    MapData m;
    sector_t a; a.floorheight = 0; a.ceilingheight = 128; m.sectors.push_back(a);
    side_t sa; sa.sector = 0; m.sides.push_back(sa);
    line_t L; L.sidenum[0] = 0; L.sidenum[1] = -1; m.lines.push_back(L);
    Opening o = P_LineOpening(m, L);
    CHECK(o.range == 0);
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
