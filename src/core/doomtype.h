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

// Binary angle measurement (r_main.c). angle_t = uint32; 0 = +X (East),
// increasing counterclockwise toward +Y (North); full circle = 2^32.
using angle_t = std::uint32_t;

inline constexpr angle_t ANG45  = 0x20000000u;
inline constexpr angle_t ANG90  = 0x40000000u;
inline constexpr angle_t ANG180 = 0x80000000u;
inline constexpr angle_t ANG270 = 0xC0000000u;

// Fine-angle trig table constants (tables.h).
inline constexpr int FINEANGLES       = 8192;
inline constexpr int FINEMASK         = FINEANGLES - 1;
inline constexpr int ANGLETOFINESHIFT = 19;        // 2^32 >> 19 == FINEANGLES

// Convert a degree quantity (DOOM THINGS angle: 0=East, 90=North) to BAM.
// 64-bit intermediate avoids truncation; exact for multiples of 45 deg.
inline angle_t degreesToBam(int deg) {
    return static_cast<angle_t>(
        (static_cast<std::uint64_t>(static_cast<unsigned>(deg)) * 0x100000000ULL) / 360ULL);
}
