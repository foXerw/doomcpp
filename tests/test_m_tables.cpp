#include "doctest.h"
#include "core/m_tables.h"
#include "core/doomtype.h"

TEST_CASE("R_InitTables: sine/cosine key values") {
    R_InitTables();
    // sin(0) = 0, cos(0) = 1
    CHECK(finesine[0] == 0);
    CHECK(finecosine[0] == FRACUNIT);
    // quarter turn: sin = 1, cos = 0  (finecosine points into finesine + FINEANGLES/4)
    CHECK(finesine[FINEANGLES/4]     == FRACUNIT);          // sin(90)  = 1
    CHECK(finecosine[FINEANGLES/4]   == 0);                 // cos(90)  = 0
    // half turn: sin = 0, cos = -1
    CHECK(finesine[FINEANGLES/2]     == 0);                 // sin(180) = 0
    CHECK(finecosine[FINEANGLES/2]   == -FRACUNIT);         // cos(180) = -1
}

TEST_CASE("finesine indexed by BAM angle via ANGLETOFINESHIFT") {
    R_InitTables();
    CHECK(finesine[(0u    >> ANGLETOFINESHIFT) & FINEMASK] == 0);          // East  -> sin=0
    CHECK(finesine[(ANG90 >> ANGLETOFINESHIFT) & FINEMASK] ==
          doctest::Approx(FRACUNIT).epsilon(0.001));                        // North -> sin=1
    CHECK(finecosine[(ANG90 >> ANGLETOFINESHIFT) & FINEMASK] ==
          doctest::Approx(0).epsilon(0.001));                               // cos(90)=0
}

TEST_CASE("finecosine aliases finesine shifted by a quarter period") {
    R_InitTables();
    for (angle_t a = 0; a < 8u * ANG45; a += ANG45) {       // 0..360 in 45-deg steps
        int i = (a >> ANGLETOFINESHIFT) & FINEMASK;
        CHECK(finecosine[i] == finesine[i + FINEANGLES/4]);  // definition of finecosine
    }
}
