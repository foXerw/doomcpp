#pragma once
#include "play/p_tick.h"
#include "play/p_mobj.h"

// P5b damage/death resolution (vanilla p_inter.c subset).
void P_DamageMobj(World& w, mobj_t& target, mobj_t* source, int damage);
void P_KillMobj(World& w, mobj_t& target, mobj_t* source);
