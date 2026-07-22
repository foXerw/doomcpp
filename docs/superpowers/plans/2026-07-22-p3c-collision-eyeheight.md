# P3c · BLOCKMAP 碰撞 + 平滑眼高 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Stop the player walking through walls (faithful BLOCKMAP line collision + stairstep slide), make the eye height follow the current sector's floor with smooth stepping, drive movement + flat animation from a 35 Hz tic clock (decouples animation from refresh rate), and route `F_SKY1` floors as sky.

**Architecture:** Two new doomcore (SDL-free) modules. `src/play/p_maputl` ports the play-side geometry/locator utilities (`R_PointOnSide`/`R_PointInSubsector`/`sectorOf` from `r_main.c`, `P_BoxOnLineSide`/`P_LineOpening`/`blockLinesInCell` from `p_maputl.c`). `src/play/p_map` ports collision + the player (`P_CheckPosition`/`P_TryMove` from `p_map.c`, a stairstep `P_TrySlide` — the vanilla `P_SlideMove` `stairstep:` fallback — `P_MovePlayer`, and a bob-less `P_CalcHeight` from `p_user.c`). `p_setup` grows `Blockmap` parse (`P_LoadBlockMap`); `r_bsp::renderSeg` marks floor visplanes as sky; `main.cpp` runs a fixed-35 Hz tic accumulator over a `Player`.

**Faithfulness notes:** Functions follow `linuxdoom-1.10` `p_map.c`/`p_maputl.c`/`r_main.c`/`p_setup.c`/`p_user.c`. **Collision math is FLOAT** (continues the documented P2 float simplification — `fixed_t`/`FixedMul` not used here). **Slide is stairstep only** (full `P_SlideMove` path-traversal deferred to P6 with momentum physics). **Omitted as YAGNI for P3c** (no mobj/thinker/specials yet): thing collision (`PIT_CheckThing`), `spechit`/`P_PointOnLineSide` cross-line specials, view bob, momentum, friction. `line_t.special` is parsed but not acted on. Constants taken verbatim from `p_local.h`/`info.c`: `VIEWHEIGHT=41`, `PLAYERRADIUS=16`, `MT_PLAYER height=56`, `MAXMOVE=30`, step/dropoff limit `24`, block cell `128`.

**Tech Stack:** C++17, doctest, MSVC via CMake (unchanged).

## Global Constraints

- Framebuffer RGBA8888 = `(R<<24)|(G<<16)|(B<<8)|A`; opaque black = `0x000000FF`. (Unchanged.)
- World coordinates are **float** map units. `vertex_t` is `fixed_t` (= map units `<< 16`); convert with `static_cast<float>(v.x >> 16)` (arithmetic shift; matches `r_bsp.cpp`). Player `x/y/viewz/angle` are floats.
- Camera basis (unchanged from P2a/P3a/P3b): forward `F=(sin a, cos a)`, right `R=(cos a, -sin a)`. `R_RenderView(fb,w,h,map,tex,px,py,ang,eyeZ,animTick)` signature is UNCHANGED — `main.cpp` now passes `player.viewz` and a per-tic `animTick`.
- `NF_SUBSECTOR = 0x8000` (low bit of a node `children[]` entry marks a subsector leaf; index = `child & ~NF_SUBSECTOR`). A node entry whose value `& NF_SUBSECTOR` is a leaf.
- DOOM line flags (from `doomdata.h`): `ML_BLOCKING=1`, `ML_BLOCKMONSTERS=2`, `ML_TWOSIDED=4`. `line_t.flags` is already parsed.
- BLOCKMAP lump = little-endian `int16` stream: header `[orgx, orgy, width, height]` (orgx/orgy in **map units**, may be negative), then `width*height` per-cell offsets (in shorts from lump start), each pointing at a `-1`-terminated list of line indices. World point → cell: `floor((worldX - orgx) / 128)`.
- One commit per task. Build/test commands:
  ```bash
  export http_proxy=http://127.0.0.1:7890 https_proxy=http://127.0.0.1:7890
  CMAKE="/c/Users/User/cmake-4.4.0-windows-x86_64/bin/cmake.exe"
  "$CMAKE" --build build --config Release
  "$CMAKE" --build build --config Release --target RUN_TESTS
  ```
- Tests are doctest; pure logic (`p_setup`, `p_maputl`, `p_map`) is unit-tested with hand-built `MapData`/`Blockmap`; rendering is verified via `--dumpframe` → BMP → PowerShell `System.Drawing` → PNG → `Read`/`analyze_image` + interactive run.

## File Structure (additions)

```
src/play/p_setup.h / .cpp   += NF_SUBSECTOR, MAPBLOCK, ML_*; struct Blockmap; parseBlockmap; line_t.validcount; MapData.blockmap; loadMap reads marker+10
src/play/p_maputl.h / .cpp   NEW — R_PointOnSide, R_PointInSubsector, sectorOf, BBox, P_BoxOnLineSide, Opening, P_LineOpening, blockLinesInCell
src/play/p_map.h / .cpp      NEW — Player, PosCheck, P_CheckPosition, P_TryMove, P_TrySlide, P_CalcHeight, P_MovePlayer + gameplay constants
src/render/r_bsp.cpp         += renderSeg floor visplane sky marking (F_SKY1 floor)
src/main.cpp                 = Player + 35Hz tic accumulator + viewz + per-tic animTick; --dumpframe eye from start sector; title "P3c collision"
tests/test_p_setup.cpp       += parseBlockmap cases
tests/test_p_maputl.cpp      NEW — point side, point-in-subsector, sectorOf, box-on-line, line opening, block cell
tests/test_p_map.cpp         NEW — P_CheckPosition, P_TryMove, P_TrySlide, P_CalcHeight, P_MovePlayer (hand-built test map)
CMakeLists.txt               doomcore += src/play/p_maputl.cpp, src/play/p_map.cpp
tests/CMakeLists.txt         doomcpp_tests += test_p_maputl.cpp, test_p_map.cpp
```

**Cross-task type contract** (names fixed; defined where noted, consumed by later tasks):

```cpp
// p_setup.h (Task 1)
constexpr uint32_t NF_SUBSECTOR = 0x8000;
constexpr int MAPBLOCK = 128;
constexpr int ML_BLOCKING = 1, ML_BLOCKMONSTERS = 2, ML_TWOSIDED = 4;
struct Blockmap { int orgx=0, orgy=0, width=0, height=0; std::vector<std::int16_t> lump; };
Blockmap parseBlockmap(const byte* d, size_t n);
// struct line_t gets:  mutable int validcount = 0;
// struct MapData gets: Blockmap blockmap;

// p_maputl.h (Tasks 2–3)
int R_PointOnSide(float x, float y, const node_t& n);                 // 0 front, 1 back
int R_PointInSubsector(const MapData& m, float x, float y);           // subsector index
int sectorOf(const MapData& m, int subsectorIdx);                     // sector index, -1 if invalid
struct BBox { float left, right, bottom, top; };
int P_BoxOnLineSide(const BBox& bb, float x1,float y1,float x2,float y2);   // -1 straddle, else 0/1
struct Opening { float top=0, bottom=0, range=0, lowfloor=0; };
Opening P_LineOpening(const MapData& m, const line_t& L);             // one-sided -> range 0
std::vector<int> blockLinesInCell(const Blockmap& bm, int cx, int cy);

// p_map.h (Tasks 4–6)
constexpr float PLAYERRADIUS = 16.0f, PLAYERHEIGHT = 56.0f;
constexpr float VIEWHEIGHT = 41.0f, MAXMOVE = 30.0f, STEP_LIMIT = 24.0f;
struct Player { float x=0,y=0,angle=0,viewz=0; int subsector=0,sector=0; float floorz=0,ceilingz=0; };
struct PosCheck { float floorz=0, ceilingz=0, dropoffz=0; bool ok=true; };
PosCheck P_CheckPosition(const MapData& m, const Blockmap& bm, float x, float y);
bool    P_TryMove(const MapData& m, const Blockmap& bm, Player& p, float nx, float ny);
bool    P_TrySlide(const MapData& m, const Blockmap& bm, Player& p, float dx, float dy);
void    P_CalcHeight(const MapData& m, Player& p);
void    P_MovePlayer(const MapData& m, const Blockmap& bm, Player& p, float forward, float strafe, float turn);
```

