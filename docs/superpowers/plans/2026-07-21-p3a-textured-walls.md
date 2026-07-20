# P3a · 贴图管线 + 贴图墙 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Replace P2b's solid-grey wall renderer with **fully textured walls** (upper/middle/lower) on E1M1 — decode the DOOM picture format, composite wall textures at load, and render perspective-correct textured columns with sector-light modulation.

**Architecture:** New `render/r_data` owns the texture pipeline: `PLAYPAL` → palette, `PNAMES` → patch names, patch lumps → decoded RGBA `Patch`es, `TEXTURE1/2` → composited `Texture`s (full-brightness RGBA, alpha=0 for gaps). `p_setup` structs grow texture-name/offset/lightlevel fields. `r_bsp` swaps P2b's single `occluded[]` bool for per-column `ceilingClip[]`/`floorClip[]`, computes perspective-correct U (per-column ray-line `t`) and V (invert projection), and draws upper/middle/lower parts with per-pixel `lightlevel/255` modulation. `main` builds one `TextureLookup` and feeds it to `R_RenderView` (window + `--dumpframe`).

**Faithfulness notes:** Picture/PNAMES/TEXTURE1 formats follow `r_data.c` byte-for-byte. **Render math stays FLOAT** (P2 simplification; data on disk stays fixed-point). Wall texture mapping is **1 map unit = 1 texel** (DOOM convention), so U/V come straight from world distances. Full 32-level COLORMAP, distance fog, animated textures, flats/visplanes, and collision are out of scope (later phases).

**Tech Stack:** C++17, doctest, MSVC via CMake (unchanged).

## Global Constraints

- On-disk structs are LE `short` (`doomdata.h`); parse with `rd_i16`. Framebuffer is RGBA8888 = `(R<<24)|(G<<16)|(B<<8)|A`; opaque black = `0x000000FF` (NOT `0xFF000000`).
- mapsidedef_t (30 B): `textureoffset` i16 @+0, `rowoffset` i16 @+2, `toptexture` 8B @+4, `bottomtexture` 8B @+12, `midtexture` 8B @+20, `sector` i16 @+28.
- mapsector_t (26 B): `floorheight` i16 @+0, `ceilingheight` i16 @+2, `floorpic` 8B @+4, `ceilingpic` 8B @+12, `lightlevel` i16 @+20, `special` i16 @+22, `tag` i16 @+24.
- seg_t (12 B): `v1` @+0, `v2` @+2, `angle` @+4, `linedef` @+6, `side` @+8, `offset` i16 @+10.
- Picture (patch): 8B header (`width`/`height`/`leftoffset`/`topoffset` i16) + `width`× i32 column offsets; each column = posts `topdelta`(u8),`length`(u8),`[pad]`(u8),`length`×palidx,`[pad]`(u8); `topdelta==0xFF` ends column.
- PNAMES: i32 `count` + `count`×8B names. TEXTURE1/2: i32 `numtextures` + `numtextures`×i32 offsets → each maptexture_t: `name`8, `masked`i32(ignore), `width`i16, `height`i16, `columndirectory`i32(ignore), `patchcount`i16, then `patchcount`× `originx`i16/`originy`i16/`patch`i16(PNAMES idx).
- PLAYPAL: 256×3 RGB (use first set).
- Units: **1 map unit = 1 texel** for walls. Vertices stored fixed (`v>>16` → float map units). `seg.offset`+`side.textureoffset` (both already texel-scale i16) form the U base; world distance along seg adds to U.
- Camera (unchanged from P2a/P2b): forward `F=(sin a, cos a)`, right `R=(cos a, -sin a)`. `depth = dx*sin+dy*cos`, `right = dx*cos-dy*sin` where `dx=(vx>>16)-px`. `screen_x = W/2 + focal*right/depth`, `screen_y = H/2 - focal*(z-eyeZ)/depth`, `focal = W/2`. Eye height = 41 (fixed until P3c).
- Texture name lookup is case-insensitive (WAD names are upper-case ASCII, space-padded).
- Rendering has no unit tests; verify via `--dumpframe` → BMP → PowerShell `System.Drawing` → PNG → `Read`/`analyze_image`. Pure parsers (p_setup, r_data) are unit-tested with crafted buffers (no WAD).
- One commit per task; proxy + vcpkg toolchain for builds. Build/test commands:
  ```bash
  export http_proxy=http://127.0.0.1:7890 https_proxy=http://127.0.0.1:7890
  CMAKE="/c/Users/User/cmake-4.4.0-windows-x86_64/bin/cmake.exe"
  "$CMAKE" --build build --config Release
  "$CMAKE" --build build --config Release --target RUN_TESTS
  ```

## File Structure (additions)

```
src/render/r_data.h / .cpp    NEW — palette, PNAMES, patch decode, TEXTURE1/2 parse, composite, TextureLookup (doomcore, SDL-free)
src/play/p_setup.h / .cpp     side_t/sector_t/seg_t extended + parsers updated
src/render/r_bsp.h / .cpp     R_RenderView + TextureLookup param; textured wall renderer; ceilingClip/floorClip
src/main.cpp                  build TextureLookup, pass to R_RenderView (--dumpframe + window)
tests/test_r_data.cpp         NEW — parsePnames/decodePatch/parseTextureDefs/compositeTexture
tests/test_p_setup.cpp        +sidedef/sector/seg extended cases
CMakeLists.txt                doomcore += src/render/r_data.cpp
```

---

## Task 1: Extend map structs + parsers (sidedef textures, sector lightlevel, seg offset)

**Files:**
- Modify: `src/play/p_setup.h`, `src/play/p_setup.cpp`
- Modify: `tests/test_p_setup.cpp`

