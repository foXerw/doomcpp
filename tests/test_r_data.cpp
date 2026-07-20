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

TEST_CASE("decodePatch leaves transparent gaps and forces opacity") {
    // palette index 3 has alpha 0x00 — decodePatch must force it to 0xFF (opaque)
    std::vector<uint32_t> pal(256, 0);
    pal[3] = (0xAAu << 24) | (0xBBu << 16) | (0xCCu << 8) | 0x00u;   // alpha is ZERO on purpose

    // 1x3 patch with a GAP row: post topdelta=0 len=1 (row 0), row 1 empty,
    // then post topdelta=2 len=1 (row 2). Row 1 must stay transparent (0).
    std::vector<byte> b;
    w16(b, 1); w16(b, 3); w16(b, 0); w16(b, 0);   // width=1, height=3, leftoffset, topoffset
    w32(b, 12);                                     // columnofs[0] = 8 + 4*1 = 12
    // post A: topdelta=0, length=1, pad, pix=3, pad
    w8(b, 0); w8(b, 1); w8(b, 0); w8(b, 3); w8(b, 0);
    // post B: topdelta=2, length=1, pad, pix=3, pad
    w8(b, 2); w8(b, 1); w8(b, 0); w8(b, 3); w8(b, 0);
    w8(b, 0xFF);                                    // end of column

    Patch p = decodePatch(b.data(), b.size(), pal.data());
    CHECK(p.width == 1);
    CHECK(p.height == 3);
    REQUIRE(p.rgba.size() == 3);
    // rows 0 and 2 decoded from pal[3] with alpha FORCED to 0xFF despite pal[3] having alpha 0x00
    CHECK(p.rgba[0] == (pal[3] | 0xFFu));
    CHECK(p.rgba[2] == (pal[3] | 0xFFu));
    // gap row 1 untouched -> transparent
    CHECK(p.rgba[1] == 0u);
}
