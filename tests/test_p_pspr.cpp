#include "doctest.h"
#include "play/p_pspr.h"
#include "play/p_tick.h"
#include "play/p_mobj.h"
#include "play/p_map.h"
#include "play/p_inter.h"
#include "play/info.h"
#include "core/m_tables.h"
#include "core/m_random.h"   // M_ClearRandom (RNG-sensitive damage tests)

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

// ---- P8b: the items the campaign actually places ----

namespace {
// Touch an item of `type` dropped on the player and report whether it was consumed.
bool grab(World& w, mobjtype_t type) {
    size_t n0 = w.mobjs.size();
    P_SpawnMobj(w, 0, 0, 0, type);
    P_TouchSpecialThings(w, *w.player.mo);
    return w.mobjs.size() == n0;                 // spawn then removal -> back to n0
}
}  // namespace

TEST_CASE("Pickup: yellow and red keys set their key bits") {
    MapData m = openMap(); World w; w.map = &m; w.bm = &m.blockmap; setupPlayer(w, m);
    CHECK(grab(w, MT_D6));
    CHECK((w.player.keys & KEY_YELLOW) != 0);
    CHECK(grab(w, MT_D13));
    CHECK((w.player.keys & KEY_RED) != 0);
}

TEST_CASE("Pickup: blue armor is the heavy armour (armortype 2, 200 points)") {
    MapData m = openMap(); World w; w.map = &m; w.bm = &m.blockmap; setupPlayer(w, m);
    CHECK(grab(w, MT_D2019));
    CHECK(w.player.armortype == 2);
    CHECK(w.player.armorpoints[1] == 200);
}

TEST_CASE("Pickup: backpack doubles the ammo caps") {
    MapData m = openMap(); World w; w.map = &m; w.bm = &m.blockmap; setupPlayer(w, m);
    const int before = w.player.maxammo[AM_SHELL];
    CHECK(grab(w, MT_D8));
    CHECK(w.player.maxammo[AM_SHELL] == before * 2);
    CHECK(w.player.ammo[AM_SHELL] >= before);      // carried shells move with it
}

TEST_CASE("Pickup: BFG grants the weapon, cell pack tops up cells") {
    MapData m = openMap(); World w; w.map = &m; w.bm = &m.blockmap; setupPlayer(w, m);
    CHECK(grab(w, MT_D2006));
    CHECK((w.player.weapons & (1u << WP_BFG)) != 0);
    CHECK(grab(w, MT_D2047));
    CHECK(w.player.ammo[AM_CELL] >= 300);
}

TEST_CASE("Pickup: power-ups arm their timers") {
    MapData m = openMap(); World w; w.map = &m; w.bm = &m.blockmap; setupPlayer(w, m);
    CHECK(grab(w, MT_D2022)); CHECK(w.player.powers[pw_invulnerability] > 0);
    CHECK(grab(w, MT_D2024)); CHECK(w.player.powers[pw_invisibility] > 0);
    CHECK(grab(w, MT_D2025)); CHECK(w.player.powers[pw_ironfeet] > 0);
    CHECK(grab(w, MT_D2026)); CHECK(w.player.powers[pw_allmap] != 0);
    CHECK(grab(w, MT_D2023));                        // berserk
    CHECK(w.player.powers[pw_strength] != 0);
    CHECK(w.player.health == 200);
    CHECK(w.player.readyweapon == WP_FIST);          // vanilla forces the fist
}

TEST_CASE("Powers tick down; invulnerability shrugs off damage") {
    MapData m = openMap(); World w; w.map = &m; w.bm = &m.blockmap; setupPlayer(w, m);
    w.player.powers[pw_invisibility] = 10;
    w.player.powers[pw_strength] = 1;
    for (int i = 0; i < 12; ++i) tick(w, false);
    CHECK(w.player.powers[pw_invisibility] == 0);    // timed power expired
    CHECK(w.player.powers[pw_strength] == 1);        // permanent ones do not decay
    w.player.powers[pw_invulnerability] = 100;
    const int hp = w.player.health;
    mobj_t* victim = w.player.mo;
    P_DamageMobj(w, *victim, nullptr, 50);
    CHECK(w.player.health == hp);
}

