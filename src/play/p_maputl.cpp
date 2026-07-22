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

// Stubs for Task 3 (so the file links now):
int P_BoxOnLineSide(const BBox&, float, float, float, float) { return -1; }
Opening P_LineOpening(const MapData&, const line_t&) { return Opening{}; }
std::vector<int> blockLinesInCell(const Blockmap&, int, int) { return {}; }
