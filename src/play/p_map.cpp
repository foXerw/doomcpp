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

// Stubs for Tasks 5-6 (so the file links now):
bool P_TryMove(const MapData&, const Blockmap&, Player&, float, float) { return false; }
bool P_TrySlide(const MapData&, const Blockmap&, Player&, float, float) { return false; }
void P_CalcHeight(const MapData&, Player&) {}
void P_MovePlayer(const MapData&, const Blockmap&, Player&, float, float, float) {}
