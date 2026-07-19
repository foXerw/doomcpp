# P2b · BSP 3D 墙渲染 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Render E1M1 as a first-person **pseudo-3D view** — front-to-back BSP traversal, seg projection into screen columns, solid-color walls with perspective height. The "walk into DOOM" milestone.

**Architecture:** Extend `p_setup` to parse SECTORS/SIDEDEFS/SEGS/SSECTORS/NODES/THINGS and resolve each seg's front sector + the player start. New `render/r_bsp` does the 3D pass: `R_RenderView(fb, w, h, map, player)` sets up the camera, recurses the BSP near-side-first (leaves → subsectors → segs), near-clips + projects each seg to a column range, and draws solid wall columns with per-column `occluded[]` occlusion. `r_draw` gains a `writeBMP` for frame verification. `main` runs first-person movement (reuses `Input`) and renders each frame; `--dumpframe` renders one frame to a BMP for inspection.

**Faithfulness notes:** BSP traversal, near-first ordering, `R_PointOnSide`, seg→column projection follow the original `r_bsp`/`r_segs`. **Float** is used for render math (P2 simplification; data stays fixed-point). **BBox frustum culling deferred** (optimization only). **All segs treated opaque** in P2b (two-sided doorways render as walls until P3 visplanes).

**Tech Stack:** C++17, doctest, MSVC via CMake (unchanged).

## Global Constraints

- Map structs are LE `short` (doomdata.h); parse with `rd_i16`. `NF_SUBSECTOR = 0x8000`.
- `R_PointOnSide`: `side = (node.dx*dy < node.dy*dx) ? 0 : 1` where `dx=x-node.x, dy=y-node.y` (float; matches original semantics).
- Camera (matches P2a movement): forward `F=(sin a, cos a)`, right `R=(cos a, -sin a)`. `depth = dx*sin+dy*cos`, `right = dx*cos-dy*sin`. `screen_x = W/2 + focal*right/depth`, `screen_y = H/2 - focal*(z-eyeZ)/depth`.
- Eye height 41 (DOOM VIEWHEIGHT) over floor 0 (P2b assumes floor 0; P3 tracks real floor).
- DOOM thing angle → camera angle: `myang = (90 - thingAngleDeg) * pi/180`.
- Unit tests stay WAD-free (crafted buffers). 3D rendering verified by `--dumpframe` → BMP → Read.
- One commit per task; proxy + vcpkg toolchain for builds.

## File Structure (additions)

```
src/play/p_setup.h / .cpp    # +sectors/sides/segs/subsectors/nodes/things; seg frontsector; playerStart
src/render/r_bsp.h / .cpp    # R_RenderView: BSP traversal + seg projection + occlusion
src/render/r_draw.h / .cpp   # +writeBMP (frame verification)
src/render/r_column.h?       # (none; columns drawn inline in r_bsp for P2b)
tests/test_p_setup.cpp       # +cases for new parsers
```

`r_bsp.cpp` is SDL-free → added to `doomcore`.

---

## Task 1: Parse BSP structures + resolve seg sectors + player start

**Files:**
- Modify: `src/play/p_setup.h`, `src/play/p_setup.cpp`
- Modify: `tests/test_p_setup.cpp`

**Interfaces:**
- Produces:
  - `struct sector_t { int floorheight, ceilingheight; };`
  - `struct side_t   { int sector; };` (offsets/textures deferred to P3)
  - `struct seg_t    { int v1, v2, frontsector, backsector; };`
  - `struct subsector_t { int segcount, firstseg; };`
  - `struct node_t    { float x, y, dx, dy; uint32_t children[2]; };`
  - `struct thing_t   { int x, y, angleDeg, type; };`
  - `struct MapData { vertices, lines, sectors, sides, segs, subsectors, nodes, things; ... };`
  - `bool playerStart(const MapData&, float& x, float& y, float& ang);` (thing type 1)

- [ ] **Step 1: Add failing cases to `tests/test_p_setup.cpp`**