---

## Task 1: BLOCKMAP parse + `line_t.validcount` (p_setup)

**Files:**
- Modify: `src/play/p_setup.h`, `src/play/p_setup.cpp`
- Modify: `tests/test_p_setup.cpp`

**Interfaces:**
- Consumes: existing `WadFile::numLumps/lumpName/lumpSize/readLump`, `byte`/`rd_i16` patterns, `loadMap`'s `bytes(offset)` helper.
- Produces: `NF_SUBSECTOR`, `MAPBLOCK`, `ML_BLOCKING/ML_BLOCKMONSTERS/ML_TWOSIDED`, `struct Blockmap`, `parseBlockmap`, `line_t::validcount`, `MapData::blockmap`. `parseBlockmap` is consumed by `loadMap`; `blockLinesInCell` (Task 3) consumes `Blockmap`.

- [ ] **Step 1: Add failing tests to `tests/test_p_setup.cpp`**

Append (reuse the existing `w16` helper at top of the file):

```cpp
#include <cstdint>

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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: FAIL — `Blockmap`/`parseBlockmap` undefined.

- [ ] **Step 3: Implement Blockmap + parseBlockmap + line_t/MapData fields + loadMap wiring**

In `src/play/p_setup.h`:

After the `#include` lines, add the shared constants (above `struct vertex_t`):

```cpp
#include <cstdint>
// ...
// Node child encodings + blockmap geometry (from r_main.c / p_local.h).
constexpr uint32_t NF_SUBSECTOR = 0x8000;   // low bit of a node child marks a subsector leaf
constexpr int MAPBLOCK = 128;               // blockmap cell size in map units (MAPBLOCKUNITS)
// linedef flags (doomdata.h)
constexpr int ML_BLOCKING      = 1;
constexpr int ML_BLOCKMONSTERS = 2;
constexpr int ML_TWOSIDED      = 4;
```

Add `mutable int validcount = 0;` as the last member of `struct line_t`. Add the `Blockmap` struct + `parseBlockmap` decl after the `MapData` struct (before the `parse*` decls):

```cpp
// Parsed BLOCKMAP (p_setup.c::P_LoadBlockMap). lump holds the raw int16 stream:
// header [orgx,orgy,width,height] then per-cell offsets then -1-terminated line lists.
struct Blockmap {
    int orgx = 0, orgy = 0;        // origin in map units (may be negative)
    int width = 0, height = 0;     // cell counts
    std::vector<std::int16_t> lump;
};
Blockmap parseBlockmap(const byte* d, size_t n);
```

Add `Blockmap blockmap;` as the last member of `struct MapData`.

In `src/play/p_setup.cpp`, add (after `parseThings`, before `playerStart`):

```cpp
Blockmap parseBlockmap(const byte* d, size_t n) {
    Blockmap bm;
    size_t count = n / 2;
    bm.lump.resize(count);
    for (size_t i = 0; i < count; ++i) {
        std::uint16_t lo = d[i * 2];
        std::uint16_t hi = d[i * 2 + 1];
        bm.lump[i] = static_cast<std::int16_t>(lo | (hi << 8));
    }
    if (count >= 4) {
        bm.orgx   = bm.lump[0];
        bm.orgy   = bm.lump[1];
        bm.width  = bm.lump[2];
        bm.height = bm.lump[3];
    }
    return bm;
}
```

In `loadMap`, after the `auto th = bytes(1); m.things = ...` line, add the BLOCKMAP read (marker+10):

```cpp
    auto bm = bytes(10);  // ML_BLOCKMAP
    m.blockmap = bm.empty() ? Blockmap{} : parseBlockmap(bm.data(), bm.size());
```

- [ ] **Step 4: Run test to verify it passes**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/play/p_setup.h src/play/p_setup.cpp tests/test_p_setup.cpp
git commit -m "feat(play): parse BLOCKMAP lump + line_t.validcount for collision dedup"
```

---

## Task 2: p_maputl geometry — `R_PointOnSide`, `R_PointInSubsector`, `sectorOf`

**Files:**
- Create: `src/play/p_maputl.h`, `src/play/p_maputl.cpp`
- Create: `tests/test_p_maputl.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `MapData` (nodes/subsectors/segs), `node_t`, `NF_SUBSECTOR` (Task 1).
- Produces: `R_PointOnSide`, `R_PointInSubsector`, `sectorOf` (signatures in the contract). Consumed by `P_CheckPosition` (Task 4) and `P_MovePlayer` (Task 6) and `main.cpp` (Task 7).

- [ ] **Step 1: Register sources in CMake**

In `CMakeLists.txt`, add `src/play/p_maputl.cpp` and `src/play/p_map.cpp` to the `doomcore` `add_library` source list (place them after `src/play/p_setup.cpp`). In `tests/CMakeLists.txt`, add `test_p_maputl.cpp` and `test_p_map.cpp` to the `doomcpp_tests` source list.

- [ ] **Step 2: Write failing tests**

Create `tests/test_p_maputl.cpp`:

```cpp
#include "doctest.h"
#include "play/p_maputl.h"
#include "play/p_setup.h"

namespace {
// Minimal map with a single vertical BSP partition at x=256 splitting two subsectors.
// side 0 = right half (x>256), side 1 = left half (x<256).
struct MiniMap { MapData m; };
static MiniMap buildBspMap() {
    MiniMap mm;
    auto& m = mm.m;
    // sectors: 0 (right), 1 (left)
    sector_t s0; s0.floorheight = 0;   s0.ceilingheight = 128; m.sectors.push_back(s0);
    sector_t s1; s1.floorheight = 10;  s1.ceilingheight = 128; m.sectors.push_back(s1);
    // sides -> sectors
    side_t d0; d0.sector = 0; m.sides.push_back(d0);
    side_t d1; d1.sector = 1; m.sides.push_back(d1);
    // two segs, each in its own subsector
    seg_t g0; g0.linedef = 0; g0.side = 0; g0.frontsector = 0; g0.backsector = -1; m.segs.push_back(g0);
    seg_t g1; g1.linedef = 0; g1.side = 1; g1.frontsector = 1; g1.backsector = -1; m.segs.push_back(g1);
    subsector_t ss0; ss0.segcount = 1; ss0.firstseg = 0; m.subsectors.push_back(ss0);
    subsector_t ss1; ss1.segcount = 1; ss1.firstseg = 1; m.subsectors.push_back(ss1);
    // root node: partition from (256,0) going +Y (dx=0, dy>0)
    node_t n; n.x = 256; n.y = 0; n.dx = 0; n.dy = 256;
    n.children[0] = NF_SUBSECTOR | 0;   // right -> subsector 0
    n.children[1] = NF_SUBSECTOR | 1;   // left  -> subsector 1
    m.nodes.push_back(n);
    return mm;
}
} // namespace

TEST_CASE("R_PointOnSide picks front/back of a partition") {
    node_t n; n.x = 256; n.y = 0; n.dx = 0; n.dy = 256;   // vertical partition at x=256
    CHECK(R_PointOnSide(300, 100, n) == 0);   // right -> front (0)
    CHECK(R_PointOnSide(100, 100, n) == 1);   // left  -> back  (1)
}

TEST_CASE("R_PointInSubsector descends BSP to the right leaf") {
    auto mm = buildBspMap();
    CHECK(R_PointInSubsector(mm.m, 300, 100) == 0);   // right half -> subsector 0
    CHECK(R_PointInSubsector(mm.m, 100, 100) == 1);   // left half  -> subsector 1
}

TEST_CASE("R_PointInSubsector: no nodes returns subsector 0") {
    MapData m;
    subsector_t ss; ss.segcount = 0; ss.firstseg = 0; m.subsectors.push_back(ss);
    CHECK(R_PointInSubsector(m, 5, 5) == 0);
}

TEST_CASE("sectorOf resolves subsector -> sector via its first seg") {
    auto mm = buildBspMap();
    CHECK(sectorOf(mm.m, 0) == 0);   // seg0.frontsector = 0
    CHECK(sectorOf(mm.m, 1) == 1);   // seg1.frontsector = 1
    CHECK(sectorOf(mm.m, 99) == -1); // out of range
}
```

- [ ] **Step 3: Run tests to verify failure**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: FAIL — `p_maputl.h` missing.

- [ ] **Step 4: Implement p_maputl.h + the three functions**

Create `src/play/p_maputl.h`:

```cpp
#pragma once
#include "play/p_setup.h"   // MapData, node_t
#include <vector>

// Partition side test (r_main.c::R_PointOnSide): 0 = front, 1 = back.
int R_PointOnSide(float x, float y, const node_t& n);
// BSP descent to the subsector containing (x,y) (r_main.c::R_PointInSubsector).
int R_PointInSubsector(const MapData& m, float x, float y);
// First seg's front sector for a subsector index (-1 if invalid).
int sectorOf(const MapData& m, int subsectorIdx);

struct BBox { float left, right, bottom, top; };
int  P_BoxOnLineSide(const BBox& bb, float x1, float y1, float x2, float y2);   // Task 3
struct Opening { float top = 0, bottom = 0, range = 0, lowfloor = 0; };
Opening P_LineOpening(const MapData& m, const line_t& L);                         // Task 3
std::vector<int> blockLinesInCell(const Blockmap& bm, int cx, int cy);           // Task 3
```

Create `src/play/p_maputl.cpp`:

```cpp
#include "p_maputl.h"
#include <cmath>

int R_PointOnSide(float x, float y, const node_t& n) {
    float dx = x - n.x, dy = y - n.y;
    float left  = n.dy * dx;
    float right = n.dx * dy;
    return (right < left) ? 0 : 1;   // matches r_bsp.cpp::pointOnSide; equivalent to vanilla
}

int R_PointInSubsector(const MapData& m, float x, float y) {
    if (m.nodes.empty()) return 0;            // single subsector (vanilla special case)
    uint32_t nodenum = static_cast<uint32_t>(m.nodes.size() - 1);
    while (!(nodenum & NF_SUBSECTOR)) {
        const node_t& n = m.nodes[nodenum];
        nodenum = n.children[R_PointOnSide(x, y, n)];
    }
    return static_cast<int>(nodenum & ~NF_SUBSECTOR);
}

int sectorOf(const MapData& m, int subsectorIdx) {
    if (subsectorIdx < 0 || subsectorIdx >= static_cast<int>(m.subsectors.size())) return -1;
    const subsector_t& ss = m.subsectors[subsectorIdx];
    for (int i = 0; i < ss.segcount; ++i) {
        int segi = ss.firstseg + i;
        if (segi >= 0 && segi < static_cast<int>(m.segs.size())) return m.segs[segi].frontsector;
    }
    return -1;
}

// Stubs for Task 3 (so the file links now):
int P_BoxOnLineSide(const BBox&, float, float, float, float) { return -1; }
Opening P_LineOpening(const MapData&, const line_t&) { return Opening{}; }
std::vector<int> blockLinesInCell(const Blockmap&, int, int) { return {}; }
```

- [ ] **Step 5: Run tests to verify pass**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/play/p_maputl.h src/play/p_maputl.cpp tests/test_p_maputl.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(play): R_PointInSubsector + sectorOf (play-side BSP locator)"
```

---

## Task 3: p_maputl — `P_BoxOnLineSide`, `P_LineOpening`, `blockLinesInCell`

**Files:**
- Modify: `src/play/p_maputl.cpp`, `tests/test_p_maputl.cpp`

**Interfaces:**
- Consumes: `Blockmap` (Task 1), `MapData`/`line_t`/`sector_t`, `BBox`/`Opening` (Task 2 header).
- Produces: real `P_BoxOnLineSide`, `P_LineOpening`, `blockLinesInCell` (replacing Task 2 stubs). Consumed by `P_CheckPosition` (Task 4).

- [ ] **Step 1: Add failing tests**

Append to `tests/test_p_maputl.cpp`:

```cpp
#include <cmath>

TEST_CASE("P_BoxOnLineSide: box straddling a line returns -1") {
    BBox bb{ /*left*/240, /*right*/272, /*bottom*/240, /*top*/272};
    // vertical line at x=270
    CHECK(P_BoxOnLineSide(bb, 270, 128, 270, 384) == -1);   // straddle -> collision candidate
}

TEST_CASE("P_BoxOnLineSide: box wholly on one side returns that side") {
    BBox left{100, 120, 100, 120};   // entirely left of vertical line x=270
    CHECK(P_BoxOnLineSide(left, 270, 128, 270, 384) != -1);
    BBox right{400, 420, 100, 120};  // entirely right
    CHECK(P_BoxOnLineSide(right, 270, 128, 270, 384) != -1);
}

TEST_CASE("P_LineOpening: two-sided -> range = min ceil - max floor") {
    MapData m;
    sector_t a; a.floorheight = 0;  a.ceilingheight = 128; m.sectors.push_back(a);
    sector_t b; b.floorheight = 16; b.ceilingheight = 200; m.sectors.push_back(b);
    side_t sa; sa.sector = 0; m.sides.push_back(sa);
    side_t sb; sb.sector = 1; m.sides.push_back(sb);
    line_t L; L.sidenum[0] = 0; L.sidenum[1] = 1; m.lines.push_back(L);
    Opening o = P_LineOpening(m, L);
    CHECK(o.top == doctest::Approx(128));    // min(128,200)
    CHECK(o.bottom == doctest::Approx(16));  // max(0,16)
    CHECK(o.lowfloor == doctest::Approx(0)); // min(0,16)
    CHECK(o.range == doctest::Approx(112));
}

TEST_CASE("P_LineOpening: one-sided line -> range 0") {
    MapData m;
    sector_t a; a.floorheight = 0; a.ceilingheight = 128; m.sectors.push_back(a);
    side_t sa; sa.sector = 0; m.sides.push_back(sa);
    line_t L; L.sidenum[0] = 0; L.sidenum[1] = -1; m.lines.push_back(L);
    Opening o = P_LineOpening(m, L);
    CHECK(o.range == 0.0f);
}

