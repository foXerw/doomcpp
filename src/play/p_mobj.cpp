#include "p_mobj.h"
#include "p_tick.h"
#include "p_enemy.h"        // P_MobjAction / P_SkullFly (P6b)
#include "p_inter.h"        // P_DamageMobj (missile impacts)
#include "p_maputl.h"
#include "p_setup.h"
#include "p_map.h"          // P_TryMove, MAXMOVE, VIEWHEIGHT
#include "core/m_fixed.h"   // FixedMul/FixedDiv
#include "info.h"
#include <cstdlib>
#include <cstring>

namespace {
// State-entry dispatch: AI actions (P6b) + explosions + screams.
void P_DoMobjAction(World& w, mobj_t& mo, action_t action) {
    P_MobjAction(w, mo, action);
}

// Missile impact: stop moving, drop MF_MISSILE, enter the explosion/death state (which
// dispatches A_Explode for rockets/barrels) or unlink when there is none (plasma).
void P_ExplodeMissile(World& w, mobj_t& mo) {
    mo.momx = mo.momy = mo.momz = 0;
    mo.flags &= ~MF_MISSILE;
    statenum_t ds = (mo.info && mo.info->deathstate != S_NULL) ? mo.info->deathstate : S_NULL;
    if (ds == S_NULL) { P_RemoveMobj(w, mo); return; }
    P_SetMobjState(w, mo, ds);
}

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

// Vanilla p_mobj.c::P_XYMovement：按 MAXMOVE 分步走 P_TryMove；撞墙时沿阻挡线滑一次（玩家手感），
// 之后摩擦（每 tic 衰减，小于 STOPSPEED 直接归零）。注意碰撞判据是位置——动量可能非零但被挡逐 tic 衰减。
void P_XYMovement(World& w, mobj_t& mo) {
    fixed_t xleft = mo.momx, yleft = mo.momy;
    while (xleft != 0 || yleft != 0) {
        fixed_t xl = xleft, yl = yleft;
        fixed_t mag = apx(xl, yl);
        if (mag > MAXMOVE) {
            fixed_t s = FixedDiv(MAXMOVE, mag);
            xl = FixedMul(xl, s);
            yl = FixedMul(yl, s);
        }
        if (!P_TryMove(w, mo, mo.x + xl, mo.y + yl)) {
            // Blocked: non-missiles slide along the blocking wall with the remaining
            // momentum (vanilla P_SlideMove). Projectiles must stop so the caller can
            // explode them on contact.
            if (!(mo.flags & MF_MISSILE) && (xleft || yleft))
                P_SlideMove(w, mo, xleft, yleft);
            break;
        }
        xleft -= xl;
        yleft -= yl;
        if (xl == 0 && yl == 0) break;
    }
    if (apx(mo.momx, mo.momy) < STOPSPEED) { mo.momx = 0; mo.momy = 0; }
    else { mo.momx = FixedMul(mo.momx, FRICTION); mo.momy = FixedMul(mo.momy, FRICTION); }
    if (&mo == w.player.mo) P_TouchSpecialThings(w, mo);   // P5b pickups on move/knockback
}

// Vanilla p_mobj.c::P_ZMovement：重力（MF_NOGRAVITY 除外）、落地（z<=floorz 清 momz）、
// 撞顶（z>=ceilingz-height 清 momz）。
static void P_ZMovement(const World& w, mobj_t& mo) {
    (void)w;
    if (!(mo.flags & MF_NOGRAVITY)) mo.momz -= GRAVITY;
    mo.z += mo.momz;
    if (mo.z <= mo.floorz) { if (mo.momz < 0) mo.momz = 0; mo.z = mo.floorz; }   // 落地
    fixed_t h = mo.height ? mo.height : (mo.info ? mo.info->height : 0);
    fixed_t cz = mo.ceilingz - h;
    if (cz < mo.floorz) cz = mo.floorz;   // degenerate/未初始化 sector：不要把物体压穿地板
    if (mo.z > cz) { mo.z = cz; if (mo.momz > 0) mo.momz = 0; }                 // 撞顶
}

void P_MobjThinker(World& w, mobj_t& mo) {
    const bool isPlayer = (&mo == w.player.mo);
    if (isPlayer) {
        // Movement/physics for the player are driven by P_MovePlayerWorld (vanilla
        // P_PlayerThink). Integrating momentum here too would double the speed; only
        // the state chain (pain/death animation) advances below.
    } else if (mo.flags & MF_SKULLFLY) {
        P_SkullFly(w, mo);                       // lost soul charge (P6b)
    } else if (mo.flags & MF_MISSILE) {
        const fixed_t ox = mo.x, oy = mo.y;
        const bool hadMom = (mo.momx || mo.momy);
        if (hadMom) P_XYMovement(w, mo);
        if (mo.momz || !(mo.flags & MF_NOGRAVITY)) P_ZMovement(w, mo);

        const bool blocked = hadMom && mo.x == ox && mo.y == oy;
        const bool hitVert = (mo.momz < 0 && mo.z <= mo.floorz) ||
                             (mo.momz > 0 && mo.z >= mo.ceilingz - mo.height);

        mobj_t* touched = nullptr;
        const fixed_t r = mo.info ? mo.info->radius : 0;
        P_BlockThingsIterator(w, mo.x - r, mo.y - r, mo.x + r, mo.y + r,
            [&](mobj_t& th) {
                if (&th == &mo || &th == mo.target) return false;
                if (th.health <= 0 || !(th.flags & MF_SHOOTABLE)) return false;
                fixed_t tr = th.info ? th.info->radius : 0;
                if (mo.z > th.z + th.height || mo.z + mo.height < th.z) return false;   // vertical miss
                if (std::abs(th.x - mo.x) <= tr + r && std::abs(th.y - mo.y) <= tr + r) {
                    touched = &th;
                    return true;
                }
                return false;
            });
        if (touched) {
            P_DamageMobj(w, *touched, mo.target, mo.info ? mo.info->damage : 0);
            P_ExplodeMissile(w, mo);
            return;
        }
        if (blocked || hitVert) { P_ExplodeMissile(w, mo); return; }
    } else {
        if (mo.momx || mo.momy) P_XYMovement(w, mo);
        if (mo.momz || !(mo.flags & MF_NOGRAVITY)) P_ZMovement(w, mo);
    }
    // State advance (vanilla): entering S_NULL removes the mobj (puffs/blood/explosions).
    if (mo.state && mo.state->tics != -1 && mo.tics > 0) {
        if (--mo.tics == 0) P_SetMobjState(w, mo, mo.state->nextstate);
    }
}

mobj_t* P_SpawnMobj(World& w, fixed_t x, fixed_t y, fixed_t z, mobjtype_t type) {
    w.mobjs.push_back({});
    mobj_t& m = w.mobjs.back();
    m.x = x; m.y = y; m.z = z;
    m.info = &mobjinfo[type];
    m.height = m.info->height;
    m.health = m.info->spawnhealth;
    m.flags = m.info->flags;
    m.reactiontime = m.info->reactiontime;
    P_SetMobjState(m, m.info->spawnstate);
    P_SetThingPosition(w, m);
    // Seed floor/ceiling from the spawn sector so gravity/ceiling clamps are sane before the
    // first P_TryMove (runtime spawns: puffs, blood, missiles, tossed barrels).
    if (w.map) {
        int sec = sectorOf(*w.map, m.subsector);
        if (sec >= 0) {
            m.floorz   = static_cast<fixed_t>(w.map->sectors[sec].floorheight)   << FRACBITS;
            m.ceilingz = static_cast<fixed_t>(w.map->sectors[sec].ceilingheight) << FRACBITS;
            if (m.z < m.floorz) m.z = m.floorz;
            fixed_t maxz = m.ceilingz - m.height;
            if (maxz < m.floorz) maxz = m.floorz;
            if (m.z > maxz) m.z = maxz;
        }
    }
    return &m;
}

void P_SetMobjState(mobj_t& m, statenum_t sn) {
    if (sn == S_NULL) {   // removed/inactive: clear the render cache so nothing is drawn
        m.state = &states[S_NULL];
        m.tics = -1;
        m.sprname[0] = '\0';
        m.frame = 'A';
        m.fullbright = false;
        return;
    }
    m.state = &states[sn];
    m.tics = m.state->tics;
    // refresh render cache (faithful: copy state->sprite/frame into mobj)
    if (m.state->sprite != SPR_NONE)
        std::memcpy(m.sprname, sprnames[m.state->sprite], 5);
    else
        m.sprname[0] = '\0';
    m.frame = static_cast<char>('A' + (m.state->frame & FF_FRAMEMASK));
    m.fullbright = (m.state->frame & FF_FULLBRIGHT) != 0;
}

void P_SetMobjState(World& w, mobj_t& m, statenum_t sn) {
    if (sn == S_NULL) { P_RemoveMobj(w, m); return; }   // vanilla: state S_NULL unlinks
    P_SetMobjState(m, sn);
    P_DoMobjAction(w, m, m.state->action);               // vanilla calls the action on entry
}

void P_RemoveMobj(World& w, mobj_t& m) {
    // Vanilla unlinks mobjs via P_RemoveThinker after the thinker pass. During P_RunThinkers an
    // immediate erase would invalidate the iteration (and any pointer another thinker holds), so
    // defer it; outside a pass (e.g. level teardown) erase immediately.
    if (w.thinkering) { m.removePending = true; return; }
    for (auto it = w.mobjs.begin(); it != w.mobjs.end(); ++it)
        if (&*it == &m) { w.mobjs.erase(it); return; }
}

void P_SetThingPosition(World& w, mobj_t& m) {
    if (!w.map) return;
    m.subsector = R_PointInSubsector(*w.map, m.x, m.y);
}
