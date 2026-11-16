#include "doctest.h"
#include "render/r_things.h"
#include "render/r_data.h"   // Patch
#include "render/r_plane.h"  // R_DistanceShade
#include <cmath>
#include <cstring>
#include <limits>
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

// ---- spriteRotationIndex (Task 5) ----

TEST_CASE("spriteRotationIndex maps relative angle to 8-way (0=front,4=back)") {
    const float PI = 3.141592653589793f;
    CHECK(spriteRotationIndex(0.0f)        == 4);   // player behind thing's facing -> back
    CHECK(spriteRotationIndex(PI)          == 0);   // player in front             -> front
    CHECK(spriteRotationIndex(PI / 2.0f)   == 6);   // +90
    CHECK(spriteRotationIndex(-PI / 2.0f)  == 2);   // -90
    CHECK(spriteRotationIndex(PI / 4.0f)   == 5);   // +45
    CHECK(spriteRotationIndex(-PI / 4.0f)  == 3);   // -45
    // periodicity: +2pi == 0
    CHECK(spriteRotationIndex(2.0f * PI)   == 4);
}

// ---- R_ProjectSprite (Task 7) ----

TEST_CASE("R_ProjectSprite rejects a thing behind the camera") {
    SpriteCache sc; Patch p{2,2,0,0,std::vector<uint32_t>(4,0xFFu)};
    sc.install("POSSA0", &p);                // non-rotate, all angles
    mobj_t t; t.x = 0; t.y = (fixed_t)(-5 << 16); t.z = 0; t.angle = 0;
    std::memcpy(t.sprname,"POSS",5); t.frame='A';
    vissprite_t vs;
    // camera at origin looking +Y (sin=0,cos=1 -> depth=dy); thing south at (0,-5): depth=-5 <0 -> behind
    bool ok = R_ProjectSprite(vs, t, sc, 0,0, 0.0f,1.0f, 41.0f, 160.0f, 320,200);
    CHECK(ok == false);
}

TEST_CASE("R_ProjectSprite projects a centered thing with correct x1/x2/gzt") {
    SpriteCache sc;
    // 4 wide, 8 tall patch; leftoffset=2, topoffset=8 -> feet at z
    Patch p{4,8, 2,8, std::vector<uint32_t>(32, 0xFFFFFFFFu)};
    sc.install("BAR1A0", &p);
    mobj_t t; t.x = 0; t.y = (fixed_t)(100 << 16); t.z = 0; t.angle = 0;
    std::memcpy(t.sprname,"BAR1",5); t.frame='A'; t.light=160;
    vissprite_t vs;
    // camera at origin looking +Y (sin=0,cos=1); thing north at (0,100): depth=dy=100, tx=dx=0
    bool ok = R_ProjectSprite(vs, t, sc, 0,0, 0.0f,1.0f, 41.0f, 160.0f, 320,200);
    REQUIRE(ok);
    CHECK(vs.depth == doctest::Approx(100.0f));
    CHECK(vs.scale == doctest::Approx(0.01f));
    // ppe = focal*scale = 160*0.01 = 1.6 px/texel; width 4 -> 6.4px; centered at x=160
    // xTrueLeft = 160 - leftoffset(2)*1.6 = 156.8 ; x1=156, x2=163
    CHECK(vs.x1 <= 160); CHECK(vs.x2 >= 160);
    CHECK(vs.gzt == doctest::Approx(8.0f));   // z(0) + topoffset(8)
    CHECK(vs.patch == &p);
    CHECK(vs.flip == false);
}

