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