**Interfaces:**
- Consumes: existing `parseSidedefs`/`parseSectors`/`parseSegs` (replaced).
- Produces:
  - `struct side_t { int textureoffset, rowoffset; char toptexture[9], bottomtexture[9], midtexture[9]; int sector; };`
  - `struct sector_t { int floorheight, ceilingheight; char floorpic[9], ceilingpic[9]; int lightlevel, special, tag; };`
  - `struct seg_t { int v1, v2, linedef, side, offset, frontsector, backsector; };` (added `offset`)
  - Parsers still return `std::vector<...>`; signature unchanged.

- [ ] **Step 1: Add failing cases to `tests/test_p_setup.cpp`**

Append (the file already has a `w16(std::vector<byte>&, int)` helper and `using byte = std::uint8_t;` — reuse them; add a name helper if absent):

```cpp
#include <cstring>

// pack an 8-char WAD name (space-padded) into the buffer
static void wname(std::vector<byte>& b, const std::string& s) {
    std::string n(8, ' ');
    for (size_t i = 0; i < s.size() && i < 8; ++i) n[i] = s[i];
    for (char c : n) b.push_back(static_cast<byte>(c));
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
```

- [ ] **Step 2: Run, expect fail** — `parseSidedefs` etc. don't populate the new fields / `offset` missing.

```bash
"$CMAKE" --build build --config Release --target RUN_TESTS
```

- [ ] **Step 3: Extend structs in `src/play/p_setup.h`**

Replace the three struct definitions and keep the parser declarations (signatures unchanged):

```cpp
struct vertex_t    { fixed_t x, y; };
struct line_t      { int v1, v2, flags, special, tag; int sidenum[2]; };
struct side_t      {
    int  textureoffset;          // mapsidedef_t +0 (texel units)
    int  rowoffset;              // +2
    char toptexture[9];          // +4  (8 chars + NUL)
    char bottomtexture[9];       // +12
    char midtexture[9];          // +20
    int  sector;                 // +28
};
struct sector_t    {
    int  floorheight, ceilingheight;   // +0 / +2
    char floorpic[9], ceilingpic[9];   // +4 / +12
    int  lightlevel, special, tag;     // +20 / +22 / +24
};
struct seg_t       { int v1, v2, linedef, side, offset, frontsector, backsector; };
struct subsector_t { int segcount, firstseg; };
struct node_t      { float x, y, dx, dy; uint32_t children[2]; };
struct thing_t     { int x, y, angleDeg, type; };
```

- [ ] **Step 4: Extend parsers in `src/play/p_setup.cpp`**

Add a name-copy helper near `rd_i16` and rewrite the three parsers:

```cpp
namespace {
// ...
static void rd_name(const byte* p, char out[9]) {
    for (int i = 0; i < 8; ++i) out[i] = static_cast<char>(p[i]);
    out[8] = '\0';
    // trim trailing spaces so std::string compares are clean
    for (int i = 7; i >= 0 && out[i] == ' '; --i) out[i] = '\0';
}
}
```

```cpp
std::vector<side_t> parseSidedefs(const byte* d, size_t n) {
    if (n % 30) I_Error("parseSidedefs: bad size");
    std::vector<side_t> out(n / 30);
    for (size_t i = 0; i < out.size(); ++i) {
        const byte* p = d + i * 30;
        out[i].textureoffset = rd_i16(p);
        out[i].rowoffset     = rd_i16(p + 2);
        rd_name(p + 4,  out[i].toptexture);
        rd_name(p + 12, out[i].bottomtexture);
        rd_name(p + 20, out[i].midtexture);
        out[i].sector = rd_i16(p + 28);
    }
    return out;
}

std::vector<sector_t> parseSectors(const byte* d, size_t n) {
    if (n % 26) I_Error("parseSectors: bad size");
    std::vector<sector_t> out(n / 26);
    for (size_t i = 0; i < out.size(); ++i) {
        const byte* p = d + i * 26;
        out[i].floorheight   = rd_i16(p);
        out[i].ceilingheight = rd_i16(p + 2);
        rd_name(p + 4,  out[i].floorpic);
        rd_name(p + 12, out[i].ceilingpic);
        out[i].lightlevel = rd_i16(p + 20);
        out[i].special    = rd_i16(p + 22);
        out[i].tag        = rd_i16(p + 24);
    }
    return out;
}

std::vector<seg_t> parseSegs(const byte* d, size_t n) {
    if (n % 12) I_Error("parseSegs: bad size");
    std::vector<seg_t> out(n / 12);
    for (size_t i = 0; i < out.size(); ++i) {
        const byte* p = d + i * 12;
        out[i].v1 = rd_i16(p);
        out[i].v2 = rd_i16(p + 2);
        out[i].linedef = rd_i16(p + 6);
        out[i].side    = rd_i16(p + 8);
        out[i].offset  = rd_i16(p + 10);
        out[i].frontsector = -1;   // resolved in loadMap
        out[i].backsector  = -1;
    }
    return out;
}
```

- [ ] **Step 5: Build + run tests.** Expect the 3 new cases pass and the existing 21 still pass (24 total).

```bash
"$CMAKE" --build build --config Release --target RUN_TESTS
```

- [ ] **Step 6: Commit** — `feat(play): parse sidedef textures, sector lightlevel, seg offset`

---

## Task 2: Texture pipeline — palette, PNAMES, patch decode

**Files:**
- Create: `src/render/r_data.h`, `src/render/r_data.cpp`
- Create: `tests/test_r_data.cpp`
- Modify: `CMakeLists.txt` (`doomcore` += `src/render/r_data.cpp`), `tests/CMakeLists.txt` (+= `test_r_data.cpp`)

**Interfaces:**
- Consumes: `WadFile::checkNumForName` / `readLump` (used by `TextureLookup` in Task 3).
- Produces (free functions, all SDL-free, unit-testable):
  - `struct Patch { int width=0, height=0; std::vector<uint32_t> rgba; };`  (`rgba` width*height; alpha 0 = transparent)
  - `std::vector<std::string> parsePnames(const byte* d, size_t n);`
  - `Patch decodePatch(const byte* data, size_t n, const uint32_t* palette);`  (`palette` = 256 RGBA entries)

