#include "doctest.h"
#include "play/p_map.h"
#include "play/p_tick.h"
#include "play/p_mobj.h"
#include "play/p_inter.h"
#include "play/p_pspr.h"
#include "play/info.h"
#include "core/m_tables.h"   // R_InitTables (tests self-init, as in every trig-touching test)
#include <string>

namespace {
fixed_t AF(int v) { return static_cast<fixed_t>(v) << FRACBITS; }

// One open sector, no lines. subsector 0 resolves to sector 0.
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

// Adds a vertical wall at x=`x` from -1000..1000; twoSided=false -> solid, true -> open.
void addWall(MapData& m, int x, bool twoSided) {
    int base = static_cast<int>(m.vertices.size());
    m.vertices.push_back({AF(x), AF(-1000)});
    m.vertices.push_back({AF(x), AF(1000)});
    line_t L; L.v1 = base; L.v2 = base + 1; L.flags = twoSided ? ML_TWOSIDED : 0;
    L.special = 0; L.tag = 0;
    side_t s0; s0.sector = 0; m.sides.push_back(s0);
    L.sidenum[0] = static_cast<int>(m.sides.size()) - 1;
    if (twoSided) {
        side_t s1; s1.sector = 0; m.sides.push_back(s1);
        L.sidenum[1] = static_cast<int>(m.sides.size()) - 1;
    } else {
        L.sidenum[1] = -1;
    }
    m.lines.push_back(L);
}

void addPlayer(World& w, MapData& m) {
    R_InitTables();   // trig tables for angle->direction (main does this at startup)
    m.things.push_back({0, 0, 0, 1});
    P_SetupLevel(w, m);
}
}  // namespace

TEST_CASE("P_BlockThingsIterator visits only mobjs whose radius-extended bbox overlaps") {
    World w;
    mobj_t* a = P_SpawnMobj(w, AF(100), AF(100), 0, MT_BARREL);   // radius 10
    mobj_t* b = P_SpawnMobj(w, AF(500), AF(500), 0, MT_BARREL);
    int seen = 0; bool sawB = false;
    P_BlockThingsIterator(w, AF(90), AF(90), AF(120), AF(120), [&](mobj_t& m) {
        ++seen; if (&m == b) sawB = true; return false;
    });
    CHECK(seen == 1);
    CHECK(!sawB);
    (void)a;
}

TEST_CASE("P_BlockThingsIterator: early stop and pending-removal skip") {
    World w;
    P_SpawnMobj(w, AF(100), AF(100), 0, MT_BARREL);
    mobj_t* b = P_SpawnMobj(w, AF(110), AF(110), 0, MT_BARREL);
    int seen = 0;
    P_BlockThingsIterator(w, AF(0), AF(0), AF(300), AF(300), [&](mobj_t&) {
        ++seen; return true;                     // stop after the first
    });
    CHECK(seen == 1);

    b->removePending = true;
    int seen2 = 0;
    P_BlockThingsIterator(w, AF(0), AF(0), AF(300), AF(300), [&](mobj_t& m) {
        ++seen2; CHECK(&m != b); return false;
    });
    CHECK(seen2 == 1);
}

// ---- P_LineAttack / damage ----

TEST_CASE("P_LineAttack hits the nearest shootable thing and applies damage") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m);
    mobj_t* target = P_SpawnMobj(w, AF(200), 0, 0, MT_POSSESSED);
    int hp0 = target->health;
    mobj_t* hit = P_LineAttack(w, w.player.mo, 0, 0, AF(41), 0, 0, 5);
    CHECK(hit == target);
    CHECK(target->health == hp0 - 5);
}

TEST_CASE("P_LineAttack: a nearer solid wall blocks the shot") {
    MapData m = openMap();
    addWall(m, 100, false);
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m);
    mobj_t* target = P_SpawnMobj(w, AF(200), 0, 0, MT_POSSESSED);
    int hp0 = target->health;
    CHECK(P_LineAttack(w, w.player.mo, 0, 0, AF(41), 0, 0, 5) == nullptr);
    CHECK(target->health == hp0);
}

