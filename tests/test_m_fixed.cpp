#include "doctest.h"
#include "core/m_fixed.h"
#include <limits>

TEST_CASE("FixedMul multiplies fixed-point values") {
    CHECK(FixedMul(FRACUNIT, FRACUNIT) == FRACUNIT);                          // 1.0 * 1.0
    CHECK(FixedMul(static_cast<fixed_t>(2.5 * FRACUNIT), 2 * FRACUNIT) == 5 * FRACUNIT); // 2.5 * 2.0
    CHECK(FixedMul(FRACUNIT / 2, FRACUNIT / 2) == FRACUNIT / 4);              // 0.5 * 0.5
}

TEST_CASE("FixedDiv divides fixed-point values") {
    CHECK(FixedDiv(FRACUNIT, 2 * FRACUNIT) == FRACUNIT / 2);                  // 1.0 / 2.0
    CHECK(FixedDiv(6 * FRACUNIT, 3 * FRACUNIT) == 2 * FRACUNIT);              // 6.0 / 3.0
}

TEST_CASE("FixedDiv saturates instead of UB on overflow") {
    CHECK(FixedDiv(64 * FRACUNIT, 1) == std::numeric_limits<fixed_t>::max());
    CHECK(FixedDiv(-64 * FRACUNIT, 1) == std::numeric_limits<fixed_t>::min());
}

TEST_CASE("FixedSqrt takes the root of a fixed-point value") {
    CHECK(FixedSqrt(0) == 0);
    CHECK(FixedSqrt(FRACUNIT) == FRACUNIT);                                     // sqrt(1.0)=1.0
    CHECK(FixedSqrt(4 * FRACUNIT) == 2 * FRACUNIT);                             // sqrt(4.0)=2.0
    CHECK(FixedSqrt(256 * FRACUNIT) == 16 * FRACUNIT);                          // sqrt(256)=16
    const fixed_t r = FixedSqrt(2 * FRACUNIT);                                  // sqrt(2) ~= 1.41421
    CHECK(r >= static_cast<fixed_t>(1.4141 * FRACUNIT));                        // within 16.16 rounding
    CHECK(r <= static_cast<fixed_t>(1.4143 * FRACUNIT));
}

#include "core/doomtype.h"   // already included transitively via m_fixed.h

TEST_CASE("BAM angle constants and degreesToBam") {
    CHECK(ANG90  == 0x40000000u);
    CHECK(ANG180 == 0x80000000u);
    CHECK(ANG270 == 0xC0000000u);
    CHECK(degreesToBam(0)   == 0u);
    CHECK(degreesToBam(90)  == ANG90);
    CHECK(degreesToBam(180) == ANG180);
    CHECK(degreesToBam(360) == 0u);            // wraps full circle
    CHECK((angle_t)0 == 0u);                   // angle_t is uint32
}
