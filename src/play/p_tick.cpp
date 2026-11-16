#include "p_tick.h"
#include "p_mobj.h"
#include "p_pspr.h"
#include "p_spec.h"     // P_RunMovers
#include "p_setup.h"
#include "p_maputl.h"
#include "p_map.h"      // VIEWHEIGHT
#include "info.h"
#include "core/m_fixed.h"    // FixedMul
#include "core/m_tables.h"   // finecosine/finesine
#include <algorithm>
#include <iterator>          // std::next

void P_SetupLevel(World& w, const MapData& m, const player_t* carry) {
    player_t saved{};
    if (carry) {
        saved.health = carry->health;
        for (int i = 0; i < 2; ++i) saved.armorpoints[i] = carry->armorpoints[i];
        saved.armortype = carry->armortype;
        for (int i = 0; i < NUMAMMO; ++i) saved.ammo[i] = carry->ammo[i];
        for (int i = 0; i < NUMAMMO; ++i) saved.maxammo[i] = carry->maxammo[i];
        saved.weapons = carry->weapons;
        saved.readyweapon = carry->readyweapon;
        saved.keys = carry->keys;
    }
    w.mobjs.clear();
    w.player.mo = nullptr;
    w.movers.clear();
    w.lights.clear();
    w.levelExitRequested = false;
    w.secretExit = false;
    w.totalKills = 0;
    w.totalItems = 0;
    w.totalSecrets = 0;
    w.secretFound.assign(m.sectors.size(), 0);
    for (const sector_t& s : m.sectors) if (s.special == 9) ++w.totalSecrets;
    P_InitLights(w);   // P8b: sectors whose type animates their lightlevel
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
            if (mo.flags & MF_FLOAT) {       // P6b: lost souls/cacos hover above the floor
                fixed_t hover = mo.z + (24 << FRACBITS);
                fixed_t maxz = mo.ceilingz - mo.height;
                mo.z = (maxz < mo.floorz) ? mo.floorz : std::min(hover, maxz);
            }
        }
        if (mo.flags & MF_COUNTKILL) ++w.totalKills;
        if (mo.flags & MF_SPECIAL)   ++w.totalItems;
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
        if (carry) {                                          // P7: carry inventory
            w.player.health = saved.health > 0 ? saved.health : 100;
            for (int i = 0; i < 2; ++i) w.player.armorpoints[i] = saved.armorpoints[i];
            w.player.armortype = saved.armortype;
            for (int i = 0; i < NUMAMMO; ++i)
                w.player.ammo[i] = std::min(saved.ammo[i], w.player.maxammo[i]);
            w.player.weapons = saved.weapons | (1u << WP_FIST);
            w.player.keys = saved.keys;
            if (saved.readyweapon >= 0 && saved.readyweapon < NUMWEAPONS &&
                (w.player.weapons & (1u << saved.readyweapon)))
                w.player.readyweapon = saved.readyweapon;
            mo->health = w.player.health;
        }
        w.player.pendingweapon = NUMWEAPONS;                  // no change pending
        P_SetPsprite(w, w.player, 0, weaponinfo[w.player.readyweapon].readystate);
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

void P_CheckSecrets(World& w, mobj_t& mo) {
    if (!w.map || &mo != w.player.mo) return;
    const int sec = sectorOf(*w.map, mo.subsector);
    if (sec < 0 || sec >= static_cast<int>(w.map->sectors.size())) return;
    if (w.map->sectors[sec].special != 9) return;
    if (sec >= static_cast<int>(w.secretFound.size()) || w.secretFound[sec]) return;
    w.secretFound[sec] = 1;
    ++w.player.secretsFound;
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
    P_RunMovers(w);   // P6a doors/lifts/floors/ceilings
    P_RunLights(w);   // P8b sector glow / flash / strobe
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
    b->target = mo;                 // explosion credit / monster infighting
    b->momx = FixedMul(fc, 10 << FRACBITS);
    b->momy = FixedMul(fs, 10 << FRACBITS);
    b->momz = 9 << FRACBITS;   // upward toss
    return b;
}
