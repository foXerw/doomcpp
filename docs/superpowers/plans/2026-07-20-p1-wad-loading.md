# P1 · WAD 加载 (WAD Loading) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Parse DOOM WAD files (header + lump directory) and provide lump lookup/data access, culminating in a `--listlumps` diagnostic that opens `freedoom1.wad` and prints every lump.

**Architecture:** A `WadFile` class (`src/wad/wadfile.{h,cpp}`) owns one open WAD, validates the IWAD/PWAD magic, reads the 16-byte directory entries, and offers original-style operations (`checkNumForName`/`getNumForName`/`readLump`) with faithful semantics (backward scan, case-insensitive, 8-char names). Added to the SDL-free `doomcore` lib so it's unit-testable. `main` gains a `--listlumps <path>` mode for the milestone.

**Tech Stack:** C++17, `std::ifstream` (binary), doctest, MSVC via CMake (unchanged from P0).

## Global Constraints

(inherited from the project design — every task implicitly includes these)
- C++17, `CMAKE_CXX_EXTENSIONS OFF`, original PascalCase where it aids source cross-referencing.
- **WAD byte order is little-endian**; read int32 fields with an explicit LE helper (not struct memcpy) for portability.
- **Preserve original WAD semantics:** names are 8 bytes uppercase NUL-padded; `checkNumForName` scans **backward** (later files/last duplicate wins) and is **case-insensitive**; unknown lumps via `getNumForName` call `I_Error`.
- Function-of-original naming: `checkNumForName`/`getNumForName`/`readLump` mirror `W_CheckNumForName`/`W_GetNumForName`/`W_ReadLump`.
- GPL-3.0; one logical commit per task; Conventional Commits.
- Build/test commands require the proxy (`http_proxy=http://127.0.0.1:7890`) and the vcpkg toolchain file (see `[[doomcpp-build-and-toolchain]]`).

## File Structure (P0 additions)

```
src/wad/wadfile.h        # WadFile class
src/wad/wadfile.cpp      # parser + lump access
tests/test_wadfile.cpp   # TDD with a crafted in-test WAD
assets/freedoom1.wad     # real IWAD for the milestone (gitignored, downloaded T0)
```

`wadfile.cpp` is added to the existing `doomcore` lib (SDL-free, unit-testable). `wad/` gets its own source dir now; a dedicated `doomwad` lib can be split out later if the module grows.

---

## Task 0: Acquire Freedoom IWAD

**Files:**
- Create: `assets/freedoom1.wad` (gitignored, downloaded — not committed)

- [ ] **Step 1: Download the Freedoom release zip (via proxy)**

Run:
```bash
export http_proxy=http://127.0.0.1:7890 https_proxy=http://127.0.0.1:7890
# resolve the latest release zip URL
URL=$(curl -s https://api.github.com/repos/freedoom/freedoom/releases/latest \
      | grep browser_download_url | grep '.zip"' | head -1 \
      | cut -d '"' -f 4)
echo "latest: $URL"
curl -fSL --retry 8 --retry-all-errors --retry-delay 3 -o /tmp/freedoom.zip "$URL"
```

- [ ] **Step 2: Extract `freedoom1.wad` into `assets/`**

Run:
```bash
mkdir -p D:/code/doom-fork/assets
powershell.exe -NoProfile -Command \
  "Expand-Archive -Path '/tmp/freedoom.zip' -DestinationPath '/tmp/freedoom' -Force"
cp /tmp/freedoom/*/freedoom1.wad D:/code/doom-fork/assets/
ls -la D:/code/doom-fork/assets/freedoom1.wad   # ~12-18 MB
```
Expected: `assets/freedoom1.wad` exists, multi-megabyte. (Gitignored via `*.wad`.)

---

## Task 1: WAD header + directory parser

**Files:**
- Create: `src/wad/wadfile.h`
- Create: `src/wad/wadfile.cpp`
- Create: `tests/test_wadfile.cpp`
- Modify: `CMakeLists.txt` (add `src/wad/wadfile.cpp` to `doomcore`)
- Modify: `tests/CMakeLists.txt` (add `test_wadfile.cpp`)

**Interfaces:**
- Consumes: `I_Error` (doomcore, P0).
- Produces:
  - `class WadFile` — `explicit WadFile(const std::string& path)`.
  - `bool isIWAD() const;`, `const std::string& magic() const;`.
  - `int numLumps() const;`, `std::string lumpName(int i) const;`, `int lumpSize(int i) const;`.
  - `int checkNumForName(const std::string&) const;` (−1 if absent), `int getNumForName(const std::string&) const;` (I_Error if absent).

