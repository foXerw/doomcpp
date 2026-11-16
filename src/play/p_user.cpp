#include "play/p_tick.h"
#include "play/p_pspr.h"

// Per-tic player logic (vanilla P_PlayerThink subset). SDL-free: main translates input into
// (attack held, requested weapon) and calls this once per tic.
void P_PlayerThink(World& w, bool attack, int weaponSwitch) {
    player_t& p = w.player;
    p.attackdown = attack;

    if (p.damagecount > 0) --p.damagecount;
    if (p.bonuscount  > 0) --p.bonuscount;

    // P8b: timed powers count down; berserk and the area map are permanent flags.
    for (int i = 0; i < NUMPOWERS; ++i)
        if (i != pw_strength && i != pw_allmap && p.powers[i] > 0) --p.powers[i];

    if (p.dead) return;   // no weapon handling while dead

    if (weaponSwitch >= 0 && weaponSwitch < NUMWEAPONS &&
        (p.weapons & (1u << weaponSwitch)) && weaponSwitch != p.readyweapon) {
        p.pendingweapon = weaponSwitch;    // A_WeaponReady performs the switch
    }
    P_MovePsprites(w, p);
}