TEST_CASE("blockLinesInCell returns the cell's line list, empty out of range") {
    Blockmap bm;
    bm.orgx = 0; bm.orgy = 0; bm.width = 2; bm.height = 2;
    // same layout as the parseBlockmap test: cell(0,0) -> [5,7,-1]; others empty
    bm.lump = {0,0,2,2,  9,8,8,8,  -1,  5,7,-1};
    auto c00 = blockLinesInCell(bm, 0, 0);
    REQUIRE(c00.size() == 2);
    CHECK(c00[0] == 5);
    CHECK(c00[1] == 7);
    CHECK(blockLinesInCell(bm, 1, 0).empty());   // empty cell
    CHECK(blockLinesInCell(bm, -1, 0).empty());  // out of range
    CHECK(blockLinesInCell(bm, 99, 99).empty());
}
```

- [ ] **Step 2: Run tests to verify failure**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: FAIL — stubs return wrong values (`P_BoxOnLineSide` always -1 etc.).

- [ ] **Step 3: Implement (replace the Task 3 stubs in `src/play/p_maputl.cpp`)**

Replace the three stubs at the bottom of `p_maputl.cpp` with:

```cpp
int P_BoxOnLineSide(const BBox& bb, float x1, float y1, float x2, float y2) {
    float dx = x2 - x1, dy = y2 - y1;
    auto side = [&](float px, float py) -> int {
        float cross = dx * (py - y1) - dy * (px - x1);
        return (cross < 0.0f) ? 1 : 0;     // >= 0 -> front (on-line counts as front)
    };
    int s = side(bb.left, bb.bottom);
    if (s != side(bb.right, bb.bottom)) return -1;
    if (s != side(bb.left, bb.top))     return -1;
    if (s != side(bb.right, bb.top))    return -1;
    return s;   // all four corners agree; -1 above means straddle
}

Opening P_LineOpening(const MapData& m, const line_t& L) {
    Opening o;
    if (L.sidenum[0] < 0 || L.sidenum[0] >= static_cast<int>(m.sides.size())) return o;
    int frontSec = m.sides[L.sidenum[0]].sector;
    if (frontSec < 0 || frontSec >= static_cast<int>(m.sectors.size())) return o;
    if (L.sidenum[1] < 0 || L.sidenum[1] >= static_cast<int>(m.sides.size())) return o;  // one-sided
    int backSec = m.sides[L.sidenum[1]].sector;
    if (backSec < 0 || backSec >= static_cast<int>(m.sectors.size())) return o;
    float fH_f = static_cast<float>(m.sectors[frontSec].floorheight);
    float cH_f = static_cast<float>(m.sectors[frontSec].ceilingheight);
    float fH_b = static_cast<float>(m.sectors[backSec].floorheight);
    float cH_b = static_cast<float>(m.sectors[backSec].ceilingheight);
    o.top      = std::fmin(cH_f, cH_b);
    o.bottom   = std::fmax(fH_f, fH_b);
    o.lowfloor = std::fmin(fH_f, fH_b);
    o.range    = o.top - o.bottom;
    return o;
}

std::vector<int> blockLinesInCell(const Blockmap& bm, int cx, int cy) {
    std::vector<int> out;
    if (cx < 0 || cy < 0 || cx >= bm.width || cy >= bm.height) return out;
    int offset = bm.lump[4 + cy * bm.width + cx];
    if (offset < 0 || offset >= static_cast<int>(bm.lump.size())) return out;
    for (int i = offset; i < static_cast<int>(bm.lump.size()); ++i) {
        std::int16_t v = bm.lump[i];
        if (v == -1) break;
        out.push_back(v);
    }
    return out;
}
```

(Add `#include <algorithm>` at the top of `p_maputl.cpp` for `std::fmin/std::fmax` — actually these are `<cmath>`; `<cmath>` is already included. Keep `<cmath>`.)

- [ ] **Step 4: Run tests to verify pass**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/play/p_maputl.cpp tests/test_p_maputl.cpp
git commit -m "feat(play): P_BoxOnLineSide + P_LineOpening + blockmap cell iteration"
```

---

## Task 4: p_map — `Player`, `P_CheckPosition`

**Files:**
- Create: `src/play/p_map.h`, `src/play/p_map.cpp`
- Create: `tests/test_p_map.cpp`

**Interfaces:**
- Consumes: `R_PointInSubsector`/`sectorOf`/`BBox`/`P_BoxOnLineSide`/`P_LineOpening`/`blockLinesInCell` (Tasks 2–3), `Blockmap`/`MapData`/`MAPBLOCK`/`ML_BLOCKING` (Task 1).
- Produces: `Player`, `PosCheck`, `P_CheckPosition`, gameplay constants (`PLAYERRADIUS` etc.). Consumed by Tasks 5–7.

- [ ] **Step 1: Write failing tests**

Create `tests/test_p_map.cpp`:

```cpp
#include "doctest.h"
#include "play/p_map.h"
#include "play/p_maputl.h"
#include "play/p_setup.h"

namespace {
// Hand-built collision test map. A single spatial sector A (floor 0, ceil 128) with:
//  - line L0 at x=270 (y 128..384): one-sided WALL (oneSided=true) OR a two-sided PORTAL
//    to sector B (floor = backFloor, ceil 128).
// Player at (256,256) (bbox [240,272]^2) straddles L0; at (64,64) it's in open space.
// Blockmap is 4x4 (org 0,0); L0 listed in cells (2,1) and (2,2).
struct TestMap { MapData m; Blockmap bm; };
static TestMap buildTestMap(bool oneSided, int backFloor) {
    TestMap tm;
    auto& m = tm.m;
    m.vertices.push_back({270 << 16, 128 << 16});   // v0
    m.vertices.push_back({270 << 16, 384 << 16});   // v1
    line_t L; L.v1 = 0; L.v2 = 1; L.flags = ML_TWOSIDED; L.special = 0; L.tag = 0;
    L.sidenum[0] = 0; L.sidenum[1] = oneSided ? -1 : 1;
    m.lines.push_back(L);
    side_t s0; s0.sector = 0; m.sides.push_back(s0);
    if (!oneSided) { side_t s1; s1.sector = 1; m.sides.push_back(s1); }
    sector_t A; A.floorheight = 0;        A.ceilingheight = 128; m.sectors.push_back(A);
    sector_t B; B.floorheight = backFloor; B.ceilingheight = 128; m.sectors.push_back(B);
    seg_t g; g.linedef = 0; g.side = 0; g.frontsector = 0; g.backsector = oneSided ? -1 : 1;
    m.segs.push_back(g);
    subsector_t ss; ss.segcount = 1; ss.firstseg = 0; m.subsectors.push_back(ss);  // no nodes -> ss0

    Blockmap& bm = tm.bm;
    bm.orgx = 0; bm.orgy = 0; bm.width = 4; bm.height = 4;
    // lump: header[0..3]; offsets[4..19] (cell linear = cy*4+cx); index20 = empty [-1];
    // index21..22 = list [0, -1]. cells (2,1) and (2,2) -> offset 21; all others -> 20.
    bm.lump = {0,0,4,4,
               20,20,20,20,  20,20,21,20,  20,20,21,20,  20,20,20,20,
               -1,  0,-1};
    return tm;
}
} // namespace