- [ ] **Step 1: Write the failing test `tests/test_wadfile.cpp`**

```cpp
#include "doctest.h"
#include "wad/wadfile.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
void put_le32(std::vector<unsigned char>& v, std::int32_t x) {
    auto u = static_cast<std::uint32_t>(x);
    v.push_back(static_cast<unsigned char>(u));
    v.push_back(static_cast<unsigned char>(u >> 8));
    v.push_back(static_cast<unsigned char>(u >> 16));
    v.push_back(static_cast<unsigned char>(u >> 24));
}
void put_name(std::vector<unsigned char>& v, const std::string& n) {
    char buf[8] = {0};
    for (int i = 0; i < 8 && i < static_cast<int>(n.size()); ++i) buf[i] = n[i];
    v.insert(v.end(), buf, buf + 8);
}
// Minimal valid WAD: header(12) + "hello"(5) + "worlds!"(7) + dir(2*16).
std::string make_test_wad() {
    std::vector<unsigned char> w;
    w.insert(w.end(), {'I', 'W', 'A', 'D'});
    put_le32(w, 2);            // numlumps
    put_le32(w, 24);           // infotableofs = 12 + 5 + 7
    const std::string d0 = "hello", d1 = "worlds!";
    w.insert(w.end(), d0.begin(), d0.end());
    w.insert(w.end(), d1.begin(), d1.end());
    put_le32(w, 12); put_le32(w, 5); put_name(w, "LUMPONE");
    put_le32(w, 17); put_le32(w, 7); put_name(w, "LUMPTWO");
    std::string path = "test_wad_tmp.wad";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(w.data()), static_cast<std::streamsize>(w.size()));
    return path;
}
}

TEST_CASE("WadFile parses header and directory") {
    std::string p = make_test_wad();
    WadFile wad(p);
    CHECK(wad.isIWAD());
    CHECK(wad.numLumps() == 2);
    CHECK(wad.lumpName(0) == "LUMPONE");
    CHECK(wad.lumpName(1) == "LUMPTWO");
    CHECK(wad.lumpSize(0) == 5);
    CHECK(wad.lumpSize(1) == 7);
    std::remove(p.c_str());
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
CMAKE="/c/Users/User/cmake-4.4.0-windows-x86_64/bin/cmake.exe"
"$CMAKE" --build build --config Release
"$CMAKE" --build build --config Release --target RUN_TESTS -R wadfile
```
Expected: FAIL / does not build (`wad/wadfile.h` not found).

- [ ] **Step 3: Create `src/wad/wadfile.h`**

```cpp
#pragma once
#include <fstream>
#include <string>
#include <vector>
#include "core/doomtype.h"

// Faithful WAD reader (header + lump directory), mirroring w_wad.c.
class WadFile {
public:
    explicit WadFile(const std::string& path);  // opens + parses; I_Error on failure

    const std::string& magic() const { return magic_; }  // "IWAD" or "PWAD"
    bool isIWAD() const { return magic_ == "IWAD"; }

    int numLumps() const { return static_cast<int>(lumps_.size()); }
    std::string lumpName(int i) const;
    int lumpSize(int i) const;

    // -1 if not found. Scans backward (later lumps override), case-insensitive.
    int checkNumForName(const std::string& name) const;
    // Calls I_Error if not found.
    int getNumForName(const std::string& name) const;

private:
    struct LumpInfo {
        int  filepos;
        int  size;
        char name[9];   // 8 chars + NUL
    };

    std::ifstream         file_;
    std::string           magic_;
    std::vector<LumpInfo> lumps_;
};
```

- [ ] **Step 4: Create `src/wad/wadfile.cpp`**

