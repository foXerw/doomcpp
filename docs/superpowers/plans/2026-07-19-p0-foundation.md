# P0 · 地基 (Foundation) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the doomcpp project scaffold, test harness, and the core numeric/utility primitives (fixed-point math, deterministic RNG) plus a working SDL2 window with a framebuffer pipeline — so P1 (WAD loading) and P2 (rendering) have a solid foundation to build on.

**Architecture:** A static library `doomcore` (core types, fixed-point math, RNG, error/logging) and `doomplatform` (SDL2 window + framebuffer) are linked into both the `doomcpp` executable and the `doomcpp_tests` test binary. Code uses modern C++17 (RAII, `std::vector`, `std::optional`) but **numerical semantics and algorithm flow follow the original GPL engine exactly** — function names are kept in original PascalCase (`FixedMul`, `M_Random`, `I_Error`) so each function cross-references the id Software source trivially.

**Tech Stack:** C++17, CMake ≥3.16, SDL2 (via vcpkg on Windows), doctest (via CMake FetchContent).

## Global Constraints

- **Language:** C++17, `CMAKE_CXX_EXTENSIONS OFF`, no compiler-specific extensions.
- **Naming:** Keep original engine PascalCase names (`FixedMul`, `FixedDiv`, `M_Random`, `P_Random`, `M_ClearRandom`, `I_Printf`, `I_Error`) — this maximizes source cross-referencing leverage. File/module names use the lowercase originals (`m_fixed`, `m_random`, `i_system`, `i_video`).
- **Numerical fidelity:** 16.16 fixed-point preserved (`FRACUNIT = 1 << 16`). `FixedMul` uses 64-bit intermediate; `FixedDiv` saturates; `FixedDiv2` uses `double`. `m_random` uses the exact `rndtable` from the original source (do **not** regenerate).
- **License:** GPL-3.0 (this project is a derivative of the GPL'd DOOM source). A `LICENSE` file with the full GPL-3.0 text is required.
- **Platform:** Windows 11 primary; keep CMake portable. SDL2 installed via vcpkg.
- **Code comments:** English (matches referenced GPL source); prose/docs may be Chinese.
- **No placeholders:** every code step contains complete, runnable code.
- **Commits:** one logical unit per commit, Conventional Commits (`feat:`/`chore:`/`test:`/`docs:`).

---

## File Structure (locked for P0)

```
doomcpp/
├── CMakeLists.txt                  # build config (grows across tasks)
├── README.md
├── LICENSE                         # GPL-3.0 full text
├── .gitignore                      # (exists)
├── src/
│   ├── core/
│   │   ├── doomtype.h              # int32/uint32/byte/boolean/fixed_t/FRACUNIT/FRACBITS
│   │   ├── i_system.h / .cpp       # I_Printf, I_Error
│   │   ├── m_fixed.h / .cpp        # FixedMul, FixedDiv (+ internal FixedDiv2)
│   │   └── m_random.h / .cpp       # rndtable, M_Random, P_Random, M_ClearRandom
│   ├── platform/
│   │   └── i_video.h / .cpp        # SDL2 window + streaming-texture framebuffer
│   └── main.cpp                    # event loop, owns the framebuffer
└── tests/
    ├── CMakeLists.txt
    ├── test_main.cpp               # doctest entry (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
    ├── test_smoke.cpp
    ├── test_i_system.cpp
    ├── test_m_fixed.cpp
    └── test_m_random.cpp
```

**Responsibilities:**
- `doomcore` (static lib): pure logic — types, math, RNG, error/log. No SDL. Unit-testable.
- `doomplatform` (static lib): SDL2 windowing + framebuffer presentation only.
- `doomcpp` (exe): wires everything; owns the framebuffer; runs the loop.
- `doomcpp_tests` (exe): doctest binary linking `doomcore`.

---

## Task 0: Project scaffold (CMake + empty main)

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/main.cpp`
- Create: `README.md`
- Create: `LICENSE` (full GPL-3.0 text, fetched)

**Interfaces:**
- Produces: the `doomcpp` executable target and `src` include path for later tasks.

- [ ] **Step 1: Create `src/main.cpp`**

```cpp
#include <iostream>

int main() {
    std::cout << "doomcpp 0.1.0\n";
    return 0;
}
```

- [ ] **Step 2: Create `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16)
project(doomcpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(doomcpp src/main.cpp)
target_include_directories(doomcpp PRIVATE src)

if(MSVC)
    target_compile_options(doomcpp PRIVATE /W4)
else()
    target_compile_options(doomcpp PRIVATE -Wall -Wextra -Wpedantic)
endif()
```

- [ ] **Step 3: Create `README.md`**

```markdown
# doomcpp

A faithful, from-scratch reimplementation of the classic DOOM (1993) engine in
modern C++17, using SDL2 for windowing/input/audio and software rendering
matching the original renderer's algorithms.

**Status:** P0 — foundation (scaffold, fixed-point math, RNG, SDL2 window).

## Build (Windows)

Requires [vcpkg](https://github.com/microsoft/vcpkg) for SDL2.

```bash
# one-time vcpkg bootstrap
git clone https://github.com/microsoft/vcpkg.git C:/vcpkg
C:/vcpkg/bootstrap-vcpkg.bat
C:/vcpkg/vcpkg install sdl2

# configure + build
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

## Assets

Engine reads standard WAD files. For development/testing use
[Freedoom](https://freedoom.github.io/) (free, GPL-compatible IWAD).
Place `freedoom1.wad` in `assets/`. WADs are not shipped with this repo.

## License

GPL-3.0. This engine is a derivative work of the GPL'd DOOM source released by
id Software in 1997.
```

- [ ] **Step 4: Fetch the GPL-3.0 `LICENSE` file**

Run:
```bash
curl -L -o LICENSE https://www.gnu.org/licenses/gpl-3.0.txt
```
Verify the file starts with `GNU GENERAL PUBLIC LICENSE` and `Version 3`.

- [ ] **Step 5: Configure and build**

Run:
```bash
cmake -S . -B build
cmake --build build
```
Expected: build succeeds with no errors. Binary appears at `build/doomcpp` (or `build/Debug/doomcpp.exe` under MSVC).

- [ ] **Step 6: Run the binary**

Run:
```bash
./build/doomcpp
```
Expected output:
```
doomcpp 0.1.0
```

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt README.md LICENSE src/main.cpp
git commit -m "chore: scaffold doomcpp project (CMake, C++17, README, GPL-3.0)"
```

---

## Task 1: doctest test harness

**Files:**
- Modify: `CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `tests/test_main.cpp`
- Create: `tests/test_smoke.cpp`

**Interfaces:**
- Produces: `doomcpp_tests` target runnable via `ctest`; `doctest.h` available to all test files via the `doctest` target.

- [ ] **Step 1: Append test infrastructure to `CMakeLists.txt`**

Add the following to the end of `CMakeLists.txt` (after the `doomcpp` target):

```cmake
include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
)
FetchContent_MakeAvailable(doctest)

enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 2: Create `tests/CMakeLists.txt`**

```cmake
add_executable(doomcpp_tests
    test_main.cpp
    test_smoke.cpp
)
target_link_libraries(doomcpp_tests PRIVATE doctest)
target_include_directories(doomcpp_tests PRIVATE src)

include(doctest)
doctest_discover_tests(doomcpp_tests)
```

- [ ] **Step 3: Create `tests/test_main.cpp`**

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
```

- [ ] **Step 4: Create `tests/test_smoke.cpp`**

```cpp
#include "doctest.h"

TEST_CASE("smoke: doctest is wired up") {
    CHECK(1 + 1 == 2);
}
```

- [ ] **Step 5: Configure, build, run tests**

Run:
```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: `1 test passed` (the smoke test).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt tests/
git commit -m "test: add doctest harness via FetchContent"
```

---

## Task 2: Error/log system (`i_system`)

**Files:**
- Create: `src/core/i_system.h`
- Create: `src/core/i_system.cpp`
- Create: `tests/test_i_system.cpp`
- Modify: `CMakeLists.txt` (introduce `doomcore` static lib)
- Modify: `tests/CMakeLists.txt` (add `test_i_system.cpp`, link `doomcore`)
- Modify: `src/main.cpp` (top-level try/catch)

**Interfaces:**
- Consumes: nothing (foundational).
- Produces:
  - `void I_Printf(std::string_view msg)` — logs to stderr (`std::clog`).
  - `[[noreturn]] void I_Error(std::string_view msg)` — logs + throws `std::runtime_error`; caught in `main`.

- [ ] **Step 1: Write the failing test `tests/test_i_system.cpp`**

```cpp
#include "doctest.h"
#include "core/i_system.h"
#include <stdexcept>

TEST_CASE("I_Printf does not throw") {
    CHECK_NOTHROW(I_Printf("hello doomcpp"));
}

TEST_CASE("I_Error throws std::runtime_error carrying the message") {
    REQUIRE_THROWS_AS(I_Error("boom"), std::runtime_error);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R i_system
```
Expected: FAIL / does not build (`core/i_system.h` not found).

- [ ] **Step 3: Create `src/core/i_system.h`**

```cpp
#pragma once
#include <string_view>

// Engine-wide logging and fatal-error handling.
// Mirrors the original engine's i_system.c I_Printf / I_Error contract.

// Normal logging (stderr).
void I_Printf(std::string_view msg);

// Fatal error: logs the message then throws std::runtime_error to unwind
// back to main(), where it is caught and the program exits non-zero.
// [[noreturn]] from the caller's perspective once invoked.
[[noreturn]] void I_Error(std::string_view msg);
```

- [ ] **Step 4: Create `src/core/i_system.cpp`**

```cpp
#include "i_system.h"
#include <iostream>
#include <stdexcept>

void I_Printf(std::string_view msg) {
    std::clog << msg << '\n';
}

[[noreturn]] void I_Error(std::string_view msg) {
    std::cerr << "I_Error: " << msg << '\n';
    throw std::runtime_error(std::string(msg));
}
```

- [ ] **Step 5: Introduce the `doomcore` library in `CMakeLists.txt`**

Insert this **before** the `add_executable(doomcpp ...)` line:

```cmake
add_library(doomcore STATIC
    src/core/i_system.cpp
)
target_include_directories(doomcore PUBLIC src)
```

And make the executable depend on it by adding after the executable is created:

```cmake
target_link_libraries(doomcpp PRIVATE doomcore)
```

- [ ] **Step 6: Add the test to `tests/CMakeLists.txt`**

Change the `add_executable(doomcpp_tests ...)` source list to include `test_i_system.cpp`, and link `doomcore`:

```cmake
add_executable(doomcpp_tests
    test_main.cpp
    test_smoke.cpp
    test_i_system.cpp
)
target_link_libraries(doomcpp_tests PRIVATE doctest doomcore)
target_include_directories(doomcpp_tests PRIVATE src)

include(doctest)
doctest_discover_tests(doomcpp_tests)
```

- [ ] **Step 7: Wrap `main` in a top-level try/catch (`src/main.cpp`)**

```cpp
#include <iostream>
#include <string>
#include "core/i_system.h"

int main() {
    try {
        std::cout << "doomcpp 0.1.0\n";
        return 0;
    } catch (const std::exception& e) {
        I_Printf(std::string("Fatal: ") + e.what());
        return 1;
    }
}
```

- [ ] **Step 8: Run tests to verify they pass**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: all tests pass (smoke + both i_system cases).

- [ ] **Step 9: Commit**

```bash
git add CMakeLists.txt src/core/i_system.h src/core/i_system.cpp src/main.cpp tests/test_i_system.cpp tests/CMakeLists.txt
git commit -m "feat(core): add i_system (I_Printf/I_Error) and doomcore lib"
```

---

## Task 3: Core types + fixed-point math (`doomtype.h`, `m_fixed`)

**Files:**
- Create: `src/core/doomtype.h`
- Create: `src/core/m_fixed.h`
- Create: `src/core/m_fixed.cpp`
- Create: `tests/test_m_fixed.cpp`
- Modify: `CMakeLists.txt` (add `m_fixed.cpp` to `doomcore`)
- Modify: `tests/CMakeLists.txt` (add `test_m_fixed.cpp`)

**Interfaces:**
- Consumes: `I_Error` (from Task 2) for `FixedDiv2` overflow.
- Produces:
  - `fixed_t` (= `int32_t`), `FRACUNIT` (= `1<<16`), `FRACBITS` (= 16).
  - `fixed_t FixedMul(fixed_t a, fixed_t b)`.
  - `fixed_t FixedDiv(fixed_t a, fixed_t b)` (saturates on overflow; otherwise calls `FixedDiv2`).

- [ ] **Step 1: Write the failing test `tests/test_m_fixed.cpp`**

```cpp
#include "doctest.h"
#include "core/m_fixed.h"
#include <limits>

TEST_CASE("FixedMul multiplies fixed-point values") {
    CHECK(FixedMul(FRACUNIT, FRACUNIT) == FRACUNIT);                          // 1.0 * 1.0
    CHECK(FixedMul(static_cast<fixed_t>(2.5 * FRACUNIT), 2 * FRACUNIT) == 5 * FRACUNIT); // 2.5 * 2.0
    CHECK(FixedMul(FRACUNIT / 2, FRACUNIT / 2) == FRACUNIT / 4);              // 0.5 * 0.5
}

TEST_CASE("FixedDiv divides fixed-point values") {
    CHECK(FixedDiv(FRACUNIT, 2 * FRACUNIT) == FRACUNIT / 2);                  // 1.0 / 2.0
    CHECK(FixedDiv(6 * FRACUNIT, 3 * FRACUNIT) == 2 * FRACUNIT);              // 6.0 / 3.0
}

TEST_CASE("FixedDiv saturates instead of UB on overflow") {
    CHECK(FixedDiv(64 * FRACUNIT, 1) == std::numeric_limits<fixed_t>::max());
    CHECK(FixedDiv(-64 * FRACUNIT, 1) == std::numeric_limits<fixed_t>::min());
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R m_fixed
```
Expected: FAIL / does not build (`core/m_fixed.h` not found).

- [ ] **Step 3: Create `src/core/doomtype.h`**

```cpp
#pragma once
#include <cstdint>

// Engine-wide scalar types. fixed_t is 16.16 fixed-point.
using int32  = std::int32_t;
using uint32 = std::uint32_t;
using byte   = std::uint8_t;
using boolean = int;

using fixed_t = std::int32_t;

inline constexpr fixed_t FRACUNIT  = 1 << 16;
inline constexpr int     FRACBITS  = 16;
```

- [ ] **Step 4: Create `src/core/m_fixed.h`**

```cpp
#pragma once
#include "doomtype.h"

// Fixed-point arithmetic, faithful to the original engine's m_fixed.c.
fixed_t FixedMul(fixed_t a, fixed_t b);   // (a * b) >> FRACBITS, via 64-bit.
fixed_t FixedDiv(fixed_t a, fixed_t b);   // saturates on overflow, else FixedDiv2.
```

- [ ] **Step 5: Create `src/core/m_fixed.cpp`**

```cpp
#include "m_fixed.h"
#include "i_system.h"
#include <cstdlib>
#include <limits>

static fixed_t FixedDiv2(fixed_t a, fixed_t b);

fixed_t FixedMul(fixed_t a, fixed_t b) {
    return static_cast<fixed_t>(
        (static_cast<std::int64_t>(a) * static_cast<std::int64_t>(b)) >> FRACBITS);
}

fixed_t FixedDiv(fixed_t a, fixed_t b) {
    if ((std::abs(a) >> 14) >= std::abs(b)) {
        return ((a ^ b) < 0) ? std::numeric_limits<fixed_t>::min()
                             : std::numeric_limits<fixed_t>::max();
    }
    return FixedDiv2(a, b);
}

static fixed_t FixedDiv2(fixed_t a, fixed_t b) {
    double c = (static_cast<double>(a) / static_cast<double>(b)) * FRACUNIT;
    if (c >= 2147483648.0 || c < -2147483648.0) {
        I_Error("FixedDiv: divide by zero");
    }
    return static_cast<fixed_t>(c);
}
```

- [ ] **Step 6: Add `m_fixed.cpp` to `doomcore` in `CMakeLists.txt`**

Update the `doomcore` source list:

```cmake
add_library(doomcore STATIC
    src/core/i_system.cpp
    src/core/m_fixed.cpp
)
```

- [ ] **Step 7: Add the test to `tests/CMakeLists.txt`**

Add `test_m_fixed.cpp` to the `doomcpp_tests` source list.

- [ ] **Step 8: Run tests to verify they pass**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R m_fixed
```
Expected: all 3 `m_fixed` cases pass.

- [ ] **Step 9: Commit**

```bash
git add CMakeLists.txt src/core/doomtype.h src/core/m_fixed.h src/core/m_fixed.cpp tests/test_m_fixed.cpp tests/CMakeLists.txt
git commit -m "feat(core): add doomtype.h and faithful fixed-point math (m_fixed)"
```

---

## Task 4: Deterministic RNG (`m_random`)

**Files:**
- Create: `src/core/m_random.h`
- Create: `src/core/m_random.cpp`
- Create: `tests/test_m_random.cpp`
- Modify: `CMakeLists.txt` (add `m_random.cpp` to `doomcore`)
- Modify: `tests/CMakeLists.txt` (add `test_m_random.cpp`)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `int M_Random()` — advances `rndindex`, returns `rndtable[rndindex]` (0–255).
  - `int P_Random()` — advances `prndindex` independently, returns `rndtable[prndindex]`.
  - `void M_ClearRandom()` — resets both indices to 0.

- [ ] **Step 1: Write the failing test `tests/test_m_random.cpp`**

```cpp
#include "doctest.h"
#include "core/m_random.h"

TEST_CASE("M_Random returns values in [0,255]") {
    M_ClearRandom();
    int v = M_Random();
    CHECK(v >= 0);
    CHECK(v <= 255);
}

TEST_CASE("M_Random sequence is reproducible") {
    M_ClearRandom();
    int seq1[8];
    for (int i = 0; i < 8; ++i) seq1[i] = M_Random();

    M_ClearRandom();
    int seq2[8];
    for (int i = 0; i < 8; ++i) seq2[i] = M_Random();

    for (int i = 0; i < 8; ++i) CHECK(seq1[i] == seq2[i]);
}

TEST_CASE("M_ClearRandom resets the sequence to rndtable[1]") {
    for (int i = 0; i < 30; ++i) (void)M_Random();
    M_ClearRandom();
    CHECK(M_Random() == 8); // rndtable[1] == 8
}

TEST_CASE("P_Random advances an index independent of M_Random") {
    M_ClearRandom();
    for (int i = 0; i < 50; ++i) (void)M_Random();   // burn M_Random only
    CHECK(P_Random() == 8);                          // prndindex untouched -> rndtable[1]
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R m_random
```
Expected: FAIL / does not build (`core/m_random.h` not found).

- [ ] **Step 3: Create `src/core/m_random.h`**

```cpp
#pragma once

// Deterministic pseudo-random, faithful to the original engine's m_random.c.
// Two independent indices over a shared lookup table.
int  M_Random();        // misc random (advances rndindex)
int  P_Random();        // play-simulation random (advances prndindex)
void M_ClearRandom();   // reset both indices to 0
```

- [ ] **Step 4: Create `src/core/m_random.cpp`**

```cpp
#include "m_random.h"

// Verbatim rndtable from the original m_random.c. Do NOT regenerate.
static const unsigned char rndtable[256] = {
      0,   8, 109, 220, 222, 241, 149, 107,  75, 248, 254, 140,  16,  66,
     74,  21, 211,  47,  80, 242, 154,  27, 205, 128, 161,  89,  77,  36,
     95, 110,  85,  48, 212, 140, 211, 249,  22,  79, 200,  50,  28, 188,
     52, 140, 202, 120,  68, 145,  62,  70, 184, 190,  91, 197, 152, 224,
    149, 104,  25, 178, 252, 182, 202, 182, 141, 197,   4,  81, 181, 242,
    145,  42,  39, 227, 156, 198, 225, 193, 219,  93, 122, 175, 249,   0,
    175, 143,  70, 239,  46, 246, 163,  53, 163, 109, 168, 135,   2, 235,
     25,  92,  20, 145, 138,  77,  69, 166,  78, 176, 173, 212, 166, 113,
     94, 161,  41,  50, 239,  49, 111, 164,  70,  60,   2,  37, 171,  75,
    136, 156,  11,  56,  42, 146, 138, 229,  73, 146,  77,  61,  98, 196,
    135, 106,  63, 197, 195,  86,  96, 203, 113, 101, 170, 247, 181, 113,
     80, 250, 108,   7, 255, 237, 129, 226,  79, 107, 112, 166, 103, 241,
     24, 223, 239, 120, 198,  58,  60,  82, 128,   3, 184,  66, 143, 224,
    145, 224,  81, 206, 163,  45,  63,  90, 168, 114,  59,  33, 159,  95,
     28, 139, 123,  98, 125, 196,  15,  70, 194, 253,  54,  14, 109, 226,
     71,  17, 161,  93, 186,  87, 244, 138,  20,  52, 123, 251,  26,  36,
     17,  46,  52, 231, 232,  76,  31, 221,  84,  37, 216, 165, 212, 106,
    197, 242,  98,  43,  39, 175, 254, 145, 190,  84, 118, 222, 187, 136,
    120, 163, 236, 249
};

static int rndindex  = 0;
static int prndindex = 0;

int P_Random(void) {
    prndindex = (prndindex + 1) & 0xff;
    return rndtable[prndindex];
}

int M_Random(void) {
    rndindex = (rndindex + 1) & 0xff;
    return rndtable[rndindex];
}

void M_ClearRandom(void) {
    rndindex = prndindex = 0;
}
```

- [ ] **Step 5: Add `m_random.cpp` to `doomcore` in `CMakeLists.txt`**

```cmake
add_library(doomcore STATIC
    src/core/i_system.cpp
    src/core/m_fixed.cpp
    src/core/m_random.cpp
)
```

- [ ] **Step 6: Add the test to `tests/CMakeLists.txt`**

Add `test_m_random.cpp` to the `doomcpp_tests` source list.

- [ ] **Step 7: Run tests to verify they pass**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R m_random
```
Expected: all 4 `m_random` cases pass.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/core/m_random.h src/core/m_random.cpp tests/test_m_random.cpp tests/CMakeLists.txt
git commit -m "feat(core): add deterministic m_random (verbatim rndtable)"
```

---

## Task 5: SDL2 window + framebuffer pipeline (`i_video`)

This is the P0 visual milestone. It establishes the software-renderer's framebuffer path (streaming texture) that P2/P3 will write pixels into.

**Prerequisite (one-time vcpkg setup, if not already done):**

```bash
git clone https://github.com/microsoft/vcpkg.git C:/vcpkg
C:/vcpkg/bootstrap-vcpkg.bat
C:/vcpkg/vcpkg install sdl2
```

**Files:**
- Create: `src/platform/i_video.h`
- Create: `src/platform/i_video.cpp`
- Modify: `CMakeLists.txt` (add `doomplatform` lib, `find_package(SDL2)`, link into exe)
- Modify: `src/main.cpp` (event loop + framebuffer)

**Interfaces:**
- Consumes: `I_Printf` (logging), SDL2.
- Produces:
  - `namespace i_video { bool init(int w, int h, const std::string& title); }`
  - `namespace i_video { void present(const std::uint32_t* pixels); }` — copies a `w*h` RGBA8888 buffer to the screen.
  - `namespace i_video { bool pollEvents(); }` — returns `false` when the window should close (quit or ESC).
  - `namespace i_video { void shutdown(); }`

- [ ] **Step 1: Create `src/platform/i_video.h`**

```cpp
#pragma once
#include <cstdint>
#include <string>

namespace i_video {

// Initialize an SDL2 window + renderer + streaming texture of the given
// logical (framebuffer) resolution. Returns false (and logs) on failure.
bool init(int width, int height, const std::string& title);

// Copy a width*height RGBA8888 pixel buffer to the screen, scaled to the window.
void present(const std::uint32_t* pixels);

// Drain the event queue for one frame. Returns false on window-close or ESC.
bool pollEvents();

// Tear down SDL video resources.
void shutdown();

} // namespace i_video
```

- [ ] **Step 2: Create `src/platform/i_video.cpp`**

```cpp
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "i_video.h"
#include "core/i_system.h"
#include <cstring>
#include <string>

namespace {
SDL_Window*   g_window   = nullptr;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture*  g_texture  = nullptr;
int g_w = 0;
int g_h = 0;

constexpr Uint32 PIXEL_FORMAT = SDL_PIXELFORMAT_RGBA8888;
}

namespace i_video {

bool init(int width, int height, const std::string& title) {
    g_w = width;
    g_h = height;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        I_Printf(std::string("SDL_Init failed: ") + SDL_GetError());
        return false;
    }
    g_window = SDL_CreateWindow(title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width * 2, height * 2, SDL_WINDOW_SHOWN);
    if (!g_window) {
        I_Printf(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        return false;
    }
    g_renderer = SDL_CreateRenderer(g_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) {
        I_Printf(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
        return false;
    }
    g_texture = SDL_CreateTexture(g_renderer, PIXEL_FORMAT,
        SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!g_texture) {
        I_Printf(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
        return false;
    }
    return true;
}

void present(const std::uint32_t* pixels) {
    void* dst   = nullptr;
    int   pitch = 0;
    SDL_LockTexture(g_texture, nullptr, &dst, &pitch);
    std::memcpy(dst, pixels,
        static_cast<size_t>(g_w) * static_cast<size_t>(g_h) * sizeof(std::uint32_t));
    SDL_UnlockTexture(g_texture);

    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);
    SDL_RenderPresent(g_renderer);
}

bool pollEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) return false;
    }
    return true;
}

void shutdown() {
    if (g_texture)  SDL_DestroyTexture(g_texture);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window)   SDL_DestroyWindow(g_window);
    g_texture  = nullptr;
    g_renderer = nullptr;
    g_window   = nullptr;
    SDL_Quit();
}

} // namespace i_video
```

- [ ] **Step 3: Wire SDL2 + `doomplatform` into `CMakeLists.txt`**

Add `find_package` and the platform library. Insert **before** the `add_executable(doomcpp ...)` line:

```cmake
find_package(SDL2 CONFIG REQUIRED)

