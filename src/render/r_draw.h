#pragma once
#include "core/doomtype.h"

// Framebuffer drawing primitives (RGBA8888, top-left origin). Clipped.
void R_DrawPixel(uint32_t* fb, int w, int h, int x, int y, uint32_t color);
void R_DrawLine(uint32_t* fb, int w, int h,
                int x0, int y0, int x1, int y1, uint32_t color);