```cpp
#include "wadfile.h"
#include "core/i_system.h"
#include <cstring>

namespace {
std::int32_t read_le_i32(const unsigned char* p) {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24));
}
void normalize_name(const std::string& in, char out[8]) {
    std::memset(out, 0, 8);
    for (int i = 0; i < 8 && i < static_cast<int>(in.size()); ++i) {
        char c = in[i];
        out[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
    }
}
}

WadFile::WadFile(const std::string& path) {
    file_.open(path, std::ios::binary);
    if (!file_) {
        I_Error(std::string("WadFile: cannot open ") + path);
    }
    unsigned char hdr[12];
    file_.read(reinterpret_cast<char*>(hdr), 12);
    if (!file_) {
        I_Error("WadFile: truncated header");
    }
    magic_.assign(reinterpret_cast<const char*>(hdr), 4);
    if (magic_ != "IWAD" && magic_ != "PWAD") {
        I_Error(std::string("WadFile: not IWAD/PWAD: ") + magic_);
    }
    int numlumps     = read_le_i32(hdr + 4);
    int infotableofs = read_le_i32(hdr + 8);

    std::vector<unsigned char> dir(static_cast<size_t>(numlumps) * 16);
    file_.seekg(infotableofs, std::ios::beg);
    file_.read(reinterpret_cast<char*>(dir.data()),
               static_cast<std::streamsize>(dir.size()));
    if (!file_) {
        I_Error("WadFile: truncated directory");
    }
    lumps_.reserve(static_cast<size_t>(numlumps));
    for (int i = 0; i < numlumps; ++i) {
        const unsigned char* e = dir.data() + static_cast<size_t>(i) * 16;
        LumpInfo li;
        li.filepos = read_le_i32(e);
        li.size    = read_le_i32(e + 4);
        std::memcpy(li.name, e + 8, 8);
        li.name[8] = '\0';
        lumps_.push_back(li);
    }
}

std::string WadFile::lumpName(int i) const {
    if (i < 0 || i >= numLumps()) I_Error("WadFile::lumpName: out of range");
    return std::string(lumps_[i].name);
}

int WadFile::lumpSize(int i) const {
    if (i < 0 || i >= numLumps()) I_Error("WadFile::lumpSize: out of range");
    return lumps_[i].size;
}

int WadFile::checkNumForName(const std::string& name) const {
    char want[8];
    normalize_name(name, want);
    for (int i = numLumps() - 1; i >= 0; --i) {   // backward scan (last wins)
        if (std::memcmp(lumps_[i].name, want, 8) == 0) return i;
    }
    return -1;
}

int WadFile::getNumForName(const std::string& name) const {
    int i = checkNumForName(name);
    if (i < 0) I_Error(std::string("WadFile::getNumForName: not found: ") + name);
    return i;
}
```

- [ ] **Step 5: Add to `doomcore` in `CMakeLists.txt`**

```cmake
add_library(doomcore STATIC
    src/core/i_system.cpp
    src/core/m_fixed.cpp
    src/core/m_random.cpp
    src/wad/wadfile.cpp
)
```

- [ ] **Step 6: Add `test_wadfile.cpp` to `tests/CMakeLists.txt`** source list.

- [ ] **Step 7: Build + run, expect pass**

Run:
```bash
"$CMAKE" --build build --config Release
"$CMAKE" --build build --config Release --target RUN_TESTS -R wadfile
```
Expected: `WadFile parses header and directory ... Passed`.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/wad/wadfile.h src/wad/wadfile.cpp tests/test_wadfile.cpp tests/CMakeLists.txt
git commit -m "feat(wad): parse WAD header and lump directory"
```

---

## Task 2: Lump lookup + data access

**Files:**
- Modify: `src/wad/wadfile.h` (add `readLump`, `readLumpByName`, `cache_`)
- Modify: `src/wad/wadfile.cpp` (implement + init cache in ctor)
- Modify: `tests/test_wadfile.cpp` (add cases)

**Interfaces:**
- Produces:
  - `std::vector<byte> readLump(int i);` — returns lump bytes (cached after first read).
  - `std::vector<byte> readLumpByName(const std::string& name);`

- [ ] **Step 1: Append failing cases to `tests/test_wadfile.cpp`**

```cpp
TEST_CASE("WadFile name lookup is case-insensitive and backward") {
    std::string p = make_test_wad();
    WadFile wad(p);
    CHECK(wad.checkNumForName("lumpone") == 0);   // lowercase query
    CHECK(wad.checkNumForName("LUMPTWO") == 1);
    CHECK(wad.checkNumForName("MISSING") == -1);
    std::remove(p.c_str());
}

TEST_CASE("WadFile reads lump bytes") {
    std::string p = make_test_wad();
    WadFile wad(p);
    auto b0 = wad.readLump(0);
    CHECK(std::string(b0.begin(), b0.end()) == "hello");
    auto b1 = wad.readLumpByName("LUMPTWO");
    CHECK(std::string(b1.begin(), b1.end()) == "worlds!");
    std::remove(p.c_str());
}

TEST_CASE("WadFile::getNumForName throws for missing lump") {
    std::string p = make_test_wad();
    WadFile wad(p);
    CHECK_THROWS_AS(wad.getNumForName("NOPE"), std::runtime_error);
    std::remove(p.c_str());
}
```

- [ ] **Step 2: Run to verify the new cases fail**

Run: `"$CMAKE" --build build --config Release && "$CMAKE" --build build --config Release --target RUN_TESTS -R wadfile`
Expected: FAIL (no `readLump`).

- [ ] **Step 3: Extend the class in `src/wad/wadfile.h`**

Add to the public section:
```cpp
    std::vector<byte> readLump(int i);
    std::vector<byte> readLumpByName(const std::string& name);
