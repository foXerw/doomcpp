#include "p_map.h"
#include "p_inter.h"
#include "p_spec.h"   // P_CrossSpecialLines (walk triggers)
#include "p_pspr.h"    // P_SetPsprite (berserk forces the fist)
#include "core/m_fixed.h"
#include "core/m_tables.h"
#include "core/i_system.h"
#include "play/sfx.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <limits>

// Vanilla m_fixed.c::P_AproxDistance (sqrt-free): |dx|+|dy| - min/2.
static fixed_t P_AproxDistance(fixed_t dx, fixed_t dy) {
    dx = std::abs(dx); dy = std::abs(dy);
    if (dx < dy) return dx + dy - (dx >> 1);
    return dx + dy - (dy >> 1);
}

PosCheck P_CheckPosition(const MapData& m, const Blockmap& bm, const mobj_t& mo, fixed_t x, fixed_t y) {
    PosCheck pc;
    fixed_t r = mo.info ? mo.info->radius : PLAYERRADIUS;
    BBox bb{ x - r, x + r, y - r, y + r };

    int sec = sectorOf(m, R_PointInSubsector(m, x, y));
    if (sec < 0) { pc.ok = false; return pc; }
    pc.floorz   = static_cast<fixed_t>(m.sectors[sec].floorheight)   << FRACBITS;
    pc.ceilingz = static_cast<fixed_t>(m.sectors[sec].ceilingheight) << FRACBITS;
    pc.dropoffz = pc.floorz;

    int xl = (bb.left   - (bm.orgx << FRACBITS)) >> MAPBLOCKSHIFT;
    int xh = (bb.right  - (bm.orgx << FRACBITS)) >> MAPBLOCKSHIFT;
    int yl = (bb.bottom - (bm.orgy << FRACBITS)) >> MAPBLOCKSHIFT;
    int yh = (bb.top    - (bm.orgy << FRACBITS)) >> MAPBLOCKSHIFT;

    static int validgen = 0;   // per-call dedup (vanilla validcount)
    ++validgen;
    for (int cy = yl; cy <= yh; ++cy)
        for (int cx = xl; cx <= xh; ++cx)
            for (int li : blockLinesInCell(bm, cx, cy)) {
                if (li < 0 || li >= static_cast<int>(m.lines.size())) continue;
                const line_t& L = m.lines[li];
                if (L.validcount == validgen) continue;
                L.validcount = validgen;
                if (L.v1 < 0 || L.v1 >= (int)m.vertices.size()) continue;
                if (L.v2 < 0 || L.v2 >= (int)m.vertices.size()) continue;
                fixed_t x1 = m.vertices[L.v1].x, y1 = m.vertices[L.v1].y;
                fixed_t x2 = m.vertices[L.v2].x, y2 = m.vertices[L.v2].y;
                fixed_t lminx = std::min(x1, x2), lmaxx = std::max(x1, x2);
                fixed_t lminy = std::min(y1, y2), lmaxy = std::max(y1, y2);
                if (bb.right <= lminx || bb.left >= lmaxx ||
                    bb.top   <= lminy || bb.bottom >= lmaxy) continue;        // bbox early-out
                if (P_BoxOnLineSide(bb, x1, y1, x2, y2) != -1) continue;      // not straddling
                if (L.sidenum[1] < 0) { pc.ok = false; pc.blockedLine = li; return pc; }  // one-sided wall
                if (L.flags & ML_BLOCKING) { pc.ok = false; pc.blockedLine = li; return pc; }  // explicit block
                Opening op = P_LineOpening(m, L);
                if (op.top      < pc.ceilingz) { pc.ceilingz = op.top;    pc.blockedLine = li; }
                if (op.bottom   > pc.floorz)   { pc.floorz   = op.bottom; pc.blockedLine = li; }
                if (op.lowfloor < pc.dropoffz) pc.dropoffz = op.lowfloor;
            }
    return pc;
}

