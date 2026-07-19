#pragma once
#include "doomtype.h"

// Fixed-point arithmetic, faithful to the original engine's m_fixed.c.
fixed_t FixedMul(fixed_t a, fixed_t b);   // (a * b) >> FRACBITS, via 64-bit.
fixed_t FixedDiv(fixed_t a, fixed_t b);   // saturates on overflow, else FixedDiv2.
