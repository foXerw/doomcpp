# doomcpp P5b 战斗 + HUD — 设计文档

- **日期**: 2026-11-13（用户实机确认 P3.5d 修复后，夜间继续实施）
- **前置**: P5a（活 mobj/thinker 物理，`v0.11`）、P3.5d（渲染覆盖/贴图方向，`v0.11.1`）
- **目标 tag**: `v0.12-p5b-combat`

## 1. 背景与目标

P5a 已有：活 `mobj_t`、thinker 链、动量/摩擦/重力、玩家并入 mobj、G 键抛桶、`--smoketest`。
P5b 把「能走能扔桶」推进到「能打」：

**里程碑**：拿枪射击 E1M1 里的静止 thing（桶/敌人）→ 命中判定 → 扣血/死亡 → 尸体；
拾取弹药/血包/护甲会更新 HUD；屏幕底部有完整状态栏（血量/护甲/弹药/武器/脸/钥匙）。
敌人仍然静止（AI 在 P6），但可被击杀。

## 2. 范围

### 范围内
1. **info 表扩展**（P5a 显式延后项）：武器 psprite 状态链、怪物 pain/death/xdeath 状态链、
   投射物/puff/爆炸状态、`weaponinfo[]`、`ammoinfo[]`；`state_t` 增加 `action_t`（只实现 P5b
   需要的武器/怪物动作，其余留 P6）。
2. **玩家状态**：`health/armor/armorclass/ammo[4]/maxammo[4]/weapons(位掩码)/readyweapon/
   pendingweapon/psprites[2]/attackdown/usedown`；`P_SetupLevel` 初始化（100 血、手枪、50 弹）。
3. **`p_pspr` 武器状态机**：raise/lower/fire/refire；`A_WeaponReady/A_Lower/A_Raise/A_Fire*`；
   切枪（数字键）；开火键（Ctrl）与连锁开火。
4. **`p_inter` 伤害结算**：`P_DamageMobj`（护甲吸收、随机 knockback、`M_Random` painchance、
   pain/death/xdeath 状态、尸体 `MF_CORPSE`、怪物计数）、`P_KillMobj`。
5. **命中判定**：`P_BlockThingsIterator`（沿 blockmap 单元遍历 thing）+ `P_LineAttack`
   （2D 最近墙交点 + 沿射线圆相交 + 3D 高度区间；hitscan；墙 puff/血花）、`P_RadiusAttack`
   （火箭/等爆炸）。
6. **武器**：拳头/电锯（近战）、手枪/霰弹/机枪（hitscan）、火箭筒/等离子（投射物）。
   BFG 简化为投射物 + 半径伤害（不做 40 条 tracer，登记偏差，P6/P7 再补）。
7. **投射物**：`MT_ROCKET`/`MT_PLASMA`（`MF_MISSILE` 飞行、撞墙/thing 爆炸、`P_RemoveMobj`）。
8. **物品拾取**：`P_TouchSpecialThing`（弹药/血包/护甲/武器/钥匙）；拾取上限与满血/满甲拒绝。
   超级球/狂暴/无敌等 powerup 延后（P5c/P6，登记）。
9. **HUD `ui/st_stuff`**：从 WAD 直接解码 patch lump（`STBAR/STTNUM*/STTPRCNT/STTMINUS/
   STFST*/STFKILL*` 等），把底部 32 行状态栏画进 framebuffer；3D 视口改 320x168（vanilla）。
10. **输入**：开火（Ctrl）、武器切换（1–7）；`Input` 结构扩展。
11. **武器视图精灵（psprite）**：把当前 psprite 的 patch 按 vanilla 的屏幕锚点/缩放画在 3D 视口上
    （muzzle flash 为第二 psprite）。

### 范围外（显式延后）
- 敌人 AI/视线/巡逻/攻击（P6b）；怪物之间内讧（P6b）。
- 门/开关/电梯/传送/钥匙门的使用逻辑（P6a）。钥匙本阶段只做拾取 + HUD 显示。
- 音效/音乐（P7）；存档/demo（P7）；关卡切换/结局（P7）。
- 玩家死亡后的关卡重开/自动复位（P7）；本阶段死亡只播死亡动画并停住开火。
- powerup（超级球/狂暴/无敌/隐身/夜视）与 BFG tracer（登记偏差）。
- thing↔thing 物理碰撞、全 `P_SlideMove`（P6a）。

## 3. 关键设计决策

- **动作用枚举而非函数指针**：`state_t::action` 存 `action_t`（`A_NULL/A_WeaponReady/
  A_FirePistol/.../A_Pain/A_Scream/...`）。`P_SetPsprite`/`P_SetMobjState` 在状态切换时调用
  `A_DoAction`。P6 往同一个枚举里补 AI action。相比 `void(*)(...)` 更易测试、无 ABI 问题。
- **info 表一次性补齐 P5b 子集**：以 vanilla `info.c` 的表为参考，按名字移植本阶段需要的状态
  与类型；保留现有 `MT_D*` 兼容条目（P4/P5a 测试在用），新类型追加。生成/校核由一次性脚本
  完成（`dbg/` 下，gitignored），提交的是我们自己的 C++ 表 + `static_assert` 行数校验。
- **命中判定几何正确即可，不追求 intercept 逐序等价**：`P_LineAttack` 用「blockmap 内候选
  thing 的射线-圆相交」+「2D 最近墙交点」，取最近者；垂直方向用射线 slope 检查
  `[z, z+height]`。这是 vanilla intercept 体系的可证明等价简化（同样取最近命中），文档登记。
