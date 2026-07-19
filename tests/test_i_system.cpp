#include "doctest.h"
#include "core/i_system.h"
#include <stdexcept>

TEST_CASE("I_Printf does not throw") {
    CHECK_NOTHROW(I_Printf("hello doomcpp"));
}

TEST_CASE("I_Error throws std::runtime_error carrying the message") {
    REQUIRE_THROWS_AS(I_Error("boom"), std::runtime_error);
}
