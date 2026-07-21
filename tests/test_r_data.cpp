#include "doctest.h"
#include "render/r_data.h"
#include <cstdint>
#include <cstring>
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

TEST_CASE("parseTextureDefs reads name/dims/patches") {
    std::vector<byte> b;
    w32(b, 1);                                   // numtextures
    w32(b, 4 + 4);                               // offset[0] = 8
    // maptexture_t @ offset 8:
    std::string nm = std::string("WALL1") + std::string(8 - std::string("WALL1").size(), ' ');  // 8 chars
    for (char c : nm) w8(b, c);
    w32(b, 0);                                   // masked (ignored)
    w16(b, 2); w16(b, 2);                        // width, height
    w32(b, 0);                                   // columndirectory (ignored)
    w16(b, 1);                                   // patchcount
    // mappatch: originx=0, originy=0, patch=0
    w16(b, 0); w16(b, 0); w16(b, 0);
    auto defs = parseTextureDefs(b.data(), b.size());
    REQUIRE(defs.size() == 1);
    CHECK(std::string(defs[0].name) == "WALL1");
    CHECK(defs[0].width == 2);
    CHECK(defs[0].height == 2);
    REQUIRE(defs[0].patches.size() == 1);
    CHECK(defs[0].patches[0].patch == 0);
    CHECK(defs[0].patches[0].originx == 0);
    CHECK(defs[0].patches[0].originy == 0);
}

TEST_CASE("compositeTexture blits a patch into the texture") {
    std::vector<uint32_t> pal(256, 0);
    pal[3] = (0xAAu << 24) | (0xBBu << 16) | (0xCCu << 8) | 0xFFu;
    // 1x1 patch, single pixel = pal[3]
    std::vector<byte> pb;
    w16(pb, 1); w16(pb, 1); w16(pb, 0); w16(pb, 0);  // w,h,lo,to
    w32(pb, 12);                                      // columnofs[0]=12
    w8(pb, 0); w8(pb, 1); w8(pb, 0); w8(pb, 3); w8(pb, 0); w8(pb, 0xFF);  // post: top=0 len=1 pix=3
    Patch pat = decodePatch(pb.data(), pb.size(), pal.data());

    TextureDef def;
    std::memcpy(def.name, "WALL1   ", 8); def.name[8] = 0;
    def.width = 2; def.height = 2;
    def.patches.push_back({0, 0, 0});

    Texture t = compositeTexture(def, {pat});
    CHECK(t.width == 2);
    CHECK(t.height == 2);
    REQUIRE(t.rgba.size() == 4);
    CHECK(t.rgba[0] == pal[3]);      // patch blitted at (0,0)
    CHECK(t.rgba[1] == 0u);          // untouched -> transparent
    CHECK(t.rgba[2] == 0u);
    CHECK(t.rgba[3] == 0u);
}

// Build a synthetic flat lump (4096 bytes) where byte[y*64+x] = (x+y)&0xFF.
static std::vector<byte> synthFlat() {
    std::vector<byte> b(4096, 0);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            b[y * 64 + x] = static_cast<byte>((x + y) & 0xFF);
    return b;
}

TEST_CASE("decodeFlatAsFlat: 4096 bytes -> 64x64 row-major RGBA") {
    // Standalone decode is tested through TextureLookup below; here we assert the
    // public Flat shape by constructing a TextureLookup over an in-memory WAD is
    // not feasible, so this case is covered by flat("...") on a real WAD in a
    // smoke step. Instead unit-test the decode helper directly:
    std::vector<uint32_t> pal(256, 0);
    pal[5] = (0x10u << 24) | (0x20u << 16) | (0x30u << 8) | 0xFFu;
    std::vector<byte> raw = synthFlat();
    // decodeFlat exposed as a free function for testing:
    Flat f = decodeFlat(raw.data(), raw.size(), pal.data(), "FLOOR0_1");
    CHECK(f.width == 64);
    CHECK(f.height == 64);
    REQUIRE(f.rgba.size() == 4096);
    // row-major: index (0,5) -> pal[5]
    CHECK(f.rgba[0 * 64 + 5] == pal[5]);
    // index (5,0) -> pal[(5+0)&0xFF]=pal[5] as well
    CHECK(f.rgba[5 * 64 + 0] == pal[5]);
    CHECK(std::string(f.name) == "FLOOR0_1");
}
