#include "doctest.h"
#include "play/info.h"
#include <string>

TEST_CASE("sprnames maps spritenum to 4-char name") {
    CHECK(std::string(sprnames[SPR_PLAY]) == "PLAY");
    CHECK(std::string(sprnames[SPR_BAR1]) == "BAR1");
    CHECK(std::string(sprnames[SPR_TROO]) == "TROO");
    CHECK(std::string(sprnames[SPR_POSS]) == "POSS");
}

TEST_CASE("mobjtypeForDoomednum: E1M1 mappings + skips unknown/player-start-handled-by-caller") {
    CHECK(mobjtypeForDoomednum(2035) == MT_BARREL);
    CHECK(mobjtypeForDoomednum(3004) == MT_POSSESSED);
    CHECK(mobjtypeForDoomednum(3001) == MT_TROOP);
    CHECK(mobjtypeForDoomednum(9)    == MT_SHOTGUY);
    CHECK(mobjtypeForDoomednum(1)    == MT_PLAYER);     // player start
    CHECK(mobjtypeForDoomednum(11)   == -1);            // DM start -> not spawned
    CHECK(mobjtypeForDoomednum(9999) == -1);            // unknown
}

TEST_CASE("spawnstate sprite/frame/fullbright reproduces P4 SpriteDef") {
    auto check = [](int dn, const char* spr, char frame, bool fb){
        int mt = mobjtypeForDoomednum(dn); REQUIRE(mt >= 0);
        const state_t& st = states[mobjinfo[mt].spawnstate];
        CHECK(std::string(sprnames[st.sprite]) == spr);
        CHECK(('A' + (st.frame & FF_FRAMEMASK)) == frame);
        CHECK(((st.frame & FF_FULLBRIGHT) != 0) == fb);
    };
    check(2035, "BAR1", 'A', false);
    check(3004, "POSS", 'A', false);
    check(3001, "TROO", 'A', false);
    check(9,    "SPOS", 'A', false);
    check(18,   "POSS", 'L', false);   // hanging corpse (gibbed former human)
    check(20,   "TROO", 'M', false);
    check(21,   "SARG", 'N', false);
    check(15,   "PLAY", 'N', false);
    check(2013, "SOUL", 'A', true);    // fullbright
    check(2028, "COLU", 'A', true);
}

TEST_CASE("MT_PLAYER collision footprint equals P3.5 constants") {
    CHECK(mobjinfo[MT_PLAYER].radius == 16 << FRACBITS);
    CHECK(mobjinfo[MT_PLAYER].height == 56 << FRACBITS);
    CHECK((mobjinfo[MT_PLAYER].flags & MF_SOLID)   != 0);
    CHECK((mobjinfo[MT_PLAYER].flags & MF_DROPOFF) != 0);
}

TEST_CASE("MT_BARREL is solid + has gravity (no NOGRAVITY)") {
    CHECK((mobjinfo[MT_BARREL].flags & MF_SOLID)      != 0);
    CHECK((mobjinfo[MT_BARREL].flags & MF_NOGRAVITY)  == 0);
    CHECK(mobjinfo[MT_BARREL].radius > 0);
    CHECK(mobjinfo[MT_BARREL].height > 0);
}
