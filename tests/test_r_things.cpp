#include "doctest.h"
#include "render/r_things.h"
#include <cstring>

TEST_CASE("parseSpriteLumpName: single rotation") {
    auto n = parseSpriteLumpName("POSSA1");
    CHECK(std::string(n.sprname) == "POSS");
    REQUIRE(n.nInst == 1);
    CHECK(n.inst[0].frame == 'A');
    CHECK(n.inst[0].rotation == 1);
    CHECK(n.inst[0].flip == false);
}

TEST_CASE("parseSpriteLumpName: mirrored pair POSSA2A8") {
    auto n = parseSpriteLumpName("POSSA2A8");
    CHECK(std::string(n.sprname) == "POSS");
    REQUIRE(n.nInst == 2);
    CHECK(n.inst[0].frame == 'A'); CHECK(n.inst[0].rotation == 2); CHECK(n.inst[0].flip == false);
    CHECK(n.inst[1].frame == 'A'); CHECK(n.inst[1].rotation == 8); CHECK(n.inst[1].flip == true);
}

TEST_CASE("parseSpriteLumpName: all-angles (rotation 0) and non-A frame") {
    auto n = parseSpriteLumpName("BAR1A0");
    CHECK(std::string(n.sprname) == "BAR1");
    REQUIRE(n.nInst == 1);
    CHECK(n.inst[0].frame == 'A');
    CHECK(n.inst[0].rotation == 0);   // 0 => install for all 8
    auto m = parseSpriteLumpName("PLAYN0");
    CHECK(std::string(m.sprname) == "PLAY");
    CHECK(m.inst[0].frame == 'N');
    CHECK(m.inst[0].rotation == 0);
}