- [ ] **Step 1: Create `tests/test_r_data.cpp` with failing cases**

```cpp
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
```

- [ ] **Step 2: Run, expect fail** (no `parsePnames` / `decodePatch`).

- [ ] **Step 3: Create `src/render/r_data.h`**

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "core/doomtype.h"

class WadFile;   // fwd

// Decoded picture (column-major source format flattened to w*h RGBA).
struct Patch { int width = 0, height = 0; std::vector<uint32_t> rgba; };

// A composited wall texture (full-brightness RGBA; alpha 0 = no patch there).
struct Texture { char name[9] = {0}; int width = 0, height = 0; std::vector<uint32_t> rgba; };

// PNAMES: count + 8-byte names.
std::vector<std::string> parsePnames(const byte* d, size_t n);

// Decode a patch lump to RGBA using a 256-entry palette (each = (R<<24)|(G<<16)|(B<<8)|A).
Patch decodePatch(const byte* data, size_t n, const uint32_t* palette);

class TextureLookup {
public:
    explicit TextureLookup(const WadFile& wad);
    // Case-insensitive; nullptr if no such wall texture.
    const Texture* wall(const std::string& name) const;
private:
    std::array<uint32_t, 256> palette_{};
    std::unordered_map<std::string, int> wallIndex_;
    std::vector<Texture> walls_;
};
```

Note: add `#include <unordered_map>` to the header (for `wallIndex_`).

- [ ] **Step 4: Create `src/render/r_data.cpp` with `parsePnames` + `decodePatch` only** (Task 3 adds the rest):

```cpp
#include "r_data.h"
#include "core/i_system.h"
#include "wad/wadfile.h"
#include <cstring>

namespace {
std::int16_t rd_i16(const byte* p) {
    return static_cast<std::int16_t>(
        static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}
std::int32_t rd_i32(const byte* p) {
    return static_cast<std::int32_t>(
        static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
}
}

std::vector<std::string> parsePnames(const byte* d, size_t n) {
    if (n < 4) I_Error("parsePnames: too small");
    int count = rd_i32(d);
    std::vector<std::string> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        const byte* p = d + 4 + i * 8;
        if (p + 8 > d + n) I_Error("parsePnames: truncated");
        char name[9];
        std::memcpy(name, p, 8);
        name[8] = '\0';
        for (int k = 7; k >= 0 && name[k] == ' '; --k) name[k] = '\0';
        out.emplace_back(name);
    }
    return out;
}

Patch decodePatch(const byte* data, size_t n, const uint32_t* palette) {
    Patch patch;
    if (n < 8) return patch;
    int width  = rd_i16(data);
    int height = rd_i16(data + 2);
    patch.width = width;
    patch.height = height;
    patch.rgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0u);  // transparent
    for (int col = 0; col < width; ++col) {
        const byte* base = data + 8;
        if (base + 4 * col + 4 > data + n) break;
        int ofs = rd_i32(base + 4 * col);
        if (ofs <= 0 || ofs >= static_cast<int>(n)) continue;
        const byte* p = data + ofs;
        while (p < data + n) {
            byte topdelta = *p++;
            if (topdelta == 0xFF) break;
            if (p + 1 > data + n) break;
            byte length = *p++;
            if (p < data + n) ++p;            // unused pad
            for (byte i = 0; i < length; ++i) {
                if (p >= data + n) break;
                byte pix = *p++;
                int row = topdelta + i;
                if (row >= 0 && row < height)
                    patch.rgba[static_cast<size_t>(row) * width + col] = palette[pix] | 0xFFu;
            }
            if (p < data + n) ++p;            // unused pad
        }
    }
    return patch;
}
```

- [ ] **Step 5: Wire into CMake.** Add `src/render/r_data.cpp` to the `doomcore` sources in `CMakeLists.txt`, and `test_r_data.cpp` to `tests/CMakeLists.txt` (mirror how `test_r_draw.cpp` is wired).

`CMakeLists.txt` `doomcore` list — add after `src/render/r_draw.cpp`:
```cmake
    src/render/r_data.cpp
```

`tests/CMakeLists.txt` — add `test_r_data.cpp` to the test sources list exactly as `test_r_draw.cpp` appears.

- [ ] **Step 6: Build + run tests.** Expect the 2 new `test_r_data` cases pass; `TextureLookup` is declared but not yet defined — that's fine (Task 3 implements it; do not construct it yet).

