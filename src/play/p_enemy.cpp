#include "p_enemy.h"
#include "play/p_map.h"
#include "play/p_maputl.h"
#include "play/p_inter.h"
#include "play/p_setup.h"
#include "play/p_spec.h"
#include "core/m_fixed.h"
#include "core/m_random.h"
#include "core/m_tables.h"
#include "core/i_system.h"
#include "play/sfx.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

constexpr fixed_t MAXSIGHT = 1800 << FRACBITS;   // vanilla MAXSIGHT
constexpr fixed_t MONSTER_MELEE = 64 << FRACBITS;

double toUnits(fixed_t v) { return static_cast<double>(v) / FRACUNIT; }

angle_t angleTo(fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2) {
    const double dx = toUnits(static_cast<fixed_t>(static_cast<std::int64_t>(x2) - x1));
    const double dy = toUnits(static_cast<fixed_t>(static_cast<std::int64_t>(y2) - y1));
    if (dx == 0.0 && dy == 0.0) return 0;
    constexpr double TAU = 6.28318530717958647692;
    double a = std::atan2(dy, dx);
    if (a < 0.0) a += TAU;
    return static_cast<angle_t>(a / TAU * 4294967296.0);
}

fixed_t aproxDistance(fixed_t dx, fixed_t dy) {
    // int64 subtraction before abs avoids overflow on extreme map coordinates.
    std::int64_t ax = std::llabs(static_cast<std::int64_t>(dx));
    std::int64_t ay = std::llabs(static_cast<std::int64_t>(dy));
    if (ax < ay) return static_cast<fixed_t>(ax + ay - (ax >> 1));
    return static_cast<fixed_t>(ax + ay - (ay >> 1));
}

// Two 2D segments (a->b, c->d) intersect strictly inside both spans.
bool segmentsCross(double ax, double ay, double bx, double by,
                   double cx, double cy, double dx, double dy) {
    const double rx = bx - ax, ry = by - ay;
    const double sx = dx - cx, sy = dy - cy;
    const double denom = rx * sy - ry * sx;
    if (std::fabs(denom) < 1e-9) return false;
    const double qx = cx - ax, qy = cy - ay;
    const double t = (qx * sy - qy * sx) / denom;
    const double u = (qx * ry - qy * rx) / denom;
    return t > 0.0 && t < 1.0 && u > 0.0 && u < 1.0;
}

// The monster's eye height above its feet, used for sight/opening checks.
fixed_t eyeHeight(const mobj_t& mo) {
    fixed_t h = mo.height ? mo.height : (mo.info ? mo.info->height : 0);
    return mo.z + h - (h >> 2);   // 3/4 up the body
}

int monsterSightSfx(const mobj_t& mo) {
    if (!mo.info) return SFX_NONE;
    if (mo.info == &mobjinfo[MT_POSSESSED]) {
        static const int posSit[3] = { SFX_POSIT1, SFX_POSIT2, SFX_POSIT3 };
        return posSit[M_Random() % 3];
    }
    if (mo.info == &mobjinfo[MT_SHOTGUY])  return SFX_SGTSIT;
    if (mo.info == &mobjinfo[MT_TROOP])    return SFX_BGSIT1;
    if (mo.info == &mobjinfo[MT_SERGEANT]) return SFX_BGSIT2;
    if (mo.info == &mobjinfo[MT_BRUISER])  return SFX_BRSSIT;
    if (mo.info == &mobjinfo[MT_HEAD])     return SFX_CACSIT;
    if (mo.info == &mobjinfo[MT_CYBORG])   return SFX_CYBSIT;
    if (mo.info == &mobjinfo[MT_SPIDER])   return SFX_SPISIT;
    return SFX_NONE;
}

