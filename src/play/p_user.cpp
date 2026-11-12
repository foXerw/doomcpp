#include "play/p_tick.h"
#include "play/p_pspr.h"

// Per-tic player logic (vanilla P_PlayerThink subset). SDL-free: main translates input into
// (attack held, requested weapon) and calls this once per tic.
void P_PlayerThink(World& w, bool attack, int weaponSwitch) {
    player_t& p = w.player;
    p.attackdown = attack;
    (void)weaponSwitch;   // P5b Task 6: pendingweapon + raise/lower timing

    if (p.damagecount > 0) --p.damagecount;
    if (p.bonuscount  > 0) --p.bonuscount;

    if (p.dead || p.health <= 0) return;   // no weapon handling while dead
    P_MovePsprites(w, p);
}
