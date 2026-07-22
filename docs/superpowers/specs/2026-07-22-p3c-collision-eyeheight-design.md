# P3c · BLOCKMAP 碰撞 + 平滑眼高 — 设计文档

- **日期**: 2026-07-22
- **阶段**: P3c（路线图：`BLOCKMAP 碰撞 + 眼高随 sector floor`）
- **分支**: `feat/p3c-collision-eyeheight`（→ merge `main` + tag `v0.7-p3c-collision`）
- **状态**: 已通过头脑风暴 + 用户确认（范围 = 方案 A），待实现

## 1. 目标与范围

P3b 之后玩家仍**自由穿墙**、`eyeZ=41` **硬编码绝对高度**（无视 sector 地板）、贴图动画与刷新率耦合、floor-sky 未接通。本阶段解决全部四项：

1. **玩家碰撞**：解析 BLOCKMAP，移植 `R_PointInSubsector` / `P_CheckPosition` / `PIT_CheckLine` / `P_TryMove`（玩家专用，things 留空），滑墙用 stairstep。
2. **眼高随 sector 地板**：`viewz → sector.floorheight + 41`，过渡平滑（`P_CalcHeight` 去 bob 版）。
3. **35Hz tic 时钟**：移动与贴图动画按 tic 驱动，每帧渲染 → 解掉「动画帧率耦合」（P3b 技术债 #1）。
4. **floor-sky 接通**：`F_SKY1` 作地板 → 天空（P3b 技术债 #2）。

**里程碑**：在完整贴图关卡中走动，撞墙不穿、上下楼梯眼高平滑跟随，动画不再随帧率变化。

## 2. 忠实度取舍（延续 P2 在案简化）

本项目核心杠杆是「逐函数对照原源码」，但以下简化已记录在案、本阶段延续：

- **碰撞用 float，不用 16.16 定点**。渲染路径早已全 float（memory 记录的 P2 简化）。碰撞对 demo 级确定性无要求，float 足够；`FixedMul`/定点仅保留给后续真正需要确定性的部分。
- **滑墙用 stairstep**（先整步、失败则仅 X、再仅 Y）。这是原版 `P_SlideMove` 内 `stairstep:` 分支的忠实子集。**完整 `P_SlideMove`**（沿墙角斜滑的 path-traversal + `P_HitSlideLine` 动量裁剪）**延后到 P6**（与动量物理一起），记为已知缺口。
- **不做**：view bob、动量累加、摩擦、spechit 特殊线触发。bob 属手感打磨（更适合与 P4 武器摆动一起）；动量/摩擦属 P6 mobj thinker；spechit/机关属 P6 `p_spec`。`line_t.special` 已解析但本阶段不动作。
- `heightsec` 是源端口概念，原版不存在，不移植。

**运动模型**：P3c = 「键位 → 每 tic 直接位移 → 碰撞 + 滑墙」。非原版的 thrust→momentum→friction（那是 P6）。

## 3. 模块划分（紧贴原版文件）

| 文件 | 对应原版 | 职责 | 状态 |
|---|---|---|---|
| `src/play/p_maputl.{h,cpp}` | `p_maputl.c` + `r_main.c` | 几何/定位：`R_PointOnSide`、`R_PointInSubsector`、`P_PointOnLineSide`、`P_BoxOnLineSide`、`P_LineOpening`、blockmap 取行迭代 | NEW（doomcore，无 SDL） |
| `src/play/p_map.{h,cpp}` | `p_map.c` + `p_user.c` | `Player` 结构、`P_CheckPosition`、`PIT_CheckLine`、`P_TryMove`、滑墙 stairstep、`P_MovePlayer`、`P_CalcHeight` | NEW（doomcore，无 SDL） |
| `src/play/p_setup.{h,cpp}` | `p_setup.c::P_LoadBlockMap` | `struct Blockmap` + `parseBlockmap` + `MapData::blockmap`（marker+10）；`line_t` += `mutable int validcount` | 扩展 |
| `src/render/r_bsp.cpp` | — | `renderSeg` 地板 visplane 在 `isSky(floorpic)` 时标 sky | 扩展（floor-sky） |
| `src/main.cpp` | `d_main`/`g_game` 循环 | `Player` + 35Hz tic 累加 + `viewz` + `animTick`(按 tic) | 改写循环 |
| `tests/test_p_maputl.cpp`、`tests/test_p_map.cpp` | — | 纯逻辑单测 | NEW |
| `tests/test_p_setup.cpp` | — | blockmap 解析用例 | 扩展 |

## 4. 碰撞模型（关键算法与常量）

### 4.1 常量（取自原版 `p_local.h` / `info.c` / `p_mobj.c`）

