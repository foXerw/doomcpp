# P3b · Visplane 地板/天花板 设计文档

- **日期**: 2026-07-22
- **阶段**: P3b（P3 拆分为 P3a/P3b/P3c 的第二段）
- **状态**: 已通过头脑风暴，待写实现计划
- **前置**: P3a（`v0.5-p3a-textured-walls`）—— 贴图管线 + upper/middle/lower 贴图墙 + sector 亮度 + 逐列 ceilingClip/floorClip 遮挡

## 1. 目标与范围

**P3b = 地板/天花板渲染（flats + visplanes）。** 加载 flat 贴图（64×64 原始调色板），用 DOOM 的 visplane 算法把 P3a 遗留为黑色的地板/天花板区域填上贴图，并引入距离变暗、天空 hack、动画 flat。

- **里程碑**: 第一人称走在 E1M1 里能看到真实地板/天花板贴图；跨共面 sector 无接缝；地板随距离变暗；户外区（`F_SKY1`）天花板呈天空色；毒液/水面（`NUKAGE`/`FWATER`）随时间动画。
- **明确不做（推迟）**: BLOCKMAP 碰撞与眼高随 sector 变化（**P3c**）；完整 32 级 COLORMAP 表（本档用 float 衰减曲线近似距离变暗）；全景 sky 贴图映射（本档天空为纯色）；精灵/敌人（P4）；贴图墙的 upper/middle/lower 已在 P3a 完成。

### P3 拆分回顾
- P3a: 贴图管线 + 贴图墙 + sector 亮度。✅
- **P3b（本档）**: flats + visplane 地板/天花板 + 距离变暗 + 天空 hack + 动画 flat。
- P3c: BLOCKMAP 碰撞 + 眼高随 sector floor 变化。

## 2. 关键设计决策

- **D1 渲染数学保持 FLOAT（与 P2b/P3a 一致的 P2 简化）**，但引入原版的 **yslope / distscale 预计算表**（用 float 值）。即忠实复刻 visplane *算法结构*（`R_FindPlane`/`R_CheckPlane`/`R_MakeSpans`/`R_MapPlane`/`R_DrawSpan`），数值仍为 float。视点水平、无俯仰 → 地平线恒在 `h/2`，两表只依赖分辨率，可初始化一次。定点 16.16 表留待统一的"高保真 pass"。
- **D2 忠实 visplane 链表 + 合并**（而非逐列退化填法）。`R_FindPlane` 按 *height + flat + lightlevel* 合并共面相邻 sector → 跨 sector 无接缝；`R_CheckPlane` 逐列认领、冲突则分裂出新 plane；`R_MakeSpans` 把逐列 top/bottom 边缘转成水平 `(y, x1, x2)` 跨度；span 内投影可增量。
- **D3 距离变暗统一应用于 flats 与 walls**。共享 `distanceShade(depth)∈[0,1]` 衰减函数：flats 逐行、walls 逐列。否则"墙亮地暗"会割裂。这是对 P3a `drawCol`（仅 sector lightlevel 调制）的一处 retrofit。
- **D4 天空 hack 用纯色**（`F_SKY1` 天花板 plane 填恒定色，不随距离变暗）。全景 sky 贴图映射推迟。
- **D5 动画 flat 用帧 tick 循环**：硬编码原版动画组（E1M1 相关 `NUKAGE1/2/3`、`FWATER1/2/3/4`，表可扩展），visplane 构建时按"本帧 tick"解析当前帧；每帧重建 visplane 即推进动画。
- **D6 flats 与 wall textures 同层加载**（`r_data` / `TextureLookup`），均为全亮 RGBA，渲染时调制；flat 解码复用现有 `palette_`。
- **D7 模块划分贴原版**：新建 `src/render/r_plane.{h,cpp}` 对应原版 `r_plane.c`；SDL-free，进 `doomcore`，可单测。

## 3. Flats 加载（`r_data` 扩展）

### 3.1 WAD flat 格式（实现依据）
- Flats 位于 WAD 中 `F_START` 与 `F_END` 标记 lump 之间。
- 每张 flat = **4096 字节 = 64×64**，**行优先**（字节 `y*64 + x` = row y、col x 的调色板索引）。
- `F_SKY1` 是天空**标记名**，不作普通 flat 解码。
- 动画组（doom1 / E1M1 相关，可扩展）：`NUKAGE1→NUKAGE2→NUKAGE3`、`FWATER1→FWATER2→FWATER3→FWATER4`。实现期对照 WAD `--listlumps` 核实确切帧名。

