#include "play/p_tick.h"
#include "play/p_pspr.h"

// Per-tic player logic (vanilla P_PlayerThink subset). SDL-free: main translates input into
// (attack held, requested weapon) and calls this once per tic.
void P_PlayerThink(World& w, bool attack, int weaponSwitch) {
    player_t& p = w.player;
    p.attackdown = attack;

    if (p.damagecount > 0) --p.damagecount;
    if (p.bonuscount  > 0) --p.bonuscount;

    if (p.dead) return;   // no weapon handling while dead

    if (weaponSwitch >= 0 && weaponSwitch < NUMWEAPONS &&
        (p.weapons & (1u << weaponSwitch)) && weaponSwitch != p.readyweapon) {
        p.pendingweapon = weaponSwitch;    // A_WeaponReady performs the switch
    }
    P_MovePsprites(w, p);
}
