#include "p_maputl.h"
#include "core/m_fixed.h"
#include <algorithm>

// Faithful to r_main.c::R_PointOnSide: node direction is taken in map units
// (>> FRACBITS) before the FixedMul to avoid overflow, exactly as vanilla.
int R_PointOnSide(fixed_t x, fixed_t y, const node_t& n) {
    fixed_t dx = x - n.x;
    fixed_t dy = y - n.y;
    fixed_t left  = FixedMul(n.dy >> FRACBITS, dx);
    fixed_t right = FixedMul(n.dx >> FRACBITS, dy);
    return (right < left) ? 0 : 1;
}

int R_PointInSubsector(const MapData& m, fixed_t x, fixed_t y) {
    if (m.nodes.empty()) return 0;
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

// Cross-product sign test on all four bbox corners; -1 = straddle (collision
// candidate). FixedMul preserves sign; int64 intermediate avoids overflow.
int P_BoxOnLineSide(const BBox& bb, fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2) {
    fixed_t dx = x2 - x1, dy = y2 - y1;
    auto side = [&](fixed_t px, fixed_t py) -> int {
        fixed_t cross = FixedMul(dy, (px - x1)) - FixedMul(dx, (py - y1));
        // Sign is FLIPPED vs vanilla (negated cross product). Harmless: straddle
        // detection (corner signs differ) and the sole caller's !=-1 test are both
        // sign-invariant, so the per-side 0/1 label is never consumed.
        return (cross < 0) ? 1 : 0;
    };
    int s = side(bb.left, bb.bottom);
    if (s != side(bb.right, bb.bottom)) return -1;
    if (s != side(bb.left, bb.top))     return -1;
    if (s != side(bb.right, bb.top))    return -1;
    return s;
}

Opening P_LineOpening(const MapData& m, const line_t& L) {
    Opening o;
    if (L.sidenum[0] < 0 || L.sidenum[0] >= static_cast<int>(m.sides.size())) return o;
    int frontSec = m.sides[L.sidenum[0]].sector;
    if (frontSec < 0 || frontSec >= static_cast<int>(m.sectors.size())) return o;
    if (L.sidenum[1] < 0 || L.sidenum[1] >= static_cast<int>(m.sides.size())) return o;  // one-sided
    int backSec = m.sides[L.sidenum[1]].sector;
    if (backSec < 0 || backSec >= static_cast<int>(m.sectors.size())) return o;
    fixed_t fH_f = static_cast<fixed_t>(m.sectors[frontSec].floorheight)   << FRACBITS;
    fixed_t cH_f = static_cast<fixed_t>(m.sectors[frontSec].ceilingheight) << FRACBITS;
    fixed_t fH_b = static_cast<fixed_t>(m.sectors[backSec].floorheight)    << FRACBITS;
    fixed_t cH_b = static_cast<fixed_t>(m.sectors[backSec].ceilingheight)  << FRACBITS;
    o.top      = std::min(cH_f, cH_b);
    o.bottom   = std::max(fH_f, fH_b);
    o.lowfloor = std::min(fH_f, fH_b);
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