### 3.2 类型与接口（`r_data.h` 增）
```cpp
// 已解码 flat：64×64 全亮 RGBA（alpha 0xFF）
struct Flat { char name[9] = {0}; int width = 64, height = 64; std::vector<uint32_t> rgba; };

class TextureLookup {
public:
    explicit TextureLookup(const WadFile&);
    const Texture* wall(const std::string& name) const;            // 已有
    const Flat*   flat(const std::string& name) const;             // 新：大小写不敏感；缺失 nullptr
    const Flat*   flatForFrame(const std::string& name, uint32_t tick) const;  // 新：动画解析
    static bool   isSky(const std::string& name);                  // 新：F_SKY1 等
private:
    std::array<uint32_t,256> palette_{};
    std::unordered_map<std::string,int> wallIndex_;  std::vector<Texture> walls_;   // 已有
    std::unordered_map<std::string,int> flatIndex_;  std::vector<Flat>   flats_;    // 新
    std::unordered_map<std::string,std::vector<std::string>> animGroups_;           // 新
};
```
- 加载：遍历 `wad.numLumps()`，定位 `F_START`..`F_END` 区间；区间内 `lumpSize==4096` 者按行解码为 `Flat`，跳过 `F_SKY1`。建 `flatIndex_`（大写键）。
- `flatForFrame`：若 `name`（去尾数字后）是某动画组首帧，按 `tick % group.size()` 取当前帧名再 `flat()`；否则直接 `flat(name)`。
- `isSky`：`upper(name)` 形如 `F_SKY1`/`F_SKY*` 返回 true。

## 4. Visplane 数据模型与构建

### 4.1 数据结构（`r_plane.h`）
```cpp
struct Visplane {
    float       height;        // 世界 z（floorheight/ceilingheight）—— 合并 key 之一
    const Flat* flat;          // 本帧解析；nullptr + sky==true 即天空
    int         lightlevel;    // 0..255 —— 合并 key 之一
    bool        sky;
    int         minx, maxx;    // 覆盖列范围
    // 逐列跨度（含端点，屏幕 y）。`top[x] > bottom[x]` 表示该列未认领（sentinel）。
    std::vector<int> top, bottom;
};
```
- 平面集合为 `std::vector<Visplane>`，逐帧重建；`top/bottom` 长度 = 屏宽。

### 4.2 构建（`r_bsp.cpp` 的 renderSeg 逐列，sector 上下文已在手）
当前列 `x` 经投影得到前 sector 的天花板/地板屏幕行：
```
yCeilF  = h/2 - focal*(cH_f - eyeZ)*scale
yFloorF = h/2 - focal*(fH_f - eyeZ)*scale
```
（与 `drawCol` 投影同式；二开口时同样算后 sector 的 `yCeilB/yFloorB`。）逐列：
- 天花板：若前 sector 天花板在本列可见（即 `yCeilF` 上方有未填区），`plane = R_FindPlane(cH_f, flatForFrame(sec.ceilingpic,tick), light, isSky(sec.ceilingpic))`；`R_CheckPlane(plane, x)` 认领（冲突分裂）；按 `ceilingClip[x]` 与 `yCeilF` 写 `plane.top[x]/bottom[x]`。
- 地板：对称，用 `yFloorF` 与 `floorClip[x]`。
- 二开口的后 sector：在开口区域内同样为其天花板/地板建平面。

**精确逐列 top/bottom 簿记按原版 `r_plane.c` 的 `R_CheckPlane` 移植**（实现期对照 chocolate-doom 定准 sentinel 与索引语义）。合并 key = `(height, flat指针, lightlevel, sky)`，使共面相邻 sector 落到同一 plane。

### 4.3 绘制（`R_RenderView` 在 `renderNode` 之后调 `R_DrawPlanes()`）
对每个 plane：`R_MakeSpans` 把逐列 `top/bottom` 转成水平 `(y, x1, x2)` 跨度 → 对每段 `R_MapPlane(y,x1,x2)` → `R_DrawSpan`。天空 plane 的 span 走纯色分支，不走投影。

## 5. 平面投影数学（float 表）

视点水平、无俯仰 → 地平线恒在 `h/2`。两表只依赖分辨率（`w,h,focal=w/2`），初始化一次：

- **`yslope[y] = focal / (h/2 - y)`**（带号；`h/2 - y`>0 在天花板区，<0 在地板区）。行 y 到平面距离：
  `depth(y) = |planeHeight - eyeZ| * |yslope[y]| = focal*|planeHeight-eyeZ| / |y - h/2|`。
- **`distscale[x] = (x - w/2) / focal`**（该列右斜率）。

**`R_MapPlane(y, x1, x2)`**：
```
F = depth(y)                                  // 沿视线前进量
R = distscale[x] * depth(y)                   // 右移量
worldX = px + F*sin + R*cos
worldY = py + F*cos - R*sin                   // 与 renderSeg.toCam 同一坐标系（已推导自洽）
tx = floor(worldX) & 63;  ty = floor(worldY) & 63
texel = flat->rgba[ty*64 + tx]
```
span 内 `worldX/worldY` 每列增量恒定（`ΔR = depth/focal`，故 `ΔworldX = (depth/focal)*cos`，`ΔworldY = -(depth/focal)*sin`）→ 增量采样，即原版 span 相干性。

