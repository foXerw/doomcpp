# doomcpp

A faithful, from-scratch reimplementation of the classic DOOM (1993) engine in
modern C++17, using SDL2 for windowing/input/audio and software rendering
matching the original renderer's algorithms.

**Status:** P7 — the single-player campaign is complete (branch `feat/p6b-enemy-ai`,
tag `v0.14-p7-campaign`). The game now runs E1M1 → E4M8 with all monsters used by
Freedoom's four episodes, monsters that wake/chase/attack/infight and open doors,
full weapon combat, **level progression with intermission tallies and secret
counts**, **sound effects and MIDI music**, and **quick save/load**.

Highlights of the final phase:

- **P6b enemy AI** (`play/p_enemy`): sight (`P_CheckSight`), wake-up (`A_Look`),
  chase movement with wall detours (`A_Chase`), monster melee/hitscan/projectile
  attacks, lost-soul charges, infighting (damage retargets), pain/death sounds,
  boss death handling (tag-666 floors for E1M8/E4M8, direct level end for E2M8/E3M8).
  Monsters: possessed, shotgun guy, imp, demon, spectre (`MF_SHADOW` 50% fuzz
  blend), lost soul, cacodemon, baron, cyberdemon, spider mastermind — plus imp/
  caco/baron fireballs and cyber rockets.
- **P7 campaign flow**: every exit line works (normal/secret/boss), `E#M#` sequencing
  (M9 secret levels return to M4, M8 advances the episode, E4M8 ends the game),
  inventory carries across levels, intermission screen with kills/items/secrets
  percentages, death screen + Enter restart, campaign-complete screen.
- **P7 audio**: all 67 Freedoom sound effects (8-channel SDL mixer, Doom DMX
  headers) and level/intermission/victory music (Standard MIDI parser + a small
  sine/octave synth; Freedoom ships MIDI, not MUS).
- **P7 save/load**: F5/F9 quick save to `doomcpp-quicksave.dat` — player inventory,
  mobjs (including live projectiles and target pointers), movers, sector heights,
  secrets.
- **Renderer fix**: sprite occlusion is now a per-pixel depth buffer instead of a
  per-column nearest-wall depth. The old array let a low floor riser at 32 units
  hide every sprite in the room (the P4 window/riser limitation); sprites behind
  steps, windows and grates now draw correctly.
- **Player feel**: vanilla-style thrust + friction momentum, wall sliding, view bob,
  Shift to run, knockback.

Earlier milestones: P6a doors/lifts/floors/ceilings/teleports/switches (`v0.13`),
P5b combat + HUD, P5a thinkers/mobj physics, P4 sprites, P3.5 fixed-point collision,
P3.5d render-coverage/texture-orientation fixes, P3c BLOCKMAP collision, P3b
visplanes, P3a textures, P2b BSP, P2a map loading, P1 WAD, P0 foundation. The
renderer (`r_bsp`/`r_plane`/`r_things`) stays **float** (doomcpp's own from-scratch
code; a documented permanent deviation — collision/play code is 16.16 fixed-point).

## Controls

| Key | Action |
| --- | --- |
| WASD / arrows | Move / turn |
| Shift | Run |
| Space | Use (doors, switches, exits) |
| Ctrl | Fire |
| 1–8 | Select weapon (fist … BFG) |
| G | Toss a barrel (debug/physics fun) |
| F5 / F9 | Quick save / quick load |
| Enter | Continue (intermission / death / victory) |
| ESC | Quit |

`--warp E#M#` starts on any level; `--nosound` disables audio.

## Roadmap