TEST_CASE("R_ProjectSprite picks front/back rotation by relative facing") {
    // Locks the atan2 sign (player->thing, NOT thing->player).
    SpriteCache sc;
    Patch front{1,1,0,0,std::vector<uint32_t>(1,0xAAAAAAAAu)};   // rotation 1 (index 0) = front view
    Patch back {1,1,0,0,std::vector<uint32_t>(1,0xBBBBBBBBu)};   // rotation 5 (index 4) = back view
    sc.install("TROOA1", &front);
    sc.install("TROOA5", &back);
    mobj_t t; t.x = 0; t.y = (fixed_t)(100 << 16); t.z = 0;
    std::memcpy(t.sprname,"TROO",5); t.frame='A'; t.light=160;
    vissprite_t vs;
    // camera at origin looking +Y; thing north at (0,100). viewAng(player->thing)=atan2(100,0)=North(+pi/2).
    // thing faces South (ANG270, toward player) -> rel = pi/2 - 3pi/2 = -pi ~= pi -> rotIdx 0 -> FRONT
    t.angle = ANG270;
    REQUIRE(R_ProjectSprite(vs, t, sc, 0,0, 0.0f,1.0f, 41.0f, 160.0f, 320,200));
    CHECK(vs.patch == &front);
    // thing faces North (ANG90, away from player) -> rel = pi/2 - pi/2 = 0 -> rotIdx 4 -> BACK
    t.angle = ANG90;
    REQUIRE(R_ProjectSprite(vs, t, sc, 0,0, 0.0f,1.0f, 41.0f, 160.0f, 320,200));
    CHECK(vs.patch == &back);
}

// ---- R_DrawVisSprite (Task 8) ----

TEST_CASE("R_DrawVisSprite writes opaque texel, skips transparent, bounded by screen") {
    int w=8,h=8; float focal=4.0f, eyeZ=4.0f;
    std::vector<uint32_t> fb(w*h, 0u);
    std::vector<float> wd(static_cast<size_t>(w) * h, std::numeric_limits<float>::infinity());   // no walls -> sprite draws
    // 2x2 patch: opaque red at (row0,col0) and (row0,col1); transparent elsewhere
    Patch p{2,2, 0,2, std::vector<uint32_t>(4, 0u)};
    uint32_t red = (0xFFu<<24)|0x00u|(0x00u<<8)|0xFFu;   // R=255
    p.rgba[0*2+0] = red; p.rgba[0*2+1] = red;
    vissprite_t vs;
    vs.depth=4.0f; vs.scale=0.25f; vs.x1=2; vs.x2=5; vs.xTrueLeft=2.0f;
    vs.gzt = 4.0f + 2.0f;   // eyeZ + topoffset(2) -> image top
    vs.patch=&p; vs.flip=false; vs.light=255;
    // ppe = focal*scale = 4*0.25 = 1 px/texel -> 2px wide starting x=2
    R_DrawVisSprite(fb.data(), w, h, vs, eyeZ, focal, wd);
    // image top row: worldZ=gzt=6 -> syTop = 4 - 4*(6-4)*0.25 = 4-2 = 2 ; rows 2,3
    CHECK((fb[2*w+2] >> 24 & 0xFF) == 0xFF);   // opaque red written at (x=2,y=2)
    CHECK((fb[2*w+3] >> 24 & 0xFF) == 0xFF);   // (x=3,y=2)
    CHECK(fb[3*w+2] == 0u);                    // row1 of patch transparent -> untouched
}

