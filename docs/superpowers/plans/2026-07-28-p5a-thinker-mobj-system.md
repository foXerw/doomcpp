# P5a thinker/mobj 系统实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 P4 的渲染侧静态 `mobj_t` 升级为 vanilla 活 mobj + thinker 系统（info 表 / World / `P_MobjThinker` 物理 / 泛化碰撞 / `P_SetupLevel`），玩家并入 mobj，并实现「按键抛桶」可见里程碑。

**Architecture:** game logic（`p_*`）走 16.16 定点，渲染器（`r_*`）走 float（P3.5 永久偏差）。`mobj_t` 从 `render/r_things.h` 迁到 `play/p_mobj.h`；`World`（`p_tick`）持有 `std::list<mobj_t>` + `player_t{mobj_t* mo}` + map 引用；碰撞从 Player 专属泛化为按 `mobjinfo` 的 radius/height/flags；玩家移动保留直接位移（不走动量）以最小化回归。每帧渲染前按 mobj 当前 subsector 重建 `thingsBySubsec`。

**Tech Stack:** C++17 / CMake + vcpkg(SDL2) / doctest / vanilla `p_mobj.c`·`info.c`·`p_map.c` 作算法参考（复刻非翻译）。

## Global Constraints

- **定点**：game logic 全部 16.16（`fixed_t`/`FRACBITS`/`FixedMul`/`finecos`/`finesine`）；渲染器数学保持 float（不得把渲染改定点）。
- **每任务结尾**：`ctest --test-dir build --build-config Release --output-on-failure` 全绿，且 `cmake --build build --config Release` 成功。每个任务一次或多次提交（Conventional Commits）。
- **渲染零回归基线**：Task 2/4/5 之后，`--dumpframe` E1M1 固定相机产出的图景与 P4 一致（精灵数量/位置/朝向/明暗）。Task 5 额外要 WASD 实机可动（memory 记录的回归盲点）。
- **info 表必须复现 P4 的 doomednum→(sprname,frame,fullbright) 映射**（见 Task 1 表），否则 Task 2 起渲染会变。
- **不逐行翻译 vanilla**、不装逐像素 oracle；物理常量在 Task 6 标注来源。
- 分支 `feat/p5a-thinker-mobj`（已创建）。spec `docs/superpowers/specs/2026-07-28-p5a-thinker-mobj-system-design.md`。
- 构建/测试命令：`cmake --build build --config Release` 与 `ctest --test-dir build --build-config Release --output-on-failure`（`build/` 已配置）。

---

## 文件结构

| 文件 | 责任 |
|---|---|
| `play/info.{h,cpp}` **新** | 枚举（`spritenum_t`/`statenum_t`/`mobjtype_t`）、`state_t`/`mobjinfo_t`、`states[]`/`mobjinfo[]`/`sprnames[]`、`mobjtypeForDoomednum()`、MF_/FF_ 常量。 |
| `play/p_mobj.h` **新** | 活 `mobj_t`（从 `r_things.h` 迁入并扩展）。 |
| `play/p_mobj.cpp` **新** | `P_SpawnMobj`/`P_RemoveMobj`/`P_SetMobjState`/`P_SetThingPosition`/`P_MobjThinker`（物理）。 |
| `play/p_tick.{h,cpp}` **新** | `World`、`player_t`、`P_RunThinkers`、`P_BuildThingsBySubsec`、`P_SetupLevel`。 |
| `play/p_map.{h,cpp}` 改 | 泛化碰撞到 `mobj_t`；`P_MovePlayer`/`P_CalcHeight` 走 `player_t`；删 `struct Player`。 |
| `render/r_things.{h,cpp}` 改 | 删 `mobj_t`/`SpriteDef`/`spriteDefForType`；include `p_mobj.h`；`R_ProjectSprite` 取 `const mobj_t&`。 |
| `render/r_bsp.{h,cpp}` 改 | `R_RenderView` 去 `things` 参、`thingsBySubsec` 改 `const mobj_t*`；`renderSubsector` 指针遍历、跳过 player.mo。 |
| `platform/i_video.{h,cpp}` 改 | `Input` 加边沿触发 `spawn`。 |
| `main.cpp` 改 | World + `P_SetupLevel` + `player_t.mo` + 思考者 tick + 每帧 `P_BuildThingsBySubsec` + 抛桶 + `--tickdump`。 |
| `tests/test_info.cpp`、`tests/test_p_mobj.cpp`、`tests/test_p_tick.cpp` **新**；`test_p_map.cpp`、`test_r_things.cpp` 改；`tests/CMakeLists.txt` 增源。 |
| `CMakeLists.txt` 增 `info.cpp`/`p_mobj.cpp`/`p_tick.cpp` 到 `doomcore`。 |

任务依赖：1 → 2 → 3 → 4 → 5 → 6 → 7（线性；Task 4/5 是渲染/碰撞收口，6 物理依赖 5 的泛化碰撞，7 依赖 6）。

---

### Task 1: info 表（mobjinfo / states / sprnames）

**Files:**
- Create: `src/play/info.h`, `src/play/info.cpp`, `tests/test_info.cpp`
- Modify: `CMakeLists.txt`（`doomcore` 增 `src/play/info.cpp`），`tests/CMakeLists.txt`（增 `test_info.cpp`）

**Interfaces:**
- Consumes: `core/doomtype.h`（`fixed_t`/`FRACBITS`）。
- Produces: `enum spritenum_t`/`enum statenum_t`/`enum mobjtype_t`；`struct state_t{...}`；`struct mobjinfo_t{...}`；`extern const state_t states[];`、`extern const mobjinfo_t mobjinfo[];`、`extern const char* const sprnames[];`；`int mobjtypeForDoomednum(int doomednum);`（-1 if unknown）；MF_/FF_ 常量。

- [ ] **Step 1: 写失败测试 `tests/test_info.cpp`**

```cpp
#include "doctest.h"
#include "play/info.h"

TEST_CASE("sprnames maps spritenum to 4-char name") {
    CHECK(std::string(sprnames[SPR_PLAY]) == "PLAY");
    CHECK(std::string(sprnames[SPR_BAR1]) == "BAR1");
    CHECK(std::string(sprnames[SPR_TROO]) == "TROO");
    CHECK(std::string(sprnames[SPR_POSS]) == "POSS");
}

TEST_CASE("mobjtypeForDoomednum: E1M1 mappings + skips unknown/player-start-handled-by-caller") {
    CHECK(mobjtypeForDoomednum(2035) == MT_BARREL);
    CHECK(mobjtypeForDoomednum(3004) == MT_POSSESSED);
    CHECK(mobjtypeForDoomednum(3001) == MT_TROOP);
    CHECK(mobjtypeForDoomednum(9)    == MT_SHOTGUY);
    CHECK(mobjtypeForDoomednum(1)    == MT_PLAYER);     // player start
    CHECK(mobjtypeForDoomednum(11)   == -1);            // DM start -> not spawned
    CHECK(mobjtypeForDoomednum(9999) == -1);            // unknown
}

TEST_CASE("spawnstate sprite/frame/fullbright reproduces P4 SpriteDef") {
    auto check = [](int dn, const char* spr, char frame, bool fb){
        int mt = mobjtypeForDoomednum(dn); REQUIRE(mt >= 0);
        const state_t& st = states[mobjinfo[mt].spawnstate];
        CHECK(std::string(sprnames[st.sprite]) == spr);
        CHECK(('A' + (st.frame & FF_FRAMEMASK)) == frame);
        CHECK(((st.frame & FF_FULLBRIGHT) != 0) == fb);
    };
    check(2035, "BAR1", 'A', false);
    check(3004, "POSS", 'A', false);
    check(3001, "TROO", 'A', false);
    check(9,    "SPOS", 'A', false);
    check(18,   "POSS", 'L', false);   // hanging corpse (gibbed former human)
    check(20,   "TROO", 'M', false);
    check(21,   "SARG", 'N', false);
    check(15,   "PLAY", 'N', false);
    check(2013, "SOUL", 'A', true);    // fullbright
    check(2028, "COLU", 'A', true);
}

TEST_CASE("MT_PLAYER collision footprint equals P3.5 constants") {
    CHECK(mobjinfo[MT_PLAYER].radius == 16 << FRACBITS);
    CHECK(mobjinfo[MT_PLAYER].height == 56 << FRACBITS);
    CHECK((mobjinfo[MT_PLAYER].flags & MF_SOLID)   != 0);
    CHECK((mobjinfo[MT_PLAYER].flags & MF_DROPOFF) != 0);
}

TEST_CASE("MT_BARREL is solid + has gravity (no NOGRAVITY)") {
    CHECK((mobjinfo[MT_BARREL].flags & MF_SOLID)      != 0);
    CHECK((mobjinfo[MT_BARREL].flags & MF_NOGRAVITY)  == 0);
    CHECK(mobjinfo[MT_BARREL].radius > 0);
    CHECK(mobjinfo[MT_BARREL].height > 0);
}
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `cmake --build build --config Release`（先会因 `test_info.cpp`/`info.h` 不在 CMake 而报错；先做 Step 3 注册再构建），然后 `ctest --test-dir build --build-config Release -R test_info --output-on-failure`
Expected: 编译失败（`play/info.h` 不存在）→ 注册后测试 FAIL（符号未定义）。

- [ ] **Step 3: 写 `src/play/info.h`**

```cpp
#pragma once
#include "core/doomtype.h"

// ---- frame bit flags (vanilla info.h) ----
constexpr int FF_FRAMEMASK   = 0x3FFF;
constexpr int FF_FULLBRIGHT  = 0x8000;

// ---- mobj flags (vanilla mobjinfo, subset used through P5b) ----
constexpr int MF_SPECIAL     = 1;       // P5b pick-up
constexpr int MF_SOLID       = 2;
constexpr int MF_SHOOTABLE   = 4;       // P5b damage
constexpr int MF_NOSECTOR    = 8;
constexpr int MF_NOBLOCKMAP  = 16;
constexpr int MF_AMBUSH      = 32;
constexpr int MF_JUSTHIT     = 64;
constexpr int MF_JUSTATTACKED= 128;
constexpr int MF_SPAWNCEILING= 256;
constexpr int MF_NOGRAVITY   = 512;
constexpr int MF_DROPOFF     = 1024;
constexpr int MF_PICKUP      = 2048;
constexpr int MF_NOCLIP      = 4096;
constexpr int MF_SLIDE       = 8192;
constexpr int MF_FLOAT       = 16384;
constexpr int MF_TELEPORT    = 32768;
constexpr int MF_MISSILE     = 65536;
constexpr int MF_DROPPED     = 131072;
constexpr int MF_SHADOW      = 262144;
constexpr int MF_NOBLOOD     = 524288;
constexpr int MF_CORPSE      = 1048576;
constexpr int MF_INFLOAT     = 2097152;
constexpr int MF_COUNTKILL   = 4194304;
constexpr int MF_COUNTITEM   = 8388608;
constexpr int MF_SKULLFLY    = 16777216;
constexpr int MF_NOTDMATCH   = 33554432;

