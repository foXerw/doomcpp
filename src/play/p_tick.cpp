#include "p_tick.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_maputl.h"
#include "p_map.h"      // VIEWHEIGHT
#include "info.h"

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
    // 玩家初始 viewz
    if (w.player.mo) {
        w.player.mo->floorz = (w.player.mo->floorz ? w.player.mo->floorz : 0);
        w.player.viewz = w.player.mo->floorz + VIEWHEIGHT;
    }
}

void P_BuildThingsBySubsec(World& w) {
    if (!w.map) return;
    w.thingsBySubsec.assign(w.map->subsectors.size(), {});
    for (mobj_t& mo : w.mobjs) {
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
    for (mobj_t& mo : w.mobjs) P_MobjThinker(w, mo);
}
