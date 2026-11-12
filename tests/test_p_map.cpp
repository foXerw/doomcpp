#include "doctest.h"
#include "play/p_map.h"
#include "play/p_maputl.h"
#include "play/p_setup.h"
#include "play/p_mobj.h"
#include "play/p_tick.h"
#include "play/info.h"
#include "core/m_tables.h"
#include <cstdlib>

namespace {
struct TestMap { MapData m; Blockmap bm; };
static fixed_t F(int v) { return static_cast<fixed_t>(v) << FRACBITS; }
static TestMap buildTestMap(bool oneSided, int backFloor) {
    TestMap tm; auto& m = tm.m;
    m.vertices.push_back({270 << 16, 128 << 16});
    m.vertices.push_back({270 << 16, 384 << 16});
    line_t L; L.v1=0; L.v2=1; L.flags=ML_TWOSIDED; L.special=0; L.tag=0;
    L.sidenum[0]=0; L.sidenum[1]= oneSided?-1:1; m.lines.push_back(L);
    side_t s0; s0.sector=0; m.sides.push_back(s0);
    if (!oneSided) { side_t s1; s1.sector=1; m.sides.push_back(s1); }
    sector_t A; A.floorheight=0;        A.ceilingheight=128; m.sectors.push_back(A);
    sector_t B; B.floorheight=backFloor;B.ceilingheight=128; m.sectors.push_back(B);
    seg_t g; g.linedef=0; g.side=0; g.frontsector=0; g.backsector= oneSided?-1:1; m.segs.push_back(g);
    subsector_t ss; ss.segcount=1; ss.firstseg=0; m.subsectors.push_back(ss);
    Blockmap& bm = tm.bm; bm.orgx=0; bm.orgy=0; bm.width=4; bm.height=4;
    bm.lump = {0,0,4,4, 20,20,20,20, 20,20,21,20, 20,20,21,20, 20,20,20,20, -1, 0,-1};
    return tm;
}
static mobj_t playerMobj(fixed_t x, fixed_t y) {
    mobj_t mo; mo.info = &mobjinfo[MT_PLAYER]; mo.flags = mobjinfo[MT_PLAYER].flags;
    mo.x=x; mo.y=y; mo.floorz=0;
    return mo;
}
}

