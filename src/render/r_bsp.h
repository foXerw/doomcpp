#pragma once
#include "core/doomtype.h"

struct MapData;
class TextureLookup;

// Render one frame: textured walls (BSP front-to-back, per-column clip occlusion) with
// distance shading, then visplane floor/ceiling flats (P3b). animTick drives animated flats.
void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  const TextureLookup& tex, float px, float py, float ang,
                  float eyeZ, uint32_t animTick);
