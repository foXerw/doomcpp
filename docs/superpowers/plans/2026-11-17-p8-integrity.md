# P8a/P8b 正确性体检与内容补全 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用自动化体检把 E1–E4 全部 36 张地图的「贴图错误 / 穿模」类基础缺陷扫出来并修掉，同时补上阻塞通关与破坏代入感的内容缺口（缺失物件、传送 specials、环境伤害/灯光）。

**Architecture:** 三件事：(1) 在 `main.cpp` 增加三个永久 headless 体检 CLI（渲染覆盖、碰撞穿模、资产解析），全部注册为 CTest gate；(2) 修渲染器的两类遮挡/覆盖缺陷（平面不写深度 → 精灵穿地板；玩家 `mo->z` 从不更新 → 高台上穿物件、命中判定错位）；(3) 用 `dbg/vanilla_info.c` 与 WAD 自身几何当"原理参考"补内容：物件表、teleport 家族、地板伤害、sector 灯光。

**Tech Stack:** C++17 / CMake / doctest / SDL2；MSVC；诊断沿用 `--gapscan` 的洋红哨兵法。

**Spec:** `docs/superpowers/specs/2026-07-25-roadmap-realignment-design.md`（保真标尺/验收三道关）；本阶段范围由本次体检数据驱动确定（`dbg/wadaudit.txt`、`dbg/reachaudit.txt`）。

## Global Constraints

- 复刻而非翻译：可读 vanilla 源码/WAD 数据理解原理，**不逐行翻译**、不装 chocolate-doom 做 bit-exact oracle（见 memory `doomcpp-reimplement-not-copy`）。
- 碰撞/移动保持 16.16 定点 + BAM；渲染器保持 float（doomcpp 自有实现）。
- 函数命名沿用原版 PascalCase（`P_*`/`R_*`）；代码与 commit message 用英文，文档用中文。
- 每阶段验收：`ctest` 全绿 **+** `--smoketest`/`--aittest`/`--campaigntest`/`--soundtest` 全绿 **+** 渲染覆盖 `geometry=0`（memory `verification-blindspot-runtime-init`）。
- 分支 `feat/p8-integrity`，完成后 `--no-ff` 合入 main 并打 tag `v0.15-p8-integrity`。
- 构建/测试命令（memory `doomcpp-build-and-toolchain`）：
  `C:/Users/User/cmake-4.4.0-windows-x86_64/bin/cmake.exe --build build --config Release [--target doomcpp_tests]`；
  `C:/Users/User/cmake-4.4.0-windows-x86_64/bin/ctest.exe --test-dir build -C Release`。

## Review Focus

体检阶段最怕「工具说绿了但玩家看得见 bug」，以下五类是本次计划必须显式钉住的：

1. **姿态采样点必须在真实可行走空间内**——多边形质心可能落在凹 sector 之外，用它渲染会得到整屏空洞的假阳性；体检必须校验 `sectorOf(point)==该 sector` 才纳入姿态集。
2. **精灵遮挡的"深度相等"边界**——物件脚底所在那一行平面深度与物件深度天然相等，若用 `<=` 判 occlude 会把脚削掉一圈；必须带偏置。
3. **玩家 `mo->z` 与 `viewz` 双轨**——只改 `viewz` 会让 thing 阻挡/命中判定继续用出生高度；两者必须由同一个支撑面推出。
4. **新增物件必须"能被渲染"**——只加 doomednum 不加 sprite/state 行，会生成一个不可见的阻挡物（比缺失更糟）；资产体检必须同时覆盖「doomednum→sprite→patch」整链。
5. **special 语义必须由地图几何反证**——没有离线文档可查，任何 special 的实现前必须先在 36 张地图上验证其结构假设（单/双面、tag sector 内是否有 type 14 落点等），验证不通过就不实现。

---

## Task 1: 全地图渲染覆盖体检 `--renderaudit`

**Files:**
- Modify: `src/main.cpp`（在 `--gapscan` 分支之后新增 `--renderaudit`）
- Modify: `tests/CMakeLists.txt`（注册 `render_audit`，仿照现有 `runtime_smoke` 对 `assets/freedoom1.wad` 的存在性守卫）
- Test: 该 CLI 自身即验收；另在 `tests/test_smoke.cpp` 不需要新增