bool P_TryMove(const MapData& m, const Blockmap& bm, mobj_t& mo, fixed_t nx, fixed_t ny) {
    PosCheck pc = P_CheckPosition(m, bm, mo, nx, ny);
    if (!pc.ok) return false;
    fixed_t h = mo.height ? mo.height : (mo.info ? mo.info->height : PLAYERHEIGHT);
    if (pc.ceilingz - pc.floorz < h) return false;   // doesn't fit vertically
    if (pc.ceilingz - mo.floorz  < h) return false;   // not enough headroom
    if (pc.floorz   - mo.floorz  > STEP_LIMIT) return false;   // step up too high
    // P6b: monsters without MF_DROPOFF refuse a step down larger than STEP_LIMIT
    // (vanilla P_TryMove: tmfloorz - tmdropoffz > 24). The player has MF_DROPOFF.
    if (!(mo.flags & MF_DROPOFF) && (pc.floorz - pc.dropoffz > STEP_LIMIT)) return false;
    mo.x = nx; mo.y = ny;
    mo.floorz   = pc.floorz;
    mo.ceilingz = pc.ceilingz;
    return true;
}

bool P_TrySlide(const MapData& m, const Blockmap& bm, mobj_t& mo, fixed_t dx, fixed_t dy) {
    if (P_TryMove(m, bm, mo, mo.x + dx, mo.y + dy)) return true;
    if (dx != 0 && P_TryMove(m, bm, mo, mo.x + dx, mo.y)) return true;
    if (dy != 0 && P_TryMove(m, bm, mo, mo.x, mo.y + dy)) return true;
    return false;
}

void P_BlockThingsIterator(World& w, fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2,
                           const std::function<bool(mobj_t&)>& fn) {
    // Lookahead: the callback may remove the current mobj (damage with no death state);
    // `next` stays valid because std::list only invalidates the erased node.
    for (auto it = w.mobjs.begin(); it != w.mobjs.end(); ) {
        auto next = std::next(it);
        if (!it->removePending) {
            mobj_t& mo = *it;
            fixed_t r = mo.info ? mo.info->radius : 0;
            if (!(mo.x + r <= x1 || mo.x - r >= x2 || mo.y + r <= y1 || mo.y - r >= y2)) {
                if (fn(mo)) return;
            }
        }
        it = next;
    }
}