int monsterDeathSfx(const mobj_t& mo) {
    if (!mo.info) return SFX_NONE;
    if (mo.info == &mobjinfo[MT_SKULL])    return SFX_SKLDTH;
    if (mo.info == &mobjinfo[MT_HEAD])     return SFX_CACDTH;
    if (mo.info == &mobjinfo[MT_CYBORG])   return SFX_CYBDTH;
    if (mo.info == &mobjinfo[MT_SPIDER])   return SFX_SPIDTH;
    if (mo.info == &mobjinfo[MT_BRUISER])  return (M_Random() & 1) ? SFX_BGDTH1 : SFX_BGDTH2;
    if (mo.info == &mobjinfo[MT_SHOTGUY])  return SFX_SGTDTH;
    if (mo.info == &mobjinfo[MT_TROOP])    return SFX_BGDTH1;
    static const int posDth[3] = { SFX_PODTH1, SFX_PODTH2, SFX_PODTH3 };
    return posDth[M_Random() % 3];
}

void faceTarget(mobj_t& mo) {
    if (mo.target) mo.angle = angleTo(mo.x, mo.y, mo.target->x, mo.target->y);
}

// One step in `ang`; returns true when P_TryMove accepted it.
bool tryStep(World& w, mobj_t& mo, angle_t ang) {
    const int fi = static_cast<int>((ang >> ANGLETOFINESHIFT) & FINEMASK);
    const fixed_t speed = mo.info ? mo.info->speed : 0;
    return P_TryMove(w, mo, mo.x + FixedMul(finecosine[fi], speed),
                            mo.y + FixedMul(finesine[fi], speed));
}

// Vanilla P_Move: try the desired direction, then progressively wider detours.
bool moveMonster(World& w, mobj_t& mo, angle_t ang) {
    if (tryStep(w, mo, ang)) return true;
    if (tryStep(w, mo, ang + ANG45)) return true;
    if (tryStep(w, mo, ang - ANG45)) return true;
    if (tryStep(w, mo, ang + ANG90)) return true;
    if (tryStep(w, mo, ang - ANG90)) return true;
    return false;
}

// --- attack helpers ---

void posAttack(World& w, mobj_t& mo) {
    if (!mo.target) return;
    faceTarget(mo);
    if (!P_CheckSight(w, mo, *mo.target)) return;
    I_StartSound(SFX_POSACT);
    angle_t ang = mo.angle + static_cast<angle_t>((M_Random() - M_Random()) << 18);
    fixed_t slope = 0;
    const fixed_t z = mo.z + ((mo.height ? mo.height : mo.info->height) >> 1);
    P_AimLineAttack(w, &mo, mo.x, mo.y, z, ang, MISSILERANGE, slope);
    P_LineAttack(w, &mo, mo.x, mo.y, z, ang, slope, (M_Random() % 5 + 1) * 3);
}

void shotgunAttack(World& w, mobj_t& mo, int shots) {
    if (!mo.target) return;
    faceTarget(mo);
    if (!P_CheckSight(w, mo, *mo.target)) return;
    I_StartSound(SFX_SGTATK);
    const fixed_t z = mo.z + ((mo.height ? mo.height : mo.info->height) >> 1);
    for (int i = 0; i < shots; ++i) {
        angle_t ang = mo.angle + static_cast<angle_t>((M_Random() - M_Random()) << 18);
        fixed_t slope = 0;
        P_AimLineAttack(w, &mo, mo.x, mo.y, z, ang, MISSILERANGE, slope);
        P_LineAttack(w, &mo, mo.x, mo.y, z, ang, slope, (M_Random() % 5 + 1) * 3);
    }
}

void meleeAttack(World& w, mobj_t& mo, int maxDamage, int dieSize) {
    if (!mo.target) return;
    faceTarget(mo);
    const fixed_t dist = aproxDistance(mo.target->x - mo.x, mo.target->y - mo.y);
    if (dist >= MONSTER_MELEE) return;
    const fixed_t z = mo.z + ((mo.height ? mo.height : mo.info->height) >> 1);
    fixed_t slope = 0;
    I_StartSound(SFX_CLAW);
    P_AimLineAttack(w, &mo, mo.x, mo.y, z, mo.angle, MONSTER_MELEE, slope);
    P_LineAttack(w, &mo, mo.x, mo.y, z, mo.angle, slope,
                 maxDamage * (M_Random() % dieSize + 1));
}