TEST_CASE("R_DrawVisSprite flip mirrors texel column") {
    int w=8,h=8; float focal=4.0f, eyeZ=4.0f;
    std::vector<uint32_t> fb(w*h, 0u);
    std::vector<float> wd(static_cast<size_t>(w) * h, std::numeric_limits<float>::infinity());
    // 2x1 patch: red at col0, green at col1 (both opaque so the swap is non-vacuous)
    Patch p{2,1, 0,1, std::vector<uint32_t>(2,0u)};
    p.rgba[0] = (0xFFu<<24)|0xFFu;            // col0 red-ish (R=255,A=255)
    p.rgba[1] = (0x00u<<24)|(0xFFu<<16)|0xFFu;// col1 green-ish (R=0,G=255,A=255) -- opaque
    vissprite_t vs; vs.depth=4.0f; vs.scale=0.25f; vs.x1=2; vs.x2=3; vs.xTrueLeft=2.0f;
    vs.gzt=6.0f; vs.patch=&p; vs.flip=false; vs.light=255;   // image top worldZ=6 -> syTop=2 (row 2)
    R_DrawVisSprite(fb.data(), w, h, vs, eyeZ, focal, wd);
    uint32_t left = fb[2*w+2], right = fb[2*w+3];
    CHECK(left  == 0xFF0000FFu);   // red on left, non-zero baseline
    CHECK(right == 0x00FF00FFu);   // green on right, non-zero baseline
    // flipped: col mapping reversed
    std::vector<uint32_t> fbf(w*h, 0u);
    vs.flip = true;
    R_DrawVisSprite(fbf.data(), w, h, vs, eyeZ, focal, wd);
    CHECK(fbf[2*w+2] == right);   // what was on the right is now on the left
    CHECK(fbf[2*w+3] == left);
}

TEST_CASE("R_DrawVisSprite: a low wall occludes only the rows it covers (P7 per-pixel depth)") {
    int w=8,h=8; float focal=4.0f, eyeZ=4.0f;
    std::vector<uint32_t> fb(w*h, 0u);
    Patch p{2,2, 0,2, std::vector<uint32_t>(4, 0u)};
    uint32_t red = (0xFFu<<24)|0xFFu;
    for (auto& c : p.rgba) c = red;                // 2x2 fully opaque
    vissprite_t vs;
    vs.depth=4.0f; vs.scale=0.25f; vs.x1=2; vs.x2=5; vs.xTrueLeft=2.0f;
    vs.gzt=6.0f; vs.patch=&p; vs.flip=false; vs.light=255;
    // The sprite covers rows 2..3. A floor riser at 32 units only covers row 3.
    std::vector<float> wd(static_cast<size_t>(w) * h, std::numeric_limits<float>::infinity());
    wd[3*w+2] = 3.0f; wd[3*w+3] = 3.0f;
    R_DrawVisSprite(fb.data(), w, h, vs, eyeZ, focal, wd);
    CHECK(fb[2*w+2] != 0u);        // row 2 (above the riser) still drawn
    CHECK(fb[2*w+3] != 0u);
    CHECK(fb[3*w+2] == 0u);        // row 3 hidden by the nearer riser pixel
    CHECK(fb[3*w+3] == 0u);
}

TEST_CASE("R_DrawVisSprite occludes columns behind a nearer wall") {
    int w=8,h=8; float focal=4.0f, eyeZ=4.0f;
    std::vector<uint32_t> fb(w*h, 0u);
    // same centered sprite as the opaque test: 2x2 patch, opaque red at row0 col0/col1
    Patch p{2,2, 0,2, std::vector<uint32_t>(4, 0u)};
    uint32_t red = (0xFFu<<24)|0x00u|(0x00u<<8)|0xFFu;   // R=255
    p.rgba[0*2+0] = red; p.rgba[0*2+1] = red;
    vissprite_t vs;
    vs.depth=4.0f; vs.scale=0.25f; vs.x1=2; vs.x2=5; vs.xTrueLeft=2.0f;
    vs.gzt=6.0f; vs.patch=&p; vs.flip=false; vs.light=255;
    // wallDepth[x] < vis.depth at one column hides it; everywhere else stays open.
    std::vector<float> wd(static_cast<size_t>(w) * h, std::numeric_limits<float>::infinity());
    wd[2*w+3] = 3.0f;   // wall pixels at x=3 are nearer than the sprite (3 < depth 4) -> occlude
    wd[3*w+3] = 3.0f;
    R_DrawVisSprite(fb.data(), w, h, vs, eyeZ, focal, wd);
    CHECK(fb[2*w+3] == 0u);                       // x=3 occluded -> untouched
    CHECK((fb[2*w+2] >> 24 & 0xFF) == 0xFF);      // x=2 still drawn
    // equal-depth wall also occludes (test uses `wallDepth[x] <= vis.depth`)
    std::vector<float> wd2(static_cast<size_t>(w) * h, std::numeric_limits<float>::infinity());
    wd2[2*w+3] = 4.0f;   // == vis.depth
    wd2[3*w+3] = 4.0f;
    std::vector<uint32_t> fb2(w*h, 0u);
    R_DrawVisSprite(fb2.data(), w, h, vs, eyeZ, focal, wd2);
    CHECK(fb2[2*w+3] == 0u);                      // x=3 still occluded at equal depth
    CHECK((fb2[2*w+2] >> 24 & 0xFF) == 0xFF);     // x=2 still drawn
}