```bash
"$CMAKE" --build build --config Release --target RUN_TESTS
```
(If the link fails because `TextureLookup::TextureLookup`/`wall` are referenced but undefined — they aren't called yet, so this builds. If your compiler emits an undefined-symbol error for the constructor, temporarily add empty stubs `TextureLookup::TextureLookup(const WadFile&){} const Texture* TextureLookup::wall(const std::string&)const{return nullptr;}` and replace them in Task 3.)

- [ ] **Step 7: Commit** — `feat(render): decode PNAMES + patch picture format (r_data)`

---

## Task 3: Texture pipeline — TEXTURE1 parse, composite, TextureLookup

**Files:**
- Modify: `src/render/r_data.h`, `src/render/r_data.cpp`
- Modify: `tests/test_r_data.cpp`

**Interfaces:**
- Produces:
  - `struct Mappatch { int originx, originy, patch; };`  (`patch` = PNAMES index)
  - `struct TextureDef { char name[9]; int width, height; std::vector<Mappatch> patches; };`
  - `std::vector<TextureDef> parseTextureDefs(const byte* d, size_t n);`  (one call per TEXTURE1/TEXTURE2)
  - `Texture compositeTexture(const TextureDef& def, const std::vector<Patch>& patches);`  (`patches[i]` = decoded patch for PNAMES index `i`; missing → `Patch{0,0,{}}`)
  - `TextureLookup` ctor builds `palette_` (PLAYPAL), parses PNAMES + TEXTURE1/2, decodes referenced patches, composites walls.

- [ ] **Step 1: Add failing cases to `tests/test_r_data.cpp`**

```cpp
TEST_CASE("parseTextureDefs reads name/dims/patches") {
    std::vector<byte> b;
    w32(b, 1);                                   // numtextures
    w32(b, 4 + 4);                               // offset[0] = 8
    // maptexture_t @ offset 8:
    std::string nm = std::string("WALL1") + std::string(4, ' ');  // 8 chars
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
```

- [ ] **Step 2: Run, expect fail** (no `parseTextureDefs` / `compositeTexture`).

- [ ] **Step 3: Add types + decls to `src/render/r_data.h`** (insert above `class TextureLookup`):

```cpp
struct Mappatch   { int originx, originy, patch; };          // patch = PNAMES index
struct TextureDef { char name[9] = {0}; int width = 0, height = 0; std::vector<Mappatch> patches; };

std::vector<TextureDef> parseTextureDefs(const byte* d, size_t n);  // one TEXTURE1/2 lump
Texture compositeTexture(const TextureDef& def, const std::vector<Patch>& patches);  // patches[i] = PNAMES idx i
```

- [ ] **Step 4: Implement `parseTextureDefs` + `compositeTexture` in `src/render/r_data.cpp`** (append; keep the empty `TextureLookup` stubs if present — they're replaced below):

```cpp
std::vector<TextureDef> parseTextureDefs(const byte* d, size_t n) {
    if (n < 4) I_Error("parseTextureDefs: too small");
    int num = rd_i32(d);
    std::vector<TextureDef> out;
    out.reserve(num);
    for (int i = 0; i < num; ++i) {
        if (d + 4 + (i + 1) * 4 > d + n) I_Error("parseTextureDefs: truncated offsets");
        int ofs = rd_i32(d + 4 + i * 4);
        const byte* p = d + ofs;
        if (p + 22 > d + n) I_Error("parseTextureDefs: truncated maptexture");
        // maptexture_t: name(8)@0, masked(4)@8[ignore], width(2)@12, height(2)@14,
        // columndirectory(4)@16[ignore], patchcount(2)@20, mappatches@22.
        TextureDef def;
        std::memcpy(def.name, p, 8); def.name[8] = '\0';
        for (int k = 7; k >= 0 && def.name[k] == ' '; --k) def.name[k] = '\0';
        def.width  = rd_i16(p + 12);
        def.height = rd_i16(p + 14);
        int patchcount = rd_i16(p + 20);
        const byte* pp = p + 22;
        for (int j = 0; j < patchcount; ++j) {
            if (pp + 6 > d + n) I_Error("parseTextureDefs: truncated mappatch");
            Mappatch mp;
            mp.originx = rd_i16(pp);
            mp.originy = rd_i16(pp + 2);
            mp.patch   = rd_i16(pp + 4);
            def.patches.push_back(mp);
            pp += 6;
        }
        out.push_back(std::move(def));
    }
    return out;
}
```

(maptexture_t offsets are name@0, masked@8[ignored], width@12, height@14, columndirectory@16[ignored], patchcount@20, mappatches@22 — matches the test buffer in Step 1.)

```cpp
Texture compositeTexture(const TextureDef& def, const std::vector<Patch>& patches) {
    Texture t;
    std::memcpy(t.name, def.name, 9);
    t.width = def.width;
    t.height = def.height;
    t.rgba.assign(static_cast<size_t>(def.width) * static_cast<size_t>(def.height), 0u);
    for (const Mappatch& mp : def.patches) {
        if (mp.patch < 0 || mp.patch >= static_cast<int>(patches.size())) continue;
        const Patch& pat = patches[mp.patch];
        for (int y = 0; y < pat.height; ++y) {
            for (int x = 0; x < pat.width; ++x) {
                int tx = mp.originx + x;
                int ty = mp.originy + y;
                if (tx < 0 || tx >= def.width || ty < 0 || ty >= def.height) continue;
                uint32_t src = pat.rgba[static_cast<size_t>(y) * pat.width + x];
                if (!(src & 0xFFu)) continue;                 // transparent -> skip
                t.rgba[static_cast<size_t>(ty) * def.width + tx] = src;
            }
        }
    }
    return t;
}
```

> maptexture_t layout used: `name`8(@0), `masked`i32(@8, ignored), `width`i16(@8+4=12)... **Wait** — recheck offsets. The on-disk order is `name`(8) @0, `masked`(4) @8, `width`(2) @12, `height`(2) @14, `columndirectory`(4) @16, `patchcount`(2) @20, patches @22. The code above reads `width` at p+8 and `height` at p+10 — **that is wrong**; it must be p+12 / p+14. Fix `parseTextureDefs` to read `def.width = rd_i16(p + 12); def.height = rd_i16(p + 14);` (patchcount stays at p+20, patches at p+22). The failing test in Step 1 writes width/height immediately after `masked`, so it expects p+12/p+14 — keep the test as written and fix the parser offsets to match. (This note is intentional: get the maptexture offsets right.)

- [ ] **Step 5: Implement `TextureLookup` ctor + `wall`** (replace any stubs). Add `#include <cctype>` and a case-fold helper:

```cpp
namespace {
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}

TextureLookup::TextureLookup(const WadFile& wad) {
    // PLAYPAL -> palette_ (first 256 entries; RGB -> RGBA opaque)
    auto pal = const_cast<WadFile&>(wad).readLumpByName("PLAYPAL");
    for (int i = 0; i < 256; ++i) {
        if (static_cast<size_t>(i * 3 + 2) < pal.size())
            palette_[i] = (static_cast<uint32_t>(pal[i * 3]) << 24) |
                          (static_cast<uint32_t>(pal[i * 3 + 1]) << 16) |
                          (static_cast<uint32_t>(pal[i * 3 + 2]) << 8) | 0xFFu;
        else
            palette_[i] = 0u;
    }

    // PNAMES
    auto pn = const_cast<WadFile&>(wad).readLumpByName("PNAMES");
    std::vector<std::string> pnames = pn.empty() ? std::vector<std::string>{} : parsePnames(pn.data(), pn.size());

    // decode every patch referenced by TEXTURE1 (+TEXTURE2), one decode each
    std::vector<Patch> patches(pnames.size());
    auto ensurePatch = [&](int idx) {
        if (idx < 0 || idx >= static_cast<int>(patches.size())) return;
        if (patches[idx].width != 0 || patches[idx].height != 0) return;   // decoded
        int lump = const_cast<WadFile&>(wad).checkNumForName(pnames[idx]);
        if (lump < 0) { patches[idx] = Patch{0, 0, {}}; return; }
        auto raw = const_cast<WadFile&>(wad).readLump(lump);
        patches[idx] = raw.empty() ? Patch{0, 0, {}} : decodePatch(raw.data(), raw.size(), palette_.data());
    };

    auto loadLumpDefs = [&](const char* lumpName) {
        int lump = const_cast<WadFile&>(wad).checkNumForName(lumpName);
        if (lump < 0) return std::vector<TextureDef>{};
        auto raw = const_cast<WadFile&>(wad).readLump(lump);
        return raw.empty() ? std::vector<TextureDef>{} : parseTextureDefs(raw.data(), raw.size());
    };

    for (TextureDef def : loadLumpDefs("TEXTURE1")) {
        for (const Mappatch& mp : def.patches) ensurePatch(mp.patch);
        walls_.push_back(compositeTexture(def, patches));
    }
    for (TextureDef def : loadLumpDefs("TEXTURE2")) {
        for (const Mappatch& mp : def.patches) ensurePatch(mp.patch);
        walls_.push_back(compositeTexture(def, patches));
    }
    for (int i = 0; i < static_cast<int>(walls_.size()); ++i) wallIndex_[upper(walls_[i].name)] = i;
}

const Texture* TextureLookup::wall(const std::string& name) const {
    auto it = wallIndex_.find(upper(name));
    return it == wallIndex_.end() ? nullptr : &walls_[it->second];
}
```

(`#include <unordered_map>` is already in the header for `wallIndex_`.)

- [ ] **Step 6: Build + run tests.** Expect the 2 new cases pass (total 28).

```bash
"$CMAKE" --build build --config Release --target RUN_TESTS
```

- [ ] **Step 7: Commit** — `feat(render): parse TEXTURE1, composite wall textures, TextureLookup`

---

## Task 4: Textured one-sided walls + occlusion + light + wire TextureLookup

**Files:**
- Modify: `src/render/r_bsp.h`, `src/render/r_bsp.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `TextureLookup::wall`, extended `side_t`/`sector_t`/`seg_t`.
- Produces: `void R_RenderView(uint32_t* fb, int w, int h, const MapData& map, const TextureLookup& tex, float px, float py, float ang, float eyeZ);`  (added `tex` param).

- [ ] **Step 1: Update `src/render/r_bsp.h`** — add the `tex` param and include:

```cpp
#pragma once
#include "core/doomtype.h"

struct MapData;
class TextureLookup;

// Render one frame of textured walls (BSP front-to-back, per-column ceilingClip/floorClip occlusion).
// Two-sided openings render as black until P3b visplanes.
void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  const TextureLookup& tex, float px, float py, float ang, float eyeZ);
```

- [ ] **Step 2: Rewrite `src/render/r_bsp.cpp`** with the textured renderer (one-sided middle texture this task; two-sided added in Task 5):

```cpp
#include "r_bsp.h"
#include "r_draw.h"
#include "r_data.h"
#include "play/p_setup.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
constexpr uint32_t NF_SUBSECTOR = 0x8000;

struct Cam {
    float px, py, sin, cos, eyeZ, focal;
    int w, h;
    uint32_t* fb;
    std::vector<int> ceilingClip;    // filled-down-from-top row; init -1
    std::vector<int> floorClip;      // filled-up-from-bottom row; init h
    const TextureLookup* tex;
};

int pointOnSide(float x, float y, const node_t& n) {
    float dx = x - n.x, dy = y - n.y;
    float left = n.dy * dx, right = n.dx * dy;
    return (right < left) ? 0 : 1;
}

// Draw one textured wall column over world-z span [zTop, zBot] (zTop>zBot) at screen column x.
// solid: spans full opening (one-sided). upper/lower: ceiling/floor-step walls. Updates clips for opaque parts.
void drawCol(Cam& c, int x, float scale, const Texture& T, float U, int rowoffset,
             int zTop, int zBot, int light, bool solid, bool upper, bool lower) {
    float syTop = c.h / 2.0f - c.focal * (zTop - c.eyeZ) * scale;   // smaller y (wall top)
    float syBot = c.h / 2.0f - c.focal * (zBot - c.eyeZ) * scale;   // larger y  (wall bottom)
    int yA = std::max(static_cast<int>(std::ceil(syTop)),  c.ceilingClip[x] + 1);
    int yB = std::min(static_cast<int>(std::floor(syBot)), c.floorClip[x] - 1);
    float lightf = std::clamp(light / 255.0f, 0.0f, 1.0f);
    for (int y = yA; y <= yB; ++y) {
        float z = c.eyeZ + (c.h / 2.0f - y) / (c.focal * scale);     // invert projection -> world z
        float V = (z - zTop) + rowoffset;                            // 1 map unit = 1 texel
        int vi = static_cast<int>(std::floor(V)); vi %= T.height; if (vi < 0) vi += T.height;
        int ui = static_cast<int>(std::floor(U));   ui %= T.width;  if (ui < 0) ui += T.width;
        uint32_t texel = T.rgba[static_cast<size_t>(vi) * T.width + ui];
        if (!(texel & 0xFFu)) continue;                              // transparent
        uint8_t r = static_cast<uint8_t>((texel >> 24 & 0xFF) * lightf);
        uint8_t g = static_cast<uint8_t>((texel >> 16 & 0xFF) * lightf);
        uint8_t b = static_cast<uint8_t>((texel >> 8  & 0xFF) * lightf);
        c.fb[static_cast<size_t>(y) * c.w + x] =
            (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
            (static_cast<uint32_t>(b) << 8) | 0xFFu;
    }
    if (solid || upper) c.ceilingClip[x] = std::max(c.ceilingClip[x], std::min(static_cast<int>(std::floor(syBot)), c.h - 1));
    if (solid || lower) c.floorClip[x]   = std::min(c.floorClip[x],   std::max(static_cast<int>(std::ceil(syTop)),  0));
}

void renderSeg(Cam& c, const MapData& m, const seg_t& sg) {
    if (sg.v1 < 0 || sg.v1 >= static_cast<int>(m.vertices.size())) return;
    if (sg.v2 < 0 || sg.v2 >= static_cast<int>(m.vertices.size())) return;
    if (sg.frontsector < 0 || sg.frontsector >= static_cast<int>(m.sectors.size())) return;

    // original camera-space endpoints (used for perspective-correct U/scale)
    auto toCam = [&](fixed_t vx, fixed_t vy, float& depth, float& right) {
        float dx = static_cast<float>(vx >> 16) - c.px;
        float dy = static_cast<float>(vy >> 16) - c.py;
        depth = dx * c.sin + dy * c.cos;
        right = dx * c.cos - dy * c.sin;
    };
    float d0o, r0o, d1o, r1o;
    toCam(m.vertices[sg.v1].x, m.vertices[sg.v1].y, d0o, r0o);
    toCam(m.vertices[sg.v2].x, m.vertices[sg.v2].y, d1o, r1o);

    // seg world length (map units = texels)
    float ax = static_cast<float>(m.vertices[sg.v2].x >> 16) - (m.vertices[sg.v1].x >> 16);
    float ay = static_cast<float>(m.vertices[sg.v2].y >> 16) - (m.vertices[sg.v1].y >> 16);
    float segLen = std::sqrt(ax * ax + ay * ay);

    // near-clip a copy to find the visible column range [x0,x1] (P2b logic)
    float d0 = d0o, r0 = r0o, d1 = d1o, r1 = r1o;
    const float nearZ = 0.01f;
    if (d0 <= nearZ && d1 <= nearZ) return;
    if (d0 <= nearZ) { float t = (nearZ - d0) / (d1 - d0); d0 = nearZ; r0 += (r1 - r0) * t; }
    else if (d1 <= nearZ) { float t = (nearZ - d1) / (d0 - d1); d1 = nearZ; r1 += (r0 - r1) * t; }
    int x0 = static_cast<int>(c.w / 2 + c.focal * r0 / d0);
    int x1 = static_cast<int>(c.w / 2 + c.focal * r1 / d1);
    if (x0 > x1) std::swap(x0, x1);
    if (x1 < 0 || x0 >= c.w) return;
    x0 = std::max(0, x0); x1 = std::min(c.w - 1, x1);

    // resolve side + textures + sector
    const auto& L = m.lines[sg.linedef];
    int sideIdx = (sg.side == 0 || sg.side == 1) ? L.sidenum[sg.side] : -1;
    if (sideIdx < 0 || sideIdx >= static_cast<int>(m.sides.size())) return;
    const auto& sd = m.sides[sideIdx];
    const auto& sec = m.sectors[sg.frontsector];
    int fH = sec.floorheight, cH = sec.ceilingheight, light = sec.lightlevel;

    const Texture* T = nullptr;
    if (sg.backsector < 0) {
        T = c.tex->wall(sd.midtexture);          // one-sided: middle texture
    }
    // (two-sided handled in Task 5)
    if (!T) return;

    float texBaseU = static_cast<float>(sg.offset) + static_cast<float>(sd.textureoffset);

    for (int x = x0; x <= x1; ++x) {
        if (c.ceilingClip[x] >= c.floorClip[x] - 1) continue;       // column fully occluded
        float sx = (x - c.w / 2.0f) / c.focal;                      // right/depth for this column's ray
        float denom = (r1o - r0o) - sx * (d1o - d0o);               // ORIGINAL endpoints
        if (std::fabs(denom) < 1e-9f) continue;
        float t = (sx * d0o - r0o) / denom;                         // param along original seg line
        if (t < -0.001f || t > 1.001f) continue;
        float depth = d0o + t * (d1o - d0o);
        if (depth <= nearZ) continue;
        float scale = 1.0f / depth;
        float U = texBaseU + t * segLen;
        drawCol(c, x, scale, *T, U, sd.rowoffset, cH, fH, light, /*solid*/true, false, false);
    }
}

void renderSubsector(Cam& c, const MapData& m, int idx) {
    if (idx < 0 || idx >= static_cast<int>(m.subsectors.size())) return;
    const subsector_t& ss = m.subsectors[idx];
    for (int i = 0; i < ss.segcount; ++i) {
        int s = ss.firstseg + i;
        if (s >= 0 && s < static_cast<int>(m.segs.size())) renderSeg(c, m, m.segs[s]);
    }
}
void renderNode(Cam& c, const MapData& m, uint32_t idx) {
    if (idx & NF_SUBSECTOR) { renderSubsector(c, m, idx & ~NF_SUBSECTOR); return; }
    if (idx >= m.nodes.size()) return;
    const node_t& n = m.nodes[idx];
    int side = pointOnSide(c.px, c.py, n);
    renderNode(c, m, n.children[side]);
    renderNode(c, m, n.children[side ^ 1]);
}
}  // namespace

void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  const TextureLookup& tex, float px, float py, float ang, float eyeZ) {
    if (map.nodes.empty() || map.subsectors.empty()) return;
    Cam c;
    c.px = px; c.py = py; c.eyeZ = eyeZ;
    c.sin = std::sin(ang); c.cos = std::cos(ang);
    c.w = w; c.h = h; c.fb = fb; c.focal = w / 2.0f;
    c.tex = &tex;
    c.ceilingClip.assign(w, -1);
    c.floorClip.assign(w, h);
    for (int i = 0; i < w * h; ++i) fb[i] = 0x000000FFu;            // opaque black clear
    renderNode(c, map, static_cast<uint32_t>(map.nodes.size() - 1));
}
```

(`r_bsp.cpp` includes `<algorithm>` for `std::clamp` and `<cmath>` for `sqrt`/`ceil`/`floor`/`fabs`.)

- [ ] **Step 3: Update `src/main.cpp`** — build `TextureLookup` once and pass it in both paths. Add include and change both call sites:

Add include near the others:
```cpp
#include "render/r_data.h"
```

`--dumpframe` branch — build textures + pass to `R_RenderView`:
```cpp
            if (std::string(argv[i]) == "--dumpframe" && i + 2 < argc) {
                WadFile wad(argv[i + 1]);
                MapData map = loadMap(wad, "E1M1");
                TextureLookup tex(wad);
                float px, py, ang;
                if (!playerStart(map, px, py, ang)) { I_Printf("no player start"); return 1; }
                if (i + 3 < argc) {
                    int a = std::atoi(argv[i + 3]);
                    ang = (90.0f - static_cast<float>(a)) * 3.14159265f / 180.0f;
                }
                constexpr int FW = 320, FH = 200;
                std::vector<std::uint32_t> fb(static_cast<size_t>(FW) * FH, 0);
                R_RenderView(fb.data(), FW, FH, map, tex, px, py, ang, 41.0f);
                writeBMP(argv[i + 2], fb.data(), FW, FH);
                std::cout << "wrote " << argv[i + 2] << "\n";
                return 0;
            }
