#include "doctest.h"
#include "play/p_map.h"
#include "play/p_maputl.h"
#include "play/p_setup.h"

namespace {
// Hand-built collision test map. A single spatial sector A (floor 0, ceil 128) with:
//  - line L0 at x=270 (y 128..384): one-sided WALL (oneSided=true) OR a two-sided PORTAL
//    to sector B (floor = backFloor, ceil 128).
// Player at (256,256) (bbox [240,272]^2) straddles L0; at (64,64) it's in open space.
// Blockmap is 4x4 (org 0,0); L0 listed in cells (2,1) and (2,2).
struct TestMap { MapData m; Blockmap bm; };
static TestMap buildTestMap(bool oneSided, int backFloor) {
    TestMap tm;
    auto& m = tm.m;
    m.vertices.push_back({270 << 16, 128 << 16});   // v0
    m.vertices.push_back({270 << 16, 384 << 16});   // v1
    line_t L; L.v1 = 0; L.v2 = 1; L.flags = ML_TWOSIDED; L.special = 0; L.tag = 0;
    L.sidenum[0] = 0; L.sidenum[1] = oneSided ? -1 : 1;
    m.lines.push_back(L);
    side_t s0; s0.sector = 0; m.sides.push_back(s0);
    if (!oneSided) { side_t s1; s1.sector = 1; m.sides.push_back(s1); }
    sector_t A; A.floorheight = 0;        A.ceilingheight = 128; m.sectors.push_back(A);
    sector_t B; B.floorheight = backFloor; B.ceilingheight = 128; m.sectors.push_back(B);
    seg_t g; g.linedef = 0; g.side = 0; g.frontsector = 0; g.backsector = oneSided ? -1 : 1;
    m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0; m.subsectors.push_back(ss);  // no nodes -> ss0

    Blockmap& bm = tm.bm;
    bm.orgx = 0; bm.orgy = 0; bm.width = 4; bm.height = 4;
    // lump: header[0..3]; offsets[4..19] (cell linear = cy*4+cx); index20 = empty [-1];
    // index21..22 = list [0, -1]. cells (2,1) and (2,2) -> offset 21; all others -> 20.
    bm.lump = {0,0,4,4,
               20,20,20,20,  20,20,21,20,  20,20,21,20,  20,20,20,20,
               -1,  0,-1};
    return tm;
}
} // namespace

TEST_CASE("P_CheckPosition: one-sided wall blocks") {
    auto tm = buildTestMap(/*oneSided*/true, 0);
    PosCheck pc = P_CheckPosition(tm.m, tm.bm, 256, 256);
    CHECK(pc.ok == false);
}

TEST_CASE("P_CheckPosition: open space is clear, seeds floor/ceiling") {
    auto tm = buildTestMap(true, 0);
    PosCheck pc = P_CheckPosition(tm.m, tm.bm, 64, 64);   // cell (0,0): no lines
    CHECK(pc.ok == true);
    CHECK(pc.floorz == 0.0f);
    CHECK(pc.ceilingz == 128.0f);
}

TEST_CASE("P_CheckPosition: two-sided portal raises floor, keeps ok") {
    auto tm = buildTestMap(false, 8);   // back floor +8 (a step up)
    PosCheck pc = P_CheckPosition(tm.m, tm.bm, 256, 256);
    CHECK(pc.ok == true);               // not a solid blocker
    CHECK(pc.floorz == 8.0f);           // raised to back floor
    CHECK(pc.dropoffz == 0.0f);         // min(front 0, back 8)
}

TEST_CASE("P_TryMove: passable step commits and raises floorz") {
    auto tm = buildTestMap(false, 8);   // back floor +8
    Player p; p.x = 100; p.y = 100; p.floorz = 0;
    CHECK(P_TryMove(tm.m, tm.bm, p, 256, 256) == true);
    CHECK(p.x == 256.0f);
    CHECK(p.y == 256.0f);
    CHECK(p.floorz == 8.0f);            // adopted the stepped-up floor
}

TEST_CASE("P_TryMove: step too high (>24) is rejected") {
    auto tm = buildTestMap(false, 32);  // back floor +32
    Player p; p.x = 100; p.y = 100; p.floorz = 0;
    CHECK(P_TryMove(tm.m, tm.bm, p, 256, 256) == false);
    CHECK(p.x == 100.0f);               // unchanged
    CHECK(p.y == 100.0f);
}

TEST_CASE("P_TryMove: dropoff >24 is rejected") {
    auto tm = buildTestMap(false, -32); // back floor -32
    Player p; p.x = 100; p.y = 100; p.floorz = 0;
    CHECK(P_TryMove(tm.m, tm.bm, p, 256, 256) == false);
    CHECK(p.x == 100.0f);
}

TEST_CASE("P_TryMove: one-sided wall rejected; open target accepted") {
    auto tm = buildTestMap(true, 0);
    Player p; p.x = 100; p.y = 100; p.floorz = 0;
    CHECK(P_TryMove(tm.m, tm.bm, p, 256, 256) == false);   // into wall
    CHECK(P_TryMove(tm.m, tm.bm, p, 64, 64) == true);      // open
    CHECK(p.x == 64.0f);
}

TEST_CASE("P_TrySlide: fully blocked -> no move, returns false") {
    auto tm = buildTestMap(true, 0);
    Player p; p.x = 250; p.y = 200; p.floorz = 0;          // just left of wall at x=270
    CHECK(P_TrySlide(tm.m, tm.bm, p, 30, 0) == false);     // +X into wall, no Y to slide
    CHECK(p.x == 250.0f);
    CHECK(p.y == 200.0f);
}

TEST_CASE("P_TrySlide: diagonal into wall slides along the perpendicular axis") {
    auto tm = buildTestMap(true, 0);
    Player p; p.x = 250; p.y = 200; p.floorz = 0;
    // full (280,240) straddles wall -> blocked; X-only (280,200) straddles -> blocked;
    // Y-only (250,240) is clear -> commits. Player ends at (250,240).
    CHECK(P_TrySlide(tm.m, tm.bm, p, 30, 40) == true);
    CHECK(p.x == 250.0f);              // X component eaten by wall
    CHECK(p.y == 240.0f);              // Y component kept (slid along wall)
}
