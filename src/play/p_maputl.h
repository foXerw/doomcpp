#pragma once
#include "core/doomtype.h"
#include "play/p_setup.h"   // MapData, node_t
#include <vector>

// Partition side test (r_main.c::R_PointOnSide): 0 = front, 1 = back.
int R_PointOnSide(fixed_t x, fixed_t y, const node_t& n);
// BSP descent to the subsector containing (x,y) (r_main.c::R_PointInSubsector).
int R_PointInSubsector(const MapData& m, fixed_t x, fixed_t y);
// First seg's front sector for a subsector index (-1 if invalid).
int sectorOf(const MapData& m, int subsectorIdx);

struct BBox { fixed_t left, right, bottom, top; };
int  P_BoxOnLineSide(const BBox& bb, fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2);
struct Opening { fixed_t top = 0, bottom = 0, range = 0, lowfloor = 0; };
Opening P_LineOpening(const MapData& m, const line_t& L);
std::vector<int> blockLinesInCell(const Blockmap& bm, int cx, int cy);
