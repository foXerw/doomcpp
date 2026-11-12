#include "doctest.h"
#include "play/p_enemy.h"
#include "play/p_map.h"
#include "play/p_tick.h"
#include "play/p_setup.h"
#include "play/p_spec.h"
#include "play/p_inter.h"
#include "play/info.h"
#include "core/m_tables.h"
#include "core/m_random.h"
#include <cmath>
#include <string>

namespace {
fixed_t EF(int v) { return static_cast<fixed_t>(v) << FRACBITS; }

MapData openMap() {
    MapData m;
    sector_t s; s.floorheight = 0; s.ceilingheight = 128; s.lightlevel = 160;
    m.sectors.push_back(s);
    seg_t g; g.linedef = -1; g.side = 0; g.frontsector = 0; g.backsector = -1;
    m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0;
    m.subsectors.push_back(ss);
    return m;
}

// Vertical two-sided line at x=100 between sector 0 (open room) and sector 1.
// `closedDoor` gives sector 1 zero ceiling (sight blocked).
void addCrossLine(MapData& m, bool closedDoor, int backFloor = 0) {
    const int v = static_cast<int>(m.vertices.size());
    m.vertices.push_back({EF(100), EF(-200)});
    m.vertices.push_back({EF(100), EF(200)});
    line_t L; L.v1 = v; L.v2 = v + 1; L.flags = ML_TWOSIDED;
    L.sidenum[0] = 0; L.sidenum[1] = 1;
    m.lines.push_back(L);
    side_t s0; s0.sector = 0; m.sides.push_back(s0);
    side_t s1; s1.sector = 1; m.sides.push_back(s1);
    sector_t back; back.floorheight = backFloor;
    back.ceilingheight = closedDoor ? backFloor : 128;
    m.sectors.push_back(back);
}

void addPlayer(World& w, MapData& m, int x = 100) {
    R_InitTables();
    m.things.push_back({x, 0, 0, 1});   // player 1 start
    P_SetupLevel(w, m);
}

// A 4x4 blockmap whose every cell lists linedef 0 (used by the dropoff test).
Blockmap allCellsLine0() {
    Blockmap bm;
    bm.orgx = 0; bm.orgy = 0; bm.width = 4; bm.height = 4;
    bm.lump.assign(4, 0);
    bm.lump[0] = 0; bm.lump[1] = 0; bm.lump[2] = 4; bm.lump[3] = 4;
    for (int i = 0; i < 16; ++i) bm.lump.push_back(20);   // every cell -> list at 20
    bm.lump.push_back(0);    // line 0
    bm.lump.push_back(-1);
    return bm;
}
}  // namespace

TEST_CASE("P6b tables: new monsters map from Freedoom doomednums") {
    CHECK(mobjtypeForDoomednum(3006) == MT_SKULL);
    CHECK(mobjtypeForDoomednum(3005) == MT_HEAD);
    CHECK(mobjtypeForDoomednum(3003) == MT_BRUISER);
    CHECK(mobjtypeForDoomednum(16)   == MT_CYBORG);
    CHECK(mobjtypeForDoomednum(7)    == MT_SPIDER);
    CHECK(mobjtypeForDoomednum(58)   == MT_SHADOWS);
    CHECK(mobjtypeForDoomednum(3004) == MT_POSSESSED);   // unchanged
    CHECK(mobjtypeForDoomednum(3002) == MT_SERGEANT);
}

TEST_CASE("P6b tables: every monster state chain is live and uses a known sprite") {
    const mobjtype_t mon[] = { MT_SKULL, MT_HEAD, MT_BRUISER, MT_CYBORG,
                               MT_SPIDER, MT_SHADOWS, MT_POSSESSED, MT_SHOTGUY,
                               MT_TROOP, MT_SERGEANT };
    for (mobjtype_t t : mon) {
        const mobjinfo_t& mi = mobjinfo[t];
        CHECK(mi.spawnstate != S_NULL);
        CHECK(mi.seestate   != S_NULL);
        CHECK(mi.deathstate != S_NULL);
        CHECK(mi.spawnhealth > 0);
        for (statenum_t st : { mi.spawnstate, mi.seestate, mi.deathstate }) {
            CHECK(static_cast<int>(st) < NUMSTATES);
            CHECK(states[st].sprite >= 0);
            CHECK(states[st].sprite < NUMSPRITES);
            CHECK(states[st].nextstate < NUMSTATES);
        }
    }
}

