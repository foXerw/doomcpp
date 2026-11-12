#include "doctest.h"
#include "play/p_tick.h"
#include "play/p_mobj.h"
#include "play/p_setup.h"
#include "play/p_maputl.h"

namespace {
fixed_t F(int v){ return static_cast<fixed_t>(v)<<FRACBITS; }
// 单 subsector、无 node 的最小图（沿用 test_p_map 约定：R_PointInSubsector 返回 0）。
// 若实现发现 R_PointInSubsector 需要 node，则补一个 node：children=[NF_SUBSECTOR|0, NF_SUBSECTOR|0]。
MapData oneSectorMap() {
    MapData m;
    sector_t s; s.floorheight=0; s.ceilingheight=128; s.lightlevel=160; m.sectors.push_back(s);
    subsector_t ss; ss.segcount=0; ss.firstseg=0; m.subsectors.push_back(ss);
    return m;
}
}

TEST_CASE("P_SetupLevel spawns one mobj per known thing + player as mo") {
    MapData m = oneSectorMap();
    m.things.push_back({256,256,90,2035});   // barrel
    m.things.push_back({300,300,90,3004});   // zombieman
    m.things.push_back({400,400,90,11});     // DM start -> skipped
    m.things.push_back({500,500,90,1});      // player start
    World w; w.map = &m; w.bm = &m.blockmap;
    P_SetupLevel(w, m);
    CHECK(w.mobjs.size() == 3);              // barrel + zombieman + player
    CHECK(w.player.mo != nullptr);
    CHECK(w.player.mo->info == &mobjinfo[MT_PLAYER]);
    // 玩家坐标取自 thing 1 起点
    CHECK(w.player.mo->x == F(500));
    CHECK(w.player.mo->y == F(500));
}

TEST_CASE("P_SetupLevel initializes P5b combat state (100 hp, 50 clip, pistol ready)") {
    MapData m = oneSectorMap();
    m.things.push_back({256,256,90,1});      // player start
    World w; w.map = &m; w.bm = &m.blockmap;
    P_SetupLevel(w, m);
    REQUIRE(w.player.mo != nullptr);
    CHECK(w.player.health == 100);
    CHECK(w.player.mo->health == 100);
    CHECK(w.player.ammo[AM_CLIP] == 50);
    CHECK(w.player.maxammo[AM_CLIP] == 200);
    CHECK(w.player.weapons == ((1u << WP_FIST) | (1u << WP_PISTOL)));
    CHECK(w.player.readyweapon == WP_PISTOL);
    CHECK(w.player.pendingweapon == NUMWEAPONS);
    REQUIRE(w.player.psprites[0].state != nullptr);
    CHECK(w.player.psprites[0].state == &states[S_PISTOL]);
    CHECK(w.player.psprites[1].state == &states[S_NULL]);
}

TEST_CASE("P_PlayerThink decrements HUD timers and advances psprite states") {
    MapData m = oneSectorMap();
    m.things.push_back({256,256,90,1});
    World w; w.map = &m; w.bm = &m.blockmap;
    P_SetupLevel(w, m);
    w.player.damagecount = 3;
    w.player.bonuscount  = 2;
    for (int i = 0; i < 3; ++i) P_PlayerThink(w, false, -1);
    CHECK(w.player.damagecount == 0);
    CHECK(w.player.bonuscount  == 0);
    // S_PISTOL has tics=1 and nextstate=S_PISTOL: it re-enters every tic (A_WeaponReady).
    CHECK(w.player.psprites[0].state == &states[S_PISTOL]);
}

TEST_CASE("P_BuildThingsBySubsec bins mobjs by subsector, skips player.mo") {
    MapData m = oneSectorMap();
    m.things.push_back({256,256,90,2035});
    m.things.push_back({300,300,90,3004});
    World w; w.map = &m; w.bm = &m.blockmap;
    P_SetupLevel(w, m);
    P_BuildThingsBySubsec(w);
    REQUIRE(w.thingsBySubsec.size() >= 1);
    // subsector 0 持有 barrel+zombieman，不含 player.mo
    int counted = 0; bool sawPlayer = false;
    for (const mobj_t* mo : w.thingsBySubsec[0]) {
        ++counted;
        if (mo == w.player.mo) sawPlayer = true;
    }
    CHECK(counted == 2);
    CHECK(sawPlayer == false);
}
