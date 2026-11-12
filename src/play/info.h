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
    SPR_BFE1,
    SPR_BFE2,
    SPR_BFGF,
    SPR_BFGG,
    SPR_BFS1,
    SPR_BLUD,
    SPR_CHGF,
    SPR_CHGG,
    SPR_GOR1,
    SPR_MISF,
    SPR_MISG,
    SPR_MISL,
    SPR_PISF,
    SPR_PISG,
    SPR_PLSE,
    SPR_PLSF,
    SPR_PLSG,
    SPR_PLSS,
    SPR_PUFF,
    SPR_PUNG,
    SPR_SAWG,
    SPR_SHTF,
    SPR_SHTG,
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
    S_LIGHTDONE,
    S_PUNCH,
    S_PUNCHDOWN,
    S_PUNCHUP,
    S_PUNCH1,
    S_PUNCH2,
    S_PUNCH3,
    S_PUNCH4,
    S_PUNCH5,
    S_PISTOL,
    S_PISTOLDOWN,
    S_PISTOLUP,
    S_PISTOL1,
    S_PISTOL2,
    S_PISTOL3,
    S_PISTOL4,
    S_PISTOLFLASH,
    S_SGUN,
    S_SGUNDOWN,
    S_SGUNUP,
    S_SGUN1,
    S_SGUN2,
    S_SGUN3,
    S_SGUN4,
    S_SGUN5,
    S_SGUN6,
    S_SGUN7,
    S_SGUN8,
    S_SGUN9,
    S_SGUNFLASH1,
    S_SGUNFLASH2,
    S_CHAIN,
    S_CHAINDOWN,
    S_CHAINUP,
    S_CHAIN1,
    S_CHAIN2,
    S_CHAIN3,
    S_CHAINFLASH1,
    S_CHAINFLASH2,
    S_MISSILE,
    S_MISSILEDOWN,
    S_MISSILEUP,
    S_MISSILE1,
    S_MISSILE2,
    S_MISSILE3,
    S_MISSILEFLASH1,
    S_MISSILEFLASH2,
    S_MISSILEFLASH3,
    S_MISSILEFLASH4,
    S_SAW,
    S_SAWB,
    S_SAWDOWN,
    S_SAWUP,
    S_SAW1,
    S_SAW2,
    S_SAW3,
    S_PLASMA,
    S_PLASMADOWN,
    S_PLASMAUP,
    S_PLASMA1,
    S_PLASMA2,
    S_PLASMAFLASH1,
    S_PLASMAFLASH2,
    S_BFG,
    S_BFGDOWN,
    S_BFGUP,
    S_BFG1,
    S_BFG2,
    S_BFG3,
    S_BFG4,
    S_BFGFLASH1,
    S_BFGFLASH2,
    S_BLOOD1,
    S_BLOOD2,
    S_BLOOD3,
    S_PUFF1,
    S_PUFF2,
    S_PUFF3,
    S_PUFF4,
    S_PLASBALL,
    S_PLASBALL2,
    S_PLASEXP,
    S_PLASEXP2,
    S_PLASEXP3,
    S_PLASEXP4,
    S_PLASEXP5,
    S_ROCKET,
    S_BFGSHOT,
    S_BFGSHOT2,
    S_BFGLAND,
    S_BFGLAND2,
    S_BFGLAND3,
    S_BFGLAND4,
    S_BFGLAND5,
    S_BFGLAND6,
    S_BFGEXP,
    S_BFGEXP2,
    S_BFGEXP3,
    S_BFGEXP4,
    S_EXPLODE1,
    S_EXPLODE2,
    S_EXPLODE3,
    S_PLAY_RUN1,
    S_PLAY_RUN2,
    S_PLAY_RUN3,
    S_PLAY_RUN4,
    S_PLAY_ATK1,
    S_PLAY_ATK2,
    S_PLAY_PAIN,
    S_PLAY_PAIN2,
    S_PLAY_DIE1,
    S_PLAY_DIE2,
    S_PLAY_DIE3,
    S_PLAY_DIE4,
    S_PLAY_DIE5,
    S_PLAY_DIE6,
    S_PLAY_DIE7,
    S_PLAY_XDIE1,
    S_PLAY_XDIE2,
    S_PLAY_XDIE3,
    S_PLAY_XDIE4,
    S_PLAY_XDIE5,
    S_PLAY_XDIE6,
    S_PLAY_XDIE7,
    S_PLAY_XDIE8,
    S_PLAY_XDIE9,
    S_POSS_STND2,
    S_POSS_RUN1,
    S_POSS_RUN2,
    S_POSS_RUN3,
    S_POSS_RUN4,
    S_POSS_RUN5,
    S_POSS_RUN6,
    S_POSS_RUN7,
    S_POSS_RUN8,
    S_POSS_ATK1,
    S_POSS_ATK2,
    S_POSS_ATK3,
    S_POSS_PAIN,
    S_POSS_PAIN2,
    S_POSS_DIE1,
    S_POSS_DIE2,
    S_POSS_DIE3,
    S_POSS_DIE4,
    S_POSS_DIE5,
    S_POSS_XDIE1,
    S_POSS_XDIE2,
    S_POSS_XDIE3,
    S_POSS_XDIE4,
    S_POSS_XDIE5,
    S_POSS_XDIE6,
    S_POSS_XDIE7,
    S_POSS_XDIE8,
    S_POSS_XDIE9,
    S_POSS_RAISE1,
    S_POSS_RAISE2,
    S_POSS_RAISE3,
    S_POSS_RAISE4,
    S_SPOS_STND2,
    S_SPOS_RUN1,
    S_SPOS_RUN2,
    S_SPOS_RUN3,
    S_SPOS_RUN4,
    S_SPOS_RUN5,
    S_SPOS_RUN6,
    S_SPOS_RUN7,
    S_SPOS_RUN8,
    S_SPOS_ATK1,
    S_SPOS_ATK2,
    S_SPOS_ATK3,
    S_SPOS_PAIN,
    S_SPOS_PAIN2,
    S_SPOS_DIE1,
    S_SPOS_DIE2,
    S_SPOS_DIE3,
    S_SPOS_DIE4,
    S_SPOS_DIE5,
    S_SPOS_XDIE1,
    S_SPOS_XDIE2,
    S_SPOS_XDIE3,
    S_SPOS_XDIE4,
    S_SPOS_XDIE5,
    S_SPOS_XDIE6,
    S_SPOS_XDIE7,
    S_SPOS_XDIE8,
    S_SPOS_XDIE9,
    S_SPOS_RAISE1,
    S_SPOS_RAISE2,
    S_SPOS_RAISE3,
    S_SPOS_RAISE4,
    S_SPOS_RAISE5,
    S_TROO_STND2,
    S_TROO_RUN1,
    S_TROO_RUN2,
    S_TROO_RUN3,
    S_TROO_RUN4,
    S_TROO_RUN5,
    S_TROO_RUN6,
    S_TROO_RUN7,
    S_TROO_RUN8,
    S_TROO_ATK1,
    S_TROO_ATK2,
    S_TROO_ATK3,
    S_TROO_PAIN,
    S_TROO_PAIN2,
    S_TROO_DIE1,
    S_TROO_DIE2,
    S_TROO_DIE3,
    S_TROO_DIE4,
    S_TROO_DIE5,
    S_TROO_XDIE1,
    S_TROO_XDIE2,
    S_TROO_XDIE3,
    S_TROO_XDIE4,
    S_TROO_XDIE5,
    S_TROO_XDIE6,
    S_TROO_XDIE7,
    S_TROO_XDIE8,
    S_TROO_RAISE1,
    S_TROO_RAISE2,
    S_TROO_RAISE3,
    S_TROO_RAISE4,
    S_TROO_RAISE5,
    S_SARG_STND2,
    S_SARG_RUN1,
    S_SARG_RUN2,
    S_SARG_RUN3,
    S_SARG_RUN4,
    S_SARG_RUN5,
    S_SARG_RUN6,
    S_SARG_RUN7,
    S_SARG_RUN8,
    S_SARG_ATK1,
    S_SARG_ATK2,
    S_SARG_ATK3,
    S_SARG_PAIN,
    S_SARG_PAIN2,
    S_SARG_DIE1,
    S_SARG_DIE2,
    S_SARG_DIE3,
    S_SARG_DIE4,
    S_SARG_DIE5,
    S_SARG_DIE6,
    S_SARG_RAISE1,
    S_SARG_RAISE2,
    S_SARG_RAISE3,
    S_SARG_RAISE4,
    S_SARG_RAISE5,
    S_SARG_RAISE6,
    S_BAR2,
    S_BLOODYTWITCH,
    S_BLOODYTWITCH2,
    S_BLOODYTWITCH3,
    S_BLOODYTWITCH4,
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
    MT_ROCKET, MT_PLASMA, MT_BFG, MT_PUFF, MT_BLOOD,   // P5b projectiles/effects
    NUMMOBJTYPES
};