```
VIEWHEIGHT   = 41        // 眼高（map 单位）
PLAYERRADIUS = 16        // 碰撞半径（= mobjinfo[MT_PLAYER].radius）
PLAYERHEIGHT = 56        // mobjinfo[MT_PLAYER].height（净空判定）
MAXMOVE      = 30        // 单 tic 位移上限（本速度下不会触发子步）
MAXRADIUS    = 32        // things 迭代的 bbox 外扩（things 留空，仅供参考）
MAPBLOCK     = 128       // 每格 128 map 单位（MAPBLOCKSHIFT = FRACBITS+7）
STEP_LIMIT   = 24        // 上台 / dropoff 上限（map 单位）
```

### 4.2 BLOCKMAP 解析（对应 `P_LoadBlockMap`）

lump 为一串 `int16`（小端）：前 4 个为头 `[orgx, orgy, width, height]`，其中 `orgx/orgy` 为 **map 单位**（原版 `bmaporgx = h[0]<<FRACBITS`）；其后 `width*height` 个 `int16` 偏移，每格指向该格的行号列表（相对 lump 起点，单位 short），列表以 `-1` 结束。

- 世界点 → 格：`cellx = floor((worldX - orgx) / 128.0)`，`celly = floor((worldY - orgy) / 128.0)`。
- 格内行号：`offset = lump[4 + celly*width + cellx]`；从 `lump[offset]` 起读 `int16` 直到 `-1`。
- 越界格：空（无行）。

### 4.3 点定位（对应 `r_main.c`）

- **`R_PointOnSide(x,y,node)`**：对分区线做前/后侧判定（含 dx==0 / dy==0 的轴对齐特例）。
- **`R_PointInSubsector(x,y)`**：从根 `numnodes-1` 下行，`R_PointOnSide` 选 child，遇 `NF_SUBSECTOR`（低 bit = 0x8000）即叶 → `subsector → seg → sector`。用于眼高种子与碰撞种子。

### 4.4 开口与线判定（对应 `p_maputl.c`）

- **`P_LineOpening(front,back)`** → `{top, bottom, range, lowfloor}`：`top=min(两ceiling)`，`bottom=max(两floor)`，`lowfloor=min(两floor)`，`range=top-bottom`。单侧线 `range=0`。
- **`P_BoxOnLineSide(bbox, line)`**：盒与线侧判定，盒跨线返回 -1（碰撞候选）。

### 4.5 碰撞核心（对应 `p_map.c`，things 全部留空）

- **`P_CheckPosition(map, bm, x, y)`** → `{floorz, ceilingz, dropoffz, ok}`：
  1. 建 `tmbbox`（半径 16）。
  2. `R_PointInSubsector` 取 sector，种 `floorz=ceilingz=...`：`floorz=dropoffz=sector.floorheight`、`ceilingz=sector.ceilingheight`。
  3. 遍历 bbox 覆盖的 blockmap 格，每行（`validcount` 去重）跑 `PIT_CheckLine`；任一返回 false → `ok=false`。
- **`PIT_CheckLine`**：bbox 早出 → `P_BoxOnLineSide != -1` 则跳过；单侧线→挡；`ML_BLOCKING`→挡（`ML_BLOCKMONSTERS` 仅怪物，玩家忽略）；否则 `P_LineOpening` 调 `floorz/ceilingz/dropoffz`（取更受限的值）。
- **`P_TryMove(p, x, y)`**：`P_CheckPosition`；再查「`ceilingz-floorz < 56`」「`ceilingz - p.z < 56`（压头）」「`floorz - p.z > 24`（上台过高）」「`floorz - dropoffz > 24`（落 dropoff）」→任一失败 false；否则提交 `p.x/p.y` + 更新 `p.floorz/ceilingz`。

> **重要（原版结构纠正）**：stairstep 滑墙与 MAXMOVE 子步在原版**不在** `P_TryMove` 内，而在 `P_SlideMove`(`stairstep:`) 与 `P_XYMovement`。本阶段把 stairstep 直接放进每 tic 的移动步（见 4.6），因为还没有动量 thinker。

### 4.6 每 tic 移动（`P_MovePlayer`，本阶段简化版）

```
angle += turnInput * turnPerTic
位移 (dx,dy) = forwardVec*forwardInput*speed + rightVec*(strafeInput)*speed
              // forwardVec=(sin a, cos a), rightVec=(cos a, -sin a)（沿用 P2a 相机基）
封顶 |(dx,dy)| ≤ MAXMOVE
if (!P_TryMove(p, x+dx, y+dy))
    if (!P_TryMove(p, x+dx, y))      // 仅 X
        P_TryMove(p, x, y+dy);        // 仅 Y（stairstep）
更新 p.subsector = R_PointInSubsector(x,y); p.sector = ...
P_CalcHeight(p)                       // 平滑眼高
```

`speed`（每 tic 单位）取接近原版步行速度的初值，计划期通过 `--dumpframe` + 交互试跑微调（与 P3b 常量同法）。

