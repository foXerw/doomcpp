#include "r_plane.h"
#include "r_data.h"   // Flat
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <vector>

float R_DistanceShade(float depth) {
    float s = 1.15f - depth * 0.0006f;
    if (s < 0.12f) s = 0.12f;     // kDistFloor
    return s;
}

int R_FindPlane(std::vector<Visplane>& vps, float height, const Flat* flat,
                int light, bool sky, int w) {
    if (sky) { height = 0; light = 0; }   // all sky merges into one plane
    for (size_t i = 0; i < vps.size(); ++i) {
        const Visplane& p = vps[i];
        // Sky planes share DOOM's skyflatnum semantics: merge on height+light+sky
        // alone (flat pointer is irrelevant for sky — all sky is one plane).
        if (p.height == height && p.lightlevel == light && p.sky == sky
            && (sky || p.flat == flat))
            return static_cast<int>(i);
    }
    Visplane p;
    p.height = height; p.flat = flat; p.lightlevel = light; p.sky = sky;
    p.minx = w; p.maxx = -1;            // empty (minx > maxx)
    p.top.assign(w, kOpenTop);
    p.bottom.assign(w, kOpenBot);
    vps.push_back(std::move(p));
    return static_cast<int>(vps.size() - 1);
}

int R_CheckPlane(std::vector<Visplane>& vps, int idx, int start, int stop) {
    Visplane& pl = vps[idx];
    int intrl, intrh, unionl, unionh;
    if (start < pl.minx) { intrl = pl.minx; unionl = start; }
    else                 { unionl = pl.minx; intrl = start; }
    if (stop > pl.maxx)  { intrh = pl.maxx; unionh = stop; }
    else                 { unionh = pl.maxx; intrh = stop; }
    int x = intrl;
    for (; x <= intrh; ++x)
        if (pl.top[x] != kOpenTop) break;   // claimed -> collision
    if (x > intrh) {
        pl.minx = unionl; pl.maxx = unionh; // no collision: extend
        return idx;
    }
    // collision: create a new plane (copy identity), claim [start,stop]
    Visplane np;
    np.height = pl.height; np.flat = pl.flat; np.lightlevel = pl.lightlevel; np.sky = pl.sky;
    np.minx = start; np.maxx = stop;
    np.top.assign(pl.top.size(), kOpenTop);
    np.bottom.assign(pl.bottom.size(), kOpenBot);
    vps.push_back(std::move(np));
    return static_cast<int>(vps.size() - 1);
}

void R_SetupPlaneTables(PlaneCtx& c) {
    c.yslope.assign(c.h, 0.0f);
    c.distscale.assign(c.w, 0.0f);
    for (int y = 0; y < c.h; ++y) {
        float dy = static_cast<float>(y) - c.h / 2.0f;
        // Horizon row (dy==0): FINITE and far, not INFINITY. Vanilla returns MAXINT here;
        // a large finite value shade-clamps to the same dark/far texels WITHOUT forcing
        // R_DrawSpan to skip the row (which left a permanent 1px black seam). P3d fix.
        c.yslope[y] = (std::fabs(dy) < 1e-6f) ? (c.focal * 64.0f) : (c.focal / std::fabs(dy));
    }
    for (int x = 0; x < c.w; ++x)
        c.distscale[x] = (x - c.w / 2.0f) / c.focal;
}
std::vector<std::tuple<int,int,int>> R_PlaneSpans(const Visplane& pl) {
    std::vector<std::tuple<int,int,int>> out;
    if (pl.minx > pl.maxx) return out;
    int ymax = -1;
    for (int x = pl.minx; x <= pl.maxx; ++x)
        if (pl.top[x] <= pl.bottom[x]) ymax = std::max(ymax, pl.bottom[x]);
    for (int y = 0; y <= ymax; ++y) {
        int x = pl.minx;
        while (x <= pl.maxx) {
            bool covered = (pl.top[x] <= y) && (y <= pl.bottom[x]); // unclaimed col: kOpenTop<=y false
            if (covered) {
                int x1 = x;
                while (x + 1 <= pl.maxx && pl.top[x+1] <= y && y <= pl.bottom[x+1]) ++x;
                out.emplace_back(y, x1, x);
                ++x;
            } else {
                ++x;
            }
        }
    }
    return out;
}
void R_DrawSpan(PlaneCtx& c, int y, int x1, int x2, const Flat* flat,
                float planeheight, int light, bool sky) {
    if (y < 0 || y >= c.h) return;
    if (sky) {
        for (int x = x1; x <= x2; ++x)
            if (x >= 0 && x < c.w) c.fb[(size_t)y * c.w + x] = kSkyColor;
        return;
    }
    if (!flat || flat->rgba.empty() || flat->width <= 0) return;
    float dist = planeheight * c.yslope[y];
    if (!std::isfinite(dist) || dist <= 0.0f) return;
    float shade = R_DistanceShade(dist) * (light / 255.0f);
    if (shade > 1.0f) shade = 1.0f;
    const float r1 = c.distscale[std::clamp(x1, 0, c.w - 1)] * dist;
    float wx = c.px + dist * c.sin + r1 * c.cos;
    float wy = c.py + dist * c.cos - r1 * c.sin;
    const float stepR = dist / c.focal;
    const float stepX = stepR * c.cos;
    const float stepY = -stepR * c.sin;
    for (int x = x1; x <= x2; ++x) {
        if (x >= 0 && x < c.w) {
            int tx = static_cast<int>(std::floor(wx)) & 63;
            int ty = static_cast<int>(std::floor(wy)) & 63;
            uint32_t t = flat->rgba[(size_t)ty * flat->width + tx];
            uint8_t rr = static_cast<uint8_t>((t >> 24 & 0xFF) * shade);
            uint8_t gg = static_cast<uint8_t>((t >> 16 & 0xFF) * shade);
            uint8_t bb = static_cast<uint8_t>((t >> 8  & 0xFF) * shade);
            c.fb[(size_t)y * c.w + x] =
                (static_cast<uint32_t>(rr) << 24) | (static_cast<uint32_t>(gg) << 16) |
                (static_cast<uint32_t>(bb) << 8) | 0xFFu;
        }
        wx += stepX; wy += stepY;
    }
}
void R_DrawPlanes(PlaneCtx& c, std::vector<Visplane>& vps) {
    for (Visplane& pl : vps) {
        if (pl.minx > pl.maxx) continue;
        float planeheight = std::fabs(pl.height - c.eyeZ);
        for (auto [y, x1, x2] : R_PlaneSpans(pl))
            R_DrawSpan(c, y, x1, x2, pl.flat, planeheight, pl.lightlevel, pl.sky);
    }
}