TEST_CASE("P_CheckPosition: one-sided wall blocks") {
    auto tm = buildTestMap(/*oneSided*/true, 0);
    PosCheck pc = P_CheckPosition(tm.m, tm.bm, 256, 256);
    CHECK(pc.ok == false);
}

TEST_CASE("P_CheckPosition: open space is clear, seeds floor/ceiling") {
    auto tm = buildTestMap(true, 0);
    PosCheck pc = P_CheckPosition(tm.m, tm.bm, 64, 64);   // cell (0,0): no lines
    CHECK(pc.ok == true);
    CHECK(pc.floorz == 0.0f);
    CHECK(pc.ceilingz == 128.0f);
}

TEST_CASE("P_CheckPosition: two-sided portal raises floor, keeps ok") {
    auto tm = buildTestMap(false, 8);   // back floor +8 (a step up)
    PosCheck pc = P_CheckPosition(tm.m, tm.bm, 256, 256);
    CHECK(pc.ok == true);               // not a solid blocker
    CHECK(pc.floorz == 8.0f);           // raised to back floor
    CHECK(pc.dropoffz == 0.0f);         // min(front 0, back 8)
}
```

- [ ] **Step 2: Run tests to verify failure**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: FAIL — `p_map.h` missing.

- [ ] **Step 3: Implement p_map.h + P_CheckPosition**

Create `src/play/p_map.h`:

```cpp
#pragma once
#include "play/p_setup.h"
#include "play/p_maputl.h"

// Gameplay constants (p_local.h / info.c MT_PLAYER).
constexpr float PLAYERRADIUS = 16.0f;   // PLAYERRADIUS / mobjinfo[MT_PLAYER].radius
constexpr float PLAYERHEIGHT = 56.0f;   // mobjinfo[MT_PLAYER].height
constexpr float VIEWHEIGHT   = 41.0f;   // VIEWHEIGHT
constexpr float MAXMOVE      = 30.0f;   // MAXMOVE (per-tic displacement cap)
constexpr float STEP_LIMIT   = 24.0f;   // step-up / dropoff limit

struct Player {
    float x = 0, y = 0, angle = 0;
    float viewz = 0;
    int   subsector = 0, sector = 0;
    float floorz = 0, ceilingz = 0;
};

struct PosCheck { float floorz = 0, ceilingz = 0, dropoffz = 0; bool ok = true; };

// Faithful P_CheckPosition (things omitted): can a PLAYERRADIUS circle fit at (x,y)?
PosCheck P_CheckPosition(const MapData& m, const Blockmap& bm, float x, float y);
bool P_TryMove(const MapData& m, const Blockmap& bm, Player& p, float nx, float ny);     // Task 5
bool P_TrySlide(const MapData& m, const Blockmap& bm, Player& p, float dx, float dy);    // Task 5
void P_CalcHeight(const MapData& m, Player& p);                                          // Task 6
void P_MovePlayer(const MapData& m, const Blockmap& bm, Player& p, float fwd, float str, float turn);  // Task 6
```

Create `src/play/p_map.cpp`:

```cpp
#include "p_map.h"
#include <algorithm>
#include <cmath>

PosCheck P_CheckPosition(const MapData& m, const Blockmap& bm, float x, float y) {
    PosCheck pc;
    BBox bb{x - PLAYERRADIUS, x + PLAYERRADIUS, y - PLAYERRADIUS, y + PLAYERRADIUS};

    int sec = sectorOf(m, R_PointInSubsector(m, x, y));
    if (sec < 0) { pc.ok = false; return pc; }
    pc.floorz = pc.dropoffz = static_cast<float>(m.sectors[sec].floorheight);
    pc.ceilingz = static_cast<float>(m.sectors[sec].ceilingheight);

    int xl = static_cast<int>(std::floor((bb.left   - bm.orgx) / MAPBLOCK));
    int xh = static_cast<int>(std::floor((bb.right  - bm.orgx) / MAPBLOCK));
    int yl = static_cast<int>(std::floor((bb.bottom - bm.orgy) / MAPBLOCK));
    int yh = static_cast<int>(std::floor((bb.top    - bm.orgy) / MAPBLOCK));

    static int validgen = 0;  // generation counter for per-call line dedup (vanilla validcount)
    ++validgen;
    for (int cy = yl; cy <= yh; ++cy)
        for (int cx = xl; cx <= xh; ++cx)
            for (int li : blockLinesInCell(bm, cx, cy)) {
                if (li < 0 || li >= static_cast<int>(m.lines.size())) continue;
                line_t& L = m.lines[li];
                if (L.validcount == validgen) continue;
                L.validcount = validgen;
                if (L.v1 < 0 || L.v1 >= (int)m.vertices.size()) continue;
                if (L.v2 < 0 || L.v2 >= (int)m.vertices.size()) continue;
                float ax1 = static_cast<float>(m.vertices[L.v1].x >> 16);
                float ay1 = static_cast<float>(m.vertices[L.v1].y >> 16);
                float ax2 = static_cast<float>(m.vertices[L.v2].x >> 16);
                float ay2 = static_cast<float>(m.vertices[L.v2].y >> 16);
                float lminx = std::fmin(ax1, ax2), lmaxx = std::fmax(ax1, ax2);
                float lminy = std::fmin(ay1, ay2), lmaxy = std::fmax(ay1, ay2);
                if (bb.right <= lminx || bb.left >= lmaxx ||
                    bb.top   <= lminy || bb.bottom >= lmaxy) continue;          // bbox early-out
                if (P_BoxOnLineSide(bb, ax1, ay1, ax2, ay2) != -1) continue;    // not straddling
                if (L.sidenum[1] < 0) { pc.ok = false; return pc; }              // one-sided wall
                if (L.flags & ML_BLOCKING) { pc.ok = false; return pc; }         // explicit block
                Opening op = P_LineOpening(m, L);
                if (op.top      < pc.ceilingz) pc.ceilingz = op.top;
                if (op.bottom   > pc.floorz)   pc.floorz   = op.bottom;
                if (op.lowfloor < pc.dropoffz) pc.dropoffz = op.lowfloor;
            }
    return pc;
}

// Stubs for Tasks 5–6 (so the file links now):
bool P_TryMove(const MapData&, const Blockmap&, Player&, float, float) { return false; }
bool P_TrySlide(const MapData&, const Blockmap&, Player&, float, float) { return false; }
void P_CalcHeight(const MapData&, Player&) {}
void P_MovePlayer(const MapData&, const Blockmap&, Player&, float, float, float) {}
```

- [ ] **Step 4: Run tests to verify pass**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/play/p_map.h src/play/p_map.cpp tests/test_p_map.cpp
git commit -m "feat(play): P_CheckPosition (BLOCKMAP line collision, player-only)"
```

---

## Task 5: p_map — `P_TryMove` + stairstep `P_TrySlide`

**Files:**
- Modify: `src/play/p_map.cpp`, `tests/test_p_map.cpp`

**Interfaces:**
- Consumes: `P_CheckPosition` + constants (Task 4).
- Produces: real `P_TryMove` (validate + commit) and `P_TrySlide` (full → X-only → Y-only). Consumed by `P_MovePlayer` (Task 6).

- [ ] **Step 1: Add failing tests**

Append to `tests/test_p_map.cpp`:

