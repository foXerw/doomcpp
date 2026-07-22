#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include "core/doomtype.h"

class WadFile;   // fwd

// Node child encodings + blockmap geometry (from r_main.c / p_local.h).
constexpr uint32_t NF_SUBSECTOR = 0x8000;   // high bit (0x8000) of a node child marks a subsector leaf
constexpr int MAPBLOCK = 128;               // blockmap cell size in map units (MAPBLOCKUNITS)
// linedef flags (doomdata.h)
constexpr int ML_BLOCKING      = 1;
constexpr int ML_BLOCKMONSTERS = 2;
constexpr int ML_TWOSIDED      = 4;

struct vertex_t    { fixed_t x, y; };
struct line_t      { int v1, v2, flags, special, tag; int sidenum[2]; mutable int validcount = 0; };
struct side_t      {
    int  textureoffset;          // mapsidedef_t +0 (texel units)
    int  rowoffset;              // +2
    char toptexture[9];          // +4  (8 chars + NUL)
    char bottomtexture[9];       // +12
    char midtexture[9];          // +20
    int  sector;                 // +28
};
struct sector_t    {
    int  floorheight, ceilingheight;   // +0 / +2
    char floorpic[9], ceilingpic[9];   // +4 / +12
    int  lightlevel, special, tag;     // +20 / +22 / +24
};
struct seg_t       { int v1, v2, linedef, side, offset, frontsector, backsector; };
struct subsector_t { int segcount, firstseg; };
struct node_t      { float x, y, dx, dy; uint32_t children[2]; };
struct thing_t     { int x, y, angleDeg, type; };

// Parsed BLOCKMAP (p_setup.c::P_LoadBlockMap). lump holds the raw int16 stream:
// header [orgx,orgy,width,height] then per-cell offsets then -1-terminated line lists.
struct Blockmap {
    int orgx = 0, orgy = 0;        // origin in map units (may be negative)
    int width = 0, height = 0;     // cell counts
    std::vector<std::int16_t> lump;
};
Blockmap parseBlockmap(const byte* d, size_t n);

struct MapData {
    std::vector<vertex_t>    vertices;
    std::vector<line_t>      lines;
    std::vector<sector_t>    sectors;
    std::vector<side_t>      sides;
    std::vector<seg_t>       segs;
    std::vector<subsector_t> subsectors;
    std::vector<node_t>      nodes;
    std::vector<thing_t>     things;
    Blockmap blockmap;
};

// Pure parsers (on-disk LE short structs from doomdata.h).
std::vector<vertex_t>    parseVertexes(const byte* d, size_t n);   // 4 bytes
std::vector<line_t>      parseLinedefs(const byte* d, size_t n);   // 14 bytes
std::vector<sector_t>    parseSectors(const byte* d, size_t n);    // 26 bytes
std::vector<side_t>      parseSidedefs(const byte* d, size_t n);   // 30 bytes
std::vector<seg_t>       parseSegs(const byte* d, size_t n);       // 12 bytes
std::vector<subsector_t> parseSubsectors(const byte* d, size_t n); // 4 bytes
std::vector<node_t>      parseNodes(const byte* d, size_t n);      // 28 bytes
std::vector<thing_t>     parseThings(const byte* d, size_t n);     // 10 bytes

// Load a map by marker name (e.g. "E1M1"); resolves seg front/back sectors.
MapData loadMap(const WadFile& wad, const std::string& mapname);

// Find thing type 1 (player 1 start); sets x/y/ang. Returns false if none.
bool playerStart(const MapData& m, float& x, float& y, float& ang);