**Interfaces:**
- Consumes: `loadMap`、`TextureLookup`、`R_RenderView(..., clearColor)`、`R_SetForceOpaqueForDiagnostics`、`playerStart`、`R_PointInSubsector`、`sectorOf`
- Produces: `--renderaudit <wad> [map...] [--poses N] [--tolerance N]`，stdout 每地图一行 `E1M1: poses=24 worst=0 (start)`，末行 `RENDERAUDIT PASSED|FAILED (n failing poses)`；退出码非 0 表示失败。

- [ ] **Step 1: 实现姿态生成 + 扫描**
  姿态来源：玩家出生点（必有）+ sector 质心（`segs` 端点均值，且 `R_PointInSubsector`→`sectorOf` 回验等于该 sector 才采用），按 sector 索引等距抽样至多 `--poses` 个（默认 12）；每姿态 8 个角度（0/45/…/315°）。每姿态渲染两遍：`forceOpaque=true` 计 `geometry` 空洞数，`false` 计 `raw`。
- [ ] **Step 2: 跑一遍拿到真实基线**
  Run: `./build/Release/doomcpp.exe --renderaudit assets/freedoom1.wad`
  Expected: 输出 36 行；记录所有 `worst>0` 的地图与姿态（写进 `dbg/renderaudit.txt`）。
- [ ] **Step 3: 设门槛**
  若基线全为 0 → `--tolerance 0` 注册进 CTest；若有非 0 → 先不注册 gate，把清单留到 Task 2 处理后再注册（**不许**为了让 gate 变绿而放宽 tolerance 到掩盖问题的程度）。
- [ ] **Step 4: 提交** `git add -A && git commit -m "test(p8): add all-map render coverage audit CLI"`

## Task 2: 修渲染覆盖问题（按 Task 1 清单）

**Files:**
- Modify: `src/render/r_bsp.cpp` / `src/render/r_plane.cpp`（视清单而定）
- Test: `tests/test_r_plane.cpp`（若涉及 span 取整，补一条边界用例）

- [ ] **Step 1: 复现** 取清单里 gap 最多的姿态，`--gapscan <wad> <ang> <x> <y> dbg/g.bmp` + BMP→PNG→Read 看图，确认是墙/平面接缝还是漏画。
- [ ] **Step 2: 先写失败测试**（能单元化的接缝取整问题）或在计划注释里记录"仅视觉验证"的理由。
- [ ] **Step 3: 修** 保持既有 P3.5d 修复语义（见 memory `known-visplane-coverage-bug`）。
- [ ] **Step 4: `--renderaudit` 与 `--gapscan` 双跑确认改善且无回归**。
- [ ] **Step 5: 提交**。

## Task 3: 玩家支撑面 z（修高台穿物件 / 命中高度错乱）

**Files:**
- Modify: `src/play/p_map.cpp:P_CalcHeight`（让 `p.mo->z` 跟随 `p.mo->floorz`，`viewz` 由它推出）
- Test: `tests/test_p_map.cpp`

**Interfaces:**
- Produces: 玩家 mobj 的 `z` 在任何时刻 = 其支撑面高度（`floorz`），供 thing 阻挡/命中/渲染统一使用。

- [ ] **Step 1: 写失败测试**（合成地图：两个 sector，A 地面 0、B 地面 +128，中间 24 单位可上台阶）
  - `TEST_CASE("player z follows the support floor")`：把玩家移到 B，`CHECK(w.player.mo->z == (128<<FRACBITS))`；当前会停在 0。
  - `TEST_CASE("player cannot walk through a barrel on a ledge")`：在 B 里放 `MT_BARREL`，`P_TryMove(World&)` 目标点压在桶上 → `CHECK_FALSE(moved)`；当前 z 陈旧 → 判成"垂直错开"而穿过。
  - `TEST_CASE("hitscan at foot height misses a player on a ledge")`：从 A 地面高度水平射击 B 上的玩家 → `CHECK(P_LineAttack(...) == nullptr)`。
- [ ] **Step 2: 跑测试确认失败**（`ctest -R "player z follows"`，Expected FAIL）
- [ ] **Step 3: 实现**：`P_CalcHeight` 里 `p.mo->z = p.mo->floorz`（保留现有 bob 与 ceiling cap；不动 P3d 的 floorz 语义）。
- [ ] **Step 4: 全量 `ctest` + `--smoketest` 绿**（`--smoketest` 的 barrel-toss/AI 段会间接检验 z 变化）。
- [ ] **Step 5: 提交**。

## Task 4: 平面写入深度缓冲 → 精灵不再穿地板/台阶

