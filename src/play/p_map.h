#pragma once
#include "core/doomtype.h"
#include "play/p_setup.h"
#include "play/p_maputl.h"
#include "play/p_mobj.h"
#include "play/p_tick.h"
#include <functional>

// Gameplay constants (p_local.h / info.c MT_PLAYER) as fixed_t.
// P5a: superseded -- collision now reads radius/height from mobjinfo[MT_PLAYER]
// via mobj_t.info. Kept defined (redundant) so a default-constructed mobj with
// null info still has a sane fallback, and to avoid touching unrelated refs.
constexpr fixed_t PLAYERRADIUS = 16 << FRACBITS;   // mobjinfo[MT_PLAYER].radius
constexpr fixed_t PLAYERHEIGHT = 56 << FRACBITS;   // mobjinfo[MT_PLAYER].height
constexpr fixed_t VIEWHEIGHT   = 41 << FRACBITS;   // VIEWHEIGHT
constexpr fixed_t MAXMOVE      = 30 << FRACBITS;   // per-tic displacement cap
constexpr fixed_t STEP_LIMIT   = 24 << FRACBITS;   // step-up / dropoff limit
constexpr int     MAPBLOCKSHIFT = FRACBITS + 7;    // MAPBLOCK = 128 = 2^7
constexpr fixed_t MISSILERANGE = 32 * 64 << FRACBITS;   // vanilla p_local.h

struct PosCheck { fixed_t floorz = 0, ceilingz = 0, dropoffz = 0; bool ok = true; };

PosCheck P_CheckPosition(const MapData& m, const Blockmap& bm, const mobj_t& mo, fixed_t x, fixed_t y);
bool     P_TryMove(const MapData& m, const Blockmap& bm, mobj_t& mo, fixed_t nx, fixed_t ny);
bool     P_TrySlide(const MapData& m, const Blockmap& bm, mobj_t& mo, fixed_t dx, fixed_t dy);
void     P_CalcHeight(const MapData& m, player_t& p);
void     P_MovePlayer(const MapData& m, const Blockmap& bm, player_t& p,
                      fixed_t forward, fixed_t strafe, angle_t turn);

// P5b: iterate mobjs whose radius-extended bbox intersects the given box. The callback
// returns true to stop early. (E1M1's ~230 mobjs make a list scan cheaper than maintaining
// per-cell thing bins; the blockmap is still used for walls.)
void P_BlockThingsIterator(World& w, fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2,
                           const std::function<bool(mobj_t&)>& fn);

// P5b hitscan (vanilla p_map.c P_LineAttack subset): shoot from (x,y,z) along `angle`
// with vertical `slope` (dz/dist); the nearest shootable mobj or wall wins. Damage is
// applied via P_DamageMobj; a puff/blood is spawned at the impact. Returns the hit mobj
// (nullptr for wall/range impacts). P_AimLineAttack does vanilla-style auto-aim.
mobj_t* P_LineAttack(World& w, mobj_t* source, fixed_t x, fixed_t y, fixed_t z,
                     angle_t angle, fixed_t slope, int damage);
mobj_t* P_AimLineAttack(World& w, mobj_t* source, fixed_t x, fixed_t y, fixed_t z,
                        angle_t angle, fixed_t distance, fixed_t& slopeOut);
mobj_t* P_SpawnPuff(World& w, fixed_t x, fixed_t y, fixed_t z);
mobj_t* P_SpawnBlood(World& w, fixed_t x, fixed_t y, fixed_t z, int damage);
