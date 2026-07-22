#pragma once
#include "play/p_setup.h"   // MapData, node_t
#include <vector>

// Partition side test (r_main.c::R_PointOnSide): 0 = front, 1 = back.
int R_PointOnSide(float x, float y, const node_t& n);
// BSP descent to the subsector containing (x,y) (r_main.c::R_PointInSubsector).
int R_PointInSubsector(const MapData& m, float x, float y);
// First seg's front sector for a subsector index (-1 if invalid).
int sectorOf(const MapData& m, int subsectorIdx);

struct BBox { float left, right, bottom, top; };
int  P_BoxOnLineSide(const BBox& bb, float x1, float y1, float x2, float y2);   // Task 3
struct Opening { float top = 0, bottom = 0, range = 0, lowfloor = 0; };
Opening P_LineOpening(const MapData& m, const line_t& L);                         // Task 3
std::vector<int> blockLinesInCell(const Blockmap& bm, int cx, int cy);           // Task 3
