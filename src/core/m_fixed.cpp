#include "m_fixed.h"
#include "i_system.h"
#include <cstdlib>
#include <limits>

static fixed_t FixedDiv2(fixed_t a, fixed_t b);

fixed_t FixedMul(fixed_t a, fixed_t b) {
    return static_cast<fixed_t>(
        (static_cast<std::int64_t>(a) * static_cast<std::int64_t>(b)) >> FRACBITS);
}

fixed_t FixedDiv(fixed_t a, fixed_t b) {
    if ((std::abs(a) >> 14) >= std::abs(b)) {
        return ((a ^ b) < 0) ? std::numeric_limits<fixed_t>::min()
                             : std::numeric_limits<fixed_t>::max();
    }
    return FixedDiv2(a, b);
}

static fixed_t FixedDiv2(fixed_t a, fixed_t b) {
    double c = (static_cast<double>(a) / static_cast<double>(b)) * FRACUNIT;
    if (c >= 2147483648.0 || c < -2147483648.0) {
        I_Error("FixedDiv: divide by zero");
    }
    return static_cast<fixed_t>(c);
}
