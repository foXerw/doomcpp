# P2a · 地图加载 + 2D 自动地图 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Load a DOOM map's geometry from the WAD (vertices/linedefs/etc.), add 2D framebuffer drawing primitives + keyboard input, and render a walkable **2D automap** of E1M1 — the first time the player "sees" a DOOM level.

**Architecture:** `p_setup` parses on-disk map structs (from `doomdata.h`) into typed arrays via pure functions (unit-testable with crafted buffers); `loadMap(wad, name)` glues them to the WAD. `render/r_draw` adds pixel/line drawing on the RGBA framebuffer. `i_video` gains an `Input` struct (SDL-free header). `main` runs a player (float pos/angle for the automap; fixed-point physics deferred to P3), applies WASD/arrows, and draws all linedefs transformed to screen space each frame.

This is the **2D half** of design-phase P2. The 3D BSP renderer is a separate follow-up plan (P2b).

**Tech Stack:** C++17, doctest, MSVC via CMake (unchanged). Render math uses **float** (data stays fixed-point for P3); documented simplification.

## Global Constraints

(inherited; every task includes these)
- Map on-disk structs are little-endian `short`; read with an explicit LE helper. Field layouts match `doomdata.h` verbatim.
- Map lumps follow the marker in fixed order: `ML_LABEL,THINGS,LINEDEFS,SIDEDEFS,VERTEXES,SEGS,SSECTORS,NODES,SECTORS,REJECT,BLOCKMAP` — loader reads them at `markerIndex + offset`.
- Vertices stored as `fixed_t` (`raw << FRACBITS`), faithful for P3 collision. Automap converts to float at draw time.
- Unit tests must NOT depend on the real (gitignored) `freedoom1.wad` — test the pure parse functions with crafted byte buffers; verify WAD integration by running the program.
- One logical commit per task; Conventional Commits. Proxy + vcpkg toolchain for builds.

## File Structure (additions)

```
src/play/p_setup.h / .cpp    # map structs + parse fns + loadMap
src/render/r_draw.h / .cpp   # framebuffer pixel/line drawing
tests/test_p_setup.cpp       # parse fns (crafted buffers)
tests/test_r_draw.cpp        # drawLine pixels
src/platform/i_video.h       # + Input struct, pollEvents(Input&)
```

`p_setup.cpp` and `r_draw.cpp` are SDL-free → added to `doomcore`.

---

## Task 1: Map data types + parse functions + loadMap

**Files:**
- Create: `src/play/p_setup.h`, `src/play/p_setup.cpp`
- Create: `tests/test_p_setup.cpp`
- Modify: `CMakeLists.txt` (`doomcore` += `src/play/p_setup.cpp`)
- Modify: `tests/CMakeLists.txt` (+= `test_p_setup.cpp`)

**Interfaces:**
- Consumes: `WadFile` (P1), `fixed_t`/`byte` (doomtype).
- Produces:
  - `struct vertex_t { fixed_t x, y; };`
  - `struct line_t { int v1, v2, flags, special, tag, sidenum[2]; };`
  - `struct MapData { std::vector<vertex_t> vertices; std::vector<line_t> lines; int numSides, numSectors; ... };`
  - `std::vector<vertex_t> parseVertexes(const byte* d, size_t n);`
  - `std::vector<line_t> parseLinedefs(const byte* d, size_t n);`
  - `MapData loadMap(const WadFile& wad, const std::string& mapname);`

- [ ] **Step 1: Write the failing test `tests/test_p_setup.cpp`**

```cpp
#include "doctest.h"
#include "play/p_setup.h"
#include <cstring>

namespace {
// little-endian short writer for crafted buffers
void w16(std::vector<byte>& v, int16_t s) {
    v.push_back(static_cast<byte>(static_cast<uint16_t>(s) & 0xff));
    v.push_back(static_cast<byte>((static_cast<uint16_t>(s) >> 8) & 0xff));
}
}

TEST_CASE("parseVertexes reads LE int16 pairs into fixed_t") {
    // two vertices: (10,-20), (300,400)
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
    // one linedef: v1=0, v2=1, flags=1, special=0, tag=0, sides=[0,-1]
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
```

- [ ] **Step 2: Run, expect fail** (`play/p_setup.h` not found).

- [ ] **Step 3: Create `src/play/p_setup.h`**

