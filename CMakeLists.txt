cmake_minimum_required(VERSION 3.20)
# CMake 4.x dropped compatibility for cmake_minimum_required < 3.5; doctest
# (latest, 2.4.11) still declares an older floor, so allow it.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(doomcpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core engine library (types, math, RNG, error/log) -- no SDL, unit-testable.
add_library(doomcore STATIC
    src/core/i_system.cpp
    src/core/m_fixed.cpp
    src/core/m_random.cpp
    src/core/m_tables.cpp
    src/wad/wadfile.cpp
    src/play/p_setup.cpp
    src/play/p_maputl.cpp
    src/play/p_map.cpp
    src/play/p_mobj.cpp
    src/play/p_tick.cpp
    src/play/p_pspr.cpp
    src/play/p_user.cpp
    src/play/p_inter.cpp
    src/play/p_enemy.cpp
    src/play/p_spec.cpp
    src/play/g_game.cpp
    src/play/sfx.cpp
    src/play/g_save.cpp
    src/play/midi.cpp
    src/play/info.cpp
    src/render/r_draw.cpp
    src/render/r_data.cpp
    src/render/r_plane.cpp
    src/render/r_things.cpp
    src/render/r_bsp.cpp
    src/ui/st_stuff.cpp
    src/ui/f_font.cpp
    src/ui/f_intermission.cpp
)
target_include_directories(doomcore PUBLIC src)

# Platform layer: SDL2 windowing + framebuffer presentation.
find_package(SDL2 CONFIG REQUIRED)
add_library(doomplatform STATIC
    src/platform/i_video.cpp
    src/platform/i_sound.cpp
)
target_include_directories(doomplatform PUBLIC src)
target_link_libraries(doomplatform PUBLIC SDL2::SDL2 ${CMAKE_DL_LIBS})

add_executable(doomcpp src/main.cpp)
target_link_libraries(doomcpp PRIVATE doomcore doomplatform)

include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
)
FetchContent_MakeAvailable(doctest)
# doctest ships a CMake helper (doctest.cmake) for test discovery.
list(APPEND CMAKE_MODULE_PATH ${doctest_SOURCE_DIR}/scripts/cmake)

enable_testing()
add_subdirectory(tests)

# Runtime smoke: exercises the real init/movement/toss/render path (catches missing
# main-path init calls that unit tests self-initialize away). Only registered when the
# Freedoom IWAD is present, since assets/ is gitignored.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/freedoom1.wad)
    add_test(NAME runtime_smoke
             COMMAND $<TARGET_FILE:doomcpp> --smoketest ${CMAKE_CURRENT_SOURCE_DIR}/assets/freedoom1.wad)
    add_test(NAME ai_smoke
             COMMAND $<TARGET_FILE:doomcpp> --aittest ${CMAKE_CURRENT_SOURCE_DIR}/assets/freedoom1.wad)
    add_test(NAME campaign_smoke
             COMMAND $<TARGET_FILE:doomcpp> --campaigntest ${CMAKE_CURRENT_SOURCE_DIR}/assets/freedoom1.wad)
    add_test(NAME sound_smoke
             COMMAND $<TARGET_FILE:doomcpp> --soundtest ${CMAKE_CURRENT_SOURCE_DIR}/assets/freedoom1.wad)
    # P8a render coverage: every map, camera poses taken from the map's own things,
    # counting pixels no wall and no visplane covered. --poses 40 --angles 6 samples
    # ~9.6k frames in a few seconds. Tolerance 2 px/frame: two adjacent segs at slightly
    # different depths can project a shared world height to neighbouring rows and leave a
    # 1px seam (the shipped renderer seam-fills it, so the player never sees it); anything
    # larger is a real coverage hole — the ones this caught were 105..23400 px.
    add_test(NAME render_audit
             COMMAND $<TARGET_FILE:doomcpp> --renderaudit ${CMAKE_CURRENT_SOURCE_DIR}/assets/freedoom1.wad
                     --poses 40 --angles 6 --tolerance 2)
    # P8a clipping audit: seed a short deterministic walk from mapper-placed spots in every
    # map and re-test the positions the movement code produced (inside a blocking line? does
    # the player still fit? are the feet on the support?). Gate settings are the fast ones;
    # a deep run is `--clipaudit <wad>` with no caps (~2 min, ~3x the sector coverage).
    add_test(NAME clip_audit
             COMMAND $<TARGET_FILE:doomcpp> --clipaudit ${CMAKE_CURRENT_SOURCE_DIR}/assets/freedoom1.wad
                     --seeds 25 --steps 150 --every 4)
endif()