TEST_CASE("P6b tables: monster projectiles animate to S_NULL") {
    const mobjtype_t shots[] = { MT_TROOPSHOT, MT_HEADSHOT, MT_BRUISERSHOT };
    for (mobjtype_t t : shots) {
        const mobjinfo_t& mi = mobjinfo[t];
        CHECK((mi.flags & MF_MISSILE) != 0);
        CHECK(mi.deathstate != S_NULL);
        CHECK(mi.damage > 0);
        // Walk the death chain; it must terminate.
        statenum_t st = mi.deathstate;
        int guard = 0;
        while (st != S_NULL && guard++ < 32) st = states[st].nextstate;
        CHECK(st == S_NULL);
    }
}

TEST_CASE("P_CheckSight: open two-sided line passes, one-sided wall blocks") {
    MapData m = openMap();
    addCrossLine(m, false);
    World w; w.map = &m;
    mobj_t a; a.info = &mobjinfo[MT_POSSESSED]; a.x = EF(50); a.y = 0; a.z = 0;
    a.height = a.info->height;
    mobj_t b; b.info = &mobjinfo[MT_PLAYER]; b.x = EF(200); b.y = 0; b.z = 0;
    b.height = b.info->height;
    CHECK(P_CheckSight(w, a, b));

    MapData solid = openMap();
    const int v = static_cast<int>(solid.vertices.size());
    solid.vertices.push_back({EF(100), EF(-200)});
    solid.vertices.push_back({EF(100), EF(200)});
    line_t L; L.v1 = v; L.v2 = v + 1; L.flags = 0; L.sidenum[0] = 0; L.sidenum[1] = -1;
    solid.lines.push_back(L);
    side_t s0; s0.sector = 0; solid.sides.push_back(s0);
    World ws; ws.map = &solid;
    CHECK(!P_CheckSight(ws, a, b));
}

TEST_CASE("P_CheckSight: a closed door blocks sight") {
    MapData m = openMap();
    addCrossLine(m, true);
    World w; w.map = &m;
    mobj_t a; a.info = &mobjinfo[MT_POSSESSED]; a.x = EF(50); a.z = 0;
    a.height = a.info->height;
    mobj_t b; b.info = &mobjinfo[MT_PLAYER]; b.x = EF(200); b.z = 0;
    b.height = b.info->height;
    CHECK(!P_CheckSight(w, a, b));
}

TEST_CASE("A_Look wakes a monster that can see the player") {
    MapData m = openMap();
    addCrossLine(m, false);
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m, 200);
    mobj_t* mon = P_SpawnMobj(w, EF(50), 0, 0, MT_POSSESSED);
    mon->angle = 0;                        // facing east toward the player
    REQUIRE(mon->target == nullptr);
    P_SetMobjState(w, *mon, S_POSS_STND2); // state-entry action A_Look
    CHECK(mon->target == w.player.mo);
    CHECK(mon->state == &states[S_POSS_RUN1]);
}