enum action_t {
    A_NULL = 0,
    A_BFGSpray,
    A_BFGsound,
    A_Chase,
    A_Explode,
    A_FaceTarget,
    A_Fall,
    A_FireBFG,
    A_FireCGun,
    A_FireMissile,
    A_FirePistol,
    A_FirePlasma,
    A_FireShotgun,
    A_GunFlash,
    A_Light0,
    A_Light1,
    A_Light2,
    A_Look,
    A_Lower,
    A_Pain,
    A_PlayerScream,
    A_PosAttack,
    A_Punch,
    A_Raise,
    A_ReFire,
    A_SPosAttack,
    A_SargAttack,
    A_Saw,
    A_Scream,
    A_TroopAttack,
    A_WeaponReady,
    A_XScream,
};

struct state_t {
    spritenum_t sprite;
    int         frame;        // 0-based; FF_FULLBRIGHT bit set => fullbright
    int         tics;         // -1 = never auto-advance
    statenum_t  nextstate;
    action_t    action = A_NULL;   // P5b: dispatched on state entry
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
// ---- P5b weapons/ammo ----
enum ammotype_t { AM_NOAMMO = 0, AM_CLIP, AM_SHELL, AM_CELL, AM_MISL, NUMAMMO };
struct ammoinfo_t { int maxammo; };            // ammo per pickup is handled in p_map (P5b)
extern const ammoinfo_t ammoinfo[NUMAMMO];

enum weapontype_t { WP_FIST = 0, WP_CHAINSAW, WP_PISTOL, WP_SHOTGUN, WP_CHAINGUN,
                    WP_MISSILE, WP_PLASMA, WP_BFG, NUMWEAPONS };
struct weaponinfo_t {
    ammotype_t  ammo;
    statenum_t  upstate, downstate, readystate, atkstate, flashstate;
};
extern const weaponinfo_t weaponinfo[NUMWEAPONS];

int mobjtypeForDoomednum(int doomednum);