```cpp
#pragma once
#include <string>
#include <vector>
#include "core/doomtype.h"

class WadFile;   // fwd

struct vertex_t { fixed_t x, y; };
struct line_t   { int v1, v2, flags, special, tag; int sidenum[2]; };

struct MapData {
    std::vector<vertex_t> vertices;
    std::vector<line_t>   lines;
    int numSides = 0;
    int numSectors = 0;
    // SECTORS/SEGS/SSECTORS/NODES parsed in P2b.
};

// Pure parsers (unit-testable with crafted buffers).
std::vector<vertex_t> parseVertexes(const byte* d, size_t n);  // n = byte count
std::vector<line_t>   parseLinedefs(const byte* d, size_t n);

// Load a map by its marker lump name (e.g. "E1M1") from an open WAD.
MapData loadMap(const WadFile& wad, const std::string& mapname);
```

- [ ] **Step 4: Create `src/play/p_setup.cpp`**

```cpp
#include "p_setup.h"
#include "core/i_system.h"
#include "wad/wadfile.h"

namespace {
std::int16_t rd_i16(const byte* p) {
    return static_cast<std::int16_t>(
        static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}
}

std::vector<vertex_t> parseVertexes(const byte* d, size_t n) {
    const size_t rec = 4;
    if (n % rec != 0) I_Error("parseVertexes: bad size");
    std::vector<vertex_t> out(n / rec);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i].x = static_cast<fixed_t>(rd_i16(d + i * rec))     << 16;
        out[i].y = static_cast<fixed_t>(rd_i16(d + i * rec + 2)) << 16;
    }
    return out;
}

std::vector<line_t> parseLinedefs(const byte* d, size_t n) {
    const size_t rec = 14;
    if (n % rec != 0) I_Error("parseLinedefs: bad size");
    std::vector<line_t> out(n / rec);
    for (size_t i = 0; i < out.size(); ++i) {
        const byte* p = d + i * rec;
        out[i].v1      = rd_i16(p);
        out[i].v2      = rd_i16(p + 2);
        out[i].flags   = rd_i16(p + 4);
        out[i].special = rd_i16(p + 6);
        out[i].tag     = rd_i16(p + 8);
        out[i].sidenum[0] = rd_i16(p + 10);
        out[i].sidenum[1] = rd_i16(p + 12);
    }
    return out;
}

MapData loadMap(const WadFile& wad, const std::string& mapname) {
    int marker = wad.checkNumForName(mapname);
    if (marker < 0) I_Error(std::string("loadMap: map not found: ") + mapname);

    // Map lumps follow the marker in fixed order (doomdata.h ML_* enum).
    auto lumpBytes = [&](int offset) -> std::vector<byte> {
        int idx = marker + offset;
        if (idx < 0 || idx >= wad.numLumps()) return {};
        if (wad.lumpSize(idx) <= 0) return {};
        return const_cast<WadFile&>(wad).readLump(idx);
    };

    MapData m;
    m.vertices = parseVertexes(lumpBytes(4));   // ML_VERTEXES
    m.lines    = parseLinedefs(lumpBytes(2));   // ML_LINEDEFS
    m.numSides    = static_cast<int>(lumpBytes(3).size() / 30); // ML_SIDEDEFS (30 bytes)
    m.numSectors  = static_cast<int>(lumpBytes(8).size() / 26); // ML_SECTORS  (26 bytes)
    return m;
}
```

- [ ] **Step 5: Add `src/play/p_setup.cpp` to `doomcore`; add `test_p_setup.cpp` to tests.**

- [ ] **Step 6: Build + run, expect the 2 new cases pass.**

- [ ] **Step 7: Commit** — `feat(play): parse map vertexes/linedefs and load a map by name`

---

## Task 2: Framebuffer drawing primitives + Input

**Files:**
- Create: `src/render/r_draw.h`, `src/render/r_draw.cpp`
- Create: `tests/test_r_draw.cpp`
- Modify: `src/platform/i_video.h` / `.cpp` (add `Input`, `pollEvents(Input&)`)
- Modify: `CMakeLists.txt` (`doomcore` += `src/render/r_draw.cpp`)
- Modify: `tests/CMakeLists.txt` (+= `test_r_draw.cpp`)

**Interfaces:**
- Produces:
  - `void R_DrawPixel(uint32_t* fb, int w, int h, int x, int y, uint32_t color);`
  - `void R_DrawLine(uint32_t* fb, int w, int h, int x0,int y0,int x1,int y1, uint32_t color);` (Bresenham, clipped)
  - `struct Input { bool forward,back,turnLeft,turnRight,strafeLeft,strafeRight; };`
  - `bool i_video::pollEvents(Input& input);`

