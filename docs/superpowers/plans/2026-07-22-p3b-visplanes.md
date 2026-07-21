# P3b · Visplane 地板/天花板 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Fill P3a's black floor/ceiling regions with real DOOM flats using the visplane algorithm — load 64×64 flats, build/merge visplanes during BSP rendering, and draw perspective-correct, distance-darkened floor/ceiling spans (plus `F_SKY1` sky hack and animated flats).

**Architecture:** `render/r_data` grows flat loading (lumps between `F_START`/`F_END`, 4096-byte → 64×64 RGBA, anim groups, `isSky`). New `render/r_plane` owns the faithful visplane machinery: `Visplane` structs, `R_FindPlane` (merge coplanar sectors by height+flat+light+sky), `R_CheckPlane` (claim/split columns), `R_PlaneSpans` (row-run span enumeration — replaces vanilla's byte-sentinel `R_MakeSpans`), `R_DrawSpan`/`R_MapPlane` math with **float** `yslope`/`distscale` tables. `render/r_bsp`'s `renderSeg` records ceiling/floor plane bands per column (vanilla band formulas adapted to this renderer's clip conventions) and `R_RenderView` calls `R_DrawPlanes` after the BSP pass. A shared `R_DistanceShade(depth)` is applied to flats (per-row) **and retroactively to walls** (per-column) for visual consistency.

**Faithfulness notes:** `R_FindPlane`/`R_CheckPlane`/`R_MapPlane`/`R_DrawSpan` follow `r_plane.c`/`r_segs.c`. **Render math stays FLOAT** (P2 simplification); `yslope`/`distscale` are float tables, not 16.16. **One intentional deviation:** vanilla `R_MakeSpans` writes sentinel `top[minx-1]`/`top[maxx+1]` and reads `top[x-1]` — out-of-bounds by modern standards; we replace the span-emission loop with `R_PlaneSpans` (row-run), which is equivalent and provably in-bounds. Visplane merge keys and projection math are unchanged.

**Tech Stack:** C++17, doctest, MSVC via CMake (unchanged).

## Global Constraints

- Framebuffer RGBA8888 = `(R<<24)|(G<<16)|(B<<8)|A`; opaque black = `0x000000FF` (NOT `0xFF000000`). Channel multiply: `uint8_t(c * shade)` (truncates; matches P3a `drawCol`).
- Flats: lumps between WAD markers `F_START`/`F_END`, each **4096 bytes = 64×64, row-major** (`byte[y*64 + x]` = palette index). Decode with the existing `TextureLookup` palette (full-bright RGBA, alpha `0xFF`).
- Freedoom1.wad flat layout (verified via `--listlumps`): `F_START`@2840 … `F_END`@3080. Animated groups present: `NUKAGE1/2/3`, `FWATER1/2/3/4`, `BLOOD1/3`, `LAVA1/2/3/4`, `SWATER1/2/3/4`. `F_SKY1` is a 4096-byte lump but is the **sky marker** (never sampled as a flat).
- Flat tiling: **1 map unit = 1 texel**, flats tile every 64 units → texel = `floor(worldX) & 63`, `floor(worldY) & 63`.
- Camera basis (unchanged from P2a/P3a): forward `F=(sin a, cos a)`, right `R=(cos a, -sin a)`. `depth = dx*sin+dy*cos`, `right = dx*cos-dy*sin`. `focal = w/2`, horizon at `h/2` (eye looks horizontally, no pitch). Eye height = 41 (fixed until P3c).
- Plane projection (derived, self-consistent with the camera basis): for a plane at world height `planeH`, screen row `y`, `depth = focal*|planeH-eyeZ| / |y - h/2|`. World point at column `x`: `worldX = px + depth*sin + (distscale[x]*depth)*cos`, `worldY = py + depth*cos - (distscale[x]*depth)*sin`, where `distscale[x] = (x - w/2)/focal`.
- Distance darkening: shared `R_DistanceShade(depth)` ∈ [kDistFloor, 1.0]; flats multiply per-row, walls per-column. Constants tuned via `--dumpframe` (Task 8).
- `ceilingClip[x]`/`floorClip[x]` semantics (current P3a code): init `-1` / `h`. After a solid wall they invert (column occluded). Plane bands are read from the **pre-`drawCol`** values.
- Tests are doctest; pure logic (r_data, r_plane) is unit-tested with crafted buffers; rendering is verified via `--dumpframe` → BMP → PowerShell `System.Drawing` → PNG → `Read`/`analyze_image`.
- One commit per task. Build/test commands:
  ```bash
  export http_proxy=http://127.0.0.1:7890 https_proxy=http://127.0.0.1:7890
  CMAKE="/c/Users/User/cmake-4.4.0-windows-x86_64/bin/cmake.exe"
  "$CMAKE" --build build --config Release
  "$CMAKE" --build build --config Release --target RUN_TESTS
  ```

## File Structure (additions)

```
src/render/r_data.h / .cpp     += Flat struct; TextureLookup::flat/flatForFrame/isSky; F_START/F_END load; anim groups
src/render/r_plane.h / .cpp    NEW — Visplane, PlaneCtx, R_FindPlane/R_CheckPlane/R_PlaneSpans/R_DrawSpan/R_DrawPlanes, yslope/distscale, R_DistanceShade (doomcore, SDL-free)
src/render/r_bsp.h / .cpp      += renderSeg marks visplanes; drawCol distance-shade; R_RenderView gains animTick + calls R_DrawPlanes
src/main.cpp                   += per-frame animTick; --dumpframe tick=0; title "P3b visplanes"
tests/test_r_data.cpp          += flat decode / F_START-F_END / isSky / flatForFrame cases
tests/test_r_plane.cpp         NEW — find/check merge, table values, projection coords, span enumeration, shade monotonic
CMakeLists.txt                 doomcore += src/render/r_plane.cpp; tests += test_r_plane.cpp
```

**Cross-task type contract** (defined in Task 3, consumed by Tasks 4–6 — names are fixed):

```cpp
// r_plane.h
struct Flat;  // defined in r_data.h (Task 1)
constexpr int kOpenTop = 0x7fffffff;   // sentinel: column unclaimed (top > bottom)
constexpr int kOpenBot = -1;
constexpr uint32_t kSkyColor = (0x40u<<24)|(0x30u<<16)|(0x28u<<8)|0xFFu;  // tuned in Task 8
struct Visplane { float height=0; const Flat* flat=nullptr; int lightlevel=0; bool sky=false;
                  int minx=0, maxx=-1; std::vector<int> top, bottom; };
struct PlaneCtx { int w=0,h=0; float focal=0,eyeZ=0,px=0,py=0,sin=0,cos=0; uint32_t* fb=nullptr;
                  std::vector<float> yslope, distscale; };
float R_DistanceShade(float depth);
int  R_FindPlane(std::vector<Visplane>& vps, float height, const Flat* flat, int light, bool sky, int w);
int  R_CheckPlane(std::vector<Visplane>& vps, int idx, int start, int stop);
void R_SetupPlaneTables(PlaneCtx& c);
std::vector<std::tuple<int,int,int>> R_PlaneSpans(const Visplane& pl);  // (y,x1,x2) in draw order
void R_DrawSpan(PlaneCtx& c, int y, int x1, int x2, const Flat* flat, float planeheight, int light, bool sky);
void R_DrawPlanes(PlaneCtx& c, std::vector<Visplane>& vps);
```

---

## Task 1: Flat decoding + TextureLookup::flat (F_START/F_END)

**Files:**
- Modify: `src/render/r_data.h`, `src/render/r_data.cpp`
- Modify: `tests/test_r_data.cpp`

**Interfaces:**
- Consumes: existing `TextureLookup` palette (`palette_`), `WadFile::numLumps/lumpName/lumpSize/readLump`.
- Produces: `struct Flat { char name[9]; int width=64,height=64; std::vector<uint32_t> rgba; };` and `const Flat* TextureLookup::flat(const std::string&) const` (case-insensitive, nullptr if absent). Flats stored in `flats_`/`flatIndex_`.

- [ ] **Step 1: Add failing tests to `tests/test_r_data.cpp`**

Append (reuse existing `w8`/`w16`/`byte` helpers; add a 64×64 flat builder):

```cpp
#include "../src/render/r_data.h"
// ... existing includes/helpers ...

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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: FAIL — `decodeFlat` undefined.

- [ ] **Step 3: Implement flat decode + TextureLookup::flat**

In `src/render/r_data.h`, add after the `Texture` struct:

```cpp
// A decoded flat: 64x64 full-bright RGBA (alpha 0xFF).
struct Flat { char name[9] = {0}; int width = 64, height = 64; std::vector<uint32_t> rgba; };

// Decode a 4096-byte (64x64, row-major) flat lump to RGBA using a 256-entry palette.
Flat decodeFlat(const byte* data, size_t n, const uint32_t* palette, const std::string& name);
```

Add to `class TextureLookup` (public): `const Flat* flat(const std::string& name) const;`
Add to `class TextureLookup` (private): `std::unordered_map<std::string,int> flatIndex_; std::vector<Flat> flats_;`

In `src/render/r_data.cpp`, add (after `compositeTexture`):

```cpp
Flat decodeFlat(const byte* data, size_t n, const uint32_t* palette, const std::string& name) {
    Flat f;
    std::snprintf(f.name, sizeof(f.name), "%s", name.c_str());
    f.width = 64; f.height = 64;
    f.rgba.assign(64 * 64, 0u);
    for (size_t i = 0; i < 4096 && i < n; ++i) {
        byte idx = data[i];
        f.rgba[i] = palette[idx] | 0xFFu;
    }
    return f;
}
```

In `TextureLookup::TextureLookup(...)` (ctor body, after the wall-texture loading / `wallIndex_` fill), add flat loading:

```cpp
    // Flats: lumps between F_START and F_END, size==4096, name != "F_SKY1".
    bool inFlats = false;
    for (int i = 0; i < const_cast<WadFile&>(wad).numLumps(); ++i) {
        std::string nm = const_cast<WadFile&>(wad).lumpName(i);
        if (nm == "F_START") { inFlats = true; continue; }
        if (nm == "F_END")   { inFlats = false; continue; }
        if (!inFlats) continue;
        if (const_cast<WadFile&>(wad).lumpSize(i) != 4096) continue;
        if (nm == "F_SKY1") continue;   // sky marker, not a flat
        auto raw = const_cast<WadFile&>(wad).readLump(i);
        if (raw.empty()) continue;
        flats_.push_back(decodeFlat(raw.data(), raw.size(), palette_.data(), nm));
    }
    for (int i = 0; i < static_cast<int>(flats_.size()); ++i)
        flatIndex_[upper(flats_[i].name)] = i;
```

(`upper` is the existing file-local helper in r_data.cpp.)

Add the accessor:

```cpp
const Flat* TextureLookup::flat(const std::string& name) const {
    auto it = flatIndex_.find(upper(name));
    return it == flatIndex_.end() ? nullptr : &flats_[it->second];
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/render/r_data.h src/render/r_data.cpp tests/test_r_data.cpp
git commit -m "feat(render): load 64x64 flats (F_START/F_END) into TextureLookup"
```

---

## Task 2: isSky + animated flats (flatForFrame)

**Files:**
- Modify: `src/render/r_data.h`, `src/render/r_data.cpp`
- Modify: `tests/test_r_data.cpp`

**Interfaces:**
- Consumes: `flat()` (Task 1).
- Produces: `static bool TextureLookup::isSky(const std::string&)`; `const Flat* TextureLookup::flatForFrame(const std::string& name, uint32_t tick) const`. Anim groups hardcoded; `flatForFrame` cycles known first-frame names by `tick`, else returns `flat(name)`.

- [ ] **Step 1: Add failing tests**

Append to `tests/test_r_data.cpp`:

```cpp
TEST_CASE("TextureLookup::isSky recognizes F_SKY1 only") {
    CHECK(TextureLookup::isSky("F_SKY1") == true);
    CHECK(TextureLookup::isSky("FLOOR0_1") == false);
    CHECK(TextureLookup::isSky("NUKAGE1") == false);
}
```

For `flatForFrame`, unit-test the resolver logic without a full WAD by exposing a tiny helper. Add to `r_data.h` (free function, testable):

```cpp
// Resolve an (possibly animated) flat name to its frame name at `tick`.
// Known anim groups cycle; everything else returns the input unchanged.
std::string resolveFlatFrame(const std::string& name, uint32_t tick);
```

Append to test:

```cpp
TEST_CASE("resolveFlatFrame cycles anim groups, leaves others") {
    CHECK(resolveFlatFrame("NUKAGE1", 0) == "NUKAGE1");
    CHECK(resolveFlatFrame("NUKAGE1", 1) == "NUKAGE2");
    CHECK(resolveFlatFrame("NUKAGE1", 2) == "NUKAGE3");
    CHECK(resolveFlatFrame("NUKAGE1", 3) == "NUKAGE1");   // wraps
    CHECK(resolveFlatFrame("FWATER1", 5) == "FWATER2");   // 5 % 4 == 1
    CHECK(resolveFlatFrame("FLOOR0_1", 9) == "FLOOR0_1"); // not animated
    CHECK(resolveFlatFrame("F_SKY1", 3) == "F_SKY1");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: FAIL — `isSky`/`resolveFlatFrame` undefined.

- [ ] **Step 3: Implement**

In `src/render/r_data.cpp`, add the anim-group table + resolver (file scope, above `TextureLookup` ctor):

```cpp
#include <array>
// DOOM animated flat groups (base name -> ordered frames). Extend as needed.
struct AnimGroup { const char* first; std::vector<std::string> frames; };
static const std::vector<AnimGroup>& animGroups() {
    static const std::vector<AnimGroup> g = {
        {"NUKAGE1", {"NUKAGE1","NUKAGE2","NUKAGE3"}},
        {"FWATER1", {"FWATER1","FWATER2","FWATER3","FWATER4"}},
        {"BLOOD1",  {"BLOOD1","BLOOD2","BLOOD3"}},
        {"LAVA1",   {"LAVA1","LAVA2","LAVA3","LAVA4"}},
        {"SWATER1", {"SWATER1","SWATER2","SWATER3","SWATER4"}},
    };
    return g;
}
std::string resolveFlatFrame(const std::string& name, uint32_t tick) {
    for (const auto& g : animGroups()) {
        for (size_t i = 0; i < g.frames.size(); ++i) {
            if (g.frames[i] == name) {
                return g.frames[(i + tick) % g.frames.size()];
            }
        }
    }
    return name;
}
```

Add to `r_data.h` (`class TextureLookup`, public):

```cpp
    static bool isSky(const std::string& name);
    const Flat* flatForFrame(const std::string& name, uint32_t tick) const;
```

In `r_data.cpp`:

```cpp
bool TextureLookup::isSky(const std::string& name) {
    return upper(name) == "F_SKY1";
}
const Flat* TextureLookup::flatForFrame(const std::string& name, uint32_t tick) const {
    return flat(resolveFlatFrame(name, tick));
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/render/r_data.h src/render/r_data.cpp tests/test_r_data.cpp
git commit -m "feat(render): F_SKY1 sky marker + animated flat frame cycling"
```

---

## Task 3: r_plane core — Visplane, R_FindPlane, R_CheckPlane

**Files:**
- Create: `src/render/r_plane.h`, `src/render/r_plane.cpp`
- Create: `tests/test_r_plane.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Flat` (r_data.h), `<vector>`.
- Produces: `Visplane`, `kOpenTop/kOpenBot`, `R_FindPlane`, `R_CheckPlane` (signatures in the cross-task contract).

- [ ] **Step 1: Register sources in CMake**

In `CMakeLists.txt`, add `src/render/r_plane.cpp` to the `doomcore` `add_library` source list. In `tests/CMakeLists.txt`, add `test_r_plane.cpp` to the `doomcpp_tests` source list.

- [ ] **Step 2: Write failing tests**

Create `tests/test_r_plane.cpp`:

```cpp
#include "doctest.h"
#include "render/r_plane.h"
#include "render/r_data.h"   // Flat
#include <vector>

using namespace std::string_literals;

TEST_CASE("R_FindPlane merges coplanar, creates distinct") {
    int w = 16;
    std::vector<Visplane> vps;
    Flat fA{};   // default-constructed; merge key is the Flat* address, not the name
    Flat fB{};
    int a = R_FindPlane(vps, 0.0f, &fA, 160, false, w);
    int a2 = R_FindPlane(vps, 0.0f, &fA, 160, false, w);  // same key -> merge
    CHECK(a == a2);
    CHECK(vps.size() == 1);

    int b = R_FindPlane(vps, 8.0f, &fA, 160, false, w);   // different height
    CHECK(b != a);
    CHECK(vps.size() == 2);

    int c = R_FindPlane(vps, 0.0f, &fB, 160, false, w);   // different flat
    CHECK(c != a);
    CHECK(vps.size() == 3);

    int s1 = R_FindPlane(vps, 0.0f, &fA, 160, true, w);   // sky: merges all sky
    int s2 = R_FindPlane(vps, 99.0f, &fB, 200, true, w);
    CHECK(s1 == s2);
}

TEST_CASE("R_FindPlane new plane is empty (minx>maxx, columns unclaimed)") {
    std::vector<Visplane> vps;
    Flat f{}; 
    int i = R_FindPlane(vps, 0.0f, &f, 160, false, 8);
    CHECK(vps[i].minx == 8);      // SCREENWIDTH sentinel (empty)
    CHECK(vps[i].maxx == -1);
    REQUIRE((int)vps[i].top.size() == 8);
    for (int x = 0; x < 8; ++x) {
        CHECK(vps[i].top[x] == kOpenTop);    // unclaimed
        CHECK(vps[i].bottom[x] == kOpenBot);
    }
}

TEST_CASE("R_CheckPlane extends range on disjoint columns, splits on collision") {
    int w = 32;
    std::vector<Visplane> vps;
    Flat f{};
    int p = R_FindPlane(vps, 0.0f, &f, 160, false, w);
    // claim column 5 with a band
    vps[p].minx = 5; vps[p].maxx = 5; vps[p].top[5] = 10; vps[p].bottom[5] = 20;
    // disjoint range [10..12] -> same plane, range unions
    int q = R_CheckPlane(vps, p, 10, 12);
    CHECK(q == p);
    CHECK(vps[p].minx == 5);
    CHECK(vps[p].maxx == 12);
    // overlapping-but-unclaimed columns [4..6]: col 5 claimed -> collision -> new plane
    int r = R_CheckPlane(vps, p, 4, 6);
    CHECK(r != p);
    CHECK(vps[r].minx == 4);
    CHECK(vps[r].maxx == 6);
    CHECK(vps.size() == 2);
}
```

- [ ] **Step 3: Run tests to verify failure**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: FAIL — `r_plane.h` missing.

- [ ] **Step 4: Implement r_plane.h + find/check**

Create `src/render/r_plane.h` with the cross-task contract block (all declarations listed in File Structure). Create `src/render/r_plane.cpp`:

```cpp
#include "r_plane.h"
#include "r_data.h"   // Flat
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <vector>

float R_DistanceShade(float depth) {
    float s = 1.15f - depth * 0.0006f;
    if (s < 0.12f) s = 0.12f;     // kDistFloor
    return s;
}

int R_FindPlane(std::vector<Visplane>& vps, float height, const Flat* flat,
                int light, bool sky, int w) {
    if (sky) { height = 0; light = 0; }   // all sky merges into one plane
    for (size_t i = 0; i < vps.size(); ++i) {
        const Visplane& p = vps[i];
        if (p.height == height && p.flat == flat && p.lightlevel == light && p.sky == sky)
            return static_cast<int>(i);
    }
    Visplane p;
    p.height = height; p.flat = flat; p.lightlevel = light; p.sky = sky;
    p.minx = w; p.maxx = -1;            // empty (minx > maxx)
    p.top.assign(w, kOpenTop);
    p.bottom.assign(w, kOpenBot);
    vps.push_back(std::move(p));
    return static_cast<int>(vps.size() - 1);
}

int R_CheckPlane(std::vector<Visplane>& vps, int idx, int start, int stop) {
    Visplane& pl = vps[idx];
    int intrl, intrh, unionl, unionh;
    if (start < pl.minx) { intrl = pl.minx; unionl = start; }
    else                 { unionl = pl.minx; intrl = start; }
    if (stop > pl.maxx)  { intrh = pl.maxx; unionh = stop; }
    else                 { unionh = pl.maxx; intrh = stop; }
    int x = intrl;
    for (; x <= intrh; ++x)
        if (pl.top[x] != kOpenTop) break;   // claimed -> collision
    if (x > intrh) {
        pl.minx = unionl; pl.maxx = unionh; // no collision: extend
        return idx;
    }
    // collision: create a new plane (copy identity), claim [start,stop]
    Visplane np;
    np.height = pl.height; np.flat = pl.flat; np.lightlevel = pl.lightlevel; np.sky = pl.sky;
    np.minx = start; np.maxx = stop;
    np.top.assign(pl.top.size(), kOpenTop);
    np.bottom.assign(pl.bottom.size(), kOpenBot);
    vps.push_back(std::move(np));
    return static_cast<int>(vps.size() - 1);
}
```

(`R_SetupPlaneTables`, `R_PlaneSpans`, `R_DrawSpan`, `R_DrawPlanes` are stubs returning default/empty for now — implemented in Tasks 4–5; add empty definitions so the file links.)

```cpp
void R_SetupPlaneTables(PlaneCtx&) {}
std::vector<std::tuple<int,int,int>> R_PlaneSpans(const Visplane&) { return {}; }
void R_DrawSpan(PlaneCtx&, int, int, int, const Flat*, float, int, bool) {}
void R_DrawPlanes(PlaneCtx&, std::vector<Visplane>&) {}
```

- [ ] **Step 5: Run tests to verify pass**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/render/r_plane.h src/render/r_plane.cpp tests/test_r_plane.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(render): visplane core (R_FindPlane/R_CheckPlane) + r_plane module"
```

---

## Task 4: yslope/distscale tables + R_DistanceShade + R_DrawSpan

**Files:**
- Modify: `src/render/r_plane.cpp`, `tests/test_r_plane.cpp`

**Interfaces:**
- Consumes: `PlaneCtx`, `Flat`, `kSkyColor`, framebuffer layout (Global Constraints).
- Produces: `R_SetupPlaneTables(PlaneCtx&)` (fills `yslope[h]`, `distscale[w]`); `R_DrawSpan(...)` (perspective-correct flat span, sky branch, distance shade). `R_DistanceShade` already defined in Task 3.

- [ ] **Step 1: Add failing tests**

Append to `tests/test_r_plane.cpp`:

```cpp
#include <cmath>

TEST_CASE("R_SetupPlaneTables fills yslope/distscale per formula") {
    PlaneCtx c; c.w = 320; c.h = 200; c.focal = 160.0f;
    R_SetupPlaneTables(c);
    REQUIRE((int)c.yslope.size() == 200);
    REQUIRE((int)c.distscale.size() == 320);
    // distscale[center] == 0
    CHECK(c.distscale[160] == doctest::Approx(0.0f));
    // yslope[y] = focal / |y - h/2|
    CHECK(c.yslope[100] == doctest::Approx(160.0f / 50.0f));   // y=150 -> |150-100|=50
    CHECK(c.yslope[50]  == doctest::Approx(160.0f / 50.0f));   // y=50  -> |50-100|=50
    // horizon row (y==100) is huge but finite
    CHECK(std::isinf(c.yslope[100]) || c.yslope[100] > 1e6f);
}

TEST_CASE("R_DistanceShade is monotonic decreasing with a floor") {
    CHECK(R_DistanceShade(0.0f) <= 1.0f);
    CHECK(R_DistanceShade(100.0f) >= R_DistanceShade(500.0f));
    CHECK(R_DistanceShade(10000.0f) == doctest::Approx(0.12f));  // clamped to floor
}

TEST_CASE("R_DrawSpan writes flat texels with shading; sky writes constant") {
    PlaneCtx c; c.w = 8; c.h = 8; c.focal = 4.0f; c.eyeZ = 41.0f;
    c.px = 0; c.py = 0; c.sin = 0; c.cos = 1.0f;   // looking +Y
    std::vector<uint32_t> fb(8*8, 0); c.fb = fb.data();
    R_SetupPlaneTables(c);
    Flat f{}; f.width = 64; f.height = 64;
    f.rgba.assign(64*64, (0xFFu<<24)|(0xFFu<<16)|(0xFFu<<8)|0xFFu); // white
    // light=128 forces total shade < 1.0 (0.50) regardless of distance clamp,
    // so white must be shaded down.
    R_DrawSpan(c, 6, 0, 7, &f, /*planeheight=*/41.0f, /*light=*/128, /*sky=*/false);
    for (int x = 0; x < 8; ++x) {
        uint32_t px = fb[6*8 + x];
        CHECK((px >> 24 & 0xFF) < 0xFF);   // shaded down from full white
        CHECK((px & 0xFF) == 0xFF);        // alpha opaque
    }
    // sky span: constant kSkyColor
    R_DrawSpan(c, 1, 0, 7, nullptr, 0.0f, 0, /*sky=*/true);
    for (int x = 0; x < 8; ++x)
        CHECK(fb[1*8 + x] == kSkyColor);
}
```

- [ ] **Step 2: Run tests to verify failure**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: FAIL — stubs return nothing; `kSkyColor`/shading not applied.

- [ ] **Step 3: Implement R_SetupPlaneTables + R_DrawSpan (replace stubs)**

Replace the Task 3 stubs in `src/render/r_plane.cpp`:

```cpp
void R_SetupPlaneTables(PlaneCtx& c) {
    c.yslope.assign(c.h, 0.0f);
    c.distscale.assign(c.w, 0.0f);
    for (int y = 0; y < c.h; ++y) {
        float dy = static_cast<float>(y) - c.h / 2.0f;
        c.yslope[y] = (std::fabs(dy) < 1e-6f) ? INFINITY : c.focal / std::fabs(dy);
    }
    for (int x = 0; x < c.w; ++x)
        c.distscale[x] = (x - c.w / 2.0f) / c.focal;
}

void R_DrawSpan(PlaneCtx& c, int y, int x1, int x2, const Flat* flat,
                float planeheight, int light, bool sky) {
    if (y < 0 || y >= c.h) return;
    if (sky) {
        for (int x = x1; x <= x2; ++x)
            if (x >= 0 && x < c.w) c.fb[(size_t)y * c.w + x] = kSkyColor;
        return;
    }
    if (!flat || flat->rgba.empty() || flat->width <= 0) return;
    if (y == c.h / 2) return;                       // horizon: infinite distance
    float dist = planeheight * c.yslope[y];
    if (!std::isfinite(dist) || dist <= 0.0f) return;
    float shade = R_DistanceShade(dist) * (light / 255.0f);
    if (shade > 1.0f) shade = 1.0f;
    const float r1 = c.distscale[std::clamp(x1, 0, c.w - 1)] * dist;
    float wx = c.px + dist * c.sin + r1 * c.cos;
    float wy = c.py + dist * c.cos - r1 * c.sin;
    const float stepR = dist / c.focal;
    const float stepX = stepR * c.cos;
    const float stepY = -stepR * c.sin;
    for (int x = x1; x <= x2; ++x) {
        if (x >= 0 && x < c.w) {
            int tx = static_cast<int>(std::floor(wx)) & 63;
            int ty = static_cast<int>(std::floor(wy)) & 63;
            uint32_t t = flat->rgba[(size_t)ty * flat->width + tx];
            uint8_t rr = static_cast<uint8_t>((t >> 24 & 0xFF) * shade);
            uint8_t gg = static_cast<uint8_t>((t >> 16 & 0xFF) * shade);
            uint8_t bb = static_cast<uint8_t>((t >> 8  & 0xFF) * shade);
            c.fb[(size_t)y * c.w + x] =
                (static_cast<uint32_t>(rr) << 24) | (static_cast<uint32_t>(gg) << 16) |
                (static_cast<uint32_t>(bb) << 8) | 0xFFu;
        }
        wx += stepX; wy += stepY;
    }
}
```

- [ ] **Step 4: Run tests to verify pass**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/render/r_plane.cpp tests/test_r_plane.cpp
git commit -m "feat(render): plane projection (yslope/distscale) + R_DrawSpan + distance shade"
```

---

## Task 5: R_PlaneSpans (row-run) + R_DrawPlanes

**Files:**
- Modify: `src/render/r_plane.cpp`, `tests/test_r_plane.cpp`

**Interfaces:**
- Consumes: `Visplane` (with `top/bottom` per-column bands), `R_DrawSpan`.
- Produces: `R_PlaneSpans(const Visplane&) -> vector<(y,x1,x2)>`; `R_DrawPlanes(PlaneCtx&, vector<Visplane>&)`.

- [ ] **Step 1: Add failing tests**

Append to `tests/test_r_plane.cpp`:

```cpp
TEST_CASE("R_PlaneSpans emits contiguous (y,x1,x2) runs; skips gaps/unclaimed") {
    int w = 8;
    Visplane pl; pl.minx = 0; pl.maxx = 5;
    pl.top.assign(w, kOpenTop); pl.bottom.assign(w, kOpenBot);
    // columns 0..2 cover rows 1..2; col 3 unclaimed (gap); col 4..5 cover rows 1..2
    for (int x = 0; x <= 2; ++x) { pl.top[x] = 1; pl.bottom[x] = 2; }
    for (int x = 4; x <= 5; ++x) { pl.top[x] = 1; pl.bottom[x] = 2; }
    auto spans = R_PlaneSpans(pl);
    // Expect, per row y=1 and y=2: run [0,2] and run [4,5] => 4 spans
    REQUIRE(spans.size() == 4);
    CHECK(std::get<0>(spans[0]) == 1); CHECK(std::get<1>(spans[0]) == 0); CHECK(std::get<2>(spans[0]) == 2);
    CHECK(std::get<0>(spans[1]) == 1); CHECK(std::get<1>(spans[1]) == 4); CHECK(std::get<2>(spans[1]) == 5);
    CHECK(std::get<0>(spans[2]) == 2); CHECK(std::get<1>(spans[2]) == 0); CHECK(std::get<2>(spans[2]) == 2);
    CHECK(std::get<0>(spans[3]) == 2); CHECK(std::get<1>(spans[3]) == 4); CHECK(std::get<2>(spans[3]) == 5);
}

TEST_CASE("R_PlaneSpans: empty plane (minx>maxx) yields nothing") {
    Visplane pl; pl.minx = 5; pl.maxx = -1;
    pl.top.assign(8, kOpenTop); pl.bottom.assign(8, kOpenBot);
    CHECK(R_PlaneSpans(pl).empty());
}
```

- [ ] **Step 2: Run tests to verify failure**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: FAIL — stub returns `{}`.

- [ ] **Step 3: Implement R_PlaneSpans + R_DrawPlanes**

Replace the Task 3 stubs:

```cpp
std::vector<std::tuple<int,int,int>> R_PlaneSpans(const Visplane& pl) {
    std::vector<std::tuple<int,int,int>> out;
    if (pl.minx > pl.maxx) return out;
    int ymax = -1;
    for (int x = pl.minx; x <= pl.maxx; ++x)
        if (pl.top[x] <= pl.bottom[x]) ymax = std::max(ymax, pl.bottom[x]);
    for (int y = 0; y <= ymax; ++y) {
        int x = pl.minx;
        while (x <= pl.maxx) {
            bool covered = (pl.top[x] <= y) && (y <= pl.bottom[x]); // unclaimed col: kOpenTop<=y false
            if (covered) {
                int x1 = x;
                while (x + 1 <= pl.maxx && pl.top[x+1] <= y && y <= pl.bottom[x+1]) ++x;
                out.emplace_back(y, x1, x);
                ++x;
            } else {
                ++x;
            }
        }
    }
    return out;
}

void R_DrawPlanes(PlaneCtx& c, std::vector<Visplane>& vps) {
    for (Visplane& pl : vps) {
        if (pl.minx > pl.maxx) continue;
        float planeheight = std::fabs(pl.height - c.eyeZ);
        for (auto [y, x1, x2] : R_PlaneSpans(pl))
            R_DrawSpan(c, y, x1, x2, pl.flat, planeheight, pl.lightlevel, pl.sky);
    }
}
```

- [ ] **Step 4: Run tests to verify pass**

Run: `"$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/render/r_plane.cpp tests/test_r_plane.cpp
git commit -m "feat(render): R_PlaneSpans row-run span enumeration + R_DrawPlanes"
```

---

## Task 6: r_bsp integration — visplane marking + wall distance-shade + R_RenderView

**Files:**
- Modify: `src/render/r_bsp.h`, `src/render/r_bsp.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `R_FindPlane`/`R_CheckPlane`/`R_SetupPlaneTables`/`R_DrawPlanes` (Tasks 3–5); `TextureLookup::flatForFrame/isSky` (Task 2).
- Produces: `R_RenderView(...,uint32_t animTick)`; per-column ceiling/floor visplane marking in `renderSeg`; `drawCol` distance-shade.

- [ ] **Step 1: Extend R_RenderView signature + wire animTick in main.cpp**

In `src/render/r_bsp.h`:

```cpp
void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  const TextureLookup& tex, float px, float py, float ang,
                  float eyeZ, uint32_t animTick);
```

In `src/main.cpp`, update both call sites (window loop + `--dumpframe`) to pass `animTick`. Add `uint32_t tick = 0;` before the window loop, pass `tick++` each frame; for `--dumpframe` pass `0`. Update the banner line to `doomcpp 0.1.0  (P3b visplanes)  ...`.

- [ ] **Step 2: Extend Cam + drawCol distance-shade**

In `src/render/r_bsp.cpp`, extend the anonymous-namespace `Cam`:

```cpp
struct Cam {
    float px, py, sin, cos, eyeZ, focal;
    int w, h;
    uint32_t* fb;
    std::vector<int> ceilingClip;
    std::vector<int> floorClip;
    const TextureLookup* tex;
    PlaneCtx plane;                       // NEW
    std::vector<Visplane> vps;            // NEW
    uint32_t tick = 0;                    // NEW
};
```

Update `drawCol` to take the column depth and apply `R_DistanceShade`. Change signature to add `float colDepth`:

```cpp
void drawCol(Cam& c, int x, float scale, const Texture& T, float U, int rowoffset,
             int zTop, int zBot, int light, bool solid, bool upper, bool lower, float colDepth) {
    if (T.width <= 0 || T.height <= 0 || T.rgba.empty()) return;
    float syTop = c.h / 2.0f - c.focal * (zTop - c.eyeZ) * scale;
    float syBot = c.h / 2.0f - c.focal * (zBot - c.eyeZ) * scale;
    int yA = std::max(static_cast<int>(std::ceil(syTop)),  c.ceilingClip[x] + 1);
    int yB = std::min(static_cast<int>(std::floor(syBot)), c.floorClip[x] - 1);
    float shade = std::clamp((light / 255.0f) * R_DistanceShade(colDepth), 0.0f, 1.0f);  // CHANGED
    for (int y = yA; y <= yB; ++y) {
        float z = c.eyeZ + (c.h / 2.0f - y) / (c.focal * scale);
        float V = (z - zTop) + rowoffset;
        int vi = static_cast<int>(std::floor(V)); vi %= T.height; if (vi < 0) vi += T.height;
        int ui = static_cast<int>(std::floor(U));   ui %= T.width;  if (ui < 0) ui += T.width;
        uint32_t texel = T.rgba[static_cast<size_t>(vi) * T.width + ui];
        if (!(texel & 0xFFu)) continue;
        uint8_t r = static_cast<uint8_t>((texel >> 24 & 0xFF) * shade);
        uint8_t g = static_cast<uint8_t>((texel >> 16 & 0xFF) * shade);
        uint8_t b = static_cast<uint8_t>((texel >> 8  & 0xFF) * shade);
        c.fb[static_cast<size_t>(y) * c.w + x] =
            (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
            (static_cast<uint32_t>(b) << 8) | 0xFFu;
    }
    if (solid || upper) c.ceilingClip[x] = std::max(c.ceilingClip[x], std::min(static_cast<int>(std::floor(syBot)), c.h - 1));
    if (solid || lower) c.floorClip[x]   = std::min(c.floorClip[x],   std::max(static_cast<int>(std::ceil(syTop)),  0));
}
```

Update the four `drawCol(...)` call sites in `renderSeg` to pass the per-column `depth` (already computed in the loop as `float depth`) as the final argument.

- [ ] **Step 3: Add visplane marking in renderSeg**

In `renderSeg`, after resolving `sec`/`sd`/heights/textures and computing `twoSided`, and **before** the column loop, compute `markCeil`/`markFloor` and reserve the seg's plane ranges. `sec` is `m.sectors[sg.frontsector]`; back sector is `m.sectors[sg.backsector]` when `twoSided`. Add:

```cpp
    const char* ceilPicF = sec.ceilingpic;
    const char* floorPicF = sec.floorpic;
    bool skyCeilF = TextureLookup::isSky(ceilPicF);
    const char* ceilPicB = twoSided ? m.sectors[sg.backsector].ceilingpic : ceilPicF;
    const char* floorPicB = twoSided ? m.sectors[sg.backsector].floorpic : floorPicF;
    int lightB = twoSided ? m.sectors[sg.backsector].lightlevel : light;
    bool ceilPicDiff  = std::strcmp(ceilPicF, ceilPicB)  != 0;
    bool floorPicDiff = std::strcmp(floorPicF, floorPicB) != 0;

    bool markCeil, markFloor;
    if (!twoSided) {
        markCeil = markFloor = true;
    } else {
        markCeil  = (cH_f >  cH_b) || (cH_f == cH_b && (ceilPicDiff  || light != lightB));
        markFloor = (fH_f <  fH_b) || (fH_f == fH_b && (floorPicDiff || light != lightB));
        if (cH_b <= fH_f || fH_b >= cH_f) { markCeil = markFloor = true; }  // closed door
    }
    if (fH_f >= static_cast<int>(c.eyeZ)) markFloor = false;            // floor above view
    if (cH_f <= static_cast<int>(c.eyeZ) && !skyCeilF) markCeil = false; // ceiling below view

    int ceilPlane = -1, floorPlane = -1;
    if (markCeil) {
        const Flat* cf = skyCeilF ? nullptr : c.tex->flatForFrame(ceilPicF, c.tick);
        ceilPlane = R_FindPlane(c.vps, static_cast<float>(cH_f), cf, light, skyCeilF, c.w);
        ceilPlane = R_CheckPlane(c.vps, ceilPlane, x0, x1);
    }
    if (markFloor) {
        const Flat* ff = c.tex->flatForFrame(floorPicF, c.tick);
        floorPlane = R_FindPlane(c.vps, static_cast<float>(fH_f), ff, light, false, c.w);
        floorPlane = R_CheckPlane(c.vps, floorPlane, x0, x1);
    }
```

Then inside the per-column loop, **after** computing `scale`/`depth` and **before** the `drawCol` calls, read pre-`drawCol` clips and write the plane bands (vanilla formulas):

```cpp
        float yCeilF  = c.h / 2.0f - c.focal * (cH_f - c.eyeZ) * scale;
        float yFloorF = c.h / 2.0f - c.focal * (fH_f - c.eyeZ) * scale;
        int cc = c.ceilingClip[x], fc = c.floorClip[x];
        if (markCeil && ceilPlane >= 0) {
            int top = cc + 1;
            int bot = static_cast<int>(std::floor(yCeilF)) - 1;
            if (bot >= fc) bot = fc - 1;
            if (top <= bot) { c.vps[ceilPlane].top[x] = top; c.vps[ceilPlane].bottom[x] = bot; }
        }
        if (markFloor && floorPlane >= 0) {
            int top = static_cast<int>(std::floor(yFloorF)) + 1;
            int bot = fc - 1;
            if (top <= cc) top = cc + 1;
            if (top <= bot) { c.vps[floorPlane].top[x] = top; c.vps[floorPlane].bottom[x] = bot; }
        }
```

(Then the existing `drawCol(...)` calls run and update clips. `#include <cstring>` for `std::strcmp`; `#include "r_plane.h"` at top.)

- [ ] **Step 4: Wire R_RenderView to set up tables + draw planes**

Replace the `R_RenderView` body's setup so it initializes `Cam::plane` and calls `R_DrawPlanes` after `renderNode`:

```cpp
void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  const TextureLookup& tex, float px, float py, float ang,
                  float eyeZ, uint32_t animTick) {
    if (map.nodes.empty() || map.subsectors.empty()) return;
    Cam c;
    c.px = px; c.py = py; c.eyeZ = eyeZ;
    c.sin = std::sin(ang); c.cos = std::cos(ang);
    c.w = w; c.h = h; c.fb = fb; c.focal = w / 2.0f;
    c.tex = &tex; c.tick = animTick;
    c.ceilingClip.assign(w, -1);
    c.floorClip.assign(w, h);
    c.plane.w = w; c.plane.h = h; c.plane.focal = c.focal; c.plane.eyeZ = eyeZ;
    c.plane.px = px; c.plane.py = py; c.plane.sin = c.sin; c.plane.cos = c.cos;
    c.plane.fb = fb;
    R_SetupPlaneTables(c.plane);
    c.vps.clear();
    for (int i = 0; i < w * h; ++i) fb[i] = 0x000000FFu;
    renderNode(c, map, static_cast<uint32_t>(map.nodes.size() - 1));
    R_DrawPlanes(c.plane, c.vps);
}
```

- [ ] **Step 5: Build + run full test suite**

Run: `"$CMAKE" --build build --config Release && "$CMAKE" --build build --config Release --target RUN_TESTS`
Expected: build OK, all unit tests PASS.

- [ ] **Step 6: Visual checkpoint — dump a frame and verify**

```bash
build/Release/doomcpp.exe --dumpframe assets/freedoom1.wad build/p3b_check.bmp 90
powershell.exe -NoProfile -Command "Add-Type -AssemblyName System.Drawing; \$i=[System.Drawing.Bitmap]::new('build/p3b_check.bmp'); \$i.Save('build/p3b_check.png',[System.Drawing.Imaging.ImageFormat]::Png); \$i.Dispose()"
```

Then `Read`/`analyze_image` on `build/p3b_check.png`. **Acceptance criteria (must hold before commit):**
- Floors and ceilings show real flat textures (not black).
- No ceiling/floor flat bleeds **over** wall textures (upper/lower/mid walls remain intact). **If bleed is visible** at two-sided openings, the band formula is interacting with P3a's clip update; apply this localized fix — change the ceiling band `bot` clamp to also respect the topmost wall row drawn this column by capping `bot` at `static_cast<int>(std::ceil(std::min({proj(zTop of each drawn tier)}))) - 1`, re-dump, and confirm the bleed is gone.
- Sky sectors (`F_SKY1` ceiling) render as a solid `kSkyColor` region, not a flat.
- Floors darken with distance (far rows darker than near rows).

- [ ] **Step 7: Commit**

```bash
git add src/render/r_bsp.h src/render/r_bsp.cpp src/main.cpp
git commit -m "feat(render): visplane floor/ceiling + distance shade on flats&walls + sky"
```

---

## Task 7: Smoke + wiring sanity

**Files:** none (verification only).

- [ ] **Step 1: Full clean build + tests**

```bash
"$CMAKE" --build build --config Release
"$CMAKE" --build build --config Release --target RUN_TESTS
```
Expected: clean build, all tests PASS (count increases by the new r_data/r_plane cases).

- [ ] **Step 2: Interactive run sanity**

Launch `build/Release/doomcpp.exe`, WASD/arrows to move through E1M1, ESC to quit. Confirm floors/ceilings render and animate (nukage/water) as you move. (Manual; record any artifacts for Task 8 tuning.)

---

## Task 8: Visual tuning + ship

**Files:** possibly `src/render/r_plane.cpp` (constants only).

- [ ] **Step 1: Tune distance-shade + sky color**

Dump frames at several angles/positions. Adjust in `src/render/r_plane.cpp`:
- `R_DistanceShade`: `1.15f` (near brightness), `0.0006f` (falloff rate), `0.12f` (floor). Goal: floors visibly darken with distance but never pure black; near floor near full sector brightness.
- `kSkyColor`: pick a value that reads as sky against E1M1's outdoor areas (the E1 sky is warm/brown; a muted brown-gray works).

After each change: rebuild, re-dump `build/p3b_tune.bmp`→PNG, `analyze_image`. Stop when it reads as DOOM.

- [ ] **Step 2: Final verification**

```bash
"$CMAKE" --build build --config Release --target RUN_TESTS   # all green
build/Release/doomcpp.exe --dumpframe assets/freedoom1.wad build/p3b_final.bmp 90
# convert + analyze_image: floors/ceilings textured, distance-darkened, sky solid, no bleed
```

- [ ] **Step 3: Update project memory + merge + tag**

Update `doomcpp-project-overview` memory: mark **P3b Visplane ✅ DONE** (`v0.6-p3b-visplanes`, merged 2026-07-22), advance **P3c BLOCKMAP + eye-height NEXT**. Then:

```bash
git add docs   # any doc/memory updates
git commit -m "docs(p3b): milestone" || true
git checkout main
git merge --no-ff feat/p3b-visplanes -m "merge: P3b visplane floors/ceilings complete"
git tag v0.6-p3b-visplanes
```

---

## Self-Review (completed by plan author)

- **Spec coverage:** §2 (D1 float tables → T4; D2 find/check → T3, spans → T5; D3 shade flats+walls → T4/T6; D4 sky → T4/T6; D5 anim → T2; D6 flats in r_data → T1; D7 r_plane module → T3). §3 flats → T1/T2. §4 visplane model+build → T3/T6. §5 projection → T4. §6 shade/sky/anim → T4/T6. §7 integration → T6/T7. §8 tests → T1–T5. §9 files → File Structure. §10 release → T8. All covered.
- **Placeholder scan:** distance-shade/sky constants are concrete values with an explicit tune step (T8), not placeholders. Band-bleed fallback (T6 Step 6) gives concrete code. No TBD/TODO.
- **Type consistency:** `Visplane` fields, `kOpenTop/kOpenBot`, `PlaneCtx` fields, `R_*` signatures are identical across the contract (File Structure) and every task. `drawCol` final `float colDepth` arg is passed at all four call sites in T6. `flatForFrame(name, tick)` signature matches T2 def and T6 use.
