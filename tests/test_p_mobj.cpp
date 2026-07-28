#include "doctest.h"
#include "play/p_mobj.h"
#include "play/p_tick.h"
#include "play/info.h"
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
