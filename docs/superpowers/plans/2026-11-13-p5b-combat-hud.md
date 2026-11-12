# P5b 战斗 + HUD — 实现计划

- 分支：`feat/p5b-combat`（Task 0-9 已完成；135/135 tests，--smoketest 通过；待 Task 10 实机验收）
- 设计：`docs/superpowers/specs/2026-11-13-p5b-combat-hud-design.md`
- 全流程 TDD：每个 Task 先加断言（合成地图/合成纹理，不依赖 IWAD），再实现，再跑全量 ctest。

## Task 0 — groundwork ✅ 已完成（7cbc278）
- `mobj_t::removePending` + `World::thinkering`，thinker pass 内延迟删除。
- `P_SetMobjState(S_NULL)` 清渲染缓存；info 表行数 `static_assert`。

## Task 1 — info 表扩展 ✅
- 新增 `action_t` 枚举（A_NULL/A_WeaponReady/A_Lower/A_Raise/A_FirePistol/A_FireShotgun/
  A_FireCGun/A_FireMissile/A_FirePlasma/A_FireBFG/A_Saw/A_Punch/A_ReFire/A_Light0/A_Pain/
  A_Scream/A_Fall/A_XScream/A_Look/A_Chase… 仅实现 P5b 所需，其余占位）。
- 追加武器 psprite 状态链、怪物 pain/death/xdeath、puff/blood/explosion、玩家死亡状态。
- 追加 `MT_ROCKET/MT_PLASMA/MT_BFG/MT_PUFF/MT_BLOOD`；补齐现有怪物的 pain/death 等字段。
- 新增 `weaponinfo_t weaponinfo[]`、`ammo_t`/`ammoinfo_t`。
- 测试：`test_info` 增表行数/关键条目（S_PISTOL2 action、MT_ROCKET flags、
  weaponinfo[PISTOL].ammo==bullets、sprnames 全表）。

## Task 2 — 玩家战斗字段与初始装备 ✅
- `player_t` 增 `health/armor/armorclass/ammo[4]/maxammo[4]/weapons/readyweapon/
  pendingweapon/attackdown/usedown/psprites[2]/damagecount/bonuscount`。
- `P_SetupLevel`：满血 100、手枪、50 弹；`P_PlayerThink` 每 tic 递减计时器、推进 psprite。
- 测试：spawn 后字段初值；计时器按 tic 递减；死亡玩家不再开火。

## Task 3 — blockmap thing 迭代 ✅
- `P_BlockThingsIterator(bm, x1,y1,x2,y2, fn)`（按 bbox 覆盖的 cell 去重遍历）。
- 测试：两 cell 内 mobj 都访问一次；越界 cell 跳过；去重。

## Task 4 — hitscan（P_LineAttack） ✅
- 2D 射线与 linedef 最近交点（用 blockmap line 列表；仅 solid/opening 阻挡）。
- 沿射线遍历候选 thing：射线-圆相交 t + 垂直 `[z, z+height]` 区间；取最近。
- 命中墙 → `P_SpawnPuff`；命中 thing → `P_DamageMobj` + `P_SpawnBlood`。
- 测试：直线命中前方 thing；墙更近时打墙；垂直偏移打空；range 上限。

## Task 5 — 伤害结算 P_DamageMobj/P_KillMobj ✅
- 护甲（green 1/3、blue 1/2）吸收；血量为 0 → deathstate/xdeathstate；`MF_CORPSE`；
  `MF_SHOOTABLE` 检查；knockback（angle/mass）；`M_Random` painchance → painstate；
  玩家死亡状态。
- 测试：无甲扣血精确；有甲按比例；致死进 deathstate；不可射击对象无视；pain 统计不超界。

## Task 6 — 武器状态机 p_pspr ✅
- `P_SetPsprite`/`A_DoAction`；`A_WeaponReady`（可开火）、`A_Lower`/`A_Raise`、
  `A_FirePistol/Shotgun/CGun/Missile/Plasma/BFG/Saw/Punch`、`A_ReFire`、`A_Light0`。
- 弹药检查/扣除；`P_FireWeapon` 冷却由 state 时长驱动；切枪 pendingweapon→raise/lower。
- 测试：开火扣 1 弹并进入 fire state；空弹不开火；切枪时序；refire 保持 attackdown。

## Task 7 — 投射物与爆炸 ✅
- `P_SpawnMissile`（rocket/plasma/BFG）；`MF_MISSILE` 每 tic 沿动量移动，撞墙/thing →
  `P_ExplodeMissile`（`P_RadiusAttack` + 爆炸状态 + 移除）。
- 测试：直线飞行；命中墙产生半径伤害；范围内 thing 扣血；超距不伤。

## Task 8 — 物品拾取 ✅
- `P_TouchSpecialThing`：按 doomednum 表处理 clip/shell/rock/cell/stim/medi/soul/armor/
  武器/钥匙；上限检查；满血/满甲拒绝；成功则 `P_RemoveMobj`。
- 测试：拾取增加弹药/血量到上限；满值不消耗物品；武器位掩码；钥匙位。

## Task 9 — psprite + HUD + 输入 + 接线 ✅
- `R_DrawPSprite`：patch → framebuffer，按 psprite 状态 sx/sy 锚定/缩放。
- `ui/st_stuff`：直接解码 STBAR/STTNUM*/STTPRCNT/STTMINUS/STFST*/STF* 等 lump；
  绘制血/甲/弹药/武器/脸/钥匙；数字右对齐。
- `Input` 增 fire/weapon 键；main：视口 320x168 + 状态栏 32 行；Ctrl 开火、1–7 切枪。
- 测试：状态栏数字 blit 位置；武器掩码显示；HUD 不越界。

## Task 10 — 收口（待用户实机验收后合并/tag）
- `--smoketest` 增「手枪打桶 → 掉血 → 死亡」；gapscan geometry 不回退；全量 ctest。
- README milestone、memory、spec/plan 状态更新；final review（dead code/注释/头文件）。
- 合并 `--no-ff` → main，tag `v0.12-p5b-combat`；用户实机验收。

## 执行顺序
Task 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10。每个 Task 一个 commit。
若时间不足，优先保证 1–6（能开枪打死静止 thing），HUD 可拆到下一次会话。
