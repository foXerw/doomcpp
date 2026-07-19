#pragma once
#include <cstdint>

// Engine-wide scalar types. fixed_t is 16.16 fixed-point.
using int32  = std::int32_t;
using uint32 = std::uint32_t;
using byte   = std::uint8_t;
using boolean = int;

using fixed_t = std::int32_t;

inline constexpr fixed_t FRACUNIT  = 1 << 16;
inline constexpr int     FRACBITS  = 16;
