# doomcpp 路线图重新对齐 — 设计文档

- **日期**: 2026-07-25
- **状态**: 已通过头脑风暴,待写入实现计划
- **关联**: 在 `2026-07-19-doom-clone-design.md` 基础上,经 P0–P3c 落地实践后对**保真标尺、完成线、剩余阶段安排**的重新对齐。原文档的目标、技术栈、模块划分仍然有效;本文档只重申/收紧其中影响后续排期的部分。

## 1. 目的与范围(重申)

用现代 C++17 从零**忠实复刻**经典 DOOM(1993)的引擎与游戏逻辑:加载标准 WAD、原版软件渲染、完整单人战役体验(移动/碰撞、机关、敌人 AI、战斗、HUD、音频、存档)。

- **范围内**:P0–P7 全部(见 §4 路线图),交付**单人战役从 E1M1 通关**。
- **范围外 / 延后**:硬件加速渲染(OpenGL/Vulkan);P8 联机(`i_net` 死斗/合作)作为**正式路线图之外**的延伸目标,不计入"完成"。
- **法务**:GPL-3.0(原版源码派生);资源用 Freedoom(BSD-style),WAD 不入库。

## 2. 保真标尺(收紧)

实践过程中,原文档"保留 16.16 定点数"的决策在 P2/P3b/P3c 出现了漂移(三角函数、`yslope`/`distscale`、碰撞改用浮点)。本次重新对齐**回到最严档**:

> **算法 + 数值双忠实。** 渲染/碰撞的算法流程严格跟随原引擎,且**数值用 16.16 定点实现**(含 trig 表 `finecosine`/`finesine`/`finetangent`)。

连带要求:
1. 已落地的浮点代码必须**回填为定点**(→ 新增 P3.5 阶段)。
2. P4 起所有新代码天然定点。
3. 验收加**与 vanilla/Chocolate Doom 的数值+帧级比对**(见 §5)。

## 3. 完成线(定义"done")

> **单人战役可从 E1M1 带声音玩到结局,且可存读档。** 即 P0–P7 全部完成。

P8 联机不计入完成;P7 内的 demo 录制回放为可选项,不阻塞完成。

## 4. 对齐后的路线图与阶段目标

### 4.1 已冻结里程碑(已打 tag,不改)

| Tag | 阶段 | 交付 |
|---|---|---|
| `v0.1` | P0 地基 | 脚手架 / 定点数 / RNG / SDL2 窗口 |
| `v0.2` | P1 WAD | header + lump 目录 + 缓存 |
| `v0.3` | P2a | 地图解析 + 2D 自动地图 |
| `v0.4` | P2b | BSP 3D 纯色墙渲染 |
| `v0.5` | P3a | 贴图墙(patch 解码 + 透视校正) |
| `v0.6` | P3b | Visplane 地板/天花板 + 天空 + 距离阴影 |
| `v0.7` | P3c | BLOCKMAP 碰撞 + 眼高 + 35Hz tic |

### 4.2 待处理分支 `feat/p3d-fixes`

3 个 commit:地平线缝合线修复、天空填充到地平线、floorz 爬梯修复,以及调试工具 `--mapinfo` / `--sample` / `--dumpframe <wad> <bmp> [angle] [x y]`。
**处置**:合入 main(`--no-ff`)并打 tag **`v0.8-p3d-fixes`**,锁定修复;调试工具保留(比对阶段要用)。P3.5 从干净的 main 拉新分支。

### 4.3 新增阶段

#### `v0.9` · P3.5 定点回填 + Chocolate-Doom 比对骨架
- **范围**:
  1. 渲染数学回填 16.16 定点——`finecosine`/`finesine`/`finetangent` trig 表;`r_bsp` 投影与 seg 列高/透视 U/V;`r_data` 距离阴影表。
  2. `r_plane`:`yslope`/`distscale` 表与 `R_DrawSpan` 内循环定点化。
  3. `p_map`/`p_maputl` 碰撞(`R_PointOnSide` / `P_CheckPosition` / `P_TryMove`)定点化。
  4. 搭 **Chocolate-Doom 同机位帧 diff 骨架**:给定 WAD+关+机位+角度,双端 dump BMP → 像素 diff + 关键数值日志比对。
- **交付**:全渲染/碰撞核心走定点;既有测试全绿;至少一组 E1M1 标杆机位 dump 帧与 chocolate-doom 在容差内吻合。
- **验收**:a) 核心路径无 `float`/`double` 残留(仅边界 I/O 例外);b) 标杆机位像素 diff 在阈值内(见 §5);c) 单测全绿。
- **保留偏差**:`R_PlaneSpans`(替代 `R_MakeSpans`,可证等价)保留;stairstep slide(全 `P_SlideMove` 延后 P6a)。

### 4.4 前向阶段(细化)