**Files:**
- Modify: `src/render/r_plane.h`（`PlaneCtx` 增加 `std::vector<float>* depth`）、`src/render/r_plane.cpp:R_DrawSpan`
- Modify: `src/render/r_bsp.cpp`（把 `wallDepthPx` 交给 plane ctx；改名 `worldDepthPx`）
- Modify: `src/render/r_things.cpp:R_DrawVisSprite`（occlusion 判据加偏置）
- Test: `tests/test_r_things.cpp`（新合成场景）

**Interfaces:**
- Consumes: `R_DrawSpan` 已有的 `dist = planeheight * yslope[y]`——与 `drawCol` 的 `depth` 同一度量（前向距离），可直接写入同一缓冲。
- Produces: `R_DrawVisSprite(fb,w,h,vis,eyeZ,focal,depthBuffer)` 语义不变，但缓冲现在含**全部**世界表面（墙+平面，天空除外）。

- [ ] **Step 1: 写失败测试**：合成三段地图——A(玩家, floor 0, ceil 128) / B(floor 32, 深 100 的平台, 无 bottomtexture) / C(floor 0, 内放一个 `MT_POSSESSED`)，相机在 A 望向 C。断言：(a) 精灵确有像素被画出来（`visPixels > 0`，防止"全被挡"回归）；(b) 精灵最底行屏幕 y > 平台远端投影行 → 那些像素不得出现（用当前实现会失败：脚下地板不挡）。
- [ ] **Step 2: 跑测试确认失败**
- [ ] **Step 3: 实现**：`R_DrawSpan` 对每个画出的非天空像素 `(*depth)[i] = min((*depth)[i], dist)`；`R_DrawVisSprite` 判据改 `depth[px] + 0.5f <= vis.depth`（0.5 map unit 偏置，保住脚底行）。
- [ ] **Step 4: 跑测试确认通过 + `--renderaudit`/`--gapscan` 无回归**（世界几何不走精灵路径，理应不变；这一步是防串扰）。
- [ ] **Step 5: 视觉 A/B**：E1M1 起始（矮台阶场景）改前/改后各出一帧，`--dumpframe` → PNG → Read 对比，确认矮墙后的怪物下半身不再浮在地板上。
- [ ] **Step 6: 提交**。

## Task 5: 全地图碰撞穿模体检 `--clipaudit`

**Files:**
- Modify: `src/main.cpp`（新增 `--clipaudit`）
- Modify: `tests/CMakeLists.txt`（注册 `clip_audit`）
- Test: CLI 本身；发现漏洞时回落到 `tests/test_p_map.cpp` 补单元用例

**Interfaces:**
- Produces: `--clipaudit <wad> [--steps N] [--seed S]`：对 36 张地图各做确定性漫游（固定种子决定 前进/ strafing / 转向 / 开门 use），每 32 tic 复检不变量；末行 `CLIPAUDIT PASSED|FAILED (k violations)`；违规打印 `map tick x y kind`。

不变量（每个复检点）：
1. `!bboxStraddlesBlockingLine(player)` —— 玩家 32×32 bbox 不跨任何单面线或 `ML_BLOCKING` 线；
2. `!overlapsSolidThing(player)` —— 与任何 `MF_SOLID` 物件 bbox 不重叠（z 区间用**修复后的** `mo->z`）；
3. `playerFits()` —— `floorz + height <= ceilingz`（允许 +4 容差，crusher 场景除外）；
4. `onSupport()` —— `mo->z == mo->floorz`（Task 3 之后应恒成立）。

- [ ] **Step 1: 实现漫游 + 四条不变量**（复用 `P_MovePlayerWorld`/`P_RunThinkers`/`P_UseLines`，不新造物理）
- [ ] **Step 2: 跑真实地图，记录违规清单** `dbg/clipaudit.txt`
- [ ] **Step 3: 逐条定位**（用 memory `p3d-fixes-and-clipmyth` 的教训：**没有复现就不改碰撞判据**；每条修复都要先有最小复现用例）
- [ ] **Step 4: 清零后注册 CTest gate（tolerance=0）+ 全量回归 + 提交**

## Task 6: 资产/物件解析体检 `--assetaudit`

**Files:**
- Modify: `src/main.cpp`（新增 `--assetaudit`）
- Modify: `tests/CMakeLists.txt`（注册 `asset_audit`）

