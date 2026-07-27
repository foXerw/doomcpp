#pragma once
#include "core/doomtype.h"
#include <vector>

struct MapData;
class TextureLookup;
struct mobj_t;          // r_things.h
class SpriteCache;       // r_things.h
struct vissprite_t;      // r_things.h

// Render one frame: textured walls (BSP front-to-back, per-column clip occlusion) with
// distance shading, then visplane floor/ceiling flats (P3b). animTick drives animated flats.
// P4: optional things/sprites/thingsBySubsec enable sprite collection (during BSP traversal)
// and back-to-front vissprite draw AFTER visplanes. When all three are nullptr (default),
// the sprite phase is skipped entirely -> rendering identical to pre-P4.
void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  const TextureLookup& tex, float px, float py, float ang,
                  float eyeZ, uint32_t animTick,
                  const std::vector<mobj_t>* things = nullptr,
                  const SpriteCache* sprites = nullptr,
                  const std::vector<std::vector<int>>* thingsBySubsec = nullptr);