## 5. 平滑眼高（`P_CalcHeight` 去 bob 版）

玩家始终贴地（`z = floorz`，无重力/跳跃），故直接平滑 `viewz`：

```
target  = sector.floorheight + VIEWHEIGHT           // 41
viewz  += (target - viewz) * 0.125                    // ≈ 原版 deltaviewheight>>3，约 8 tic(~0.23s) 趋近
viewz   = min(viewz, sector.ceilingheight - 4)        // 原版夹断
```

无 bob（P6）。`--dumpframe` 直接把 `viewz` 置为 `target` 渲染（起点贴地，无需等待趋近）。

## 6. 35Hz tic 时钟 + 动画解耦 + floor-sky

### 6.1 tic 钟（`main.cpp`）

用 `SDL_GetTicks()` 累加实际时间，每 `1000/35 ≈ 28.57ms` 跑一个移动 tic；每帧渲染最新状态。封顶 `4 tic/帧` 防 spiral-of-death。`animTick` **按 tic** 自增（不再按帧）→ 贴图动画 35Hz 与刷新率解耦（**P3b 技术债 #1**）。

### 6.2 floor-sky（`r_bsp.cpp::renderSeg`，P3b 技术债 #2）

地板 plane：若 `TextureLookup::isSky(sec.floorpic)` 为真，则把地板 visplane 标为 sky（`flat=nullptr, sky=true`）。`R_FindPlane` 对 sky 强制 `height=0/light=0` → 地板天与天花板天并入同一片天（无需新增合并键）。一处条件分支。

## 7. main.cpp 集成

- `Player{x, y, viewz, angle, subsector, sector, floorz, ceilingz}` 取代裸 `px/py/ang`。
- 起点：`playerStart` 取 x/y/ang → `Player`，`subsector=R_PointInSubsector`、`sector`、`viewz=sector.floor+41`。
- 循环：累计 tic → 每 tic `P_MovePlayer` → `R_RenderView(fb, w, h, map, tex, p.x, p.y, p.angle, p.viewz, ticCount)`。
- `--dumpframe`：建 `Player`、`viewz` 置 target、渲染（`animTick=0`）。
- 标题：`doomcpp 0.1.0  (P3c collision)`。

## 8. 测试策略

### 8.1 纯逻辑单测（doctest）

- **test_p_maputl**：`R_PointOnSide`（前/后/轴对齐 dx==0 & dy==0）、`R_PointInSubsector`（手搓 1 节点 BSP 两叶子→正确 sector）、`P_BoxOnLineSide`（盒跨线返回 -1 / 同侧返回 0 或 1）、`P_LineOpening`（双侧算 top/bottom/range/lowfloor；单侧 range=0）。
- **test_p_map**：手搓 `buildMiniMap()`（一堵单侧墙线 + 一个双侧台阶 portal + 1 节点 BSP）→ `P_CheckPosition`/`P_TryMove` 断言：撞墙挡、上台 >24 挡、可通行；滑墙：整步挡、仅 X 通过、最终贴墙不穿。
- **test_p_setup**（扩展）：合成 blockmap lump（头 + 1 格 + 两行号 + `-1`）→ `parseBlockmap` 格内行号正确、越界格空、org/width/height 正确。
- **眼高**：`P_CalcHeight` 多 tic 单调趋近 target；`viewz` 不超 ceiling-4。

> 手搓 mini-map 需构造合法 BSP（`nodes/subsectors/segs`），计划期写 `buildMiniMap()` 辅助；用 1 节点（根分区把两 subsector 分开）即可覆盖「点在 A/B sector」与碰撞。

### 8.2 可视化验证（与 P3a/P3b 同法）

`--dumpframe` → BMP → PowerShell `System.Drawing` → PNG → `Read`/`analyze_image`，外加交互跑（WASD）。**验收**：
- 不穿墙（贴墙停住、可沿墙滑）。
- 踏上抬高地台，眼高明显上升（dump 不同位置对比）。
- 贴图动画在 35Hz 稳定（限制刷新率可见动画速率不变）。
- floor-sky sector 渲染为 `kSkyColor`（若有此类 sector；否则仅验证不回归）。

## 9. 关键设计决定一览

1. 碰撞 **float**（非定点）— 延续 P2 简化。
2. 滑墙 **stairstep**（完整 `P_SlideMove` 留 P6）。
3. **不做** bob/动量/摩擦/spechit（均 P6）。
4. 模块按原版 `p_maputl`/`p_map` 拆分；blockmap 解析入 `p_setup`。
5. `animTick` 改按 tic 自增；floor-sky 一处分支接通。

## 10. 发布

全测试绿 + 可视化验收通过 → 更新 `doomcpp-project-overview` memory（P3c ✅、推进 P4 为 NEXT）→ `--no-ff` merge `main` → tag `v0.7-p3c-collision` → 更新 README 路线图勾选。