```cpp
TEST_CASE("P_TryMove: passable step commits and raises floorz") {
    auto tm = buildTestMap(false, 8);   // back floor +8
    Player p; p.x = 100; p.y = 100; p.floorz = 0;
    CHECK(P_TryMove(tm.m, tm.bm, p, 256, 256) == true);
    CHECK(p.x == 256.0f);
    CHECK(p.y == 256.0f);
    CHECK(p.floorz == 8.0f);            // adopted the stepped-up floor
}

TEST_CASE("P_TryMove: step too high (>24) is rejected") {
    auto tm = buildTestMap(false, 32);  // back floor +32
    Player p; p.x = 100; p.y = 100; p.floorz = 0;
    CHECK(P_TryMove(tm.m, tm.bm, p, 256, 256) == false);
    CHECK(p.x == 100.0f);               // unchanged
    CHECK(p.y == 100.0f);
}

TEST_CASE("P_TryMove: dropoff >24 is rejected") {
    auto tm = buildTestMap(false, -32); // back floor -32
    Player p; p.x = 100; p.y = 100; p.floorz = 0;
    CHECK(P_TryMove(tm.m, tm.bm, p, 256, 256) == false);
    CHECK(p.x == 100.0f);
}

TEST_CASE("P_TryMove: one-sided wall rejected; open target accepted") {
    auto tm = buildTestMap(true, 0);
    Player p; p.x = 100; p.y = 100; p.floorz = 0;
    CHECK(P_TryMove(tm.m, tm.bm, p, 256, 256) == false);   // into wall
    CHECK(P_TryMove(tm.m, tm.bm, p, 64, 64) == true);      // open
    CHECK(p.x == 64.0f);
}

TEST_CASE("P_TrySlide: fully blocked -> no move, returns false") {
    auto tm = buildTestMap(true, 0);
    Player p; p.x = 250; p.y = 200; p.floorz = 0;          // just left of wall at x=270
    CHECK(P_TrySlide(tm.m, tm.bm, p, 30, 0) == false);     // +X into wall, no Y to slide
    CHECK(p.x == 250.0f);
    CHECK(p.y == 200.0f);
}

TEST_CASE("P_TrySlide: diagonal into wall slides along the perpendicular axis") {
    auto tm = buildTestMap(true, 0);
    Player p; p.x = 250; p.y = 200; p.floorz = 0;
    // full (280,240) straddles wall -> blocked; X-only (280,200) straddles -> blocked;
    // Y-only (250,240) is clear -> commits. Player ends at (250,240).
    CHECK(P_TrySlide(tm.m, tm.bm, p, 30, 40) == true);
    CHECK(p.x == 250.0f);              // X component eaten by wall
    CHECK(p.y == 240.0f);              // Y component kept (slid along wall)
}
```

- [ ] **Step 2: Run tests to verify failure**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: FAIL — `P_TryMove`/`P_TrySlide` are stubs (always false).

- [ ] **Step 3: Implement P_TryMove + P_TrySlide (replace stubs)**

Replace the `P_TryMove`/`P_TrySlide` stubs in `src/play/p_map.cpp`:

```cpp
bool P_TryMove(const MapData& m, const Blockmap& bm, Player& p, float nx, float ny) {
    PosCheck pc = P_CheckPosition(m, bm, nx, ny);
    if (!pc.ok) return false;
    if (pc.ceilingz - pc.floorz < PLAYERHEIGHT) return false;   // doesn't fit vertically
    if (pc.ceilingz - p.floorz  < PLAYERHEIGHT) return false;   // not enough headroom at current z
    if (pc.floorz   - p.floorz  > STEP_LIMIT)   return false;   // step up too high
    if (pc.floorz   - pc.dropoffz > STEP_LIMIT) return false;   // would stand over a dropoff
    p.x = nx; p.y = ny;
    p.floorz = pc.floorz;
    p.ceilingz = pc.ceilingz;
    return true;
}

bool P_TrySlide(const MapData& m, const Blockmap& bm, Player& p, float dx, float dy) {
    if (P_TryMove(m, bm, p, p.x + dx, p.y + dy)) return true;   // full move
    if (dx != 0.0f && P_TryMove(m, bm, p, p.x + dx, p.y)) return true;   // X-only
    if (dy != 0.0f && P_TryMove(m, bm, p, p.x, p.y + dy)) return true;   // Y-only
    return false;
}
```

- [ ] **Step 4: Run tests to verify pass**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/play/p_map.cpp tests/test_p_map.cpp
git commit -m "feat(play): P_TryMove (step/dropoff/headroom) + stairstep P_TrySlide"
```

---

## Task 6: p_map — `P_CalcHeight` + `P_MovePlayer`

**Files:**
- Modify: `src/play/p_map.cpp`, `tests/test_p_map.cpp`

**Interfaces:**
- Consumes: `P_TrySlide`, `R_PointInSubsector`, `sectorOf`, `VIEWHEIGHT`, `MAXMOVE` (earlier tasks).
- Produces: real `P_CalcHeight` (smooth eye-z) and `P_MovePlayer` (one 35 Hz tic). Consumed by `main.cpp` (Task 7).

- [ ] **Step 1: Add failing tests**

Append to `tests/test_p_map.cpp`:

```cpp
TEST_CASE("P_CalcHeight: viewz approaches floor+VIEWHEIGHT, clamped under ceiling") {
    MapData m;
    sector_t s; s.floorheight = 0; s.ceilingheight = 40; m.sectors.push_back(s);  // low ceiling
    Player p; p.sector = 0; p.viewz = 0;
    P_CalcHeight(m, p);   // target = 0+41 = 41, but ceiling-4 = 36 -> clamp
    CHECK(p.viewz == doctest::Approx(36.0f));
}

TEST_CASE("P_CalcHeight: viewz converges to floor+VIEWHEIGHT over several tics") {
    MapData m;
    sector_t s; s.floorheight = 100; s.ceilingheight = 1000; m.sectors.push_back(s);
    Player p; p.sector = 0; p.viewz = 100;   // starts at floor, target = 141
    for (int i = 0; i < 40; ++i) P_CalcHeight(m, p);
    CHECK(p.viewz == doctest::Approx(141.0f).epsilon(0.01f));
    // monotonic approach
    Player q; q.sector = 0; q.viewz = 100;
    float prev = q.viewz;
    for (int i = 0; i < 5; ++i) { P_CalcHeight(m, q); CHECK(q.viewz >= prev); prev = q.viewz; }
}

TEST_CASE("P_MovePlayer: forward in open space moves + re-sectors + keeps viewz near target") {
    auto tm = buildTestMap(true, 0);
    Player p; p.x = 64; p.y = 64; p.angle = 0; p.floorz = 0; p.viewz = 41; p.sector = 0;
    // angle 0 -> forwardVec=(sin0,cos0)=(0,1): +Y. A small forward nudge stays in open space.
    P_MovePlayer(tm.m, tm.bm, p, /*fwd*/5, /*str*/0, /*turn*/0);
    CHECK(p.y > 64.0f);
    CHECK(p.x == 64.0f);
    CHECK(p.sector == 0);
    CHECK(p.viewz == doctest::Approx(41.0f).epsilon(0.01f));
}

