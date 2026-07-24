#pragma once
#include "core/doomtype.h"

// Vanilla trig tables (tables.c / R_InitTables in r_main.c).
// finesine covers a full period plus one extra quarter so that finecosine
// (== finesine + FINEANGLES/4) can be indexed over a full period without OOB.
extern fixed_t  finesine[5 * FINEANGLES / 4];
extern fixed_t* const finecosine;   // == finesine + FINEANGLES/4

// Populate finesine (and thereby finecosine). Idempotent. Float is used ONLY
// here, at table-generation time — exactly how vanilla's tables.c was produced.
void R_InitTables();
