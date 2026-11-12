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

// ---- P5b tables ----

TEST_CASE("P5b: weapon psprite state chains carry actions") {
    CHECK(states[S_PISTOL].action == A_WeaponReady);
    CHECK(states[S_PISTOL2].action == A_FirePistol);
    CHECK(states[S_PISTOL2].nextstate == S_PISTOL3);
    CHECK(std::string(sprnames[states[S_PISTOL2].sprite]) == "PISG");
    CHECK(states[S_PISTOLFLASH].action == A_Light1);
    CHECK(states[S_SGUN2].action == A_FireShotgun);
    CHECK(states[S_CHAIN2].action == A_FireCGun);
    CHECK(states[S_MISSILE2].action == A_FireMissile);
    CHECK(states[S_SAW].action == A_WeaponReady);
}

TEST_CASE("P5b: weaponinfo/ammoinfo match vanilla") {
    CHECK(weaponinfo[WP_PISTOL].ammo == AM_CLIP);
    CHECK(weaponinfo[WP_PISTOL].readystate == S_PISTOL);
    CHECK(weaponinfo[WP_PISTOL].atkstate == S_PISTOL1);
    CHECK(weaponinfo[WP_SHOTGUN].ammo == AM_SHELL);
    CHECK(weaponinfo[WP_MISSILE].ammo == AM_MISL);
    CHECK(weaponinfo[WP_PLASMA].ammo == AM_CELL);
    CHECK(weaponinfo[WP_FIST].ammo == AM_NOAMMO);
    CHECK(ammoinfo[AM_CLIP].maxammo == 200);
    CHECK(ammoinfo[AM_SHELL].maxammo == 50);
    CHECK(ammoinfo[AM_CELL].maxammo == 300);
    CHECK(ammoinfo[AM_MISL].maxammo == 50);
}

TEST_CASE("P5b: monsters have pain/death state fields") {
    CHECK(mobjinfo[MT_POSSESSED].painstate == S_POSS_PAIN);
    CHECK(mobjinfo[MT_POSSESSED].deathstate == S_POSS_DIE1);
    CHECK(mobjinfo[MT_POSSESSED].painchance == 200);
    CHECK(mobjinfo[MT_SHOTGUY].deathstate == S_SPOS_DIE1);
    CHECK(mobjinfo[MT_TROOP].xdeathstate == S_TROO_XDIE1);
    CHECK(mobjinfo[MT_SERGEANT].deathstate == S_SARG_DIE1);
    CHECK(mobjinfo[MT_PLAYER].deathstate == S_PLAY_DIE1);
    CHECK(mobjinfo[MT_PLAYER].painchance == 255);
}

TEST_CASE("P5b: projectile/effect mobj types") {
    CHECK((mobjinfo[MT_ROCKET].flags & MF_MISSILE) != 0);
    CHECK(mobjinfo[MT_ROCKET].deathstate == S_EXPLODE1);
    CHECK(mobjinfo[MT_ROCKET].damage == 20);
    CHECK(mobjinfo[MT_PLASMA].spawnstate == S_PLASBALL);
    CHECK(mobjinfo[MT_BFG].damage == 100);
    CHECK(mobjinfo[MT_PUFF].spawnstate == S_PUFF1);
    CHECK(mobjinfo[MT_BLOOD].spawnstate == S_BLOOD1);
    CHECK((mobjinfo[MT_PUFF].flags & MF_NOGRAVITY) != 0);
}

TEST_CASE("P5b: new sprite names are bound") {
    CHECK(std::string(sprnames[SPR_PISG]) == "PISG");
    CHECK(std::string(sprnames[SPR_PUNG]) == "PUNG");
    CHECK(std::string(sprnames[SPR_PUFF]) == "PUFF");
    CHECK(std::string(sprnames[SPR_MISL]) == "MISL");
}
