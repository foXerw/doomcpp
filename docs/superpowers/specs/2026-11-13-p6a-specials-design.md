# doomcpp P6a 关卡机关 + 移动/碰撞回填 — 设计文档

- **日期**: 2026-11-13（P5b 合并 `v0.12` 后继续）
- **前置**: P5b 战斗+HUD（`v0.12-p5b-combat`）
- **目标 tag**: `v0.13-p6a-specials`

## 1. 目标

让地图“活”起来：**门（手动/远程/钥匙）、电梯/升降台、地板/天花板升降、楼梯、开关贴图、传送**
可用；并把 P5a 留下的移动/碰撞简化回填到接近 vanilla：

- 门必须能开/关/等待；钥匙门拒绝无钥匙玩家；开关贴图 SW1→SW2 切换。
- 电梯/升降台把玩家和怪物一起带走。
- 传送把玩家送到 tagged 目标。
- 玩家能沿墙滑动（全 P_SlideMove），thing 之间互相阻挡。

## 2. 范围

1. **Sector mover 系统**（`play/p_spec`）：`World.movers`，每 tic `P_RunMovers` 更新
   door / lift(plat) / floor / ceiling；到达目标后等待/返回；高度变化时同步该 sector 内
   mobj 的 floorz/ceilingz/z（电梯带人）。
   - Door：ceiling 在 floor..top 间移动；打开→等待→关闭（手动门/远程门）；W1 门常开。
   - Lift/Plat：floor 在 low..high 间移动，等待后返回。
   - Floor/Ceiling：移动到目标高度后停止；楼梯按 tag 逐 sector 递增。
2. **Line specials**（`play/p_spec`）：`P_ActivateLine` 按 special 分派；按键盘 Space 触发
   `P_UseLines`（64 单位内最近的可触发线）；移动路径上穿越 W1/WR 线触发 `P_CrossSpecialLine`。
   实现 Doom 常用 special 子集：
   - 门：1,2,3,4,26/27/28,31/32/33/34,117/118,133/135/137
   - 升降台：62,88,120,121
   - 地板：58,92,119,130,131,23,82,38,19,55,101,102；楼梯 7,8,100,127
   - 天花板：40,41,43,44
   - 传送：39,97（目标 = `THINGS` 中 type 14 且 sector tag 匹配）
   - 开关/灯光：104-107 等（切换贴图 + 改 sector light）
   - 退出：11,51,52（P6a 只记录，不切关）
3. **开关贴图**：激活后把该 sidedef 的 `SW1xxx` 贴图名改成 `SW2xxx`（直接改字符串；渲染器按名查表）。
4. **钥匙门**：检查 `player.keys` 的蓝/黄/红位；无钥匙则不触发。
5. **传送**：玩家移动到目标点；保留角度（vanilla 保留视角）或对准目标角度（简化：保留）；
   清除动量。
6. **碰撞回填**：
   - `P_CheckPosition` 增加 thing↔thing 阻挡（MF_SOLID / MF_SPECIAL 重叠处理）。
   - `P_SlideMove`（最多 3 次，沿阻挡线投影动量）替换 stairstep；玩家/怪物使用。
   - 玩家移动仍用 P_MovePlayer 的直接位移（动量/摩擦/视点 bob 留到 P6a-part2，登记）。
7. **输入**：Space = use（`Input.use`），边沿触发。

### 范围外（显式延后）
- 全动量/摩擦玩家移动、view bob（P6a-part2 或 P6b 前）。
- 挤压伤害（crusher）、怪物使用门/开关（P6b AI 使用 specials）。
- 关卡切换/结局（P7）、音效（P7）、存档（P7）。
- 复杂灯光闪烁/频闪（P7 视觉收口）。

## 3. 设计决策

- **mover 用数据表 + 每 tic 更新**，不引入通用 thinker 类型：`Mover{SectorKind, sector, speed,
  top, bottom, direction, waitTics, state, stayOpen}`。`P_RunMovers` 在 `P_RunThinkers` 之后调用。
- **高度同步**：`P_MoveSectorHeight` 更新 sector 高度后遍历 mobj（用 subsector→sector），
  调整 floorz/ceilingz；z 低于新 floor → 抬到 floor（电梯带人）；高于 ceiling-height → 压下
  （P6a 无 crush 伤害）。
- **special 分派表**：`struct SpecialDef { int special; Trigger trigger; Action action; int floorStep?; }`
  纯数据；未列出的 special 忽略（P6b/P7 再补）。
- **UseLines**：不用 blockmap 线迭代的精确 vanilla intercept，而是遍历玩家周围 64 单位内的线、
  取最近且玩家在其前方的线（几何足够）。登记为简化。
- **CrossSpecialLine**：用移动前后线段与附近 linedef 的 2D 相交判定；不维护 spechit 数组。
  同一 special 每 tic 最多触发一次（按玩家移动段事实触发）。
- **测试**：合成 sector/line 直接调用 `P_ActivateLine`/`P_RunMovers`/`P_CrossSpecialLine`；
  门/电梯高度时序、钥匙拒绝、传送、开关贴图、thing 阻挡、SlideMove 不穿墙。

## 4. 文件

| 文件 | 动作 | 内容 |
|---|---|---|
| `play/p_spec.{h,cpp}` | 新 | Mover、P_RunMovers、P_StartDoor/Plat/Floor/Ceiling、P_ActivateLine、P_UseLines、P_CrossSpecialLine、开关贴图、传送 |
| `play/p_tick.{h,cpp}` | 改 | `World.movers` + `P_RunMovers` 接入 tick |
| `play/p_map.{h,cpp}` | 改 | PosCheck.blockedLine、thing↔thing 阻挡、P_SlideMove |
| `play/p_mobj.cpp` | 改 | P_XYMovement 使用 P_SlideMove（MF_SLIDE / 玩家/怪物） |
| `play/info.h` | 改 | MF_SLIDE 已存在；玩家/怪物 flags 加 MF_SLIDE |
| `platform/i_video.{h,cpp}` | 改 | `Input.use`（Space，边沿） |
| `src/main.cpp` | 改 | 每 tic 调 P_RunMovers + P_UseLines（use 边沿） |
| `tests/test_p_spec.cpp` | 新 | 门/电梯/钥匙/传送/开关/地板/楼梯/穿越触发 |
| `tests/test_p_map.cpp` | 扩 | thing 阻挡、SlideMove |

## 5. 验收
1. 单测：门开/关/等待时序；钥匙门拒绝/通过；电梯带人；传送；开关贴图改名；地板/楼梯；
   thing 阻挡；SlideMove 沿墙不穿。
2. 运行时：E1M1 用 Space 开门/按开关，电梯可乘；无钥匙门不开；传送可到目标。
3. `--smoketest` 扩展：开门前后 ceilingheight 变化；钥匙门拒绝。
4. 用户实机验收；gapscan geometry 不回退。

## 6. 已知简化
- 玩家动量/摩擦/view bob 延后（P6a-part2）。
- UseLines 用几何最近线而非 vanilla intercept；穿越触发按移动段相交。
- 无挤压伤害；灯光效果只做 104-107 的亮度切换；无音效。
- 怪物不会主动开门/按开关（P6b）。
