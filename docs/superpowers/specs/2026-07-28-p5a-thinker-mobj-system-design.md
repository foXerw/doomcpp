# doomcpp P5a thinker/游戏 tick 系统（p_mobj + p_tick + info）— 设计文档

- **日期**: 2026-07-28
- **状态**: 已通过头脑风暴,待写入实现计划
- **阶段**: `v0.11` · P5a 游戏对象系统打底
- **关联**: 承接 P4 精灵（`2026-07-27-p4-sprites-design.md`）；把 P4 的**渲染侧静态 mobj** 升级为 vanilla 的**活 mobj + thinker 系统**，为 P5b（战斗/伤害/武器/HUD）与 P6（敌人 AI/机关）提供共同地基。
- **决策出处**: 头风暴确认两点——(1) 本期深度＝「机制＋物理＋可见验证」；(2) 玩家**现在并入** mobj（vanilla 忠实路线，泛化碰撞，保留直接位移移动以最小化回归）。

## 1. 目的与范围

P4 的精灵是 `main.cpp` 从 THINGS **直接构造的静态 `std::vector<mobj_t>`**，从不 tick、从不移动，`mobj_t` 是纯渲染结构（无 health/momentum/state/flags/info）。P5a 把它变成 vanilla 的活对象：每个 mobj 持 `info`/`state`/`flags`/`momentum`，由 **thinker 链每 tic 更新**；玩家也成为 mobj；碰撞从「玩家专属」泛化为「按 info 的 radius/height/flags」。

**P5a 里程碑**：E1M1 里所有东西作为活 mobj 存在（渲染图景与 P4 一致，但 sprname/frame 现从 `state` 派生）；按一个键朝玩家面前抛一个带初速度的 mobj（桶）→ **肉眼可见它抛物落地、滑动、摩擦停**，证明物理端到端。

- **范围内**: 活 `mobj_t`（info/state/flags/health/momentum/floorz 等）+ `info.h` 表（`mobjinfo[]`/`state_t[]`/`sprnames[]`/doomednum→type）+ thinker 链（`World` + `P_RunThinkers`）+ `P_MobjThinker`（P_XYMovement 摩擦/碰撞 + P_ZMovement 重力/落地 + 状态推进）+ 泛化碰撞（`P_CheckPosition`/`P_TryMove`/`P_TrySlide` 走 mobj）+ `P_SetupLevel`/`P_SpawnMapThing` 生成活 mobj + 玩家并入 mobj（`player_t.mo`）+ 渲染适配（每帧 `thingsBySubsec` 重建 + 签名收口）+ 抛掷可见里程碑 + 单测/运行时 smoke/物理 dump。
- **范围外（显式延后）**: 动作函数与敌人 AI（`A_Look`/`A_Chase`/视线，P6）；伤害结算/武器/HUD（P5b）；需 action 的状态转换；**玩家动量化移动**（P5a 保留直接位移，见 §10）；projectile 类 mobjinfo（P5b）；**mobj↔mobj 碰撞**（P5a 只 mobj↔墙）；blockmap thing-thing（P6）；存档/demo。

## 2. 保真标尺（沿用）

- **game logic（`p_*`）走定点**，渲染器（`r_*`）走 float（P3.5 永久偏差）。mobj 物理与碰撞属 game logic → **16.16 定点**，沿用 `m_fixed`/`m_tables`（`finecos`/`finesine`）。
- **复刻,不是翻译**：读 vanilla `p_mobj.c`/`p_tick.c`/`info.c`/`p_map.c` 理解算法与数据结构，用现代 C++ 重写；物理常量（`GRAVITY`/`STOPSPEED`/`FRICTION`/`MAXMOVE`）逐值照搬以保证手感；验收靠 doomcpp 自身数值自洽 + 实机视觉（roadmap §5），不装逐像素 oracle。
- **YAGNI**：thinker 系统本期只需 mobj 一种；用 `std::list<mobj_t>` 取代 vanilla 的侵入式双向链表（语义等价、C++ 更干净），为 P6 加非 mobj thinker（T_MoveFloor 等）预留「按类型分派」的扩展点但不提前实现。

## 3. 模块与职责

