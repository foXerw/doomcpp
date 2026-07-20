# P3a · 贴图管线 + 贴图墙 设计文档

- **日期**: 2026-07-21
- **阶段**: P3a（P3 拆分为 P3a/P3b/P3c 的第一段）
- **状态**: 已通过头脑风暴，待写实现计划
- **前置**: P2b（`v0.4-p2b-3d`）—— BSP 前向渲染、纯色灰墙、第一人称走动

## 1. 目标与范围

**P3a = 贴图墙渲染。** 解码 DOOM 图片格式（patch → 经 PNAMES/TEXTURE1 组合成 wall texture），把 P2b 的纯色灰墙渲染器替换为**完整的 upper / middle / lower 贴图墙**渲染器，并引入 sector 亮度。

- **里程碑**: 第一人称走在 E1M1 里能看到真实墙面贴图（砖、木板、金属面板等）；门口 / 台阶 / 高台的 upper / lower 贴图正确；地板天花板区域仍为黑色（留给 **P3b**）。
- **明确不做（推迟）**: 地板/天花板平面（P3b visplanes）；碰撞与眼高追踪（P3c）；完整 32 级 COLORMAP；精灵贴图（P4）；动画贴图。

### P3 拆分回顾
- **P3a（本档）**: 贴图管线 + 贴图墙（upper/middle/lower）+ sector 亮度。
- **P3b**: 平面贴图（flats，64×64 原始）+ visplane 地板/天花板渲染。
- **P3c**: BLOCKMAP 碰撞 + 眼高随 sector floor 变化。

## 2. 关键设计决策

- **方案 A：贴图在加载时合成为 RGBA 缓冲**（而非逐 patch 列渲染）。patch 图片格式一次性解码；每个被引用的 wall texture（可由 TEXTURE1 + PNAMES 组合多个 patch）在加载时合成进一个预解码 RGBA 缓冲；渲染时逐列采样该 2D 缓冲。渲染循环简单、合成可单测、与现代源端口做法一致；纹理都很小（典型 64×128），内存无忧。
- **亮度不入库**: 贴图 RGBA 存**全亮**值，渲染时按 sector `lightlevel` 线性调制（同一张贴图会出现在不同亮度的 sector）。完整 COLORMAP（含距离衰减曲线）推迟。
- **渲染数学保持 FLOAT**（与 P2b 一致的 P2 简化；数据仍为 fixed-point）。定点三角/纹理表留待更高保真需求。
- **逐列 ceiling-clip / floor-clip 遮挡**取代 P2b 的单 `occluded[]` 布尔——这是绘制 upper/lower/middle 所必需，也为 P3b visplane 直接复用。
- **模块划分贴原版**: 新建 `src/render/r_data.{h,cpp}` 对应原版 `r_data.c`；SDL-free，进 `doomcore`，可单测。

## 3. 新模块：贴图管线 `src/render/r_data.{h,cpp}`

### 3.1 WAD 图片格式（回顾，作为实现依据）

**Patch（图片，列式存储）**:
- 头 8 字节: `width`(i16)、`height`(i16)、`leftoffset`(i16)、`topoffset`(i16)
- `width` 个 `uint32` 列偏移（指向本 lump 内）
- 每列由若干 post 组成，post 格式: `topdelta`(u8)、`length`(u8)、`[pad]`(u8)、`length` 个像素（调色板索引）、`[pad]`(u8)；`topdelta==0xFF` 表示该列结束。

**PNAMES** (lump `PNAMES`): `count`(i32) 后跟 `count` 个 8 字节名字（空格/0 填充）。下标即 patch 索引 → patch lump 名。

**TEXTURE1 / TEXTURE2** (lump `TEXTURE1`、`TEXTURE2`):
- `numtextures`(i32)
- `numtextures` 个 `int32` 偏移（指向各 maptexture_t）
- 每个 maptexture_t: `name`(8)、`masked`(i32，忽略)、`width`(i16)、`height`(i16)、`columndirectory`(i32，忽略)、`patchcount`(i16)、后跟 `patchcount` 个 mappatch_t: `originx`(i16)、`originy`(i16)、`patch`(i16，PNAMES 下标)。