// Missile choice for imp/baron/caco/cyber: melee when adjacent, else a projectile.
void missileOrMelee(World& w, mobj_t& mo, mobjtype_t type,
                    int meleeMax, int meleeDie) {
    if (!mo.target) return;
    faceTarget(mo);
    const fixed_t dist = aproxDistance(mo.target->x - mo.x, mo.target->y - mo.y);
    if (meleeMax > 0 && dist < MONSTER_MELEE) {
        meleeAttack(w, mo, meleeMax, meleeDie);
        return;
    }
    if (!P_CheckSight(w, mo, *mo.target)) return;
    I_StartSound(type == MT_BRUISERSHOT ? SFX_BGACT :
                 type == MT_ROCKET      ? SFX_RLAUNC : SFX_FIRSHT);
    P_SpawnMissile(w, mo, *mo.target, type);
}

}  // namespace

bool P_CheckSight(const World& w, const mobj_t& observer, const mobj_t& target) {
    if (!w.map) return false;
    const double ax = toUnits(observer.x), ay = toUnits(observer.y);
    const double bx = toUnits(target.x),   by = toUnits(target.y);
    const double z  = toUnits(eyeHeight(observer));
    const MapData& m = *w.map;
    for (const line_t& L : m.lines) {
        if (L.v1 < 0 || L.v2 < 0 ||
            L.v1 >= static_cast<int>(m.vertices.size()) ||
            L.v2 >= static_cast<int>(m.vertices.size())) continue;
        const double x1 = toUnits(m.vertices[L.v1].x), y1 = toUnits(m.vertices[L.v1].y);
        const double x2 = toUnits(m.vertices[L.v2].x), y2 = toUnits(m.vertices[L.v2].y);
        if (!segmentsCross(ax, ay, bx, by, x1, y1, x2, y2)) continue;
        if (L.sidenum[1] < 0) return false;                 // one-sided wall
        const Opening op = P_LineOpening(m, L);
        if (z < toUnits(op.bottom) || z > toUnits(op.top)) return false;   // closed door/low ceiling
    }
    return true;
}

bool P_LookForPlayers(World& w, mobj_t& actor) {
    mobj_t* pl = w.player.mo;
    if (!pl || &actor == pl) return false;
    if (w.player.dead || w.player.health <= 0 || pl->health <= 0) return false;
    const fixed_t dist = aproxDistance(pl->x - actor.x, pl->y - actor.y);
    if (dist > MAXSIGHT) return false;
    if (!(actor.flags & MF_AMBUSH)) {
        angle_t diff = angleTo(actor.x, actor.y, pl->x, pl->y) - actor.angle;
        if (diff > ANG180) diff = 0u - diff;
        if (diff > ANG90) return false;                     // outside the ~180° FOV
    }
    if (!P_CheckSight(w, actor, *pl)) return false;
    actor.target = pl;
    return true;
}

void P_NoiseAlert(World& w, mobj_t* source) {
    if (!source || !w.player.mo) return;
    const fixed_t range = 1200 << FRACBITS;
    for (mobj_t& mo : w.mobjs) {
        if (&mo == source || mo.removePending) continue;
        if (!(mo.flags & MF_COUNTKILL) || mo.health <= 0) continue;
        if (mo.target) continue;
        if (aproxDistance(source->x - mo.x, source->y - mo.y) > range) continue;
        if (P_CheckSight(w, mo, *source)) {
            mo.target = source;
            mo.reactiontime = 0;
        }
    }
}

