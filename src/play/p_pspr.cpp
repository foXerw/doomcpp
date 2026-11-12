#include "p_pspr.h"
#include "play/p_map.h"
#include "play/info.h"
#include "core/m_fixed.h"
#include "core/m_random.h"
#include "core/m_tables.h"

namespace {
bool hasAmmo(const player_t& p, int wp) {
    ammotype_t at = weaponinfo[wp].ammo;
    return at == AM_NOAMMO || p.ammo[at] > 0;
}

bool useAmmo(player_t& p, ammotype_t at, int amount) {
    if (at == AM_NOAMMO) return true;
    if (p.ammo[at] < amount) return false;
    p.ammo[at] -= amount;
    return true;
}

// One hitscan pellet with vanilla spread/damage randomisation. Auto-aim sets the slope.
void firePellet(World& w, player_t& p, fixed_t range, int damage) {
    mobj_t* mo = p.mo;
    if (!mo) return;
    angle_t ang = mo->angle + static_cast<angle_t>((M_Random() - M_Random()) << 18);
    fixed_t slope = 0;
    P_AimLineAttack(w, mo, mo->x, mo->y, p.viewz, ang, range, slope);
    P_LineAttack(w, mo, mo->x, mo->y, p.viewz, ang, slope, damage);
}

void fireWeapon(World& w, player_t& p) {
    if (p.dead || p.health <= 0) return;
    int wp = p.readyweapon;
    if (!hasAmmo(p, wp)) {
        // Out of ammo: vanilla switches to the best available weapon (fist as fallback).
        for (int i = NUMWEAPONS - 1; i >= 0; --i)
            if ((p.weapons & (1u << i)) && hasAmmo(p, i)) { p.pendingweapon = i; return; }
        return;
    }
    P_SetPsprite(w, p, 0, weaponinfo[wp].atkstate);
}

void P_DoWeaponAction(World& w, player_t& p, int index, action_t action) {
    if (index != 0) {                       // flash psprite actions are light-only
        if (action == A_Light0) P_SetPsprite(w, p, 1, S_NULL);
        return;
    }
    switch (action) {
    case A_WeaponReady:
        if (p.pendingweapon != NUMWEAPONS && p.pendingweapon != p.readyweapon) {
            P_SetPsprite(w, p, 0, weaponinfo[p.readyweapon].downstate);
        } else if (p.attackdown) {
            fireWeapon(w, p);
        }
        break;
    case A_Lower:
        if (p.pendingweapon != NUMWEAPONS && (p.weapons & (1u << p.pendingweapon)))
            p.readyweapon = p.pendingweapon;
        p.pendingweapon = NUMWEAPONS;
        P_SetPsprite(w, p, 0, weaponinfo[p.readyweapon].upstate);
        break;
    case A_Raise:
        P_SetPsprite(w, p, 0, weaponinfo[p.readyweapon].readystate);
        break;
    case A_ReFire:
        if (p.attackdown && hasAmmo(p, p.readyweapon))
            P_SetPsprite(w, p, 0, weaponinfo[p.readyweapon].atkstate);
        else
            P_SetPsprite(w, p, 0, weaponinfo[p.readyweapon].readystate);
        break;
    case A_FirePistol:
        if (useAmmo(p, AM_CLIP, 1)) {
            P_SetPsprite(w, p, 1, S_PISTOLFLASH);
            firePellet(w, p, MISSILERANGE, 5 * (M_Random() % 3 + 1));
        }
        break;
    case A_FireCGun:
        if (useAmmo(p, AM_CLIP, 1)) {
            P_SetPsprite(w, p, 1, S_CHAINFLASH1);
            firePellet(w, p, MISSILERANGE, 5 * (M_Random() % 3 + 1));
        }
        break;
    case A_FireShotgun:
        if (useAmmo(p, AM_SHELL, 1)) {
            P_SetPsprite(w, p, 1, S_SGUNFLASH1);
            for (int i = 0; i < 7; ++i) firePellet(w, p, MISSILERANGE, 5 * (M_Random() % 3 + 1));
        }
        break;
    case A_FireMissile:
        if (useAmmo(p, AM_MISL, 1)) {
            P_SetPsprite(w, p, 1, S_MISSILEFLASH1);
            P_SpawnPlayerMissile(w, p, MT_ROCKET);
        }
        break;
    case A_FirePlasma:
        if (useAmmo(p, AM_CELL, 1)) {
            P_SetPsprite(w, p, 1, S_PLASMAFLASH1);
            P_SpawnPlayerMissile(w, p, MT_PLASMA);
        }
        break;
    case A_FireBFG:
        // Vanilla BFG uses 40 cells. Spread is simplified to a radial blast on impact.
        if (useAmmo(p, AM_CELL, 40)) {
            P_SetPsprite(w, p, 1, S_BFGFLASH1);
            P_SpawnPlayerMissile(w, p, MT_BFG);
        }
        break;
    case A_Punch:
        firePellet(w, p, MELEERANGE, (M_Random() % 10 + 1) << 1);
        break;
    case A_Saw:
        firePellet(w, p, MELEERANGE, 2 * (M_Random() % 10 + 1));
        break;
    case A_GunFlash:
        P_SetPsprite(w, p, 1, weaponinfo[p.readyweapon].flashstate);
        break;
    case A_Light0:
    case A_Light1:
    case A_Light2:
        break;   // no dynamic lighting in P5b
    default:
        break;   // AI/sound actions land in P6/P7
    }
}
}  // namespace

void P_SetPsprite(World& w, player_t& p, int index, statenum_t st) {
    pspdef_t& ps = p.psprites[index];
    ps.state = &states[st];
    ps.tics  = ps.state->tics;
    P_DoWeaponAction(w, p, index, ps.state->action);
}

void P_MovePsprites(World& w, player_t& p) {
    for (int i = 0; i < 2; ++i) {
        pspdef_t& ps = p.psprites[i];
        if (!ps.state || ps.tics == -1) continue;
        if (--ps.tics <= 0) P_SetPsprite(w, p, i, ps.state->nextstate);
    }
}
