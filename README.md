# doomcpp

A faithful, from-scratch reimplementation of the classic DOOM (1993) engine in
modern C++17, using SDL2 for windowing/input/audio and software rendering
matching the original renderer's algorithms.

**Status:** P6a — level specials (branch `feat/p6a-specials`, pending in-game acceptance): sector movers for **doors / lifts / floors / ceilings** (`World::movers`, `P_RunMovers`: move → wait → return; sector heights are carried onto every mobj), line specials for manual/remote/**keyed doors**, lifts, floor raises/lowers/stairs, ceiling moves, **teleporters**, switches (`SW1`→`SW2` texture swap) and exits; **Space** uses the nearest line within 64 units; W1/WR walk-over triggers fire while moving. Collision backfill: thing↔thing blocking (solid mobjs), `P_SlideMove` along the blocking line, and a World-aware movement path. Earlier: P5b combat + HUD (weapon state machine, hitscan/projectiles, damage/armor/death, pickups, first-person weapon sprite, STBAR status bar over a 320x168 view); P5a thinker/mobj physics; P4 sprites; P3.5 fixed-point collision; P3.5d coverage/texture-orientation fixes. Controls: **WASD/arrows**, **Space use**, **Ctrl fire**, **1–8 weapons**, **G** toss a barrel, **ESC** quit. Earlier: P4 sprite rendering (`render/r_things`: `SpriteCache` decodes `S_START`..`S_END` lumps indexed by sprite/frame/8-way-rotation with mirror; `R_ProjectSprite` + `R_DrawVisSprite` per-column masked draw + distance shade + flip; drawn back-to-front after visplanes, occluded by nearer walls via a per-column `wallDepth` array). The renderer (`r_bsp`/`r_plane`/`r_things`) stays **float** (doomcpp's own from-scratch code; a documented permanent deviation — the collision/play code is 16.16 fixed-point). Still earlier: P3.5 collision/movement fixed-point + `FixedSqrt`; pit HOM band fix (lower-wall/bottomtexture condition was inverted vs vanilla).

## Roadmap

- [x] P0 基础 (scaffold, fixed-point math, RNG, SDL2 window)
- [x] P1 WAD 加载 (header + lump directory, cached lump access)
- [x] P2a 地图加载 + 2D 自动地图 (vertexes/linedefs, player movement)
- [x] P2b BSP 3D 墙渲染 (front-to-back, per-column occlusion)
- [x] P3a 贴图墙 (PLAYPAL/PNAMES/patch decode, TEXTURE1/2 composite, perspective-correct U/V)
- [x] P3b Visplane 地板天花板 (flats F_START/F_END, R_FindPlane/R_CheckPlane/R_PlaneSpans, float yslope/distscale, F_SKY1 sky, animated flats, distance shade)
- [x] P3c BLOCKMAP 碰撞 + 眼高随 sector floor (BLOCKMAP line collision + stairstep slide, smooth eye-z, 35Hz tic decouple, floor-sky)
- [x] P3d 修复 (horizon-row seam + sky-fill-to-horizon + floorz stair-climb/eye-height; `--mapinfo`/`--sample`/`--dumpframe` teleport debug tooling)
- [x] P3.5 碰撞定点 + 渲染器复刻(float) (angle_t/BAM + finesine/finecosine trig tables, node_t→fixed_t, p_map/p_maputl fixed-point collision + movement; renderer stays float as doomcpp's own reimplementation; FixedSqrt; pit HOM fix)
- [x] P4 精灵 (r_things: SpriteCache S_START/S_END decode + install/resolve/mirror, SpriteDef doomednum→sprite table, render-only mobj_t, R_ProjectSprite float projection + atan2 8-way rotation, R_DrawVisSprite per-column masked draw + distance shade + flip, per-column wallDepth wall occlusion, drawn back-to-front after visplanes)
- [x] P5a 思考者/mobj 系统（活对象 + 物理 + 抛桶里程碑）
- [x] P3.5d 渲染覆盖修复 (upper-wall 条件反转、markCeil/markFloor 单向判断、墙面 V 坐标上下翻转、平面带取整缝隙；`--gapscan`/`--dumptex` 诊断工具)
- [x] P5b 战斗 + HUD (psprite 武器状态机 + 弹药/切枪、hitscan P_LineAttack + auto-aim、投射物与 P_RadiusAttack、P_DamageMobj/护甲/pain/death/corpse、物品拾取、武器视图精灵、STBAR 状态栏) — merged `v0.12-p5b-combat`
- [x] P6a 关卡机关 (sector movers 门/升降台/地板/天花板、line specials 手动/远程/钥匙门、电梯、楼梯、传送、SW1→SW2 开关、Space use、W1/WR 穿越触发；thing↔thing 阻挡 + P_SlideMove；待实机验收)

## Build (Windows)

Requires a C++17 compiler (MSVC via Visual Studio 2022), CMake >= 3.16, and
[vcpkg](https://github.com/microsoft/vcpkg) for SDL2.

```bash
# one-time vcpkg bootstrap
git clone https://github.com/microsoft/vcpkg.git C:/vcpkg
C:/vcpkg/bootstrap-vcpkg.bat
C:/vcpkg/vcpkg install sdl2

# configure + build (MSVC via the VS generator)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Run unit tests:

```bash
ctest --test-dir build --build-config Release --output-on-failure
```

Runtime smoke test (real init → movement → turn → toss physics → render; exit code 1 on failure;
also registered as the `runtime_smoke` CTest when `assets/freedoom1.wad` exists):

```bash
./build/Release/doomcpp.exe --smoketest assets/freedoom1.wad
```

Renderer coverage diagnostic (renders a magenta sentinel frame; `geometry=` must be 0 — any
remaining `raw=` pixels are masked/transparent texture texels, which are legitimately black):

```bash
./build/Release/doomcpp.exe --gapscan assets/freedoom1.wad [angleDeg] [x y]
./build/Release/doomcpp.exe --dumptex assets/freedoom1.wad AQTRIM05 out.bmp   # alpha layout
./build/Release/doomcpp.exe --dumppatch assets/freedoom1.wad STBAR out.bmp    # HUD patch
```

## Assets

The engine reads standard WAD files. For development/testing use
[Freedoom](https://freedoom.github.io/) (free IWAD). Place `freedoom1.wad` in
`assets/`. WADs are not shipped with this repo.

## License

GPL-3.0. This engine is a derivative work of the GPL'd DOOM source released by
id Software in 1997. See [LICENSE](LICENSE).