mobj_t* P_SpawnMissile(World& w, mobj_t& source, mobj_t& dest, mobjtype_t type) {
    if (!w.map) return nullptr;
    const fixed_t zh = source.z + ((source.height ? source.height : source.info->height) >> 1);
    const angle_t ang = angleTo(source.x, source.y, dest.x, dest.y);
    const int fi = static_cast<int>((ang >> ANGLETOFINESHIFT) & FINEMASK);
    // Spawn 32 units ahead: a projectile spawned inside its (solid) shooter would fail
    // the thing-collision check and never move.
    const fixed_t ahead = 32 << FRACBITS;
    mobj_t* th = P_SpawnMobj(w, source.x + FixedMul(finecosine[fi], ahead),
                                source.y + FixedMul(finesine[fi], ahead), zh, type);
    th->angle = ang;
    th->target = &source;
    const fixed_t speed = th->info->speed;
    th->momx = FixedMul(finecosine[fi], speed);
    th->momy = FixedMul(finesine[fi], speed);
    fixed_t dist = aproxDistance(dest.x - th->x, dest.y - th->y);
    if (dist < FRACUNIT) dist = FRACUNIT;
    const fixed_t dz = (dest.z + (dest.height >> 1)) - th->z;
    const fixed_t slope = FixedDiv(dz, dist);
    th->momz = FixedMul(speed, slope);
    th->floorz = source.floorz;
    th->ceilingz = source.ceilingz;
    return th;
}

void P_SkullFly(World& w, mobj_t& mo) {
    const fixed_t r = mo.info ? mo.info->radius : 0;
    const fixed_t nh = mo.height ? mo.height : (mo.info ? mo.info->height : 0);
    mobj_t* hit = nullptr;
    const fixed_t nx = mo.x + mo.momx, ny = mo.y + mo.momy;
    P_BlockThingsIterator(w, nx - r, ny - r, nx + r, ny + r, [&](mobj_t& th) {
        if (&th == &mo || th.removePending) return false;
        if (th.health <= 0 || !(th.flags & MF_SHOOTABLE)) return false;
        const fixed_t tr = th.info ? th.info->radius : 0;
        if (std::abs(th.x - nx) >= r + tr || std::abs(th.y - ny) >= r + tr) return false;
        const fixed_t thh = th.height ? th.height : (th.info ? th.info->height : 0);
        if (mo.z >= th.z + thh || mo.z + nh <= th.z) return false;
        hit = &th;
        return true;
    });
    const bool moved = P_TryMove(w, mo, nx, ny);
    if (!moved || hit) {
        if (hit) P_DamageMobj(w, *hit, &mo, 3 * (M_Random() % 8 + 1));
        mo.momx = mo.momy = mo.momz = 0;
        mo.flags &= ~MF_SKULLFLY;
        if (mo.info && mo.info->seestate != S_NULL)
            P_SetMobjState(w, mo, mo.info->seestate);
        return;
    }
    mo.z += mo.momz;
    fixed_t maxz = mo.ceilingz - nh;
    if (maxz < mo.floorz) maxz = mo.floorz;
    if (mo.z < mo.floorz) mo.z = mo.floorz;
    if (mo.z > maxz) { mo.z = maxz; mo.momz = 0; }
}

namespace {

void doLook(World& w, mobj_t& mo) {
    if (P_LookForPlayers(w, mo) && mo.info && mo.info->seestate != S_NULL) {
        I_StartSound(monsterSightSfx(mo));
        P_SetMobjState(w, mo, mo.info->seestate);
    }
}

void doChase(World& w, mobj_t& mo) {
    if (mo.flags & MF_SKULLFLY) {                    // charge lapsed
        mo.flags &= ~MF_SKULLFLY;
        mo.momx = mo.momy = mo.momz = 0;
    }
    if (mo.info && mo.info->reactiontime > 0 && mo.reactiontime > 0) {
        --mo.reactiontime;
        return;
    }
    if (!mo.target || mo.target->health <= 0 || mo.target->removePending ||
        !(mo.target->flags & MF_SHOOTABLE)) {
        mo.target = nullptr;
        if (!P_LookForPlayers(w, mo)) return;
    }
    mobj_t* t = mo.target;
    faceTarget(mo);
    const bool sight = P_CheckSight(w, mo, *t);
    const fixed_t dist = aproxDistance(t->x - mo.x, t->y - mo.y);

    if (sight && mo.info) {
        if (mo.info->meleestate != S_NULL && dist < MONSTER_MELEE) {
            P_SetMobjState(w, mo, mo.info->meleestate);
            return;
        }
        if (mo.info->missilestate != S_NULL && dist < MISSILERANGE &&
            (M_Random() & 255) < 200) {
            P_SetMobjState(w, mo, mo.info->missilestate);
            return;
        }
    }
    // Vanilla chases even without sight (last known direction); our sight check only
    // gates attacks, and movement always heads for the target.
    if (!moveMonster(w, mo, mo.angle))
        P_UseLines(w, mo);                 // blocked -> try opening a door in front
}

void doBossDeath(World& w, mobj_t& mo) {
    if (!mo.info) return;
    for (const mobj_t& other : w.mobjs) {
        if (&other == &mo || other.removePending) continue;
        if (other.info == mo.info && other.health > 0) return;   // other boss alive
    }
    // Boss levels: killing the last boss ends the level directly (vanilla g_game.c).
    if (w.mapName == "E2M8" || w.mapName == "E3M8") {
        w.levelExitRequested = true;
        w.secretExit = false;
        return;
    }
    P_LowerFloorByTag(w, 666);   // E1M8/E4M8: lower the tag-666 floor to reveal the exit
}

}  // namespace

