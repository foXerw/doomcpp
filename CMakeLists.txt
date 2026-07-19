cmake_minimum_required(VERSION 3.20)
# CMake 4.x dropped compatibility for cmake_minimum_required < 3.5; doctest
# (latest, 2.4.11) still declares an older floor, so allow it.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(doomcpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(doomcpp src/main.cpp)
target_include_directories(doomcpp PRIVATE src)

if(MSVC)
    target_compile_options(doomcpp PRIVATE /W4)
else()
    target_compile_options(doomcpp PRIVATE -Wall -Wextra -Wpedantic)
endif()

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
