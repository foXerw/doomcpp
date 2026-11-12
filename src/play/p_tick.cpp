#include "p_tick.h"
#include "p_mobj.h"
#include "p_pspr.h"
#include "p_setup.h"
#include "p_maputl.h"
#include "p_map.h"      // VIEWHEIGHT
#include "info.h"
#include "core/m_fixed.h"    // FixedMul
#include "core/m_tables.h"   // finecosine/finesine
#include <iterator>          // std::next

void P_SetupLevel(World& w, const MapData& m) {
    w.mobjs.clear();
    w.player.mo = nullptr;
    for (const thing_t& th : m.things) {
        int mt = mobjtypeForDoomednum(th.type);
        if (mt == MT_PLAYER) {
            // 玩家单独 spawn 并挂到 player.mo（第一人称不画自己 -> P_BuildThingsBySubsec 跳过）
            mobj_t* p = P_SpawnMobj(w, static_cast<fixed_t>(th.x)<<FRACBITS,
                                    static_cast<fixed_t>(th.y)<<FRACBITS, 0, MT_PLAYER);
            p->angle = degreesToBam(th.angleDeg);
            w.player.mo = p;
        } else if (mt >= 0) {
            mobj_t* mo = P_SpawnMobj(w, static_cast<fixed_t>(th.x)<<FRACBITS,
                                     static_cast<fixed_t>(th.y)<<FRACBITS, 0,
                                     static_cast<mobjtype_t>(mt));
            mo->angle = degreesToBam(th.angleDeg);
        }
    }
    // 给每个 mobj 定 z=floor、floorz/ceilingz、light（用其 subsector 的 sector）
    for (mobj_t& mo : w.mobjs) {
        int sec = (mo.subsector >= 0) ? sectorOf(m, mo.subsector) : -1;
        if (sec >= 0) {
            mo.z        = static_cast<fixed_t>(m.sectors[sec].floorheight)   << FRACBITS;
            mo.floorz   = mo.z;
            mo.ceilingz = static_cast<fixed_t>(m.sectors[sec].ceilingheight) << FRACBITS;
            mo.light    = m.sectors[sec].lightlevel;
        }
    }
    // P5b: reset combat state and give the player the pistol (vanilla G_PlayerReborn subset).
    if (w.player.mo) {
        mobj_t* mo = w.player.mo;
        fixed_t viewz = mo->floorz + VIEWHEIGHT;
        w.player = player_t{};
        w.player.mo = mo;
        w.player.viewz = viewz;
        w.player.health = mobjinfo[MT_PLAYER].spawnhealth;
        mo->health = w.player.health;
        for (int i = 0; i < NUMAMMO; ++i) w.player.maxammo[i] = ammoinfo[i].maxammo;
        w.player.ammo[AM_CLIP] = 50;                          // vanilla pistol start
        w.player.weapons = (1u << WP_FIST) | (1u << WP_PISTOL);
        w.player.readyweapon = WP_PISTOL;
        w.player.pendingweapon = NUMWEAPONS;                  // no change pending
        P_SetPsprite(w, w.player, 0, weaponinfo[WP_PISTOL].readystate);
        P_SetPsprite(w, w.player, 1, S_NULL);
    }
}

void P_BuildThingsBySubsec(World& w) {
    if (!w.map) return;
    w.thingsBySubsec.assign(w.map->subsectors.size(), {});
    for (mobj_t& mo : w.mobjs) {
        if (mo.removePending) continue;                // unlinked after this thinker pass
        if (&mo == w.player.mo) continue;              // 第一人称：不画玩家自身
        if (mo.flags & MF_NOSECTOR) continue;
        mo.subsector = R_PointInSubsector(*w.map, mo.x, mo.y);
        if (mo.subsector >= 0 && mo.subsector < static_cast<int>(w.thingsBySubsec.size()))
            w.thingsBySubsec[mo.subsector].push_back(&mo);
        // 刷新 light（物体可能移到新 sector）
        int sec = sectorOf(*w.map, mo.subsector);
        if (sec >= 0) mo.light = mo.fullbright ? 255 : w.map->sectors[sec].lightlevel;
    }
}

void P_RunThinkers(World& w) {
    ++w.tic;
    w.thinkering = true;
    for (auto it = w.mobjs.begin(); it != w.mobjs.end(); ) {
        auto next = std::next(it);              // removal of *it (deferred) keeps `next` valid
        if (!it->removePending) P_MobjThinker(w, *it);
        it = next;
    }
    w.thinkering = false;
    w.mobjs.remove_if([](const mobj_t& m) { return m.removePending; });
}

mobj_t* P_TossBarrel(World& w) {
    mobj_t* mo = w.player.mo;
    if (!mo) return nullptr;
    int fi = (mo->angle >> ANGLETOFINESHIFT) & FINEMASK;
    fixed_t fc = finecosine[fi], fs = finesine[fi];
    fixed_t dist = 48 << FRACBITS;
    mobj_t* b = P_SpawnMobj(w, mo->x + FixedMul(fc, dist), mo->y + FixedMul(fs, dist),
                            mo->floorz, MT_BARREL);
    b->floorz = mo->floorz;
    b->ceilingz = mo->ceilingz;
    b->momx = FixedMul(fc, 10 << FRACBITS);
    b->momy = FixedMul(fs, 10 << FRACBITS);
    b->momz = 9 << FRACBITS;   // upward toss
    return b;
}
