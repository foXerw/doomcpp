#include "doctest.h"
#include "render/r_plane.h"
#include "render/r_data.h"   // Flat
#include <vector>

using namespace std::string_literals;

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