**Interfaces:**
- Produces: 对 36 张地图逐条检查并汇总：
  1. 每个 sector 的 `floorpic`/`ceilpic` ∈ flats ∪ {`F_SKY1`}；
  2. 每个 sidedef 的非空 `top/mid/bottom` ∈ textures（单面线的 mid 必须非空且可解析）；
  3. 每个出现过的 thing `doomednum` → `mobjtypeForDoomednum != -1`；
  4. 每个 spawn 出来的 mobj 的 `sprname+frame` 在 `SpriteCache` 里能 `resolve` 到 patch。
  输出四类计数 + 未解析名清单；`ASSETAUDIT PASSED|FAILED`。
- [ ] **Step 1: 实现 + 跑** → `dbg/assetaudit.txt`（预期：flats/textures 已 0 未解析（Python 已验），thing 类会有约 45 个 doomednum 缺失）
- [ ] **Step 2: 注册 gate，把 thing 类计数作为 Task 7 的输入**。

## Task 7: 补物件表 + power-up 效果

**Files:**
- Modify: `src/play/info.h` / `src/play/info.cpp`（doomednum→state→sprite；参考 `dbg/vanilla_info.c` 的对应关系，用 `dbg/gen_p5b_tables.py` 的思路生成）
- Modify: `src/play/p_map.cpp:touchSpecialThing`（新增 2006 BFG / 2019 blue armor / 2022 invuln / 2023 berserk / 2024 partial invis / 2025 rad suit / 2026 map / 2045 supercharge / 2047 cell pack）
- Modify: `src/play/p_tick.h`（`player_t.powers[NUMPOWERS]` + 每 tic 递减）、`src/play/p_pspr.cpp` 或 `p_tick.cpp`
- Modify: `src/play/p_enemy.cpp`（invis 影响 `A_Look`/`P_LookForPlayers`）
- Test: `tests/test_info.cpp`、`tests/test_p_map.cpp`、`tests/test_p_enemy.cpp`

- [ ] **Step 1: 失败测试**（每条一个）：`2047` 存在并能拾取且给 `AM_CELL`；`2023` berserk → `p.powers[pw_strength]` 置位且拳头伤害提高；`2022` invuln → 计时递减且期间受伤减免；`2024` invis → 怪物 `A_Look` 不再锁定玩家；`2025` suit → 毒液伤害间隔变长；`2026` map → `p.powers[pw_allmap]`。
- [ ] **Step 2: 生成表**（脚本读 `dbg/vanilla_info.c`，只取 doomednum/sprite/frame/radius/height/flags 的必要子集，写成 doomcpp 自己的表格式）
- [ ] **Step 3: 实现效果 + 计时**，`--assetaudit` 的 thing 未解析数应为 0；`--campaigntest` 的 `items=` 计数上升。
- [ ] **Step 4: 全绿 + 提交**

## Task 8: 传送家族 specials（数据反证后实现）

**Files:**
- Create: `dbg/teleportprobe.py`（一次性：对 special ∈ {39,48,97,98,123,125,126} 的线，统计单/双面、背面 sector 是否有 type 14、tag sector 是否有 type 14）
- Modify: `src/play/p_spec.cpp`（`triggerFor` + `P_ActivateLine` + 新的 `EV_TeleportOtherSide` / kill 落地目标上的物件）
- Test: `tests/test_p_spec.cpp`

- [ ] **Step 1: 跑探针**，把结论写进本计划末尾「special 语义验证记录」；结构假设不成立的 special **不实现**，登记为已知限制。
- [ ] **Step 2: 失败测试**：合成地图放 W1 其他侧传送线，`P_CrossSpecialLines` 后玩家出现在背面 sector 的 type-14 处、动量清零、播放 `SFX_TELEPT`。
- [ ] **Step 3: 实现**（含 G1 只触发一次 / W1 一次性 / S2 可重复的触发语义；沿用现有 `triggerFor` 1=use 2=walk 分类）
- [ ] **Step 4: `--campaigntest` 增补断言**：对每张地图报告"存在 type 14 落点但无可达触发线"的孤立传送器数（信息性，不作 gate）
- [ ] **Step 5: 全绿 + 提交**

## Task 9: 环境伤害 + sector 灯光 special

**Files:**
- Modify: `src/play/p_map.cpp` 或新 `src/play/p_env.cpp`（地板伤害：按 `floorpic` 前缀 NUKAGE/SLIME/LAVA 的间隔与伤害；`pw_ironfeet` 生效）
- Modify: `src/play/p_spec.cpp`（sector special 5/7/8/13/14/17 的 glow/strobe → 改 `sector.lightlevel`）
- Test: `tests/test_p_map.cpp`、`tests/test_p_spec.cpp`

