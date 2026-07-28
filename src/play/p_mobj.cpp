#include "p_mobj.h"
#include "p_tick.h"
#include "p_maputl.h"
#include "p_setup.h"
#include "p_map.h"          // P_TryMove, MAXMOVE, VIEWHEIGHT
#include "core/m_fixed.h"   // FixedMul/FixedDiv
#include "core/m_tables.h"
#include "info.h"
#include <cstdlib>
#include <cstring>

namespace {
// vanilla p_mobj.c physics constants (qualitative tests; tunable).
constexpr fixed_t STOPSPEED = 0x1000;     // vanilla p_mobj.c
constexpr fixed_t FRICTION  = 0xe800;     // vanilla ORIG_FRICTION
constexpr fixed_t GRAVITY   = FRACUNIT;   // P5a 采用值（1 map unit/tic²）；vanilla 确切值实现期对照 p_mobj.c 校准——物理测试为定性，常量可调

// P_AproxDistance 的本地副本（p_map.cpp 中是 static 未导出）。vanilla m_fixed.c::P_AproxDistance。
fixed_t apx(fixed_t dx, fixed_t dy) {
    dx = std::abs(dx); dy = std::abs(dy);
    if (dx < dy) return dx + dy - (dx >> 1);
    return dx + dy - (dy >> 1);
}
} // namespace

// Vanilla p_mobj.c::P_XYMovement：按 MAXMOVE 分步走 P_TryMove，撞墙即停；之后摩擦（每 tic 衰减，
// 小于 STOPSPEED 直接归零）。注意碰撞判据是位置——动量可能非零但被墙挡住逐 tic 衰减。
static void P_XYMovement(const World& w, mobj_t& mo) {
    fixed_t xleft = mo.momx, yleft = mo.momy;
    while (xleft != 0 || yleft != 0) {
        fixed_t xl = xleft, yl = yleft;
        fixed_t mag = apx(xl, yl);
        if (mag > MAXMOVE) {
            fixed_t s = FixedDiv(MAXMOVE, mag);
            xl = FixedMul(xl, s);
            yl = FixedMul(yl, s);
        }
        if (!P_TryMove(*w.map, *w.bm, mo, mo.x + xl, mo.y + yl)) break;   // 撞墙：停
        xleft -= xl;
        yleft -= yl;
        if (xl == 0 && yl == 0) break;
    }
    if (apx(mo.momx, mo.momy) < STOPSPEED) { mo.momx = 0; mo.momy = 0; }
    else { mo.momx = FixedMul(mo.momx, FRICTION); mo.momy = FixedMul(mo.momy, FRICTION); }
}

// Vanilla p_mobj.c::P_ZMovement：重力（MF_NOGRAVITY 除外）、落地（z<=floorz 清 momz）、
// 撞顶（z>=ceilingz-height 清 momz）。
static void P_ZMovement(const World& w, mobj_t& mo) {
    (void)w;
    if (!(mo.flags & MF_NOGRAVITY)) mo.momz -= GRAVITY;
    mo.z += mo.momz;
    if (mo.z <= mo.floorz) { if (mo.momz < 0) mo.momz = 0; mo.z = mo.floorz; }   // 落地
    fixed_t cz = mo.ceilingz - (mo.info ? mo.info->height : 0);
    if (mo.z >= cz) { mo.z = cz; if (mo.momz > 0) mo.momz = 0; }                 // 撞顶
}

void P_MobjThinker(const World& w, mobj_t& mo) {
    if (mo.momx || mo.momy) P_XYMovement(w, mo);
    if (mo.momz || !(mo.flags & MF_NOGRAVITY)) P_ZMovement(w, mo);
    // 状态推进（P5a spawnstate 均 tics=-1，本分支不触发；为 P5b 及死亡状态留接口）
    if (mo.state && mo.state->tics != -1 && mo.tics > 0) {
        if (--mo.tics == 0) P_SetMobjState(mo, mo.state->nextstate);
    }
}

mobj_t* P_SpawnMobj(World& w, fixed_t x, fixed_t y, fixed_t z, mobjtype_t type) {
    w.mobjs.push_back({});
    mobj_t& m = w.mobjs.back();
    m.x = x; m.y = y; m.z = z;
    m.info = &mobjinfo[type];
    m.health = m.info->spawnhealth;
    m.flags = m.info->flags;
    P_SetMobjState(m, m.info->spawnstate);
    P_SetThingPosition(w, m);
    return &m;
}

void P_SetMobjState(mobj_t& m, statenum_t sn) {
    if (sn == S_NULL) { m.state = &states[S_NULL]; m.tics = -1; return; }
    m.state = &states[sn];
    m.tics = m.state->tics;
    // refresh render cache (faithful: copy state->sprite/frame into mobj)
    if (m.state->sprite != SPR_NONE)
        std::memcpy(m.sprname, sprnames[m.state->sprite], 5);
    m.frame = static_cast<char>('A' + (m.state->frame & FF_FRAMEMASK));
    m.fullbright = (m.state->frame & FF_FULLBRIGHT) != 0;
}

void P_RemoveMobj(World& w, mobj_t& m) {
    for (auto it = w.mobjs.begin(); it != w.mobjs.end(); ++it)
        if (&*it == &m) { w.mobjs.erase(it); return; }
}

void P_SetThingPosition(World& w, mobj_t& m) {
    if (!w.map) return;
    m.subsector = R_PointInSubsector(*w.map, m.x, m.y);
}
