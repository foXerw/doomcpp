# P6a 关卡机关 + 移动/碰撞回填 — 实现计划

- 分支：`feat/p6a-specials`
- 设计：`docs/superpowers/specs/2026-11-13-p6a-specials-design.md`
- TDD：每个 Task 先加断言（合成 sector/line/blockmap，不依赖 IWAD），再实现，跑全量 ctest。

## Task 0 — 基础结构 ✅
- `World::movers` + `MoverKind/Mover`（door/plat/floor/ceiling）；`World::map` 改为可写
  （mover 需要修改 sector 高度）。
- `P_RunThinkers` 末尾调用 `P_RunMovers`（vanilla 把 movers 放在同一 thinker 列表的等价做法）。

## Task 1 — Mover 运行时 ✅
- `P_RunMovers`：state 0→top / 1 wait / 2→bottom / 3 done；door/plat 等待后返回；
  floor/ceiling 到目标即停。
- `syncSectorThings`：高度变化时同步该 sector 内 mobj 的 floorz/ceilingz/z（电梯带人）。
- 高度目标：门 = min(邻近 ceiling) - 4；lift/floor up = 下一个更高邻接地板；floor down = 下一个更低；
  ceiling raise/lower = 邻近 ceiling/floor。
- 测试：门开/等待/关时序；lift 上升/等待/返回。

## Task 2 — Line specials ✅
- `P_ActivateLine`：门（1/2/3/4/26-28/31-34/63/64/90/91/103/117/118/133/135/137）、
  lift（62/88/120）、floor raise/lower（58/92/119/130/131/66/71/55/101/102/23/82/38/19/67）、
  stairs（7/8/100/127，简化 +8）、ceiling（40/41/43/44/69/70/72）、teleport（39/97）、
  exit（11/51/52，记录不切关）。
- 钥匙门：26/32/133 蓝、27/33/137 黄、28/34/135 红；无钥匙拒绝。
- 开关贴图：use 触发时把该 sidedef 的 SW1xxx 改成 SW2xxx。
- 测试：钥匙拒绝/通过、switch 贴图、teleport、walk 触发（P_CrossSpecialLines）。

## Task 3 — Use / 穿越触发 ✅
- `P_UseLines`：沿玩家朝向 64 单位射线取最近的可 use 线（几何简化，非 intercept）。
- `P_CrossSpecialLines`：移动段与 W1/WR 线相交触发。
- `Input.use`（Space，边沿）；main 每 tic 处理。

## Task 4 — 碰撞回填 ✅
- `PosCheck.blockedLine`；`P_CheckPosition(World)` 增加 thing↔thing 阻挡（MF_SOLID）。
- `P_SlideMove`：最多 3 次，沿阻挡线投影（P_TryMove(World)）。
- `P_MovePlayerWorld`：游戏主循环使用 slide + 穿越触发；旧 `P_MovePlayer` 保留给既有测试。
- 测试：barrel 阻挡玩家；对角线撞墙滑动不穿墙。

## Task 5 — 收口（待实机验收）
- `--smoketest` 增加「use 激活门 → ceiling 上升」。
- 全量 ctest + gapscan；README/spec/memory；用户实机（E1M1 开门/开关/电梯/传送）。
- 合并 `--no-ff` → main，tag `v0.13-p6a-specials`。

## 后续（P6a-part2 / P6b）
- 玩家动量/摩擦/view bob；crusher；灯光闪烁；怪物使用 specials；P6b AI/视线/内讧。