| 文件 | 动作 | 职责 |
|---|---|---|
| `play/p_mobj.{h,cpp}` | **新** | 活 `mobj_t`、`P_SpawnMobj`/`P_RemoveMobj`/`P_SetMobjState`、`P_MobjThinker`（P_XYMovement/P_ZMovement/状态推进）、`P_SetThingPosition`（更新 subsector）。 |
| `play/p_tick.{h,cpp}` | **新** | `World`（持有 thinker 链 + `player_t` + map 引用 + 帧/`thingsBySubsec`）、`P_RunThinkers`、`P_BuildThingsBySubsec`。 |
| `play/info.{h,cpp}` | **新** | `mobjtype_t`/`statenum_t`/`spritenum_t` 枚举、`mobjinfo[]`/`states[]`/`sprnames[]`、doomednum→mobjtype 表。 |
| `play/p_setup.{h,cpp}` | 扩 | `P_SetupLevel`/`P_SpawnMapThing`：THINGS → 活 mobj（替换 `main.cpp` 两处 mob 构建）。 |
| `play/p_map.{h,cpp}` | 改 | 泛化 `P_CheckPosition`/`P_TryMove`/`P_TrySlide` 走 `mobj_t`（radius/height/flags 来自 info）；`P_MovePlayer`/`P_CalcHeight` 改操作 `player.mo`。 |
| `render/r_things.{h,cpp}` | 改 | `mobj_t` **迁出**至 `p_mobj.h`（`#include` 之）；删 `SpriteDef`/`spriteDefForType`（折叠进 `info`）；`R_ProjectSprite` 取 `const mobj_t&`。 |
| `render/r_bsp.{h,cpp}` | 改 | `R_RenderView` 去掉 `things` 向量、`thingsBySubsec` 改持 `const mobj_t*`；`renderSubsector` 指针替索引。 |
| `platform/i_video.{h,cpp}` | 扩 | `Input` 加边沿触发 `spawn`（toss 键 KEYDOWN）。 |
| `main.cpp` | 改 | 用 `P_SetupLevel` 替 mob 构建；主循环跑 `P_RunThinkers` + `P_BuildThingsBySubsec`；玩家用 `player_t.mo`；接 toss 键；新增 `--tickdump`。 |
| `tests/*` | 扩/改 | 新 `test_p_mobj`/`test_p_tick`/`test_info`；改 `test_p_map`（泛化签名）、`test_r_things`（删 SpriteDef 测、改 mobj_t 来源）。 |
| `CMakeLists.txt` | 扩 | `doomcore` 增 `p_mobj.cpp`/`p_tick.cpp`/`info.cpp`；tests 增新文件。 |

**隔离原则**：`p_mobj`/`p_tick`/`info` 属 game logic（定点），不依赖 `r_*`；渲染器经 `mobj_t`（POD）单向读其渲染缓存字段，**不依赖 `info` 表**。`World` 是可单测的聚合体（构造时不需 SDL）。

## 4. mobj_t（活对象，p_mobj.h）

```cpp
struct mobj_t {
    fixed_t x, y, z;            // 16.16 地图坐标；z = 当前离地高度
    fixed_t momx, momy, momz;   // 动量（速度），16.16
    angle_t angle;              // BAM
    fixed_t floorz, ceilingz, dropoffz;  // P_CheckPosition 写入（vanilla tmfloorz/tmceilingz）
    int     health;
    int     flags;              // MF_*（见 §5）
    int     tics;               // 当前 state 剩余 tic；-1 = 永不自动推进
    const mobjinfo_t* info;     // 类型信息（半径/高度/flags/状态…）
    const state_t*   state;     // 当前状态（sprite/frame/nextstate）
    int     subsector;          // 当前所在 subsector 索引
    // —— 渲染缓存（state 改变时由 P_SetMobjState 刷新；渲染器只读这些）——
    char    sprname[5];         // 已解析 sprite 名（"BAR1"…）
    char    frame;              // 'A'-based（SpriteCache.resolve 做 frame-'A'，与 P4 约定一致）
    bool    fullbright;
    int     light;              // 每帧渲染前由所在 sector lightlevel 填（fullbright→255）
};
```