TEST_CASE("P_LineAttack: a two-sided opening lets the shot pass") {
    MapData m = openMap();
    addWall(m, 100, true);
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m);
    mobj_t* target = P_SpawnMobj(w, AF(200), 0, 0, MT_POSSESSED);
    CHECK(P_LineAttack(w, w.player.mo, 0, 0, AF(41), 0, 0, 5) == target);
}

TEST_CASE("P_LineAttack: vertical offset outside the target's height misses") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m);
    mobj_t* target = P_SpawnMobj(w, AF(200), 0, AF(100), MT_POSSESSED);   // hovering
    int hp0 = target->health;
    CHECK(P_LineAttack(w, w.player.mo, 0, 0, AF(41), 0, 0, 5) == nullptr);
    CHECK(target->health == hp0);
}

TEST_CASE("P_AimLineAttack finds the target and slopes toward its center") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m);
    mobj_t* target = P_SpawnMobj(w, AF(200), 0, 0, MT_BARREL);   // height 42, center 21
    fixed_t slope = 123;
    mobj_t* aimed = P_AimLineAttack(w, w.player.mo, 0, 0, AF(41), 0, MISSILERANGE, slope);
    CHECK(aimed == target);
    CHECK(slope < 0);   // eye at 41 aims down at the barrel's center (21)
}

TEST_CASE("P_DamageMobj: green/blue armor absorbs damage") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m);
    w.player.armortype = 1;
    w.player.armorpoints[0] = 100;
    P_DamageMobj(w, *w.player.mo, nullptr, 30);
    CHECK(w.player.health == 80);        // 1/3 absorbed -> 20 damage
    CHECK(w.player.armorpoints[0] == 90);

    w.player.armortype = 2;
    w.player.armorpoints[1] = 100;
    P_DamageMobj(w, *w.player.mo, nullptr, 30);
    CHECK(w.player.health == 65);        // 1/2 absorbed -> 15 damage
    CHECK(w.player.armorpoints[1] == 85);
}

TEST_CASE("P_DamageMobj: lethal damage enters the death state and clears SHOOTABLE") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m);
    mobj_t* target = P_SpawnMobj(w, AF(200), 0, 0, MT_POSSESSED);
    target->health = 4;
    P_DamageMobj(w, *target, w.player.mo, 5);
    CHECK(target->health <= 0);
    CHECK(target->state == &states[S_POSS_DIE1]);
    CHECK((target->flags & MF_CORPSE) != 0);
    CHECK((target->flags & MF_SHOOTABLE) == 0);
    CHECK(w.player.killcount == 1);
}

TEST_CASE("P_DamageMobj: barrel death uses the S_BEXP explosion chain") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m);
    mobj_t* b = P_SpawnMobj(w, AF(200), 0, 0, MT_BARREL);
    b->health = 3;
    P_DamageMobj(w, *b, w.player.mo, 5);
    CHECK(b->state == &states[S_BEXP]);
}

TEST_CASE("P_DamageMobj: player death marks player dead and drops the weapon") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m);
    P_DamageMobj(w, *w.player.mo, nullptr, 150);   // below -100 hp would gib; 150 is a normal death
    CHECK(w.player.dead == true);
    CHECK(w.player.health == 0);
    CHECK(w.player.mo->state == &states[S_PLAY_DIE1]);
    CHECK(w.player.psprites[0].state == &states[S_NULL]);
}

TEST_CASE("P_MovePlayer refuses to move a dead player") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m);
    w.player.dead = true;
    w.player.health = 0;
    fixed_t x0 = w.player.mo->x;
    P_MovePlayer(m, m.blockmap, w.player, 5 << FRACBITS, 0, 0);
    CHECK(w.player.mo->x == x0);
}

TEST_CASE("P_DamageMobj ignores non-shootable things") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    addPlayer(w, m);
    mobj_t* deco = P_SpawnMobj(w, AF(200), 0, 0, MT_D2013);   // soul sphere (not SHOOTABLE)
    int hp0 = deco->health;
    P_DamageMobj(w, *deco, w.player.mo, 50);
    CHECK(deco->health == hp0);
}