TEST_CASE("R_DrawVisSprite fuzz (spectre) blends with the framebuffer") {
    int w=8,h=8; float focal=4.0f, eyeZ=4.0f;
    std::vector<uint32_t> fb(w*h, 0x000000FFu);          // opaque black background
    Patch p{1,1, 0,1, std::vector<uint32_t>(1, 0xFF0000FFu)};   // opaque red
    vissprite_t vs;
    vs.depth=4.0f; vs.scale=0.25f; vs.x1=2; vs.x2=2; vs.xTrueLeft=2.0f;
    vs.gzt=6.0f; vs.patch=&p; vs.flip=false; vs.fuzz=true; vs.light=255;
    std::vector<float> wd(static_cast<size_t>(w) * h, std::numeric_limits<float>::infinity());
    R_DrawVisSprite(fb.data(), w, h, vs, eyeZ, focal, wd);
    const uint32_t px = fb[2*w+2];
    CHECK((px >> 24 & 0xFF) > 100);                      // half of 255 over black
    CHECK((px >> 24 & 0xFF) < 160);
    CHECK((px & 0xFFu) == 0xFFu);                        // still opaque
}

TEST_CASE("a sprite standing on a floor is not clipped by that floor, but is by a nearer one") {
    int w=8,h=8; float focal=4.0f, eyeZ=4.0f;
    std::vector<uint32_t> fb(w*h, 0u);
    Patch p{2,2, 0,2, std::vector<uint32_t>(4, 0u)};
    uint32_t red = (0xFFu<<24)|0xFFu;
    for (auto& c : p.rgba) c = red;                       // 2x2 fully opaque
    vissprite_t vs;
    vs.depth=8.0f; vs.scale=0.5f; vs.x1=2; vs.x2=5; vs.xTrueLeft=2.0f;
    vs.gzt=6.0f; vs.patch=&p; vs.flip=false; vs.light=255;
    // The floor the sprite stands on: its recorded depth is its own distance plus the
    // half-unit tolerance R_DrawSpan writes, so the sprite's own footprint must survive.
    std::vector<float> floorDepth(static_cast<size_t>(w)*h, std::numeric_limits<float>::infinity());
    for (int x = 0; x < w; ++x) floorDepth[3*w+x] = 8.5f;   // row 3 == the sprite's foot row
    R_DrawVisSprite(fb.data(), w, h, vs, eyeZ, focal, floorDepth);
    CHECK(fb[2*w+2] != 0u);                                // body drawn
    CHECK(fb[3*w+2] != 0u);                                // feet NOT clipped by their own floor
    // A platform nearer than the sprite hides the rows it covers.
    std::vector<float> plat(static_cast<size_t>(w)*h, std::numeric_limits<float>::infinity());
    for (int y = 2; y < h; ++y) for (int x = 0; x < w; ++x) plat[y*w+x] = 4.5f;
    std::vector<uint32_t> fb2(w*h, 0u);
    R_DrawVisSprite(fb2.data(), w, h, vs, eyeZ, focal, plat);
    CHECK(fb2[2*w+2] == 0u);                               // hidden behind the platform
    CHECK(fb2[3*w+2] == 0u);
}
