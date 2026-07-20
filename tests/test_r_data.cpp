#include "doctest.h"
#include "render/r_data.h"
#include <cstdint>
#include <vector>

using byte = std::uint8_t;
static void w8 (std::vector<byte>& b, unsigned v) { b.push_back(static_cast<byte>(v)); }
static void w16(std::vector<byte>& b, int v) {
    b.push_back(static_cast<byte>(v & 0xFF));
    b.push_back(static_cast<byte>((v >> 8) & 0xFF));
}
static void w32(std::vector<byte>& b, int v) {
    b.push_back(static_cast<byte>(v & 0xFF));
    b.push_back(static_cast<byte>((v >> 8) & 0xFF));
    b.push_back(static_cast<byte>((v >> 16) & 0xFF));
    b.push_back(static_cast<byte>((v >> 24) & 0xFF));
}

TEST_CASE("parsePnames reads count + names") {
    std::vector<byte> b;
    w32(b, 2);                                   // count
    std::string n1 = "WALL1", n2 = "WALL2";
    std::string a = n1 + std::string(8 - n1.size(), ' ');
    std::string c = n2 + std::string(8 - n2.size(), ' ');
    for (char ch : a) w8(b, ch);
    for (char ch : c) w8(b, ch);
    auto p = parsePnames(b.data(), b.size());
    CHECK(p.size() == 2);
    CHECK(p[0] == "WALL1");
    CHECK(p[1] == "WALL2");
}

TEST_CASE("decodePatch decodes a 1x2 column with transparent gaps") {
    // palette: index 3 = opaque red (R=0xAA,G=0xBB,B=0xCC -> RGBA (R<<24)|(G<<16)|(B<<8)|A)
    std::vector<uint32_t> pal(256, 0);
    pal[3] = (0xAAu << 24) | (0xBBu << 16) | (0xCCu << 8) | 0xFFu;

    std::vector<byte> b;
    w16(b, 1); w16(b, 2); w16(b, 0); w16(b, 0);  // width,height,leftoffset,topoffset
    w32(b, 12);                                   // columnofs[0] = 8 + 4*1 = 12
    // column 0 @ offset 12: topdelta=0,length=2,pad,pix,pix,pad, 0xFF
    w8(b, 0); w8(b, 2); w8(b, 0); w8(b, 3); w8(b, 3); w8(b, 0); w8(b, 0xFF);

    Patch p = decodePatch(b.data(), b.size(), pal.data());
    CHECK(p.width == 1);
    CHECK(p.height == 2);
    REQUIRE(p.rgba.size() == 2);
    CHECK(p.rgba[0] == pal[3]);
    CHECK(p.rgba[1] == pal[3]);
}