enum spritenum_t {
    SPR_NONE = -1,
    SPR_PLAY, SPR_POSS, SPR_SPOS, SPR_TROO, SPR_SARG, SPR_BAR1,
    SPR_BKEY, SPR_CELP, SPR_POL5, SPR_POL6, SPR_TRE1, SPR_TRE2,
    SPR_SMIT, SPR_ELEC, SPR_GOR4, SPR_SHOT, SPR_MGUN, SPR_LAUN,
    SPR_PLAS, SPR_CSAW, SPR_CLIP, SPR_SHEL, SPR_ROCK, SPR_STIM,
    SPR_MEDI, SPR_SOUL, SPR_BON1, SPR_BON2, SPR_ARM1, SPR_COLU,
    SPR_BROK, SPR_AMMO, SPR_SBOX,
    NUMSPRITES
};
extern const char* const sprnames[];   // index = spritenum_t

enum statenum_t {
    S_NULL = 0,
    S_PLAY, S_BAR1, S_TROO_STND, S_POSS_STND, S_SPOS_STND, S_SARG_STND,
    // P4-decoration spawn states (one per E1M1 renderable doomednum that is not a live monster)
    S_D5, S_D10, S_D12, S_D15, S_D17, S_D18, S_D20, S_D21, S_D24, S_D26,
    S_D43, S_D47, S_D48, S_D54, S_D60,
    S_D2001, S_D2002, S_D2003, S_D2004, S_D2005, S_D2007, S_D2008, S_D2010,
    S_D2011, S_D2012, S_D2013, S_D2014, S_D2015, S_D2018, S_D2028,
    S_D2046, S_D2048, S_D2049,
    NUMSTATES
};

enum mobjtype_t {
    MT_PLAYER = 0, MT_BARREL,
    MT_POSSESSED, MT_SHOTGUY, MT_TROOP, MT_SERGEANT,
    // decoration/item types mirror the S_D* spawn states, ordered by doomednum
    MT_D5, MT_D10, MT_D12, MT_D15, MT_D17, MT_D18, MT_D20, MT_D21, MT_D24, MT_D26,
    MT_D43, MT_D47, MT_D48, MT_D54, MT_D60,
    MT_D2001, MT_D2002, MT_D2003, MT_D2004, MT_D2005, MT_D2007, MT_D2008, MT_D2010,
    MT_D2011, MT_D2012, MT_D2013, MT_D2014, MT_D2015, MT_D2018, MT_D2028,
    MT_D2046, MT_D2048, MT_D2049,
    NUMMOBJTYPES
};

struct state_t {
    spritenum_t sprite;
    int         frame;        // 0-based; FF_FULLBRIGHT bit set => fullbright
    int         tics;         // -1 = never auto-advance
    statenum_t  nextstate;
    // action function pointer omitted in P5a (no AI). P6 adds it here.
};
extern const state_t states[];   // index = statenum_t

struct mobjinfo_t {
    int        doomednum;     // -1 = not spawnable from a map thing
    statenum_t spawnstate;
    int        spawnhealth;
    fixed_t    radius;
    fixed_t    height;
    fixed_t    speed;
    int        mass;
    int        flags;
    // P5b-ready (unused in P5a, included so the table is ported once):
    statenum_t seestate, painstate, meleestate, missilestate, deathstate, xdeathstate;
    int        painchance, damage, reactiontime;
};
extern const mobjinfo_t mobjinfo[];   // index = mobjtype_t

// doomednum -> mobjtype_t; -1 if not spawnable (DM starts, unknown).
int mobjtypeForDoomednum(int doomednum);
```

- [ ] **Step 4: 写 `src/play/info.cpp`（数据表）**

```cpp
#include "info.h"

const char* const sprnames[] = {
    "PLAY","POSS","SPOS","TROO","SARG","BAR1",
    "BKEY","CELP","POL5","POL6","TRE1","TRE2",
    "SMIT","ELEC","GOR4","SHOT","MGUN","LAUN",
    "PLAS","CSAW","CLIP","SHEL","ROCK","STIM",
    "MEDI","SOUL","BON1","BON2","ARM1","COLU",
    "BROK","AMMO","SBOX",
};

// states[]: S_NULL then live spawn states then one per decoration/item.
// All E1M1 renderables are static in P5a -> tics = -1 (never advance), nextstate = self.
// Frame letter convention: stored 0-based; FF_FULLBRIGHT OR-ed for bright sprites.
const state_t states[] = {
    /* S_NULL */   { SPR_NONE, 0, -1, S_NULL },
    /* S_PLAY */   { SPR_PLAY, 0, -1, S_PLAY },
    /* S_BAR1 */   { SPR_BAR1, 0, -1, S_BAR1 },
    /* S_TROO_STND */ { SPR_TROO, 0, -1, S_TROO_STND },
    /* S_POSS_STND */ { SPR_POSS, 0, -1, S_POSS_STND },
    /* S_SPOS_STND */ { SPR_SPOS, 0, -1, S_SPOS_STND },
    /* S_SARG_STND */ { SPR_SARG, 0, -1, S_SARG_STND },
    /* S_D5  (BKEY A)   */ { SPR_BKEY, 0, -1, S_D5  },
    /* S_D10 (PLAY W)   */ { SPR_PLAY, 'W'-'A', -1, S_D10 },
    /* S_D12 (PLAY W)   */ { SPR_PLAY, 'W'-'A', -1, S_D12 },
    /* S_D15 (PLAY N)   */ { SPR_PLAY, 'N'-'A', -1, S_D15 },
    /* S_D17 (CELP A)   */ { SPR_CELP, 0, -1, S_D17 },
    /* S_D18 (POSS L)   */ { SPR_POSS, 'L'-'A', -1, S_D18 },
    /* S_D20 (TROO M)   */ { SPR_TROO, 'M'-'A', -1, S_D20 },
    /* S_D21 (SARG N)   */ { SPR_SARG, 'N'-'A', -1, S_D21 },
    /* S_D24 (POL5 A)   */ { SPR_POL5, 0, -1, S_D24 },
    /* S_D26 (POL6 A)   */ { SPR_POL6, 0, -1, S_D26 },
    /* S_D43 (TRE1 A)   */ { SPR_TRE1, 0, -1, S_D43 },
    /* S_D47 (SMIT A)   */ { SPR_SMIT, 0, -1, S_D47 },
    /* S_D48 (ELEC A)   */ { SPR_ELEC, 0, -1, S_D48 },
    /* S_D54 (TRE2 A)   */ { SPR_TRE2, 0, -1, S_D54 },
    /* S_D60 (GOR4 A)   */ { SPR_GOR4, 0, -1, S_D60 },
    /* S_D2001 (SHOT A) */ { SPR_SHOT, 0, -1, S_D2001 },
    /* S_D2002 (MGUN A) */ { SPR_MGUN, 0, -1, S_D2002 },
    /* S_D2003 (LAUN A) */ { SPR_LAUN, 0, -1, S_D2003 },
    /* S_D2004 (PLAS A) */ { SPR_PLAS, 0, -1, S_D2004 },
    /* S_D2005 (CSAW A) */ { SPR_CSAW, 0, -1, S_D2005 },
    /* S_D2007 (CLIP A) */ { SPR_CLIP, 0, -1, S_D2007 },
    /* S_D2008 (SHEL A) */ { SPR_SHEL, 0, -1, S_D2008 },
    /* S_D2010 (ROCK A) */ { SPR_ROCK, 0, -1, S_D2010 },
    /* S_D2011 (STIM A) */ { SPR_STIM, 0, -1, S_D2011 },
    /* S_D2012 (MEDI A) */ { SPR_MEDI, 0, -1, S_D2012 },
    /* S_D2013 (SOUL A, bright) */ { SPR_SOUL, 0 | FF_FULLBRIGHT, -1, S_D2013 },
    /* S_D2014 (BON1 A) */ { SPR_BON1, 0, -1, S_D2014 },
    /* S_D2015 (BON2 A) */ { SPR_BON2, 0, -1, S_D2015 },
    /* S_D2018 (ARM1 A) */ { SPR_ARM1, 0, -1, S_D2018 },
    /* S_D2028 (COLU A, bright) */ { SPR_COLU, 0 | FF_FULLBRIGHT, -1, S_D2028 },
    /* S_D2046 (BROK A) */ { SPR_BROK, 0, -1, S_D2046 },
    /* S_D2048 (AMMO A) */ { SPR_AMMO, 0, -1, S_D2048 },
    /* S_D2049 (SBOX A) */ { SPR_SBOX, 0, -1, S_D2049 },
};