```cpp
#include "doctest.h"
#include "play/p_setup.h"
#include <cstdint>
#include <vector>
// (existing w16 helper)

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
    w16(b, 3); w16(b, 4); w16(b, 0); w16(b, 1); w16(b, 0); w16(b, 0);  // v1,v2,angle,linedef,side,offset
    auto s = parseSegs(b.data(), b.size());
    CHECK(s.size() == 1);
    CHECK(s[0].v1 == 3);
    CHECK(s[0].linedef == 1);
}

TEST_CASE("parseNodes reads 28-byte records + children") {
    std::vector<byte> b;
    w16(b, 10); w16(b, 20); w16(b, 5); w16(b, -5);   // x,y,dx,dy
    for (int i = 0; i < 16; ++i) b.push_back(0);      // bbox[2][4]
    b.push_back(0x34); b.push_back(0x12);             // child0 = 0x1234
    b.push_back(0x00); b.push_back(0x80);             // child1 = 0x8000 (NF_SUBSECTOR leaf)
    auto n = parseNodes(b.data(), b.size());
    CHECK(n.size() == 1);
    CHECK(n[0].x == 10);
    CHECK(n[0].dx == 5);
    CHECK(n[0].children[0] == 0x1234);
    CHECK((n[0].children[1] & 0x8000) != 0);          // leaf flag
}
```

- [ ] **Step 2: Run, expect fail** (no `parseSectors` etc.).

- [ ] **Step 3: Extend `src/play/p_setup.h`** — add the structs above to `MapData` and declare the new parsers + `playerStart`:

```cpp
struct sector_t     { int floorheight, ceilingheight; };
struct side_t       { int sector; };
struct seg_t        { int v1, v2, frontsector, backsector; };
struct subsector_t  { int segcount, firstseg; };
struct node_t       { float x, y, dx, dy; uint32_t children[2]; };
struct thing_t      { int x, y, angleDeg, type; };

struct MapData {
    std::vector<vertex_t>    vertices;
    std::vector<line_t>      lines;
    std::vector<sector_t>    sectors;
    std::vector<side_t>      sides;
    std::vector<seg_t>       segs;
    std::vector<subsector_t> subsectors;
    std::vector<node_t>      nodes;
    std::vector<thing_t>     things;
    int numSides = 0, numSectors = 0;
};

std::vector<sector_t>    parseSectors(const byte* d, size_t n);   // 26 bytes
std::vector<side_t>      parseSidedefs(const byte* d, size_t n);   // 30 bytes (sector at +28)
std::vector<seg_t>       parseSegs(const byte* d, size_t n);       // 12 bytes (frontsector unresolved)
std::vector<subsector_t> parseSubsectors(const byte* d, size_t n);// 4 bytes
std::vector<node_t>      parseNodes(const byte* d, size_t n);     // 28 bytes
std::vector<thing_t>     parseThings(const byte* d, size_t n);    // 10 bytes

// Find thing type 1 (player 1 start); returns false if none.
bool playerStart(const MapData& m, float& x, float& y, float& ang);
```

- [ ] **Step 4: Extend `src/play/p_setup.cpp`** — implement the parsers and resolve seg sectors in `loadMap`:

```cpp
std::vector<sector_t> parseSectors(const byte* d, size_t n) {
    if (n % 26) I_Error("parseSectors: bad size");
    std::vector<sector_t> out(n/26);
    for (size_t i=0;i<out.size();++i){ const byte* p=d+i*26;
        out[i].floorheight=rd_i16(p); out[i].ceilingheight=rd_i16(p+2); }
    return out;
}
std::vector<side_t> parseSidedefs(const byte* d, size_t n) {
    if (n % 30) I_Error("parseSidedefs: bad size");
    std::vector<side_t> out(n/30);
    for (size_t i=0;i<out.size();++i){ out[i].sector = rd_i16(d+i*30+28); }
    return out;
}
std::vector<seg_t> parseSegs(const byte* d, size_t n) {
    if (n % 12) I_Error("parseSegs: bad size");
    std::vector<seg_t> out(n/12);
    for (size_t i=0;i<out.size();++i){ const byte* p=d+i*12;
        out[i].v1=rd_i16(p); out[i].v2=rd_i16(p+2);
        int linedef=rd_i16(p+6); int side=rd_i16(p+8);
        out[i].frontsector=-1; out[i].backsector=-1; // resolved below
        (void)linedef; (void)side; // stored via loadMap; keep raw here
        out[i].frontsector = linedef; out[i].backsector = side; // TEMP carry; see note
    }
    return out;
}
```
(TEMP carry: `parseSegs` can't resolve sectors (needs lines/sides). Instead store linedef/side in front/back temporarily, then resolve in `loadMap`. Cleaner: add `int linedef, side;` to `seg_t` and resolve there.)

**Revised:** give `seg_t` the fields `int v1,v2,linedef,side,frontsector,backsector;`. `parseSegs` fills v1,v2,linedef,side. `loadMap` resolves front/back sector:

```cpp
// inside loadMap, after parsing lines/sides/segs:
for (auto& sg : m.segs) {
    if (sg.linedef < 0 || sg.linedef >= (int)m.lines.size()) continue;
    const auto& L = m.lines[sg.linedef];
    int f = L.sidenum[sg.side];      // front side
    int b = L.sidenum[sg.side ^ 1];  // back side (other)
    sg.frontsector = (f >= 0 && f < (int)m.sides.size()) ? m.sides[f].sector : -1;
    sg.backsector  = (b >= 0 && b < (int)m.sides.size()) ? m.sides[b].sector : -1;
}
```

```cpp
std::vector<subsector_t> parseSubsectors(const byte* d, size_t n) {
    if (n % 4) I_Error("parseSubsectors: bad size");
    std::vector<subsector_t> out(n/4);
    for (size_t i=0;i<out.size();++i){ out[i].segcount=rd_i16(d+i*4); out[i].firstseg=rd_i16(d+i*4+2); }
    return out;
}
std::vector<node_t> parseNodes(const byte* d, size_t n) {
    if (n % 28) I_Error("parseNodes: bad size");
    std::vector<node_t> out(n/28);
    for (size_t i=0;i<out.size();++i){ const byte* p=d+i*28;
        out[i].x=rd_i16(p); out[i].y=rd_i16(p+2); out[i].dx=rd_i16(p+4); out[i].dy=rd_i16(p+6);
        out[i].children[0]=static_cast<uint16_t>(rd_i16(p+20));
        out[i].children[1]=static_cast<uint16_t>(rd_i16(p+22)); }
    return out;
}
std::vector<thing_t> parseThings(const byte* d, size_t n) {
    if (n % 10) I_Error("parseThings: bad size");
    std::vector<thing_t> out(n/10);
    for (size_t i=0;i<out.size();++i){ const byte* p=d+i*10;
        out[i].x=rd_i16(p); out[i].y=rd_i16(p+2); out[i].angleDeg=rd_i16(p+4); out[i].type=rd_i16(p+6); }
    return out;
}
bool playerStart(const MapData& m, float& x, float& y, float& ang) {
    for (const auto& t : m.things) {
        if (t.type == 1) {
            x = static_cast<float>(t.x); y = static_cast<float>(t.y);
            ang = (90.0f - static_cast<float>(t.angleDeg)) * 3.14159265f / 180.0f;
            return true;
        }
    }
    return false;
}
```

And in `loadMap`, after vertices/lines, also parse the rest:
```cpp
    m.sectors    = parseSectors(bytes(8));    // ML_SECTORS (full parse replaces count)
    m.sides      = parseSidedefs(bytes(3));   // ML_SIDEDEFS
    m.segs       = parseSegs(bytes(5));       // ML_SEGS
    m.subsectors = parseSubsectors(bytes(6)); // ML_SSECTORS
    m.nodes      = parseNodes(bytes(7));      // ML_NODES
    m.things     = parseThings(bytes(1));     // ML_THINGS
    // resolve seg sectors (block above)
```
(rename the `lumpBytes` lambda to `bytes` if you like; keep using the marker+offset reads).

- [ ] **Step 5: Build + run new parser cases.** Expect pass.

- [ ] **Step 6: Commit** — `feat(play): parse BSP structures (sectors/sides/segs/subsectors/nodes/things)`

---

## Task 2: 3D renderer + BMP dump

**Files:**
- Create: `src/render/r_bsp.h`, `src/render/r_bsp.cpp`
- Modify: `src/render/r_draw.h`, `src/render/r_draw.cpp` (+`writeBMP`)
- Modify: `CMakeLists.txt` (`doomcore` += `src/render/r_bsp.cpp`)

**Interfaces:**
- Produces:
  - `void R_RenderView(uint32_t* fb, int w, int h, const MapData& map, float px, float py, float ang, float eyeZ);`
  - `bool writeBMP(const std::string& path, const uint32_t* fb, int w, int h);`

- [ ] **Step 1: Create `src/render/r_bsp.h`**

```cpp
#pragma once
#include "core/doomtype.h"

struct MapData;
struct ViewPlayer;

// Render one 3D frame of solid-color walls (BSP front-to-back, per-column occlusion).
void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  float px, float py, float ang, float eyeZ);
```

- [ ] **Step 2: Create `src/render/r_bsp.cpp`**

```cpp
#include "r_bsp.h"
#include "r_draw.h"
#include "play/p_setup.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
constexpr uint32_t NF_SUBSECTOR = 0x8000;

struct Cam { float px, py, sin, cos, eyeZ; int w, h; uint32_t* fb;
             std::vector<uint8_t> occluded; float focal; };

int pointOnSide(float x, float y, const node_t& n) {
    float dx = x - n.x, dy = y - n.y;
    float left = n.dy * dx, right = n.dx * dy;
    return (right < left) ? 0 : 1;   // front=0, back=1
}

void renderSeg(Cam& c, const MapData& m, const seg_t& sg) {
    if (sg.v1 < 0 || sg.v1 >= (int)m.vertices.size()) return;
    if (sg.v2 < 0 || sg.v2 >= (int)m.vertices.size()) return;
    if (sg.frontsector < 0 || sg.frontsector >= (int)m.sectors.size()) return;

    // camera-space transform for both endpoints
    auto toCam = [&](fixed_t vx, fixed_t vy, float& depth, float& right) {
        float dx = static_cast<float>(vx >> 16) - c.px;
        float dy = static_cast<float>(vy >> 16) - c.py;
        depth = dx * c.sin + dy * c.cos;
        right = dx * c.cos - dy * c.sin;
    };
    float d0, r0, d1, r1;
    toCam(m.vertices[sg.v1].x, m.vertices[sg.v1].y, d0, r0);
    toCam(m.vertices[sg.v2].x, m.vertices[sg.v2].y, d1, r1);

    // near-clip against z = 0.01f
    const float nearZ = 0.01f;
    if (d0 <= nearZ && d1 <= nearZ) return;              // fully behind
    if (d0 <= nearZ) { float t = (nearZ - d0) / (d1 - d0); d0 = nearZ; r0 += (r1 - r0) * t; }
    else if (d1 <= nearZ) { float t = (nearZ - d1) / (d0 - d1); d1 = nearZ; r1 += (r0 - r1) * t; }

    int x0 = static_cast<int>(c.w / 2 + c.focal * r0 / d0);
    int x1 = static_cast<int>(c.w / 2 + c.focal * r1 / d1);
    if (x0 > x1) std::swap(x0, x1);
    if (x1 < 0 || x0 >= c.w) return;
    x0 = std::max(0, x0); x1 = std::min(c.w - 1, x1);

    float s0 = 1.0f / d0, s1 = 1.0f / d1;
    int fH = m.sectors[sg.frontsector].floorheight;
    int cH = m.sectors[sg.frontsector].ceilingheight;
    float wallColor = 0x808080u; // mid grey; vary by depth for some life
    (void)wallColor;

    for (int x = x0; x <= x1; ++x) {
        if (c.occluded[x]) continue;
        float t = (x1 == x0) ? 0.0f : static_cast<float>(x - x0) / (x1 - x0);
        float scale = s0 + (s1 - s0) * t;                 // 1/depth, linear in screen x
        float topY    = c.h / 2 - c.focal * (cH - c.eyeZ) * scale;
        float botY    = c.h / 2 - c.focal * (fH - c.eyeZ) * scale;
        int y0 = std::max(0, static_cast<int>(topY));
        int y1 = std::min(c.h - 1, static_cast<int>(botY));
        // shade by inverse depth so distant walls darken
        uint8_t sh = static_cast<uint8_t>(std::min(255.0f, 60.0f + 9000.0f * scale));
        uint32_t col = 0xFF000000u | (sh << 16) | (sh << 8) | sh;
        for (int y = y0; y <= y1; ++y) c.fb[y * c.w + x] = col;
        c.occluded[x] = 1;
    }
}

void renderSubsector(Cam& c, const MapData& m, int idx) {
    const subsector_t& ss = m.subsectors[idx];
    for (int i = 0; i < ss.segcount; ++i) {
        int s = ss.firstseg + i;
        if (s >= 0 && s < (int)m.segs.size()) renderSeg(c, m, m.segs[s]);
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
}

void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  float px, float py, float ang, float eyeZ) {
    if (map.nodes.empty() || map.subsectors.empty()) return;
    Cam c;
    c.px = px; c.py = py; c.eyeZ = eyeZ;
    c.sin = std::sin(ang); c.cos = std::cos(ang);
    c.w = w; c.h = h; c.fb = fb;
    c.focal = w / 2.0f;                 // ~90° hfov
    c.occluded.assign(w, 0);
    for (int i = 0; i < w * h; ++i) fb[i] = 0xFF000000u;   // black clear
    renderNode(c, map, static_cast<uint32_t>(map.nodes.size() - 1));  // root = last node
}
```
Note: the root node is the **last** entry in NODES (highest index), per the DOOM convention. `children` indices are node indices (or subsector index | NF_SUBSECTOR).

- [ ] **Step 3: Add `writeBMP` to `r_draw`**

`src/render/r_draw.h`:
```cpp
#include <string>
bool writeBMP(const std::string& path, const uint32_t* fb, int w, int h);
```
`src/render/r_draw.cpp` (append):
```cpp
#include <cstdio>
bool writeBMP(const std::string& path, const uint32_t* fb, int w, int h) {
    int row = ((24 * w + 31) / 32) * 4;     // bytes per row, 4-aligned
    int sz = 54 + row * h;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    unsigned char hdr[54] = {0};
    hdr[0]='B'; hdr[1]='M';
    hdr[2]=sz; hdr[3]=sz>>8; hdr[4]=sz>>16; hdr[5]=sz>>24;
    hdr[10]=54; hdr[14]=40;
    hdr[18]=w; hdr[19]=w>>8; hdr[22]=h; hdr[23]=h>>8;
    hdr[26]=1; hdr[28]=24;            // planes=1, bpp=24
    std::fwrite(hdr, 1, 54, f);
    std::vector<unsigned char> rowbuf(row, 0);
    for (int y = h - 1; y >= 0; --y) {            // BMP is bottom-up
        for (int x = 0; x < w; ++x) {
            uint32_t p = fb[y * w + x];
            rowbuf[x * 3 + 0] = (p >> 8) & 0xff;   // B
            rowbuf[x * 3 + 1] = (p >> 16) & 0xff;  // G
            rowbuf[x * 3 + 2] = (p >> 24) & 0xff;  // R (RGBA8888: R is high byte)
        }
        std::fwrite(rowbuf.data(), 1, row, f);
    }
    std::fclose(f);
    return true;
}
```

- [ ] **Step 4: Wire `--dumpframe` into `main.cpp`** (add alongside `--listlumps`):

```cpp
        if (std::string(argv[i]) == "--dumpframe" && i + 2 < argc) {
            WadFile wad(argv[i + 1]);
            MapData map = loadMap(wad, "E1M1");
            float px, py, ang;
            if (!playerStart(map, px, py, ang)) { I_Printf("no player start"); return 1; }
            constexpr int FW = 320, FH = 200;
            std::vector<uint32_t> fb(static_cast<size_t>(FW) * FH, 0);
            R_RenderView(fb.data(), FW, FH, map, px, py, ang, 41.0f);
            writeBMP(argv[i + 2], fb.data(), FW, FH);
            std::cout << "wrote " << argv[i + 2] << "\n";
            return 0;
        }
```
Usage: `doomcpp --dumpframe assets/freedoom1.wad frame.bmp`.

- [ ] **Step 5: Add `r_bsp.cpp` to `doomcore`; build.**

- [ ] **Step 6: Render + inspect the frame** — run `--dumpframe`, then Read the BMP. Expect: a recognizable first-person view of E1M1's starting room (walls as grey columns, darker with distance, black floor/ceiling). If inside-out/empty/wrong, debug (check `pointOnSide` sign, root node index, camera transform).

- [ ] **Step 7: Commit** — `feat(render): BSP front-to-back wall renderer + BMP frame dump`

---

## Task 3: First-person 3D mode (milestone)

**Files:**
- Modify: `src/main.cpp` (replace the automap window branch with 3D)

- [ ] **Step 1: Replace the window branch in `main.cpp`** with first-person 3D:

```cpp
        // --- window / 3D mode ---
        std::cout << "doomcpp 0.1.0  (P2b 3D)\n";
        WadFile wad("assets/freedoom1.wad");
        MapData map = loadMap(wad, "E1M1");
        float px, py, ang;
        if (!playerStart(map, px, py, ang)) { px=0; py=0; ang=0; }

        constexpr int FB_W = 320, FB_H = 200;
        if (!i_video::init(FB_W, FB_H, "doomcpp - 3D")) return 1;
        std::vector<std::uint32_t> fb(static_cast<size_t>(FB_W) * FB_H, 0);

        const float moveSpeed = 4.0f, turnSpeed = 0.04f;
        Input in{}; bool running = true;
        while (running) {
            running = i_video::pollEvents(in);
            if (in.forward)     { px += std::sin(ang) * moveSpeed; py += std::cos(ang) * moveSpeed; }
            if (in.back)        { px -= std::sin(ang) * moveSpeed; py -= std::cos(ang) * moveSpeed; }
            if (in.strafeLeft)  { px += std::cos(ang) * moveSpeed; py -= std::sin(ang) * moveSpeed; }
            if (in.strafeRight) { px -= std::cos(ang) * moveSpeed; py += std::sin(ang) * moveSpeed; }
            if (in.turnLeft)    ang += turnSpeed;
            if (in.turnRight)   ang -= turnSpeed;

            R_RenderView(fb.data(), FB_W, FB_H, map, px, py, ang, 41.0f);
            i_video::present(fb.data());
        }
        i_video::shutdown();
        return 0;
```

- [ ] **Step 2: Build + dump a frame to confirm still correct after integration.**

- [ ] **Step 3: Launch the 3D window (background, confirm alive).** You run interactively to walk around (WASD + arrows).

- [ ] **Step 4: Commit** — `feat(render): first-person 3D walkthrough of E1M1`

---

## P2b Definition of Done

- [ ] BSP structures parsed (doomdata.h layouts); seg front/back sectors resolved; player start found.
- [ ] `R_RenderView` produces a recognizable 3D view (verified by `--dumpframe` BMP).
- [ ] Walls occlude correctly (per-column, front-to-back); distant walls darker.
- [ ] WASD/arrows walk through E1M1 in first person.
- [ ] All unit tests pass (18 + 3 = 21). P2b merged to `main`, tagged `v0.4-p2b-3d`.

## Next: P3 · 贴图 + 地板天花板 + 碰撞

Add visplane floor/ceiling rendering, wall textures (TEXTURE1/PNAMES + patches), and player collision (BLOCKMAP). Bring in fixed-point trig tables + per-column texture mapping for exact fidelity.
