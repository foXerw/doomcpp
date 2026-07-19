#include "p_setup.h"
#include "core/i_system.h"
#include "wad/wadfile.h"

namespace {
std::int16_t rd_i16(const byte* p) {
    return static_cast<std::int16_t>(
        static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}
}

std::vector<vertex_t> parseVertexes(const byte* d, size_t n) {
    const size_t rec = 4;
    if (n % rec != 0) I_Error("parseVertexes: bad size");
    std::vector<vertex_t> out(n / rec);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i].x = static_cast<fixed_t>(rd_i16(d + i * rec))     << 16;
        out[i].y = static_cast<fixed_t>(rd_i16(d + i * rec + 2)) << 16;
    }
    return out;
}

std::vector<line_t> parseLinedefs(const byte* d, size_t n) {
    const size_t rec = 14;
    if (n % rec != 0) I_Error("parseLinedefs: bad size");
    std::vector<line_t> out(n / rec);
    for (size_t i = 0; i < out.size(); ++i) {
        const byte* p = d + i * rec;
        out[i].v1      = rd_i16(p);
        out[i].v2      = rd_i16(p + 2);
        out[i].flags   = rd_i16(p + 4);
        out[i].special = rd_i16(p + 6);
        out[i].tag     = rd_i16(p + 8);
        out[i].sidenum[0] = rd_i16(p + 10);
        out[i].sidenum[1] = rd_i16(p + 12);
    }
    return out;
}

MapData loadMap(const WadFile& wad, const std::string& mapname) {
    int marker = wad.checkNumForName(mapname);
    if (marker < 0) I_Error(std::string("loadMap: map not found: ") + mapname);

    // Map lumps follow the marker in fixed order (doomdata.h ML_* enum).
    auto lumpBytes = [&](int offset) -> std::vector<byte> {
        int idx = marker + offset;
        if (idx < 0 || idx >= wad.numLumps()) return {};
        if (wad.lumpSize(idx) <= 0) return {};
        return const_cast<WadFile&>(wad).readLump(idx);
    };

    MapData m;
    auto verts = lumpBytes(4);   // ML_VERTEXES
    auto lines = lumpBytes(2);   // ML_LINEDEFS
    auto sides = lumpBytes(3);   // ML_SIDEDEFS
    auto sects = lumpBytes(8);   // ML_SECTORS
    m.vertices = verts.empty() ? std::vector<vertex_t>{} : parseVertexes(verts.data(), verts.size());
    m.lines    = lines.empty() ? std::vector<line_t>{}   : parseLinedefs(lines.data(), lines.size());
    m.numSides   = static_cast<int>(sides.size() / 30);  // mapsidedef_t = 30 bytes
    m.numSectors = static_cast<int>(sects.size() / 26);  // mapsector_t  = 26 bytes
    return m;
}
