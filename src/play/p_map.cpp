#include "p_map.h"
#include "p_inter.h"
#include "core/m_fixed.h"
#include "core/m_tables.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
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
                if (L.sidenum[1] < 0) { pc.ok = false; return pc; }           // one-sided wall
                if (L.flags & ML_BLOCKING) { pc.ok = false; return pc; }      // explicit block
                Opening op = P_LineOpening(m, L);
                if (op.top      < pc.ceilingz) pc.ceilingz = op.top;
                if (op.bottom   > pc.floorz)   pc.floorz   = op.bottom;
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
    // MT_PLAYER carries MF_DROPOFF -> exempt from dropoff gate (vanilla). dropoffz
    // is still tracked for monster AI in P6.
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
    for (mobj_t& mo : w.mobjs) {
        if (mo.removePending) continue;
        fixed_t r = mo.info ? mo.info->radius : 0;
        if (mo.x + r <= x1 || mo.x - r >= x2 || mo.y + r <= y1 || mo.y - r >= y2) continue;
        if (fn(mo)) return;
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

void P_CalcHeight(const MapData& m, player_t& p) {
    if (!p.mo) return;
    // Eye-height tracks mo->floorz (bbox opening floor = vanilla tmfloorz). P3d fix preserved.
    fixed_t target = p.mo->floorz + VIEWHEIGHT;
    p.viewz += (target - p.viewz) >> 3;                 // ~vanilla deltaviewheight >> 3
    int sec = (p.mo->subsector >= 0) ? sectorOf(m, p.mo->subsector) : -1;
    fixed_t cap = (sec >= 0 ? (static_cast<fixed_t>(m.sectors[sec].ceilingheight) << FRACBITS)
                            : (static_cast<fixed_t>(1 << 30)) ) - (4 << FRACBITS);
    if (p.viewz > cap) p.viewz = cap;
}

void P_MovePlayer(const MapData& m, const Blockmap& bm, player_t& p,
                  fixed_t forward, fixed_t strafe, angle_t turn) {
    mobj_t* mo = p.mo;
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
