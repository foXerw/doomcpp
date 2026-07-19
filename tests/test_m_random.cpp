#include "doctest.h"
#include "core/m_random.h"

TEST_CASE("M_Random returns values in [0,255]") {
    M_ClearRandom();
    int v = M_Random();
    CHECK(v >= 0);
    CHECK(v <= 255);
}

TEST_CASE("M_Random sequence is reproducible") {
    M_ClearRandom();
    int seq1[8];
    for (int i = 0; i < 8; ++i) seq1[i] = M_Random();

    M_ClearRandom();
    int seq2[8];
    for (int i = 0; i < 8; ++i) seq2[i] = M_Random();

    for (int i = 0; i < 8; ++i) CHECK(seq1[i] == seq2[i]);
}

TEST_CASE("M_ClearRandom resets the sequence to rndtable[1]") {
    for (int i = 0; i < 30; ++i) (void)M_Random();
    M_ClearRandom();
    CHECK(M_Random() == 8); // rndtable[1] == 8
}

TEST_CASE("P_Random advances an index independent of M_Random") {
    M_ClearRandom();
    for (int i = 0; i < 50; ++i) (void)M_Random();   // burn M_Random only
    CHECK(P_Random() == 8);                          // prndindex untouched -> rndtable[1]
}
