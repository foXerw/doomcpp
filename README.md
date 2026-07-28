# doomcpp

A faithful, from-scratch reimplementation of the classic DOOM (1993) engine in
modern C++17, using SDL2 for windowing/input/audio and software rendering
matching the original renderer's algorithms.

**Status:** P5a — thinker/mobj system: a live `mobj_t` list driven by a per-tic thinker chain (`P_RunThinkers` → `P_MobjThinker` applies momentum/friction/gravity via `P_XYMovement`/`P_ZMovement`); the player is a `mobj_t` (`World::player.mo`) routed through the same generalized `P_TryMove` collision, and the renderer reads live mobj state each frame (`P_BuildThingsBySubsec` rebuilds the per-subsector render bins). Visible milestone: press **G** in-game to toss an `MT_BARREL` out the player's front that arcs up, lands, slides under friction, and stops against walls; `--tickdump <wad> <bmp> <n> [x y vx vy vz]` visualizes the physics headlessly (spawns a barrel, runs N thinker tics, prints z/momz/x before/after, writes a frame). Earlier: P4 sprite rendering (`render/r_things`: `SpriteCache` decodes `S_START`..`S_END` lumps indexed by sprite/frame/8-way-rotation with mirror; `R_ProjectSprite` + `R_DrawVisSprite` per-column masked draw + distance shade + flip; drawn back-to-front after visplanes, occluded by nearer walls via a per-column `wallDepth` array). The renderer (`r_bsp`/`r_plane`/`r_things`) stays **float** (doomcpp's own from-scratch code; a documented permanent deviation — the collision/play code is 16.16 fixed-point). Still earlier: P3.5 collision/movement fixed-point + `FixedSqrt`; pit HOM band fix (lower-wall/bottomtexture condition was inverted vs vanilla).

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

## Assets

The engine reads standard WAD files. For development/testing use
[Freedoom](https://freedoom.github.io/) (free IWAD). Place `freedoom1.wad` in
`assets/`. WADs are not shipped with this repo.

## License

GPL-3.0. This engine is a derivative work of the GPL'd DOOM source released by
id Software in 1997. See [LICENSE](LICENSE).
