cmake_minimum_required(VERSION 3.16)
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