- `mobj_t` 从 `render/r_things.h` **迁到** `play/p_mobj.h`（它是游戏对象）。`r_things.h` 改 `#include "play/p_mobj.h"`。
- **thinker 存储**：`World` 持 `std::list<mobj_t> mobjs`（节点地址稳定 → `player_t.mo` 与 `thingsBySubsec` 持裸指针安全；O(1) 删除，为 P5b 死亡/拾取预留）。`P_SpawnMobj` 返回 `mobj_t*`（指向 list 节点）；`P_RemoveMobj` 从 list 擦除。
- **state 刷新渲染缓存**：`P_SetMobjState(mobj, statenum)` 设 `state=&states[sn]`、`tics=state->tics`、并刷新 `sprname=sprnames[state->sprite]`、`frame = 'A' + (state->frame & FF_FRAMEMASK)`（state 存 0-based、mobj 缓存 'A'-based 以喂 `SpriteCache.resolve`）、`fullbright = !!(state->frame & FF_FULLBRIGHT)`（faithful：vanilla 在 `P_SetMobjState` 拷 `state->sprite`/`frame` 到 mobj）。空 action（P5a）→ 不调动作函数。

## 5. info.h 表（faithful 子集，P5b-ready）

**枚举**：`spritenum_t { SPR_TROO, SPR_POSS, SPR_SPOS, SPR_SARG, SPR_BAR1, SPR_PLAY, … }` + `const char* sprnames[]`；`statenum_t { S_PLAY, S_TROO_STND, S_BAR1, … }`；`mobjtype_t { MT_PLAYER, MT_POSSESSED, MT_SHOTGUY, MT_TROOP, MT_SERGEANT, MT_BARREL, … }`。

```cpp
struct state_t {
    spritenum_t sprite;  statenum_t nextstate;
    int frame;           // 0-based 帧序号（vanilla FF_FRAMEMASK）; FF_FULLBRIGHT 位(0x8000) 单独解析
    int tics;            // -1 = 永不自动推进
    /* action = nullptr in P5a */
};
struct mobjinfo_t {
    int doomednum;       // -1 = 非地图可生成
    statenum_t spawnstate;  int spawnhealth;
    fixed_t radius, height, speed;  int mass;
    int flags;           // MF_SOLID | MF_DROPOFF | MF_NOBLOCKMAP | MF_NOGRAVITY | …
    // —— 以下含到 P5b 用得着的字段,避免每期改表（P5a 不用）——
    statenum_t seestate, painstate, meleestate, missilestate, deathstate, xdeathstate;
    int painchance, damage, reactiontime;
    /* seesound/painsound/…/activesound 留到音频期或占位 -1 */
};
extern const state_t    states[];
extern const mobjinfo_t mobjinfo[];     // index = mobjtype_t
int mobjtypeForDoomednum(int doomednum); // 替换 P4 的 spriteDefForType
```

- **覆盖**：`MT_PLAYER` + E1M1 全部渲染物（`POSS`/`SPOS`/`TROO`/`SARG`、`BAR1`、装饰/尸体 `POL5`/`POL6`/`GOR4`/`TRE1`/`TRE2`/`CELP`/`SMIT`/`ELEC`、武器/弹药/血包/护甲/钥匙 `SHOT`/`MGUN`/`LAUN`/`PLAS`/`CSAW`/`CLIP`/`SHEL`/`ROCK`/`STIM`/`MEDI`/`SOUL`/`BON1`/`BON2`/`ARM1`/`BROK`/`AMMO`/`SBOX`/`BKEY`、柱 `COLU`）。spawnstate 取自 vanilla `mobjinfo`，实现期以 `freedoom1.wad` 实际存在的 sprite lump 校准。
- **删 P4 的 `SpriteDef`/`spriteDefForType`**：doomednum 与 spawnstate→sprite/frame/fullbright 现在都从 `mobjinfo`/`states` 派生；`tests/test_r_things.cpp` 中相关用例改测 `info` 表。
- **MT_PLAYER**：`radius = 16<<FRACBITS`、`height = 56<<FRACBITS`（＝现 `PLAYERRADIUS/PLAYERHEIGHT`，**数值不变**→玩家碰撞行为不变）、`flags = MF_SOLID | MF_DROPOFF`（P5b 再加 `MF_SHOOTABLE`）。

## 6. 碰撞泛化（p_map）— 最高回归风险区

