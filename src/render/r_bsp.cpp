#include "r_bsp.h"
#include "r_draw.h"
#include "play/p_setup.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
constexpr uint32_t NF_SUBSECTOR = 0x8000;

struct Cam {
    float px, py, sin, cos, eyeZ, focal;
    int w, h;
    uint32_t* fb;
    std::vector<uint8_t> occluded;
};

int pointOnSide(float x, float y, const node_t& n) {
    float dx = x - n.x, dy = y - n.y;
    float left = n.dy * dx, right = n.dx * dy;
    return (right < left) ? 0 : 1;   // matches R_PointOnSide: front=0
}

void renderSeg(Cam& c, const MapData& m, const seg_t& sg) {
    if (sg.v1 < 0 || sg.v1 >= static_cast<int>(m.vertices.size())) return;
    if (sg.v2 < 0 || sg.v2 >= static_cast<int>(m.vertices.size())) return;
    if (sg.frontsector < 0 || sg.frontsector >= static_cast<int>(m.sectors.size())) return;

    auto toCam = [&](fixed_t vx, fixed_t vy, float& depth, float& right) {
        float dx = static_cast<float>(vx >> 16) - c.px;
        float dy = static_cast<float>(vy >> 16) - c.py;
        depth = dx * c.sin + dy * c.cos;
        right = dx * c.cos - dy * c.sin;
    };
    float d0, r0, d1, r1;
    toCam(m.vertices[sg.v1].x, m.vertices[sg.v1].y, d0, r0);
    toCam(m.vertices[sg.v2].x, m.vertices[sg.v2].y, d1, r1);

    // near-clip against a small positive depth
    const float nearZ = 0.01f;
    if (d0 <= nearZ && d1 <= nearZ) return;                                  // fully behind
    if (d0 <= nearZ) { float t = (nearZ - d0) / (d1 - d0); d0 = nearZ; r0 += (r1 - r0) * t; }
    else if (d1 <= nearZ) { float t = (nearZ - d1) / (d0 - d1); d1 = nearZ; r1 += (r0 - r1) * t; }

    int x0 = static_cast<int>(c.w / 2 + c.focal * r0 / d0);
    int x1 = static_cast<int>(c.w / 2 + c.focal * r1 / d1);
    if (x0 > x1) std::swap(x0, x1);
    if (x1 < 0 || x0 >= c.w) return;
    x0 = std::max(0, x0);
    x1 = std::min(c.w - 1, x1);

    const float s0 = 1.0f / d0, s1 = 1.0f / d1;
    const int fH = m.sectors[sg.frontsector].floorheight;
    const int cH = m.sectors[sg.frontsector].ceilingheight;

    for (int x = x0; x <= x1; ++x) {
        if (c.occluded[x]) continue;
        float t = (x1 == x0) ? 0.0f : static_cast<float>(x - x0) / (x1 - x0);
        float scale = s0 + (s1 - s0) * t;                 // 1/depth, ~linear in screen x
        int y0 = std::max(0, static_cast<int>(c.h / 2 - c.focal * (cH - c.eyeZ) * scale));
        int y1 = std::min(c.h - 1, static_cast<int>(c.h / 2 - c.focal * (fH - c.eyeZ) * scale));
        uint8_t sh = static_cast<uint8_t>(std::min(255.0f, 60.0f + 4000.0f * scale));
        // RGBA8888: (R<<24)|(G<<16)|(B<<8)|A ; grey shade, opaque
        uint32_t col = (static_cast<uint32_t>(sh) << 24) |
                       (static_cast<uint32_t>(sh) << 16) |
                       (static_cast<uint32_t>(sh) << 8) | 0xFFu;
        for (int y = y0; y <= y1; ++y) c.fb[y * c.w + x] = col;
        c.occluded[x] = 1;
    }
}

void renderSubsector(Cam& c, const MapData& m, int idx) {
    if (idx < 0 || idx >= static_cast<int>(m.subsectors.size())) return;
    const subsector_t& ss = m.subsectors[idx];
    for (int i = 0; i < ss.segcount; ++i) {
        int s = ss.firstseg + i;
        if (s >= 0 && s < static_cast<int>(m.segs.size())) renderSeg(c, m, m.segs[s]);
    }
}

void renderNode(Cam& c, const MapData& m, uint32_t idx) {
    if (idx & NF_SUBSECTOR) { renderSubsector(c, m, idx & ~NF_SUBSECTOR); return; }
    if (idx >= m.nodes.size()) return;
    const node_t& n = m.nodes[idx];
    int side = pointOnSide(c.px, c.py, n);
    renderNode(c, m, n.children[side]);        // near first
    renderNode(c, m, n.children[side ^ 1]);    // far
}
}

void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  float px, float py, float ang, float eyeZ) {
    if (map.nodes.empty() || map.subsectors.empty()) return;
    Cam c;
    c.px = px; c.py = py; c.eyeZ = eyeZ;
    c.sin = std::sin(ang); c.cos = std::cos(ang);
    c.w = w; c.h = h; c.fb = fb;
    c.focal = w / 2.0f;
    c.occluded.assign(w, 0);
    for (int i = 0; i < w * h; ++i) fb[i] = 0x000000FFu;   // opaque black
    renderNode(c, map, static_cast<uint32_t>(map.nodes.size() - 1));   // root = last node
}