```
Add to the private section (after `lumps_`):
```cpp
    std::vector<std::vector<byte>> cache_;   // index -> bytes, filled lazily
```

- [ ] **Step 4: Implement in `src/wad/wadfile.cpp`**

At the end of the constructor (after the directory loop), size the cache:
```cpp
    cache_.assign(static_cast<size_t>(numlumps), {});
```
Append the two methods:
```cpp
std::vector<byte> WadFile::readLump(int i) {
    if (i < 0 || i >= numLumps()) I_Error("WadFile::readLump: out of range");
    if (!cache_[i].empty()) return cache_[i];
    const LumpInfo& li = lumps_[i];
    std::vector<byte> buf(static_cast<size_t>(li.size));
    if (li.size > 0) {
        file_.seekg(li.filepos, std::ios::beg);
        file_.read(reinterpret_cast<char*>(buf.data()), li.size);
        if (!file_) I_Error(std::string("WadFile::readLump: short read on ") + li.name);
    }
    cache_[i] = buf;
    return buf;
}

std::vector<byte> WadFile::readLumpByName(const std::string& name) {
    return readLump(getNumForName(name));
}
```

- [ ] **Step 5: Build + run, expect all wadfile cases pass**

Run:
```bash
"$CMAKE" --build build --config Release
"$CMAKE" --build build --config Release --target RUN_TESTS -R wadfile
```
Expected: all 4 `wadfile` cases pass.

- [ ] **Step 6: Commit**

```bash
git add src/wad/wadfile.h src/wad/wadfile.cpp tests/test_wadfile.cpp
git commit -m "feat(wad): lump name lookup and cached data access"
```

---

## Task 3: `--listlumps` diagnostic (the milestone)

**Files:**
- Modify: `src/main.cpp` (add `--listlumps <path>` mode before the window code)

**Interfaces:**
- Produces: a CLI mode `doomcpp --listlumps <wad>` that prints `N lumps (<IWAD|PWAD>)` then `index: NAME (size bytes)` per lump, then exits 0.

- [ ] **Step 1: Add the mode to `src/main.cpp`**

Add includes near the top:
```cpp
#include "wad/wadfile.h"
```
Insert at the start of the `try` block in `main` (before the window code):
```cpp
        // P1 diagnostic: dump a WAD's lump directory and exit.
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--listlumps" && i + 1 < argc) {
                WadFile wad(argv[i + 1]);
                std::cout << wad.numLumps() << " lumps in " << argv[i + 1]
                          << " (" << wad.magic() << ")\n";
                for (int l = 0; l < wad.numLumps(); ++l) {
                    std::cout << "  " << l << ": " << wad.lumpName(l)
                              << " (" << wad.lumpSize(l) << " bytes)\n";
                }
                return 0;
            }
        }
```

- [ ] **Step 2: Build**

Run:
```bash
"$CMAKE" --build build --config Release
```
Expected: clean build.

- [ ] **Step 3: Run the milestone on real Freedoom**

Run:
```bash
./build/Release/doomcpp.exe --listlumps assets/freedoom1.wad | head -15
echo "..."
./build/Release/doomcpp.exe --listlumps assets/freedoom1.wad | wc -l
```
Expected: prints `N lumps in assets/freedoom1.wad (IWAD)` then a long list including well-known lumps like `PLAYPAL`, `COLORMAP`, `E1M1`, `DEHACKED`, `DEMO1`. Total line count ≈ numlumps + 1.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat(wad): --listlumps diagnostic to dump a WAD's lump directory"
```

---

## P1 Definition of Done

- [ ] `WadFile` parses any IWAD/PWAD: header magic validated, directory read, lumps counted/named/sized.
- [ ] Name lookup is case-insensitive + backward-scanning; `getNumForName` errors on miss.
- [ ] `readLump` returns correct bytes (cached).
- [ ] All unit tests pass (P0's 10 + P1's 4 = 14).
- [ ] `./build/doomcpp.exe --listlumps assets/freedoom1.wad` lists every lump (milestone).
- [ ] Commits clean; P1 merged to `main` and tagged `v0.2-p1-wad-loading`.

## Next: P2 · 地图 + 首次 3D

After P1 ships, write the P2 plan: parse map lumps (VERTEXES/LINEDEFS/SIDEDEFS/SECTORS/THINGS/NODES/SEGS/SSECTORS/BLOCKMAP), render a 2D automap, then BSP + column-based wall rendering (uncolored) — the "see E1M1 in pseudo-3D" milestone.