- `P_CheckPosition(const World&, const mobj_t&, fixed_t x, fixed_t y) → PosCheck`：bbox 用 `info->radius`；垂直用 `info->height`；dropoff/blocking 按 `flags`（`MF_SOLID`/`MF_DROPOFF`/linedef `ML_BLOCKING`）。blockmap 遍历、bbox 早退、`P_BoxOnLineSide`、`P_LineOpening` 全不变（P3.5 既有）。
- `P_TryMove(World&, mobj_t&, nx, ny) → bool`：复用 `P_CheckPosition`；可走则更新 `mobj.x/y/floorz/ceilingz`。判据（高度够/净空/step 限）用 `info->height`，不再用编译期 `PLAYERHEIGHT`。
- `P_TrySlide(World&, mobj_t&, dx, dy)`：沿轴退化尝试（逻辑不变，参数 mobj 化）。
- **玩家路由**：`P_MovePlayer(World&, player_t&, forward, strafe, turn)` 改操作 `player.mo`——`mo->angle += turn`；forward/strafe 向量由 `finecos`/`finesine` 算（同现）；`MAXMOVE` 截断；`P_TrySlide(World, *mo, dx, dy)`；之后 `mo->subsector=R_PointInSubsector`、`P_CalcHeight(World, player)`。**直接位移保留,不走 momx/momy**（见 §10）。
- `P_CalcHeight` 用 `mo->floorz` + `VIEWHEIGHT` 追 `player.viewz`（原 Player 逻辑搬过来，P3d 修复保留）。`Player` 结构退役，由 `player_t { mobj_t* mo; fixed_t viewz; … }` 取代。

> **回归守卫**：泛化后用「MT_PLAYER 半径 mobj」跑既有 `test_p_map` 用例，断言与 P3.5 同输入同可走性/坐标（数值回归）。

## 7. P_MobjThinker 物理（p_mobj，可见里程碑引擎）

每 tic 对 thinker 链中每个 mobj 调 `P_MobjThinker`（faithful `p_mobj.c::P_MobjThinker` 顺序）：

1. **P_XYMovement**（`momx|momy` 非零时）：按 `MAXMOVE/2` 步长逐步 `P_TryMove`（避免高速穿墙）；移动后施摩擦 `momx -= FixedMul-ish`；`|mom| < STOPSPEED` 归零；撞墙（`P_TryMove` 失败）→ 停（P5a 不做反弹/`special`）。
2. **P_ZMovement**（`momz` 非零或受重力时）：非 `MF_NOGRAVITY` → `momz -= GRAVITY`；`z += momz`；`z <= floorz` → 贴地 `z=floorz`、落地 `momz=0`；`z + height >= ceilingz` → 撞顶 `z=ceilingz-height`、`momz=0`。
3. **状态推进**：`state->tics != -1` → `tics--`；`tics==0` → `P_SetMobjState(info->…nextstate)`（P5a 多数对象 spawnstate 为静止、tics=-1 不推进；nextstate 链留 P5b 死亡动画用）。

- **常量**（逐字照搬 vanilla `p_mobj.c`，实现时读源定值）：`MAXMOVE`(已有)、`GRAVITY`、`STOPSPEED`、`FRICTION`。重落地 squat/squash、`MF_FLOAT` 漂浮、missile 爆炸 = P5b/P6。
- **MF_NOGRAVITY**（如漂浮装饰/灯）不下落；**MF_NOBLOCKMAP** 不入 blockmap（P5a thing-thing 未做，仅记录位）。

## 8. 关卡生成（p_setup）

- `P_SetupLevel(World&, map)`：`loadMap` 后遍历 `map.things`，对每个调 `P_SpawnMapThing`：
  - `mt = mobjtypeForDoomednum(th.type)`；未知/`mt<0` → 跳过（天然滤掉玩家起点之外的无精灵 thing；type 1 见下）。
  - `P_SpawnMobj`：`info=&mobjinfo[mt]`、`x/y`（THINGS×FRACBITS）、`angle=degreesToBam`、`flags=info->flags`、`health=info->spawnhealth`、`P_SetMobjState(info->spawnstate)`（刷渲染缓存）、`z=sector.floorheight`（先定 floorz）。
  - `subsector=R_PointInSubsector`、`floorz/ceilingz/dropoffz` 由 `P_CheckPosition` 或 sector 直读。
- **type 1（玩家起点）**：生成 `MT_PLAYER` mobj 存入 `world.player.mo`（**不进可见精灵收集**——第一人称不画自己）。
- 替换 `main.cpp` 里两处静态 mob 构建（实时模式与 `--dumpframe`）。

## 9. 渲染适配（r_bsp / r_things）