namespace {
// 2D cross product of int64-shifted map-unit vectors (avoids fixed overflow).
double cross2(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

// Clamp a freshly spawned effect's z into its sector and copy floor/ceiling heights.
void clampSpawnToSector(World& w, mobj_t& mo) {
    if (!w.map) return;
    int sec = sectorOf(*w.map, R_PointInSubsector(*w.map, mo.x, mo.y));
    if (sec < 0) return;
    mo.floorz   = static_cast<fixed_t>(w.map->sectors[sec].floorheight)   << FRACBITS;
    mo.ceilingz = static_cast<fixed_t>(w.map->sectors[sec].ceilingheight) << FRACBITS;
    fixed_t maxz = mo.ceilingz - (mo.height ? mo.height : (mo.info ? mo.info->height : 0));
    if (mo.z < mo.floorz) mo.z = mo.floorz;
    if (mo.z > maxz)      mo.z = maxz;
}
}  // namespace

mobj_t* P_SpawnPuff(World& w, fixed_t x, fixed_t y, fixed_t z) {
    mobj_t* th = P_SpawnMobj(w, x, y, z, MT_PUFF);
    clampSpawnToSector(w, *th);
    return th;
}

mobj_t* P_SpawnBlood(World& w, fixed_t x, fixed_t y, fixed_t z, int damage) {
    mobj_t* th = P_SpawnMobj(w, x, y, z, MT_BLOOD);
    clampSpawnToSector(w, *th);
    th->momz = 2 * FRACUNIT;      // small upward spurt; gravity pulls it down
    (void)damage;
    return th;
}

mobj_t* P_LineAttack(World& w, mobj_t* source, fixed_t x, fixed_t y, fixed_t z,
                     angle_t angle, fixed_t slope, int damage) {
    if (!w.map) return nullptr;
    const int fi = static_cast<int>((angle >> ANGLETOFINESHIFT) & FINEMASK);
    const double ox = static_cast<double>(x) / FRACUNIT;
    const double oy = static_cast<double>(y) / FRACUNIT;
    const double oz = static_cast<double>(z) / FRACUNIT;
    const double dx = static_cast<double>(finecosine[fi]) / FRACUNIT;
    const double dy = static_cast<double>(finesine[fi]) / FRACUNIT;
    const double slopeU = static_cast<double>(slope) / FRACUNIT;
    const double rangeU = static_cast<double>(MISSILERANGE) / FRACUNIT;

    // --- nearest wall that stops the shot (two-sided openings let it pass) ---
    double wallT = std::numeric_limits<double>::infinity();
    for (const line_t& L : w.map->lines) {
        if (L.v1 < 0 || L.v1 >= static_cast<int>(w.map->vertices.size())) continue;
        if (L.v2 < 0 || L.v2 >= static_cast<int>(w.map->vertices.size())) continue;
        const double ax = static_cast<double>(w.map->vertices[L.v1].x) / FRACUNIT;
        const double ay = static_cast<double>(w.map->vertices[L.v1].y) / FRACUNIT;
        const double bx = static_cast<double>(w.map->vertices[L.v2].x) / FRACUNIT;
        const double by = static_cast<double>(w.map->vertices[L.v2].y) / FRACUNIT;
        const double sx = bx - ax, sy = by - ay;
        const double denom = cross2(dx, dy, sx, sy);
        if (std::fabs(denom) < 1e-9) continue;              // parallel
        const double t = cross2(ax - ox, ay - oy, sx, sy) / denom;
        const double u = cross2(ax - ox, ay - oy, dx, dy) / denom;
        if (t <= 0.0 || t > rangeU || t >= wallT) continue;
        if (u < 0.0 || u > 1.0) continue;
        const bool twoSided = L.sidenum[1] >= 0;
        if (twoSided) {
            Opening op = P_LineOpening(*w.map, L);
            const double top = static_cast<double>(op.top) / FRACUNIT;
            const double bot = static_cast<double>(op.bottom) / FRACUNIT;
            const double zHit = oz + slopeU * t;
            if (zHit >= bot && zHit <= top) continue;       // passes through the opening
        }
        wallT = t;
    }

    // --- nearest shootable mobj along the ray ---
    mobj_t* hit = nullptr;
    double thingT = std::numeric_limits<double>::infinity();
    {
        const double ex = ox + dx * rangeU, ey = oy + dy * rangeU;
        const double bx1 = std::min(ox, ex) - 64, bx2 = std::max(ox, ex) + 64;
        const double by1 = std::min(oy, ey) - 64, by2 = std::max(oy, ey) + 64;
        P_BlockThingsIterator(
            w, static_cast<fixed_t>(bx1 * FRACUNIT), static_cast<fixed_t>(by1 * FRACUNIT),
            static_cast<fixed_t>(bx2 * FRACUNIT), static_cast<fixed_t>(by2 * FRACUNIT),
            [&](mobj_t& mo) {
                if (&mo == source) return false;
                if (!(mo.flags & MF_SHOOTABLE) || mo.health <= 0) return false;
                const double cx = static_cast<double>(mo.x) / FRACUNIT;
                const double cy = static_cast<double>(mo.y) / FRACUNIT;
                const double rx = cx - ox, ry = cy - oy;
                const double tca = rx * dx + ry * dy;
                if (tca < 0.0) return false;
                const double r = static_cast<double>(mo.info ? mo.info->radius : 0) / FRACUNIT;
                const double perp2 = rx * rx + ry * ry - tca * tca;
                if (perp2 > r * r) return false;
                double tenter = tca - std::sqrt(std::max(0.0, r * r - perp2));
                if (tenter < 0.0) tenter = 0.0;
                if (tenter > rangeU || tenter >= thingT) return false;
                const double zHit = oz + slopeU * tenter;
                const double mz  = static_cast<double>(mo.z) / FRACUNIT;
                const double mh  = static_cast<double>(mo.height ? mo.height : (mo.info ? mo.info->height : 0)) / FRACUNIT;
                if (zHit < mz || zHit > mz + mh) return false;
                hit = &mo;
                thingT = tenter;
                return false;
            });
    }

    if (hit && thingT < wallT) {
        I_StartSound(SFX_SLOP);
        const double zHit = oz + slopeU * thingT;
        const fixed_t px = static_cast<fixed_t>((ox + dx * thingT) * FRACUNIT);
        const fixed_t py = static_cast<fixed_t>((oy + dy * thingT) * FRACUNIT);
        const fixed_t pz = static_cast<fixed_t>(zHit * FRACUNIT);
        if (!(hit->flags & MF_NOBLOOD)) P_SpawnBlood(w, px, py, pz, damage);
        else                            P_SpawnPuff(w, px, py, pz);
        P_DamageMobj(w, *hit, source, damage);
        return hit;
    }

    // Wall/range impact: puff at the impact point (clamped into the sector).
    I_StartSound(SFX_TINK);
    const double t = (wallT < rangeU) ? wallT : rangeU;
    const double zHit = oz + slopeU * t;
    P_SpawnPuff(w, static_cast<fixed_t>((ox + dx * t) * FRACUNIT),
                static_cast<fixed_t>((oy + dy * t) * FRACUNIT),
                static_cast<fixed_t>(zHit * FRACUNIT));
    return nullptr;
}

mobj_t* P_AimLineAttack(World& w, mobj_t* source, fixed_t x, fixed_t y, fixed_t z,
                        angle_t angle, fixed_t distance, fixed_t& slopeOut) {
    slopeOut = 0;
    if (!w.map) return nullptr;
    const int fi = static_cast<int>((angle >> ANGLETOFINESHIFT) & FINEMASK);
    const double ox = static_cast<double>(x) / FRACUNIT;
    const double oy = static_cast<double>(y) / FRACUNIT;
    const double oz = static_cast<double>(z) / FRACUNIT;
    const double dx = static_cast<double>(finecosine[fi]) / FRACUNIT;
    const double dy = static_cast<double>(finesine[fi]) / FRACUNIT;
    const double rangeU = static_cast<double>(distance) / FRACUNIT;

    mobj_t* best = nullptr;
    double bestT = std::numeric_limits<double>::infinity();
    const double ex = ox + dx * rangeU, ey = oy + dy * rangeU;
    P_BlockThingsIterator(
        w, static_cast<fixed_t>((std::min(ox, ex) - 64) * FRACUNIT),
           static_cast<fixed_t>((std::min(oy, ey) - 64) * FRACUNIT),
           static_cast<fixed_t>((std::max(ox, ex) + 64) * FRACUNIT),
           static_cast<fixed_t>((std::max(oy, ey) + 64) * FRACUNIT),
        [&](mobj_t& mo) {
            if (&mo == source) return false;
            if (!(mo.flags & MF_SHOOTABLE) || mo.health <= 0) return false;
            const double cx = static_cast<double>(mo.x) / FRACUNIT;
            const double cy = static_cast<double>(mo.y) / FRACUNIT;
            const double rx = cx - ox, ry = cy - oy;
            const double tca = rx * dx + ry * dy;
            if (tca < 0.0 || tca > rangeU) return false;
            const double r = static_cast<double>(mo.info ? mo.info->radius : 0) / FRACUNIT;
            const double perp2 = rx * rx + ry * ry - tca * tca;
            if (perp2 > r * r) return false;
            const double mz = static_cast<double>(mo.z) / FRACUNIT;
            const double mh = static_cast<double>(mo.height ? mo.height : (mo.info ? mo.info->height : 0)) / FRACUNIT;
            if (oz < mz || oz > mz + mh) return false;      // horizontal aim must cross the box
            double tenter = tca - std::sqrt(std::max(0.0, r * r - perp2));
            if (tenter < 0.0) tenter = 0.0;
            if (tenter < bestT) { bestT = tenter; best = &mo; }
            return false;
        });
    if (best) {
        const double targetZ = (static_cast<double>(best->z) +
                                static_cast<double>(best->height ? best->height : best->info->height) / 2) / FRACUNIT;
        slopeOut = static_cast<fixed_t>((targetZ - oz) / (bestT > 1e-6 ? bestT : 1.0) * FRACUNIT);
    }
    return best;
}

namespace {
// Applies one special thing to the player. Returns true when it was consumed.
bool touchSpecialThing(World& w, mobj_t& thing) {
    player_t& p = w.player;
    const int dn = thing.info ? thing.info->doomednum : -1;
    auto addAmmo = [&](ammotype_t at, int amount) {
        int& a = p.ammo[at];
        if (a >= p.maxammo[at]) return false;
        a = std::min(p.maxammo[at], a + amount);
        return true;
    };
    auto giveWeapon = [&](weapontype_t wp) {
        if (p.weapons & (1u << wp)) return false;
        p.weapons |= (1u << wp);
        return true;
    };
    switch (dn) {
    case 2007: if (addAmmo(AM_CLIP, 10))  return true; break;    // clip
    case 2048: if (addAmmo(AM_CLIP, 50))  return true; break;    // box of bullets
    case 2008: if (addAmmo(AM_SHELL, 4))  return true; break;    // shells
    case 2049: if (addAmmo(AM_SHELL, 20)) return true; break;    // box of shells
    case 2010: if (addAmmo(AM_MISL, 1))   return true; break;    // rocket
    case 2046: if (addAmmo(AM_MISL, 5))   return true; break;    // box of rockets
    case 2011:                                                    // stimpack
        if (p.health < 100) { p.health = std::min(100, p.health + 10); return true; }
        break;
    case 2012:                                                    // medikit
        if (p.health < 100) { p.health = std::min(100, p.health + 25); return true; }
        break;
    case 2013:                                                    // soul sphere
        if (p.health < 200) { p.health = std::min(200, p.health + 100); return true; }
        break;
    case 2014:                                                    // health bonus
        if (p.health < 200) { p.health = std::min(200, p.health + 1); return true; }
        break;
    case 2015:                                                    // armor bonus
        if (p.armorpoints[0] < 200) {
            p.armorpoints[0] = std::min(200, p.armorpoints[0] + 1);
            if (p.armortype == 0) p.armortype = 1;
            return true;
        }
        break;
    case 2018:                                                    // green armor
        if (p.armorpoints[0] < 100) { p.armorpoints[0] = 100; p.armortype = 1; return true; }
        break;
    case 2001: {                                                  // shotgun + 8 shells
        bool got = giveWeapon(WP_SHOTGUN);
        if (addAmmo(AM_SHELL, 8)) got = true;
        return got;
    }
    case 2002: {                                                  // chaingun + 20 bullets
        bool got = giveWeapon(WP_CHAINGUN);
        if (addAmmo(AM_CLIP, 20)) got = true;
        return got;
    }
    case 2003: {                                                  // rocket launcher + 2 rockets
        bool got = giveWeapon(WP_MISSILE);
        if (addAmmo(AM_MISL, 2)) got = true;
        return got;
    }
    case 2004: {                                                  // plasma + 40 cells
        bool got = giveWeapon(WP_PLASMA);
        if (addAmmo(AM_CELL, 40)) got = true;
        return got;
    }
    case 2005: return giveWeapon(WP_CHAINSAW);                    // chainsaw
    case 2006:                                                    // BFG 9000
        if (giveWeapon(WP_BFG)) { addAmmo(AM_CELL, 40); return true; }
        break;
    case 2047: if (addAmmo(AM_CELL, 300)) return true; break;      // cell pack
    case 2019:                                                     // blue (heavy) armour
        if (p.armorpoints[1] < 200) { p.armorpoints[1] = 200; p.armortype = 2; return true; }
        break;
    case 8: {                                                      // backpack: caps double
        bool any = false;
        for (int i = 0; i < NUMAMMO; ++i)
            if (p.maxammo[i] < 2 * ammoinfo[i].maxammo) {
                p.ammo[i] += ammoinfo[i].maxammo;                  // carried ammo moves with it
                p.maxammo[i] = 2 * ammoinfo[i].maxammo;
                any = true;
            }
        return any;
    }
    // ---- P8b power-ups: the effect is the timer; see p_pspr/p_inter/p_enemy ----
    case 2022:                                                     // invulnerability sphere
        if (p.powers[pw_invulnerability] < 30 * 35) {
            p.powers[pw_invulnerability] = 30 * 35; return true;
        }
        break;
    case 2024:                                                     // partial invisibility
        if (p.powers[pw_invisibility] < 60 * 35) {
            p.powers[pw_invisibility] = 60 * 35; return true;
        }
        break;
    case 2025:                                                     // radiation suit
        if (p.powers[pw_ironfeet] < 60 * 35) {
            p.powers[pw_ironfeet] = 60 * 35; return true;
        }
        break;
    case 2026:                                                     // computer area map
        if (!p.powers[pw_allmap]) { p.powers[pw_allmap] = 1; return true; }
        break;
    case 2023: {                                                   // berserk pack
        // Always consumed, even at full health: an item that refuses to be picked up
        // re-triggers every tic and litters the floor forever.
        p.powers[pw_strength] = 1;                                  // permanent
        if (p.health < 200) { p.health = 200; if (w.player.mo) w.player.mo->health = 200; }
        p.readyweapon = WP_FIST;                                    // vanilla forces the fist
        p.pendingweapon = NUMWEAPONS;
        P_SetPsprite(w, p, 0, weaponinfo[WP_FIST].readystate);
        return true;
    }
    case 2045: {                                                    // supercharge sphere
        for (int i = 0; i < NUMAMMO; ++i)
            if (p.ammo[i] < p.maxammo[i]) p.ammo[i] = p.maxammo[i];
        if (p.health < 200) { p.health = std::min(200, p.health + 100);
                              if (w.player.mo) w.player.mo->health = p.health; }
        return true;
    }
    case 5:  if (!(p.keys & KEY_BLUE))   { p.keys |= KEY_BLUE;   return true; } break;
    case 6:  if (!(p.keys & KEY_YELLOW)) { p.keys |= KEY_YELLOW; return true; } break;
    case 13: if (!(p.keys & KEY_RED))    { p.keys |= KEY_RED;    return true; } break;
    default: break;
    }
    return false;
}
}  // namespace

void P_TouchSpecialThings(World& w, mobj_t& mo) {
    if (&mo != w.player.mo || w.player.dead || w.player.health <= 0) return;
    const fixed_t r = mo.info ? mo.info->radius : 0;
    P_BlockThingsIterator(w, mo.x - r, mo.y - r, mo.x + r, mo.y + r,
        [&](mobj_t& th) {
            if (!(th.flags & MF_SPECIAL) || th.removePending) return false;
            fixed_t tr = th.info ? th.info->radius : 0;
            if (std::abs(th.x - mo.x) > r + tr || std::abs(th.y - mo.y) > r + tr) return false;
            if (th.z > mo.z + mo.height || th.z + th.height < mo.z) return false;
            if (touchSpecialThing(w, th)) {
                const int dn = th.info ? th.info->doomednum : -1;
                I_StartSound((dn >= 2001 && dn <= 2005) ? SFX_WPNUP
                           : (dn == 2013)               ? SFX_GETPOW
                                                        : SFX_ITEMUP);
                ++w.player.itemcount;
                w.player.bonuscount = 6;
                if (w.player.mo) w.player.mo->health = w.player.health;   // keep the mobj in sync
                P_RemoveMobj(w, th);
            }
            return false;
        });
}

void P_RadiusAttack(World& w, mobj_t& bomb, int damage) {
    if (damage <= 0) return;
    const fixed_t range = 128 << FRACBITS;
    P_BlockThingsIterator(w, bomb.x - range, bomb.y - range, bomb.x + range, bomb.y + range,
        [&](mobj_t& th) {
            if (&th == &bomb) return false;
            if (th.health <= 0 || !(th.flags & MF_SHOOTABLE)) return false;
            fixed_t dist = (P_AproxDistance(th.x - bomb.x, th.y - bomb.y) -
                            (th.info ? th.info->radius : 0)) >> FRACBITS;
            if (dist < 0) dist = 0;
            if (dist > damage) return false;
            P_DamageMobj(w, th, bomb.target, damage - static_cast<int>(dist));
            return false;
        });
}

mobj_t* P_SpawnPlayerMissile(World& w, player_t& p, mobjtype_t type) {
    if (!w.map || !w.bm || !p.mo) return nullptr;
    mobj_t* mo = p.mo;
    angle_t ang = mo->angle;
    fixed_t slope = 0;
    fixed_t shootz = p.viewz;
    P_AimLineAttack(w, mo, mo->x, mo->y, shootz, ang, MISSILERANGE, slope);

    const int fi = static_cast<int>((ang >> ANGLETOFINESHIFT) & FINEMASK);
    const fixed_t speed = mobjinfo[type].speed;
    const fixed_t dist  = 16 << FRACBITS;
    mobj_t* th = P_SpawnMobj(w, mo->x + FixedMul(finecosine[fi], dist),
                             mo->y + FixedMul(finesine[fi], dist), shootz, type);
    th->angle = ang;
    th->target = mo;
    th->momx = FixedMul(finecosine[fi], speed);
    th->momy = FixedMul(finesine[fi], speed);
    th->momz = FixedMul(slope, speed);
    PosCheck pc = P_CheckPosition(*w.map, *w.bm, *th, th->x, th->y);
    th->floorz = pc.floorz;
    th->ceilingz = pc.ceilingz;
    return th;
}

// ---- P6a World-aware collision (thing↔thing blocking) ----
PosCheck P_CheckPosition(World& w, const mobj_t& mo, fixed_t x, fixed_t y) {
    PosCheck pc = P_CheckPosition(*w.map, *w.bm, mo, x, y);
    if (!pc.ok) return pc;
    const fixed_t r = mo.info ? mo.info->radius : 0;
    const fixed_t mh = mo.height ? mo.height : (mo.info ? mo.info->height : 0);
    for (const mobj_t& th : w.mobjs) {
        if (&th == &mo || th.removePending) continue;
        if (!(th.flags & MF_SOLID)) continue;                 // pickups/decorations don't block
        const fixed_t tr = th.info ? th.info->radius : 0;
        if (std::abs(th.x - x) >= r + tr || std::abs(th.y - y) >= r + tr) continue;
        const fixed_t thh = th.height ? th.height : (th.info ? th.info->height : 0);
        if (mo.z >= th.z + thh || mo.z + mh <= th.z) continue;   // vertically clear
        pc.ok = false;
        return pc;
    }
    return pc;
}

bool P_TryMove(World& w, mobj_t& mo, fixed_t nx, fixed_t ny) {
    PosCheck pc = P_CheckPosition(w, mo, nx, ny);
    if (!pc.ok) return false;
    const fixed_t h = mo.height ? mo.height : (mo.info ? mo.info->height : PLAYERHEIGHT);
    if (pc.ceilingz - pc.floorz < h) return false;
    if (pc.ceilingz - mo.floorz < h) return false;
    if (pc.floorz - mo.floorz > STEP_LIMIT) return false;
    if (!(mo.flags & MF_DROPOFF) && (pc.floorz - pc.dropoffz > STEP_LIMIT)) return false;
    mo.x = nx; mo.y = ny;
    mo.floorz = pc.floorz; mo.ceilingz = pc.ceilingz;
    mo.subsector = R_PointInSubsector(*w.map, nx, ny);
    return true;
}

bool P_SlideMove(World& w, mobj_t& mo, fixed_t dx, fixed_t dy) {
    for (int pass = 0; pass < 3 && (dx || dy); ++pass) {
        if (P_TryMove(w, mo, mo.x + dx, mo.y + dy)) return true;
        PosCheck pc = P_CheckPosition(w, mo, mo.x + dx, mo.y + dy);
        const int li = pc.blockedLine;
        if (li < 0 || li >= static_cast<int>(w.map->lines.size())) break;
        const line_t& L = w.map->lines[li];
        if (L.v1 < 0 || L.v2 < 0 ||
            L.v1 >= static_cast<int>(w.map->vertices.size()) ||
            L.v2 >= static_cast<int>(w.map->vertices.size())) break;
        fixed_t lx = w.map->vertices[L.v2].x - w.map->vertices[L.v1].x;
        fixed_t ly = w.map->vertices[L.v2].y - w.map->vertices[L.v1].y;
        fixed_t len = P_AproxDistance(lx, ly);
        if (len == 0) break;
        fixed_t ux = FixedDiv(lx, len), uy = FixedDiv(ly, len);
        fixed_t dot = FixedMul(dx, ux) + FixedMul(dy, uy);
        fixed_t ndx = FixedMul(dot, ux), ndy = FixedMul(dot, uy);
        if (ndx == dx && ndy == dy) break;                    // no progress: stop
        dx = ndx; dy = ndy;
        if (std::abs(dx) + std::abs(dy) < (FRACUNIT >> 2)) break;
    }
    return false;
}

void P_MovePlayerWorld(World& w, player_t& p, fixed_t forward, fixed_t strafe, angle_t turn) {
    // P7: vanilla player physics. `forward`/`strafe` are per-tic thrust magnitudes
    // (main passes 25*2048 walking / 50*2048 running, matching p_user.c forwardmove),
    // not displacement. Momentum + friction then live in P_XYMovement, so knockback
    // and wall sliding work the same way as for every other mobj.
    mobj_t* mo = p.mo;
    if (!mo || p.dead) return;
    const fixed_t oldx = mo->x, oldy = mo->y;
    mo->angle += turn;
    const int fi = static_cast<int>((mo->angle >> ANGLETOFINESHIFT) & FINEMASK);
    const fixed_t fc = finecosine[fi], fs = finesine[fi];
    if (forward) {
        mo->momx += FixedMul(forward, fc);
        mo->momy += FixedMul(forward, fs);
    }
    if (strafe) {
        mo->momx += FixedMul(strafe, fs);
        mo->momy -= FixedMul(strafe, fc);
    }
    P_XYMovement(w, *mo);
    mo->subsector = R_PointInSubsector(*w.map, mo->x, mo->y);
    P_CalcHeight(*w.map, p);
    P_CrossSpecialLines(w, *mo, oldx, oldy);
}

void P_CalcHeight(const MapData& m, player_t& p) {
    if (!p.mo) return;
    // The feet sit on the support: P_TryMove already rejected anything the player cannot
    // reach, and there is no jump or drop-off physics, so mo->z is mo->floorz. It must be
    // refreshed here because mo->floorz changes with every move — leaving mo->z at the spawn
    // height made every height-dependent test read a phantom player: thing blocking skipped
    // (walk straight through barrels/monsters once off the spawn floor), and hitscan/missile
    // vertical tests hit or missed at the wrong height (P8a, found while auditing E1M1 ledges).
    // Same clamp syncSectorThings applies when a mover does it: the feet are on the
    // support, and when the ceiling has closed to less than the player's height there is
    // no legal height left, so the floor wins (the crush damage that goes with it is what
    // removes the body; an invulnerable player simply squashes visually, as in vanilla).
    p.mo->z = p.mo->floorz;
    // Vanilla view bob: eye rises slightly with horizontal momentum (capped at MAXBOB).
    fixed_t bob = 0;
    if (p.mo->z <= p.mo->floorz + (4 << FRACBITS)) {
        fixed_t mag = P_AproxDistance(p.mo->momx, p.mo->momy);
        bob = FixedMul(mag, mag) >> 2;
        const fixed_t MAXBOB = 16 << FRACBITS;
        if (bob > MAXBOB) bob = MAXBOB;
    }
    // Eye-height tracks mo->floorz (bbox opening floor = vanilla tmfloorz). P3d fix preserved.
    fixed_t target = p.mo->floorz + VIEWHEIGHT + bob;
    p.viewz += (target - p.viewz) >> 3;                 // ~vanilla deltaviewheight >> 3
    int sec = (p.mo->subsector >= 0) ? sectorOf(m, p.mo->subsector) : -1;
    fixed_t cap = (sec >= 0 ? (static_cast<fixed_t>(m.sectors[sec].ceilingheight) << FRACBITS)
                            : (static_cast<fixed_t>(1 << 30)) ) - (4 << FRACBITS);
    if (p.viewz > cap) p.viewz = cap;
}

void P_MovePlayer(const MapData& m, const Blockmap& bm, player_t& p,
                  fixed_t forward, fixed_t strafe, angle_t turn) {
    mobj_t* mo = p.mo;
    if (!mo || p.dead) return;   // corpses don't walk
    mo->angle += turn;   // uint32 modular wraparound
    int fi = (mo->angle >> ANGLETOFINESHIFT) & FINEMASK;
    fixed_t fc = finecosine[fi];   // forward = (cos, sin) = vanilla
    fixed_t fs = finesine[fi];
    // forwardVec = (fc, fs); rightVec = (fs, -fc). Strafe sign matches P2d net direction.
    fixed_t dx = FixedMul(forward, fc) + FixedMul(strafe, fs);
    fixed_t dy = FixedMul(forward, fs) - FixedMul(strafe, fc);
    fixed_t mag = P_AproxDistance(dx, dy);
    if (mag > MAXMOVE) {
        fixed_t scale = FixedDiv(MAXMOVE, mag);
        dx = FixedMul(dx, scale);
        dy = FixedMul(dy, scale);
    }
    P_TrySlide(m, bm, *mo, dx, dy);
    mo->subsector = R_PointInSubsector(m, mo->x, mo->y);
    P_CalcHeight(m, p);
}
