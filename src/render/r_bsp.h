#pragma once
#include "core/doomtype.h"

struct MapData;
class TextureLookup;

// Render one frame of textured walls (BSP front-to-back, per-column ceilingClip/floorClip occlusion).
// Two-sided openings render as black until P3b visplanes.
void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  const TextureLookup& tex, float px, float py, float ang, float eyeZ);