TEST_CASE("A_Look does not wake a monster facing away or behind a closed door") {
    {
        MapData m = openMap();
        addCrossLine(m, false);
        World w; w.map = &m; w.bm = &m.blockmap;
        addPlayer(w, m, 200);
        mobj_t* mon = P_SpawnMobj(w, EF(50), 0, 0, MT_POSSESSED);
        mon->angle = ANG180;               // facing west, player is east
        P_SetMobjState(w, *mon, S_POSS_STND2);
        CHECK(mon->target == nullptr);
    }
    {
        MapData m = openMap();
        addCrossLine(m, true);
        World w; w.map = &m; w.bm = &m.blockmap;
        addPlayer(w, m, 200);
        mobj_t* mon = P_SpawnMobj(w, EF(50), 0, 0, MT_POSSESSED);
        mon->angle = 0;
        P_SetMobjState(w, *mon, S_POSS_STND2);
        CHECK(mon->target == nullptr);
    }
}

TEST_CASE("A_Chase moves a monster toward a distant target") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m, 3000);
    mobj_t* mon = P_SpawnMobj(w, 0, 0, 0, MT_POSSESSED);
    mon->target = w.player.mo;
    mon->flags &= ~MF_AMBUSH;
    mon->reactiontime = 0;
    const fixed_t x0 = mon->x;
    P_SetMobjState(w, *mon, S_POSS_RUN1);  // first A_Chase runs on state entry
    for (int i = 0; i < 20; ++i) P_RunThinkers(w);
    CHECK(mon->x > x0);                    // walked east toward the player
}

TEST_CASE("A_PosAttack damages the player over repeated shots") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m, 100);
    mobj_t* mon = P_SpawnMobj(w, 0, 0, 0, MT_POSSESSED);
    mon->angle = 0;
    mon->target = w.player.mo;
    w.player.health = 1000;
    w.player.mo->health = 1000;
    for (int i = 0; i < 30; ++i) {
        P_SetMobjState(w, *mon, S_POSS_ATK2);   // action A_PosAttack
        P_SetMobjState(w, *mon, S_POSS_RUN1);
    }
    CHECK(w.player.health < 1000);
    CHECK(w.player.mo->health == w.player.health);
}

TEST_CASE("P_SpawnMissile spawns an imp fireball aimed at the player") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m, 300);
    mobj_t* mon = P_SpawnMobj(w, 0, 0, 0, MT_TROOP);
    mobj_t* shot = P_SpawnMissile(w, *mon, *w.player.mo, MT_TROOPSHOT);
    REQUIRE(shot != nullptr);
    CHECK(shot->info == &mobjinfo[MT_TROOPSHOT]);
    CHECK(shot->target == mon);
    CHECK(shot->momx > 0);
    const fixed_t x0 = shot->x;
    for (int i = 0; i < 10; ++i) P_RunThinkers(w);
    CHECK(shot->x > x0);
}

TEST_CASE("P_NoiseAlert wakes a monster facing away but in line of sight") {
    MapData m = openMap();
    addCrossLine(m, false);
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m, 200);
    mobj_t* mon = P_SpawnMobj(w, EF(50), 0, 0, MT_POSSESSED);
    mon->angle = ANG180;                   // looking away from the player
    REQUIRE(mon->target == nullptr);
    P_NoiseAlert(w, w.player.mo);
    CHECK(mon->target == w.player.mo);
}

TEST_CASE("Monster infighting: damage retargets the monster at its attacker") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m, 500);
    mobj_t* a = P_SpawnMobj(w, 0, 0, 0, MT_TROOP);
    mobj_t* b = P_SpawnMobj(w, EF(32), 0, 0, MT_POSSESSED);
    P_DamageMobj(w, *a, b, 5);
    CHECK(a->target == b);
}

TEST_CASE("Monsters without MF_DROPOFF refuse to step off a high ledge") {
    MapData m = openMap();
    addCrossLine(m, false, -64);           // back sector floor 64 units lower
    m.blockmap = allCellsLine0();
    World w; w.map = &m; w.bm = &m.blockmap;
    mobj_t* mon = P_SpawnMobj(w, EF(50), 0, 0, MT_POSSESSED);   // radius 20
    mon->floorz = 0;
    mon->z = 0;
    CHECK(!P_TryMove(w, *mon, EF(110), 0));   // bbox straddles x=100; dropoff > 24

    // The player carries MF_DROPOFF and is exempt.
    mobj_t* pl = P_SpawnMobj(w, EF(50), 0, 0, MT_PLAYER);
    pl->floorz = 0;
    CHECK(P_TryMove(w, *pl, EF(110), 0));
}

