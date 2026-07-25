#include "m_tables.h"
#include <cmath>

fixed_t  finesine[5 * FINEANGLES / 4];
fixed_t* const finecosine = finesine + FINEANGLES / 4;

void R_InitTables() {
    constexpr double twoPi = 6.283185307179586;
    const int n = 5 * FINEANGLES / 4;
    for (int i = 0; i < n; ++i) {
        // Fold into [0, FINEANGLES) so the trailing quarter period stays correct.
        double a = (i % FINEANGLES) * (twoPi / FINEANGLES);
        finesine[i] = static_cast<fixed_t>(std::sin(a) * FRACUNIT);
    }
    // finecosine aliases finesine; nothing else to fill.
}
