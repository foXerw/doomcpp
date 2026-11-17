#pragma once
#include "play/p_tick.h"

struct line_t;
struct MapData;

// P6a sector movers (vanilla p_spec.c subset): doors, lifts/platforms, floors, ceilings.
// Mover/MoverKind live in p_tick.h (World owns the active list).

// Advance every active mover one tic (and carry things standing on the sector).
void P_RunMovers(World& w);

// P8b sector light specials: P_InitLights builds the animated set from the map's sector
// types, P_RunLights advances them once per tic (call from P_RunThinkers).
void P_InitLights(World& w);
void P_RunLights(World& w);

// Line specials: use (Space) and walk-over triggers.
void P_ActivateLine(World& w, const line_t& L, mobj_t* who);
void P_UseLines(World& w, player_t& p);
// P7: monsters open doors when their chase is blocked (vanilla P_UseSpecialLine subset).
void P_UseLines(World& w, mobj_t& mo);
// Called after the player moved from (oldx,oldy): fires crossed W1/WR specials.
void P_CrossSpecialLines(World& w, mobj_t& mo, fixed_t oldx, fixed_t oldy);

// Test/utility hooks.
int  P_DoorOpenHeight(const MapData& m, int sector);
bool P_SectorHasMover(const World& w, int sector);

// P6b boss death: lower every sector with the given tag to its next lower floor
// (vanilla A_BossDeath special 666 handling).
void P_LowerFloorByTag(World& w, int tag);
// ---- P9: one data table is the single source of truth about supported specials ----
//
// Every linedef special doomcpp supports is described once here: how it is triggered,
// whether it needs a key, what it does, and with what speed. P_ActivateLine dispatches on
// this table, and the reachability audit asks the same table "can the engine operate this
// line?" -- so the list of what is implemented cannot drift away from what the code does,
// and the list of what is NOT implemented is trustworthy enough to plan work from.
enum class SpecialKind {
    None,                       // not a special this engine knows
    DoorOpenClose,              // open, wait, close again
    DoorOpenStay,               // open and stay open
    DoorClose,
    Plat,                       // floor down, wait, back up
    FloorRaise,                 // floor to a neighbouring level
    FloorLower,
    Stairs,                     // stepped floors (simplified: one step per tagged sector)
    CeilingRaiseToNeighbours,
    CeilingLowerToFloor,
    TeleportTag,                // jump to a landing thing in the tag-matched sector
    TeleportOther,              // jump to a landing thing behind this line
    ExitNormal,
    ExitSecret,
};

struct SpecialDef {
    int special;
    SpecialKind kind;
    int trigger;                // bitmask: 1 = usable, 2 = triggered by crossing
    int key;                    // KEY_BLUE/YELLOW/RED, or 0 when unlocked
    int speed;                  // map units per tic for movers
};

// nullptr when the special number is unknown to this engine.
const SpecialDef* P_LookupSpecial(int special);
bool P_LineSpecialSupported(int special);
// The exit specials, for the campaign-flow code and the audits.
bool P_IsExitSpecial(int special);
bool P_IsSecretExitSpecial(int special);

