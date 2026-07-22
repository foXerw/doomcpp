#include "p_setup.h"
#include "core/i_system.h"
#include "wad/wadfile.h"

namespace {
std::int16_t rd_i16(const byte* p) {
    return static_cast<std::int16_t>(
        static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

// Copy an 8-char WAD name (not necessarily NUL-terminated) into out[9],
// NUL-terminating and trimming trailing spaces so std::string compares are clean.
void rd_name(const byte* p, char out[9]) {
    for (int i = 0; i < 8; ++i) out[i] = static_cast<char>(p[i]);
    out[8] = '\0';
    for (int i = 7; i >= 0 && out[i] == ' '; --i) out[i] = '\0';
}
}

std::vector<vertex_t> parseVertexes(const byte* d, size_t n) {
    if (n % 4) I_Error("parseVertexes: bad size");
    std::vector<vertex_t> out(n / 4);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i].x = static_cast<fixed_t>(rd_i16(d + i * 4))     << 16;
        out[i].y = static_cast<fixed_t>(rd_i16(d + i * 4 + 2)) << 16;
    }
    return out;
}

std::vector<line_t> parseLinedefs(const byte* d, size_t n) {
    if (n % 14) I_Error("parseLinedefs: bad size");
    std::vector<line_t> out(n / 14);
    for (size_t i = 0; i < out.size(); ++i) {
        const byte* p = d + i * 14;
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

std::vector<sector_t> parseSectors(const byte* d, size_t n) {
    if (n % 26) I_Error("parseSectors: bad size");
    std::vector<sector_t> out(n / 26);
    for (size_t i = 0; i < out.size(); ++i) {
        const byte* p = d + i * 26;
        out[i].floorheight   = rd_i16(p);
        out[i].ceilingheight = rd_i16(p + 2);
        rd_name(p + 4,  out[i].floorpic);
        rd_name(p + 12, out[i].ceilingpic);
        out[i].lightlevel = rd_i16(p + 20);
        out[i].special    = rd_i16(p + 22);
        out[i].tag        = rd_i16(p + 24);
    }
    return out;
}

std::vector<side_t> parseSidedefs(const byte* d, size_t n) {
    if (n % 30) I_Error("parseSidedefs: bad size");
    std::vector<side_t> out(n / 30);
    for (size_t i = 0; i < out.size(); ++i) {
        const byte* p = d + i * 30;
        out[i].textureoffset = rd_i16(p);
        out[i].rowoffset     = rd_i16(p + 2);
        rd_name(p + 4,  out[i].toptexture);
        rd_name(p + 12, out[i].bottomtexture);
        rd_name(p + 20, out[i].midtexture);
        out[i].sector = rd_i16(p + 28);
    }
    return out;
}

std::vector<seg_t> parseSegs(const byte* d, size_t n) {
    if (n % 12) I_Error("parseSegs: bad size");
    std::vector<seg_t> out(n / 12);
    for (size_t i = 0; i < out.size(); ++i) {
        const byte* p = d + i * 12;
        out[i].v1 = rd_i16(p);
        out[i].v2 = rd_i16(p + 2);
        out[i].linedef = rd_i16(p + 6);
        out[i].side    = rd_i16(p + 8);
        out[i].offset  = rd_i16(p + 10);
        out[i].frontsector = -1;   // resolved in loadMap
        out[i].backsector  = -1;
    }
    return out;
}

std::vector<subsector_t> parseSubsectors(const byte* d, size_t n) {
    if (n % 4) I_Error("parseSubsectors: bad size");
    std::vector<subsector_t> out(n / 4);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i].segcount = rd_i16(d + i * 4);
        out[i].firstseg = rd_i16(d + i * 4 + 2);
    }
    return out;
}

std::vector<node_t> parseNodes(const byte* d, size_t n) {
    if (n % 28) I_Error("parseNodes: bad size");
    std::vector<node_t> out(n / 28);
    for (size_t i = 0; i < out.size(); ++i) {
        const byte* p = d + i * 28;
        out[i].x  = static_cast<float>(rd_i16(p));
        out[i].y  = static_cast<float>(rd_i16(p + 2));
        out[i].dx = static_cast<float>(rd_i16(p + 4));
        out[i].dy = static_cast<float>(rd_i16(p + 6));
        out[i].children[0] = static_cast<uint16_t>(rd_i16(p + 24));
        out[i].children[1] = static_cast<uint16_t>(rd_i16(p + 26));
    }
    return out;
}