// radius/height in 16.16. Live monsters/player/barrel use vanilla values (P5a only
// physically simulates MT_PLAYER via direct displacement and MT_BARREL when tossed);
// items/decorations are MF_NOBLOCKMAP|MF_NOGRAVITY (inert in P5a). pain/death fields
// are placeholder S_NULL until P5b ports real state chains.
const mobjinfo_t mobjinfo[] = {
    // doomednum, spawnstate, health, radius, height, speed, mass, flags, seestate,painstate,meleestate,missilestate,deathstate,xdeathstate, painchance,damage,reactiontime
    /* MT_PLAYER     */ {1,    S_PLAY,    100, 16<<FRACBITS, 56<<FRACBITS, 24<<FRACBITS, 100, MF_SOLID|MF_DROPOFF,  S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_BARREL     */ {2035, S_BAR1,    20,  10<<FRACBITS, 42<<FRACBITS, 7<<FRACBITS,  0,   MF_SOLID|MF_DROPOFF|MF_NOBLOOD, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_POSSESSED  */ {3004, S_POSS_STND,20, 20<<FRACBITS, 56<<FRACBITS, 8<<FRACBITS,  100, MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 200,3,8 },
    /* MT_SHOTGUY    */ {9,    S_SPOS_STND,30, 20<<FRACBITS, 56<<FRACBITS, 8<<FRACBITS,  100, MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 200,3,8 },
    /* MT_TROOP      */ {3001, S_TROO_STND,60, 16<<FRACBITS, 56<<FRACBITS, 8<<FRACBITS,  100, MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 200,3,8 },
    /* MT_SERGEANT   */ {3002, S_SARG_STND,150,30<<FRACBITS, 56<<FRACBITS, 8<<FRACBITS,  300, MF_SOLID|MF_SHOOTABLE|MF_COUNTKILL, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 60,3,8 },
    /* MT_D5  */ {5,    S_D5,   1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D10 */ {10,   S_D10,  1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_NOBLOCKMAP|MF_NOGRAVITY, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D12 */ {12,   S_D12,  1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_NOBLOCKMAP|MF_NOGRAVITY, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D15 */ {15,   S_D15,  1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_NOBLOCKMAP|MF_NOGRAVITY, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D17 */ {17,   S_D17,  1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_NOBLOCKMAP|MF_NOGRAVITY, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D18 */ {18,   S_D18,  1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_NOBLOCKMAP|MF_NOGRAVITY|MF_NOBLOOD, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D20 */ {20,   S_D20,  1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_NOBLOCKMAP|MF_NOGRAVITY|MF_NOBLOOD, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D21 */ {21,   S_D21,  1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_NOBLOCKMAP|MF_NOGRAVITY|MF_NOBLOOD, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D24 */ {24,   S_D24,  1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_NOBLOCKMAP|MF_NOGRAVITY, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D26 */ {26,   S_D26,  1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_NOBLOCKMAP|MF_NOGRAVITY, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D43 */ {43,   S_D43,  1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_SOLID|MF_NOBLOCKMAP, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D47 */ {47,   S_D47,  1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_SOLID|MF_NOBLOCKMAP, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D48 */ {48,   S_D48,  1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_SOLID|MF_NOBLOCKMAP, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D54 */ {54,   S_D54,  1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_SOLID|MF_NOBLOCKMAP, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D60 */ {60,   S_D60,  1000, 16<<FRACBITS,56<<FRACBITS,0,100, MF_NOBLOCKMAP|MF_NOGRAVITY, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2001 */ {2001,S_D2001,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2002 */ {2002,S_D2002,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2003 */ {2003,S_D2003,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2004 */ {2004,S_D2004,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2005 */ {2005,S_D2005,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2007 */ {2007,S_D2007,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2008 */ {2008,S_D2008,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2010 */ {2010,S_D2010,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2011 */ {2011,S_D2011,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2012 */ {2012,S_D2012,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2013 */ {2013,S_D2013,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2014 */ {2014,S_D2014,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2015 */ {2015,S_D2015,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2018 */ {2018,S_D2018,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2028 */ {2028,S_D2028,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SOLID|MF_NOBLOCKMAP, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2046 */ {2046,S_D2046,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2048 */ {2048,S_D2048,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
    /* MT_D2049 */ {2049,S_D2049,1000,16<<FRACBITS,56<<FRACBITS,0,100, MF_SPECIAL|MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF, S_NULL,S_NULL,S_NULL,S_NULL,S_NULL,S_NULL, 0,0,8 },
};

int mobjtypeForDoomednum(int dn) {
    for (int i = 0; i < NUMMOBJTYPES; ++i)
        if (mobjinfo[i].doomednum == dn) return i;
    return -1;
}
```

> 实现者校对：`states[]` 顺序必须与 `statenum_t` 枚举顺序逐项对齐（含 `S_NULL` 占首）。`mobjinfo[]` 顺序必须与 `mobjtype_t` 对齐。表共 38 个可生成 doomednum（覆盖 P4 全部渲染物）+ MT_PLAYER。

- [ ] **Step 5: 注册源（`CMakeLists.txt` 的 `doomcore` 增 `src/play/info.cpp`；`tests/CMakeLists.txt` 增 `test_info.cpp`）**

- [ ] **Step 6: 构建并运行测试，确认全绿**

Run: `cmake --build build --config Release && ctest --test-dir build --build-config Release -R test_info --output-on-failure`
Expected: PASS（5 个 case）。再跑全量 `ctest --test-dir build --build-config Release --output-on-failure` 确认无回归。

- [ ] **Step 7: 提交**

```bash
git add src/play/info.h src/play/info.cpp tests/test_info.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(info): mobjinfo/states/sprnames tables + doomednum->type (P5a)"
```

---

### Task 2: 把 mobj_t 迁到 p_mobj.h；渲染器读 z 为定点；main.cpp 改用 info 查表；删 SpriteDef

**目标**：`mobj_t` 从 `render/r_things.h` 迁到 `play/p_mobj.h` 并扩成活对象（新字段默认 0），渲染器对 `z` 按 16.16 取高位，`main.cpp` 的 mob 构建改走 `mobjtypeForDoomednum`+`states`（复现 P4 映射），删除 `SpriteDef`/`spriteDefForType`。**渲染结果与 P4 逐一致。**

**Files:**
- Create: `src/play/p_mobj.h`
- Modify: `src/render/r_things.h`, `src/render/r_things.cpp`, `src/main.cpp`, `tests/test_r_things.cpp`

**Interfaces:**
- Consumes: Task 1 的 `info.h`（`mobjtypeForDoomednum`/`states`/`mobjinfo`/`sprnames`/`FF_*`）。
- Produces: `play/p_mobj.h` 的 `struct mobj_t`（被 `r_things`/`r_bsp`/`main` 使用；本任务仅结构 + 默认值，无方法）。

- [ ] **Step 1: 写 `src/play/p_mobj.h`**

```cpp
#pragma once
#include "core/doomtype.h"
#include "play/info.h"
#include <cstring>

// 活 mobj（P5a）。渲染缓存字段（sprname/frame/fullbright/light）由 P_SetMobjState 刷新
// （Task 3）；Task 2 期间 main.cpp 仍直接填写它们。z 现为 16.16 定点（P4 是 int map units）。
struct mobj_t {
    fixed_t x = 0, y = 0, z = 0;            // 16.16
    fixed_t momx = 0, momy = 0, momz = 0;   // 动量（Task 6 物理）
    angle_t angle = 0;                      // BAM
    fixed_t floorz = 0, ceilingz = 0, dropoffz = 0;
    int     health = 0;
    int     flags = 0;
    int     tics = -1;                      // 当前 state 剩余 tic
    const mobjinfo_t* info = nullptr;
    const state_t*   state = nullptr;
    int     subsector = -1;
    // —— 渲染缓存（渲染器只读）——
    char    sprname[5] = {0};
    char    frame = 'A';                    // 'A'-based（SpriteCache.resolve 做 frame-'A'）
    bool    fullbright = false;
    int     light = 160;
};
```

- [ ] **Step 2: 改 `src/render/r_things.h`** —— 删 `mobj_t` 结构定义、`SpriteDef`、`spriteDefForType`；改为 include。

把现有的 `struct SpriteDef {...}; const SpriteDef* spriteDefForType(int);` 与整个 `struct mobj_t {...};` 块删除，在文件顶部 `#include` 区加：

```cpp
#include "play/p_mobj.h"   // mobj_t (moved from here in P5a)
```

保留 `vissprite_t`、`R_ProjectSprite`（签名仍是 `const mobj_t&`，只是 mobj_t 现来自 p_mobj.h）、`R_DrawVisSprite`、`SpriteCache`、`parseSpriteLumpName`、`spriteRotationIndex`。

- [ ] **Step 3: 改 `src/render/r_things.cpp`** —— 删 `kSpriteDefs` 表与 `spriteDefForType`；`R_ProjectSprite` 的 `gzt` 用 `t.z >> FRACBITS`。

删除 `namespace { const SpriteDef kSpriteDefs[] = {...}; }` 与 `const SpriteDef* spriteDefForType(int)` 整个函数。

在 `R_ProjectSprite` 内把：
```cpp
out.gzt = static_cast<float>(t.z) + static_cast<float>(v.patch->topoffset);
```
改为：
```cpp
out.gzt = static_cast<float>(t.z >> FRACBITS) + static_cast<float>(v.patch->topoffset);
```
（`t.z` 现为 16.16；其它字段读取不变。）

- [ ] **Step 4: 改 `src/main.cpp` 两处 mob 构建（`--dumpframe` 与实时）**

把每处的：
```cpp
const SpriteDef* def = spriteDefForType(th.type);
if (!def) continue;
mobj_t m;
m.x = static_cast<fixed_t>(th.x) << FRACBITS;
m.y = static_cast<fixed_t>(th.y) << FRACBITS;
m.angle = degreesToBam(th.angleDeg);
std::memcpy(m.sprname, def->sprname, 4); m.sprname[4] = 0;
m.frame = def->frame;
m.fullbright = def->fullbright;
int ss = R_PointInSubsector(map, m.x, m.y);
int sec = sectorOf(map, ss);
m.z    = (sec >= 0) ? map.sectors[sec].floorheight : 0;
m.light = (sec >= 0) ? map.sectors[sec].lightlevel : 160;
```
改为：
```cpp
int mt = mobjtypeForDoomednum(th.type);
if (mt < 0) continue;
const state_t& st = states[mobjinfo[mt].spawnstate];
mobj_t m;
m.x = static_cast<fixed_t>(th.x) << FRACBITS;
m.y = static_cast<fixed_t>(th.y) << FRACBITS;
m.angle = degreesToBam(th.angleDeg);
std::memcpy(m.sprname, sprnames[st.sprite], 5);     // 4 字符 + NUL
m.frame = static_cast<char>('A' + (st.frame & FF_FRAMEMASK));
m.fullbright = (st.frame & FF_FULLBRIGHT) != 0;
int ss = R_PointInSubsector(map, m.x, m.y);
int sec = sectorOf(map, ss);
m.z    = (sec >= 0) ? (static_cast<fixed_t>(map.sectors[sec].floorheight) << FRACBITS) : 0;
m.light = (sec >= 0) ? map.sectors[sec].lightlevel : 160;
```
并在 `main.cpp` 顶部 `#include` 区加 `#include "play/info.h"`。

- [ ] **Step 5: 改 `tests/test_r_things.cpp`** —— 删除 `spriteDefForType` 测试用例。

删除整个 `TEST_CASE("spriteDefForType maps doomednum to sprite, skips player/DM starts") {...}` 块。其余用例（`parseSpriteLumpName`/`SpriteCache`/`spriteRotationIndex`/`R_ProjectSprite`/`R_DrawVisSprite`）不动——它们直接填 `mobj_t` 的 `x/y/z/angle/sprname/frame/light`，字段名未变，`t.z=0` 经 `>>FRACBITS` 仍为 0，数值不变。

- [ ] **Step 6: 构建 + 全量测试**

Run: `cmake --build build --config Release && ctest --test-dir build --build-config Release --output-on-failure`
Expected: 全绿。

- [ ] **Step 7: 渲染零回归核对**

Run: `./build/Release/doomcpp.exe --dumpframe assets/freedoom1.wad p5a_t2_baseline.bmp 90 1056 -2144`
（玩家起点附近固定相机；参数沿用你之前 P4 核对用的同一组。实现者：用与 P4 基线**完全相同**的命令行参数。）
Expected: 输出 `wrote p5a_t2_baseline.bmp (mobs=N)`，N 与 P4 一致；肉眼/`analyze_image` 比 P4 同位置帧无差异（精灵数量/位置/朝向/明暗）。若 mobs 数量变化 → info 表缺/多条目，回到 Task 1 修。

- [ ] **Step 8: 提交**

```bash
git add src/play/p_mobj.h src/render/r_things.h src/render/r_things.cpp src/main.cpp tests/test_r_things.cpp
git commit -m "refactor(p_mobj): move mobj_t to play layer; z as fixed; info lookup; drop SpriteDef"
```

---

### Task 3: World + mobj 生命周期（spawn/state/remove）+ P_SetupLevel + P_BuildThingsBySubsec

**目标**：建立 `World`/`player_t` 与 mobj 生命周期函数（`P_SpawnMobj`/`P_SetMobjState`/`P_RemoveMobj`/`P_SetThingPosition`）、`P_SetupLevel`（THINGS→活 mobj）、`P_BuildThingsBySubsec`（每帧分桶）。本任务**只新增库代码 + 单测**，不接 main.cpp、不动渲染器签名、不做物理。

**Files:**
- Create: `src/play/p_mobj.cpp`, `src/play/p_tick.h`, `src/play/p_tick.cpp`, `tests/test_p_mobj.cpp`, `tests/test_p_tick.cpp`
- Modify: `src/play/p_mobj.h`（加函数声明）, `CMakeLists.txt`（`doomcore` 增 `p_mobj.cpp`/`p_tick.cpp`）, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 `info.h`；`p_setup.h`（`MapData`/`thing_t`/`loadMap`）；`p_maputl.h`（`R_PointInSubsector`/`sectorOf`）。
- Produces:
  - `struct player_t { mobj_t* mo = nullptr; fixed_t viewz = 0; };`（`p_tick.h`）
  - `struct World { const MapData* map=nullptr; const Blockmap* bm=nullptr; std::list<mobj_t> mobjs; player_t player; std::uint32_t tic=0; std::vector<std::vector<const mobj_t*>> thingsBySubsec; };`（`p_tick.h`）
  - `mobj_t* P_SpawnMobj(World&, fixed_t x, fixed_t y, fixed_t z, mobjtype_t type);`
  - `void P_SetMobjState(mobj_t&, statenum_t);` / `void P_RemoveMobj(World&, mobj_t&);` / `void P_SetThingPosition(World&, mobj_t&);`
  - `void P_SetupLevel(World&, const MapData&);`（用 `world.map`/`world.bm`；spawn 所有 THINGS + 玩家）
  - `void P_BuildThingsBySubsec(World&);`

- [ ] **Step 1: 写失败测试 `tests/test_p_mobj.cpp`**

```cpp
#include "doctest.h"
#include "play/p_mobj.h"
#include "play/p_tick.h"
#include "play/info.h"

TEST_CASE("P_SpawnMobj inits fields from mobjinfo + spawnstate render cache") {
    World w;
    mobj_t* m = P_SpawnMobj(w, 100<<FRACBITS, 200<<FRACBITS, 0, MT_BARREL);
    REQUIRE(m != nullptr);
    CHECK(m->x == 100<<FRACBITS); CHECK(m->y == 200<<FRACBITS);
    CHECK(m->info == &mobjinfo[MT_BARREL]);
    CHECK(m->health == mobjinfo[MT_BARREL].spawnhealth);
    CHECK(m->flags == mobjinfo[MT_BARREL].flags);
    CHECK(std::string(m->sprname) == "BAR1");
    CHECK(m->frame == 'A');
    CHECK(w.mobjs.size() == 1);
    CHECK(&w.mobjs.back() == m);            // 返回的指针指向 list 节点（稳定）
}

TEST_CASE("P_SetMobjState refreshes render cache (sprite/frame/fullbright)") {
    World w;
    mobj_t* m = P_SpawnMobj(w, 0,0,0, MT_D2013);   // SOUL A fullbright
    CHECK(std::string(m->sprname) == "SOUL");
    CHECK(m->fullbright == true);
    CHECK(m->tics == -1);                          // static decoration -> never advance
    P_SetMobjState(*m, S_BAR1);                    // switch to barrel sprite
    CHECK(std::string(m->sprname) == "BAR1");
    CHECK(m->frame == 'A');
    CHECK(m->fullbright == false);
}

TEST_CASE("P_RemoveMobj erases from the thinker list") {
    World w;
    mobj_t* a = P_SpawnMobj(w, 0,0,0, MT_BARREL);
    P_SpawnMobj(w, 0,0,0, MT_BARREL);
    REQUIRE(w.mobjs.size() == 2);
    P_RemoveMobj(w, *a);
    CHECK(w.mobjs.size() == 1);
}
```

- [ ] **Step 2: 写失败测试 `tests/test_p_tick.cpp`**

```cpp
#include "doctest.h"
#include "play/p_tick.h"
#include "play/p_mobj.h"
#include "play/p_setup.h"
#include "play/p_maputl.h"

namespace {
fixed_t F(int v){ return static_cast<fixed_t>(v)<<FRACBITS; }
// 单 subsector、无 node 的最小图（沿用 test_p_map 约定：R_PointInSubsector 返回 0）。
// 若实现发现 R_PointInSubsector 需要 node，则补一个 node：children=[NF_SUBSECTOR|0, NF_SUBSECTOR|0]。
MapData oneSectorMap() {
    MapData m;
    sector_t s; s.floorheight=0; s.ceilingheight=128; s.lightlevel=160; m.sectors.push_back(s);
    subsector_t ss; ss.segcount=0; ss.firstseg=0; m.subsectors.push_back(ss);
    return m;
}
}

TEST_CASE("P_SetupLevel spawns one mobj per known thing + player as mo") {
    MapData m = oneSectorMap();
    m.things.push_back({256,256,90,2035});   // barrel
    m.things.push_back({300,300,90,3004});   // zombieman
    m.things.push_back({400,400,90,11});     // DM start -> skipped
    m.things.push_back({500,500,90,1});      // player start
    World w; w.map = &m; w.bm = &m.blockmap;
    P_SetupLevel(w, m);
    CHECK(w.mobjs.size() == 3);              // barrel + zombieman + player
    CHECK(w.player.mo != nullptr);
    CHECK(w.player.mo->info == &mobjinfo[MT_PLAYER]);
    // 玩家坐标取自 thing 1 起点
    CHECK(w.player.mo->x == F(500));
    CHECK(w.player.mo->y == F(500));
}

TEST_CASE("P_BuildThingsBySubsec bins mobjs by subsector, skips player.mo") {
    MapData m = oneSectorMap();
    m.things.push_back({256,256,90,2035});
    m.things.push_back({300,300,90,3004});
    World w; w.map = &m; w.bm = &m.blockmap;
    P_SetupLevel(w, m);
    P_BuildThingsBySubsec(w);
    REQUIRE(w.thingsBySubsec.size() >= 1);
    // subsector 0 持有 barrel+zombieman，不含 player.mo
    int counted = 0; bool sawPlayer = false;
    for (const mobj_t* mo : w.thingsBySubsec[0]) {
        ++counted;
        if (mo == w.player.mo) sawPlayer = true;
    }
    CHECK(counted == 2);
    CHECK(sawPlayer == false);
}
```

- [ ] **Step 3: 写 `src/play/p_tick.h`**

```cpp
#pragma once
#include "core/doomtype.h"
#include "play/p_mobj.h"
#include "play/info.h"
#include <cstddef>
#include <list>
#include <vector>

struct MapData;
struct Blockmap;

struct player_t {
    mobj_t*  mo = nullptr;
    fixed_t  viewz = 0;
};

struct World {
    const MapData*              map = nullptr;
    const Blockmap*             bm  = nullptr;
    std::list<mobj_t>           mobjs;
    player_t                    player;
    std::uint32_t               tic = 0;
    std::vector<std::vector<const mobj_t*>> thingsBySubsec;
};

mobj_t* P_SpawnMobj(World& w, fixed_t x, fixed_t y, fixed_t z, mobjtype_t type);
void    P_SetMobjState(mobj_t& m, statenum_t st);
void    P_RemoveMobj(World& w, mobj_t& m);
void    P_SetThingPosition(World& w, mobj_t& m);   // set m.subsector from (x,y)

void    P_SetupLevel(World& w, const MapData& m);   // spawn all THINGS + player
void    P_BuildThingsBySubsec(World& w);            // rebuild per-frame render bins

void    P_RunThinkers(World& w);                    // defined in Task 6 (physics)
```

- [ ] **Step 4: 写 `src/play/p_mobj.cpp`（生命周期）**

```cpp
#include "p_mobj.h"
#include "p_tick.h"
#include "p_maputl.h"
#include "p_setup.h"
#include "info.h"
#include <cstring>

mobj_t* P_SpawnMobj(World& w, fixed_t x, fixed_t y, fixed_t z, mobjtype_t type) {
    w.mobjs.push_back({});
    mobj_t& m = w.mobjs.back();
    m.x = x; m.y = y; m.z = z;
    m.info = &mobjinfo[type];
    m.health = m.info->spawnhealth;
    m.flags = m.info->flags;
    P_SetMobjState(m, m.info->spawnstate);
    P_SetThingPosition(w, m);
    return &m;
}

void P_SetMobjState(mobj_t& m, statenum_t sn) {
    if (sn == S_NULL) { m.state = &states[S_NULL]; m.tics = -1; return; }
    m.state = &states[sn];
    m.tics = m.state->tics;
    // refresh render cache (faithful: copy state->sprite/frame into mobj)
    if (m.state->sprite != SPR_NONE)
        std::memcpy(m.sprname, sprnames[m.state->sprite], 5);
    m.frame = static_cast<char>('A' + (m.state->frame & FF_FRAMEMASK));
    m.fullbright = (m.state->frame & FF_FULLBRIGHT) != 0;
}

void P_RemoveMobj(World& w, mobj_t& m) {
    for (auto it = w.mobjs.begin(); it != w.mobjs.end(); ++it)
        if (&*it == &m) { w.mobjs.erase(it); return; }
}

void P_SetThingPosition(World& w, mobj_t& m) {
    if (!w.map) return;
    m.subsector = R_PointInSubsector(*w.map, m.x, m.y);
}
```

- [ ] **Step 5: 写 `src/play/p_tick.cpp`（P_SetupLevel / P_BuildThingsBySubsec）**

```cpp
#include "p_tick.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_maputl.h"
#include "info.h"

void P_SetupLevel(World& w, const MapData& m) {
    w.mobjs.clear();
    w.player.mo = nullptr;
    for (const thing_t& th : m.things) {
        int mt = mobjtypeForDoomednum(th.type);
        if (mt == MT_PLAYER) {
            // 玩家单独 spawn 并挂到 player.mo（第一人称不画自己 -> P_BuildThingsBySubsec 跳过）
            mobj_t* p = P_SpawnMobj(w, static_cast<fixed_t>(th.x)<<FRACBITS,
                                    static_cast<fixed_t>(th.y)<<FRACBITS, 0, MT_PLAYER);
            p->angle = degreesToBam(th.angleDeg);
            w.player.mo = p;
        } else if (mt >= 0) {
            mobj_t* mo = P_SpawnMobj(w, static_cast<fixed_t>(th.x)<<FRACBITS,
                                     static_cast<fixed_t>(th.y)<<FRACBITS, 0,
                                     static_cast<mobjtype_t>(mt));
            mo->angle = degreesToBam(th.angleDeg);
        }
    }
    // 给每个 mobj 定 z=floor、floorz/ceilingz、light（用其 subsector 的 sector）
    for (mobj_t& mo : w.mobjs) {
        int sec = (mo.subsector >= 0) ? sectorOf(m, mo.subsector) : -1;
        if (sec >= 0) {
            mo.z        = static_cast<fixed_t>(m.sectors[sec].floorheight)   << FRACBITS;
            mo.floorz   = mo.z;
            mo.ceilingz = static_cast<fixed_t>(m.sectors[sec].ceilingheight) << FRACBITS;
            mo.light    = m.sectors[sec].lightlevel;
        }
    }
    // 玩家初始 viewz
    if (w.player.mo) {
        w.player.mo->floorz = (w.player.mo->floorz ? w.player.mo->floorz : 0);
        w.player.viewz = w.player.mo->floorz + VIEWHEIGHT;
    }
}

void P_BuildThingsBySubsec(World& w) {
    if (!w.map) return;
    w.thingsBySubsec.assign(w.map->subsectors.size(), {});
    for (mobj_t& mo : w.mobjs) {
        if (&mo == w.player.mo) continue;              // 第一人称：不画玩家自身
        if (mo.flags & MF_NOSECTOR) continue;
        mo.subsector = R_PointInSubsector(*w.map, mo.x, mo.y);
        if (mo.subsector >= 0 && mo.subsector < static_cast<int>(w.thingsBySubsec.size()))
            w.thingsBySubsec[mo.subsector].push_back(&mo);
        // 刷新 light（物体可能移到新 sector）
        int sec = sectorOf(*w.map, mo.subsector);
        if (sec >= 0) mo.light = mo.fullbright ? 255 : w.map->sectors[sec].lightlevel;
    }
}

void P_RunThinkers(World&) { /* Task 6 */ }
```

> `degreesToBam` 与 `VIEWHEIGHT` 已在 `p_map.h`/`doomtype` 链中；`p_tick.cpp` include `p_map.h` 以取 `VIEWHEIGHT`。若 `degreesToBam` 不在此 include 链，实现者在 `p_setup.h` 或 `p_map.h` 中确认其声明并 include。

- [ ] **Step 6: `p_mobj.h` 末尾无需改（声明全在 p_tick.h）。注册 CMake（`doomcore` 增 `p_mobj.cpp`/`p_tick.cpp`；tests 增 `test_p_mobj.cpp`/`test_p_tick.cpp`）。**

- [ ] **Step 7: 构建 + 测试**

Run: `cmake --build build --config Release && ctest --test-dir build --build-config Release -R "test_p_mobj|test_p_tick" --output-on-failure`
Expected: PASS。再跑全量确认无回归。

- [ ] **Step 8: 提交**

```bash
git add src/play/p_mobj.cpp src/play/p_tick.h src/play/p_tick.cpp tests/test_p_mobj.cpp tests/test_p_tick.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(p_tick): World + mobj lifecycle + P_SetupLevel + P_BuildThingsBySubsec (P5a)"
```

---

### Task 4: 渲染器签名收口（thingsBySubsec 改 `const mobj_t*`，去 `things`）+ main 改用 World 喂渲染

**目标**：`R_RenderView` 去掉 `things` 向量、`thingsBySubsec` 改持 `const mobj_t*`；`main.cpp` 不再手建 mobs，改用 `World`+`P_SetupLevel`+每帧 `P_BuildThingsBySubsec`，把 `&w.thingsBySubsec` 喂给渲染器。**移动仍用旧 `Player`（Task 5 才切）；渲染==P4。**

**Files:**
- Modify: `src/render/r_bsp.h`, `src/render/r_bsp.cpp`, `src/main.cpp`

**Interfaces:**
- Consumes: Task 3 `World`/`P_SetupLevel`/`P_BuildThingsBySubsec`；`R_ProjectSprite(const mobj_t&, ...)`（Task 2 后已是 const&）。
- Produces: `R_RenderView(..., animTick, const SpriteCache* sprites, const std::vector<std::vector<const mobj_t*>>* thingsBySubsec)`（去掉 `things`）。

- [ ] **Step 1: 改 `src/render/r_bsp.h` 签名**

```cpp
void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  const TextureLookup& tex, float px, float py, float ang,
                  float eyeZ, uint32_t animTick,
                  const SpriteCache* sprites = nullptr,
                  const std::vector<std::vector<const mobj_t*>>* thingsBySubsec = nullptr);
```

- [ ] **Step 2: 改 `src/render/r_bsp.cpp`**

`Cam` 结构：删除 `const std::vector<mobj_t>* things = nullptr;`，把 `thingsBySubsec` 类型改为 `const std::vector<std::vector<const mobj_t*>>* thingsBySubsec = nullptr;`。

`renderSubsector` 的精灵收集块改为指针遍历：
```cpp
if (c.sprites && c.thingsBySubsec && idx >= 0 &&
    idx < static_cast<int>(c.thingsBySubsec->size())) {
    for (const mobj_t* mo : (*c.thingsBySubsec)[idx]) {
        if (!mo) continue;
        vissprite_t vs;
        if (R_ProjectSprite(vs, *mo, *c.sprites,
                            c.px, c.py, c.sin, c.cos, c.eyeZ, c.focal, c.w, c.h))
            c.vissprites.push_back(vs);
    }
}
```

`R_RenderView` 签名与函数体：删 `things` 形参；删 `c.things = things;`；保留 `c.thingsBySubsec = thingsBySubsec;`、`c.sprites = sprites;`。其余（墙/平面/精灵绘制）不变。

- [ ] **Step 3: 改 `src/main.cpp`** —— 实时模式与 `--dumpframe` 都改用 World。

顶部 include 加 `#include "play/p_tick.h"`。

**`--dumpframe` 分支**：删除其中手建的 `std::vector<mobj_t> mobs;` 与 `std::vector<std::vector<int>> thingsBySubsec(...)` 整段循环，替换为：
```cpp
World w; w.map = &map; w.bm = &map.blockmap;
P_SetupLevel(w, map);
P_BuildThingsBySubsec(w);
```
并把 `R_RenderView(...)` 调用改为（去掉 `&mobs`，末两项 `&sprites, &w.thingsBySubsec`）：
```cpp
R_RenderView(fb.data(), FW, FH, map, tex,
             static_cast<float>(px >> FRACBITS), static_cast<float>(py >> FRACBITS),
             bamToRendererRad(ang), static_cast<float>(eyeZ0 >> FRACBITS), 0,
             &sprites, &w.thingsBySubsec);
```
（`mobs=N` 的 cout 改为 `w.mobjs.size()`。）

**实时模式**：同样删除手建 mobs/thingsBySubsec 循环，替换为：
```cpp
World w; w.map = &map; w.bm = &map.blockmap;
P_SetupLevel(w, map);
```
保留 `Player p; {...playerStart...}`（移动仍用旧 Player）。主循环里渲染前加 `P_BuildThingsBySubsec(w);`，`R_RenderView` 调用改为：
```cpp
P_BuildThingsBySubsec(w);
R_RenderView(fb.data(), FB_W, FB_H, map, tex,
             static_cast<float>(p.x >> FRACBITS), static_cast<float>(p.y >> FRACBITS),
             bamToRendererRad(p.angle), static_cast<float>(p.viewz >> FRACBITS),
             ticCount, &sprites, &w.thingsBySubsec);
```

> 注意：本任务 `Player p`（移动）与 `w.player.mo`（P_SetupLevel 生成、暂不用）并存到 Task 5。无害。

- [ ] **Step 4: 构建 + 全量测试**

Run: `cmake --build build --config Release && ctest --test-dir build --build-config Release --output-on-failure`
Expected: 全绿（`R_RenderView` 无测试直接调用；main 是唯一调用方）。

- [ ] **Step 5: 渲染零回归核对**

Run: 与 Task 2 Step 7 相同的 `--dumpframe` 命令。
Expected: `mobs=N` 与 P4 一致；帧图景与 P4 一致。

- [ ] **Step 6: 提交**

```bash
git add src/render/r_bsp.h src/render/r_bsp.cpp src/main.cpp
git commit -m "refactor(r_bsp): R_RenderView sprite bins as const mobj_t*; main feeds World"
```

---

### Task 5: 泛化碰撞到 mobj_t + 玩家并入 player_t（删除 Player）— 最高回归风险

**目标**：`P_CheckPosition`/`P_TryMove`/`P_TrySlide` 改吃 `mobj_t&`（radius/height/flags 取自 `mobjinfo`）；`P_MovePlayer`/`P_CalcHeight` 改吃 `player_t`；删除 `struct Player`；`main.cpp` 移动改用 `w.player`（`player_t`，`mo` 指向 `w.player.mo`）。**MT_PLAYER 的 radius=16/height=56 与 P3.5 数值一致 → 碰撞行为不变。** `test_p_map.cpp` 全量改写为回归守卫。

**Files:**
- Modify: `src/play/p_map.h`, `src/play/p_map.cpp`, `src/main.cpp`, `tests/test_p_map.cpp`

**Interfaces:**
- Consumes: Task 1 `mobjinfo[MT_PLAYER]`；Task 3 `player_t`/`World`/`w.player.mo`。
- Produces: `P_CheckPosition(m,bm,const mobj_t&,x,y)` / `P_TryMove(m,bm,mobj_t&,nx,ny)` / `P_TrySlide(m,bm,mobj_t&,dx,dy)` / `P_CalcHeight(m,player_t&)` / `P_MovePlayer(m,bm,player_t&,f,s,turn)`。删除 `struct Player`。

- [ ] **Step 1: 先改写 `tests/test_p_map.cpp`（失败测试）**

用 `mobj_t`（`info=&mobjinfo[MT_PLAYER]`）替换所有 `Player`；碰撞函数加 `mobj_t&`/`player_t&`。完整替换文件：

```cpp
#include "doctest.h"
#include "play/p_map.h"
#include "play/p_maputl.h"
#include "play/p_setup.h"
#include "play/p_mobj.h"
#include "play/p_tick.h"
#include "play/info.h"
#include "core/m_tables.h"
#include <cstdlib>

namespace {
struct TestMap { MapData m; Blockmap bm; };
static fixed_t F(int v) { return static_cast<fixed_t>(v) << FRACBITS; }
static TestMap buildTestMap(bool oneSided, int backFloor) {
    TestMap tm; auto& m = tm.m;
    m.vertices.push_back({270 << 16, 128 << 16});
    m.vertices.push_back({270 << 16, 384 << 16});
    line_t L; L.v1=0; L.v2=1; L.flags=ML_TWOSIDED; L.special=0; L.tag=0;
    L.sidenum[0]=0; L.sidenum[1]= oneSided?-1:1; m.lines.push_back(L);
    side_t s0; s0.sector=0; m.sides.push_back(s0);
    if (!oneSided) { side_t s1; s1.sector=1; m.sides.push_back(s1); }
    sector_t A; A.floorheight=0;        A.ceilingheight=128; m.sectors.push_back(A);
    sector_t B; B.floorheight=backFloor;B.ceilingheight=128; m.sectors.push_back(B);
    seg_t g; g.linedef=0; g.side=0; g.frontsector=0; g.backsector= oneSided?-1:1; m.segs.push_back(g);
    subsector_t ss; ss.segcount=1; ss.firstseg=0; m.subsectors.push_back(ss);
    Blockmap& bm = tm.bm; bm.orgx=0; bm.orgy=0; bm.width=4; bm.height=4;
    bm.lump = {0,0,4,4, 20,20,20,20, 20,20,21,20, 20,20,21,20, 20,20,20,20, -1, 0,-1};
    return tm;
}
static mobj_t playerMobj(fixed_t x, fixed_t y) {
    mobj_t mo; mo.info = &mobjinfo[MT_PLAYER]; mo.x=x; mo.y=y; mo.floorz=0;
    return mo;
}
}

TEST_CASE("P_CheckPosition: one-sided wall blocks") {
    auto tm = buildTestMap(true,0); mobj_t mo = playerMobj(F(256),F(256));
    CHECK(P_CheckPosition(tm.m, tm.bm, mo, F(256),F(256)).ok == false);
}
TEST_CASE("P_CheckPosition: open space clear, seeds floor/ceiling") {
    auto tm = buildTestMap(true,0); mobj_t mo = playerMobj(F(64),F(64));
    PosCheck pc = P_CheckPosition(tm.m, tm.bm, mo, F(64),F(64));
    CHECK(pc.ok == true); CHECK(pc.floorz == 0); CHECK(pc.ceilingz == 128<<FRACBITS);
}
TEST_CASE("P_CheckPosition: two-sided portal raises floor, keeps ok") {
    auto tm = buildTestMap(false,8); mobj_t mo = playerMobj(F(256),F(256));
    PosCheck pc = P_CheckPosition(tm.m, tm.bm, mo, F(256),F(256));
    CHECK(pc.ok == true); CHECK(pc.floorz == 8<<FRACBITS); CHECK(pc.dropoffz == 0);
}
TEST_CASE("P_TryMove: passable step commits and raises floorz") {
    auto tm = buildTestMap(false,8); mobj_t mo = playerMobj(F(100),F(100));
    CHECK(P_TryMove(tm.m, tm.bm, mo, F(256),F(256)) == true);
    CHECK(mo.x == F(256)); CHECK(mo.y == F(256)); CHECK(mo.floorz == 8<<FRACBITS);
}
TEST_CASE("P_TryMove: step too high rejected") {
    auto tm = buildTestMap(false,32); mobj_t mo = playerMobj(F(100),F(100));
    CHECK(P_TryMove(tm.m, tm.bm, mo, F(256),F(256)) == false);
    CHECK(mo.x == F(100)); CHECK(mo.y == F(100));
}
TEST_CASE("P_TryMove: dropoff allowed (MF_DROPOFF)") {
    auto tm = buildTestMap(false,-32); mobj_t mo = playerMobj(F(100),F(100));
    CHECK(P_TryMove(tm.m, tm.bm, mo, F(256),F(256)) == true);
    CHECK(mo.x == F(256)); CHECK(mo.floorz == 0);
}
TEST_CASE("P_TryMove: one-sided wall rejected; open accepted") {
    auto tm = buildTestMap(true,0); mobj_t mo = playerMobj(F(100),F(100));
    CHECK(P_TryMove(tm.m, tm.bm, mo, F(256),F(256)) == false);
    CHECK(P_TryMove(tm.m, tm.bm, mo, F(64),F(64)) == true);
    CHECK(mo.x == F(64));
}
TEST_CASE("P_TrySlide: fully blocked -> no move") {
    auto tm = buildTestMap(true,0); mobj_t mo = playerMobj(F(250),F(200));
    CHECK(P_TrySlide(tm.m, tm.bm, mo, F(30),0) == false);
    CHECK(mo.x == F(250)); CHECK(mo.y == F(200));
}
TEST_CASE("P_TrySlide: diagonal slides along perpendicular axis") {
    auto tm = buildTestMap(true,0); mobj_t mo = playerMobj(F(250),F(200));
    CHECK(P_TrySlide(tm.m, tm.bm, mo, F(30),F(40)) == true);
    CHECK(mo.x == F(250)); CHECK(mo.y == F(240));
}
TEST_CASE("P_CalcHeight: viewz clamped under low ceiling") {
    MapData m; sector_t s; s.floorheight=0; s.ceilingheight=40; m.sectors.push_back(s);
    mobj_t mo = playerMobj(0,0); mo.subsector=0;
    player_t pl; pl.mo=&mo; pl.viewz=0;
    for (int i=0;i<40;++i) P_CalcHeight(m, pl);
    CHECK(pl.viewz == 36<<FRACBITS);
}
TEST_CASE("P_CalcHeight: viewz converges to floorz+VIEWHEIGHT") {
    MapData m; sector_t s; s.floorheight=100; s.ceilingheight=1000; m.sectors.push_back(s);
    mobj_t mo = playerMobj(0,0); mo.subsector=0; mo.floorz=100<<FRACBITS;
    player_t pl; pl.mo=&mo; pl.viewz=100<<FRACBITS;
    for (int i=0;i<40;++i) P_CalcHeight(m, pl);
    CHECK(std::abs(pl.viewz - (141<<FRACBITS)) <= (1<<FRACBITS));
}
TEST_CASE("P_MovePlayer keeps raised floorz straddling a step") {
    R_InitTables();
    auto tm = buildTestMap(false,8);
    mobj_t mo = playerMobj(F(264),F(256)); mo.angle=0; mo.subsector=0;
    player_t pl; pl.mo=&mo; pl.viewz=VIEWHEIGHT;
    P_MovePlayer(tm.m, tm.bm, pl, 0,0,0);
    CHECK(mo.floorz == 8<<FRACBITS);
}
TEST_CASE("P_MovePlayer: forward in open space moves +X at angle 0") {
    R_InitTables();
    auto tm = buildTestMap(true,0);
    mobj_t mo = playerMobj(F(64),F(64)); mo.angle=0; mo.subsector=0;
    player_t pl; pl.mo=&mo; pl.viewz=VIEWHEIGHT;
    P_MovePlayer(tm.m, tm.bm, pl, F(5),0,0);
    CHECK(mo.x > F(64)); CHECK(mo.y == F(64));
    CHECK(std::abs(pl.viewz - (F(0)+VIEWHEIGHT)) <= (1<<FRACBITS));
}
TEST_CASE("P_MovePlayer: walking into a wall does not cross it") {
    R_InitTables();
    auto tm = buildTestMap(true,0);
    mobj_t mo = playerMobj(F(250),F(200)); mo.angle=ANG90; mo.subsector=0;
    player_t pl; pl.mo=&mo; pl.viewz=VIEWHEIGHT;
    P_MovePlayer(tm.m, tm.bm, pl, F(30),0,0);
    CHECK(mo.y > F(200));
    mo.angle = 0; fixed_t before = mo.x;
    P_MovePlayer(tm.m, tm.bm, pl, F(30),0,0);
    CHECK(mo.x < F(270)); CHECK(mo.x >= before);
}
```

- [ ] **Step 2: 运行测试，确认编译失败**

Run: `cmake --build build --config Release`
Expected: 编译失败（`Player` 未定义 / 旧签名不匹配）。

- [ ] **Step 3: 改 `src/play/p_map.h`**

删除 `struct Player {...};`。把 `#include "play/p_setup.h"` 之后增加 `#include "play/p_mobj.h"` 与 `#include "play/p_tick.h"`（取 `mobj_t`/`player_t`）。函数声明改为：

```cpp
PosCheck P_CheckPosition(const MapData& m, const Blockmap& bm, const mobj_t& mo, fixed_t x, fixed_t y);
bool     P_TryMove(const MapData& m, const Blockmap& bm, mobj_t& mo, fixed_t nx, fixed_t ny);
bool     P_TrySlide(const MapData& m, const Blockmap& bm, mobj_t& mo, fixed_t dx, fixed_t dy);
void     P_CalcHeight(const MapData& m, player_t& p);
void     P_MovePlayer(const MapData& m, const Blockmap& bm, player_t& p, fixed_t forward, fixed_t strafe, angle_t turn);
```

> `PLAYERRADIUS`/`PLAYERHEIGHT` 常量保留定义（现为冗余，mobj 走 info；不删除以免动无关代码），注释标「P5a 后由 mobjinfo[MT_PLAYER] 提供」。

- [ ] **Step 4: 改 `src/play/p_map.cpp`**

`P_CheckPosition`：形参加 `const mobj_t& mo`；bbox 用 `mo.info->radius`：
```cpp
fixed_t r = mo.info ? mo.info->radius : PLAYERRADIUS;
BBox bb{ x - r, x + r, y - r, y + r };
```
（其余 blockmap/line 逻辑不变。）

`P_TryMove`：形参 `mobj_t& mo`；高度判据用 `mo.info->height`，位移写 `mo`：
```cpp
PosCheck P_TryMove(const MapData& m, const Blockmap& bm, mobj_t& mo, fixed_t nx, fixed_t ny) {
    PosCheck pc = P_CheckPosition(m, bm, mo, nx, ny);
    if (!pc.ok) return false;
    fixed_t h = mo.info ? mo.info->height : PLAYERHEIGHT;
    if (pc.ceilingz - pc.floorz < h) return false;
    if (pc.ceilingz - mo.floorz   < h) return false;
    if (pc.floorz   - mo.floorz   > STEP_LIMIT) return false;
    mo.x = nx; mo.y = ny; mo.floorz = pc.floorz; mo.ceilingz = pc.ceilingz;
    return true;
}
```

`P_TrySlide`：形参 `mobj_t& mo`，内部三处 `P_TryMove(..., mo, ...)`，逻辑不变。

`P_CalcHeight(const MapData& m, player_t& p)`：
```cpp
void P_CalcHeight(const MapData& m, player_t& p) {
    if (!p.mo) return;
    fixed_t target = p.mo->floorz + VIEWHEIGHT;
    p.viewz += (target - p.viewz) >> 3;
    int sec = (p.mo->subsector >= 0) ? sectorOf(m, p.mo->subsector) : -1;
    fixed_t cap = (sec >= 0 ? (static_cast<fixed_t>(m.sectors[sec].ceilingheight) << FRACBITS)
                            : (static_cast<fixed_t>(1 << 30)) ) - (4 << FRACBITS);
    if (p.viewz > cap) p.viewz = cap;
}
```

`P_MovePlayer(const MapData& m, const Blockmap& bm, player_t& p, fixed_t forward, fixed_t strafe, angle_t turn)`：操作 `p.mo`：
```cpp
void P_MovePlayer(const MapData& m, const Blockmap& bm, player_t& p,
                  fixed_t forward, fixed_t strafe, angle_t turn) {
    mobj_t* mo = p.mo;
    mo->angle += turn;
    int fi = (mo->angle >> ANGLETOFINESHIFT) & FINEMASK;
    fixed_t fc = finecosine[fi], fs = finesine[fi];
    fixed_t dx = FixedMul(forward, fc) + FixedMul(strafe, fs);
    fixed_t dy = FixedMul(forward, fs) - FixedMul(strafe, fc);
    fixed_t mag = P_AproxDistance(dx, dy);
    if (mag > MAXMOVE) { fixed_t s = FixedDiv(MAXMOVE, mag); dx = FixedMul(dx, s); dy = FixedMul(dy, s); }
    P_TrySlide(m, bm, *mo, dx, dy);
    mo->subsector = R_PointInSubsector(m, mo->x, mo->y);
    P_CalcHeight(m, p);
}
```

- [ ] **Step 5: 改 `src/main.cpp`** —— 删除 `Player p`，移动/相机改用 `w.player`。

删除 `Player p; {...playerStart...}` 块。P_SetupLevel 已生成 `w.player.mo` 并置 `viewz`。主循环移动调用改为：
```cpp
P_MovePlayer(map, map.blockmap, w.player, fwd * moveSpeed, str * moveSpeed, trn * turnSpeed);
```
渲染相机参数改为：
```cpp
R_RenderView(fb.data(), FB_W, FB_H, map, tex,
             static_cast<float>(w.player.mo->x >> FRACBITS),
             static_cast<float>(w.player.mo->y >> FRACBITS),
             bamToRendererRad(w.player.mo->angle),
             static_cast<float>(w.player.viewz >> FRACBITS),
             ticCount, &sprites, &w.thingsBySubsec);
```
（`--dumpframe` 分支的相机 `px/py/ang/eyeZ0` 仍用 `playerStart` 直接取，不受影响——它不依赖 Player。）

- [ ] **Step 6: 构建 + 全量测试**

Run: `cmake --build build --config Release && ctest --test-dir build --build-config Release --output-on-failure`
Expected: 全绿；`test_p_map` 14 个 case 通过（碰撞回归守卫）。

- [ ] **Step 7: 运行时回归核对（memory 盲点，必跑）**

Run: `./build/Release/doomcpp.exe`
Expected: E1M1 窗口出现；**WASD 实机移动正常、方向键转向正常、不穿墙**（与 P3.5 手感一致）。再跑 `--dumpframe`（Task 2 命令）确认图景==P4。若 WASD 失效 → 回到 Step 4 查 `P_MovePlayer`/`info->radius`。

- [ ] **Step 8: 提交**

```bash
git add src/play/p_map.h src/play/p_map.cpp src/main.cpp tests/test_p_map.cpp
git commit -m "refactor(p_map): generalize collision to mobj_t; player as player_t (drop Player)"
```

---

### Task 6: P_MobjThinker 物理（动量/摩擦/重力/状态推进）+ P_RunThinkers + 接主循环

**目标**：实现 `P_MobjThinker`（`P_XYMovement` 分步碰撞+摩擦、`P_ZMovement` 重力+落地/撞顶、状态推进），实现 `P_RunThinkers`（替换 Task 3 的空桩），并在 `main.cpp` 主循环每个 tic 调 `P_RunThinkers(w)`。静态 mobj（mom=0）保持静止（验证不回归）；带 mom 的 mobj 物理可见。

**Files:**
- Modify: `src/play/p_mobj.h`（加 `P_MobjThinker` 声明）, `src/play/p_mobj.cpp`（实现物理 + 替换 `P_RunThinkers` 桩在 p_tick.cpp），`src/play/p_tick.cpp`（删空桩、改为调 p_mobj 的实现 OR 保留在 p_tick），`src/main.cpp`（主循环加 `P_RunThinkers`）, `tests/test_p_mobj.cpp`（加物理用例）

**Interfaces:**
- Consumes: Task 5 泛化碰撞 `P_TryMove(m,bm,mobj_t&,nx,ny)`；Task 3 `P_SetMobjState`。
- Produces: `void P_MobjThinker(const World& w, mobj_t& mo);`（p_mobj.h）；`P_RunThinkers` 真实实现。

> 实现者决定 `P_RunThinkers` 定义放 p_tick.cpp 还是 p_mobj.cpp；二处都 include 对方头。本计划放 **p_tick.cpp**（属 thinker 调度），`P_MobjThinker` 放 **p_mobj.cpp**（属 mobj 行为）。

- [ ] **Step 1: 写失败测试（追加到 `tests/test_p_mobj.cpp`）**

```cpp
#include "play/p_setup.h"
#include "play/p_maputl.h"
#include "play/p_map.h"
#include <cmath>

namespace {
fixed_t PF(int v){ return static_cast<fixed_t>(v)<<FRACBITS; }
// 单开放 sector（floor 0 ceil 128），单 subsector、无 node。
MapData openMap() {
    MapData m;
    sector_t s; s.floorheight=0; s.ceilingheight=128; s.lightlevel=160; m.sectors.push_back(s);
    subsector_t ss; ss.segcount=0; ss.firstseg=0; m.subsectors.push_back(ss);
    return m;
}
}

TEST_CASE("P_ZMovement: upward momz arcs up then falls back to floorz, momz->0") {
    MapData m = openMap();
    World w; w.map=&m; w.bm=&m.blockmap;
    mobj_t* b = P_SpawnMobj(w, PF(100), PF(100), 0, MT_BARREL);
    b->floorz = 0; b->ceilingz = 128<<FRACBITS;
    b->momz = 8<<FRACBITS;
    fixed_t startZ = b->z, maxZ = b->z;
    for (int i=0;i<60;++i) { P_RunThinkers(w); if (b->z > maxZ) maxZ = b->z; }
    CHECK(maxZ > startZ);                       // 上升过
    CHECK(b->z   == b->floorz);                 // 最终落地
    CHECK(b->momz == 0);
}

TEST_CASE("P_XYMovement: horizontal mom slides then friction stops it") {
    MapData m = openMap();
    World w; w.map=&m; w.bm=&m.blockmap;
    mobj_t* b = P_SpawnMobj(w, PF(64), PF(64), 0, MT_BARREL);
    b->floorz = 0; b->ceilingz = 128<<FRACBITS;
    b->momx = 12<<FRACBITS;
    for (int i=0;i<80;++i) P_RunThinkers(w);
    CHECK(b->x > PF(64));                        // 前进了
    CHECK(b->y == PF(64));
    CHECK(b->momx == 0);                         // 摩擦停了
}

TEST_CASE("P_XYMovement: moving into a wall stops, does not cross") {
    // 复用 test_p_map 的墙图：墙在 x=270。barrel 在 (250,200) 向 +X。
    MapData m; Blockmap bm;
    m.vertices.push_back({270<<16,128<<16}); m.vertices.push_back({270<<16,384<<16});
    line_t L; L.v1=0;L.v2=1;L.flags=ML_TWOSIDED;L.special=0;L.tag=0;L.sidenum[0]=0;L.sidenum[1]=-1; m.lines.push_back(L);
    side_t s0; s0.sector=0; m.sides.push_back(s0);
    sector_t A; A.floorheight=0; A.ceilingheight=128; m.sectors.push_back(A);
    seg_t g; g.linedef=0;g.side=0;g.frontsector=0;g.backsector=-1; m.segs.push_back(g);
    subsector_t ss; ss.segcount=1; ss.firstseg=0; m.subsectors.push_back(ss);
    bm.orgx=0;bm.orgy=0;bm.width=4;bm.height=4;
    bm.lump={0,0,4,4, 20,20,20,20, 20,20,21,20, 20,20,21,20, 20,20,20,20, -1,0,-1};
    World w; w.map=&m; w.bm=&bm;
    mobj_t* b = P_SpawnMobj(w, PF(250), PF(200), 0, MT_BARREL);
    b->floorz=0; b->ceilingz=128<<FRACBITS;
    b->momx = 30<<FRACBITS;
    for (int i=0;i<20;++i) P_RunThinkers(w);
    CHECK(b->x < PF(270));                       // 未穿墙
    CHECK(b->x >= PF(250));                      // 朝墙移动了但被挡（不越过）
    // 注：不断言 momx==0——摩擦 0xe800(~0.906/tic) 在 20 tic 内不会把 momx=30 衰减到
    // STOPSPEED 以下；墙碰撞的正确判据是位置不越线（vanilla 同样靠摩擦逐 tic 衰减）。
}

TEST_CASE("static mobj (mom=0) stays put after many thinkers") {
    MapData m = openMap();
    World w; w.map=&m; w.bm=&m.blockmap;
    mobj_t* b = P_SpawnMobj(w, PF(100), PF(100), 0, MT_D2013);  // SOUL: NOGRAVITY
    b->floorz=0; b->ceilingz=128<<FRACBITS;
    fixed_t x0=b->x, y0=b->y, z0=b->z;
    for (int i=0;i<50;++i) P_RunThinkers(w);
    CHECK(b->x==x0); CHECK(b->y==y0); CHECK(b->z==z0);
}
```

- [ ] **Step 2: 运行测试，确认失败（P_RunThinkers 是空桩）**

Run: `cmake --build build --config Release && ctest --test-dir build --build-config Release -R test_p_mobj --output-on-failure`
Expected: 物理 3 个 case FAIL（mobj 不动 / 不落地）。

- [ ] **Step 3: `src/play/p_mobj.h` 加声明**

```cpp
struct World;   // fwd
void P_MobjThinker(const World& w, mobj_t& mo);
```

- [ ] **Step 4: `src/play/p_mobj.cpp` 实现物理**

顶部 include 补 `#include "p_tick.h"`、`#include "p_map.h"`（`P_TryMove`/`VIEWHEIGHT`）、`#include "core/m_fixed.h"`、`#include "core/m_tables.h"`。加常量与辅助：

```cpp
namespace {
constexpr fixed_t STOPSPEED = 0x1000;     // vanilla p_mobj.c
constexpr fixed_t FRICTION  = 0xe800;     // vanilla ORIG_FRICTION
constexpr fixed_t GRAVITY   = FRACUNIT;   // P5a 采用值（1 map unit/tic²）；vanilla 确切值实现期对照 p_mobj.c 校准——物理测试为定性，常量可调

fixed_t apx(fixed_t dx, fixed_t dy) { dx=std::abs(dx); dy=std::abs(dy);   // P_AproxDistance 的本地副本（p_map.cpp 中是 static 未导出）
    if (dx<dy) return dx+dy-(dx>>1); return dx+dy-(dy>>1); }
}

static void P_XYMovement(const World& w, mobj_t& mo) {
    fixed_t xleft = mo.momx, yleft = mo.momy;
    while (xleft != 0 || yleft != 0) {
        fixed_t xl = xleft, yl = yleft;
        fixed_t mag = apx(xl, yl);
        if (mag > MAXMOVE) { fixed_t s = FixedDiv(MAXMOVE, mag); xl = FixedMul(xl, s); yl = FixedMul(yl, s); }
        if (!P_TryMove(*w.map, *w.bm, mo, mo.x + xl, mo.y + yl)) break;   // 撞墙：停
        xleft -= xl; yleft -= yl;
        if (xl == 0 && yl == 0) break;
    }
    if (apx(mo.momx, mo.momy) < STOPSPEED) { mo.momx = 0; mo.momy = 0; }
    else { mo.momx = FixedMul(mo.momx, FRICTION); mo.momy = FixedMul(mo.momy, FRICTION); }
}

static void P_ZMovement(const World& w, mobj_t& mo) {
    (void)w;
    if (!(mo.flags & MF_NOGRAVITY)) mo.momz -= GRAVITY;
    mo.z += mo.momz;
    if (mo.z <= mo.floorz) { if (mo.momz < 0) mo.momz = 0; mo.z = mo.floorz; }   // 落地
    fixed_t cz = mo.ceilingz - (mo.info ? mo.info->height : 0);
    if (mo.z >= cz) { mo.z = cz; if (mo.momz > 0) mo.momz = 0; }                 // 撞顶
}

void P_MobjThinker(const World& w, mobj_t& mo) {
    if (mo.momx || mo.momy) P_XYMovement(w, mo);
    if (mo.momz || !(mo.flags & MF_NOGRAVITY)) P_ZMovement(w, mo);
    if (mo.state && mo.state->tics != -1 && mo.tics > 0) {
        if (--mo.tics == 0) P_SetMobjState(mo, mo.state->nextstate);
    }
}
```

- [ ] **Step 5: `src/play/p_tick.cpp` 把 `P_RunThinkers` 空桩改为真实实现**

```cpp
void P_RunThinkers(World& w) {
    ++w.tic;
    for (mobj_t& mo : w.mobjs) P_MobjThinker(w, mo);
}
```
（p_tick.cpp 顶部 include `p_mobj.h` 以取 `P_MobjThinker`。）

- [ ] **Step 6: `src/main.cpp` 主循环每个 tic 调 `P_RunThinkers`**

在 tic 批处理循环里 `P_MovePlayer(...)` 之后加：
```cpp
P_MovePlayer(map, map.blockmap, w.player, fwd * moveSpeed, str * moveSpeed, trn * turnSpeed);
P_RunThinkers(w);
++ticCount;
```

- [ ] **Step 7: 构建 + 全量测试**

Run: `cmake --build build --config Release && ctest --test-dir build --build-config Release --output-on-failure`
Expected: 全绿（含物理 4 个新 case）。

- [ ] **Step 8: 渲染/移动零回归核对**

Run: `--dumpframe`（Task 2 命令）+ 实机 WASD。
Expected: 静态场景==P4（静态 mobj 未动）；WASD 正常。

- [ ] **Step 9: 提交**

```bash
git add src/play/p_mobj.h src/play/p_mobj.cpp src/play/p_tick.cpp src/main.cpp tests/test_p_mobj.cpp
git commit -m "feat(p_mobj): P_MobjThinker physics (momentum/friction/gravity) + tick loop"
```

---

### Task 7: 可见里程碑（抛桶按键）+ `--tickdump` 物理可视化

**目标**：`i_video` 加边沿触发 `spawn`；`main.cpp` 按 toss 键在玩家正前方生成 `MT_BARREL` 并赋 forward+上抛 mom，肉眼可见抛物落地+滑动+停。加 `--tickdump <n>`：载入 → 在固定点生成带初速桶 → 跑 n 个 thinker tic → dump 帧（并打印 z/momz 前后值）。**P5a 收官里程碑。**

**Files:**
- Modify: `src/platform/i_video.h`, `src/platform/i_video.cpp`, `src/main.cpp`

**Interfaces:**
- Consumes: Task 6 `P_RunThinkers`；Task 3 `P_SpawnMobj`。
- Produces: `Input::spawn`（边沿）；`--tickdump <wad> <out.bmp> <n> [x y vx vy vz]`。

- [ ] **Step 1: `i_video.h` 加字段**

`struct Input` 内加 `bool spawn=false;`。

- [ ] **Step 2: `i_video.cpp` 边沿触发**

`pollEvents(Input& input)` 的事件循环里，KEYDOWN 分支加（toss 键用 `G`，`ev.key.repeat==0` 保证按一次只触发一次）：
```cpp
if (ev.type == SDL_KEYDOWN) {
    if (ev.key.keysym.sym == SDLK_ESCAPE) return false;
    if (ev.key.keysym.scancode == SDL_SCANCODE_G && ev.key.repeat == 0) input.spawn = true;
}
```

- [ ] **Step 3: `main.cpp` 实时模式处理 toss**

在主循环 `pollEvents(in)` 之后、tic 批处理之前（每帧至多一次）：
```cpp
if (in.spawn) {
    in.spawn = false;
    mobj_t* mo = w.player.mo;
    int fi = (mo->angle >> ANGLETOFINESHIFT) & FINEMASK;
    fixed_t fc = finecosine[fi], fs = finesine[fi];
    fixed_t dist = 48 << FRACBITS;
    mobj_t* b = P_SpawnMobj(w, mo->x + FixedMul(fc, dist), mo->y + FixedMul(fs, dist),
                            mo->floorz, MT_BARREL);
    b->floorz = mo->floorz; b->ceilingz = mo->ceilingz;
    b->momx = FixedMul(fc, 10<<FRACBITS);
    b->momy = FixedMul(fs, 10<<FRACBITS);
    b->momz = 9 << FRACBITS;           // 上抛
}
```
顶部 include 补 `core/m_tables.h`（finecos/finesine）、`core/m_fixed.h`（FixedMul）。

- [ ] **Step 4: `main.cpp` 加 `--tickdump`**

在参数解析循环里（与 `--dumpframe` 并列）加：
```cpp
if (std::string(argv[i]) == "--tickdump" && i + 3 < argc) {
    WadFile wad(argv[i+1]); MapData map = loadMap(wad, "E1M1");
    TextureLookup tex(wad); SpriteCache sprites(wad);
    World w; w.map=&map; w.bm=&map.blockmap; P_SetupLevel(w, map);
    // 在固定点生成带初速桶（默认或命令行覆盖）
    fixed_t bx = (i+5<argc)? static_cast<fixed_t>(std::atof(argv[i+4])*(1<<FRACBITS)) : 1056<<FRACBITS;
    fixed_t by = (i+6<argc)? static_cast<fixed_t>(std::atof(argv[i+5])*(1<<FRACBITS)) : -2144<<FRACBITS;
    mobj_t* b = P_SpawnMobj(w, bx, by, 0, MT_BARREL);
    int sec0 = sectorOf(map, b->subsector);
    b->floorz = (sec0>=0)? (fixed_t)map.sectors[sec0].floorheight<<FRACBITS : 0;
    b->ceilingz = (sec0>=0)? (fixed_t)map.sectors[sec0].ceilingheight<<FRACBITS : (128<<FRACBITS);
    b->z = b->floorz;
    b->momx = (i+7<argc)? static_cast<fixed_t>(std::atof(argv[i+6])*(1<<FRACBITS)) : (10<<FRACBITS);
    b->momy = 0;
    b->momz = (i+8<argc)? static_cast<fixed_t>(std::atof(argv[i+7])*(1<<FRACBITS)) : (9<<FRACBITS);
    int n = std::atoi(argv[i+3]);
    std::cout << "before: z=" << (b->z>>FRACBITS) << " momz=" << (b->momz>>FRACBITS)
              << " x=" << (b->x>>FRACBITS) << " momx=" << (b->momx>>FRACBITS) << "\n";
    for (int t=0;t<n;++t) P_RunThinkers(w);
    std::cout << "after(" << n << " tics): z=" << (b->z>>FRACBITS)
              << " momz=" << (b->momz>>FRACBITS) << " x=" << (b->x>>FRACBITS) << "\n";
    // dump 玩家起点视角的帧
    fixed_t px,py; angle_t ang; playerStart(map,px,py,ang);
    P_BuildThingsBySubsec(w);
    constexpr int FW=320,FH=200; std::vector<std::uint32_t> fb((size_t)FW*FH,0);
    R_RenderView(fb.data(),FW,FH,map,tex,(float)(px>>FRACBITS),(float)(py>>FRACBITS),
                 bamToRendererRad(ang),(float)((w.player.viewz)>>FRACBITS),w.tic,&sprites,&w.thingsBySubsec);
    writeBMP(argv[i+2], fb.data(), FW, FH);
    std::cout << "wrote " << argv[i+2] << "\n";
    return 0;
}
```

- [ ] **Step 5: 构建**

Run: `cmake --build build --config Release`
Expected: 成功。

- [ ] **Step 6: 物理可视化核对**

Run: `./build/Release/doomcpp.exe --tickdump assets/freedoom1.wad tick.bmp 60`
Expected: stdout 显示 `before z=0 momz=9 x=1056 momx=10` → `after(60 tics): z=<floor> momz=0 x=<moved>`（z 落回 floor、momz 归零、x 前进）。若 z 未归零/momz 未归零 → 回 Task 6 查 `P_ZMovement`。

- [ ] **Step 7: 实机里程碑核对（人肉）**

Run: `./build/Release/doomcpp.exe`
Expected: E1M1 中**按 G 键** → 玩家正前方出现桶，抛物上升、落地、向前滑动、摩擦停下、被墙挡住不穿墙。WASD 仍正常。此为 P5a 可见里程碑达成。

- [ ] **Step 8: README + 提交收官**

更新 `README.md`：Roadmap 增 `[x] P5a 思考者/mobj 系统（活对象 + 物理 + 抛桶里程碑）`；Status 段补一句 P5a 说明（thinker 链 + `P_MobjThinker` 物理 + 玩家并入 mobj + 渲染器读 live state）。

```bash
git add src/platform/i_video.h src/platform/i_video.cpp src/main.cpp README.md
git commit -m "feat(main): toss-barrel milestone + --tickdump; README P5a milestone"
```

---

## Self-Review（spec 覆盖核对）

- §1 范围：Task 1–7 覆盖活 mobj/info/thinker/物理/泛化碰撞/P_SetupLevel/玩家并入/渲染适配/抛桶里程碑。✓
- §2 定点 + 复刻非翻译：Global Constraints + Task 6 常量注释。✓
- §3 模块表：每文件在对应 Task 落地。✓
- §4 mobj_t：Task 2（结构迁移）+ Task 3（生命周期/`P_SetMobjState` 刷缓存）。✓
- §5 info 表：Task 1（含 P4 映射复现测试）。✓
- §6 碰撞泛化 + 玩家路由：Task 5（+ `test_p_map` 回归守卫）。✓
- §7 P_MobjThinker 物理：Task 6（含落地/撞墙/静止定性测试）。✓
- §8 关卡生成：Task 3 `P_SetupLevel`（+ count/types 测试）。✓
- §9 渲染适配：Task 4（签名收口 + 每帧 `P_BuildThingsBySubsec`，跳 player）。✓
- §10 偏差清单：玩家直接位移（Task 5 保留）、`std::list`（Task 3）、mobj↔墙 only（Task 6）、无 AI/伤害（范围外）。✓
- §11 测试三道关 + 盲点：单测（Task 1/3/5/6）、运行时 WASD smoke（Task 5 Step 7 必跑）、`--tickdump`（Task 7）。✓

**类型一致性核对**：`P_CheckPosition/P_TryMove/P_TrySlide` 在 Task 5 定义为 `(m,bm,mobj_t&,…)`、Task 6 `P_XYMovement` 调用处匹配；`P_MobjThinker(const World&, mobj_t&)` 声明（Task 6 p_mobj.h）与 `P_RunThinkers`（Task 6 p_tick.cpp）一致；`thingsBySubsec` 在 Task 4 改 `const mobj_t*`，Task 3 `P_BuildThingsBySubsec` 产出该类型、`renderSubsector`（Task 4）消费该类型。`frame` 缓存 'A'-based（Task 1/3 与 `SpriteCache.resolve` 约定一致）。