- [ ] **Step 1: Failing test `tests/test_r_draw.cpp`**

```cpp
#include "doctest.h"
#include "render/r_draw.h"
#include <vector>

TEST_CASE("R_DrawPixel sets the pixel, clipped") {
    std::vector<uint32_t> fb(10 * 10, 0);
    R_DrawPixel(fb.data(), 10, 10, 3, 4, 0xFFFFFFFFu);
    CHECK(fb[4 * 10 + 3] == 0xFFFFFFFFu);
    R_DrawPixel(fb.data(), 10, 10, -1, 0, 0xFFFFFFFFu);   // off-screen: ignored
    R_DrawPixel(fb.data(), 10, 10, 10, 0, 0xFFFFFFFFu);
    CHECK(fb[0] == 0);
}

TEST_CASE("R_DrawLine draws a horizontal row") {
    std::vector<uint32_t> fb(10 * 10, 0);
    R_DrawLine(fb.data(), 10, 10, 2, 5, 6, 5, 0xFFFFFFFFu);
    for (int x = 2; x <= 6; ++x) CHECK(fb[5 * 10 + x] == 0xFFFFFFFFu);
}
```

- [ ] **Step 2: Run, expect fail.**

- [ ] **Step 3: Create `src/render/r_draw.h`**

```cpp
#pragma once
#include "core/doomtype.h"

void R_DrawPixel(uint32_t* fb, int w, int h, int x, int y, uint32_t color);
void R_DrawLine(uint32_t* fb, int w, int h,
                int x0, int y0, int x1, int y1, uint32_t color);
```

- [ ] **Step 4: Create `src/render/r_draw.cpp`**

```cpp
#include "r_draw.h"

void R_DrawPixel(uint32_t* fb, int w, int h, int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    fb[y * w + x] = color;
}

void R_DrawLine(uint32_t* fb, int w, int h,
                int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = (x1 - x0), dy = (y1 - y0);
    int sx = (dx < 0) ? -1 : 1, sy = (dy < 0) ? -1 : 1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    int err = dx - dy;
    for (;;) {
        R_DrawPixel(fb, w, h, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}
```

- [ ] **Step 5: Add `Input` + `pollEvents(Input&)` to `i_video`**

In `src/platform/i_video.h`, add before `namespace i_video`:
```cpp
struct Input {
    bool forward=false, back=false;
    bool turnLeft=false, turnRight=false;
    bool strafeLeft=false, strafeRight=false;
};
```
Add to the `i_video` namespace (alongside the existing `bool pollEvents()`):
```cpp
    bool pollEvents(Input& input);   // fills movement/turn state; returns running
```
In `src/platform/i_video.cpp`, implement (keys: WASD + arrows):
```cpp
bool pollEvents(Input& input) {
    const Uint8* kb = SDL_GetKeyboardState(nullptr);
    input.forward     = kb[SDL_SCANCODE_W] || kb[SDL_SCANCODE_UP];
    input.back        = kb[SDL_SCANCODE_S] || kb[SDL_SCANCODE_DOWN];
    input.turnLeft    = kb[SDL_SCANCODE_LEFT];
    input.turnRight   = kb[SDL_SCANCODE_RIGHT];
    input.strafeLeft  = kb[SDL_SCANCODE_A];
    input.strafeRight = kb[SDL_SCANCODE_D];
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) return false;
    }
    return true;
}
```
(Keep the old no-arg `pollEvents()` too, or replace its body — main will use the `Input` overload.)

- [ ] **Step 6: Add `src/render/r_draw.cpp` to `doomcore`; add `test_r_draw.cpp` to tests.**

- [ ] **Step 7: Build + run, expect the 2 r_draw cases pass.**

- [ ] **Step 8: Commit** — `feat(render): framebuffer pixel/line drawing + keyboard Input`

---

## Task 3: Player + automap render loop (milestone)

**Files:**
- Modify: `src/main.cpp`

**Interfaces:** none new — wires `loadMap` + `R_DrawLine` + `Input` into the main loop.

- [ ] **Step 1: Rewrite the window branch of `src/main.cpp`**

Replace the existing window-mode body (keep the `--listlumps` block and the try/catch) with:

