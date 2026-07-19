#include "doctest.h"
#include "play/p_setup.h"
#include <cstdint>
#include <vector>

namespace {
void w16(std::vector<byte>& v, std::int16_t s) {
    v.push_back(static_cast<byte>(static_cast<uint16_t>(s) & 0xff));
    v.push_back(static_cast<byte>((static_cast<uint16_t>(s) >> 8) & 0xff));
}
}

TEST_CASE("parseVertexes reads LE int16 pairs into fixed_t") {
    std::vector<byte> buf;
    w16(buf, 10);  w16(buf, -20);
    w16(buf, 300); w16(buf, 400);
    auto v = parseVertexes(buf.data(), buf.size());
    CHECK(v.size() == 2);
    CHECK(v[0].x == 10 << 16);
    CHECK(v[0].y == (-20) << 16);
    CHECK(v[1].x == 300 << 16);
}

TEST_CASE("parseLinedefs reads 14-byte records") {
    std::vector<byte> buf;
    w16(buf, 0); w16(buf, 1); w16(buf, 1); w16(buf, 0); w16(buf, 0);
    w16(buf, 0); w16(buf, -1);
    auto l = parseLinedefs(buf.data(), buf.size());
    CHECK(l.size() == 1);
    CHECK(l[0].v1 == 0);
    CHECK(l[0].v2 == 1);
    CHECK(l[0].flags == 1);
    CHECK(l[0].sidenum[0] == 0);
    CHECK(l[0].sidenum[1] == -1);
}

TEST_CASE("parseSectors reads 26-byte records") {
    std::vector<byte> b;
    w16(b, 0); w16(b, 128);                       // floor=0, ceil=128
    for (int i = 0; i < 22; ++i) b.push_back(0);  // textures+light+special+tag
    auto s = parseSectors(b.data(), b.size());
    CHECK(s.size() == 1);
    CHECK(s[0].floorheight == 0);
    CHECK(s[0].ceilingheight == 128);
}

TEST_CASE("parseSegs reads 12-byte records") {
    std::vector<byte> b;
    w16(b, 3); w16(b, 4); w16(b, 0); w16(b, 1); w16(b, 0); w16(b, 0);
    auto s = parseSegs(b.data(), b.size());
    CHECK(s.size() == 1);
    CHECK(s[0].v1 == 3);
    CHECK(s[0].linedef == 1);
}

TEST_CASE("parseNodes reads 28-byte records + children flags") {
    std::vector<byte> b;
    w16(b, 10); w16(b, 20); w16(b, 5); w16(b, -5);
    for (int i = 0; i < 16; ++i) b.push_back(0);     // bbox
    b.push_back(0x34); b.push_back(0x12);            // child0 = 0x1234
    b.push_back(0x00); b.push_back(0x80);            // child1 = 0x8000 (NF_SUBSECTOR)
    auto n = parseNodes(b.data(), b.size());
    CHECK(n.size() == 1);
    CHECK(n[0].x == 10);
    CHECK(n[0].dx == 5);
    CHECK(n[0].children[0] == 0x1234);
    CHECK((n[0].children[1] & 0x8000) != 0);
}