- [x] P0 基础 (scaffold, fixed-point math, RNG, SDL2 window)
- [x] P1 WAD 加载 (header + lump directory, cached lump access)
- [x] P2a 地图加载 + 2D 自动地图
- [x] P2b BSP 3D 墙渲染
- [x] P3a 贴图墙 (PLAYPAL/PNAMES/patch, TEXTURE1/2 composite)
- [x] P3b Visplane 地板天花板 (flats, sky, animation, distance shade)
- [x] P3c BLOCKMAP 碰撞 + 眼高
- [x] P3d 修复 (horizon seam, sky fill, stair eye-height, debug tooling)
- [x] P3.5 碰撞定点 + 渲染器复刻 (BAM trig tables, fixed-point p_map/p_maputl)
- [x] P4 精灵 (SpriteCache, projection, masked draw, rotation/mirror)
- [x] P5a 思考者/mobj 系统（活对象 + 物理 + 抛桶）
- [x] P3.5d 渲染覆盖修复 (upper-wall condition, V orientation, plane-band gaps)
- [x] P5b 战斗 + HUD (weapon state machine, hitscan/projectiles, damage, pickups, STBAR) — `v0.12`
- [x] P6a 关卡机关 (doors/lifts/floors/ceilings/teleports/switches, use/walk triggers, slide) — `v0.13`
- [x] P6b 敌人 AI (sight/look/chase, all DOOM 1 monsters, attacks, infighting, boss deaths, monster doors)
- [x] P7 流程 + 音频 + 存档 (campaign sequencing, intermission, SFX + MIDI music, quick save/load) — `v0.14`

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

Run unit tests (ctest also registers the runtime/AI/campaign/sound smoke tests
when `assets/freedoom1.wad` exists):

```bash
ctest --test-dir build --build-config Release --output-on-failure
```

## Diagnostics

```bash
# Runtime smoke: init → movement → toss physics → weapon → AI wake → door → render
./build/Release/doomcpp.exe --smoketest assets/freedoom1.wad

# Every monster family on the real E1M1 map: wake, chase, attacks, projectiles
./build/Release/doomcpp.exe --aittest assets/freedoom1.wad

# Load+spawn+tick all 36 E1–E4 maps, exit lines, campaign chain, save/load round-trip
./build/Release/doomcpp.exe --campaigntest assets/freedoom1.wad

# Decode every DS lump, parse every MIDI track, render music synth RMS, play sounds
./build/Release/doomcpp.exe --soundtest assets/freedoom1.wad

# Render one frame to BMP (convert BMP→PNG with System.Drawing to view)
./build/Release/doomcpp.exe --dumpframe-map assets/freedoom1.wad E1M2 out.bmp [angle] [x y]
./build/Release/doomcpp.exe --dumpmobs assets/freedoom1.wad prefix_ 150   # one frame per monster
./build/Release/doomcpp.exe --dumpintermission assets/freedoom1.wad out.bmp

# Renderer coverage (geometry=0 means no true holes; raw= are masked texels)
./build/Release/doomcpp.exe --gapscan assets/freedoom1.wad [angleDeg] [x y]
./build/Release/doomcpp.exe --dumptex assets/freedoom1.wad AQTRIM05 out.bmp
./build/Release/doomcpp.exe --dumppatch assets/freedoom1.wad STBAR out.bmp
```

## Assets

The engine reads standard WAD files. For development/testing use
[Freedoom](https://freedoom.github.io/) (free IWAD). Place `freedoom1.wad` in
`assets/`. WADs are not shipped with this repo.

## Known limitations

- Music is played by a small built-in synth from Freedoom's MIDI lumps, not an
  OPL/DMX-faithful synthesizer. Melodies and timing are faithful; timbre is not.
- No menus/title screens; the game starts directly on E1M1 (or `--warp`), and
  death/intermission continue with Enter.
- Status-bar face expressions are static (no pain/grin tiers).
- Intermission uses the status font on a black screen rather than the WI* graphics.
- Monster pathing is a simplified direct chase with ±45°/±90° detours (no full
  vanilla `P_NewChaseDir`); monsters open doors when blocked.
- Crushers, light-flicker sector specials and teleport fog are not implemented;
  stairs raise each tagged sector by 8 units.
- Vertical aiming is auto-aim only (no mouse look), matching vanilla.
- The renderer remains float (documented; collision/play code is fixed-point).

## License

GPL-3.0. This engine is a derivative work of the GPL'd DOOM source released by
id Software in 1997. See [LICENSE](LICENSE).
