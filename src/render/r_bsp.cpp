#include "r_bsp.h"
#include <cstdio>
#include "r_draw.h"
#include "r_data.h"
#include "r_plane.h"
#include "r_things.h"
#include "play/p_setup.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace {
constexpr uint32_t NF_SUBSECTOR = 0x8000;
bool g_forceOpaque = false;          // diagnostics only (see R_SetForceOpaqueForDiagnostics)
int  g_probeCol = -1;                // diagnostics: trace one screen column
bool g_seamFill = true;              // paint leftover pixels from their neighbour

struct Cam {
    float px, py, sin, cos, eyeZ, focal;
    int w, h;
    uint32_t* fb;
    std::vector<int> ceilingClip;    // filled-down-from-top row; init -1
    std::vector<int> floorClip;      // filled-up-from-bottom row; init h
    std::vector<float> worldDepthPx; // per-pixel nearest world surface (w*h, init +inf); sprite occlusion
    std::vector<uint8_t> seeThru;    // per-pixel "a masked texel deliberately left this open"
    const TextureLookup* tex;
    PlaneCtx plane;                  // visplane rendering context
    std::vector<Visplane> vps;       // visplane list for this frame
    uint32_t tick = 0;               // animated-flat tick
    // P4 sprite phase (optional; populated only when R_RenderView callers pass sprites/thingsBySubsec).
    const SpriteCache* sprites = nullptr;
    const std::vector<std::vector<const mobj_t*>>* thingsBySubsec = nullptr;
    std::vector<vissprite_t> vissprites;
};

// P3.5a: node_t is now fixed_t; the float renderer reads coords as map units
// (>> FRACBITS) exactly as it already does for vertices. P3.5b makes this fixed.
int pointOnSide(float x, float y, const node_t& n) {
    float nx = static_cast<float>(n.x >> FRACBITS);
    float ny = static_cast<float>(n.y >> FRACBITS);
    float dx = x - nx, dy = y - ny;
    float left  = static_cast<float>(n.dy >> FRACBITS) * dx;
    float right = static_cast<float>(n.dx >> FRACBITS) * dy;
    return (right < left) ? 0 : 1;
}