**PLAYPAL** (lump `PLAYPAL`): 256 个 RGB（每项 3 字节）。P3a 只用第一组调色板。

### 3.2 类型与接口

```cpp
// 已解码 patch
struct Patch { int width=0, height=0; std::vector<uint32_t> rgba; }; // w*h；alpha 0 = 透明缺口

// 一面墙贴图：名字 + 尺寸 + 合成后的 RGBA
struct Texture { char name[9]; int width=0, height=0; std::vector<uint32_t> rgba; }; // 全亮；alpha 0 = 无 patch 覆盖

// 由 WAD 一次性构建
class TextureLookup {
public:
    explicit TextureLookup(const WadFile&);
    const Texture* wall(const std::string& name) const;  // 大小写不敏感；缺失返回 nullptr
private:
    std::vector<std::array<byte,3>> palette_;   // PLAYPAL
    std::vector<Patch>   patches_;              // PNAMES 下标 → 已解码 patch
    std::vector<Texture> textures_;             // TEXTURE1（+ TEXTURE2）
    // 名字 → 下标 映射
};
```

### 3.3 流程
1. 读 `PLAYPAL` → `palette_`。
2. 读 `PNAMES` → patch 名字表。
3. 对每个 patch 名字: `checkNumForName` 找 lump → 解码图片格式 → `patches_`。缺失的 patch 跳过（留透明）。
4. 读 `TEXTURE1`（再尝试 `TEXTURE2`）: 对每个 texture 定义，建 `width*height` RGBA 缓冲（初始透明），把每个引用的 patch blit 到 `(originx, originy)`（索引 → 调色板 RGB → 全亮 RGBA；越界裁剪）。
5. 建 `name → index` 映射。

> flats（64×64 原始）解析推迟到 P3b。

## 4. 地图结构扩展（`p_setup`）

```cpp
struct side_t {
    int textureoffset, rowoffset;                 // mapsidedef_t +0 / +2
    char toptexture[9], bottomtexture[9], midtexture[9]; // +4 / +12 / +20（8 字符名 + NUL）
    int sector;                                   // +28
};
struct sector_t {
    int floorheight, ceilingheight;               // +0 / +2
    char floorpic[9], ceilingpic[9];              // +4 / +12（P3a 解析但暂不用，为 P3b 预留）
    int lightlevel, special, tag;                 // +20 / +22 / +24
};
struct seg_t { int v1, v2, linedef, side, offset, frontsector, backsector; }; // +offset = SEGS +10
```

- `parseSidedefs`（30 字节记录）: 读 offsets + 三张贴图名 + sector。
- `parseSectors`（26 字节记录）: 读 heights + floor/ceiling pic 名 + lightlevel/special/tag。
- `parseSegs`（12 字节记录）: 读 `offset`（沿 linedef 的 U 基准）。

## 5. 渲染器改造（`r_bsp.cpp`）

### 5.1 遮挡模型
用逐列 `ceilingClip[]` / `floorClip[]` 取代 `occluded[]`:
```
ceilingClip[x] = 自顶部向下已填充到的最大行号   (init -1)
floorClip[x]   = 自底部向上已填充到的最小行号   (init h)
```
绘制某墙段屏幕行区间 `[topY, botY]` 时只画 `max(topY, ceilingClip[x]+1) .. min(botY, floorClip[x]-1)`，再按墙段类型更新 clip。

