#pragma once
#include "core/doomtype.h"
#include "play/p_setup.h"
#include "play/p_maputl.h"
#include "play/p_mobj.h"
#include "play/p_tick.h"

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

struct PosCheck { fixed_t floorz = 0, ceilingz = 0, dropoffz = 0; bool ok = true; };

PosCheck P_CheckPosition(const MapData& m, const Blockmap& bm, const mobj_t& mo, fixed_t x, fixed_t y);
bool     P_TryMove(const MapData& m, const Blockmap& bm, mobj_t& mo, fixed_t nx, fixed_t ny);
bool     P_TrySlide(const MapData& m, const Blockmap& bm, mobj_t& mo, fixed_t dx, fixed_t dy);
void     P_CalcHeight(const MapData& m, player_t& p);
void     P_MovePlayer(const MapData& m, const Blockmap& bm, player_t& p,
                      fixed_t forward, fixed_t strafe, angle_t turn);
