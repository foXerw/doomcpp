#include "doctest.h"
#include "render/r_plane.h"
#include "render/r_data.h"   // Flat
#include <cmath>
#include <vector>

TEST_CASE("R_FindPlane merges coplanar, creates distinct") {
    int w = 16;
    std::vector<Visplane> vps;
    Flat fA{};   // default-constructed; merge key is the Flat* address, not the name
    Flat fB{};
    int a = R_FindPlane(vps, 0.0f, &fA, 160, false, w);
    int a2 = R_FindPlane(vps, 0.0f, &fA, 160, false, w);  // same key -> merge
    CHECK(a == a2);
    CHECK(vps.size() == 1);

    int b = R_FindPlane(vps, 8.0f, &fA, 160, false, w);   // different height
    CHECK(b != a);
    CHECK(vps.size() == 2);

    int c = R_FindPlane(vps, 0.0f, &fB, 160, false, w);   // different flat
    CHECK(c != a);
    CHECK(vps.size() == 3);

    int s1 = R_FindPlane(vps, 0.0f, &fA, 160, true, w);   // sky: merges all sky
    int s2 = R_FindPlane(vps, 99.0f, &fB, 200, true, w);
    CHECK(s1 == s2);
}

TEST_CASE("R_FindPlane new plane is empty (minx>maxx, columns unclaimed)") {
    std::vector<Visplane> vps;
    Flat f{};
    int i = R_FindPlane(vps, 0.0f, &f, 160, false, 8);
    CHECK(vps[i].minx == 8);      // SCREENWIDTH sentinel (empty)
    CHECK(vps[i].maxx == -1);
    REQUIRE((int)vps[i].top.size() == 8);
    for (int x = 0; x < 8; ++x) {
        CHECK(vps[i].top[x] == kOpenTop);    // unclaimed
        CHECK(vps[i].bottom[x] == kOpenBot);
    }
}

TEST_CASE("R_CheckPlane extends range on disjoint columns, splits on collision") {
    int w = 32;
    std::vector<Visplane> vps;
    Flat f{};
    int p = R_FindPlane(vps, 0.0f, &f, 160, false, w);
    // claim column 5 with a band
    vps[p].minx = 5; vps[p].maxx = 5; vps[p].top[5] = 10; vps[p].bottom[5] = 20;
    // disjoint range [10..12] -> same plane, range unions
    int q = R_CheckPlane(vps, p, 10, 12);
    CHECK(q == p);
    CHECK(vps[p].minx == 5);
    CHECK(vps[p].maxx == 12);
    // overlapping-but-unclaimed columns [4..6]: col 5 claimed -> collision -> new plane
    int r = R_CheckPlane(vps, p, 4, 6);
    CHECK(r != p);
    CHECK(vps[r].minx == 4);
    CHECK(vps[r].maxx == 6);
    CHECK(vps.size() == 2);
}

// --- Task 4: plane projection (yslope/distscale + R_DrawSpan) ---
// NOTE: one index typo fixed vs. the brief — the symmetric-pair check used
// yslope[100] with the comment "y=150"; index [100] is the horizon row
// (h/2 = 200/2 = 100 -> INFINITY), which the horizon check below also
// asserts. Changed to yslope[150] to match the comment and keep the suite
// internally consistent.

TEST_CASE("R_SetupPlaneTables fills yslope/distscale per formula") {
    PlaneCtx c; c.w = 320; c.h = 200; c.focal = 160.0f;
    R_SetupPlaneTables(c);
    REQUIRE((int)c.yslope.size() == 200);
    REQUIRE((int)c.distscale.size() == 320);
    // distscale[center] == 0
    CHECK(c.distscale[160] == doctest::Approx(0.0f));
    // yslope[y] = focal / |y - h/2|
    CHECK(c.yslope[150] == doctest::Approx(160.0f / 50.0f));   // y=150 -> |150-100|=50
    CHECK(c.yslope[50]  == doctest::Approx(160.0f / 50.0f));   // y=50  -> |50-100|=50
    // horizon row (y==100): FINITE and far (not INFINITY). P3d: was INFINITY, which forced
    // R_DrawSpan to skip the horizon row -> a permanent 1px black seam (whole row black in
    // outdoor views). Now finite so the row is drawn (dark/far) like vanilla.
    CHECK(!std::isinf(c.yslope[100]));
    CHECK(c.yslope[100] > 1e3f);
    CHECK(c.yslope[100] > c.focal);   // farther than the closest real row (|dy|=1 -> focal)
}

TEST_CASE("R_DistanceShade is monotonic decreasing with a floor") {
    // NOTE: brief's `<= 1.0f` is incompatible with the frozen Task 3 body
    // (1.15f - depth*0.0006f, no upper clamp) -> R_DistanceShade(0) == 1.15.
    // The 1.0 cap lives in R_DrawSpan, not here. Pinning the real peak.
    CHECK(R_DistanceShade(0.0f) == doctest::Approx(1.15f));
    CHECK(R_DistanceShade(100.0f) >= R_DistanceShade(500.0f));
    CHECK(R_DistanceShade(10000.0f) == doctest::Approx(0.12f));  // clamped to floor
}

