#include "p_mobj.h"
#include "p_tick.h"
#include "p_maputl.h"
#include "p_setup.h"
#include "info.h"
#include <cstring>

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