**边界**：地平线行（`y==h/2`）`yslope` 发散 → 跳过该行（或夹到安全最大距离）。

## 6. 亮度 / 距离变暗 / 天空 / 动画

- **`distanceShade(depth)`**: 简单衰减，返回 `[0,1]`，远处趋近一个最低值（不全黑）。具体曲线实现期用 `--dumpframe`→PNG 标定到"像 DOOM"。例形：`clamp(1 - depth*k, minBright, 1)`，`k`、`minBright` 标定常数。
- **flats 逐行**: 亮度 = `(lightlevel/255) * distanceShade(该行 depth)`。
- **walls 逐列 retrofit**: `drawCol` 改用本列墙 `depth`（renderSeg 已算，line ~117）过同一 `distanceShade`，再乘 `lightlevel/255`。最终像素 RGB = 全亮 texel × 亮度。
- **天空**: sky plane 的 `R_DrawSpan` 填**恒定色**常量（不乘亮度、不随距离）。色值标定（深蓝灰）。
- **动画**: visplane 构建时 `flat = flatForFrame(sec.floorpic/ceilingpic, animTick)`；逐帧重建即推进。`--dumpframe` tick=0。

## 7. 集成点

- `R_RenderView(fb,w,h,map,tex,px,py,ang,eyeZ)` → 增参 `uint32_t animTick`。
  - `Cam` 增 `uint32_t tick;`、持有本帧 visplane 集合与 `yslope/distscale` 表。
  - 流程：建表 → 清屏黑 → `renderNode`（含逐列记平面）→ `R_DrawPlanes`。
- `main.cpp`: 每帧 `++tick` 传入；`--dumpframe` 传 0；窗口标题改 `P3b visplanes`。
- `CMakeLists.txt`: `doomcore` 加 `src/render/r_plane.cpp`；`tests/` 加 `test_r_plane.cpp`。

## 8. 测试与验证

**`test_r_data.cpp` 续**:
- flat 解码：合成/已知 4096 字节 lump → 指定 RGBA（含调色板映射）。
- `F_START/F_END` 迭代：用 freedoom1.wad 计数 4096 字节 flat 数量 > 阈值。
- `isSky("F_SKY1")==true`、普通 flat false。
- `flatForFrame("NUKAGE1", tick)` 在 `tick` 递增时循环返回 `NUKAGE1/2/3` 对应 Flat；非动画名恒定。

**`test_r_plane.cpp` 新**:
- `yslope/distscale` 表值：指定分辨率下地平线行→极大/发散处理、若干行公式校验；`distscale[中心列]==0`。
- `R_MapPlane` 世界坐标：沿轴正交情形（如 `ang=0`、`eyeZ` 与 planeHeight 已知）→ 预期 `floor(worldX)&63`/`floor(worldY)&63`。
- `R_MakeSpans`: 合成逐列 `top/bottom` → 预期 `(y,x1,x2)` 跨度集合。
- `R_FindPlane` 合并: 两 `(height,flat,light)` 相同 → 返回同 plane；某 key 不同 → 新 plane。
- `distanceShade` 单调递减、有下界。

**视觉验证**: `--dumpframe assets/freedoom1.wad out.bmp` → PowerShell `System.Drawing` 转 PNG → Read/`analyze_image`：
- 地板/天花板不再全黑、可见贴图；
- `F_SKY1` 区域呈天空色；
- 远处地板较近处暗（距离变暗）；
- 墙体也随之轻微变暗（retroit 一致性）。

## 9. 受影响 / 新增文件

| 文件 | 动作 |
|---|---|
| `src/render/r_plane.h`、`r_plane.cpp` | 新建 |
| `src/render/r_data.h`、`r_data.cpp` | 扩展 flat 加载 + 动画 |
| `src/render/r_bsp.h`、`r_bsp.cpp` | renderSeg 记平面 + `drawCol` 距离变暗 + `R_RenderView` 增 tick/调 `R_DrawPlanes` |
| `src/main.cpp` | 每帧 tick、`--dumpframe` tick=0、标题 |
| `CMakeLists.txt`、`tests/test_r_plane.cpp` | 源/测试登记 |
| `tests/test_r_data.cpp` | flat 用例 |

## 10. 发版

分支 `feat/p3b-visplanes` → `--no-ff` 合并 `main` → tag `v0.6-p3b-visplanes`。视觉确认 + 全测试绿后发版。更新 `doomcpp-project-overview` 记忆。