- [ ] **Step 1: 失败测试**：站在 NUKAGE1 sector 的玩家每 ~32 tic 掉血，穿上 suit 后掉血显著变慢；站在普通 flat 上不掉血。sector special 7 的 lightlevel 随 tic 变化、special 0 不变。
- [ ] **Step 2: 实现**（参数取 doomcpp 自定标称值并在注释里写明是本地标定，不声称与原版逐值相等）
- [ ] **Step 3: 视觉确认**：`--dumpframe-map` 出闪烁前后两帧（若灯光生效，像素均值应变化）
- [ ] **Step 4: 全绿 + 提交**

## Task 10: 文档与收口

**Files:**
- Modify: `README.md`（Roadmap 增加 P8 行、Diagnostics 增加三个体检命令、Known limitations 更新）
- Modify: `docs/superpowers/specs/2026-07-25-roadmap-realignment-design.md`（追加 P8 段落或在 plans 目录新增阶段说明）
- Memory: 更新 `doomcpp-project-overview` + 新增 `p8-integrity-status`

- [ ] **Step 1: 全量验收**：`ctest`（含新增三个 gate）+ 四个运行时 smoke + `--renderaudit` 逐地图 `worst=0`
- [ ] **Step 2: 文档写实测数字**（不许写未跑过的结论）
- [ ] **Step 3: `--no-ff` 合并 main + tag `v0.15-p8-integrity`**

---

## special 语义验证记录（Task 8 结论）

用 `dbg/teleprobe2.py` 把每个 type-14 落点物件通过真实 BSP 解析到它所在的 sector，再问该
sector 是"线的背面"还是"线 tag 指向的 sector"：

| special | 线数 | 落点在线背面 | 落点在 tag sector | 结论 |
|---|---|---|---|---|
| 39 | 6 | 0 | 6 | tag 传送（已实现，保持） |
| 97 | 387 | 59 | 387 | tag 传送（已实现，保持） |
| 125 | 16 | 16 | 0 | **其他侧传送** → 本阶段实现 |
| 126 | 131 | 131 | 0 | **其他侧传送** → 本阶段实现 |
| 48 | 336 | 0 | 0 | **不是传送**（272 条还是单面线）→ 不实现，登记为未知 |
| 123 | 78 | 0 | 0 | **不是传送** → 不实现，登记为未知 |

即：先前记忆里"48/123 也是其他侧传送"的假设被地图数据否证，因此没有实现它们——
按计划 Review Focus 第 5 条的约定，语义未经数据反证的 special 不动手。

## 执行结果（2026-11-17）

- Task 1 `--renderaudit` ✅（含姿态来源修正：sector 质心会落到凹 sector 之外的虚空，
  改为用地图 THINGS 位置；另加 `--gapscan-map`、`--colprobe` 两个定位工具）
- Task 2 渲染覆盖修复 ✅ — 三处：平面带改用 opening 边界、边界列 t 钳位、残留像素接缝填补
  （masked texel 的"设计上透视"不被误补）。结果：9654 帧中 0 个覆盖洞（tolerance=2）。
- Task 3 玩家支撑面 z ✅（3 条回归测试：脚跟随支撑面 / 高台油桶阻挡 / 齐脚射击命中）
- Task 4 平面写深度 → 精灵遮挡 ✅（+ 负控：E1M1 起始帧改前/改后差 213 px）
- Task 5 `--clipaudit` ✅ — 发现并修复两个 mover bug：地板抬升目标未夹到天花板
  （E1M7/E3M5 把玩家顶进天花板）、mover 挤压无伤害。现 0 违规。
- Task 6 `--assetaudit` ✅ — 553 个未生成 thing → 0（其余为放置标记，已加白名单）
- Task 7 物件表 + power-up ✅（钥匙 6/13、蓝甲、背包、BFG、电池盒、五种 power-up + 计时）
- Task 8 传送家族 ✅（125/126 实现；48/123 经数据否证，不实现）
- Task 9 环境伤害 + sector 灯光 ✅（NUKAGE/SLIME/LAVA + 防化服；glow/flash/strobe；
  `--smoketest` 三条实机断言 + 负控验证）
- Task 10 文档与收口 ✅（README、四个审计 gate、合并 main + tag `v0.15-p8-integrity`）
- 额外：天空改用 SKY1 patch 绘制（原来是一整块棕色）

未完成/顺延：菜单与标题画面、HUD 表情、automap 覆盖层、OPL 音乐、静默 crusher、
真正的楼梯生成器、door 联动灯光类型 1/2/4/15/16。
