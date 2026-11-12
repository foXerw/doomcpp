#pragma once
#include "play/p_tick.h"

// P5b first-person weapon/flash sprite state machine (vanilla p_pspr.c subset).
// index: PSP_WEAPON (0) / PSP_FLASH (1). Every transition re-enters the state's action.
void P_SetPsprite(World& w, player_t& p, int index, statenum_t st);
void P_MovePsprites(World& w, player_t& p);
