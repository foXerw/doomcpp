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

// --- Stubs: implemented in Tasks 4-5 (empty now so the module links) ---
void R_SetupPlaneTables(PlaneCtx&) {}
std::vector<std::tuple<int,int,int>> R_PlaneSpans(const Visplane&) { return {}; }
void R_DrawSpan(PlaneCtx&, int, int, int, const Flat*, float, int, bool) {}
void R_DrawPlanes(PlaneCtx&, std::vector<Visplane>&) {}