TEST_CASE("A_BossDeath lowers tag-666 sectors when the last boss dies") {
    MapData m = openMap();
    sector_t s2; s2.floorheight = -64; s2.ceilingheight = 128; s2.tag = 666;
    m.sectors.push_back(s2);
    World w; w.map = &m;
    mobj_t* boss = P_SpawnMobj(w, 0, 0, 0, MT_BRUISER);
    // The only boss in the world: death releases the sector.
    P_MobjAction(w, *boss, A_BossDeath);
    REQUIRE(!w.movers.empty());
    CHECK(w.movers[0].sector == 1);
}

TEST_CASE("A dead monster is never a chase target (A_Chase re-acquires)") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m, 300);
    mobj_t* mon = P_SpawnMobj(w, 0, 0, 0, MT_POSSESSED);
    mobj_t* dead = P_SpawnMobj(w, EF(40), 0, 0, MT_POSSESSED);
    dead->health = 0;
    dead->flags &= ~MF_SHOOTABLE;
    mon->target = dead;
    mon->angle = 0;
    mon->reactiontime = 0;
    P_SetMobjState(w, *mon, S_POSS_RUN1);  // A_Chase drops the dead target, looks around
    CHECK(mon->target == w.player.mo);
}

TEST_CASE("A_BossDeath: E2M8/E3M8 end the level; other bosses must all be dead") {
    MapData m = openMap();
    World w; w.map = &m; w.mapName = "E2M8";
    mobj_t* boss = P_SpawnMobj(w, 0, 0, 0, MT_CYBORG);
    mobj_t* other = P_SpawnMobj(w, EF(200), 0, 0, MT_CYBORG);
    P_MobjAction(w, *boss, A_BossDeath);
    CHECK(!w.levelExitRequested);          // another cyberdemon is alive
    P_MobjAction(w, *other, A_BossDeath);
    CHECK(w.levelExitRequested);
    CHECK(!w.secretExit);

    // E1M8-style boss death instead lowers the tag-666 floor.
    MapData m2 = openMap();
    sector_t s2; s2.floorheight = -64; s2.ceilingheight = 128; s2.tag = 666;
    m2.sectors.push_back(s2);
    World w2; w2.map = &m2; w2.mapName = "E1M8";
    mobj_t* baron = P_SpawnMobj(w2, 0, 0, 0, MT_BRUISER);
    P_MobjAction(w2, *baron, A_BossDeath);
    CHECK(!w2.levelExitRequested);
    CHECK(!w2.movers.empty());
}

TEST_CASE("A_Chase-driven possessed actually damages the player over time") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m, 120);
    w.player.health = 500;
    w.player.mo->health = 500;
    mobj_t* mon = P_SpawnMobj(w, 0, 0, 0, MT_POSSESSED);
    mon->angle = 0;
    mon->target = w.player.mo;
    mon->reactiontime = 0;
    mon->health = 100000;               // keep it alive for the whole test
    P_SetMobjState(w, *mon, S_POSS_RUN1);
    for (int i = 0; i < 200; ++i) P_RunThinkers(w);
    CHECK(w.player.health < 500);
}

