#include "play/p_tick.h"
#include "play/p_pspr.h"
#include "play/p_map.h"      // VIEWHEIGHT
#include "play/p_inter.h"    // P_DamageMobj (hazard floors)
#include "play/p_setup.h"    // sector_t
#include "play/p_maputl.h"   // sectorOf
#include "play/sfx.h"
#include "core/i_system.h"   // I_StartSound
#include <cstring>

namespace {
// Doomcpp's own hazard calibration, keyed on the flat a sector is floored with: toxic goo
// bites slowly, lava fast. (Freedoom ships NUKAGE1-3, SLIME01-16 and LAVA1-4; BLOOD flats
// are scenery and harmless, as in vanilla.) The intervals are doomcpp's numbers, chosen so
// that a dip in goo is survivable and lava is a reason to move.
struct Hazard { const char* prefix; int damage; int interval; };
const Hazard kHazards[] = {
    {"NUKAGE", 5, 32},
    {"SLIME",  8, 32},
    {"LAVA",   8, 13},
};
}  // namespace

// Standing with your feet on a hazard flat hurts on a timer. A radiation suit stretches the
// interval by four; invulnerability is already handled inside P_DamageMobj.
static void P_CheckHazardFloor(World& w) {
    player_t& p = w.player;
    if (!w.map || !p.mo || p.dead) return;
    const int sec = sectorOf(*w.map, p.mo->subsector);
    if (sec < 0) return;
    const sector_t& s = w.map->sectors[sec];
    if (p.mo->z > (static_cast<fixed_t>(s.floorheight) << FRACBITS) + (8 << FRACBITS))
        return;                                   // on a ledge above the goo
    for (const Hazard& h : kHazards) {
        if (std::strncmp(s.floorpic, h.prefix, std::strlen(h.prefix)) != 0) continue;
        int interval = h.interval;
        if (p.powers[pw_ironfeet] > 4 * 32) interval *= 4;
        if (static_cast<int>(w.tic % static_cast<std::uint32_t>(interval)) == 0) {
            I_StartSound(SFX_OOF);
            P_DamageMobj(w, *p.mo, nullptr, h.damage);
        }
        break;
    }
}

// Per-tic player logic (vanilla P_PlayerThink subset). SDL-free: main translates input into
// (attack held, requested weapon) and calls this once per tic.
void P_PlayerThink(World& w, bool attack, int weaponSwitch) {
    player_t& p = w.player;
    p.attackdown = attack;

    if (p.damagecount > 0) --p.damagecount;
    if (p.bonuscount  > 0) --p.bonuscount;

    P_CheckHazardFloor(w);

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