- **每帧渲染前** `P_BuildThingsBySubsec(World&)`：`thingsBySubsec.assign(subsectors.size(), {})`；遍历 `world.mobjs`，跳过 `player.mo` 与 `MF_NOSECTOR`；`ss=R_PointInSubsector(map, mo->x, mo->y)`、`mo->subsector=ss`、`thingsBySubsec[ss].push_back(&mo)`。同时刷新 `mo->light`（sector `lightlevel`，`fullbright`→255）。N mobj × 35Hz，可忽略。
- **`R_RenderView` 签名收口**：去掉 `const std::vector<mobj_t>* things`；`thingsBySubsec` 改 `const std::vector<std::vector<const mobj_t*>>*`。
- `renderSubsector` 收集逻辑不变，仅把 `(*things)[ti]` 换成解引用 `const mobj_t*`；`R_ProjectSprite(out, const mobj_t& t, …)` 读 `t` 的渲染缓存字段（`sprname/frame/fullbright/light/z/angle`），数学完全不变（P4）。
- 渲染器仍**不 include `info.h`**——零渲染数学回归。

## 10. 偏差清单（本期/延后）

| 偏差 | 状态 |
|---|---|
| 玩家移动保留直接位移（非 vanilla 动量模型） | 本期刻意：保 P3.5 调好的手感 + 降回归风险；动量化玩家移动延后（可选） |
| thinker 用 `std::list<mobj_t>`（非侵入式链表） | 永久：语义等价、C++ 更干净；P6 加非 mobj thinker 时按类型分派 |
| 只有 mobj↔墙碰撞，无 mobj↔mobj | P6（blockmap thing-thing） |
| 无动作函数（`A_Look`/`A_Chase`/视线） | P6 |
| 无伤害/武器/HUD/projectile mobjinfo | P5b |
| 玩家自身精灵不画（第一人称） | 永久（vanilla 同；P5b 画武器 psprite） |
| P4 单列 `wallDepth` 遮挡限制 | 沿用（P4 已知限制,本期不动） |

## 11. 测试与验收（roadmap §5 三道关 + 回归盲点）

1. **单测**（`tests/test_p_mobj.cpp`/`test_p_tick.cpp`/`test_info.cpp`，扩 `test_p_map`/`test_r_things`）：
   - **info 表完整性**：`mobjtypeForDoomednum(2035)==MT_BARREL`、`states[mobjinfo[MT_PLAYER].spawnstate].sprite==SPR_PLAY`、`mobjinfo[MT_PLAYER].radius==16<<FRACBITS && height==56<<FRACBITS`、各 E1M1 type 的 doomednum→type 唯一。
   - **P_MobjThinker 物理**：空地给 `momx` → 若干 tic 后位置前进、`|mom|` 因摩擦递减、终归零（停在原地附近）；给 `momz>0` → z 升后受重力回落、`z==floorz && momz==0` 终态；朝墙给 `momx` → `P_TryMove` 失败、坐标不变。
   - **碰撞泛化回归守卫**：`P_TryMove` 对 MT_PLAYER 半径 mobj 在既有 `test_p_map` 场景下可走性/坐标与 P3.5 一致。
   - **P_SetupLevel**：从 E1M1 THINGS 生成 mobj 数量/类型与 P4 `--dumpframe` 的 `mobs=N` 一致；玩家 mobj 存在且 `mo->info==&mobjinfo[MT_PLAYER]`。
2. **运行时 smoke（memory 回归盲点,重点）**：重构后 `doomcpp.exe` E1M1——**WASD 实机仍能动、转向正常**（这是最易「测试绿但运行时死」的点，必跑）；`--dumpframe` 固定相机对 P4 基线，精灵现来自 live state，图景一致。
3. **物理 dump**：新增 `--tickdump <n>`——载入、按命令行参数在玩家前生成一个带初速的 `MT_BARREL`、跑 `n` 个 thinker tic、`writeBMP`。断言帧间 mobj 位置改变、`n` 足够大时 `z==floorz && momz==0`。

## 12. 不做（YAGNI）

- 不做敌人 AI、伤害、武器、HUD、机关（P5b/P6）。
- 不做 mobj↔mobj / thing-thing 碰撞、spechit、blockmap thing 链（P6）。
- 不做存档/demo、音频（P7）。
- 不逐行翻译 vanilla、不装逐像素 oracle。
