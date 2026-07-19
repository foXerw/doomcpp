#pragma once
#include "core/doomtype.h"

struct MapData;

// Render one 3D frame of solid-color walls: BSP front-to-back traversal,
// seg projection into screen columns, per-column occlusion.
void R_RenderView(uint32_t* fb, int w, int h, const MapData& map,
                  float px, float py, float ang, float eyeZ);
