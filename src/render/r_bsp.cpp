#include "r_bsp.h"
#include "r_draw.h"
#include "r_data.h"
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
    std::vector<int> ceilingClip;    // filled-down-from-top row; init -1
    std::vector<int> floorClip;      // filled-up-from-bottom row; init h
    const TextureLookup* tex;
};

int pointOnSide(float x, float y, const node_t& n) {
    float dx = x - n.x, dy = y - n.y;
    float left = n.dy * dx, right = n.dx * dy;
    return (right < left) ? 0 : 1;
}

// Draw one textured wall column over world-z span [zTop, zBot] (zTop>zBot) at screen column x.
// solid: spans full opening (one-sided). upper/lower: ceiling/floor-step walls. Updates clips for opaque parts.
void drawCol(Cam& c, int x, float scale, const Texture& T, float U, int rowoffset,
             int zTop, int zBot, int light, bool solid, bool upper, bool lower) {
    float syTop = c.h / 2.0f - c.focal * (zTop - c.eyeZ) * scale;   // smaller y (wall top)
    float syBot = c.h / 2.0f - c.focal * (zBot - c.eyeZ) * scale;   // larger y  (wall bottom)
    int yA = std::max(static_cast<int>(std::ceil(syTop)),  c.ceilingClip[x] + 1);
    int yB = std::min(static_cast<int>(std::floor(syBot)), c.floorClip[x] - 1);
    float lightf = std::clamp(light / 255.0f, 0.0f, 1.0f);
    for (int y = yA; y <= yB; ++y) {
        float z = c.eyeZ + (c.h / 2.0f - y) / (c.focal * scale);     // invert projection -> world z
        float V = (z - zTop) + rowoffset;                            // 1 map unit = 1 texel
        int vi = static_cast<int>(std::floor(V)); vi %= T.height; if (vi < 0) vi += T.height;
        int ui = static_cast<int>(std::floor(U));   ui %= T.width;  if (ui < 0) ui += T.width;
        uint32_t texel = T.rgba[static_cast<size_t>(vi) * T.width + ui];
        if (!(texel & 0xFFu)) continue;                              // transparent
        uint8_t r = static_cast<uint8_t>((texel >> 24 & 0xFF) * lightf);
        uint8_t g = static_cast<uint8_t>((texel >> 16 & 0xFF) * lightf);
        uint8_t b = static_cast<uint8_t>((texel >> 8  & 0xFF) * lightf);
        c.fb[static_cast<size_t>(y) * c.w + x] =
            (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
            (static_cast<uint32_t>(b) << 8) | 0xFFu;
    }
    if (solid || upper) c.ceilingClip[x] = std::max(c.ceilingClip[x], std::min(static_cast<int>(std::floor(syBot)), c.h - 1));
    if (solid || lower) c.floorClip[x]   = std::min(c.floorClip[x],   std::max(static_cast<int>(std::ceil(syTop)),  0));
}

void renderSeg(Cam& c, const MapData& m, const seg_t& sg) {
    if (sg.v1 < 0 || sg.v1 >= static_cast<int>(m.vertices.size())) return;
    if (sg.v2 < 0 || sg.v2 >= static_cast<int>(m.vertices.size())) return;
    if (sg.frontsector < 0 || sg.frontsector >= static_cast<int>(m.sectors.size())) return;

    // original camera-space endpoints (used for perspective-correct U/scale)
    auto toCam = [&](fixed_t vx, fixed_t vy, float& depth, float& right) {
        float dx = static_cast<float>(vx >> 16) - c.px;
        float dy = static_cast<float>(vy >> 16) - c.py;
        depth = dx * c.sin + dy * c.cos;
        right = dx * c.cos - dy * c.sin;
    };
    float d0o, r0o, d1o, r1o;
    toCam(m.vertices[sg.v1].x, m.vertices[sg.v1].y, d0o, r0o);
    toCam(m.vertices[sg.v2].x, m.vertices[sg.v2].y, d1o, r1o);

    // seg world length (map units = texels)
    float ax = static_cast<float>(m.vertices[sg.v2].x >> 16) - (m.vertices[sg.v1].x >> 16);
    float ay = static_cast<float>(m.vertices[sg.v2].y >> 16) - (m.vertices[sg.v1].y >> 16);
    float segLen = std::sqrt(ax * ax + ay * ay);

    // near-clip a copy to find the visible column range [x0,x1] (P2b logic)
    float d0 = d0o, r0 = r0o, d1 = d1o, r1 = r1o;
    const float nearZ = 0.01f;
    if (d0 <= nearZ && d1 <= nearZ) return;
    if (d0 <= nearZ) { float t = (nearZ - d0) / (d1 - d0); d0 = nearZ; r0 += (r1 - r0) * t; }
    else if (d1 <= nearZ) { float t = (nearZ - d1) / (d0 - d1); d1 = nearZ; r1 += (r0 - r1) * t; }
    int x0 = static_cast<int>(c.w / 2 + c.focal * r0 / d0);
    int x1 = static_cast<int>(c.w / 2 + c.focal * r1 / d1);
    if (x0 > x1) std::swap(x0, x1);
    if (x1 < 0 || x0 >= c.w) return;
    x0 = std::max(0, x0); x1 = std::min(c.w - 1, x1);

    // resolve side + textures + sector
    const auto& L = m.lines[sg.linedef];
    int sideIdx = (sg.side == 0 || sg.side == 1) ? L.sidenum[sg.side] : -1;
    if (sideIdx < 0 || sideIdx >= static_cast<int>(m.sides.size())) return;
    const auto& sd = m.sides[sideIdx];
    const auto& sec = m.sectors[sg.frontsector];
    int fH = sec.floorheight, cH = sec.ceilingheight, light = sec.lightlevel;

    const Texture* T = nullptr;
    if (sg.backsector < 0) {
        T = c.tex->wall(sd.midtexture);          // one-sided: middle texture
    }
    // (two-sided handled in Task 5)
    if (!T) return;

    float texBaseU = static_cast<float>(sg.offset) + static_cast<float>(sd.textureoffset);

    for (int x = x0; x <= x1; ++x) {
        if (c.ceilingClip[x] >= c.floorClip[x] - 1) continue;       // column fully occluded
        float sx = (x - c.w / 2.0f) / c.focal;                      // right/depth for this column's ray
        float denom = (r1o - r0o) - sx * (d1o - d0o);               // ORIGINAL endpoints
        if (std::fabs(denom) < 1e-9f) continue;
        float t = (sx * d0o - r0o) / denom;                         // param along original seg line
        if (t < -0.001f || t > 1.001f) continue;
        float depth = d0o + t * (d1o - d0o);
        if (depth <= nearZ) continue;
        float scale = 1.0f / depth;
        float U = texBaseU + t * segLen;
        drawCol(c, x, scale, *T, U, sd.rowoffset, cH, fH, light, /*solid*/true, false, false);
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
    renderNode(c, m, n.children[side]);
    renderNode(c, m, n.children[side ^ 1]);
}
}  // namespace

void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  const TextureLookup& tex, float px, float py, float ang, float eyeZ) {
    if (map.nodes.empty() || map.subsectors.empty()) return;
    Cam c;
    c.px = px; c.py = py; c.eyeZ = eyeZ;
    c.sin = std::sin(ang); c.cos = std::cos(ang);
    c.w = w; c.h = h; c.fb = fb; c.focal = w / 2.0f;
    c.tex = &tex;
    c.ceilingClip.assign(w, -1);
    c.floorClip.assign(w, h);
    for (int i = 0; i < w * h; ++i) fb[i] = 0x000000FFu;            // opaque black clear
    renderNode(c, map, static_cast<uint32_t>(map.nodes.size() - 1));
}
