#include "p_maputl.h"
#include <cmath>

int R_PointOnSide(float x, float y, const node_t& n) {
    float dx = x - n.x, dy = y - n.y;
    float left  = n.dy * dx;
    float right = n.dx * dy;
    return (right < left) ? 0 : 1;   // matches r_bsp.cpp::pointOnSide; equivalent to vanilla
}

int R_PointInSubsector(const MapData& m, float x, float y) {
    if (m.nodes.empty()) return 0;            // single subsector (vanilla special case)
    uint32_t nodenum = static_cast<uint32_t>(m.nodes.size() - 1);
    while (!(nodenum & NF_SUBSECTOR)) {
        const node_t& n = m.nodes[nodenum];
        nodenum = n.children[R_PointOnSide(x, y, n)];
    }
    return static_cast<int>(nodenum & ~NF_SUBSECTOR);
}

int sectorOf(const MapData& m, int subsectorIdx) {
    if (subsectorIdx < 0 || subsectorIdx >= static_cast<int>(m.subsectors.size())) return -1;
    const subsector_t& ss = m.subsectors[subsectorIdx];
    for (int i = 0; i < ss.segcount; ++i) {
        int segi = ss.firstseg + i;
        if (segi >= 0 && segi < static_cast<int>(m.segs.size())) return m.segs[segi].frontsector;
    }
    return -1;
}

int P_BoxOnLineSide(const BBox& bb, float x1, float y1, float x2, float y2) {
    float dx = x2 - x1, dy = y2 - y1;
    auto side = [&](float px, float py) -> int {
        float cross = dx * (py - y1) - dy * (px - x1);
        return (cross < 0.0f) ? 1 : 0;     // >= 0 -> front (on-line counts as front)
    };
    int s = side(bb.left, bb.bottom);
    if (s != side(bb.right, bb.bottom)) return -1;
    if (s != side(bb.left, bb.top))     return -1;
    if (s != side(bb.right, bb.top))    return -1;
    return s;   // all four corners agree; -1 above means straddle
}

Opening P_LineOpening(const MapData& m, const line_t& L) {
    Opening o;
    if (L.sidenum[0] < 0 || L.sidenum[0] >= static_cast<int>(m.sides.size())) return o;
    int frontSec = m.sides[L.sidenum[0]].sector;
    if (frontSec < 0 || frontSec >= static_cast<int>(m.sectors.size())) return o;
    if (L.sidenum[1] < 0 || L.sidenum[1] >= static_cast<int>(m.sides.size())) return o;  // one-sided
    int backSec = m.sides[L.sidenum[1]].sector;
    if (backSec < 0 || backSec >= static_cast<int>(m.sectors.size())) return o;
    float fH_f = static_cast<float>(m.sectors[frontSec].floorheight);
    float cH_f = static_cast<float>(m.sectors[frontSec].ceilingheight);
    float fH_b = static_cast<float>(m.sectors[backSec].floorheight);
    float cH_b = static_cast<float>(m.sectors[backSec].ceilingheight);
    o.top      = std::fmin(cH_f, cH_b);
    o.bottom   = std::fmax(fH_f, fH_b);
    o.lowfloor = std::fmin(fH_f, fH_b);
    o.range    = o.top - o.bottom;
    return o;
}

std::vector<int> blockLinesInCell(const Blockmap& bm, int cx, int cy) {
    std::vector<int> out;
    if (cx < 0 || cy < 0 || cx >= bm.width || cy >= bm.height) return out;
    int offset = bm.lump[4 + cy * bm.width + cx];
    if (offset < 0 || offset >= static_cast<int>(bm.lump.size())) return out;
    for (int i = offset; i < static_cast<int>(bm.lump.size()); ++i) {
        std::int16_t v = bm.lump[i];
        if (v == -1) break;
        out.push_back(v);
    }
    return out;
}
