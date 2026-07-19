#pragma once
#include <string>
#include <vector>
#include "core/doomtype.h"

class WadFile;   // fwd

struct vertex_t    { fixed_t x, y; };
struct line_t      { int v1, v2, flags, special, tag; int sidenum[2]; };
struct sector_t    { int floorheight, ceilingheight; };
struct side_t      { int sector; };
struct seg_t       { int v1, v2, linedef, side, frontsector, backsector; };
struct subsector_t { int segcount, firstseg; };
struct node_t      { float x, y, dx, dy; uint32_t children[2]; };
struct thing_t     { int x, y, angleDeg, type; };

struct MapData {
    std::vector<vertex_t>    vertices;
    std::vector<line_t>      lines;
    std::vector<sector_t>    sectors;
    std::vector<side_t>      sides;
    std::vector<seg_t>       segs;
    std::vector<subsector_t> subsectors;
    std::vector<node_t>      nodes;
    std::vector<thing_t>     things;
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