```

Window branch — build textures once before the loop:
```cpp
        std::cout << "doomcpp 0.1.0  (P3a textured walls)  WASD move, arrows turn, ESC quit\n";
        WadFile wad("assets/freedoom1.wad");
        MapData map = loadMap(wad, "E1M1");
        TextureLookup tex(wad);
        float px, py, ang;
        if (!playerStart(map, px, py, ang)) { px = 0; py = 0; ang = 0; }

        constexpr int FB_W = 320, FB_H = 200;
        if (!i_video::init(FB_W, FB_H, "doomcpp - textured")) return 1;
        std::vector<std::uint32_t> fb(static_cast<size_t>(FB_W) * FB_H, 0);

        const float moveSpeed = 4.0f, turnSpeed = 0.04f;
        Input in{};
        bool running = true;
        while (running) {
            running = i_video::pollEvents(in);
            if (in.forward)     { px += std::sin(ang) * moveSpeed; py += std::cos(ang) * moveSpeed; }
            if (in.back)        { px -= std::sin(ang) * moveSpeed; py -= std::cos(ang) * moveSpeed; }
            if (in.strafeLeft)  { px += std::cos(ang) * moveSpeed; py -= std::sin(ang) * moveSpeed; }
            if (in.strafeRight) { px -= std::cos(ang) * moveSpeed; py += std::sin(ang) * moveSpeed; }
            if (in.turnLeft)    ang += turnSpeed;
            if (in.turnRight)   ang -= turnSpeed;

            R_RenderView(fb.data(), FB_W, FB_H, map, tex, px, py, ang, 41.0f);
            i_video::present(fb.data());
        }