- **HUD 直接 blit patch**：状态栏是 2D 图元，不需要进 3D 渲染器。`ui/st_stuff` 持有
  `StatusBar`（patch 缓存 + 数字/百分比拼装），`ST_Drawer(fb, w, h, World&)`。3D 视口
  `viewH = h - 32`，状态栏占底部 32 行（与 vanilla 320x168 + 32 一致）。
- **定点游戏逻辑**：伤害/弹药/距离全部 `fixed_t`/int；HUD 只做整数 RGBA blit。
- **验证优先**：每个任务先写/扩展 doctest；命中/伤害/拾取用合成地图（blockmap + 单 sector）；
  运行时 `--smoketest` 扩展「对桶开枪 → 桶掉血 → 桶消失」；实机用 Ctrl 打 E1M1 的桶。

## 4. 模块与文件

| 文件 | 动作 | 内容 |
|---|---|---|
| `play/info.{h,cpp}` | 扩 | 新状态/类型/武器/弹药表；`action_t`；`weaponinfo[]`/`ammoinfo[]` |
| `play/p_mobj.{h,cpp}` | 扩 | 状态 action 调度；missile 物理（撞墙→爆炸）；`P_SpawnPuff`/`P_SpawnBlood` |
| `play/p_tick.{h,cpp}` | 扩 | `player_t` 战斗字段；`P_SetupLevel` 初始装备；每 tic `P_PlayerThink`（计时器/psprite 推进） |
| `play/p_pspr.{h,cpp}` | 新 | 武器状态机、开火、切枪、raise/lower、psprite 推进 |
| `play/p_inter.{h,cpp}` | 新 | `P_DamageMobj`/`P_KillMobj`/护甲/knockback/pain/death |
| `play/p_map.{h,cpp}` | 扩 | `P_BlockThingsIterator`、`P_LineAttack`、`P_RadiusAttack`、`P_TouchSpecialThing`、拾取触发 |
| `play/p_user.cpp` | 新 | 玩家每 tic 输入/开火（或并入 p_tick，视实现大小） |
| `ui/st_stuff.{h,cpp}` | 新 | 状态栏 patch 解码/缓存 + 绘制 + 数字拼装 |
| `render/r_things.{h,cpp}` | 扩 | psprite 绘制（`R_DrawPSprite`）复用 patch 解码 |
| `platform/i_video.{h,cpp}` | 扩 | `Input` 增加 fire/weapon 键；键位映射 |
| `src/main.cpp` | 改 | 视口 320x168 + 状态栏；输入→`P_PlayerThink`；`--smoketest` 扩展 |
| `tests/*` | 扩 | `test_p_inter`/`test_p_pspr`/`test_p_attack`/`test_st_stuff`；改 `test_p_tick` |

## 5. info 表扩展（本阶段子集）

- **状态**：`S_PUNCH*`、`S_PISTOL*`、`S_SGUN*`、`S_CHAIN*`、`S_MISSILE*`、`S_SAW*`、`S_PLASMA*`、
  `S_BFG*`（BFG 简化）、`S_LIGHTDONE`、`S_BLOOD1-3`、`S_PUFF1-4`、`S_EXPLODE1-3`、`S_PLSSEE/
  S_PLSEXP`、`S_POSS_*`/`S_SPOS_*`/`S_TROO_*`/`S_SARG_*` 的 stand/run/attack/pain/death/xdeath、
  `S_PLAY_*`（含死亡）；物品沿用现有静态状态（拾取不需要动画）。
- **mobjinfo**：新增 `MT_ROCKET/MT_PLASMA/MT_BFG`、`MT_PUFF/MT_BLOOD`；给现有怪物补
  `seestate/painstate/meleestate/missilestate/deathstate/xdeathstate/painchance/damage`。
- **weaponinfo**：`[fist, chainsaw, pistol, shotgun, chaingun, rocket, plasma, bfg]`：
  `ammotype/upstate/downstate/readystate/atkstate/flashstate`。
- **ammoinfo**：`bullets/shells/cells/rockets` 的 max/box/shell 数量。

## 6. 验收

1. **单测**：命中最近 thing、护甲吸收、painchance 统计、死亡状态链、拾取上限/满血拒绝、
   开火扣弹/冷却/切枪、HUD patch 解码与数字 blit。
2. **运行时 `--smoketest`**：新增「手枪对桶开火 → 桶 health 下降 → 桶被移除/变尸体」；
   仍保留 init/移动/抛桶/渲染检查。
3. **gapscan**：战斗改动不得引入几何空洞（`geometry=0` 不回退）。
4. **实机**：Ctrl 开火；1–7 切枪；打桶/敌人看到血花/死亡；拾取后 HUD 数字变化；状态栏完整。

## 7. 已知偏差（登记）
- BFG 无 40 条 tracer（仅投射物 + 半径伤害）。
- powerup（超级球/狂暴/无敌/隐身/夜视）延后；拾取它们暂时无效果（或忽略）。
- 敌人 AI 仍静止（P6b）；命中静止敌人可致死。
- 命中判定为几何等价简化，不逐 intercept 等价。
- psprite 绘制按 vanilla 屏幕锚点/缩放实现，但不做完整 `R_DrawPSprite` 的遮挡/裁剪细节。
