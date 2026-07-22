# doomcpp

A faithful, from-scratch reimplementation of the classic DOOM (1993) engine in
modern C++17, using SDL2 for windowing/input/audio and software rendering
matching the original renderer's algorithms.

**Status:** P3c — player collision (BLOCKMAP line collision + stairstep slide; can't walk through walls), eye-height follows the current sector's floor (smooth), 35 Hz tic clock (flat animation decoupled from refresh rate).

## Roadmap

- [x] P0 基础 (scaffold, fixed-point math, RNG, SDL2 window)
- [x] P1 WAD 加载 (header + lump directory, cached lump access)
- [x] P2a 地图加载 + 2D 自动地图 (vertexes/linedefs, player movement)
- [x] P2b BSP 3D 墙渲染 (front-to-back, per-column occlusion)
- [x] P3a 贴图墙 (PLAYPAL/PNAMES/patch decode, TEXTURE1/2 composite, perspective-correct U/V)
- [x] P3b Visplane 地板天花板 (flats F_START/F_END, R_FindPlane/R_CheckPlane/R_PlaneSpans, float yslope/distscale, F_SKY1 sky, animated flats, distance shade)
- [x] P3c BLOCKMAP 碰撞 + 眼高随 sector floor (BLOCKMAP line collision + stairstep slide, smooth eye-z, 35Hz tic decouple, floor-sky)

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