| Tag · 阶段 | 范围 | 交付里程碑 | 验收(保真) |
|---|---|---|---|
| **`v0.10` · P4 精灵** | `r_things` 按列绘制 + 深度排序、`R_DrawMaskedColumn`;THINGS 解析 + 最小 `mobj_t`(仅作**可见渲染对象**);sprite 翻转/缩放/距离阴影 | E1M1 看到物品桶/装饰/敌人**静止**精灵,正确遮挡 + 光照 | 同机位 sprite 位置/尺寸与 chocolate-doom 比对 |
| **`v0.11` · P5 战斗+HUD** | `p_user` 开火、hitscan(`P_LineAttack`)+ 投射物、`p_inter` 伤害结算、武器选择/动画、`st_lib`/`st_stuff` 状态栏、物品拾取 | 射击静止 thing 造成伤害/销毁;拾取更新 HUD;完整状态栏 | 命中/伤害数值与 vanilla 对齐;HUD 截图比对 |
| **`v0.12` · P6 敌人AI+机关(大头,拆 a/b)** | **P6a 机关**:`p_spec` 门/电梯/开关/传送/钥匙门、`EV_`/按钮触发、**顺带回填全 `P_SlideMove` + momentum/friction + view bob + thing-thing 碰撞 + spechit**;**P6b AI**:`p_enemy`/`p_sight`/`p_tick` 状态机、`A_*` action、`P_CheckSight`、怪物内讧 | 开关门/电梯/传送可用;敌人察觉→追击→攻击→死亡,会内讧;**打通单关** | AI 状态转换表移植对齐;机关时序比对;单关起点玩到出口 |
| **`v1.0` · P7 流程+音频+存档** | `g_game` 主循环/关卡切换、`wi_stuff`/`f_finale`/`m_menu`/`hu_stuff`、`i_sound`(SDL2_mixer)SFX+MUS、存读档、剧集/秘密关流程 | **单人战役带声音从 E1M1 玩到结局**;可存读档 | 从头通关;音效触发正确;存读档往返一致 |
| ~~P8 联机~~ | `i_net` 死斗/合作 | — | **正式路线图之外**,兴趣驱动,不计入完成 |

### 4.5 每阶段统一约定(沿用)
`feat/p<N>-...` 分支 → 合 `--no-ff` 回 main → 末尾打 tag → 更新 README milestone + memory。

## 5. 验收基准(保真如何验证)

### 5.1 比对基准(谁是"标准答案")
- **数值基准**:`id-Software/DOOM` 原源码(16.16 定点公式)——直接读它该算出的中间值。
- **运行时 oracle**:`chocolate-doom`(原版算法的可运行复刻)。
- **资源对齐(关键)**:两边都用 **freedoom1.wad** 跑,否则贴图不同 → 像素处处不同,diff 无意义。

### 5.2 "数值双忠实"的阈值定义
两条,分主次:

1. **数值对齐(主,硬指标)**:关键投影/可见性中间值——seg 列 `x1/x2`、`topheight/heightscale`、visplane 高度、`R_PointOnSide` 结果、`P_CheckPosition` 的 `floorz/ceilingz`——与 vanilla 定点公式输出**逐值相等**,允许 ±1 lsb(16.16 舍入顺序差异)。实现:引擎加 `--trace` 调试模式吐出这些中间量,与 vanilla/参考值 diff。确定性、不依赖 oracle 机位。

2. **帧级比对(次,佐证)**:在**确定性机位**(E1M1 玩家起点 + 起始角、零输入的第 0 帧;`--dumpframe` 已支持指定角度/坐标)双端 dump,做像素 diff:非天空区域**逐通道平均绝对差 < 3**,且**无结构性差异**(差异图无大片连续错位,仅允许列边缘 ±1px 舍入)。非起点机位用 crafted demo,或退化为定性比对。

> **逐像素 bit-exact 不作为硬指标**(chocolate 自身有移植微差 + 舍入)。目标是"数值逐值对齐 + 画面视觉无差"。

## 6. 偏差清单(预期管理)

| 偏差 | 状态 / 何时消除 |
|---|---|
| 渲染数学 / `yslope`/`distscale` / 碰撞用 float | **P3.5 回填定点后消除** |
| `R_PlaneSpans` 替代 `R_MakeSpans` | **永久保留**(可证等价 + 内存安全;文档留底) |
| 仅 stairstep slide(非全 `P_SlideMove`) | **P6a 消除** |
| momentum / friction / view bob 缺失 | **P6a 补齐** |
| thing-thing 碰撞、spephit specials 缺失 | **P6a 补齐** |
| P4 的 thing 仅可渲染、不可拾取/移动 | **P5 补齐** |
| 音频用 SDL2_mixer(非原版 DMX) | **永久保留为已知偏差**——原版 DMX/MUS 音色不可得,SDL2_mixer 合成仅近似 |
| 联网 P8 | 路线图外,不计入完成 |

P3.5 完成后,仅剩 **3 项永久偏差**:`R_PlaneSpans`、音频 SDL2_mixer、P8 延后——均有据可查,且不影响战役通关。

## 7. 路线图不做的事(YAGNI 收口)
- 不做硬件加速渲染(OpenGL/Vulkan)——坚持软件渲染保真。
- 不做逐像素 bit-exact——数值对齐 + 视觉无差即可。
- 不做现代化手感改写——保持原版手感。

## 8. 即刻行动项(本 spec 批准后)
1. 合 `feat/p3d-fixes` → main,打 `v0.8-p3d-fixes`。
2. 从 main 拉 `feat/p3.5-fixed-point`,进入 writing-plans 写 P3.5 实现计划。
