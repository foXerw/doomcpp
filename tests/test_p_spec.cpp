#include "doctest.h"
#include "play/p_spec.h"
#include "play/p_map.h"
#include "play/p_setup.h"
#include "play/p_mobj.h"
#include "play/info.h"
#include "core/m_tables.h"
#include <cstring>
#include <string>

namespace {
fixed_t SF(int v) { return static_cast<fixed_t>(v) << FRACBITS; }

// Sector 0 (room, floor 0 / ceil 128) | vertical line x=0 | sector 1 (door, ceil 0).
// The line carries `special`; the door sector is the line's back sector.
MapData doorMap(int special, int tag = 0) {
    MapData m;
    m.vertices.push_back({0, SF(-1000)});
    m.vertices.push_back({0, SF(1000)});
    line_t L; L.v1 = 0; L.v2 = 1; L.flags = ML_TWOSIDED;
    L.special = special; L.tag = tag; L.sidenum[0] = 0; L.sidenum[1] = 1;
    m.lines.push_back(L);
    side_t s0; s0.sector = 0; m.sides.push_back(s0);
    side_t s1; s1.sector = 1; m.sides.push_back(s1);
    sector_t A; A.floorheight = 0; A.ceilingheight = 128; m.sectors.push_back(A);
    sector_t B; B.floorheight = 0; B.ceilingheight = 0;   m.sectors.push_back(B);
    seg_t g; g.linedef = 0; g.side = 0; g.frontsector = 0; g.backsector = 1;
    m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0; m.subsectors.push_back(ss);
    return m;
}

void runTics(World& w, int n) { for (int i = 0; i < n; ++i) P_RunMovers(w); }
}

TEST_CASE("Door special 1 opens the back sector to neighborCeil-4, waits, closes") {
    MapData m = doorMap(1);
    World w; w.map = &m;
    mobj_t player; player.info = &mobjinfo[MT_PLAYER]; player.x = 100 << 16;   // room side (east)
    w.player.mo = &player;
    P_ActivateLine(w, m.lines[0], &player);
    REQUIRE(w.movers.size() == 1);
    runTics(w, 70);
    CHECK(m.sectors[1].ceilingheight == 124);       // 128 - 4
    runTics(w, 220);                                 // wait 150 + close
    CHECK(m.sectors[1].ceilingheight == 0);
    CHECK(w.movers[0].state == 3);
}

TEST_CASE("Manual door picks the side opposite the player (door can be either sidedef)") {
    // Same geometry but the door sector is on side 0 (east/front); the player stands west
    // (room side), so the door is the front sector 0, not the back sector 1.
    MapData m;
    m.vertices.push_back({0, SF(-1000)});
    m.vertices.push_back({0, SF(1000)});
    line_t L; L.v1 = 0; L.v2 = 1; L.flags = ML_TWOSIDED; L.special = 1; L.tag = 0;
    L.sidenum[0] = 0; L.sidenum[1] = 1; m.lines.push_back(L);
    side_t s0; s0.sector = 0; m.sides.push_back(s0);   // east = door
    side_t s1; s1.sector = 1; m.sides.push_back(s1);   // west = room (player)
    sector_t D; D.floorheight = 0; D.ceilingheight = 0;   m.sectors.push_back(D);
    sector_t R; R.floorheight = 0; R.ceilingheight = 128; m.sectors.push_back(R);
    seg_t g; g.linedef = 0; g.side = 0; g.frontsector = 0; g.backsector = 1;
    m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0; m.subsectors.push_back(ss);

    World w; w.map = &m;
    mobj_t player; player.info = &mobjinfo[MT_PLAYER]; player.x = -100 << 16;
    w.player.mo = &player;
    P_ActivateLine(w, m.lines[0], &player);
    REQUIRE(w.movers.size() == 1);
    CHECK(w.movers[0].sector == 0);                    // the door, not the room
    runTics(w, 70);
    CHECK(m.sectors[0].ceilingheight == 124);
    CHECK(m.sectors[1].ceilingheight == 128);          // room untouched
}

TEST_CASE("Keyed door special 26 requires the blue key") {
    MapData m = doorMap(26);
    World w; w.map = &m;
    mobj_t player; player.info = &mobjinfo[MT_PLAYER]; player.x = 100 << 16;   // room side
    w.player.mo = &player;
    w.player.keys = 0;
    P_ActivateLine(w, m.lines[0], &player);
    CHECK(w.movers.empty());

    w.player.keys = KEY_BLUE;
    P_ActivateLine(w, m.lines[0], &player);
    CHECK(w.movers.size() == 1);
}