// Draw one textured wall column over world-z span [zTop, zBot] (zTop>zBot) at screen column x.
// solid: spans full opening (one-sided). upper/lower: ceiling/floor-step walls. Updates clips for opaque parts.
void drawCol(Cam& c, int x, float scale, const Texture& T, float U, int rowoffset,
             float anchorZ, int zTop, int zBot, int light, bool solid, bool upper, bool lower,
             float colDepth) {
    if (T.width <= 0 || T.height <= 0 || T.rgba.empty()) return;
    float syTop = c.h / 2.0f - c.focal * (zTop - c.eyeZ) * scale;   // smaller y (wall top)
    float syBot = c.h / 2.0f - c.focal * (zBot - c.eyeZ) * scale;   // larger y  (wall bottom)
    int yA = std::max(static_cast<int>(std::ceil(syTop)),  c.ceilingClip[x] + 1);
    int yB = std::min(static_cast<int>(std::floor(syBot)), c.floorClip[x] - 1);
    float shade = std::clamp((light / 255.0f) * R_DistanceShade(colDepth), 0.0f, 1.0f);
    for (int y = yA; y <= yB; ++y) {
        float z = c.eyeZ + (c.h / 2.0f - y) / (c.focal * scale);     // invert projection -> world z
        // 1 map unit = 1 texel. V grows DOWNWARD from the texture's anchor height (anchorZ is the
        // world z where texture V=0; vanilla r_segs.c texturemid selected by ML_DONTPEGTOP/BOTTOM).
        float V = (anchorZ - z) + rowoffset;
        int vi = static_cast<int>(std::floor(V)); vi %= T.height; if (vi < 0) vi += T.height;
        int ui = static_cast<int>(std::floor(U));   ui %= T.width;  if (ui < 0) ui += T.width;
        uint32_t texel = T.rgba[static_cast<size_t>(vi) * T.width + ui];
        if (!(texel & 0xFFu)) {                                      // transparent (masked) texel
            if (g_forceOpaque) c.fb[static_cast<size_t>(y) * c.w + x] = 0x000000FFu;   // diagnostics
            else c.seeThru[static_cast<size_t>(y) * c.w + x] = 1;   // a hole by design, not a seam
            continue;
        }
        uint8_t r = static_cast<uint8_t>((texel >> 24 & 0xFF) * shade);
        uint8_t g = static_cast<uint8_t>((texel >> 16 & 0xFF) * shade);
        uint8_t b = static_cast<uint8_t>((texel >> 8  & 0xFF) * shade);
        c.fb[static_cast<size_t>(y) * c.w + x] =
            (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
            (static_cast<uint32_t>(b) << 8) | 0xFFu;
        if (colDepth < c.worldDepthPx[static_cast<size_t>(y) * c.w + x])
            c.worldDepthPx[static_cast<size_t>(y) * c.w + x] = colDepth;
    }
    if (solid || upper) c.ceilingClip[x] = std::max(c.ceilingClip[x], std::min(static_cast<int>(std::floor(syBot)), c.h - 1));
    if (solid || lower) c.floorClip[x]   = std::min(c.floorClip[x],   std::max(static_cast<int>(std::ceil(syTop)),  0));
    // P4/P7: sprite occlusion is per-pixel (recorded inside the texel loop above).
    // Masked-middle walls (fences/grates) don't write opaque pixels unless their texels are
    // opaque, so a sprite behind a fence still shows through its holes.
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
    if (sg.linedef < 0 || sg.linedef >= static_cast<int>(m.lines.size())) return;
    const auto& L = m.lines[sg.linedef];
    int sideIdx = (sg.side == 0 || sg.side == 1) ? L.sidenum[sg.side] : -1;
    if (sideIdx < 0 || sideIdx >= static_cast<int>(m.sides.size())) return;
    const auto& sd = m.sides[sideIdx];
    const auto& sec = m.sectors[sg.frontsector];
    int fH = sec.floorheight, cH = sec.ceilingheight, light = sec.lightlevel;

    float texBaseU = static_cast<float>(sg.offset) + static_cast<float>(sd.textureoffset);

    // Decide which wall parts exist.
    bool twoSided = sg.backsector >= 0 && sg.backsector < static_cast<int>(m.sectors.size());
    int  fH_f = fH, cH_f = cH;                       // front sector
    int  fH_b = fH, cH_b = cH;                       // back sector (defaults if invalid)
    if (twoSided) { fH_b = m.sectors[sg.backsector].floorheight; cH_b = m.sectors[sg.backsector].ceilingheight; }

    const Texture* midT = c.tex->wall(sd.midtexture);
    const Texture* topT = twoSided ? c.tex->wall(sd.toptexture) : nullptr;
    const Texture* botT = twoSided ? c.tex->wall(sd.bottomtexture) : nullptr;

    // --- Visplane marking (P3b): reserve ceiling/floor plane ranges over [x0,x1] ---
    const char* ceilPicF = sec.ceilingpic;
    const char* floorPicF = sec.floorpic;
    bool skyCeilF = TextureLookup::isSky(ceilPicF);
    bool skyFloorF = TextureLookup::isSky(floorPicF);
    const char* ceilPicB = twoSided ? m.sectors[sg.backsector].ceilingpic : ceilPicF;
    const char* floorPicB = twoSided ? m.sectors[sg.backsector].floorpic : floorPicF;
    int lightB = twoSided ? m.sectors[sg.backsector].lightlevel : light;
    bool ceilPicDiff  = std::strcmp(ceilPicF, ceilPicB)  != 0;
    bool floorPicDiff = std::strcmp(floorPicF, floorPicB) != 0;

    bool markCeil, markFloor;
    if (!twoSided) {
        markCeil = markFloor = true;
    } else {
        // vanilla r_segs.c:537-560: a plane is marked whenever the two sectors' heights DIFFER
        // (either direction) or their flat/light differ. The visible side's plane must be marked
        // at every height step so it closes against the boundary (P3.5d).
        markCeil  = (cH_f != cH_b) || ceilPicDiff  || (light != lightB);
        markFloor = (fH_f != fH_b) || floorPicDiff || (light != lightB);
        if (cH_b <= fH_f || fH_b >= cH_f) { markCeil = markFloor = true; }  // closed door
    }
    if (fH_f >= static_cast<int>(c.eyeZ) && !skyFloorF) markFloor = false;  // floor above view (sky always)
    if (cH_f <= static_cast<int>(c.eyeZ) && !skyCeilF) markCeil = false; // ceiling below view

    int ceilPlane = -1, floorPlane = -1;
    if (markCeil) {
        const Flat* cf = skyCeilF ? nullptr : c.tex->flatForFrame(ceilPicF, c.tick);
        ceilPlane = R_FindPlane(c.vps, static_cast<float>(cH_f), cf, light, skyCeilF, c.w);
        ceilPlane = R_CheckPlane(c.vps, ceilPlane, x0, x1);
    }
    if (markFloor) {
        const Flat* ff = skyFloorF ? nullptr : c.tex->flatForFrame(floorPicF, c.tick);
        floorPlane = R_FindPlane(c.vps, static_cast<float>(fH_f), ff, light, skyFloorF, c.w);
        floorPlane = R_CheckPlane(c.vps, floorPlane, x0, x1);
    }

    for (int x = x0; x <= x1; ++x) {
        const bool probe = x == g_probeCol;      // --colprobe: trace one screen column
        if (probe)
            std::printf("  seg line=%d side=%d front=%d back=%d x0=%d x1=%d cHf=%d fHf=%d"
                          " cHb=%d fHb=%d mid='%s' top='%s' bot='%s' markCeil=%d markFloor=%d",
                          sg.linedef, sg.side, sg.frontsector, sg.backsector, x0, x1,
                          cH_f, fH_f, cH_b, fH_b, sd.midtexture, sd.toptexture,
                          sd.bottomtexture, (int)markCeil, (int)markFloor);
        if (c.ceilingClip[x] >= c.floorClip[x] - 1) {
            if (probe) std::printf(" -> SKIP column already closed\n");
            continue;
        }
        float sx = (x - c.w / 2.0f) / c.focal;
        float denom = (r1o - r0o) - sx * (d1o - d0o);   // ORIGINAL (un-near-clipped) endpoints — load-bearing for perspective-correct U
        if (std::fabs(denom) < 1e-9f) {
            if (probe) std::printf(" -> SKIP parallel ray\n");
            continue;
        }
        float t = (sx * d0o - r0o) / denom;
        // The projected column range [x0,x1] decides whether this seg covers the column, but
        // the ray/line intersection can land a hair outside [0,1] at exactly those two edge
        // columns (float rounding between the forward and inverse projection). Dropping them
        // left a 1px black seam where two walls meet (E2M6 column 289: 105 unpainted pixels),
        // because the seg's plane marking is skipped along with its wall. Snap the edge columns
        // to the segment end; still reject anything else that is genuinely outside.
        if (t < -0.001f || t > 1.001f) {
            if (x != x0 && x != x1) {
                if (probe) std::printf(" -> SKIP t=%.4f outside seg\n", t);
                continue;
            }
            t = std::clamp(t, 0.0f, 1.0f);
        }
        float depth = d0o + t * (d1o - d0o);
        if (depth <= nearZ) continue;
        float scale = 1.0f / depth;
        float U = texBaseU + t * segLen;

        // Per-column plane bands (read pre-drawCol clips; vanilla visplane formulas).
        float yCeilF  = c.h / 2.0f - c.focal * (cH_f - c.eyeZ) * scale;
        float yFloorF = c.h / 2.0f - c.focal * (fH_f - c.eyeZ) * scale;
        // The opening is the band a viewer can see THROUGH: its edges are the nearer of the
        // two ceilings and the farther of the two floors. A step face (upper/lower wall) that
        // the mapper left untextured has nothing to paint it, so the plane must close over it
        // — which is why vanilla bounds the bands by the OPENING, not by the front sector's
        // nominal heights. Without that, an untextured riser or a closed door with no texture
        // leaves a black band across the view (found by --renderaudit in E2M2/E3M4/E4M1/E3M7).
        // When the step IS textured the wall paints the band, so the plane must stop at the
        // nominal edge or it would overdraw the wall texture.
        const int openTop = twoSided ? std::min(cH_f, cH_b) : cH_f;
        const int openBot = twoSided ? std::max(fH_f, fH_b) : fH_f;
        const bool upperPainted = twoSided && topT && cH_b < cH_f;
        const bool lowerPainted = twoSided && botT && fH_b > fH_f;
        const float yOpenTop = c.h / 2.0f - c.focal * (openTop - c.eyeZ) * scale;
        const float yOpenBot = c.h / 2.0f - c.focal * (openBot - c.eyeZ) * scale;
        const float yCeilEdge  = upperPainted ? yCeilF : yOpenTop;
        const float yFloorEdge = lowerPainted ? yFloorF : yOpenBot;
        int cc = c.ceilingClip[x], fc = c.floorClip[x];
        if (markCeil && ceilPlane >= 0) {
            int top = cc + 1;
            // Sky is an infinite-height backdrop: it fills from the top of the open region
            // down to the HORIZON (where the floor begins), not down to the projected nominal
            // ceiling. For a high/outdoor sky the nominal ceiling (yCeilF) projects off the
            // top of the screen, so using it left a black band between sky and floor. P3d fix.
            // Non-sky: the wall top row is ceil(yCeilF) (drawCol), so the plane band must end at
            // ceil(yCeilF)-1 to stay contiguous (floor() left a 1-px seam when fractional).
            int bot = skyCeilF ? static_cast<int>(std::floor(yFloorEdge))
                               : (static_cast<int>(std::ceil(yCeilEdge)) - 1);
            if (bot >= fc) bot = fc - 1;
            if (top <= bot) { c.vps[ceilPlane].top[x] = top; c.vps[ceilPlane].bottom[x] = bot; }
        }
        if (markFloor && floorPlane >= 0) {
            int top = static_cast<int>(std::floor(yFloorEdge)) + 1;
            int bot = fc - 1;
            if (top <= cc) top = cc + 1;
            if (top <= bot) { c.vps[floorPlane].top[x] = top; c.vps[floorPlane].bottom[x] = bot; }
        }

        if (probe)
            std::printf("     -> depth=%.1f t=%.3f cc=%d fc=%d yCeilEdge=%.1f yFloorEdge=%.1f"
                        " upperPainted=%d lowerPainted=%d hasMid=%d\n",
                        depth, t, cc, fc, yCeilEdge, yFloorEdge,
                        (int)upperPainted, (int)lowerPainted, midT != nullptr);
        if (!twoSided) {
            // vanilla: top of texture at ceiling, unless DONTPEGBOTTOM (bottom at floor).
            if (midT) {
                float anchor = (L.flags & ML_DONTPEGBOTTOM) ? static_cast<float>(fH_f) + midT->height
                                                            : static_cast<float>(cH_f);
                drawCol(c, x, scale, *midT, U, sd.rowoffset, anchor, cH_f, fH_f, light, /*solid*/true, false, false, depth);
            }
        } else {
            // upper wall (ceiling step): vanilla r_segs.c:570 draws toptexture when
            // worldhigh < worldtop -- i.e. back ceiling LOWER than front.
            // Pegging: DONTPEGTOP -> top at front ceiling; else bottom at back ceiling.
            if (topT && cH_b < cH_f) {
                float anchor = (L.flags & ML_DONTPEGTOP) ? static_cast<float>(cH_f)
                                                         : static_cast<float>(cH_b) + topT->height;
                drawCol(c, x, scale, *topT, U, sd.rowoffset, anchor, cH_f, cH_b, light, false, /*upper*/true, false, depth);
            }
            // lower wall (floor step): vanilla r_segs.c:589 draws bottomtexture when back floor
            // is HIGHER than front (worldlow>worldbottom) -- the riser of a floor step-UP.
            // (Old fH_b<fH_f was inverted -> missed the pit step-up riser -> HOM gap.)
            // Pegging: default top at back floor; DONTPEGBOTTOM anchors at front ceiling (vanilla).
            if (botT && fH_b > fH_f) {
                float anchor = (L.flags & ML_DONTPEGBOTTOM) ? static_cast<float>(cH_f)
                                                           : static_cast<float>(fH_b);
                drawCol(c, x, scale, *botT, U, sd.rowoffset, anchor, fH_b, fH_f, light, false, false, /*lower*/true, depth);
            }
            // masked middle (fences/grates): floats in the back opening; transparent pixels see through; no clip update.
            // Same one-sided-mid pegging as vanilla's masked draw (default top at ceiling, else bottom at floor).
            if (midT && cH_b > fH_b) {
                float anchor = (L.flags & ML_DONTPEGBOTTOM) ? static_cast<float>(fH_f) + midT->height
                                                            : static_cast<float>(cH_f);
                drawCol(c, x, scale, *midT, U, sd.rowoffset, anchor, cH_b, fH_b, light, false, false, false, depth);
            }
        }
    }
}

void renderSubsector(Cam& c, const MapData& m, int idx) {
    if (idx < 0 || idx >= static_cast<int>(m.subsectors.size())) return;
    const subsector_t& ss = m.subsectors[idx];
    for (int i = 0; i < ss.segcount; ++i) {
        int s = ss.firstseg + i;
        if (s >= 0 && s < static_cast<int>(m.segs.size())) renderSeg(c, m, m.segs[s]);
    }
    // P4: collect vissprites for this subsector's things. idx is the subsector index
    // (callers pass child & ~NF_SUBSECTOR); thingsBySubsec holds const mobj_t* bins.
    if (c.sprites && c.thingsBySubsec && idx >= 0 &&
        idx < static_cast<int>(c.thingsBySubsec->size())) {
        for (const mobj_t* mo : (*c.thingsBySubsec)[idx]) {
            if (!mo) continue;
            vissprite_t vs;
            if (R_ProjectSprite(vs, *mo, *c.sprites,
                                c.px, c.py, c.sin, c.cos, c.eyeZ, c.focal, c.w, c.h))
                c.vissprites.push_back(vs);
        }
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

void R_SetForceOpaqueForDiagnostics(bool on) { g_forceOpaque = on; }
void R_SetColumnProbe(int col) { g_probeCol = col; }
void R_SetSeamFillForDiagnostics(bool on) { g_seamFill = on; }


void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  const TextureLookup& tex, float px, float py, float ang,
                  float eyeZ, uint32_t animTick,
                  const SpriteCache* sprites,
                  const std::vector<std::vector<const mobj_t*>>* thingsBySubsec,
                  uint32_t clearColor) {
    if (map.nodes.empty() || map.subsectors.empty()) return;
    Cam c;
    c.px = px; c.py = py; c.eyeZ = eyeZ;
    c.sin = std::sin(ang); c.cos = std::cos(ang);
    c.w = w; c.h = h; c.fb = fb; c.focal = w / 2.0f;
    c.tex = &tex; c.tick = animTick;
    c.ceilingClip.assign(w, -1);
    c.floorClip.assign(w, h);
    c.worldDepthPx.assign(static_cast<size_t>(w) * h, std::numeric_limits<float>::infinity());
    c.seeThru.assign(static_cast<size_t>(w) * h, 0);
    c.plane.w = w; c.plane.h = h; c.plane.focal = c.focal; c.plane.eyeZ = eyeZ;
    c.plane.px = px; c.plane.py = py; c.plane.sin = c.sin; c.plane.cos = c.cos;
    c.plane.fb = fb;
    c.plane.depth = &c.worldDepthPx;   // visplanes must occlude sprites too, not only walls
    c.plane.sky = tex.sky();
    c.plane.viewAng = ang;
    c.plane.skyScroll = static_cast<int>(animTick & 0x7fffffff);   // one column per anim tick
    R_SetupPlaneTables(c.plane);
    c.vps.clear();
    c.vissprites.clear();
    c.sprites = sprites; c.thingsBySubsec = thingsBySubsec;
    for (int i = 0; i < w * h; ++i) fb[i] = clearColor;             // opaque black by default
    renderNode(c, map, static_cast<uint32_t>(map.nodes.size() - 1));
    R_DrawPlanes(c.plane, c.vps);
    // Coverage guarantee: a column can still hold a row that neither a wall nor a plane band
    // painted, when two adjacent segs at slightly different depths project the same world
    // height to neighbouring rows (float rounding; --renderaudit found 1px holes like this in
    // E1M6). Vanilla avoids it by sharing integer ranges between a wall and its planes;
    // doomcpp computes them per seg, so repair the seam from the pixel above (or below)
    // instead of showing a black speck. "Unpainted" is unambiguous: every real write sets
    // alpha 0xFF, and the default clear value has alpha 0.
    if (g_seamFill) {
        for (int x = 0; x < w; ++x)
            for (int y = 0; y < h; ++y) {
                std::uint32_t& p = fb[static_cast<size_t>(y) * w + x];
                if ((p & 0xFFu) == 0xFFu) continue;                 // painted
                // A masked texel (grate, window, fence) leaves the pixel open on purpose so the
                // geometry behind it shows; when there is none, vanilla shows black there too.
                // Only a pixel nobody *chose* to paint is a seam worth repairing.
                if (c.seeThru[static_cast<size_t>(y) * w + x]) continue;
                if (y > 0 && (fb[static_cast<size_t>(y - 1) * w + x] & 0xFFu) == 0xFFu)
                    p = fb[static_cast<size_t>(y - 1) * w + x];
                else if (y + 1 < h && (fb[static_cast<size_t>(y + 1) * w + x] & 0xFFu) == 0xFFu)
                    p = fb[static_cast<size_t>(y + 1) * w + x];
                else p = 0x000000FFu;                               // nothing in this column
            }
    }
    // P4: painter's algorithm — sort vissprites far->near (depth DESC) and draw after visplanes.
    // Per-pixel occlusion: worldDepthPx[y*w+x] holds the nearest opaque SURFACE's depth —
    // walls from drawCol's texel loop and floors/ceilings from R_DrawSpan — so a nearer wall
    // OR a nearer floor hides only the sprite pixels it covers.
    // This fixes the old per-column array, where a low floor riser at 32 units hid every sprite
    // in the room (the P4 window/riser limitation).
    // ceilingClip/floorClip stay as the wall-renderer left them (no longer passed to sprites).
    if (c.sprites && !c.vissprites.empty()) {
        std::sort(c.vissprites.begin(), c.vissprites.end(),
                  [](const vissprite_t& a, const vissprite_t& b){ return a.depth > b.depth; });
        for (const vissprite_t& vs : c.vissprites)
            R_DrawVisSprite(c.fb, c.w, c.h, vs, c.eyeZ, c.focal, c.worldDepthPx);
    }
}
