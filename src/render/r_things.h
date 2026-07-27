#pragma once
#include "core/doomtype.h"
#include "render/r_data.h"   // Patch
#include <array>
#include <string>
#include <unordered_map>
#include <vector>
class WadFile;   // fwd

// ---- Sprite lump-name parse (Task 3) ----
struct LumpInstall { char frame = 0; int rotation = 0; bool flip = false; };  // rotation 0 == all 8
struct SpriteLumpName { char sprname[5] = {0}; LumpInstall inst[2]; int nInst = 0; };
SpriteLumpName parseSpriteLumpName(const std::string& name);

// ---- Sprite frame cache (Task 4) ----
// Indexes decoded sprite patches by sprname -> frame('A'..'Z') -> rotation(1..8),
// mirroring vanilla spriteframe_t. rotation 0 installs a single lump for all angles.
struct SpriteFrame {
    bool rotate = false;            // false: single (rot0) lump[0]; true: 8-way
    const Patch* lump[8] = {};      // index 0..7 == rotation 1..8
    bool flip[8] = {};
    bool present = false;
};

class SpriteCache {
public:
    SpriteCache() = default;                    // empty cache (unit-test path)
    explicit SpriteCache(const WadFile& wad);   // decode S_START..S_END into owned_
    // Index an externally-owned patch (storage managed by caller/ctor).
    void install(const std::string& lumpName, const Patch* patch);
    struct View { const Patch* patch = nullptr; bool flip = false; };
    // rotIndex 0..7. For non-rotate frames rotIndex is ignored.
    View resolve(const std::string& sprname, char frame, int rotIndex) const;
private:
    std::unordered_map<std::string, std::array<SpriteFrame, 29>> frames_;  // sprname -> [frame-'A']
    std::vector<Patch> owned_;   // decoded patches (ctor path); stable after ctor returns
};

// ---- 8-way rotation index (Task 5) ----
// relAngRad = (angle from thing toward player) - (thing's facing), in radians.
// Returns 0..7 (0 = facing viewer / front, 4 = back). Float equivalent of
// vanilla `rot = (ang - thing->angle + (ANG45/2)*9) >> 29`.
int spriteRotationIndex(float relAngRad);

// ---- SpriteDef / mobj_t (Task 6) ----
// doomednum -> spawn-state sprite/frame/fullbright, from vanilla info.c mobjinfo[].
// spriteDefForType returns nullptr for non-renderable types (player/DM starts,
// unknown) -> those things are skipped at mob-build time.
struct SpriteDef { int doomednum; const char* sprname; char frame; bool fullbright; };
const SpriteDef* spriteDefForType(int doomednum);

// Render-only mobj: carries the resolved sprite name/frame/fullbright/light so
// the renderer needs no global table. P5/P6 add health/momentum/state.
struct mobj_t {
    fixed_t x = 0, y = 0;        // 16.16
    int     z = 0;               // sector floor (map units)
    angle_t angle = 0;           // BAM
    char    sprname[5] = {0};    // resolved (e.g. "BAR1")
    char    frame = 'A';         // 'A'..
    bool    fullbright = false;
    int     light = 160;         // sector lightlevel 0..255
};

// ---- vissprite_t + R_ProjectSprite (Task 7) ----
// A projected, screen-space sprite: the bridge between R_Project (world -> screen)
// and R_DrawVisSprite (column raster). depth/scale are post-rotation (forward axis);
// gzt is the world-z of the image TOP (z + patch.topoffset), faithful to vanilla.
struct vissprite_t {
    float depth = 0, scale = 0;
    int   x1 = 0, x2 = -1;       // clamped screen column range [0,w-1]
    float xTrueLeft = 0;         // un-clamped left screen edge (texcol mapping)
    float gzt = 0;               // world-z of image TOP = z + topoffset
    const Patch* patch = nullptr;
    bool  flip = false;
    int   light = 160;           // 0..255 (255 if fullbright)
};
// Project a thing to a vissprite using the renderer's float basis
// (depth = dx*sin+dy*cos, tx = dx*cos-dy*sin — same as renderSeg in r_bsp.cpp).
// Returns false if behind the view plane, fully off-screen, or unresolvable.
bool R_ProjectSprite(vissprite_t& out, const mobj_t& t, const SpriteCache& sprites,
                     float px, float py, float sin, float cos, float eyeZ,
                     float focal, int w, int h);

// ---- R_DrawVisSprite (Task 8) ----
// Per-screen-column masked draw of one vissprite. For each column in [vis.x1, vis.x2]:
// if wallDepth[x] <= vis.depth a nearer-or-equal opaque wall fully occludes this column
// (skip); otherwise the sprite is the nearer surface and draws its own [syTop,syBot]
// extent (clamped to screen) over wall/plane pixels. Maps each screen column to a sprite
// texel column (mirrored if vis.flip), inverse-projects the row to world-z -> patch row,
// skips transparent texels (alpha==0), distance-shades, and writes RGBA.
// Shading/byte-order mirror drawCol in r_bsp.cpp exactly.
void R_DrawVisSprite(uint32_t* fb, int w, int h, const vissprite_t& vis,
                     float eyeZ, float focal,
                     const std::vector<float>& wallDepth);
