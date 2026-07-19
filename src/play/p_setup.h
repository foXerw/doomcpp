#pragma once
#include <string>
#include <vector>
#include "core/doomtype.h"

class WadFile;   // fwd

struct vertex_t { fixed_t x, y; };
struct line_t   { int v1, v2, flags, special, tag; int sidenum[2]; };

struct MapData {
    std::vector<vertex_t> vertices;
    std::vector<line_t>   lines;
    int numSides = 0;
    int numSectors = 0;
    // SECTORS/SEGS/SSECTORS/NODES parsed in P2b.
};

// Pure parsers (unit-testable with crafted buffers).
std::vector<vertex_t> parseVertexes(const byte* d, size_t n);  // n = byte count
std::vector<line_t>   parseLinedefs(const byte* d, size_t n);

// Load a map by its marker lump name (e.g. "E1M1") from an open WAD.
MapData loadMap(const WadFile& wad, const std::string& mapname);
