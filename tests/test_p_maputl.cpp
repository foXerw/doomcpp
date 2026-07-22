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