TEST_CASE("P_CheckPosition: one-sided wall blocks") {
    auto tm = buildTestMap(true,0); mobj_t mo = playerMobj(F(256),F(256));
    CHECK(P_CheckPosition(tm.m, tm.bm, mo, F(256),F(256)).ok == false);
}
TEST_CASE("P_CheckPosition: open space clear, seeds floor/ceiling") {
    auto tm = buildTestMap(true,0); mobj_t mo = playerMobj(F(64),F(64));
    PosCheck pc = P_CheckPosition(tm.m, tm.bm, mo, F(64),F(64));
    CHECK(pc.ok == true); CHECK(pc.floorz == 0); CHECK(pc.ceilingz == 128<<FRACBITS);
}
TEST_CASE("P_CheckPosition: two-sided portal raises floor, keeps ok") {
    auto tm = buildTestMap(false,8); mobj_t mo = playerMobj(F(256),F(256));
    PosCheck pc = P_CheckPosition(tm.m, tm.bm, mo, F(256),F(256));
    CHECK(pc.ok == true); CHECK(pc.floorz == 8<<FRACBITS); CHECK(pc.dropoffz == 0);
}
TEST_CASE("P_TryMove: passable step commits and raises floorz") {
    auto tm = buildTestMap(false,8); mobj_t mo = playerMobj(F(100),F(100));
    CHECK(P_TryMove(tm.m, tm.bm, mo, F(256),F(256)) == true);
    CHECK(mo.x == F(256)); CHECK(mo.y == F(256)); CHECK(mo.floorz == 8<<FRACBITS);
}
TEST_CASE("P_TryMove: step too high rejected") {
    auto tm = buildTestMap(false,32); mobj_t mo = playerMobj(F(100),F(100));
    CHECK(P_TryMove(tm.m, tm.bm, mo, F(256),F(256)) == false);
    CHECK(mo.x == F(100)); CHECK(mo.y == F(100));
}
TEST_CASE("P_TryMove: dropoff allowed (MF_DROPOFF)") {
    auto tm = buildTestMap(false,-32); mobj_t mo = playerMobj(F(100),F(100));
    CHECK(P_TryMove(tm.m, tm.bm, mo, F(256),F(256)) == true);
    CHECK(mo.x == F(256)); CHECK(mo.floorz == 0);
}
TEST_CASE("P_TryMove: one-sided wall rejected; open accepted") {
    auto tm = buildTestMap(true,0); mobj_t mo = playerMobj(F(100),F(100));
    CHECK(P_TryMove(tm.m, tm.bm, mo, F(256),F(256)) == false);
    CHECK(P_TryMove(tm.m, tm.bm, mo, F(64),F(64)) == true);
    CHECK(mo.x == F(64));
}
TEST_CASE("P_TrySlide: fully blocked -> no move") {
    auto tm = buildTestMap(true,0); mobj_t mo = playerMobj(F(250),F(200));
    CHECK(P_TrySlide(tm.m, tm.bm, mo, F(30),0) == false);
    CHECK(mo.x == F(250)); CHECK(mo.y == F(200));
}
TEST_CASE("P_TrySlide: diagonal slides along perpendicular axis") {
    auto tm = buildTestMap(true,0); mobj_t mo = playerMobj(F(250),F(200));
    CHECK(P_TrySlide(tm.m, tm.bm, mo, F(30),F(40)) == true);
    CHECK(mo.x == F(250)); CHECK(mo.y == F(240));
}
TEST_CASE("P_CalcHeight: viewz clamped under low ceiling") {
    MapData m; sector_t s; s.floorheight=0; s.ceilingheight=40; m.sectors.push_back(s);
    // P_CalcHeight derives the sector from p.mo->subsector via sectorOf; give this
    // minimal single-sector map a subsector[0] -> sector 0 so the low ceiling engages.
    seg_t g; g.linedef=-1; g.side=0; g.frontsector=0; g.backsector=-1; m.segs.push_back(g);
    subsector_t ss; ss.segcount=1; ss.firstseg=0; m.subsectors.push_back(ss);
    mobj_t mo = playerMobj(0,0); mo.subsector=0;
    player_t pl; pl.mo=&mo; pl.viewz=0;
    for (int i=0;i<40;++i) P_CalcHeight(m, pl);
    CHECK(pl.viewz == 36<<FRACBITS);
}
TEST_CASE("P_CalcHeight: viewz converges to floorz+VIEWHEIGHT") {
    MapData m; sector_t s; s.floorheight=100; s.ceilingheight=1000; m.sectors.push_back(s);
    seg_t g; g.linedef=-1; g.side=0; g.frontsector=0; g.backsector=-1; m.segs.push_back(g);
    subsector_t ss; ss.segcount=1; ss.firstseg=0; m.subsectors.push_back(ss);
    mobj_t mo = playerMobj(0,0); mo.subsector=0; mo.floorz=100<<FRACBITS;
    player_t pl; pl.mo=&mo; pl.viewz=100<<FRACBITS;
    for (int i=0;i<40;++i) P_CalcHeight(m, pl);
    CHECK(std::abs(pl.viewz - (141<<FRACBITS)) <= (1<<FRACBITS));
}
TEST_CASE("P_MovePlayer keeps raised floorz straddling a step") {
    R_InitTables();
    auto tm = buildTestMap(false,8);
    mobj_t mo = playerMobj(F(264),F(256)); mo.angle=0; mo.subsector=0;
    player_t pl; pl.mo=&mo; pl.viewz=VIEWHEIGHT;
    P_MovePlayer(tm.m, tm.bm, pl, 0,0,0);
    CHECK(mo.floorz == 8<<FRACBITS);
}
TEST_CASE("P_MovePlayer: forward in open space moves +X at angle 0") {
    R_InitTables();
    auto tm = buildTestMap(true,0);
    mobj_t mo = playerMobj(F(64),F(64)); mo.angle=0; mo.subsector=0;
    player_t pl; pl.mo=&mo; pl.viewz=VIEWHEIGHT;
    P_MovePlayer(tm.m, tm.bm, pl, F(5),0,0);
    CHECK(mo.x > F(64)); CHECK(mo.y == F(64));
    CHECK(std::abs(pl.viewz - (F(0)+VIEWHEIGHT)) <= (1<<FRACBITS));
}
TEST_CASE("P_MovePlayer: walking into a wall does not cross it") {
    R_InitTables();
    auto tm = buildTestMap(true,0);
    mobj_t mo = playerMobj(F(250),F(200)); mo.angle=ANG90; mo.subsector=0;
    player_t pl; pl.mo=&mo; pl.viewz=VIEWHEIGHT;
    P_MovePlayer(tm.m, tm.bm, pl, F(30),0,0);
    CHECK(mo.y > F(200));
    mo.angle = 0; fixed_t before = mo.x;
    P_MovePlayer(tm.m, tm.bm, pl, F(30),0,0);
    CHECK(mo.x < F(270)); CHECK(mo.x >= before);
}
