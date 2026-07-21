#pragma once
#include <cstdint>
#include <tuple>
#include <vector>

struct Flat;  // defined in r_data.h (Task 1)

constexpr int kOpenTop = 0x7fffffff;   // sentinel: column unclaimed (top > bottom)
constexpr int kOpenBot = -1;
constexpr uint32_t kSkyColor = (0x40u<<24)|(0x30u<<16)|(0x28u<<8)|0xFFu;  // tuned in Task 8

struct Visplane { float height=0; const Flat* flat=nullptr; int lightlevel=0; bool sky=false;
                  int minx=0, maxx=-1; std::vector<int> top, bottom; };

struct PlaneCtx { int w=0,h=0; float focal=0,eyeZ=0,px=0,py=0,sin=0,cos=0; uint32_t* fb=nullptr;
                  std::vector<float> yslope, distscale; };

float R_DistanceShade(float depth);
int  R_FindPlane(std::vector<Visplane>& vps, float height, const Flat* flat, int light, bool sky, int w);
int  R_CheckPlane(std::vector<Visplane>& vps, int idx, int start, int stop);
void R_SetupPlaneTables(PlaneCtx& c);
std::vector<std::tuple<int,int,int>> R_PlaneSpans(const Visplane& pl);  // (y,x1,x2) in draw order
void R_DrawSpan(PlaneCtx& c, int y, int x1, int x2, const Flat* flat, float planeheight, int light, bool sky);
void R_DrawPlanes(PlaneCtx& c, std::vector<Visplane>& vps);
