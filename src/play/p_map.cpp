#include "p_map.h"
#include <algorithm>
#include <cmath>

PosCheck P_CheckPosition(const MapData& m, const Blockmap& bm, float x, float y) {
    PosCheck pc;
    BBox bb{x - PLAYERRADIUS, x + PLAYERRADIUS, y - PLAYERRADIUS, y + PLAYERRADIUS};

    int sec = sectorOf(m, R_PointInSubsector(m, x, y));
    if (sec < 0) { pc.ok = false; return pc; }
    pc.floorz = pc.dropoffz = static_cast<float>(m.sectors[sec].floorheight);
    pc.ceilingz = static_cast<float>(m.sectors[sec].ceilingheight);

    int xl = static_cast<int>(std::floor((bb.left   - bm.orgx) / MAPBLOCK));
    int xh = static_cast<int>(std::floor((bb.right  - bm.orgx) / MAPBLOCK));
    int yl = static_cast<int>(std::floor((bb.bottom - bm.orgy) / MAPBLOCK));
    int yh = static_cast<int>(std::floor((bb.top    - bm.orgy) / MAPBLOCK));

    static int validgen = 0;  // generation counter for per-call line dedup (vanilla validcount)
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
                float ax1 = static_cast<float>(m.vertices[L.v1].x >> 16);
                float ay1 = static_cast<float>(m.vertices[L.v1].y >> 16);
                float ax2 = static_cast<float>(m.vertices[L.v2].x >> 16);
                float ay2 = static_cast<float>(m.vertices[L.v2].y >> 16);
                float lminx = std::fmin(ax1, ax2), lmaxx = std::fmax(ax1, ax2);
                float lminy = std::fmin(ay1, ay2), lmaxy = std::fmax(ay1, ay2);
                if (bb.right <= lminx || bb.left >= lmaxx ||
                    bb.top   <= lminy || bb.bottom >= lmaxy) continue;          // bbox early-out
                if (P_BoxOnLineSide(bb, ax1, ay1, ax2, ay2) != -1) continue;    // not straddling
                if (L.sidenum[1] < 0) { pc.ok = false; return pc; }              // one-sided wall
                if (L.flags & ML_BLOCKING) { pc.ok = false; return pc; }         // explicit block
                Opening op = P_LineOpening(m, L);
                if (op.top      < pc.ceilingz) pc.ceilingz = op.top;
                if (op.bottom   > pc.floorz)   pc.floorz   = op.bottom;
                if (op.lowfloor < pc.dropoffz) pc.dropoffz = op.lowfloor;
            }
    return pc;
}

bool P_TryMove(const MapData& m, const Blockmap& bm, Player& p, float nx, float ny) {
    PosCheck pc = P_CheckPosition(m, bm, nx, ny);
    if (!pc.ok) return false;
    if (pc.ceilingz - pc.floorz < PLAYERHEIGHT) return false;   // doesn't fit vertically
    if (pc.ceilingz - p.floorz  < PLAYERHEIGHT) return false;   // not enough headroom at current z
    if (pc.floorz   - p.floorz  > STEP_LIMIT)   return false;   // step up too high
    // Vanilla gates the dropoff check on !(flags & MF_DROPOFF); MT_PLAYER carries MF_DROPOFF,
    // so the player is EXEMPT and may walk off ledges of any height. Dropoff is intentionally
    // allowed here. (dropoffz is still tracked in P_CheckPosition for monster AI in P6.)
    p.x = nx; p.y = ny;
    p.floorz = pc.floorz;
    p.ceilingz = pc.ceilingz;
    return true;
}

bool P_TrySlide(const MapData& m, const Blockmap& bm, Player& p, float dx, float dy) {
    if (P_TryMove(m, bm, p, p.x + dx, p.y + dy)) return true;   // full move
    if (dx != 0.0f && P_TryMove(m, bm, p, p.x + dx, p.y)) return true;   // X-only
    if (dy != 0.0f && P_TryMove(m, bm, p, p.x, p.y + dy)) return true;   // Y-only
    return false;
}

void P_CalcHeight(const MapData& m, Player& p) {
    if (p.sector < 0 || p.sector >= static_cast<int>(m.sectors.size())) return;
    // Eye-height target tracks p.floorz (the bbox opening floor = vanilla tmfloorz), NOT the
    // floor under the player's center point. Using the point-sector floor left the eye at the
    // wrong height on/around steps, so the floor clipped into view. P3d fix.
    float target = p.floorz + VIEWHEIGHT;
    p.viewz += (target - p.viewz) * 0.125f;                    // ~vanilla deltaviewheight >> 3
    float cap = static_cast<float>(m.sectors[p.sector].ceilingheight) - 4.0f;
    if (p.viewz > cap) p.viewz = cap;
}

void P_MovePlayer(const MapData& m, const Blockmap& bm, Player& p,
                  float forward, float strafe, float turn) {
    p.angle += turn;
    float fs = std::sin(p.angle), fc = std::cos(p.angle);
    // forwardVec=(sin,cos), rightVec=(cos,-sin)  (camera basis from P2a)
    float dx = fs * forward + fc * strafe;
    float dy = fc * forward - fs * strafe;
    float mag = std::sqrt(dx * dx + dy * dy);
    if (mag > MAXMOVE) { dx *= MAXMOVE / mag; dy *= MAXMOVE / mag; }
    P_TrySlide(m, bm, p, dx, dy);   // commits the move + sets p.floorz/p.ceilingz (bbox opening)
    int ss = R_PointInSubsector(m, p.x, p.y);
    p.subsector = ss;
    p.sector = sectorOf(m, ss);
    // Do NOT overwrite p.floorz with the point-sector floor. P_TryMove already set it to the
    // bbox opening floor (vanilla mo->floorz = tmfloorz); resetting it to the floor under the
    // player's center point breaks multi-step climbs (bbox reaches a step >24 above the reset
    // floor) and sinks the eye into steps. P3d fix.
    P_CalcHeight(m, p);
}
