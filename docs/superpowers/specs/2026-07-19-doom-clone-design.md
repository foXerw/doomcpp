# DOOM 忠实复刻 — 设计文档

- **日期**: 2026-07-19
- **仓库**: `doomcpp`（GitHub）
- **状态**: 已通过头脑风暴，待实现

## 1. 目标与范围

用现代 C++ 从零忠实复刻经典 DOOM（1993）的引擎与游戏逻辑，能加载标准 WAD（IWAD）资源、以原版软件渲染方式呈现完整的单人游戏体验。

**范围内（完整单人复刻）**:

- WAD 文件解析（lump 目录与缓存）
- BSP 遍历 + 按列软件渲染（墙、地板、天花板、精灵）
- 玩家移动与碰撞（BLOCKMAP + BSP 命中检测）
- 关卡机关（门、电梯、开关、传送、钥匙门）
- 敌人 AI（状态机：巡逻/察觉/追击/攻击/疼痛/死亡，含怪物内讧）
- 全套武器、战斗结算（hitscan + 投射物）、物品拾取
- 状态栏 HUD、自动地图、关卡间过场、菜单、结局画面
- 音效与音乐（MUS）
- 存档/读档、demo 录制回放
- 关卡推进、剧集/秘密关卡流程

**范围外 / 延后**:

- **联网多人（P8）**：最难且对"单人完整复刻"非必需，默认延后，核心完成后按兴趣决定。
- **硬件加速渲染（OpenGL/Vulkan）**：本期坚持原版软件渲染以保真；可作未来后端。

## 2. 技术栈

| 类别 | 选择 | 说明 |
|---|---|---|
| 语言 | C++17 | 现代写法（RAII/STL/`std::optional`），但数值语义跟随原引擎 |
| 窗口/输入 | SDL2 | 跨平台，社区主流 |
| 音频 | SDL2_mixer | SFX + MUS 音乐 |
| 渲染 | 软件渲染到内存帧缓冲 → SDL2 streaming texture 上屏 | 原版做法 |
| 构建 | CMake | 跨平台标准；Windows 用 vcpkg 拉 SDL2 |
| WAD 资源 | Freedoom Phase 1/2 | 免费 IWAD，可合法分发/测试 |
| 测试 | doctest | 纯逻辑单测 |
| 参考资料 | `id-Software/DOOM`（GPL 原源码）、`chocolate-doom/chocolate-doom`、DOOM Wiki | 逐段对照移植 |

## 3. 关键设计决策

- **方案选择：忠实软件渲染移植（方案 A）**。原版软件渲染器本身就是要复刻的核心遗产；1997 年 GPL 开源的原源码是现成"标准答案"，可逐函数对照——这是本项目的最大杠杆。
- **保留 16.16 定点数（`fixed_t`）**：渲染器与物理确定性的基础。改用浮点会让行为偏离原版手感，故忠实保留。
- **模块划分紧贴原引擎文件结构**：便于对照原版源码，也保留 DOOM 当年的工程结构。代码用现代 C++，但算法流程严格跟随原引擎。
- **移植而非机械翻译**：每个模块实现前先读懂对应原版文件，理解算法后再用现代 C++ 重写。

## 4. 架构与模块划分

系统按职责分层，每层内部是独立、可单独理解和测试的单元。

### 4.1 平台层（`i_*` — interface）
唯一与硬件/OS 打交道的地方，便于替换。
- `i_video`: SDL2 窗口 + 内存帧缓冲以 streaming texture 上屏
- `i_input`: 键鼠/手柄 → DOOM 事件
- `i_sound`: SDL2_mixer 加载 SFX + MUS
- `i_net`:（延后）联机

### 4.2 资源层（`wad/`）
- WAD 加载（header/lump 目录）、lump 缓存（按需加载/驱逐）
- 图片格式解析（patches/textures/flats）、MUS

### 4.3 渲染器（`render/` — 软件渲染器）
- `r_bsp`: 按 BSP 节点遍历，确定可见 seg 前后顺序
- `r_segs`: 把墙的 seg 渲染成竖列（DOOM 标志性按列绘制）
- `r_plane`: 地板/天花板（visplane 算法）
- `r_things`: 敌人/物品精灵（按深度排序）
- `r_draw`: 底层列绘制函数（光照/透明/纵深感）
- `r_data`: 渲染查表与数据

### 4.4 游戏模拟（`play/` — 最大的模块群）
- `p_setup`: 关卡加载（读 LINEDEFS/SECTORS/THINGS/NODES/SEGS 等建内部表示）
- `p_map` + `p_maputl` + `p_bsp`: 碰撞检测（BLOCKMAP + BSP 命中检测）
- `p_tick` + `p_mobj`: thinker 系统，每帧更新所有对象
- `p_enemy` + `p_sight`: 敌人 AI、视线判定
- `p_user`: 玩家移动/开火
- `p_inter`: 伤害/战斗结算
- `p_spec` + 门/电梯/开关/传送等: 关卡机关状态机