```

- [ ] **Step 4: Build** (no new unit tests this task):

```bash
"$CMAKE" --build build --config Release
```

- [ ] **Step 5: Render + inspect a frame.** Dump the player-start view, convert to PNG, and read it:

```bash
build/Release/doomcpp.exe --dumpframe assets/freedoom1.wad build/frame_p3a.bmp
powershell.exe -NoProfile -Command "Add-Type -AssemblyName System.Drawing; \$i=[System.Drawing.Image]::FromFile((Resolve-Path 'build/frame_p3a.bmp')); \$i.Save('build/frame_p3a.png',[System.Drawing.Imaging.ImageFormat]::Png); \$i.Dispose()"
```
Then `Read` `build/frame_p3a.png` (or `analyze_image`).

Expected: solid one-sided walls of E1M1's start room show real textures (brick/wood/metal panels); brightness varies by sector `lightlevel`; doorways/two-sided openings are black holes (fixed in Task 5). If walls are blank/garbled: check `parseTextureDefs` width/height offsets (p+12/p+14), that `TextureLookup::wall` returns non-null for a name from `sd.midtexture` (add a temporary `I_Printf`), and that U/V wrap and `1 map unit = 1 texel` holds.

- [ ] **Step 6: Commit** — `feat(render): textured one-sided walls with light + occlusion`

---

## Task 5: Two-sided walls — upper / lower / masked middle

**Files:**
- Modify: `src/render/r_bsp.cpp` (extend `renderSeg`)

**Interfaces:**
- Consumes: `sg.backsector`, `sd.toptexture`/`bottomtexture`/`midtexture`, back sector heights.
- Produces: two-sided segs draw their upper (ceiling step), lower (floor step), and masked middle (fences) textures; openings stay black for P3b.

- [ ] **Step 1: Replace the texture-resolution + column-loop block in `renderSeg`.** Find the block starting at `const Texture* T = nullptr;` through the end of the `for (int x = x0; ...)` loop and replace with the two-sided-aware version below. The setup above it (projection, near-clip, side/sector resolution, `texBaseU`) is unchanged.

```cpp
    // Decide which wall parts exist.
    bool twoSided = sg.backsector >= 0 && sg.backsector < static_cast<int>(m.sectors.size());
    int  fH_f = fH, cH_f = cH;                       // front sector
    int  fH_b = fH, cH_b = cH;                       // back sector (defaults if invalid)
    if (twoSided) { fH_b = m.sectors[sg.backsector].floorheight; cH_b = m.sectors[sg.backsector].ceilingheight; }

    const Texture* midT = c.tex->wall(sd.midtexture);
    const Texture* topT = twoSided ? c.tex->wall(sd.toptexture) : nullptr;
    const Texture* botT = twoSided ? c.tex->wall(sd.bottomtexture) : nullptr;

    for (int x = x0; x <= x1; ++x) {
        if (c.ceilingClip[x] >= c.floorClip[x] - 1) continue;
        float sx = (x - c.w / 2.0f) / c.focal;
        float denom = (r1o - r0o) - sx * (d1o - d0o);
        if (std::fabs(denom) < 1e-9f) continue;
        float t = (sx * d0o - r0o) / denom;
        if (t < -0.001f || t > 1.001f) continue;
        float depth = d0o + t * (d1o - d0o);
        if (depth <= nearZ) continue;
        float scale = 1.0f / depth;
        float U = texBaseU + t * segLen;

        if (!twoSided) {
            if (midT) drawCol(c, x, scale, *midT, U, sd.rowoffset, cH_f, fH_f, light, /*solid*/true, false, false);
        } else {
            // upper wall (ceiling step): back ceiling higher than front
            if (topT && cH_b > cH_f) drawCol(c, x, scale, *topT, U, sd.rowoffset, cH_b, cH_f, light, false, /*upper*/true, false);
            // lower wall (floor step): back floor lower than front
            if (botT && fH_b < fH_f) drawCol(c, x, scale, *botT, U, sd.rowoffset, fH_f, fH_b, light, false, false, /*lower*/true);
            // masked middle (fences/grates): floats in the back opening; transparent pixels see through; no clip update
            if (midT && cH_b > fH_b) drawCol(c, x, scale, *midT, U, sd.rowoffset, cH_b, fH_b, light, false, false, false);
        }
    }