```cpp
#include "play/p_setup.h"
#include "render/r_draw.h"
// (existing includes: i_video, i_system, wadfile, SDL2, <vector>...)

        // --- window / automap mode ---
        std::cout << "doomcpp 0.1.0  (P2a automap)\n";

        WadFile wad("assets/freedoom1.wad");
        MapData map = loadMap(wad, "E1M1");
        std::cout << "E1M1: " << map.vertices.size() << " verts, "
                  << map.lines.size() << " lines\n";

        constexpr int FB_W = 320, FB_H = 200;
        if (!i_video::init(FB_W, FB_H, "doomcpp")) return 1;
        std::vector<uint32_t> fb(static_cast<size_t>(FB_W) * FB_H, 0xFF000000u);

        // player in world (float for the automap; fixed-point physics in P3).
        float px = 1056.0f, py = -3616.0f, ang = 0.0f;  // E1M1 start-ish
        const float moveSpeed = 8.0f;
        const float turnSpeed = 0.05f;
        const float scale = 0.6f;

        Input in{};
        bool running = true;
        while (running) {
            running = i_video::pollEvents(in);

            if (in.forward)     { px += std::sin(ang) * moveSpeed; py += std::cos(ang) * moveSpeed; }
            if (in.back)        { px -= std::sin(ang) * moveSpeed; py -= std::cos(ang) * moveSpeed; }
            if (in.turnLeft)    ang += turnSpeed;
            if (in.turnRight)   ang -= turnSpeed;
            if (in.strafeLeft)  { px += std::cos(ang) * moveSpeed; py -= std::sin(ang) * moveSpeed; }
            if (in.strafeRight) { px -= std::cos(ang) * moveSpeed; py += std::sin(ang) * moveSpeed; }

            std::fill(fb.begin(), fb.end(), 0xFF000000u);

            const float ca = std::cos(ang), sa = std::sin(ang);
            auto project = [&](fixed_t vx, fixed_t vy) -> std::pair<int,int> {
                float dx = (vx >> 16) - px;
                float dy = (vy >> 16) - py;
                // rotate so player faces up; world y is up, screen y is down
                float sx = (dx * ca - dy * sa) * scale;
                float sy = (dx * sa + dy * ca) * scale;
                return { FB_W / 2 + static_cast<int>(sx),
                         FB_H / 2 - static_cast<int>(sy) };
            };

            for (const auto& L : map.lines) {
                if (L.v1 < 0 || L.v1 >= (int)map.vertices.size()) continue;
                if (L.v2 < 0 || L.v2 >= (int)map.vertices.size()) continue;
                auto [x0,y0] = project(map.vertices[L.v1].x, map.vertices[L.v1].y);
                auto [x1,y1] = project(map.vertices[L.v2].x, map.vertices[L.v2].y);
                R_DrawLine(fb.data(), FB_W, FB_H, x0, y0, x1, y1, 0xFFFFFFFFu);
            }
            R_DrawPixel(fb.data(), FB_W, FB_H, FB_W/2, FB_H/2, 0xFF0000FFu); // player dot (red)

            i_video::present(fb.data());
        }
        i_video::shutdown();
        return 0;
```

Add `#include <cmath>` and `#include <utility>` (for std::pair) at the top of main.cpp.

- [ ] **Step 2: Build** — `cmake --build build --config Release`

- [ ] **Step 3: Run the milestone (background, confirm it stays alive = window up)**

```bash
./build/Release/doomcpp.exe > /tmp/auto.txt 2>&1 &
sleep 3
# alive => automap rendering E1M1
grep -E "verts|lines" /tmp/auto.txt   # should print counts
# kill it
taskkill //F //IM doomcpp.exe
```
Expected: prints `E1M1: N verts, M lines`; window stays open showing the 2D map; WASD/arrows move/turn the view; ESC closes. (You run it interactively to confirm movement.)

- [ ] **Step 4: Commit** — `feat(play): walkable 2D automap of E1M1 (player movement + projection)`

---

## P2a Definition of Done

- [ ] Map lumps parsed faithfully (`doomdata.h` layouts), pure functions unit-tested (crafted buffers).
- [ ] `loadMap("E1M1")` works on the real WAD (verified by running).
- [ ] `R_DrawPixel`/`R_DrawLine` unit-tested.
- [ ] `Input` tracks WASD/arrows; ESC still quits.
- [ ] Window shows E1M1's linedefs as a 2D map; player moves/turns.
- [ ] All unit tests pass (14 + 2 + 2 = 18). P2a merged to `main`, tagged `v0.3-p2a-automap`.

## Next: P2b · BSP 3D 墙渲染

Front-to-back BSP traversal from the player's view + seg projection + solid-color wall columns — the "see E1M1 in pseudo-3D" milestone. Reuses the map data loaded here (will extend `p_setup` to parse SECTORS/SEGS/SSECTORS/NODES).
