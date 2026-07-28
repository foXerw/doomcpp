#include "doctest.h"
#include "play/p_mobj.h"
#include "play/p_tick.h"
#include "play/info.h"
#include "play/p_setup.h"
#include "play/p_maputl.h"
#include "play/p_map.h"
#include <string>

TEST_CASE("P_SpawnMobj inits fields from mobjinfo + spawnstate render cache") {
    World w;
    mobj_t* m = P_SpawnMobj(w, 100<<FRACBITS, 200<<FRACBITS, 0, MT_BARREL);
    REQUIRE(m != nullptr);
    CHECK(m->x == 100<<FRACBITS); CHECK(m->y == 200<<FRACBITS);
    CHECK(m->info == &mobjinfo[MT_BARREL]);
    CHECK(m->health == mobjinfo[MT_BARREL].spawnhealth);
    CHECK(m->flags == mobjinfo[MT_BARREL].flags);
    CHECK(std::string(m->sprname) == "BAR1");
    CHECK(m->frame == 'A');
    CHECK(w.mobjs.size() == 1);
    CHECK(&w.mobjs.back() == m);            // 返回的指针指向 list 节点（稳定）
}

TEST_CASE("P_SetMobjState refreshes render cache (sprite/frame/fullbright)") {
    World w;
    mobj_t* m = P_SpawnMobj(w, 0,0,0, MT_D2013);   // SOUL A fullbright
    CHECK(std::string(m->sprname) == "SOUL");
    CHECK(m->fullbright == true);
    CHECK(m->tics == -1);                          // static decoration -> never advance
    P_SetMobjState(*m, S_BAR1);                    // switch to barrel sprite
    CHECK(std::string(m->sprname) == "BAR1");
    CHECK(m->frame == 'A');
    CHECK(m->fullbright == false);
}

TEST_CASE("P_RemoveMobj erases from the thinker list") {
    World w;
    mobj_t* a = P_SpawnMobj(w, 0,0,0, MT_BARREL);
    P_SpawnMobj(w, 0,0,0, MT_BARREL);
    REQUIRE(w.mobjs.size() == 2);
    P_RemoveMobj(w, *a);
    CHECK(w.mobjs.size() == 1);
}

namespace {
fixed_t PF(int v){ return static_cast<fixed_t>(v)<<FRACBITS; }
// 单开放 sector（floor 0 ceil 128），单 subsector、无 node。seg/frontsector 让
// sectorOf() 能解析到 sector 0（P_CheckPosition 在 sec<0 时拒绝一切移动）。
MapData openMap() {
    MapData m;
    sector_t s; s.floorheight=0; s.ceilingheight=128; s.lightlevel=160; m.sectors.push_back(s);
    seg_t g; g.linedef=-1; g.side=0; g.frontsector=0; g.backsector=-1; m.segs.push_back(g);
    subsector_t ss; ss.segcount=1; ss.firstseg=0; m.subsectors.push_back(ss);
    return m;
}
}

TEST_CASE("P_ZMovement: upward momz arcs up then falls back to floorz, momz->0") {
    MapData m = openMap();
    World w; w.map=&m; w.bm=&m.blockmap;
    mobj_t* b = P_SpawnMobj(w, PF(100), PF(100), 0, MT_BARREL);
    b->floorz = 0; b->ceilingz = 128<<FRACBITS;
    b->momz = 8<<FRACBITS;
    fixed_t startZ = b->z, maxZ = b->z;
    for (int i=0;i<60;++i) { P_RunThinkers(w); if (b->z > maxZ) maxZ = b->z; }
    CHECK(maxZ > startZ);                       // 上升过
    CHECK(b->z   == b->floorz);                 // 最终落地
    CHECK(b->momz == 0);
}

TEST_CASE("P_XYMovement: horizontal mom slides then friction stops it") {
    MapData m = openMap();
    World w; w.map=&m; w.bm=&m.blockmap;
    mobj_t* b = P_SpawnMobj(w, PF(64), PF(64), 0, MT_BARREL);
    b->floorz = 0; b->ceilingz = 128<<FRACBITS;
    b->momx = 12<<FRACBITS;
    for (int i=0;i<80;++i) P_RunThinkers(w);
    CHECK(b->x > PF(64));                        // 前进了
    CHECK(b->y == PF(64));
    CHECK(b->momx == 0);                         // 摩擦停了
}

TEST_CASE("P_XYMovement: moving into a wall stops, does not cross") {
    // 复用 test_p_map 的墙图：墙在 x=270。barrel 在 (250,200) 向 +X。
    MapData m; Blockmap bm;
    m.vertices.push_back({270<<16,128<<16}); m.vertices.push_back({270<<16,384<<16});
    line_t L; L.v1=0;L.v2=1;L.flags=ML_TWOSIDED;L.special=0;L.tag=0;L.sidenum[0]=0;L.sidenum[1]=-1; m.lines.push_back(L);
    side_t s0; s0.sector=0; m.sides.push_back(s0);
    sector_t A; A.floorheight=0; A.ceilingheight=128; m.sectors.push_back(A);
    seg_t g; g.linedef=0;g.side=0;g.frontsector=0;g.backsector=-1; m.segs.push_back(g);
    subsector_t ss; ss.segcount=1; ss.firstseg=0; m.subsectors.push_back(ss);
    bm.orgx=0;bm.orgy=0;bm.width=4;bm.height=4;
    bm.lump={0,0,4,4, 20,20,20,20, 20,20,21,20, 20,20,21,20, 20,20,20,20, -1,0,-1};
    World w; w.map=&m; w.bm=&bm;
    mobj_t* b = P_SpawnMobj(w, PF(250), PF(200), 0, MT_BARREL);
    b->floorz=0; b->ceilingz=128<<FRACBITS;
    // momx=20（< MAXMOVE=30，单步不细分）：首步 250->270，bbox=[260,280] 横跨墙线 x=270
    // 被挡。注：brief 原写 momx=30，但 barrel radius=10 使首步 250->280 的 bbox 左沿恰好
    // =270，被 vanilla 的 bb.left>=lmaxx 早剔（相切=无穿透）放行穿越——故降到 20 以真正
    // 触发墙阻。判据仍是位置不越线。
    b->momx = 20<<FRACBITS;
    for (int i=0;i<20;++i) P_RunThinkers(w);
    CHECK(b->x < PF(270));                       // 未穿墙
    CHECK(b->x >= PF(250));                      // 朝墙移动了但被挡（不越过）
    // 注：不断言 momx==0——摩擦 0xe800(~0.906/tic) 在 20 tic 内不会把 momx 衰减到
    // STOPSPEED 以下；墙碰撞的正确判据是位置不越线（vanilla 同样靠摩擦逐 tic 衰减）。
}

TEST_CASE("static mobj (mom=0) stays put after many thinkers") {
    MapData m = openMap();
    World w; w.map=&m; w.bm=&m.blockmap;
    mobj_t* b = P_SpawnMobj(w, PF(100), PF(100), 0, MT_D2013);  // SOUL: NOGRAVITY
    b->floorz=0; b->ceilingz=128<<FRACBITS;
    fixed_t x0=b->x, y0=b->y, z0=b->z;
    for (int i=0;i<50;++i) P_RunThinkers(w);
    CHECK(b->x==x0); CHECK(b->y==y0); CHECK(b->z==z0);
}