TEST_CASE("R_DrawSpan writes shaded flat texels and sky writes constant") {
    PlaneCtx c; c.w = 8; c.h = 8; c.focal = 4.0f; c.eyeZ = 41.0f;
    c.px = 0; c.py = 0; c.sin = 0; c.cos = 1.0f;   // looking +Y
    std::vector<uint32_t> fb(8*8, 0); c.fb = fb.data();
    R_SetupPlaneTables(c);
    Flat f{}; f.width = 64; f.height = 64;
    f.rgba.assign(64*64, (0xFFu<<24)|(0xFFu<<16)|(0xFFu<<8)|0xFFu); // white
    // light=128 forces total shade < 1.0 (0.50) regardless of distance clamp,
    // so white must be shaded down.
    R_DrawSpan(c, 6, 0, 7, &f, /*planeheight=*/41.0f, /*light=*/128, /*sky=*/false);
    for (int x = 0; x < 8; ++x) {
        uint32_t px = fb[6*8 + x];
        CHECK((px >> 24 & 0xFF) < 0xFF);   // shaded down from full white
        CHECK((px & 0xFF) == 0xFF);        // alpha opaque
    }
    // sky span: constant kSkyColor
    R_DrawSpan(c, 1, 0, 7, nullptr, 0.0f, 0, /*sky=*/true);
    for (int x = 0; x < 8; ++x)
        CHECK(fb[1*8 + x] == kSkyColor);
}

TEST_CASE("R_DrawSpan draws the horizon row (y==h/2) instead of leaving a black seam") {
    PlaneCtx c; c.w = 8; c.h = 8; c.focal = 4.0f; c.eyeZ = 0.0f;
    c.px = 0; c.py = 0; c.sin = 0; c.cos = 1.0f;   // looking +Y
    std::vector<uint32_t> fb(8*8, 0); c.fb = fb.data();
    R_SetupPlaneTables(c);
    Flat f{}; f.width = 64; f.height = 64;
    f.rgba.assign(64*64, (0x80u<<24)|(0x80u<<16)|(0x80u<<8)|0xFFu);   // grey, opaque
    int hy = c.h / 2;                                  // the horizon row
    R_DrawSpan(c, hy, 0, 7, &f, /*planeheight=*/41.0f, /*light=*/255, /*sky=*/false);
    for (int x = 0; x < 8; ++x) {
        CHECK(fb[hy*8 + x] != 0u);                    // drawn, NOT left at clear-color black
        CHECK((fb[hy*8 + x] & 0xFFu) == 0xFFu);        // opaque
    }
}

// --- Task 5: R_PlaneSpans (row-run span enumeration) ---
// NOTE: brief's name had a ';' ("runs; skips gaps") which is the CMake list
// separator and breaks CTest discovery (Task 4 lesson) -> replaced with ','.
TEST_CASE("R_PlaneSpans emits contiguous (y,x1,x2) runs, skips gaps/unclaimed") {
    int w = 8;
    Visplane pl; pl.minx = 0; pl.maxx = 5;
    pl.top.assign(w, kOpenTop); pl.bottom.assign(w, kOpenBot);
    // columns 0..2 cover rows 1..2; col 3 unclaimed (gap); col 4..5 cover rows 1..2
    for (int x = 0; x <= 2; ++x) { pl.top[x] = 1; pl.bottom[x] = 2; }
    for (int x = 4; x <= 5; ++x) { pl.top[x] = 1; pl.bottom[x] = 2; }
    auto spans = R_PlaneSpans(pl);
    // Expect, per row y=1 and y=2: run [0,2] and run [4,5] => 4 spans
    REQUIRE(spans.size() == 4);
    CHECK(std::get<0>(spans[0]) == 1); CHECK(std::get<1>(spans[0]) == 0); CHECK(std::get<2>(spans[0]) == 2);
    CHECK(std::get<0>(spans[1]) == 1); CHECK(std::get<1>(spans[1]) == 4); CHECK(std::get<2>(spans[1]) == 5);
    CHECK(std::get<0>(spans[2]) == 2); CHECK(std::get<1>(spans[2]) == 0); CHECK(std::get<2>(spans[2]) == 2);
    CHECK(std::get<0>(spans[3]) == 2); CHECK(std::get<1>(spans[3]) == 4); CHECK(std::get<2>(spans[3]) == 5);
}

TEST_CASE("R_PlaneSpans: empty plane (minx>maxx) yields nothing") {
    Visplane pl; pl.minx = 5; pl.maxx = -1;
    pl.top.assign(8, kOpenTop); pl.bottom.assign(8, kOpenBot);
    CHECK(R_PlaneSpans(pl).empty());
}
