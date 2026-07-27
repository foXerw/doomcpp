# doomcpp P4 精灵（r_things）— 设计文档

- **日期**: 2026-07-27
- **状态**: 已通过头脑风暴,待写入实现计划
- **阶段**: `v0.10` · P4 精灵
- **关联**: 承接 `2026-07-25-roadmap-realignment-design.md` §4.4 的 P4；复用 P3.5 的 float 渲染器与既有 `decodePatch`/THINGS 解析。

## 1. 目的与范围

P4 让 E1M1 出现**静态精灵**：物品桶、装饰、敌人（静止）按其地图位置正确地投影到屏幕、被墙遮挡、按距离变暗、朝向合理。本期只做**可见渲染对象**——无生命/移动/AI/拾取（P5/P6 补）。

- **范围内**: sprite 数据缓存（S_START/S_END 分组解码 + offset）、最小 `mobjinfo` 表、render-only `mobj_t`、float 投影与 BSP 遍历期收集、按列遮挡的 masked 绘制、8 向旋转选择、镜像翻转、距离阴影、`R_RenderView` 集成、`--dumpframe` 可视化验证、单测。
- **范围外（显式延后）**: 拾取/伤害/移动/状态机（P5/P6）；thing-thing 碰撞、spechit（P6a）；fuzzy（spectre 半透明，本期当普通精灵）；武器 psprite（玩家手中枪，P5）。

## 2. 保真标尺（沿用 + 消解张力）

- **r_things 属渲染器 → 走 float**（与 `r_bsp`/`r_plane` 一致，P3.5 永久偏差）。这消解 roadmap §2「P4 起所有新代码天然定点」与「渲染器 float」之间的张力：**定点约束作用于 game logic（`p_*`），渲染器（`r_*`）按既定偏差走 float**。sprite 投影数学与 `renderSeg::toCam` 同构（float）。
- **不建 `tantoangle` 表**。vanilla 用 `tantoangle` 仅为在定点下选旋转帧；float 渲染器用 `atan2f` 等价达成（YAGNI）。
- **复刻,不是翻译**：可读 vanilla `r_things.c` 理解算法原理，但不逐行翻译、不逐值对标；验收靠 doomcpp 自身数值自洽 + 实机视觉（roadmap §5）。

## 3. 模块与职责

| 文件 | 动作 | 职责 |
|---|---|---|
| `render/r_data.{h,cpp}` | 扩 | `Patch` 增 `leftoffset`/`topoffset`；`decodePatch` 填充这两个字段（wall 路径不读它们 → 零回归）。 |
| `render/r_things.{h,cpp}` | **新** | `SpriteCache`、`SpriteDef`/`mobjinfo` 表、`mobj_t`、`vissprite_t`、`R_ProjectSprite`、`R_DrawSprites`、`R_DrawMaskedColumn`。 |
| `render/r_bsp.{h,cpp}` | 扩 | `Cam` 增 `vissprites`/`things`/`sprites`；`renderSubsector` 收集 thing；`R_RenderView` 末尾 `R_DrawPlanes` 后调 `R_DrawSprites`。 |
| `main.cpp` | 扩 | 启动构建 `std::vector<mobj_t>`（z=sector floor）；构建 `thingsBySubsector` 索引；传入 `R_RenderView`。 |

**隔离原则**：`r_things` 自含数据 + 投影/绘制；`r_bsp` 仅在遍历期调用 `R_ProjectSprite` 收集、在 `R_RenderView` 末尾调 `R_DrawSprites`。clip 数组以引用传入绘制函数。模块边界与当初拆 `r_plane`（`PlaneCtx`/`R_DrawPlanes` 由 `r_bsp` 调用）同构。

## 4. SpriteCache（sprite 数据）

- 扫 WAD 中 `S_START`..`S_END` 之间的 lump（仿 flat 的 `F_START/F_END` 扫描）。`S_START`/`S_END` 之外的 sprite lump（如 `SS_START/SS_END` 包裹）不处理（Freedoom 用前者）。
- 每 lump 名解析为 `(sprname[4], frame char, rotations)`：
  - `POSSA1` → sprite=`POSS`, frame=`A`, rot=1。
  - `POSSA2A8` → sprite=`POSS`, frame=`A`, **rot 2 与 8 共用此 lump**（8 是 2 的水平镜像）。
