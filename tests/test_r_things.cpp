#include "doctest.h"
#include "render/r_things.h"
#include "render/r_data.h"   // Patch
#include <cstring>
#include <vector>

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

// ---- SpriteCache (Task 4) ----

TEST_CASE("SpriteCache: single rotation install/resolve") {
    SpriteCache sc;
    Patch p{2, 2, 0, 0, std::vector<uint32_t>(4, 0x112233FFu)};
    sc.install("POSSA1", &p);
    auto v = sc.resolve("POSS", 'A', 0);
    CHECK(v.patch == &p);
    CHECK(v.flip == false);
    CHECK(sc.resolve("MISS", 'A', 0).patch == nullptr);   // absent sprite
}

TEST_CASE("SpriteCache: mirrored pair POSSA2A8 resolves rot8 flipped") {
    SpriteCache sc;
    Patch p{1, 1, 0, 0, std::vector<uint32_t>(1, 0xFFu)};
    sc.install("POSSA2A8", &p);   // rot2 normal, rot8 flipped
    auto r2 = sc.resolve("POSS", 'A', 1);   // rotIndex 1 == rotation 2
    CHECK(r2.patch == &p); CHECK(r2.flip == false);
    auto r8 = sc.resolve("POSS", 'A', 7);   // rotIndex 7 == rotation 8
    CHECK(r8.patch == &p); CHECK(r8.flip == true);
}

TEST_CASE("SpriteCache: rotation 0 -> single lump for all rotIndex, case-insensitive") {
    SpriteCache sc;
    Patch p{1, 1, 0, 0, std::vector<uint32_t>(1, 0xAAu)};
    sc.install("BAR1A0", &p);
    for (int r = 0; r < 8; ++r) {
        auto v = sc.resolve("bar1", 'A', r);   // lowercase lookup
        CHECK(v.patch == &p);
        CHECK(v.flip == false);
    }
}
