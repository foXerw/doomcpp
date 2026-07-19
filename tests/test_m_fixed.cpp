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