TEST_CASE("P_MovePlayer: walking into a wall does not cross it") {
    auto tm = buildTestMap(true, 0);
    Player p; p.x = 250; p.y = 200; p.angle = 0; p.floorz = 0; p.viewz = 41; p.sector = 0;
    P_MovePlayer(tm.m, tm.bm, p, /*fwd*/30, 0, 0);   // forward +Y away from wall -> fine
    CHECK(p.y > 200.0f);
    // now face the wall (+X): angle so forwardVec ~ (+1,0). angle=PI/2 -> (sin,cos)=(1,0).
    p.angle = 3.14159265f / 2.0f;
    float before = p.x;
    P_MovePlayer(tm.m, tm.bm, p, 30, 0, 0);          // tries to step into wall at x=270
    CHECK(p.x < 270.0f);                              // did not cross the wall
    CHECK(p.x >= before);                             // may have slid/none; never past wall
}
```

- [ ] **Step 2: Run tests to verify failure**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: FAIL — `P_CalcHeight`/`P_MovePlayer` are stubs (no-op).

- [ ] **Step 3: Implement P_CalcHeight + P_MovePlayer (replace stubs)**

Replace the `P_CalcHeight`/`P_MovePlayer` stubs in `src/play/p_map.cpp`:

```cpp
void P_CalcHeight(const MapData& m, Player& p) {
    if (p.sector < 0 || p.sector >= static_cast<int>(m.sectors.size())) return;
    float target = static_cast<float>(m.sectors[p.sector].floorheight) + VIEWHEIGHT;
    p.viewz += (target - p.viewz) * 0.125f;                    // ~vanilla deltaviewheight >> 3
    float cap = static_cast<float>(m.sectors[p.sector].ceilingheight) - 4.0f;
    if (p.viewz > cap) p.viewz = cap;
}

void P_MovePlayer(const MapData& m, const Blockmap& bm, Player& p,
                  float forward, float strafe, float turn) {
    p.angle += turn;
    float fs = std::sin(p.angle), fc = std::cos(p.angle);
    // forwardVec=(sin,cos), rightVec=(cos,-sin)  (camera basis from P2a)
    float dx = fs * forward + fc * strafe;
    float dy = fc * forward - fs * strafe;
    float mag = std::sqrt(dx * dx + dy * dy);
    if (mag > MAXMOVE) { dx *= MAXMOVE / mag; dy *= MAXMOVE / mag; }
    P_TrySlide(m, bm, p, dx, dy);
    int ss = R_PointInSubsector(m, p.x, p.y);
    p.subsector = ss;
    p.sector = sectorOf(m, ss);
    if (p.sector >= 0) p.floorz = static_cast<float>(m.sectors[p.sector].floorheight);
    P_CalcHeight(m, p);
}
```

- [ ] **Step 4: Run tests to verify pass**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/play/p_map.cpp tests/test_p_map.cpp
git commit -m "feat(play): P_MovePlayer (35Hz tic step) + smooth P_CalcHeight eye-z"
```

---

## Task 7: floor-sky (r_bsp) + 35Hz tic loop + Player wiring (main)

**Files:**
- Modify: `src/render/r_bsp.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `Player`, `P_MovePlayer`, `R_PointInSubsector`, `sectorOf`, `VIEWHEIGHT` (Tasks 2/4/6); `TextureLookup::isSky` (P3b); `R_RenderView` (unchanged signature).
- Produces: floor-sky rendering; the 35 Hz tic loop; per-tic `animTick`. No new public API.

- [ ] **Step 1: Route floor-sky in renderSeg**

In `src/render/r_bsp.cpp`, in `renderSeg`, alongside `bool skyCeilF = TextureLookup::isSky(ceilPicF);` (around line 118) add:

```cpp
    bool skyFloorF = TextureLookup::isSky(floorPicF);
```

Then change the floor-plane `R_FindPlane` call (the `if (markFloor) { ... }` block, ~line 142) to pass `nullptr`/sky when the floor is sky:

```cpp
    if (markFloor) {
        const Flat* ff = skyFloorF ? nullptr : c.tex->flatForFrame(floorPicF, c.tick);
        floorPlane = R_FindPlane(c.vps, static_cast<float>(fH_f), ff, light, skyFloorF, c.w);
        floorPlane = R_CheckPlane(c.vps, floorPlane, x0, x1);
    }
```

(`R_FindPlane` already forces `height=0`/`light=0` for sky, so floor-sky and ceiling-sky merge into one sky plane — no new merge key needed.)

- [ ] **Step 2: Wire Player + 35Hz tic loop in main.cpp**

In `src/main.cpp`, add includes after the existing `play/p_setup.h` include:

```cpp
#include "play/p_maputl.h"
#include "play/p_map.h"
```

Replace the `--dumpframe` block's eye handling. Find the lines:
```cpp
                constexpr int FW = 320, FH = 200;
                std::vector<std::uint32_t> fb(static_cast<size_t>(FW) * FH, 0);
                R_RenderView(fb.data(), FW, FH, map, tex, px, py, ang, 41.0f, 0);
```
and replace with:
```cpp
                constexpr int FW = 320, FH = 200;
                std::vector<std::uint32_t> fb(static_cast<size_t>(FW) * FH, 0);
                int ss0 = R_PointInSubsector(map, px, py);
                int sec0 = sectorOf(map, ss0);
                float eyeZ0 = (sec0 >= 0 ? static_cast<float>(map.sectors[sec0].floorheight) : 0.0f) + VIEWHEIGHT;
                R_RenderView(fb.data(), FW, FH, map, tex, px, py, ang, eyeZ0, 0);
```

Replace the window-mode block (from `std::cout << "doomcpp 0.1.0  (P3b visplanes)..."` through the end of the `while (running)` loop) with:

```cpp
        std::cout << "doomcpp 0.1.0  (P3c collision)  WASD move, arrows turn, ESC quit\n";
        WadFile wad("assets/freedoom1.wad");
        MapData map = loadMap(wad, "E1M1");
        TextureLookup tex(wad);
        float sx, sy, sang;
        if (!playerStart(map, sx, sy, sang)) { sx = 0; sy = 0; sang = 0; }

        Player p;
        p.x = sx; p.y = sy; p.angle = sang;
        {   int ss = R_PointInSubsector(map, p.x, p.y); p.subsector = ss; p.sector = sectorOf(map, ss);
            p.floorz = (p.sector >= 0) ? static_cast<float>(map.sectors[p.sector].floorheight) : 0.0f;
            p.viewz  = p.floorz + VIEWHEIGHT; }

        constexpr int FB_W = 320, FB_H = 200;
        if (!i_video::init(FB_W, FB_H, "doomcpp - collision")) return 1;
        std::vector<std::uint32_t> fb(static_cast<size_t>(FB_W) * FB_H, 0);

        const float moveSpeed = 5.0f, turnSpeed = 0.07f;   // per 35Hz tic; tunable
        constexpr uint32_t TIC_MS = 1000 / 35;             // 28 ms
        Input in{};
        bool running = true;
        uint32_t prev = SDL_GetTicks();
        uint32_t acc = 0;
        uint32_t ticCount = 0;
        while (running) {
            running = i_video::pollEvents(in);
            uint32_t now = SDL_GetTicks();
            acc += (now - prev);
            prev = now;
            for (int n = 0; n < 4 && acc >= TIC_MS; ++n, acc -= TIC_MS) {   // cap 4 tics/frame
                float fwd = (in.forward ? 1.0f : 0.0f) - (in.back ? 1.0f : 0.0f);
                float str = (in.strafeRight ? 1.0f : 0.0f) - (in.strafeLeft ? 1.0f : 0.0f);
                float trn = (in.turnLeft ? 1.0f : 0.0f) - (in.turnRight ? 1.0f : 0.0f);
                P_MovePlayer(map, map.blockmap, p, fwd * moveSpeed, str * moveSpeed, trn * turnSpeed);
                ++ticCount;
            }
            R_RenderView(fb.data(), FB_W, FB_H, map, tex, p.x, p.y, p.angle, p.viewz, ticCount);
            i_video::present(fb.data());
        }
        i_video::shutdown();
        return 0;
