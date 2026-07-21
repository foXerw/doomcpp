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
    src/wad/wadfile.cpp
    src/play/p_setup.cpp
    src/render/r_draw.cpp
    src/render/r_data.cpp
    src/render/r_plane.cpp
    src/render/r_bsp.cpp
)
target_include_directories(doomcore PUBLIC src)

# Platform layer: SDL2 windowing + framebuffer presentation.
find_package(SDL2 CONFIG REQUIRED)
add_library(doomplatform STATIC
    src/platform/i_video.cpp
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