add_library(doomplatform STATIC
    src/platform/i_video.cpp
)
target_include_directories(doomplatform PUBLIC src)
target_link_libraries(doomplatform PUBLIC SDL2::SDL2 ${CMAKE_DL_LIBS})
```

And make the executable link it (replace the existing `target_link_libraries(doomcpp PRIVATE doomcore)`):

```cmake
target_link_libraries(doomcpp PRIVATE doomcore doomplatform)
```

- [ ] **Step 4: Replace `src/main.cpp` with the event loop + framebuffer**

```cpp
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include "core/i_system.h"
#include "platform/i_video.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    SDL_SetMainReady();

    try {
        std::cout << "doomcpp 0.1.0\n";

        constexpr int FB_W = 320;
        constexpr int FB_H = 200;
        if (!i_video::init(FB_W, FB_H, "doomcpp")) {
            return 1;
        }

        std::vector<std::uint32_t> framebuffer(
            static_cast<size_t>(FB_W) * FB_H, 0xFF000000u);

        bool running = true;
        while (running) {
            running = i_video::pollEvents(); // false on ESC / window-close

            // P0 placeholder: solid fill to prove the framebuffer pipeline works.
            // (Exact channel mapping is finalized in P2; a visible color is enough here.)
            std::fill(framebuffer.begin(), framebuffer.end(), 0xFFFF8040u);

            i_video::present(framebuffer.data());
        }

        i_video::shutdown();
        return 0;
    } catch (const std::exception& e) {
        I_Printf(std::string("Fatal: ") + e.what());
        return 1;
    }
}
```

- [ ] **Step 5: Reconfigure with the vcpkg toolchain and build**

Run:
```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```
Expected: build succeeds; links against SDL2.

- [ ] **Step 6: Run the executable and verify the milestone**

Run:
```bash
./build/doomcpp
```
Expected: a window titled `doomcpp` opens showing a **solid colored screen**, and closes when you press **ESC** or click the window's close button. (Exact hue may differ; what matters is a non-black screen that responds to ESC.)

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/platform/i_video.h src/platform/i_video.cpp src/main.cpp
git commit -m "feat(platform): add SDL2 window and streaming-texture framebuffer pipeline"
```

- [ ] **Step 8: Tag the P0 milestone**

```bash
git tag -a v0.1-p0-foundation -m "P0 foundation: scaffold, fixed-point math, RNG, SDL2 window"
```

---

## P0 Definition of Done

- [ ] `doomcpp` builds via CMake with the vcpkg toolchain.
- [ ] `ctest` runs and all unit tests pass (smoke, i_system, m_fixed, m_random).
- [ ] `./build/doomcpp` opens a window, shows a solid color, and closes on ESC.
- [ ] `FixedMul`/`FixedDiv` match original numerical behavior; `m_random` reproduces the original `rndtable` sequence.
- [ ] Commit history is clean (one logical unit per commit); `v0.1-p0-foundation` tag exists.
- [ ] `LICENSE` (GPL-3.0) and `README.md` present.

## Next: P1 · WAD 加载

After P0 is done and tagged, write a new plan (`docs/superpowers/plans/YYYY-MM-DD-p1-wad-loading.md`) covering: WAD header/lump-directory parser, lump cache, and a small CLI/diagnostic that opens `freedoom1.wad` and lists all lump names — the P1 milestone.
