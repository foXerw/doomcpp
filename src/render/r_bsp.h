#pragma once
#include "core/doomtype.h"
#include <vector>

struct MapData;
class TextureLookup;
struct mobj_t;          // p_mobj.h
class SpriteCache;       // r_things.h
struct vissprite_t;      // r_things.h

// Render one frame: textured walls (BSP front-to-back, per-column clip occlusion) with
// distance shading, then visplane floor/ceiling flats (P3b). animTick drives animated flats.
// P4: optional sprites + thingsBySubsec enable sprite collection (during BSP traversal)
// and back-to-front vissprite draw AFTER visplanes. When both are nullptr (default),
// the sprite phase is skipped entirely -> rendering identical to pre-P4.
// Diagnostic only: when on, masked/transparent texture texels are painted opaque black instead
// of skipped, so a sentinel-clear scan counts only TRUE geometry holes (not texture holes).
void R_SetForceOpaqueForDiagnostics(bool on);
// Diagnostics: print every seg that reaches screen column `col` while rendering, to find
// why a band of pixels is never painted. -1 disables.
void R_SetColumnProbe(int col);

// Diagnostics: the world pass seam-fills any pixel no surface painted (see R_RenderView).
// --gapscan / --renderaudit turn it OFF so they still count the true holes themselves.
void R_SetSeamFillForDiagnostics(bool on);

// clearColor fills the framebuffer before BSP traversal. The default is the magic value 0
// (alpha 0 = "nothing painted yet"); every real write sets alpha 0xFF, so leftover pixels are
// identifiable and get seam-filled. Debug tooling (--gapscan) passes a sentinel color instead
// and disables the fill, to count pixels no surface covered.
void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  const TextureLookup& tex, float px, float py, float ang,
                  float eyeZ, uint32_t animTick,
                  const SpriteCache* sprites = nullptr,
                  const std::vector<std::vector<const mobj_t*>>* thingsBySubsec = nullptr,
                  uint32_t clearColor = 0x00000000u);
