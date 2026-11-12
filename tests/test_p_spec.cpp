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
