#pragma once
#include "core/doomtype.h"
#include "play/p_mobj.h"
#include "play/info.h"
#include <cstddef>
#include <list>
#include <string>
#include <vector>

struct MapData;
struct Blockmap;

constexpr int KEY_BLUE = 1, KEY_YELLOW = 2, KEY_RED = 4;

// P6a sector movers (doors/lifts/floors/ceilings) live in the World tick.
enum class MoverKind { Door, Plat, Floor, Ceiling };
struct Mover {
    MoverKind kind = MoverKind::Door;
    int  sector = -1;
    int  top = 0;         // target height for state 0
    int  bottom = 0;      // target height for state 2 (doors/plats)
    int  speed = 2;       // map units per tic
    int  waitTics = 0;
    int  state = 0;       // 0 -> top, 1 waiting, 2 -> bottom, 3 done
    bool stayOpen = false;
};

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
    int      secretsFound = 0;          // P7: counted once per secret sector
    int      keys = 0;                  // bit0 blue, bit1 yellow, bit2 red
    bool     dead = false;
};

struct World {
    MapData*                    map = nullptr;   // P6a: sector movers mutate heights
    const Blockmap*             bm  = nullptr;
    std::list<mobj_t>           mobjs;
    player_t                    player;
    std::uint32_t               tic = 0;
    bool                        thinkering = false;   // P_RemoveMobj defers unlinking during a pass
    std::vector<Mover>          movers;               // P6a active sector movers
    std::vector<std::vector<const mobj_t*>> thingsBySubsec;

    // ---- P7 campaign state ----
    std::string                 mapName;               // e.g. "E1M1"
    int                         totalKills = 0;        // MF_COUNTKILL spawned
    int                         totalItems = 0;        // MF_SPECIAL spawned
    bool                        levelExitRequested = false;
    bool                        secretExit = false;
    int                         totalSecrets = 0;      // sectors with special 9
    std::vector<char>           secretFound;           // per-sector, one-time count
};

mobj_t* P_SpawnMobj(World& w, fixed_t x, fixed_t y, fixed_t z, mobjtype_t type);
void    P_SetMobjState(mobj_t& m, statenum_t st);
// World-aware variant (vanilla P_SetMobjState): S_NULL removes the mobj (puffs/blood/
// explosion chains end there). Use this on the live gameplay path.
void    P_SetMobjState(World& w, mobj_t& m, statenum_t st);
void    P_RemoveMobj(World& w, mobj_t& m);
void    P_SetThingPosition(World& w, mobj_t& m);   // set m.subsector from (x,y)

// Spawn all THINGS + player. When `carry` is non-null the player's inventory
// (health/armor/ammo/weapons/keys/ready weapon) survives the level change (P7).
void    P_SetupLevel(World& w, const MapData& m, const player_t* carry = nullptr);
void    P_BuildThingsBySubsec(World& w);            // rebuild per-frame render bins

void    P_RunThinkers(World& w);                    // defined in Task 6 (physics)

// P7: count a secret sector the first time the player stands in it.
void    P_CheckSecrets(World& w, mobj_t& mo);

// P5b: per-tic player logic (weapon timers/psprites; `attack` = fire held, `weaponSwitch`
// = requested weapontype or -1). Implemented in p_user.cpp; SDL-free.
void    P_PlayerThink(World& w, bool attack, int weaponSwitch);

// P5a toss milestone, shared by the interactive G key and --smoketest: spawn an MT_BARREL
// 48 units in front of the player with forward+up momentum. Returns the new barrel (or null).
mobj_t* P_TossBarrel(World& w);