### 5.2 每列纹理坐标
投影 `[x0,x1]`（同 P2b 的 near-clip + 透视）。对每列 x:
- **U（沿墙）** 该列恒定: 基值 `seg.offset + side.textureoffset`，再叠加该列沿 seg 的世界距离。该距离按 **scale（1/depth）插值**求得（透视正确，**非** screen-x 仿射）——即每列 U 增量由该列 scale 与 seg 世界长度决定，跨列步进用 `scale0/scale1` 插值（精确公式见计划）。最终 `U %= texW`。
- **V（沿列）** 仿射（墙是竖直平面 → V 无需透视校正）: 由该列 `scale = 1/depth`（在 `s0/s1` 间按 x 线性插值）投影出该墙段的世界高度边界 → 屏幕行；`V` 随屏幕行线性变化，叠加 `side.rowoffset`。

### 5.3 墙段类型（每列）
- **单面墙**（`backsector < 0`）: 从 front floor 到 front ceiling 画 **midtexture**（实心），画完该列被完全遮挡。
- **双面墙**:
  - 若 `frontCeilH < backCeilH` → **uppertexture** 画在 front 天花板 → back 天花板之间。
  - 若 `backFloorH < frontFloorH` → **lowertexture** 画在 back 地板 → front 地板之间。
  - 若有 **midtexture**（如栅栏）→ 画在 back 地板 → back 天花板之间，**透明 alpha 像素跳过**（看穿到另一 sector）。中间的看穿缺口（back 天花板与 back 地板之间未被墙覆盖部分）= P3b visplane 开口（P3a 渲染为黑色）。
- 每像素亮度: `texRGB * (lightlevel/255)`（按 front sector 的 lightlevel）。
- 移除 P2b 的临时距离 shade；sector lightlevel 为主导（距离 colormap 偏移留待后阶段）。

### 5.4 接口
`R_RenderView` 增参接收 `const TextureLookup&`（墙贴图来源）。`main` 与 `--dumpframe` 路径都构建一次 `TextureLookup` 并传入。眼高仍固定 41（P3c 再做 floor 追踪）。

## 6. 测试策略

- **新 `tests/test_r_data.cpp`**（纯逻辑，构造缓冲，无 WAD）:
  - `parsePNames`: 构造 PNAMES 字节 → 名字表正确。
  - `decodePatch`: 1 列 / 1 post → 已知 RGBA（含透明缺口）。
  - 合成: 1 patch 的 texture → 正确 width/height + 指定像素。
  - `PLAYPAL` → RGB。
- **扩展 `tests/test_p_setup.cpp`**: sidedef offset/贴图名/sector；sector lightlevel；seg offset。
- **渲染验证**（无单测）: `--dumpframe assets/freedoom1.wad frame.bmp [angleDeg]` → PowerShell `System.Drawing` 转 PNG → `Read`/`analyze_image`（BMP 不能直接 Read）。预期: E1M1 起始房间墙面出现可辨认贴图，且随 sector 亮度变明暗。
- 保留现有 21 个测试通过；P3a 合入 `main`，打 tag `v0.5-p3a-textured-walls`。

## 7. 文件结构

```
src/render/r_data.h / .cpp     新增 — 贴图管线（doomcore，SDL-free）
src/render/r_bsp.{h,cpp}       贴图墙渲染器 + ceilingClip/floorClip 遮挡
src/play/p_setup.{h,cpp}       扩展 side_t/sector_t/seg_t + 对应解析
src/main.cpp                   构建 TextureLookup 并传入 R_RenderView（含 --dumpframe）
tests/test_r_data.cpp          新增
tests/test_p_setup.cpp         + 扩展用例
CMakeLists.txt                 doomcore += src/render/r_data.cpp
```

## 8. P3b / P3c 衔接
- **P3b** 直接复用 P3a 引入的 `ceilingClip[]` / `floorClip[]` 作为 visplane span 边界（无返工）；新增 flats 加载 + `R_MapPlane` / `R_DrawPlane`（含 `yslope` / `distscale` 表）。
- **P3c** 新增 BLOCKMAP 解析 + `P_TryMove` 碰撞；眼高从固定 41 改为 `sector.floorheight + 41`。
