#include "doctest.h"
#include "play/p_pspr.h"
#include "play/p_tick.h"
#include "play/p_mobj.h"
#include "play/p_map.h"
#include "play/p_inter.h"
#include "play/info.h"
#include "core/m_tables.h"

namespace {
fixed_t RF(int v) { return static_cast<fixed_t>(v) << FRACBITS; }

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

void setupPlayer(World& w, MapData& m) {
    R_InitTables();
    m.things.push_back({0, 0, 0, 1});
    P_SetupLevel(w, m);
}

void tick(World& w, bool attack, int weaponSwitch = -1) {
    P_PlayerThink(w, attack, weaponSwitch);
    P_RunThinkers(w);
}
}  // namespace

TEST_CASE("Pistol fires after its wind-up and consumes one bullet") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    setupPlayer(w, m);
    CHECK(w.player.ammo[AM_CLIP] == 50);
    for (int i = 0; i < 15; ++i) tick(w, true);
    CHECK(w.player.ammo[AM_CLIP] == 49);
    CHECK(w.player.psprites[0].state != nullptr);
}

TEST_CASE("Empty pistol auto-switches to the fist (no ammo used)") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    setupPlayer(w, m);
    w.player.ammo[AM_CLIP] = 0;
    for (int i = 0; i < 20; ++i) tick(w, true);
    CHECK(w.player.readyweapon == WP_FIST);
    CHECK(w.player.ammo[AM_CLIP] == 0);
}

TEST_CASE("Weapon switch request raises the requested weapon") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    setupPlayer(w, m);
    w.player.weapons |= (1u << WP_SHOTGUN);
    w.player.ammo[AM_SHELL] = 8;
    tick(w, false, WP_SHOTGUN);
    for (int i = 0; i < 5; ++i) tick(w, false);
    CHECK(w.player.readyweapon == WP_SHOTGUN);
    CHECK(w.player.psprites[0].state == &states[S_SGUN]);
}

TEST_CASE("Shotgun consumes a shell and the pellet spread damages a target") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    setupPlayer(w, m);
    w.player.weapons |= (1u << WP_SHOTGUN);
    w.player.ammo[AM_SHELL] = 8;
    w.player.readyweapon = WP_SHOTGUN;
    P_SetPsprite(w, w.player, 0, S_SGUN);
    mobj_t* target = P_SpawnMobj(w, RF(160), 0, 0, MT_POSSESSED);
    for (int i = 0; i < 15; ++i) tick(w, true);
    CHECK(w.player.ammo[AM_SHELL] == 7);
    CHECK(target->health < 20);          // at least one pellet connected
}

TEST_CASE("P_SpawnPlayerMissile spawns an aimed rocket owned by the player") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    setupPlayer(w, m);
    mobj_t* r = P_SpawnPlayerMissile(w, w.player, MT_ROCKET);
    REQUIRE(r != nullptr);
    CHECK(r->info == &mobjinfo[MT_ROCKET]);
    CHECK(r->target == w.player.mo);
    CHECK(r->momx > 0);                  // facing east
    CHECK((r->flags & MF_MISSILE) != 0);
}

TEST_CASE("Rocket flight + impact kills a target and explodes") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    setupPlayer(w, m);
    mobj_t* target = P_SpawnMobj(w, RF(200), 0, 0, MT_POSSESSED);
    mobj_t* r = P_SpawnPlayerMissile(w, w.player, MT_ROCKET);
    REQUIRE(r != nullptr);
    for (int i = 0; i < 30; ++i) P_RunThinkers(w);
    CHECK(target->health <= 0);
}

TEST_CASE("P_RadiusAttack falls off with distance") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    mobj_t* bomb = P_SpawnMobj(w, 0, 0, 0, MT_PUFF);
    mobj_t* near = P_SpawnMobj(w, RF(20), 0, 0, MT_POSSESSED);
    mobj_t* far  = P_SpawnMobj(w, RF(400), 0, 0, MT_POSSESSED);
    P_RadiusAttack(w, *bomb, 50);
    CHECK(near->health < 20);
    CHECK(far->health == 20);
}

TEST_CASE("Pickup: clip adds bullets and is removed") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    setupPlayer(w, m);
    mobj_t* clip = P_SpawnMobj(w, 0, 0, 0, MT_D2007);   // 10 bullets
    size_t n0 = w.mobjs.size();
    P_TouchSpecialThings(w, *w.player.mo);
    CHECK(w.player.ammo[AM_CLIP] == 60);
    CHECK(w.mobjs.size() == n0 - 1);
    CHECK(clip->removePending == false);                 // erased immediately outside a pass
}

TEST_CASE("Pickup: full ammo leaves the item in the world") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    setupPlayer(w, m);
    w.player.ammo[AM_CLIP] = w.player.maxammo[AM_CLIP];
    size_t n0 = w.mobjs.size();
    P_SpawnMobj(w, 0, 0, 0, MT_D2007);
    P_TouchSpecialThings(w, *w.player.mo);
    CHECK(w.mobjs.size() == n0 + 1);                     // item still there
}

TEST_CASE("Pickup: stimpack and green armor update the player") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    setupPlayer(w, m);
    w.player.health = 90;
    P_SpawnMobj(w, 0, 0, 0, MT_D2011);                   // stimpack +10
    P_TouchSpecialThings(w, *w.player.mo);
    CHECK(w.player.health == 100);
    CHECK(w.player.mo->health == 100);                   // mobj health mirrored (damage path)
    P_SpawnMobj(w, 0, 0, 0, MT_D2018);                   // green armor
    P_TouchSpecialThings(w, *w.player.mo);
    CHECK(w.player.armorpoints[0] == 100);
    CHECK(w.player.armortype == 1);
}

TEST_CASE("Pickup: shotgun grants the weapon and shells; key sets the key bit") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    setupPlayer(w, m);
    P_SpawnMobj(w, 0, 0, 0, MT_D2001);
    P_TouchSpecialThings(w, *w.player.mo);
    CHECK((w.player.weapons & (1u << WP_SHOTGUN)) != 0);
    CHECK(w.player.ammo[AM_SHELL] == 8);
    P_SpawnMobj(w, 0, 0, 0, MT_D5);                      // blue key
    P_TouchSpecialThings(w, *w.player.mo);
    CHECK((w.player.keys & KEY_BLUE) != 0);
}

TEST_CASE("Barrel explosion (S_BEXP A_Explode) damages nearby things") {
    MapData m = openMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    setupPlayer(w, m);
    mobj_t* barrel = P_SpawnMobj(w, RF(100), 0, 0, MT_BARREL);
    mobj_t* victim = P_SpawnMobj(w, RF(160), 0, 0, MT_POSSESSED);
    barrel->health = 1;
    P_DamageMobj(w, *barrel, w.player.mo, 5);   // enters S_BEXP chain
    for (int i = 0; i < 25; ++i) P_RunThinkers(w);
    CHECK(victim->health < 20);
}
