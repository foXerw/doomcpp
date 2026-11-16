#include "doctest.h"
#include "play/g_save.h"
#include "play/p_tick.h"
#include "play/p_mobj.h"
#include "play/p_setup.h"
#include "play/info.h"
#include <cstdio>
#include <fstream>
#include <string>

namespace {
const char* SAVE = "doomcpp_test_save.dat";

MapData levelMap() {
    MapData m;
    sector_t s; s.floorheight = 0; s.ceilingheight = 128; m.sectors.push_back(s);
    seg_t g; g.linedef = -1; g.side = 0; g.frontsector = 0; g.backsector = -1;
    m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0; m.subsectors.push_back(ss);
    m.things.push_back({0, 0, 0, 1});   // player start
    return m;
}
}  // namespace

TEST_CASE("G_SaveGame/G_LoadGame round-trips player, mobjs, movers and sector heights") {
    MapData m = levelMap();
    World w; w.map = &m; w.bm = &m.blockmap;
    w.mapName = "E1M1";
    P_SetupLevel(w, m);
    REQUIRE(w.player.mo != nullptr);

    w.player.health = 88;
    w.player.mo->health = 88;
    w.player.ammo[AM_SHELL] = 17;
    w.player.weapons |= (1u << WP_SHOTGUN);
    w.player.keys = KEY_RED;
    w.player.killcount = 3;
    w.player.itemcount = 9;
    w.totalKills = 12;
    w.totalItems = 20;
    w.totalSecrets = 1;
    w.secretFound.assign(1, 1);
    w.player.secretsFound = 1;
    w.tic = 4242;

    mobj_t* demon = P_SpawnMobj(w, 200 << 16, 0, 0, MT_SERGEANT);
    demon->target = w.player.mo;
    demon->health = 77;
    mobj_t* rocket = P_SpawnMobj(w, 300 << 16, 0, 10 << 16, MT_ROCKET);
    rocket->momx = 20 << 16;
    rocket->momz = 3 << 16;

    m.sectors[0].ceilingheight = 60;                              // half-open door
    Mover mv; mv.kind = MoverKind::Door; mv.sector = 0; mv.top = 124; mv.bottom = 0;
    mv.speed = 2; mv.waitTics = 50; mv.state = 1; mv.stayOpen = false;
    w.movers.push_back(mv);

    REQUIRE(G_SaveGame(w, SAVE));

    // Wreck the live state.
    w.player.health = 1; w.player.ammo[AM_SHELL] = 0; w.player.keys = 0;
    m.sectors[0].ceilingheight = 128;
    w.movers.clear();
    w.mobjs.clear();
    w.player.mo = nullptr;
    w.tic = 0;

    REQUIRE(G_LoadGame(w, SAVE));
    CHECK(w.tic == 4242);
    CHECK(w.player.health == 88);
    REQUIRE(w.player.mo != nullptr);
    CHECK(w.player.mo->health == 88);
    CHECK(w.player.ammo[AM_SHELL] == 17);
    CHECK(w.player.keys == KEY_RED);
    CHECK(w.player.killcount == 3);
    CHECK(w.totalKills == 12);
    CHECK(w.totalItems == 20);
    CHECK(w.totalSecrets == 1);
    CHECK(w.player.secretsFound == 1);
    REQUIRE(w.secretFound.size() == 1);
    CHECK(w.secretFound[0] == 1);
    CHECK(m.sectors[0].ceilingheight == 60);
    REQUIRE(w.movers.size() == 1);
    CHECK(w.movers[0].kind == MoverKind::Door);
    CHECK(w.movers[0].state == 1);
    CHECK(w.movers[0].waitTics == 50);

    // Mobjs: player + sergeant + rocket, with target pointer repaired.
    CHECK(w.mobjs.size() == 3);
    mobj_t* loadedDemon = nullptr;
    mobj_t* loadedRocket = nullptr;
    for (mobj_t& mo : w.mobjs) {
        if (mo.info == &mobjinfo[MT_SERGEANT]) loadedDemon = &mo;
        if (mo.info == &mobjinfo[MT_ROCKET]) loadedRocket = &mo;
    }
    REQUIRE(loadedDemon != nullptr);
    REQUIRE(loadedRocket != nullptr);
    CHECK(loadedDemon->health == 77);
    CHECK(loadedDemon->target == w.player.mo);
    CHECK(loadedRocket->momx == (20 << 16));
    CHECK(loadedRocket->momz == (3 << 16));

    std::remove(SAVE);
}

TEST_CASE("G_LoadGame rejects a bad magic and a map mismatch") {
    {
        std::ofstream f(SAVE, std::ios::binary);
        f.write("NOPE", 4);
    }
    MapData m = levelMap();
    World w; w.map = &m; w.bm = &m.blockmap; w.mapName = "E1M1";
    P_SetupLevel(w, m);
    CHECK(!G_LoadGame(w, SAVE));

    // Valid save, but loaded onto a different map.
    REQUIRE(G_SaveGame(w, SAVE));
    w.mapName = "E1M2";
    CHECK(!G_LoadGame(w, SAVE));
    std::string lv;
    REQUIRE(G_SaveGameLevel(SAVE, lv));
    CHECK(lv == "E1M1");
    std::remove(SAVE);
}

TEST_CASE("A v2 save (before the power-up timers) still loads") {
    MapData m = levelMap();
    World w; w.map = &m; w.bm = &m.blockmap; w.mapName = "E1M1";
    P_SetupLevel(w, m);
    w.player.ammo[AM_SHELL] = 23;
    w.player.powers[pw_strength] = 1;
    w.player.powers[pw_invulnerability] = 500;
    const std::string path = "doomcpp-v2-compat.dat";
    REQUIRE(G_SaveGame(w, path));

    // Pretend the file was written by the previous format: stamp version 2 and drop the
    // power-up tail. Everything that predates the change must still come back, and the
    // powers must read as "none" rather than failing the load outright.
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(static_cast<bool>(f));
    f.seekg(4);
    std::int32_t version = 0;
    f.read(reinterpret_cast<char*>(&version), sizeof(version));
    REQUIRE(version == 3);
    f.seekp(4);
    const std::int32_t oldVersion = 2;
    f.write(reinterpret_cast<const char*>(&oldVersion), sizeof(oldVersion));
    f.close();
    {
        std::ifstream in(path, std::ios::binary);
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        all.resize(all.size() - sizeof(std::int32_t) * NUMPOWERS);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(all.data(), static_cast<std::streamsize>(all.size()));
    }

    World r; r.map = &m; r.bm = &m.blockmap; r.mapName = "E1M1";
    P_SetupLevel(r, m);
    REQUIRE(G_LoadGame(r, path));
    CHECK(r.player.ammo[AM_SHELL] == 23);
    CHECK(r.player.powers[pw_strength] == 0);
    CHECK(r.player.powers[pw_invulnerability] == 0);
    std::remove(path.c_str());
}
