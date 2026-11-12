#pragma once
#include "core/doomtype.h"
#include "play/info.h"
#include <cstring>

// 活 mobj（P5a）。渲染缓存字段（sprname/frame/fullbright/light）由 P_SetMobjState 刷新
// （Task 3）；Task 2 期间 main.cpp 仍直接填写它们。z 现为 16.16 定点（P4 是 int map units）。
struct mobj_t {
    fixed_t x = 0, y = 0, z = 0;            // 16.16
    fixed_t height = 0;                      // per-mobj collision height (P5b corpses shrink)
    mobj_t* target = nullptr;                // attacker/shooter (vanilla; missiles, damage credit)
    fixed_t momx = 0, momy = 0, momz = 0;   // 动量（Task 6 物理）
    angle_t angle = 0;                      // BAM
    fixed_t floorz = 0, ceilingz = 0, dropoffz = 0;
    int     health = 0;
    int     flags = 0;
    int     reactiontime = 0;               // P6b: A_Chase attack delay (ticks)
    int     tics = -1;                      // 当前 state 剩余 tic
    const mobjinfo_t* info = nullptr;
    const state_t*   state = nullptr;
    int     subsector = -1;
    bool    removePending = false;          // vanilla P_RemoveMobj: unlink after the thinker pass
    // —— 渲染缓存（渲染器只读）——
    char    sprname[5] = {0};
    char    frame = 'A';                    // 'A'-based（SpriteCache.resolve 做 frame-'A'）
    bool    fullbright = false;
    int     light = 160;
};

struct World;   // fwd（p_tick.h 完整定义；避免 p_mobj.h -> p_tick.h 循环 include）
// per-tic mobj thinker（动量/摩擦/重力/状态推进；P5b 可在其中 spawn/remove）。
void P_MobjThinker(World& w, mobj_t& mo);
// Momentum integration + friction (vanilla P_XYMovement); shared by mobjs and the player.
void P_XYMovement(World& w, mobj_t& mo);
