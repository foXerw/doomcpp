#include "doctest.h"
#include "play/p_setup.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
void w16(std::vector<byte>& v, std::int16_t s) {
    v.push_back(static_cast<byte>(static_cast<uint16_t>(s) & 0xff));
    v.push_back(static_cast<byte>((static_cast<uint16_t>(s) >> 8) & 0xff));
}

// pack an 8-char WAD name (space-padded) into the buffer
void wname(std::vector<byte>& b, const std::string& s) {
    std::string n(8, ' ');
    for (size_t i = 0; i < s.size() && i < 8; ++i) n[i] = s[i];
    for (char c : n) b.push_back(static_cast<byte>(c));
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

TEST_CASE("parseSidedefs reads textures + offsets + sector") {
    std::vector<byte> b;
    w16(b, 16); w16(b, 32);            // textureoffset=16, rowoffset=32
    wname(b, "STARTAN3");              // toptexture
    wname(b, "BROVINE");               // bottomtexture
    wname(b, "MIDGRATE");              // midtexture
    w16(b, 7);                          // sector
    auto s = parseSidedefs(b.data(), b.size());
    CHECK(s.size() == 1);
    CHECK(s[0].textureoffset == 16);
    CHECK(s[0].rowoffset == 32);
    CHECK(std::string(s[0].toptexture) == "STARTAN3");
    CHECK(std::string(s[0].bottomtexture) == "BROVINE");
    CHECK(std::string(s[0].midtexture) == "MIDGRATE");
    CHECK(s[0].sector == 7);
}

TEST_CASE("parseSectors reads heights + pics + lightlevel") {
    std::vector<byte> b;
    w16(b, 0); w16(b, 128);            // floor=0, ceiling=128
    wname(b, "FLOOR4_8");              // floorpic
    wname(b, "CEIL3_5");               // ceilingpic
    w16(b, 160);                        // lightlevel
    w16(b, 0); w16(b, 0);              // special, tag
    auto s = parseSectors(b.data(), b.size());
    CHECK(s.size() == 1);
    CHECK(s[0].floorheight == 0);
    CHECK(s[0].ceilingheight == 128);
    CHECK(std::string(s[0].floorpic) == "FLOOR4_8");
    CHECK(std::string(s[0].ceilingpic) == "CEIL3_5");
    CHECK(s[0].lightlevel == 160);
}

TEST_CASE("parseSegs reads offset field") {
    std::vector<byte> b;
    w16(b, 3); w16(b, 4); w16(b, 90); w16(b, 1); w16(b, 0); w16(b, 42);  // v1,v2,angle,linedef,side,offset
    auto s = parseSegs(b.data(), b.size());
    CHECK(s.size() == 1);
    CHECK(s[0].v1 == 3);
    CHECK(s[0].linedef == 1);
    CHECK(s[0].offset == 42);
}

TEST_CASE("parseBlockmap reads header + cell line lists") {
    // header [orgx=0, orgy=0, width=2, height=2]; cell (0,0) lists lines {5,7}; others empty.
    // lump layout (int16 indices): 0..3 header; 4..7 cell offsets; 8 = empty terminator [-1];
    // 9..11 = list [5, 7, -1].  cell linear index = cy*width+cx; offset index = 4 + linear.
    std::vector<byte> b;
    w16(b, 0);  w16(b, 0);  w16(b, 2);  w16(b, 2);   // header
    w16(b, 9);                                            // cell (0,0) -> offset 9
    w16(b, 8); w16(b, 8); w16(b, 8);                     // cells (1,0),(0,1),(1,1) -> empty
    w16(b, -1);                                           // index 8: empty terminator
    w16(b, 5); w16(b, 7); w16(b, -1);                    // index 9..11: list [5,7,-1]
    Blockmap bm = parseBlockmap(b.data(), b.size());
    CHECK(bm.orgx == 0);
    CHECK(bm.orgy == 0);
    CHECK(bm.width == 2);
    CHECK(bm.height == 2);
    REQUIRE((int)bm.lump.size() == 12);
    CHECK(bm.lump[9] == 5);      // first line index in cell (0,0)'s list
    CHECK(bm.lump[10] == 7);
    CHECK(bm.lump[11] == -1);    // terminator
}

TEST_CASE("parseBlockmap handles negative origin") {
    std::vector<byte> b;
    w16(b, -2000); w16(b, -512); w16(b, 10); w16(b, 8);   // header: origin (-2000,-512), 10x8
    Blockmap bm = parseBlockmap(b.data(), b.size());
    CHECK(bm.orgx == -2000);
    CHECK(bm.orgy == -512);
    CHECK(bm.width == 10);
    CHECK(bm.height == 8);
}
