#pragma once
#include "core/doomtype.h"
#include "play/p_mobj.h"
#include "play/info.h"
#include <cstddef>
#include <list>
#include <vector>

struct MapData;
struct Blockmap;

struct player_t {
    mobj_t*  mo = nullptr;
    fixed_t  viewz = 0;
};

struct World {
    const MapData*              map = nullptr;
    const Blockmap*             bm  = nullptr;
    std::list<mobj_t>           mobjs;
    player_t                    player;
    std::uint32_t               tic = 0;
    std::vector<std::vector<const mobj_t*>> thingsBySubsec;
};

mobj_t* P_SpawnMobj(World& w, fixed_t x, fixed_t y, fixed_t z, mobjtype_t type);
void    P_SetMobjState(mobj_t& m, statenum_t st);
void    P_RemoveMobj(World& w, mobj_t& m);
void    P_SetThingPosition(World& w, mobj_t& m);   // set m.subsector from (x,y)

void    P_SetupLevel(World& w, const MapData& m);   // spawn all THINGS + player
void    P_BuildThingsBySubsec(World& w);            // rebuild per-frame render bins

void    P_RunThinkers(World& w);                    // defined in Task 6 (physics)

// P5a toss milestone, shared by the interactive G key and --smoketest: spawn an MT_BARREL
// 48 units in front of the player with forward+up momentum. Returns the new barrel (or null).
mobj_t* P_TossBarrel(World& w);
