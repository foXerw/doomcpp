#pragma once
#include "core/doomtype.h"

// ---- frame bit flags (vanilla info.h) ----
constexpr int FF_FRAMEMASK   = 0x3FFF;
constexpr int FF_FULLBRIGHT  = 0x8000;

// ---- mobj flags (vanilla mobjinfo, subset used through P5b) ----
constexpr int MF_SPECIAL     = 1;       // P5b pick-up
constexpr int MF_SOLID       = 2;
constexpr int MF_SHOOTABLE   = 4;       // P5b damage
constexpr int MF_NOSECTOR    = 8;
constexpr int MF_NOBLOCKMAP  = 16;
constexpr int MF_AMBUSH      = 32;
constexpr int MF_JUSTHIT     = 64;
constexpr int MF_JUSTATTACKED= 128;
constexpr int MF_SPAWNCEILING= 256;
constexpr int MF_NOGRAVITY   = 512;
constexpr int MF_DROPOFF     = 1024;
constexpr int MF_PICKUP      = 2048;
constexpr int MF_NOCLIP      = 4096;
constexpr int MF_SLIDE       = 8192;
constexpr int MF_FLOAT       = 16384;
constexpr int MF_TELEPORT    = 32768;
constexpr int MF_MISSILE     = 65536;
constexpr int MF_DROPPED     = 131072;
constexpr int MF_SHADOW      = 262144;
constexpr int MF_NOBLOOD     = 524288;
constexpr int MF_CORPSE      = 1048576;
constexpr int MF_INFLOAT     = 2097152;
constexpr int MF_COUNTKILL   = 4194304;
constexpr int MF_COUNTITEM   = 8388608;
constexpr int MF_SKULLFLY    = 16777216;
constexpr int MF_NOTDMATCH   = 33554432;

enum spritenum_t {
    SPR_NONE = -1,
    SPR_PLAY, SPR_POSS, SPR_SPOS, SPR_TROO, SPR_SARG, SPR_BAR1,
    SPR_BKEY, SPR_CELP, SPR_POL5, SPR_POL6, SPR_TRE1, SPR_TRE2,
    SPR_SMIT, SPR_ELEC, SPR_GOR4, SPR_SHOT, SPR_MGUN, SPR_LAUN,
    SPR_PLAS, SPR_CSAW, SPR_CLIP, SPR_SHEL, SPR_ROCK, SPR_STIM,
    SPR_MEDI, SPR_SOUL, SPR_BON1, SPR_BON2, SPR_ARM1, SPR_COLU,
    SPR_BROK, SPR_AMMO, SPR_SBOX,
    NUMSPRITES
};
extern const char* const sprnames[];   // index = spritenum_t

enum statenum_t {
    S_NULL = 0,
    S_PLAY, S_BAR1, S_TROO_STND, S_POSS_STND, S_SPOS_STND, S_SARG_STND,
    // P4-decoration spawn states (one per E1M1 renderable doomednum that is not a live monster)
    S_D5, S_D10, S_D12, S_D15, S_D17, S_D18, S_D20, S_D21, S_D24, S_D26,
    S_D43, S_D47, S_D48, S_D54, S_D60,
    S_D2001, S_D2002, S_D2003, S_D2004, S_D2005, S_D2007, S_D2008, S_D2010,
    S_D2011, S_D2012, S_D2013, S_D2014, S_D2015, S_D2018, S_D2028,
    S_D2046, S_D2048, S_D2049,
    NUMSTATES
};

enum mobjtype_t {
    MT_PLAYER = 0, MT_BARREL,
    MT_POSSESSED, MT_SHOTGUY, MT_TROOP, MT_SERGEANT,
    // decoration/item types mirror the S_D* spawn states, ordered by doomednum
    MT_D5, MT_D10, MT_D12, MT_D15, MT_D17, MT_D18, MT_D20, MT_D21, MT_D24, MT_D26,
    MT_D43, MT_D47, MT_D48, MT_D54, MT_D60,
    MT_D2001, MT_D2002, MT_D2003, MT_D2004, MT_D2005, MT_D2007, MT_D2008, MT_D2010,
    MT_D2011, MT_D2012, MT_D2013, MT_D2014, MT_D2015, MT_D2018, MT_D2028,
    MT_D2046, MT_D2048, MT_D2049,
    NUMMOBJTYPES
};

struct state_t {
    spritenum_t sprite;
    int         frame;        // 0-based; FF_FULLBRIGHT bit set => fullbright
    int         tics;         // -1 = never auto-advance
    statenum_t  nextstate;
    // action function pointer omitted in P5a (no AI). P6 adds it here.
};
extern const state_t states[];   // index = statenum_t

struct mobjinfo_t {
    int        doomednum;     // -1 = not spawnable from a map thing
    statenum_t spawnstate;
    int        spawnhealth;
    fixed_t    radius;
    fixed_t    height;
    fixed_t    speed;
    int        mass;
    int        flags;
    // P5b-ready (unused in P5a, included so the table is ported once):
    statenum_t seestate, painstate, meleestate, missilestate, deathstate, xdeathstate;
    int        painchance, damage, reactiontime;
};
extern const mobjinfo_t mobjinfo[];   // index = mobjtype_t

// doomednum -> mobjtype_t; -1 if not spawnable (DM starts, unknown).
int mobjtypeForDoomednum(int doomednum);