```

(Remove the now-unused `moveSpeed`/`turnSpeed`/`tick`/`px/py/ang` locals from the old block — they are replaced by the above. `SDL_GetTicks` is available via the existing `<SDL2/SDL.h>` include.)

- [ ] **Step 3: Build + run full test suite**

Run: `"$CMAKE" --build build --config Release && "$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: clean build, all unit tests PASS (count grows by the new p_setup/p_maputl/p_map cases).

- [ ] **Step 4: Visual checkpoint — dump a frame and verify eye height + no regression**

```bash
build/Release/doomcpp.exe --dumpframe assets/freedoom1.wad build/p3c_check.bmp 90
powershell.exe -NoProfile -Command "Add-Type -AssemblyName System.Drawing; \$i=[System.Drawing.Bitmap]::new('build/p3c_check.bmp'); \$i.Save('build/p3c_check.png',[System.Drawing.Imaging.ImageFormat]::Png); \$i.Dispose()"
```

Then `Read`/`analyze_image` on `build/p3c_check.png`. **Acceptance (must hold before proceeding):**
- Frame renders at least as well as P3b (textured walls/floors/ceilings, sky, distance shading intact) — the only change is `eyeZ` now derives from the start sector's floor; the view should look essentially identical to P3b's dump from the same start position (E1M1 start sector floor is 0 → eyeZ still ~41).
- No crash, no black screen.

- [ ] **Step 5: Interactive verification — collision + smooth eye + animation**

Launch `build/Release/doomcpp.exe`. Drive with WASD/arrows through E1M1. **Acceptance:**
- Player **cannot walk through walls** — approach a wall and stop; slide along it when moving diagonally.
- Walking onto a **raised floor / stairs** raises the view smoothly (no snap); stepping down lowers it.
- Flat **animation** (nukage/water) plays steadily and does **not** speed up/slow down with the window's frame rate (the 35 Hz tic decouple). To confirm: the anim cadence should feel constant whether the window is idle or you're spinning the view hard.
- **Floor-sky** (if a sector with `F_SKY1` floor is reachable — rare in E1M1; if not reachable, just confirm no regression) renders as the solid sky color.

If movement speed/turn feels off, tune `moveSpeed`/`turnSpeed` in `main.cpp`, rebuild, re-test. Record any residual artifacts.

- [ ] **Step 6: Commit**

```bash
git add src/render/r_bsp.cpp src/main.cpp
git commit -m "feat(play): BLOCKMAP collision + smooth eye-height + 35Hz tic + floor-sky"
```

---

## Task 8: Final verification + ship

**Files:** none (verification + release only).

- [ ] **Step 1: Clean build + full tests**

```bash
"$CMAKE" --build build --config Release
"$CMAKE" --build build --config Release --target RUN_TESTS
```
Expected: clean build, all tests PASS.

- [ ] **Step 2: Final visual dump**

```bash
build/Release/doomcpp.exe --dumpframe assets/freedoom1.wad build/p3c_final.bmp 90
# convert + analyze_image: no regression vs P3b; eye from start-sector floor
```

- [ ] **Step 3: Update project memory + merge + tag**

Update `doomcpp-project-overview` memory: mark **P3c BLOCKMAP 碰撞 + 眼高 ✅ DONE** (`v0.7-p3c-collision`, merged 2026-07-22), advance **P4 精灵 NEXT**; record the two P3b tech-debts as resolved (anim decoupled to 35Hz; floor-sky routed) and the deferred items (full `P_SlideMove`, view bob, momentum/friction, spechit specials → P6). Update README roadmap checkbox. Then:

```bash
git add README.md docs   # roadmap + any doc/memory updates
git commit -m "docs(p3c): README milestone" || true
git checkout main
git merge --no-ff feat/p3c-collision-eyeheight -m "merge: P3c BLOCKMAP collision + eye-height complete"
git tag v0.7-p3c-collision
```

---

## Self-Review (completed by plan author)

- **Spec coverage:** §1 goal (collision+eye+tic+floorsky) → T1,T4,T5,T6,T7. §2 faithful simplifications (float; stairstep; no bob/momentum/spechit) → Global Constraints + T5/T6 comments. §3 modules (p_maputl T2/T3, p_map T4/T5/T6, p_setup T1, r_bsp T7) ✓. §4.1 constants → p_map.h (T4) + MAPBLOCK/ML_* in p_setup.h (T1). §4.2 BLOCKMAP parse → T1. §4.3 point location → T2. §4.4 opening/box-side → T3. §4.5 P_CheckPosition/PIT_CheckLine/T_TryMove → T4/T5. §4.6 P_MovePlayer+stairstep → T5/T6. §5 P_CalcHeight → T6. §6.1 tic clock → T7. §6.2 floor-sky → T7. §7 main wiring → T7. §8 tests → T1/T2/T3/T4/T5/T6 + T7 visual. §9 decisions → embedded. §10 release → T8. All covered.
- **Placeholder scan:** `moveSpeed`/`turnSpeed` are concrete values (5.0/0.07) with an explicit tune step (T7 Step 5), matching P3b's pattern — not placeholders. `buildTestMap` geometry is fully specified with exact coordinates + the int16 lump literal. No TBD/TODO. `P_PointOnLineSide` deliberately omitted (YAGNI, spechit deferred) — noted in Architecture, not a placeholder.
- **Type consistency:** `Blockmap`/`parseBlockmap` identical in contract, T1 decl, T1 impl, T3 test, T4 `buildTestMap`. `Player`/`PosCheck`/`P_*` signatures identical across contract, p_map.h decls, impls, and T7 main.cpp call sites (`P_MovePlayer(map, map.blockmap, p, fwd*moveSpeed, str*moveSpeed, trn*turnSpeed)`; `R_RenderView(..., p.viewz, ticCount)`). `BBox{left,right,bottom,top}` field order identical in contract, p_maputl.h, P_BoxOnLineSide, P_CheckPosition, and tests. `R_PointInSubsector`/`sectorOf` used consistently in T4 (`sectorOf(m, R_PointInSubsector(...))`), T6, T7. `line_t.validcount` added once (T1), mutated via `mutable` in T4 (no const_cast). `blockLinesInCell(bm, cx, cy)` order matches T3 test, T4 impl.
- **Geometry sanity (test maps):** `buildTestMap` player at (256,256) bbox [240,272]² straddles the x=270 line → wall blocks (T4) ✓; portal back-floor raises `floorz` (T4) and gates step/dropoff in T5 ✓; (64,64) in cell (0,0) is empty → clear (T4) ✓; `P_TrySlide` from (250,200): full(280,240) & X-only(280,200) straddle x=270 → blocked, Y-only(250,240) bbox right=266<270 → clear → slides to (250,240) (T5) ✓. Blockmap lump literal verified: cell linear `cy*4+cx`, (2,1)→index 10, (2,2)→index 14 both =21 (list [0,-1]); others=20 (empty [-1]) ✓.