std::vector<thing_t> parseThings(const byte* d, size_t n) {
    if (n % 10) I_Error("parseThings: bad size");
    std::vector<thing_t> out(n / 10);
    for (size_t i = 0; i < out.size(); ++i) {
        const byte* p = d + i * 10;
        out[i].x        = rd_i16(p);
        out[i].y        = rd_i16(p + 2);
        out[i].angleDeg = rd_i16(p + 4);
        out[i].type     = rd_i16(p + 6);
    }
    return out;
}

Blockmap parseBlockmap(const byte* d, size_t n) {
    Blockmap bm;
    size_t count = n / 2;
    bm.lump.resize(count);
    for (size_t i = 0; i < count; ++i) {
        std::uint16_t lo = d[i * 2];
        std::uint16_t hi = d[i * 2 + 1];
        bm.lump[i] = static_cast<std::int16_t>(lo | (hi << 8));
    }
    if (count >= 4) {
        bm.orgx   = bm.lump[0];
        bm.orgy   = bm.lump[1];
        bm.width  = bm.lump[2];
        bm.height = bm.lump[3];
    }
    return bm;
}

bool playerStart(const MapData& m, float& x, float& y, float& ang) {
    for (const auto& t : m.things) {
        if (t.type == 1) {   // player 1 start
            x = static_cast<float>(t.x);
            y = static_cast<float>(t.y);
            ang = (90.0f - static_cast<float>(t.angleDeg)) * 3.14159265f / 180.0f;
            return true;
        }
    }
    return false;
}

MapData loadMap(const WadFile& wad, const std::string& mapname) {
    int marker = wad.checkNumForName(mapname);
    if (marker < 0) I_Error(std::string("loadMap: map not found: ") + mapname);

    auto bytes = [&](int offset) -> std::vector<byte> {
        int idx = marker + offset;
        if (idx < 0 || idx >= wad.numLumps()) return {};
        if (wad.lumpSize(idx) <= 0) return {};
        return const_cast<WadFile&>(wad).readLump(idx);
    };

    MapData m;
    auto v = bytes(4);   // ML_VERTEXES
    auto l = bytes(2);   // ML_LINEDEFS
    m.vertices = v.empty() ? std::vector<vertex_t>{} : parseVertexes(v.data(), v.size());
    m.lines    = l.empty() ? std::vector<line_t>{}   : parseLinedefs(l.data(), l.size());
    auto sd = bytes(3); m.sides      = sd.empty() ? std::vector<side_t>{}      : parseSidedefs(sd.data(), sd.size());
    auto sg = bytes(5); m.segs       = sg.empty() ? std::vector<seg_t>{}       : parseSegs(sg.data(), sg.size());
    auto ss = bytes(6); m.subsectors = ss.empty() ? std::vector<subsector_t>{} : parseSubsectors(ss.data(), ss.size());
    auto nd = bytes(7); m.nodes      = nd.empty() ? std::vector<node_t>{}      : parseNodes(nd.data(), nd.size());
    auto sc = bytes(8); m.sectors    = sc.empty() ? std::vector<sector_t>{}    : parseSectors(sc.data(), sc.size());
    auto th = bytes(1); m.things     = th.empty() ? std::vector<thing_t>{}     : parseThings(th.data(), th.size());
    auto bm = bytes(10);  // ML_BLOCKMAP
    m.blockmap = bm.empty() ? Blockmap{} : parseBlockmap(bm.data(), bm.size());

    // Resolve each seg's front/back sector via linedef -> sidedef -> sector.
    for (auto& s : m.segs) {
        if (s.linedef < 0 || s.linedef >= static_cast<int>(m.lines.size())) continue;
        const auto& L = m.lines[s.linedef];
        int f = L.sidenum[s.side];
        int b = L.sidenum[s.side ^ 1];
        s.frontsector = (f >= 0 && f < static_cast<int>(m.sides.size())) ? m.sides[f].sector : -1;
        s.backsector  = (b >= 0 && b < static_cast<int>(m.sides.size())) ? m.sides[b].sector : -1;
    }
    return m;
}