TEST_CASE("Lift special 62 raises the tagged floor to the next floor and returns") {
    // Room (0) | lift (1, tag 9) | upper room (2, floor 32); switch line is line 0.
    MapData m;
    m.vertices.push_back({0, SF(-1000)});  m.vertices.push_back({0, SF(1000)});
    m.vertices.push_back({64, SF(-1000)}); m.vertices.push_back({64, SF(1000)});
    line_t L0; L0.v1 = 0; L0.v2 = 1; L0.flags = ML_TWOSIDED; L0.special = 62; L0.tag = 9;
    L0.sidenum[0] = 0; L0.sidenum[1] = 1; m.lines.push_back(L0);
    line_t L1; L1.v1 = 2; L1.v2 = 3; L1.flags = ML_TWOSIDED; L1.special = 0; L1.tag = 0;
    L1.sidenum[0] = 2; L1.sidenum[1] = 3; m.lines.push_back(L1);
    side_t s0; s0.sector = 0; m.sides.push_back(s0);
    side_t s1; s1.sector = 1; m.sides.push_back(s1);
    side_t s2; s2.sector = 1; m.sides.push_back(s2);
    side_t s3; s3.sector = 2; m.sides.push_back(s3);
    sector_t A; A.floorheight = 0;  A.ceilingheight = 128; A.tag = 0; m.sectors.push_back(A);
    sector_t B; B.floorheight = 0;  B.ceilingheight = 128; B.tag = 9; m.sectors.push_back(B);
    sector_t C; C.floorheight = 32; C.ceilingheight = 128; C.tag = 0; m.sectors.push_back(C);
    seg_t g; g.linedef = 0; g.side = 0; g.frontsector = 0; g.backsector = 1;
    m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0; m.subsectors.push_back(ss);

    World w; w.map = &m;
    mobj_t player; player.info = &mobjinfo[MT_PLAYER]; player.x = 100 << 16;   // room side (east)
    w.player.mo = &player;
    P_ActivateLine(w, m.lines[0], &player);
    REQUIRE(w.movers.size() == 1);
    runTics(w, 20);
    CHECK(m.sectors[1].floorheight == 32);
    runTics(w, 130);                                 // wait 105 + descend
    CHECK(m.sectors[1].floorheight == 0);
}

TEST_CASE("Teleport special 39 moves the player to the type-14 destination") {
    MapData m;
    sector_t A; A.floorheight = 0; A.ceilingheight = 128; A.tag = 5; m.sectors.push_back(A);
    seg_t g; g.linedef = -1; g.side = 0; g.frontsector = 0; g.backsector = -1;
    m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0; m.subsectors.push_back(ss);
    m.things.push_back({300, 400, 90, 14});          // teleport landing in tag-5 sector
    line_t L; L.v1 = -1; L.v2 = -1; L.special = 39; L.tag = 5; L.sidenum[0] = -1; L.sidenum[1] = -1;
    m.lines.push_back(L);

    World w; w.map = &m;
    mobj_t player; player.info = &mobjinfo[MT_PLAYER]; player.x = 0; player.y = 0;
    player.floorz = 0; player.ceilingz = 128 << FRACBITS; w.player.mo = &player;
    P_ActivateLine(w, m.lines[0], &player);
    CHECK(player.x == SF(300));
    CHECK(player.y == SF(400));
    CHECK(player.angle == degreesToBam(90));
    CHECK(player.momx == 0);
    CHECK(player.momy == 0);
}

TEST_CASE("Use-triggered switch swaps SW1 -> SW2 on its sidedef") {
    MapData m;
    sector_t A; A.floorheight = 0; A.ceilingheight = 128; m.sectors.push_back(A);
    side_t s; s.sector = 0;
    std::memcpy(s.midtexture, "SW1DOOR", 8); s.midtexture[8] = '\0';
    m.sides.push_back(s);
    line_t L; L.v1 = -1; L.v2 = -1; L.special = 63; L.tag = 0;
    L.sidenum[0] = 0; L.sidenum[1] = -1; m.lines.push_back(L);
    World w; w.map = &m;
    mobj_t player; player.info = &mobjinfo[MT_PLAYER]; player.x = 100 << 16;   // room side (east)
    w.player.mo = &player;
    P_ActivateLine(w, m.lines[0], &player);
    CHECK(std::string(m.sides[0].midtexture) == "SW2DOOR");
}

TEST_CASE("Walk special 2 triggers when the player's move segment crosses the line") {
    MapData m = doorMap(2);
    World w; w.map = &m;
    mobj_t player; player.info = &mobjinfo[MT_PLAYER]; player.x = -10 << 16; player.y = 0;
    w.player.mo = &player;
    P_CrossSpecialLines(w, player, -10 << 16, 0);
    CHECK(w.movers.empty());                          // did not cross (x unchanged)

    player.x = 10 << 16;
    P_CrossSpecialLines(w, player, -10 << 16, 0);     // crossed x=0
    CHECK(w.movers.size() == 1);
}

