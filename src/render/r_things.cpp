#include "r_things.h"
#include "r_data.h"        // loadPalette, decodePatch
#include "r_plane.h"       // R_DistanceShade
#include "wad/wadfile.h"   // WadFile (ctor)
#include <algorithm>
#include <cctype>
#include <cmath>

SpriteLumpName parseSpriteLumpName(const std::string& name) {
    SpriteLumpName out;
    // sprname = first 4 chars, uppercased, NUL-terminated
    for (int i = 0; i < 4 && i < static_cast<int>(name.size()); ++i)
        out.sprname[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[i])));
    out.sprname[4] = 0;
    // primary install: frame = name[4], rotation = name[5]-'0'
    out.inst[0].frame = name.size() > 4 ? name[4] : 'A';
    out.inst[0].rotation = name.size() > 5 ? (name[5] - '0') : 0;
    out.inst[0].flip = false;
    out.nInst = 1;
    // mirrored second install: name[6] = frame, name[7] = rotation, flip = true
    if (name.size() > 7 && name[6] != ' ' && name[6] != '\0') {
        out.inst[1].frame = name[6];
        out.inst[1].rotation = name[7] - '0';
        out.inst[1].flip = true;
        out.nInst = 2;
    }
    return out;
}

// ---- SpriteCache (Task 4) ----
namespace {
std::string upperS(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

void SpriteCache::install(const std::string& lumpName, const Patch* patch) {
    SpriteLumpName pn = parseSpriteLumpName(lumpName);
    auto& arr = frames_[upperS(pn.sprname)];
    for (int i = 0; i < pn.nInst; ++i) {
        const LumpInstall& ins = pn.inst[i];
        int fi = ins.frame - 'A';
        if (fi < 0 || fi >= 29) continue;
        SpriteFrame& f = arr[fi];
        f.present = true;
        if (ins.rotation == 0) {                       // all angles, no flip
            f.rotate = false;
            f.lump[0] = patch;
            f.flip[0] = false;
        } else if (ins.rotation >= 1 && ins.rotation <= 8) {
            f.rotate = true;
            f.lump[ins.rotation - 1] = patch;
            f.flip[ins.rotation - 1] = ins.flip;
        }
    }
}

SpriteCache::View SpriteCache::resolve(const std::string& sprname, char frame, int rotIndex) const {
    auto it = frames_.find(upperS(sprname));
    if (it == frames_.end()) return {};                // absent sprite
    int fi = frame - 'A';
    if (fi < 0 || fi >= 29) return {};                 // out-of-range frame
    const SpriteFrame& f = it->second[fi];
    if (!f.present) return {};                         // absent frame
    int idx = f.rotate ? (rotIndex & 7) : 0;
    return {f.lump[idx], f.flip[idx]};
}

SpriteCache::SpriteCache(const WadFile& wad) {
    WadFile& w = const_cast<WadFile&>(wad);
    const auto pal = loadPalette(wad);

    // First pass: locate S_START / S_END lump indices (case-insensitive).
    int start = -1, end = -1;
    for (int i = 0; i < w.numLumps(); ++i) {
        std::string nm = upperS(w.lumpName(i));
        if (nm == "S_START") start = i;
        else if (nm == "S_END") { end = i; break; }
    }
    if (start < 0 || end < 0) return;                  // no sprite block

    // Decode every non-empty sprite lump into owned_, remembering each lump's
    // directory index so empty lumps don't desync the name->patch mapping.
    std::vector<int> lumpIdx;
    for (int i = start + 1; i < end; ++i) {
        auto raw = w.readLump(i);
        if (raw.empty()) continue;
        owned_.push_back(decodePatch(raw.data(), raw.size(), pal.data()));
        lumpIdx.push_back(i);
    }
    // Install pointers ONLY after owned_ is fully populated: push_back may
    // realloc, so installing mid-loop would dangle the stored const Patch*.
    for (size_t i = 0; i < owned_.size(); ++i) {
        install(w.lumpName(lumpIdx[i]), &owned_[i]);
    }
}

// ---- spriteRotationIndex (Task 5) ----
// Float equivalent of vanilla `rot = (ang - thing->angle + (ANG45/2)*9) >> 29`.
// (ANG45/2)*9 = 202.5 deg = 9*pi/8 shifts the bin grid so that relAng == 0
// (player directly behind the thing's facing) lands in the centre of bin 4.
int spriteRotationIndex(float relAngRad) {
    constexpr float PI = 3.141592653589793f;
    constexpr float TWO_PI = 2.0f * PI;
    constexpr float OFFSET = 9.0f * PI / 8.0f;   // (ANG45/2)*9 = 202.5 deg
    float a = std::fmod(relAngRad + OFFSET, TWO_PI);
    if (a < 0.0f) a += TWO_PI;                   // fmod may be negative
    int r = static_cast<int>(a / (TWO_PI / 8.0f));
    if (r < 0) r = 0;                            // clamp at boundaries
    if (r > 7) r = 7;
    return r;
}

// ---- R_ProjectSprite (Task 7) ----
bool R_ProjectSprite(vissprite_t& out, const mobj_t& t, const SpriteCache& sprites,
                     float px, float py, float sin, float cos, float /*eyeZ*/,
                     float focal, int w, int h) {
    float gx = static_cast<float>(t.x >> FRACBITS);
    float gy = static_cast<float>(t.y >> FRACBITS);
    float dx = gx - px, dy = gy - py;
    float depth = dx * sin + dy * cos;     // forward (same basis as renderSeg::toCam)
    float tx    = dx * cos - dy * sin;     // rightward
    (void)h;
    constexpr float nearZ = 0.01f;
    if (depth <= nearZ) return false;

    // rotation: vanilla ang = R_PointToAngle(view->thing) = angle from PLAYER to THING,
    // minus the thing's facing. NB atan2 args are (gy-py, gx-px) = player->thing vector;
    // the flipped (py-gy, px-gx) = thing->player would rotate every sprite by 4 (back<->front).
    float viewAng = std::atan2f(gy - py, gx - px);
    float thingRad = static_cast<float>(static_cast<double>(t.angle) * (6.283185307179586 / 4294967296.0));
    int rotIdx = spriteRotationIndex(viewAng - thingRad);

    SpriteCache::View v = sprites.resolve(t.sprname, t.frame, rotIdx);
    if (!v.patch) return false;

    float scale = 1.0f / depth;
    float ppe = focal * scale;                       // pixels per texel
    float xcenter = w / 2.0f + focal * tx * scale;
    float xTrueLeft = xcenter - v.patch->leftoffset * ppe;
    float xTrueRight = xTrueLeft + v.patch->width * ppe;
    int x1 = static_cast<int>(std::floor(xTrueLeft));
    int x2 = static_cast<int>(std::ceil(xTrueRight)) - 1;
    if (x1 > x2) return false;
    if (x2 < 0 || x1 >= w) return false;             // fully off-screen
    x1 = std::max(0, x1); x2 = std::min(w - 1, x2);

    out.depth = depth;
    out.scale = scale;
    out.x1 = x1; out.x2 = x2;
    out.xTrueLeft = xTrueLeft;
    out.gzt = static_cast<float>(t.z >> FRACBITS) + static_cast<float>(v.patch->topoffset);
    out.patch = v.patch;
    out.flip = v.flip;
    out.fuzz = (t.flags & MF_SHADOW) != 0;   // P7: spectres
    out.light = t.fullbright ? 255 : t.light;
    return true;
}

// ---- R_DrawVisSprite (Task 8) ----
// Per-screen-column masked draw. Image TOP (texel row 0) sits at world-z `gzt`
// (set by R_ProjectSprite = z + topoffset). syTop is the screen row of image row 0;
// moving down the screen (y increasing) decreases worldZ, so imgRow = gzt - worldZ
// grows downward (screen-down == image-down). Shading/byte-order mirror drawCol.
//
// Occlusion: depthBuffer[y*w+x] is the nearest opaque wall's depth at that pixel (inf if
// none). If it is <= vis.depth the wall pixel hides just that sprite pixel; the sprite
// overdraws wall/plane pixels within its own [syTop,syBot] extent elsewhere. Only opaque
// texels write depth, so sprites behind a fence/grating show through its holes.
void R_DrawVisSprite(uint32_t* fb, int w, int h, const vissprite_t& vis,
                     float eyeZ, float focal,
                     const std::vector<float>& depthBuffer) {
    if (!vis.patch || vis.patch->width <= 0 || vis.patch->height <= 0) return;
    if (vis.x1 > vis.x2) return;
    float ppe = focal * vis.scale;                       // pixels per texel
    if (ppe <= 0.0f) return;
    float shade = std::clamp((vis.light / 255.0f) * R_DistanceShade(vis.depth), 0.0f, 1.0f);
    float syTop = h / 2.0f - focal * (vis.gzt - eyeZ) * vis.scale;   // screen row of image row 0
    float syBot = syTop + ppe * vis.patch->height;                   // screen row just past image bottom
    int yA = std::max(0, static_cast<int>(std::ceil(syTop)));
    int yB = std::min(h - 1, static_cast<int>(std::floor(syBot)));
    for (int x = vis.x1; x <= vis.x2; ++x) {
        if (x < 0 || x >= w) continue;
        const bool colOccluded = depthBuffer.size() == static_cast<size_t>(w) * h;
        float tc = (static_cast<float>(x) - vis.xTrueLeft) / ppe;
        if (vis.flip) tc = (vis.patch->width - 1) - tc;              // mirror texel column
        int col = static_cast<int>(std::floor(tc));
        if (col < 0 || col >= vis.patch->width) continue;
        for (int y = yA; y <= yB; ++y) {
            float worldZ = eyeZ + (h / 2.0f - y) / (focal * vis.scale);  // inverse project screen row -> world z
            int row = static_cast<int>(std::floor(vis.gzt - worldZ));    // screen-down == image-down
            if (row < 0 || row >= vis.patch->height) continue;
            if (colOccluded &&
                depthBuffer[static_cast<size_t>(y) * w + x] <= vis.depth) continue;  // wall pixel in front
            uint32_t texel = vis.patch->rgba[static_cast<size_t>(row) * vis.patch->width + col];
            if (!(texel & 0xFFu)) continue;                            // transparent (alpha==0) -> skip
            uint8_t r = static_cast<uint8_t>((texel >> 24 & 0xFF) * shade);
            uint8_t g = static_cast<uint8_t>((texel >> 16 & 0xFF) * shade);
            uint8_t b = static_cast<uint8_t>((texel >> 8  & 0xFF) * shade);
            uint32_t& dst = fb[static_cast<size_t>(y) * w + x];
            if (vis.fuzz) {                       // spectre fuzz: 50/50 blend
                r = static_cast<uint8_t>((r + (dst >> 24 & 0xFF)) >> 1);
                g = static_cast<uint8_t>((g + (dst >> 16 & 0xFF)) >> 1);
                b = static_cast<uint8_t>((b + (dst >> 8  & 0xFF)) >> 1);
            }
            dst = (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
                  (static_cast<uint32_t>(b) << 8) | 0xFFu;
        }
    }
}

void R_DrawPSprite(uint32_t* fb, int w, int h, const char* sprname, char frame,
                   fixed_t sx, fixed_t sy, const SpriteCache& sprites) {
    if (!sprname || !sprname[0]) return;
    SpriteCache::View v = sprites.resolve(sprname, frame, 0);
    if (!v.patch) return;
    const Patch& p = *v.patch;
    // Vanilla R_DrawPSprite anchors (pspritescale == 1 at 320x200):
    //   x = centerx + (psp->sx - 160) - leftoffset
    //   texturemid = BASEYCENTER + 0.5 - (psp->sy - topoffset); top = centery - texturemid
    const int destX = w / 2 - 160 + static_cast<int>(sx >> FRACBITS) - p.leftoffset;
    const int destY = h / 2 - 100 + static_cast<int>(sy >> FRACBITS) - p.topoffset;
    for (int py = 0; py < p.height; ++py) {
        const int y = destY + py;
        if (y < 0 || y >= h) continue;
        for (int px = 0; px < p.width; ++px) {
            const int x = destX + (v.flip ? (p.width - 1 - px) : px);
            if (x < 0 || x >= w) continue;
            const uint32_t texel = p.rgba[static_cast<size_t>(py) * p.width + px];
            if (!(texel & 0xFFu)) continue;                 // transparent
            fb[static_cast<size_t>(y) * w + x] = texel;      // psprites are fullbright
        }
    }
}