```

Note: `drawCol` already skips transparent texels (`!(texel & 0xFF)`) and only updates clips for `solid/upper/lower`; the masked-middle call passes all three false, so it neither occludes nor claims rows — exactly right for a see-through grate.

- [ ] **Step 2: Build.**

```bash
"$CMAKE" --build build --config Release --target RUN_TESTS
```
All 28 unit tests still pass (rendering changes are visual-only).

- [ ] **Step 3: Render + inspect.** Re-dump the frame (same commands as Task 4 Step 5, optionally with a few different angles, e.g. `--dumpframe assets/freedoom1.wad build/frame_p3a_door.bmp 45`) and read the PNG.

Expected: doorways/openings show the room beyond (its walls render via BSP); raised steps and platforms show correct **lower** textures; ceiling drops show **upper** textures; gratings/fences show a see-through **middle** texture. Floor/ceiling planes remain black (P3b). If a two-sided wall shows nothing where it should: verify `topT`/`botT` resolved (the side's texture name is non-empty in the WAD) and the height conditions (`cH_b > cH_f` / `fH_b < fH_f`) match that geometry.

- [ ] **Step 4: Commit** — `feat(render): two-sided upper/lower/masked-middle textured walls`

---

## Task 6: Milestone — first-person textured walkthrough + tag

**Files:**
- Modify: `README.md` (note P3a milestone), `docs/superpowers/plans/2026-07-21-p3a-textured-walls.md` (tick boxes).

- [ ] **Step 1: Launch the window in the background and confirm it's alive** (interactive walk to sanity-check multiple rooms/angles):

```bash
build/Release/doomcpp.exe &
sleep 2
```
WASD to move, arrows to turn, ESC to quit. Verify textures stay correct across several rooms and that brightness tracks sector light.

- [ ] **Step 2: Update README** — append a one-line milestone under the existing roadmap note (mirror how P2b was noted), e.g. `P3a 贴图墙 ✅`.

- [ ] **Step 3: Final full test run + commit** — confirm all 28 tests pass, then:

```bash
"$CMAKE" --build build --config Release --target RUN_TESTS
git add README.md docs/superpowers/plans/2026-07-21-p3a-textured-walls.md
git commit -m "docs: P3a milestone — textured wall walkthrough"
```

- [ ] **Step 4: Merge to `main` + tag** (per project branch strategy — `--no-ff`):

```bash
git checkout main
git merge --no-ff feat/p3a-textured-walls -m "merge: P3a textured walls complete"
git tag v0.5-p3a-textured-walls
```

---

## P3a Definition of Done

- [x] `side_t` (texture offsets + upper/middle/lower names), `sector_t` (lightlevel + floor/ceiling pics), `seg_t` (offset) parsed; unit tests cover them.
- [x] `r_data` decodes `PLAYPAL`/`PNAMES`/patches, parses `TEXTURE1` (+`TEXTURE2`), composites wall textures to RGBA; unit tests cover parse/decode/composite.
- [x] `R_RenderView` renders **textured** walls: one-sided middle, two-sided upper/lower/masked-middle; perspective-correct U (per-column ray-line `t`) and V (projection inverse); per-column `ceilingClip`/`floorClip` occlusion; sector-`lightlevel` brightness.
- [x] `--dumpframe` BMP (→ PNG) shows recognizable E1M1 wall textures with light variation.
- [x] First-person textured walkthrough of E1M1 works (WASD/arrows).
- [ ] All 28 unit tests pass. P3a merged to `main`, tagged `v0.5-p3a-textured-walls`.

## Next: P3b · Visplane 地板天花板

Add flat (64×64 raw) loading + the visplane algorithm: reuse P3a's `ceilingClip[]`/`floorClip[]` as span boundaries during seg rendering, then render each visplane via `R_MapPlane`/`R_DrawPlane` with `yslope`/`distscale` tables. Milestone: see textured floor and ceiling.
