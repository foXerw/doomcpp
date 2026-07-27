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

// ---- spriteDefForType (Task 6) ----
namespace {
// doomednum -> spawnstate sprite/frame/fullbright, from vanilla info.c mobjinfo[].
// Covers E1M1's renderable thing types (player/DM starts 1-4,11 are intentionally absent).
const SpriteDef kSpriteDefs[] = {
    {5,    "BKEY", 'A', false}, {9,    "SPOS", 'A', false}, {10,   "PLAY", 'W', false},
    {12,   "PLAY", 'W', false}, {15,   "PLAY", 'N', false}, {17,   "CELP", 'A', false},
    {18,   "POSS", 'L', false}, {20,   "TROO", 'M', false}, {21,   "SARG", 'N', false},
    {24,   "POL5", 'A', false}, {26,   "POL6", 'A', false}, {43,   "TRE1", 'A', false},
    {47,   "SMIT", 'A', false}, {48,   "ELEC", 'A', false}, {54,   "TRE2", 'A', false},
    {60,   "GOR4", 'A', false},
    {2001, "SHOT", 'A', false}, {2002, "MGUN", 'A', false}, {2003, "LAUN", 'A', false},
    {2004, "PLAS", 'A', false}, {2005, "CSAW", 'A', false}, {2007, "CLIP", 'A', false},
    {2008, "SHEL", 'A', false}, {2010, "ROCK", 'A', false}, {2011, "STIM", 'A', false},
    {2012, "MEDI", 'A', false}, {2013, "SOUL", 'A', true},  {2014, "BON1", 'A', false},
    {2015, "BON2", 'A', false}, {2018, "ARM1", 'A', false}, {2028, "COLU", 'A', true},
    {2035, "BAR1", 'A', false}, {2046, "BROK", 'A', false}, {2048, "AMMO", 'A', false},
    {2049, "SBOX", 'A', false},
    {3001, "TROO", 'A', false}, {3002, "SARG", 'A', false}, {3004, "POSS", 'A', false},
};
}  // namespace

const SpriteDef* spriteDefForType(int doomednum) {
    for (const SpriteDef& d : kSpriteDefs)
        if (d.doomednum == doomednum) return &d;
    return nullptr;
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
    out.gzt = static_cast<float>(t.z) + static_cast<float>(v.patch->topoffset);
    out.patch = v.patch;
    out.flip = v.flip;
    out.light = t.fullbright ? 255 : t.light;
    return true;
}

// ---- R_DrawVisSprite (Task 8) ----
// Per-screen-column masked draw. Image TOP (texel row 0) sits at world-z `gzt`
// (set by R_ProjectSprite = z + topoffset). syTop is the screen row of image row 0;
// moving down the screen (y increasing) decreases worldZ, so imgRow = gzt - worldZ
// grows downward (screen-down == image-down). Shading/byte-order mirror drawCol.
void R_DrawVisSprite(uint32_t* fb, int w, int h, const vissprite_t& vis,
                     float eyeZ, float focal,
                     const std::vector<int>& ceilingClip, const std::vector<int>& floorClip) {
    if (!vis.patch || vis.patch->width <= 0 || vis.patch->height <= 0) return;
    if (vis.x1 > vis.x2) return;
    float ppe = focal * vis.scale;                       // pixels per texel
    if (ppe <= 0.0f) return;
    float shade = std::clamp((vis.light / 255.0f) * R_DistanceShade(vis.depth), 0.0f, 1.0f);
    float syTop = h / 2.0f - focal * (vis.gzt - eyeZ) * vis.scale;   // screen row of image row 0
    float syBot = syTop + ppe * vis.patch->height;                   // screen row just past image bottom
    for (int x = vis.x1; x <= vis.x2; ++x) {
        if (x < 0 || x >= w) continue;
        if (ceilingClip[x] >= floorClip[x] - 1) continue;            // column fully occluded
        float tc = (static_cast<float>(x) - vis.xTrueLeft) / ppe;
        if (vis.flip) tc = (vis.patch->width - 1) - tc;              // mirror texel column
        int col = static_cast<int>(std::floor(tc));
        if (col < 0 || col >= vis.patch->width) continue;
        int yA = std::max(static_cast<int>(std::ceil(syTop)),  ceilingClip[x] + 1);
        int yB = std::min(static_cast<int>(std::floor(syBot)), floorClip[x] - 1);
        for (int y = yA; y <= yB; ++y) {
            float worldZ = eyeZ + (h / 2.0f - y) / (focal * vis.scale);  // inverse project screen row -> world z
            int row = static_cast<int>(std::floor(vis.gzt - worldZ));    // screen-down == image-down
            if (row < 0 || row >= vis.patch->height) continue;
            uint32_t texel = vis.patch->rgba[static_cast<size_t>(row) * vis.patch->width + col];
            if (!(texel & 0xFFu)) continue;                            // transparent (alpha==0) -> skip
            uint8_t r = static_cast<uint8_t>((texel >> 24 & 0xFF) * shade);
            uint8_t g = static_cast<uint8_t>((texel >> 16 & 0xFF) * shade);
            uint8_t b = static_cast<uint8_t>((texel >> 8  & 0xFF) * shade);
            fb[static_cast<size_t>(y) * w + x] =
                (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
                (static_cast<uint32_t>(b) << 8) | 0xFFu;
        }
    }
}
