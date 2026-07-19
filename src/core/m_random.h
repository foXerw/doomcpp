#pragma once

// Deterministic pseudo-random, faithful to the original engine's m_random.c.
// Two independent indices over a shared lookup table.
int  M_Random();        // misc random (advances rndindex)
int  P_Random();        // play-simulation random (advances prndindex)
void M_ClearRandom();   // reset both indices to 0
