#include "p_map.h"
#include "core/m_fixed.h"
#include "core/m_tables.h"
#include <algorithm>
#include <cstdlib>

// Vanilla m_fixed.c::P_AproxDistance (sqrt-free): |dx|+|dy| - min/2.
static fixed_t P_AproxDistance(fixed_t dx, fixed_t dy) {
    dx = std::abs(dx); dy = std::abs(dy);
    if (dx < dy) return dx + dy - (dx >> 1);
    return dx + dy - (dy >> 1);
}

PosCheck P_CheckPosition(const MapData& m, const Blockmap& bm, fixed_t x, fixed_t y) {
    PosCheck pc;
    BBox bb{ x - PLAYERRADIUS, x + PLAYERRADIUS, y - PLAYERRADIUS, y + PLAYERRADIUS };

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

bool P_TryMove(const MapData& m, const Blockmap& bm, Player& p, fixed_t nx, fixed_t ny) {
    PosCheck pc = P_CheckPosition(m, bm, nx, ny);
    if (!pc.ok) return false;
    if (pc.ceilingz - pc.floorz < PLAYERHEIGHT) return false;   // doesn't fit vertically
    if (pc.ceilingz - p.floorz  < PLAYERHEIGHT) return false;   // not enough headroom
    if (pc.floorz   - p.floorz  > STEP_LIMIT)   return false;   // step up too high
    // MT_PLAYER carries MF_DROPOFF -> exempt from dropoff gate (vanilla). dropoffz
    // is still tracked for monster AI in P6.
    p.x = nx; p.y = ny;
    p.floorz = pc.floorz;
    p.ceilingz = pc.ceilingz;
    return true;
}

bool P_TrySlide(const MapData& m, const Blockmap& bm, Player& p, fixed_t dx, fixed_t dy) {
    if (P_TryMove(m, bm, p, p.x + dx, p.y + dy)) return true;
    if (dx != 0 && P_TryMove(m, bm, p, p.x + dx, p.y)) return true;
    if (dy != 0 && P_TryMove(m, bm, p, p.x, p.y + dy)) return true;
    return false;
}

void P_CalcHeight(const MapData& m, Player& p) {
    if (p.sector < 0 || p.sector >= static_cast<int>(m.sectors.size())) return;
    // Eye-height tracks p.floorz (bbox opening floor = vanilla tmfloorz). P3d fix preserved.
    fixed_t target = p.floorz + VIEWHEIGHT;
    p.viewz += (target - p.viewz) >> 3;                 // ~vanilla deltaviewheight >> 3
    fixed_t cap = (static_cast<fixed_t>(m.sectors[p.sector].ceilingheight) << FRACBITS) - (4 << FRACBITS);
    if (p.viewz > cap) p.viewz = cap;
}

void P_MovePlayer(const MapData& m, const Blockmap& bm, Player& p,
                  fixed_t forward, fixed_t strafe, angle_t turn) {
    p.angle += turn;   // uint32 modular wraparound
    int fi = (p.angle >> ANGLETOFINESHIFT) & FINEMASK;
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
    P_TrySlide(m, bm, p, dx, dy);
    int ss = R_PointInSubsector(m, p.x, p.y);
    p.subsector = ss;
    p.sector = sectorOf(m, ss);
    P_CalcHeight(m, p);
}