TEST_CASE("P_MovePlayerWorld: thrust accelerates, friction stops, bob does not NaN") {
    MapData m = openMap();
    R_InitTables();
    World w; w.map = &m; w.bm = &m.blockmap;
    mobj_t mo; mo.info = &mobjinfo[MT_PLAYER];
    mo.flags = mobjinfo[MT_PLAYER].flags;
    mo.floorz = 0; mo.ceilingz = 128 << FRACBITS; mo.subsector = 0;
    w.player.mo = &mo;
    const fixed_t x0 = mo.x;
    for (int i = 0; i < 30; ++i)
        P_MovePlayerWorld(w, w.player, 25 * 2048, 0, 0);   // walk thrust
    CHECK(mo.x > x0);
    CHECK(mo.momx > 0);
    for (int i = 0; i < 150; ++i)
        P_MovePlayerWorld(w, w.player, 0, 0, 0);           // release: friction stops
    CHECK(mo.momx == 0);
    const fixed_t x1 = mo.x;
    for (int i = 0; i < 10; ++i)
        P_MovePlayerWorld(w, w.player, 0, 0, 0);
    CHECK(mo.x == x1);
    CHECK(w.player.viewz > mo.floorz);                     // eye above floor, sane
}

TEST_CASE("P_MobjThinker does not re-integrate player momentum (no double speed)") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m, 100);
    mobj_t* pm = w.player.mo;
    P_MovePlayerWorld(w, w.player, 25 * 2048, 0, 0);
    CHECK(pm->momx > 0);
    const fixed_t x1 = pm->x;
    P_RunThinkers(w);
    CHECK(pm->x == x1);          // thinker pass leaves player movement to P_MovePlayerWorld
    CHECK(pm->momx > 0);
}

TEST_CASE("A projectile stops and explodes at a wall (does not slide)") {
    MapData m = openMap();
    const int v = static_cast<int>(m.vertices.size());
    m.vertices.push_back({EF(100), EF(-200)});
    m.vertices.push_back({EF(100), EF(200)});
    line_t L; L.v1 = v; L.v2 = v + 1; L.flags = 0;       // one-sided wall
    L.sidenum[0] = 0; L.sidenum[1] = -1;
    m.lines.push_back(L);
    side_t s0; s0.sector = 0; m.sides.push_back(s0);
    m.blockmap = allCellsLine0();
    World w; w.map = &m; w.bm = &m.blockmap;
    mobj_t* rocket = P_SpawnMobj(w, EF(50), 0, EF(10), MT_ROCKET);
    rocket->momx = 20 << FRACBITS;
    rocket->floorz = 0; rocket->ceilingz = 128 << FRACBITS;
    for (int i = 0; i < 10; ++i) P_RunThinkers(w);
    CHECK(rocket->x < EF(100));                          // never crossed the wall
    CHECK((rocket->flags & MF_MISSILE) == 0);            // exploded on contact
}

TEST_CASE("A blocked monster opens the door in front of it") {
    R_InitTables();
    MapData m;
    m.vertices.push_back({0, EF(-200)});
    m.vertices.push_back({0, EF(200)});
    line_t L; L.v1 = 0; L.v2 = 1; L.flags = ML_TWOSIDED; L.special = 1; L.tag = 0;
    L.sidenum[0] = 0; L.sidenum[1] = 1;
    m.lines.push_back(L);
    // v1->v2 points north, so side 0 is geometric EAST: put the door there.
    side_t s0; s0.sector = 1; m.sides.push_back(s0);   // door
    side_t s1; s1.sector = 0; m.sides.push_back(s1);   // room
    sector_t room; room.floorheight = 0; room.ceilingheight = 128; m.sectors.push_back(room);
    sector_t door; door.floorheight = 0; door.ceilingheight = 0;   m.sectors.push_back(door);
    seg_t g; g.linedef = 0; g.side = 0; g.frontsector = 1; g.backsector = 0;
    m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0; m.subsectors.push_back(ss);

    World w; w.map = &m; w.bm = &m.blockmap;
    mobj_t* mon = P_SpawnMobj(w, EF(-40), 0, 0, MT_POSSESSED);
    mon->angle = 0;                           // facing the door
    P_UseLines(w, *mon);
    REQUIRE(!w.movers.empty());
    CHECK(w.movers[0].sector == 1);           // the door, not the room
    for (int i = 0; i < 70; ++i) P_RunMovers(w);
    CHECK(m.sectors[1].ceilingheight > 0);
}