- 每 lump 用扩展后的 `decodePatch` 解码（含 `leftoffset/topoffset`）。
- 索引：`sprname → frame → rot(1..8)`，返回 `const SpritePatch*`（含 decoded rgba + offset + width/height）或 nullptr。查不到 → 该 thing 不渲染。
- **镜像**：取 rot k 时，若无独立 lump 而存在 `Rk'R(k')` 共用对（如查 rot8 命中 `A2A8`），则用 rot2 的 patch 做**水平翻转**绘制。

## 5. mobjinfo 表 + mobj_t

- `struct SpriteDef { int doomednum; const char* sprname; char frame; bool fullbright; };`——硬编码表（vanilla 也在 `info.c` 硬编码 `mobjinfo`）。
- 表覆盖 E1M1 可见 thing；**不在表中的 doomednum → 不渲染**（天然过滤玩家起点 1-4、DM 起点 11、teleport 目的地 14 等无精灵的 thing）。
- 代表条目（spawnstate 取自 vanilla `mobjinfo`，实现时以 freedoom1.wad 实际存在的 lump 校准）：

  | doomednum | sprname | frame | 说明 |
  |---|---|---|---|
  | 2035 | BAR1 | A | 桶 |
  | 3004 | POSS | A | zombieman（站） |
  | 9 | SPOS | A | shotgun guy |
  | 3001 | TROO | A | imp |
  | 3002 | SARG | A | demon |
  | … | … | … | 装饰/尸体/火炬等，实现期补全 |

- `mobj_t`（render-only，P5/P6 再加 health/momentum/state）：

  ```cpp
  struct mobj_t {
      fixed_t x, y;        // 16.16 地图坐标
      int     z;           // 所在 sector floor（map units，启动时算定）
      angle_t angle;       // BAM（degreesToBam(thing.angleDeg)）
      int     sprite;      // SpriteDef 表下标；-1 = 不渲染
  };
  ```

- **启动构建**（`main.cpp`）：遍历 `map.things`，查 `SpriteDef` 表；`z = sectors[sectorOf(R_PointInSubsector(x,y))].floorheight`；angle 转 BAM。同时建 `std::vector<std::vector<int>> thingsBySubsector`（thing 在哪个子扇区）供遍历期 O(1) 收集。

## 6. 投影与收集（R_ProjectSprite，float）

对每个位于**当前被访问子扇区**的 mobj（经 `thingsBySubsector`）：

1. `dx = gx − px; dy = gy − py;`（gx/gy 取 `mobj.x>>16` 等地图单位，与 `renderSeg::toCam` 一致）。
2. `depth = dx*sin + dy*cos;`  `tx = dx*cos − dy*sin;`（`sin/cos` 同 `Cam`）。
3. `depth <= nearZ`（≈0.01）→ 丢弃（在身后/贴脸）。
4. 旋转选择（相对方位，非相机基）：从 thing 看向 player 的方位 `viewAng = atan2f(py − gy, px − gx)`；`relAng = viewAng − thingFacingRad`（thingFacingRad 由 `mobj.angle` BAM→弧度）；把 `relAng` 量化到 8 向 rotation index（每 45° 一档、半档偏置对齐 vanilla `>>29`）。该 frame 若无该 rotation 的独立 lump → 退回 rotation 1；再查镜像（§4，决定 `flip`）。得到 `SpritePatch*`，nullptr 则丢弃。
5. `scale = 1/depth`（**与 `drawCol` 同约定**，避免单位不一致）。屏心 `xcenter = w/2 + focal*tx*scale`；sprite 屏宽 = `patchWidth*focal*scale`；`x1 = xcenter − leftoffset*focal*scale`；`x2 = x1 + patchWidth*focal*scale`。`[x1,x2]` 与屏外无交集 → 丢弃。
6. `gzt = z + topoffset`（**图像顶部**的 world-z，faithful to vanilla `R_ProjectSprite:556` `vis->gzt = thing->z + spritetopoffset`）。**非** `z + patchHeight`——对 `topoffset != height` 的精灵（多数悬挂/装饰）会垂直错位。图像底部 = `gzt − patchHeight`（patchHeight 取自 patch）。
7. `light` = 所在 sector `lightlevel`（`fullbright` 则 255）。
8. 产出 `vissprite_t { float depth, scale; int x1,x2; float xTrueLeft, gzt; const Patch* patch; bool flip; int light; }` 入 `Cam::vissprites`。`xTrueLeft` = 未裁剪的左屏沿（texcol 映射用，因 x1/x2 已 clamp 到屏）；`gzb` 不存（= `gzt − patch->height` 可推导）。