TEST_CASE("Berserk makes the fist hurt ten times as much") {
    // The roll comes from the shared M_Random stream, so reset it: a plain punch lands
    // 2..20, a berserk one is that same roll times ten (always a multiple of 20).
    M_ClearRandom();
    MapData m = openMap(); World w; w.map = &m; w.bm = &m.blockmap; setupPlayer(w, m);
    mobj_t* target = P_SpawnMobj(w, RF(40), 0, 0, MT_BARREL);   // in melee reach
    target->health = 100000;
    w.player.readyweapon = WP_FIST;
    w.player.pendingweapon = NUMWEAPONS;
    P_SetPsprite(w, w.player, 0, S_PUNCH);
    int hp0 = target->health;
    for (int i = 0; i < 200 && hp0 - target->health == 0; ++i) tick(w, true);
    const int plain = hp0 - target->health;
    REQUIRE(plain > 0);
    REQUIRE(plain <= 20);                            // an unboosted punch
    M_ClearRandom();
    target->health = hp0;
    w.player.powers[pw_strength] = 1;
    P_SetPsprite(w, w.player, 0, S_PUNCH);
    for (int i = 0; i < 200 && hp0 - target->health == 0; ++i) tick(w, true);
    const int boosted = hp0 - target->health;
    CHECK(boosted >= 20);                            // 10x the same roll
    CHECK(boosted % 20 == 0);
}

// ---- P8b: hazard floors and sector light specials ----

namespace {
// One sector whose floor is `flat`, optionally with a sector special, player standing in it.
MapData floorMap(const char* flat, int sectorSpecial) {
    MapData m;
    sector_t s; s.floorheight = 0; s.ceilingheight = 128; s.lightlevel = 160;
    s.special = sectorSpecial;
    std::snprintf(s.floorpic, sizeof(s.floorpic), "%s", flat);
    std::snprintf(s.ceilingpic, sizeof(s.ceilingpic), "FLAT1");
    m.sectors.push_back(s);
    seg_t g; g.linedef = -1; g.side = 0; g.frontsector = 0; g.backsector = -1;
    m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0;
    m.subsectors.push_back(ss);
    return m;
}
}  // namespace

TEST_CASE("Standing in toxic goo hurts; a radiation suit delays it") {
    MapData m = floorMap("NUKAGE1", 0);
    World w; w.map = &m; w.bm = &m.blockmap; setupPlayer(w, m);
    int hp0 = w.player.health;
    for (int i = 0; i < 70; ++i) tick(w, false);
    CHECK(w.player.health < hp0);                     // took poison damage

    MapData m2 = floorMap("NUKAGE1", 0);
    World w2; w2.map = &m2; w2.bm = &m2.blockmap; setupPlayer(w2, m2);
    w2.player.powers[pw_ironfeet] = 60 * 35;
    const int hp = w2.player.health;
    for (int i = 0; i < 70; ++i) tick(w2, false);
    CHECK(w2.player.health == hp);                    // suited up: nothing yet
}

TEST_CASE("Lava burns faster than goo, and dry floors never hurt") {
    MapData m = floorMap("LAVA1", 0);
    World w; w.map = &m; w.bm = &m.blockmap; setupPlayer(w, m);
    const int hp0 = w.player.health;
    for (int i = 0; i < 40; ++i) tick(w, false);
    CHECK(hp0 - w.player.health >= 8);

    MapData dry = floorMap("FLAT1_1", 0);
    World w2; w2.map = &dry; w2.bm = &dry.blockmap; setupPlayer(w2, dry);
    const int h = w2.player.health;
    for (int i = 0; i < 200; ++i) tick(w2, false);
    CHECK(w2.player.health == h);
}

TEST_CASE("Sector light specials animate; ordinary sectors do not") {
    MapData m = floorMap("FLAT1_1", 13);            // 13 = light pacing (glow)
    World w; w.map = &m; w.bm = &m.blockmap; setupPlayer(w, m);
    const int base = m.sectors[0].lightlevel;
    bool changed = false;
    for (int i = 0; i < 200; ++i) { tick(w, false); if (m.sectors[0].lightlevel != base) changed = true; }
    CHECK(changed);

    MapData still = floorMap("FLAT1_1", 0);
    World w2; w2.map = &still; w2.bm = &still.blockmap; setupPlayer(w2, still);
    for (int i = 0; i < 200; ++i) tick(w2, false);
    CHECK(still.sectors[0].lightlevel == 160);        // untouched
}
