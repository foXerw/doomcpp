#pragma once
#include "play/p_tick.h"

// P6b enemy AI (vanilla p_enemy.c / p_sight.c subset). The actions are dispatched from
// P_SetMobjState(World&) via P_MobjAction, and A_Chase performs monster movement.

// 2D line-of-sight test: one-sided walls and two-sided lines whose opening is closed at
// the monster's eye height block sight. (Vanilla uses REJECT+BSP; this is equivalent for
// the single-player maps we support and is O(lines).)
bool P_CheckSight(const World& w, const mobj_t& observer, const mobj_t& target);

// Scan the player as a wake-up target (distance/angle/sight checks). On success sets
// actor.target and returns true.
bool P_LookForPlayers(World& w, mobj_t& actor);

// Gunfire/noise wakes nearby monsters that can see the source.
void P_NoiseAlert(World& w, mobj_t* source);

// State-entry action dispatch for live mobjs (A_Look/A_Chase/A_*Attack/screams/...).
void P_MobjAction(World& w, mobj_t& mo, action_t action);

// Spawn a monster projectile from `source` aimed at `dest` (vanilla P_SpawnMissile).
mobj_t* P_SpawnMissile(World& w, mobj_t& source, mobj_t& dest, mobjtype_t type);

// Lost-soul charge movement (P_SkullFly); called from P_MobjThinker when MF_SKULLFLY.
void P_SkullFly(World& w, mobj_t& mo);
