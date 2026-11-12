#pragma once
#include "core/doomtype.h"
#include "play/p_mobj.h"
#include "play/info.h"
#include <cstddef>
#include <list>
#include <vector>

struct MapData;
struct Blockmap;

// First-person weapon/flash sprite state (vanilla pspdef_t). sx/sy are screen offsets in
// 16.16 map units; P5b keeps them at 0 (the WAD's weapon sprites are pre-offset).
struct pspdef_t {
    const state_t* state = nullptr;
    int            tics = 0;
    fixed_t        sx = 0, sy = 0;
};

struct player_t {
    mobj_t*  mo = nullptr;
    fixed_t  viewz = 0;

    // ---- P5b combat state (vanilla player_t subset) ----
    int      health = 0;
    int      armorpoints[2] = {0, 0};   // [0]=green, [1]=blue
    int      armortype = 0;             // 0 none, 1 green, 2 blue
    int      ammo[NUMAMMO] = {};
    int      maxammo[NUMAMMO] = {};
    unsigned weapons = 0;               // bit i = weapontype_t i owned
    int      readyweapon = 0;
    int      pendingweapon = NUMWEAPONS;   // NUMWEAPONS == "no change"
    pspdef_t psprites[2];               // [0]=weapon, [1]=flash
    bool     attackdown = false;
    bool     usedown = false;
    int      damagecount = 0;           // HUD face/tint timers
    int      bonuscount = 0;
    int      killcount = 0, itemcount = 0;
    int      keys = 0;                  // bit0 blue, bit1 yellow, bit2 red
    bool     dead = false;
};

struct World {
    const MapData*              map = nullptr;
    const Blockmap*             bm  = nullptr;
    std::list<mobj_t>           mobjs;
    player_t                    player;
    std::uint32_t               tic = 0;
    bool                        thinkering = false;   // P_RemoveMobj defers unlinking during a pass
    std::vector<std::vector<const mobj_t*>> thingsBySubsec;
};

mobj_t* P_SpawnMobj(World& w, fixed_t x, fixed_t y, fixed_t z, mobjtype_t type);
void    P_SetMobjState(mobj_t& m, statenum_t st);
// World-aware variant (vanilla P_SetMobjState): S_NULL removes the mobj (puffs/blood/
// explosion chains end there). Use this on the live gameplay path.
void    P_SetMobjState(World& w, mobj_t& m, statenum_t st);
void    P_RemoveMobj(World& w, mobj_t& m);
void    P_SetThingPosition(World& w, mobj_t& m);   // set m.subsector from (x,y)

void    P_SetupLevel(World& w, const MapData& m);   // spawn all THINGS + player
void    P_BuildThingsBySubsec(World& w);            // rebuild per-frame render bins

void    P_RunThinkers(World& w);                    // defined in Task 6 (physics)

// P5b: per-tic player logic (weapon timers/psprites; `attack` = fire held, `weaponSwitch`
// = requested weapontype or -1). Implemented in p_user.cpp; SDL-free.
void    P_PlayerThink(World& w, bool attack, int weaponSwitch);

// P5a toss milestone, shared by the interactive G key and --smoketest: spawn an MT_BARREL
// 48 units in front of the player with forward+up momentum. Returns the new barrel (or null).
mobj_t* P_TossBarrel(World& w);
