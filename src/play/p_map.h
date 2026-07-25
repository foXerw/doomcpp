#pragma once
#include "core/doomtype.h"
#include "play/p_setup.h"
#include "play/p_maputl.h"

// Gameplay constants (p_local.h / info.c MT_PLAYER) as fixed_t.
constexpr fixed_t PLAYERRADIUS = 16 << FRACBITS;   // mobjinfo[MT_PLAYER].radius
constexpr fixed_t PLAYERHEIGHT = 56 << FRACBITS;   // mobjinfo[MT_PLAYER].height
constexpr fixed_t VIEWHEIGHT   = 41 << FRACBITS;   // VIEWHEIGHT
constexpr fixed_t MAXMOVE      = 30 << FRACBITS;   // per-tic displacement cap
constexpr fixed_t STEP_LIMIT   = 24 << FRACBITS;   // step-up / dropoff limit
constexpr int     MAPBLOCKSHIFT = FRACBITS + 7;    // MAPBLOCK = 128 = 2^7

struct Player {
    fixed_t  x = 0, y = 0;
    angle_t  angle = 0;
    fixed_t  viewz = 0;
    int      subsector = 0, sector = 0;
    fixed_t  floorz = 0, ceilingz = 0;
};

struct PosCheck { fixed_t floorz = 0, ceilingz = 0, dropoffz = 0; bool ok = true; };

PosCheck P_CheckPosition(const MapData& m, const Blockmap& bm, fixed_t x, fixed_t y);
bool P_TryMove(const MapData& m, const Blockmap& bm, Player& p, fixed_t nx, fixed_t ny);
bool P_TrySlide(const MapData& m, const Blockmap& bm, Player& p, fixed_t dx, fixed_t dy);
void P_CalcHeight(const MapData& m, Player& p);
void P_MovePlayer(const MapData& m, const Blockmap& bm, Player& p,
                  fixed_t forward, fixed_t strafe, angle_t turn);
