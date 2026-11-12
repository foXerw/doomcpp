#pragma once
#include "play/p_tick.h"

struct line_t;
struct MapData;

// P6a sector movers (vanilla p_spec.c subset): doors, lifts/platforms, floors, ceilings.
// Mover/MoverKind live in p_tick.h (World owns the active list).

// Advance every active mover one tic (and carry things standing on the sector).
void P_RunMovers(World& w);

// Line specials: use (Space) and walk-over triggers.
void P_ActivateLine(World& w, const line_t& L, mobj_t* who);
void P_UseLines(World& w, player_t& p);
// Called after the player moved from (oldx,oldy): fires crossed W1/WR specials.
void P_CrossSpecialLines(World& w, mobj_t& mo, fixed_t oldx, fixed_t oldy);

// Test/utility hooks.
int  P_DoorOpenHeight(const MapData& m, int sector);
bool P_SectorHasMover(const World& w, int sector);