TEST_CASE("Thing<->thing: a solid barrel blocks the player") {
    MapData m;
    sector_t A; A.floorheight = 0; A.ceilingheight = 128; m.sectors.push_back(A);
    seg_t g; g.linedef = -1; g.side = 0; g.frontsector = 0; g.backsector = -1;
    m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0; m.subsectors.push_back(ss);
    World w; w.map = &m; w.bm = &m.blockmap;
    mobj_t* player = P_SpawnMobj(w, 50 << 16, 0, 0, MT_PLAYER);
    player->floorz = 0; player->ceilingz = 128 << 16;
    mobj_t* barrel = P_SpawnMobj(w, 100 << 16, 0, 0, MT_BARREL);
    barrel->floorz = 0; barrel->ceilingz = 128 << 16;
    CHECK(P_TryMove(w, *player, 85 << 16, 0) == false);   // overlaps the barrel
    CHECK(P_TryMove(w, *player, 20 << 16, 0) == true);    // clear space
}

TEST_CASE("P_SlideMove slides along a wall instead of stopping/teleporting") {
    MapData m;
    m.vertices.push_back({270 << 16, 128 << 16});
    m.vertices.push_back({270 << 16, 384 << 16});
    line_t L; L.v1 = 0; L.v2 = 1; L.flags = 0; L.special = 0; L.tag = 0;
    L.sidenum[0] = 0; L.sidenum[1] = -1; m.lines.push_back(L);
    side_t s0; s0.sector = 0; m.sides.push_back(s0);
    sector_t A; A.floorheight = 0; A.ceilingheight = 128; m.sectors.push_back(A);
    seg_t g; g.linedef = 0; g.side = 0; g.frontsector = 0; g.backsector = -1;
    m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0; m.subsectors.push_back(ss);
    Blockmap bm; bm.orgx = 0; bm.orgy = 0; bm.width = 4; bm.height = 4;
    bm.lump = {0,0,4,4, 20,20,20,20, 20,20,21,20, 20,20,21,20, 20,20,20,20, -1, 0,-1};

    World w; w.map = &m; w.bm = &bm;
    mobj_t player; player.info = &mobjinfo[MT_PLAYER]; player.height = 56 << 16;
    player.x = 250 << 16; player.y = 200 << 16;
    player.floorz = 0; player.ceilingz = 128 << 16;
    P_SlideMove(w, player, 21 << 16, 21 << 16);
    CHECK(player.x < (270 << 16));                    // did not cross the wall
    CHECK(player.y > (200 << 16));                    // slid along it
}

// ---- P8a: mover targets must stay inside the sector, and no room must mean a crush ----

namespace {
// Sector 1 (tagged) has floor 0 and ceiling `ownCeil`; sector 2 next to it has floor
// `neighFloor`. A "raise floor to nearest adjacent" line in sector 0 targets tag 5.
MapData raiseMap(int ownCeil, int neighFloor) {
    MapData m;
    m.vertices.push_back({SF(0),    SF(0)});
    m.vertices.push_back({SF(0),    SF(1000)});
    m.vertices.push_back({SF(1000), SF(0)});
    m.vertices.push_back({SF(1000), SF(1000)});
    line_t L; L.v1 = 0; L.v2 = 1; L.flags = ML_TWOSIDED; L.special = 58; L.tag = 5;
    L.sidenum[0] = 0; L.sidenum[1] = -1; m.lines.push_back(L);            // the use line
    line_t M; M.v1 = 2; M.v2 = 3; M.flags = ML_TWOSIDED; M.special = 0; M.tag = 0;
    M.sidenum[0] = 1; M.sidenum[1] = 2; m.lines.push_back(M);             // 1 | 2 boundary
    side_t s0; s0.sector = 0; m.sides.push_back(s0);
    side_t s1; s1.sector = 1; m.sides.push_back(s1);
    side_t s2; s2.sector = 2; m.sides.push_back(s2);
    sector_t A; A.floorheight = 0; A.ceilingheight = 200; A.tag = 0;   m.sectors.push_back(A);
    sector_t B; B.floorheight = 0; B.ceilingheight = ownCeil; B.tag = 5; m.sectors.push_back(B);
    sector_t C; C.floorheight = neighFloor; C.ceilingheight = 400; C.tag = 0; m.sectors.push_back(C);
    seg_t g; g.linedef = 0; g.side = 0; g.frontsector = 0; g.backsector = -1; m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0; m.subsectors.push_back(ss);
    return m;
}
}  // namespace

TEST_CASE("A raised floor stops at its own ceiling instead of passing through it") {
    MapData m = raiseMap(/*ownCeil*/ 150, /*neighFloor*/ 200);
    World w; w.map = &m; w.bm = &m.blockmap;
    mobj_t player; player.info = &mobjinfo[MT_PLAYER]; player.x = 100 << 16;
    w.player.mo = &player;
    P_ActivateLine(w, m.lines[0], &player);
    REQUIRE(w.movers.size() == 1);
    for (int i = 0; i < 200; ++i) P_RunMovers(w);
    CHECK(m.sectors[1].floorheight == 150);      // clamped, not 200
}