### 4.5 游戏管理（`core/g_game`）
主循环、关卡切换、存档、demo 录制回放。

### 4.6 UI（`ui/`）
状态栏 HUD（`st_lib`）、自动地图（`am_map`）、关卡间过场（`wi_stuff`）、消息（`hu_stuff`）、结局（`f_finale`）、菜单（`m_menu`）。

### 4.7 公共工具（`core/m_*`）
定点数（`m_fixed`）、伪随机（`m_random`）、配置（`m_misc`）、命令行参数（`m_argv`）。

## 5. 分层开发路线

每阶段产出可运行、可验证的里程碑。

| 阶段 | 内容 | 里程碑 |
|---|---|---|
| **P0 · 地基** | Git/CMake/SDL2/开窗/输入；`m_fixed`、`m_random`、日志、错误处理 | 窗口显示纯色，ESC 关闭 |
| **P1 · WAD 加载** | WAD 解析、lump 缓存 | 打开 freedoom.wad，列出所有 lump |
| **P2 · 地图 + 首次 3D**① | 解析地图 lump；2D 自动地图；BSP + seg 按列渲染墙（纯色） | 在伪 3D 看到 E1M1/MAP01 墙轮廓，能走动 |
| **P3 · 贴图 + 地板天花板 + 碰撞** | 图片格式、贴图管理；`r_plane`；玩家移动 + 碰撞 | 走在完整贴图关卡，有碰撞 |
| **P4 · 精灵**② | `r_things`、深度排序、拾取物品 | 看到敌人/物品，能拾取 |
| **P5 · 战斗 + HUD** | 开火、武器动画、伤害结算、状态栏 | 射击敌人，敌人死亡，完整 HUD |
| **P6 · 敌人 AI + 关卡机关**（大头） | `p_enemy` 状态机、内讧、视线；门/电梯/开关/传送/钥匙门 | 敌人追击攻击，机关可用，打通一关 |
| **P7 · 流程 + 音频 + 存档** | 关卡推进、过场、菜单、音效/音乐、存档 | 单人战役带声音从头玩到尾 |
| **P8 · 联机（可选/延后）** | `i_net` 死斗/合作 | 多人对战 |

工期预期：完整复刻是数月级工程。P0–P3（在贴图关卡走动）相对快达成，最有成就感；P5–P7 是代码主体。

## 6. 项目结构

```
doomcpp/
├── CMakeLists.txt
├── README.md
├── LICENSE                 # GPL-3.0
├── .gitignore
├── docs/superpowers/specs/ # 设计文档
├── assets/                 # freedoom.wad 放这（不入库）
├── cmake/                  # FindSDL2 等 helper
├── src/
│   ├── core/               # d_main, g_game, m_fixed, m_random, m_argv, m_misc
│   ├── platform/           # i_video, i_input, i_sound, i_net
│   ├── wad/                # w_wad, lump 缓存, 图片/贴图格式
│   ├── render/             # r_main/bsp/segs/plane/things/draw/data
│   ├── play/               # p_setup/map/tick/mobj/enemy/user/inter/spec/sight...
│   ├── ui/                 # st_lib, am_map, wi_stuff, hu_stuff, f_finale, m_menu
│   ├── sound/              # s_sound
│   └── main.cpp
└── tests/                  # 纯逻辑单测（doctest）
```

## 7. 仓库名与 Git 策略

- **仓库名**: `doomcpp`（短、好记、无歧义、好搜）
- **分支**: `main`（每阶段末稳定可跑）+ 每阶段功能分支（如 `feat/p1-wad-loading`）
- **提交规范**: Conventional Commits（`feat:`/`fix:`/`docs:`/`refactor:`/`test:`），一个逻辑单元一次提交
- **里程碑标签**: 每阶段末打 tag（如 `v0.3-textured-walking`）
- **.gitignore**: `build/`、`*.wad`、IDE/编辑器文件、CMake 缓存

## 8. 法务与许可

本项目移植 1997 年 GPL 开源的原版 DOOM 源码，是其派生作品，**许可证必须为 GPL-3.0**。Freedoom 资源为独立许可（BSD-style），WAD 文件不入库，由用户自行下载并在 README 给出获取说明。

## 9. 测试策略

- **纯逻辑**：doctest 单测覆盖定点数运算、WAD 解析、碰撞检测函数、伪随机序列确定性。
- **渲染/游戏逻辑**：靠每阶段可视化里程碑 + 截图与 Chocolate Doom 对比验证。