void P_MobjAction(World& w, mobj_t& mo, action_t action) {
    switch (action) {
    case A_Look:        doLook(w, mo); break;
    case A_Chase:       doChase(w, mo); break;
    case A_FaceTarget:  faceTarget(mo); break;
    case A_PosAttack:   posAttack(w, mo); break;
    case A_SPosAttack:  shotgunAttack(w, mo, 3); break;
    case A_SpidAttack:  shotgunAttack(w, mo, 3); break;
    case A_SargAttack:  meleeAttack(w, mo, 4, 10); break;
    case A_TroopAttack: missileOrMelee(w, mo, MT_TROOPSHOT, 3, 8); break;
    case A_HeadAttack:  missileOrMelee(w, mo, MT_HEADSHOT, 6, 6); break;
    case A_BruisAttack: missileOrMelee(w, mo, MT_BRUISERSHOT, 8, 8); break;
    case A_CyberAttack:
        if (mo.target && P_CheckSight(w, mo, *mo.target)) {
            I_StartSound(SFX_RLAUNC);
            P_SpawnMissile(w, mo, *mo.target, MT_ROCKET);
        }
        break;
    case A_SkullAttack: {
        if (!mo.target) break;
        I_StartSound(SFX_SKLATK);
        faceTarget(mo);
        const fixed_t dx = mo.target->x - mo.x;
        const fixed_t dy = mo.target->y - mo.y;
        fixed_t dist = aproxDistance(dx, dy);
        if (dist < FRACUNIT) dist = FRACUNIT;
        const fixed_t speed = 20 << FRACBITS;         // charge speed (units/tic)
        mo.momx = FixedDiv(FixedMul(dx, speed), dist);
        mo.momy = FixedDiv(FixedMul(dy, speed), dist);
        const fixed_t dz = (mo.target->z + (mo.target->height >> 1)) - mo.z;
        mo.momz = FixedDiv(FixedMul(dz, speed), dist);
        mo.flags |= MF_SKULLFLY;
        break;
    }
    case A_Pain:
        if (&mo == w.player.mo) I_StartSound(SFX_PLPAIN);
        else                    I_StartSound(SFX_DMPAIN);
        break;
    case A_Scream:
        I_StartSound(monsterDeathSfx(mo));
        break;
    case A_XScream:
        I_StartSound(SFX_SLOP);
        break;
    case A_PlayerScream:
        I_StartSound(SFX_PDIEHI);
        break;
    case A_Fall:
        mo.flags &= ~MF_SOLID;                        // corpses stop blocking
        break;
    case A_BossDeath:
        doBossDeath(w, mo);
        break;
    case A_Explode:
        I_StartSound(mo.info == &mobjinfo[MT_BARREL] ? SFX_BAREXP : SFX_RXPLOD);
        P_RadiusAttack(w, mo, 128);
        break;
    case A_BFGSpray:
        if (mo.info) P_RadiusAttack(w, mo, mo.info->damage);
        break;
    default:
        break;
    }
}