TEST_CASE("A mover that leaves no room crushes what is inside the sector") {
    MapData m;
    sector_t A; A.floorheight = 0; A.ceilingheight = 60; m.sectors.push_back(A);
    seg_t g; g.linedef = -1; g.side = 0; g.frontsector = 0; g.backsector = -1; m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0; m.subsectors.push_back(ss);
    World w; w.map = &m; w.bm = &m.blockmap;
    mobj_t* mo = P_SpawnMobj(w, SF(10), SF(10), 0, MT_PLAYER);
    w.player.mo = mo; w.player.health = mo->health;
    REQUIRE(mo->floorz == 0);
    Mover mv; mv.kind = MoverKind::Floor; mv.sector = 0;
    mv.top = 50; mv.bottom = 50; mv.speed = 4; mv.state = 0; mv.stayOpen = true;
    w.movers.push_back(mv);
    for (int i = 0; i < 20; ++i) P_RunMovers(w);          // floor reaches 50: 50+56 > 60
    CHECK(m.sectors[0].floorheight == 50);
    CHECK(w.player.dead);                                 // crushed, not left inside geometry
    CHECK(mo->health <= 0);
}

// ---- P8b: teleport to the other side of a line (verified from the IWAD: every such
// line has its landing thing inside the sector on the far side, and none inside a
// tag-matched sector, so the tag is not what selects the destination) ----

namespace {
// Room A (x<0) | line at x=0 carrying `special` | Room B (x>0) holding the landing pad.
MapData teleportMap(int special) {
    MapData m;
    m.vertices.push_back({0, SF(-500)});
    m.vertices.push_back({0, SF(500)});
    line_t L; L.v1 = 0; L.v2 = 1; L.flags = ML_TWOSIDED; L.special = special; L.tag = 7;
    L.sidenum[0] = 0; L.sidenum[1] = 1; m.lines.push_back(L);
    side_t sA; sA.sector = 1; m.sides.push_back(sA);          // side 0 faces +x = room B
    side_t sB; sB.sector = 0; m.sides.push_back(sB);          // side 1 faces -x = room A
    sector_t A; A.floorheight = 0; A.ceilingheight = 128; A.tag = 0; m.sectors.push_back(A);
    sector_t B; B.floorheight = 0; B.ceilingheight = 128; B.tag = 7; m.sectors.push_back(B);
    seg_t g; g.linedef = 0; g.side = 0; g.frontsector = 1; g.backsector = 0; m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0; m.subsectors.push_back(ss);
    m.things.push_back({-100, 0, 0, 1});                      // player start in room A
    m.things.push_back({200, 50, 90, 14});                    // landing pad in room B
    return m;
}
}  // namespace

TEST_CASE("Teleport other side moves the player to the landing thing behind the line") {
    MapData m = teleportMap(126);
    World w; w.map = &m; w.bm = &m.blockmap; w.mapName = "TEST";
    P_SetupLevel(w, m);
    mobj_t* mo = w.player.mo;
    REQUIRE(mo);
    mo->x = SF(-20); mo->y = 0;                                // standing at the line
    mo->momx = SF(3);
    P_ActivateLine(w, m.lines[0], mo);
    CHECK(mo->x == SF(200));
    CHECK(mo->y == SF(50));
    CHECK(mo->momx == 0);                                     // momentum cleared
}

TEST_CASE("Teleport other side kills whatever is standing on the pad (telefrag)") {
    MapData m = teleportMap(125);
    World w; w.map = &m; w.bm = &m.blockmap; w.mapName = "TEST";
    P_SetupLevel(w, m);
    mobj_t* mo = w.player.mo;
    REQUIRE(mo);
    mobj_t* victim = P_SpawnMobj(w, SF(200), SF(50), 0, MT_POSSESSED);
    victim->floorz = 0;
    mo->x = SF(-20); mo->y = 0;
    P_ActivateLine(w, m.lines[0], mo);
    CHECK(mo->x == SF(200));
    CHECK((victim->health <= 0 || victim->removePending));
}

TEST_CASE("Crossing a walk-over teleport line triggers it once per crossing") {
    MapData m = teleportMap(126);
    World w; w.map = &m; w.bm = &m.blockmap; w.mapName = "TEST";
    P_SetupLevel(w, m);
    mobj_t* mo = w.player.mo;
    REQUIRE(mo);
    mo->x = SF(100); mo->y = SF(100);                          // already in room B
    P_CrossSpecialLines(w, *mo, SF(-30), 0);                   // stepped across the line
    CHECK(mo->x == SF(200));
    CHECK(mo->y == SF(50));
}
