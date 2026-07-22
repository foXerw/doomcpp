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

TEST_CASE("P_TryMove: dropoff allowed (player MF_DROPOFF-exempt, like vanilla)") {
    auto tm = buildTestMap(false, -32);   // back floor -32 (a dropoff)
    Player p; p.x = 100; p.y = 100; p.floorz = 0;
    CHECK(P_TryMove(tm.m, tm.bm, p, 256, 256) == true);   // vanilla player walks off ledges
    CHECK(p.x == 256.0f);
    CHECK(p.floorz == 0.0f);   // point (256,256) is in sector A (floor 0); opening bottom = max(0,-32) = 0
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

TEST_CASE("P_CalcHeight: viewz approaches floor+VIEWHEIGHT, clamped under ceiling") {
    MapData m;
    sector_t s; s.floorheight = 0; s.ceilingheight = 40; m.sectors.push_back(s);  // low ceiling
    Player p; p.sector = 0; p.viewz = 0;
    // target = 0+41 = 41, but ceiling-4 = 36 -> clamp. The 0.125/tic smoothing converges
    // toward 41 but is clamped at the cap; after enough tics viewz sticks at 36 (a single
    // tic from viewz=0 only reaches 5.125, so the clamp must be observed at steady state).
    for (int i = 0; i < 40; ++i) P_CalcHeight(m, p);
    CHECK(p.viewz == doctest::Approx(36.0f));
}

TEST_CASE("P_CalcHeight: viewz converges to floor+VIEWHEIGHT over several tics") {
    MapData m;
    sector_t s; s.floorheight = 100; s.ceilingheight = 1000; m.sectors.push_back(s);
    Player p; p.sector = 0; p.viewz = 100;   // starts at floor, target = 141
    for (int i = 0; i < 40; ++i) P_CalcHeight(m, p);
    CHECK(p.viewz == doctest::Approx(141.0f).epsilon(0.01f));
    // monotonic approach
    Player q; q.sector = 0; q.viewz = 100;
    float prev = q.viewz;
    for (int i = 0; i < 5; ++i) { P_CalcHeight(m, q); CHECK(q.viewz >= prev); prev = q.viewz; }
}

TEST_CASE("P_MovePlayer: forward in open space moves + re-sectors + keeps viewz near target") {
    auto tm = buildTestMap(true, 0);
    Player p; p.x = 64; p.y = 64; p.angle = 0; p.floorz = 0; p.viewz = 41; p.sector = 0;
    // angle 0 -> forwardVec=(sin0,cos0)=(0,1): +Y. A small forward nudge stays in open space.
    P_MovePlayer(tm.m, tm.bm, p, /*fwd*/5, /*str*/0, /*turn*/0);
    CHECK(p.y > 64.0f);
    CHECK(p.x == 64.0f);
    CHECK(p.sector == 0);
    CHECK(p.viewz == doctest::Approx(41.0f).epsilon(0.01f));
}

TEST_CASE("P_MovePlayer: walking into a wall does not cross it") {
    auto tm = buildTestMap(true, 0);
    Player p; p.x = 250; p.y = 200; p.angle = 0; p.floorz = 0; p.viewz = 41; p.sector = 0;
    P_MovePlayer(tm.m, tm.bm, p, /*fwd*/30, 0, 0);   // forward +Y away from wall -> fine
    CHECK(p.y > 200.0f);
    // now face the wall (+X): angle so forwardVec ~ (+1,0). angle=PI/2 -> (sin,cos)=(1,0).
    p.angle = 3.14159265f / 2.0f;
    float before = p.x;
    P_MovePlayer(tm.m, tm.bm, p, 30, 0, 0);          // tries to step into wall at x=270
    CHECK(p.x < 270.0f);                              // did not cross the wall
    CHECK(p.x >= before);                             // may have slid/none; never past wall
}
