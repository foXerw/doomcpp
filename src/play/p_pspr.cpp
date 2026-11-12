#include "p_pspr.h"
#include "play/info.h"

// P5b Task 6 adds the action dispatch (A_WeaponReady/A_Fire*/A_ReFire/A_Raise/A_Lower...).
// For now P_SetPsprite only installs the state; P_MovePsprites advances its tics.
void P_SetPsprite(World& w, player_t& p, int index, statenum_t st) {
    (void)w;
    pspdef_t& ps = p.psprites[index];
    ps.state = &states[st];
    ps.tics  = ps.state->tics;
}

void P_MovePsprites(World& w, player_t& p) {
    for (int i = 0; i < 2; ++i) {
        pspdef_t& ps = p.psprites[i];
        if (!ps.state || ps.tics == -1) continue;
        if (--ps.tics <= 0) P_SetPsprite(w, p, i, ps.state->nextstate);
    }
}
