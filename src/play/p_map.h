#pragma once
#include "play/p_setup.h"
#include "play/p_maputl.h"

// Gameplay constants (p_local.h / info.c MT_PLAYER).
constexpr float PLAYERRADIUS = 16.0f;   // PLAYERRADIUS / mobjinfo[MT_PLAYER].radius
constexpr float PLAYERHEIGHT = 56.0f;   // mobjinfo[MT_PLAYER].height
constexpr float VIEWHEIGHT   = 41.0f;   // VIEWHEIGHT
constexpr float MAXMOVE      = 30.0f;   // MAXMOVE (per-tic displacement cap)
constexpr float STEP_LIMIT   = 24.0f;   // step-up / dropoff limit

struct Player {
    float x = 0, y = 0, angle = 0;
    float viewz = 0;
    int   subsector = 0, sector = 0;
    float floorz = 0, ceilingz = 0;
};

struct PosCheck { float floorz = 0, ceilingz = 0, dropoffz = 0; bool ok = true; };

// Faithful P_CheckPosition (things omitted): can a PLAYERRADIUS circle fit at (x,y)?
PosCheck P_CheckPosition(const MapData& m, const Blockmap& bm, float x, float y);
bool P_TryMove(const MapData& m, const Blockmap& bm, Player& p, float nx, float ny);     // Task 5
bool P_TrySlide(const MapData& m, const Blockmap& bm, Player& p, float dx, float dy);    // Task 5
void P_CalcHeight(const MapData& m, Player& p);                                          // Task 6
void P_MovePlayer(const MapData& m, const Blockmap& bm, Player& p, float fwd, float str, float turn);  // Task 6