> **精确的 per-row 绘制公式**（含 `imgRow = gzt − worldZ` 的逐行反投影、syTop/syBot、texcol/flip）见实现计划 `docs/superpowers/plans/2026-07-27-p4-sprites.md` Task 7–8；本节为概念描述。

## 7. 绘制（R_DrawSprites / R_DrawMaskedColumn）

- `Cam::vissprites` 按 `depth` **降序**排序（远→近，painter's algorithm）。
- 对每个 vissprite，遍历屏列 `x ∈ [max(0,x1), min(w-1,x2)]`：
  1. sprite texel 列 `col = (x − x1)/(focal*scale)`（`scale=1/depth`），clamp `[0,patchWidth-1]`；`flip` 则 `col = patchWidth−1−col`。
  2. 垂直屏幕区间：`syTop = h/2 − focal*(gzt−eyeZ)*scale;` `syBot = h/2 − focal*(gzb−eyeZ)*scale;`（与 `drawCol` 同式）。
  3. **裁剪到 `[ceilingClip[x]+1, floorClip[x]-1]`**（墙遮挡精灵；clip 来自 `R_RenderView` 的 `Cam`，平面绘制后仍有效）。
  4. 逐行 `y`：反投影 `z = eyeZ + (h/2 − y)/(focal*scale);`（同 `drawCol`）；`V = z − gzb`（1 map unit = 1 texel，同 wall V 映射），采样 `patch.rgba[row=V][col]`；距离阴影 `shade = clamp(light/255 * R_DistanceShade(depth), 0,1)`；**透明 texel（alpha==0）跳过**；其余按 shade 写入 fb（RGBA8888，与 `drawCol` 同字节序）。
- **精灵间不改 clip**：back-to-front + skip-transparent 即正确互遮挡（近者后画覆盖远者不透明像素，透明处透出远者）。

## 8. 集成（R_RenderView）

`R_RenderView` 末尾顺序：`renderNode`(墙) → `R_DrawPlanes`(平面) → **`R_DrawSprites(c)`**（新增，用 `c.ceilingClip`/`c.floorClip`/`c.vissprites`）。`R_RenderView` 签名增 `const std::vector<mobj_t>* things` 与 `const SpriteCache* sprites`（`main.cpp` 与 `--dumpframe` 均传入；nullptr 时跳过精灵阶段，保持旧路径可测）。

## 9. 测试与验收（roadmap §5 三道关）

1. **单测**（`tests/test_r_things.cpp` + 扩 `test_r_data`/`test_p_setup`）：
   - `decodePatch` 读 offset（构造最小 patch 字节流）。
   - lump 名分组（`POSSA1`/`POSSA2A8`）与镜像查表。
   - `R_ProjectSprite`：身后/越界丢弃、`x1/x2/scale` 数值合理。
   - 旋转 index（8 向）与 flip。
   - `R_DrawMaskedColumn`：被 clip 裁剪、透明跳过、shade 写入。
2. **数值自洽**：`--dumpframe` 洋红清屏 gap 扫描——确认 sprite 像素写入、未引入新 HOM 空洞（坑底/玩家起点基线 gap 不升）。
3. **用户实机**：`doomcpp.exe` E1M1——桶/装饰/怪物静止精灵出现，被墙遮挡、按距离变暗、朝向合理。

## 10. 偏差清单（本期/延后）

| 偏差 | 状态 |
|---|---|
| r_things 数学 float（非定点） | 永久保留（渲染器偏差，§2） |
| 不建 tantoangle（用 atan2f） | 永久保留（YAGNI，§2） |
| `mobj_t` render-only | P5 补（拾取/移动）/P6 补（AI/状态机） |
| spectre 无 fuzzy | 本期当普通精灵（可选后续） |
| 武器 psprite | P5 |

## 11. 不做（YAGNI）

- 不做 sprite 动画状态机（桶爆炸、敌人走/死帧）——P4 只画 spawn 静止帧。
- 不逐行翻译 vanilla `r_things.c`、不装 chocolate-doom 逐像素 oracle。
- 不做硬件加速 / 透明 alpha 混合（沿用 wall 的透明跳过）。
