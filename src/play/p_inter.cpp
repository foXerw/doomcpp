#include "p_inter.h"
#include "play/p_pspr.h"
#include "play/info.h"
#include "core/m_fixed.h"
#include "core/m_random.h"
#include "core/m_tables.h"
#include "core/i_system.h"
#include "play/sfx.h"
#include <cmath>
#include <cstdint>

namespace {
// Direction from (x1,y1) to (x2,y2) as a BAM angle (knockback only; geometry sanity is
// what matters, not bit-exact tantoangle values).
angle_t pointToAngle(fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2) {
    double dx = static_cast<double>(static_cast<std::int64_t>(x2) - x1) / FRACUNIT;
    double dy = static_cast<double>(static_cast<std::int64_t>(y2) - y1) / FRACUNIT;
    if (dx == 0.0 && dy == 0.0) return 0;
    constexpr double TAU = 6.28318530717958647692;
    double a = std::atan2(dy, dx);
    if (a < 0.0) a += TAU;
    return static_cast<angle_t>(a / TAU * 4294967296.0);
}
}  // namespace

void P_DamageMobj(World& w, mobj_t& target, mobj_t* source, int damage) {
    if (!(target.flags & MF_SHOOTABLE)) return;
    if (target.health <= 0) return;
    if (damage <= 0) return;

    const bool isPlayer = (&target == w.player.mo);
    // P8b: the invulnerability sphere makes the player a non-target — vanilla returns
    // before any of the armour arithmetic, so damage simply does not apply.
    if (isPlayer && w.player.powers[pw_invulnerability] > 0) return;
    if (isPlayer) {
        player_t& p = w.player;
        int absorbed = 0;
        if (p.armortype == 1)      absorbed = damage / 3;   // green armor
        else if (p.armortype == 2) absorbed = damage / 2;   // blue armor
        if (absorbed > 0) {
            int& points = p.armorpoints[p.armortype - 1];
            if (points <= absorbed) {
                absorbed = points;
                points = 0;
                p.armortype = (p.armorpoints[0] > 0) ? 1 : (p.armorpoints[1] > 0 ? 2 : 0);
            } else {
                points -= absorbed;
            }
        }
        target.health -= (damage - absorbed);
        p.health = target.health;
        p.damagecount += (damage - absorbed);
        if (p.damagecount > 100) p.damagecount = 100;
    } else {
        target.health -= damage;
    }

    if (target.health <= 0) { P_KillMobj(w, target, source); return; }

    // P6b: remember who hurt us (monster infighting + A_Chase target switching).
    if (source && source != &target) {
        target.target = source;
        target.flags |= MF_JUSTHIT;
        target.reactiontime = 0;
    }

    // Pain chance (vanilla P_Random; M_Random is our deterministic RNG).
    if (target.info && target.info->painstate != S_NULL &&
        target.info->painchance > 0 &&
        static_cast<int>(M_Random() & 255) < target.info->painchance) {
        I_StartSound(isPlayer ? SFX_POPAIN : SFX_DMPAIN);
        P_SetMobjState(w, target, target.info->painstate);
    }

    // Knockback (vanilla: thrust = damage * (FRACUNIT>>3) * 100 / mass).
    if (source && !(target.flags & MF_NOCLIP)) {
        angle_t ang = pointToAngle(source->x, source->y, target.x, target.y);
        int fi = static_cast<int>((ang >> ANGLETOFINESHIFT) & FINEMASK);
        int mass = (target.info && target.info->mass > 0) ? target.info->mass : 100;
        fixed_t thrust = static_cast<fixed_t>(static_cast<int64_t>(damage) * (FRACUNIT >> 3) * 100 / mass);
        target.momx += FixedMul(thrust, finecosine[fi]);
        target.momy += FixedMul(thrust, finesine[fi]);
    }
}

void P_KillMobj(World& w, mobj_t& target, mobj_t* source) {
    const bool isPlayer = (&target == w.player.mo);

    target.flags &= ~MF_SHOOTABLE;
    target.flags |= MF_CORPSE | MF_DROPOFF;
    target.height >>= 2;   // vanilla: corpses take less space

    if (isPlayer) {
        w.player.health = 0;
        w.player.dead = true;
        P_SetPsprite(w, w.player, 0, S_NULL);   // weapon drops
        P_SetPsprite(w, w.player, 1, S_NULL);
    }

    statenum_t death = S_NULL;
    if (target.info) {
        const bool gibbed = target.info->xdeathstate != S_NULL &&
                            target.health < -target.info->spawnhealth;
        death = gibbed ? target.info->xdeathstate : target.info->deathstate;
    }
    if (death != S_NULL) {
        P_SetMobjState(w, target, death);
    } else if (!isPlayer) {
        P_RemoveMobj(w, target);
    }

    if (!isPlayer && source == w.player.mo && target.info &&
        (target.info->flags & MF_COUNTKILL)) {
        ++w.player.killcount;
    }
}
